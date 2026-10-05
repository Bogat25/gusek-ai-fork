#define _WINSOCK_DEPRECATED_NO_WARNINGS 1
#define WIN32_LEAN_AND_MEAN 1
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <string>
#include <vector>

#include "../../scite/win32/ai/GusekAiDef.h"
#include "../../scite/win32/ai/GusekAiProtocol.h"
#include "../../scite/win32/ai/GusekAiConfig.h"
#include "../../scite/win32/ai/GusekAiDownload.h"

static int g_testsRun = 0;
static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_testsRun++; \
    if (cond) { \
        g_testsPassed++; \
        printf("  [PASS] %s\n", msg); \
    } else { \
        g_testsFailed++; \
        printf("  [FAIL] %s (line %d)\n", msg, __LINE__); \
    } \
} while(0)

// ---------------------------------------------------------------------
// 1. JSON Tests
// ---------------------------------------------------------------------
static void TestJsonEscaping() {
    printf("Testing JSON Escaping and Unescaping...\n");

    // Escaping
    std::string s1 = "Hello \"World\"\nLine 2\tTab\\Backslash";
    std::string esc1 = GusekAiProtocol::EscapeJsonString(s1);
    TEST_ASSERT(esc1 == "Hello \\\"World\\\"\\nLine 2\\tTab\\\\Backslash", "EscapeJsonString basic escaping");

    // Unescaping basic
    std::string unesc1 = GusekAiProtocol::UnescapeJsonString(esc1.c_str(), esc1.length());
    TEST_ASSERT(unesc1 == s1, "UnescapeJsonString roundtrip");

    // Unicode escape \u0041 ('A') and \u00e1 ('á' in UTF-8: \xC3\xA1)
    std::string escU = "Letter \\u0041 and \\u00e1";
    std::string unescU = GusekAiProtocol::UnescapeJsonString(escU.c_str(), escU.length());
    TEST_ASSERT(unescU == "Letter A and \xc3\xa1", "UnescapeJsonString 4-digit hex Unicode");

    // Surrogate pairs: \uD83D\uDE00 -> Grinning face emoji 😀 (\xF0\x9F\x98\x80)
    std::string escSurr = "Emoji \\uD83D\\uDE00 here";
    std::string unescSurr = GusekAiProtocol::UnescapeJsonString(escSurr.c_str(), escSurr.length());
    TEST_ASSERT(unescSurr == "Emoji \xf0\x9f\x98\x80 here", "UnescapeJsonString UTF-16 surrogate pairs");
}

// ---------------------------------------------------------------------
// 2. Think Tag Filter Tests
// ---------------------------------------------------------------------
static void TestThinkTagFilter() {
    printf("Testing ThinkTagFilter...\n");

    // Case 1: Simple complete tag in single chunk
    {
        ThinkTagFilter filter;
        std::string in = "<think>internal reasoning</think>Final answer.";
        std::string out = filter.FilterChunk(in.c_str(), in.length());
        out += filter.Flush();
        TEST_ASSERT(out == "Final answer.", "ThinkTagFilter single chunk");
    }

    // Case 2: Split opening tag across chunks: "<th" then "ink>reasoning</think>Final answer."
    {
        ThinkTagFilter filter;
        std::string c1 = "Start: <th";
        std::string out1 = filter.FilterChunk(c1.c_str(), c1.length());
        TEST_ASSERT(out1 == "Start: ", "ThinkTagFilter split opening tag chunk 1 withheld");

        std::string c2 = "ink>my thoughts</think>Real reply";
        std::string out2 = filter.FilterChunk(c2.c_str(), c2.length());
        out2 += filter.Flush();
        TEST_ASSERT(out2 == "Real reply", "ThinkTagFilter split opening tag chunk 2 parsed");
    }

    // Case 3: Split closing tag: "<think>reasoning</thi" then "nk>Done."
    {
        ThinkTagFilter filter;
        std::string c1 = "<think>reasoning</thi";
        std::string out1 = filter.FilterChunk(c1.c_str(), c1.length());
        TEST_ASSERT(out1.empty(), "ThinkTagFilter split closing tag chunk 1 inside think");

        std::string c2 = "nk>Done.";
        std::string out2 = filter.FilterChunk(c2.c_str(), c2.length());
        out2 += filter.Flush();
        TEST_ASSERT(out2 == "Done.", "ThinkTagFilter split closing tag chunk 2 closed");
    }

    // Case 4: Multiple think blocks
    {
        ThinkTagFilter filter;
        std::string in = "A<think>t1</think>B<think>t2</think>C";
        std::string out = filter.FilterChunk(in.c_str(), in.length());
        out += filter.Flush();
        TEST_ASSERT(out == "ABC", "ThinkTagFilter multiple think tags");
    }

    // Case 5: Unclosed think block flushed at EOF
    {
        ThinkTagFilter filter;
        std::string in = "Hello <th";
        std::string out1 = filter.FilterChunk(in.c_str(), in.length());
        std::string out2 = filter.Flush();
        TEST_ASSERT(out1 == "Hello " && out2 == "<th", "ThinkTagFilter unclosed prefix flushed");
    }
}

// ---------------------------------------------------------------------
// 3. Fenced Code Extraction Tests
// ---------------------------------------------------------------------
static void TestFencedCodeExtraction() {
    printf("Testing Fenced Code Extraction...\n");

    // Case 1: Single code block with language identifier
    {
        std::string md = "Here is the MathProg model:\n```gmpl\nvar x >= 0;\nmaximize obj: x;\ns.t. c1: x <= 10;\nsolve;\n```\nExplanation follows.";
        std::string code;
        bool ok = GusekAiProtocol::ExtractFencedCode(md, code);
        TEST_ASSERT(ok, "ExtractFencedCode found block");
        TEST_ASSERT(code.find("var x >= 0;") != std::string::npos, "ExtractFencedCode contains body");
        TEST_ASSERT(code.find("```") == std::string::npos, "ExtractFencedCode stripped fences");
    }

    // Case 2: Multiple code blocks joined by blank lines
    {
        std::string md = "Model part:\n```mod\nset I;\n```\nData part:\n```dat\ndata;\nset I := 1 2 3;\nend;\n```\nDone.";
        std::string code;
        bool ok = GusekAiProtocol::ExtractFencedCode(md, code);
        TEST_ASSERT(ok, "ExtractFencedCode found multiple blocks");
        TEST_ASSERT(code.find("set I;") != std::string::npos && code.find("set I := 1 2 3;") != std::string::npos, "ExtractFencedCode joined both blocks");
    }

    // Case 3: Incomplete / unclosed code block streaming
    {
        std::string md = "Streaming code:\n```mod\nvar y integer >= 0;\ns.t. bound: y <= 5;";
        std::string code;
        bool ok = GusekAiProtocol::ExtractFencedCode(md, code);
        TEST_ASSERT(ok, "ExtractFencedCode handles unclosed streaming block");
        TEST_ASSERT(code.find("var y integer >= 0;") != std::string::npos, "ExtractFencedCode extracted unclosed content");
    }

    // Case 4: Prose with no code fences
    {
        std::string md = "The objective function should be linear in decision variables.";
        std::string code;
        bool ok = GusekAiProtocol::ExtractFencedCode(md, code);
        TEST_ASSERT(!ok && code.empty(), "ExtractFencedCode returns false for plain prose");
    }
}

// ---------------------------------------------------------------------
// 4. SSE Delta Parsing Tests
// ---------------------------------------------------------------------
static void TestSseParsing() {
    printf("Testing SSE Parsing...\n");

    // Case 1: Standard content chunk
    {
        std::string sseLine = "data: {\"id\":\"chat-123\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Hello from model\"},\"finish_reason\":null}]}";
        std::string text;
        bool done = false;
        bool ok = GusekAiProtocol::ParseSseDelta(sseLine.c_str(), text, done);
        TEST_ASSERT(ok, "ParseSseDelta parsed valid line");
        TEST_ASSERT(text == "Hello from model", "ParseSseDelta extracted text content");
        TEST_ASSERT(!done, "ParseSseDelta done is false");
    }

    // Case 2: Role delta (content is null / absent)
    {
        std::string sseLine = "data: {\"id\":\"chat-123\",\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\"},\"finish_reason\":null}]}";
        std::string text;
        bool done = false;
        bool ok = GusekAiProtocol::ParseSseDelta(sseLine.c_str(), text, done);
        TEST_ASSERT(ok, "ParseSseDelta parsed role-only chunk");
        TEST_ASSERT(text.empty(), "ParseSseDelta text is empty for role-only chunk");
        TEST_ASSERT(!done, "ParseSseDelta done is false for role-only chunk");
    }

    // Case 3: [DONE] stream termination
    {
        std::string sseLine = "data: [DONE]";
        std::string text;
        bool done = false;
        bool ok = GusekAiProtocol::ParseSseDelta(sseLine.c_str(), text, done);
        TEST_ASSERT(ok, "ParseSseDelta handles [DONE]");
        TEST_ASSERT(done, "ParseSseDelta set done=true on [DONE]");
    }

    // Case 4: Non-data ping or comment
    {
        std::string sseLine = ": ping";
        std::string text;
        bool done = false;
        bool ok = GusekAiProtocol::ParseSseDelta(sseLine.c_str(), text, done);
        TEST_ASSERT(!ok, "ParseSseDelta ignores comments");
    }
}

// ---------------------------------------------------------------------
// 5. Config Defaults & Course Context Tests
// ---------------------------------------------------------------------
static void TestConfigAndCourseContext() {
    printf("Testing Config and Course Context ranking...\n");

    GusekAiConfig cfg;
    cfg.Load(""); // Loads defaults

    TEST_ASSERT(cfg.host == "127.0.0.1", "Config default host is 127.0.0.1");
    TEST_ASSERT(cfg.port == 28713, "Config default port is 28713");
    TEST_ASSERT(cfg.context_max_chars == 12000, "Config default context budget is 12000");
    TEST_ASSERT(cfg.keep_history == 12, "Config default history rounds is 12");
    TEST_ASSERT(cfg.n_predict == 1024, "Config default max tokens is 1024");

    // Test course context file scanning and ranking
    // Create temporary folder structure
    char tmpDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpDir);
    std::string testCtx = std::string(tmpDir) + "GusekAiTestCtx_" + std::to_string(GetTickCount());
    CreateDirectoryA(testCtx.c_str(), NULL);

    // Create a MathProg file
    std::string file1 = testCtx + "\\transp.mod";
    FILE *f1 = fopen(file1.c_str(), "w");
    if (f1) {
        fputs("/* Transportation problem formulation */\n"
              "set PLANTS;\nset MARKETS;\n"
              "param capacity{PLANTS};\nparam demand{MARKETS};\n"
              "var shipment{PLANTS, MARKETS} >= 0;\n"
              "minimize total_cost: sum{p in PLANTS, m in MARKETS} shipment[p,m];\n", f1);
        fclose(f1);
    }

    // Create a README file (which MUST be ignored according to rules)
    std::string fileReadme = testCtx + "\\README.txt";
    FILE *fr = fopen(fileReadme.c_str(), "w");
    if (fr) {
        fputs("This is the README for transportation problem with PLANTS and MARKETS.\n", fr);
        fclose(fr);
    }

    cfg.resolved_context_dir = testCtx;
    std::string ctxResult = cfg.GetCourseContext("How do I define shipment capacity and demand constraints?", "");
    TEST_ASSERT(ctxResult.find("transp.mod") != std::string::npos, "CourseContext includes transp.mod");
    TEST_ASSERT(ctxResult.find("README.txt") == std::string::npos, "CourseContext excludes README.txt");
    TEST_ASSERT(ctxResult.find("PLANTS") != std::string::npos, "CourseContext includes relevant excerpts");

    // Clean up temp files
    DeleteFileA(file1.c_str());
    DeleteFileA(fileReadme.c_str());
    RemoveDirectoryA(testCtx.c_str());
}

// ---------------------------------------------------------------------
// 6. Network Integration Tests (Against fakeserver.py)
// ---------------------------------------------------------------------
static void TestNetworkIntegration(int port) {
    printf("Testing Network Integration against 127.0.0.1:%d...\n", port);

    // 6.1 Health check over socket
    {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        TEST_ASSERT(s != INVALID_SOCKET, "Socket created for health check");
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((u_short)port);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        int c = connect(s, (sockaddr*)&addr, sizeof(addr));
        TEST_ASSERT(c == 0, "Connected to fake server for health check");

        DWORD tv = 5000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

        const char *req = "GET /health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
        send(s, req, (int)strlen(req), 0);

        char buf[1024];
        int n = recv(s, buf, sizeof(buf) - 1, 0);
        closesocket(s);
        if (n > 0) buf[n] = '\0'; else buf[0] = '\0';

        TEST_ASSERT(strstr(buf, "200 OK") != NULL, "Health check returned 200 OK");
        TEST_ASSERT(strstr(buf, "status") != NULL, "Health check body contains status");
    }

    // 6.2 Resumable download test via GusekAiDownload
    {
        char tmpDir[MAX_PATH];
        GetTempPathA(MAX_PATH, tmpDir);
        std::string destFile = std::string(tmpDir) + "gusek_test_dl.bin";
        DeleteFileA(destFile.c_str());

        std::string url = "http://127.0.0.1:" + std::to_string(port) + "/file/test.bin";
        std::string expectedSha = "8b3aad9576007a7f01736b5b0bde6e72405537988b5ef7d1c029bb42d540c45a";
        unsigned __int64 expectedSize = 1048576; // 1 MB
        std::string err;

        // Fresh download
        bool ok = GusekAiDownload::DownloadWithResume(url, destFile, expectedSha, expectedSize, NULL, NULL, NULL, err);
        TEST_ASSERT(ok, "DownloadWithResume succeeded on fresh download");
        TEST_ASSERT(GusekAiDownload::VerifyFileSha256(destFile, expectedSha), "VerifyFileSha256 verified downloaded file");

        // Test resume: truncate file to 500,000 bytes and resume
        HANDLE hFile = CreateFileA(destFile.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            SetFilePointer(hFile, 500000, NULL, FILE_BEGIN);
            SetEndOfFile(hFile);
            CloseHandle(hFile);
        }

        ok = GusekAiDownload::DownloadWithResume(url, destFile, expectedSha, expectedSize, NULL, NULL, NULL, err);
        TEST_ASSERT(ok, "DownloadWithResume resumed successfully");
        TEST_ASSERT(GusekAiDownload::VerifyFileSha256(destFile, expectedSha), "VerifyFileSha256 verified resumed file");

        DeleteFileA(destFile.c_str());
    }

    // 6.3 Chat completions streaming & think tag filtering over wire
    {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        TEST_ASSERT(s != INVALID_SOCKET, "Socket created for chat stream");
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((u_short)port);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        int c = connect(s, (sockaddr*)&addr, sizeof(addr));
        TEST_ASSERT(c == 0, "Connected for chat completions");

        DWORD tv = 5000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

        std::string body = "{\"messages\":[{\"role\":\"user\",\"content\":\"Explain MathProg model\"}],\"stream\":true}";
        std::string req = "POST /v1/chat/completions HTTP/1.1\r\n"
                          "Host: 127.0.0.1\r\n"
                          "Content-Type: application/json\r\n"
                          "Content-Length: " + std::to_string(body.length()) + "\r\n"
                          "Connection: close\r\n\r\n" + body;

        send(s, req.c_str(), (int)req.length(), 0);

        std::string rawData;
        char buf[2048];
        int n;
        while ((n = recv(s, buf, sizeof(buf), 0)) > 0) {
            rawData.append(buf, n);
        }
        closesocket(s);

        // Find end of HTTP headers
        size_t headEnd = rawData.find("\r\n\r\n");
        TEST_ASSERT(headEnd != std::string::npos, "Received HTTP headers in chat stream");

        std::string streamBody = rawData.substr(headEnd + 4);
        ThinkTagFilter thinkFilter;
        std::string fullAnswer;
        bool gotDone = false;

        // Process line by line as SSE
        size_t pos = 0;
        while (pos < streamBody.length()) {
            size_t nextNl = streamBody.find('\n', pos);
            if (nextNl == std::string::npos) break;
            std::string line = streamBody.substr(pos, nextNl - pos);
            pos = nextNl + 1;

            std::string textDelta;
            bool isDone = false;
            if (GusekAiProtocol::ParseSseDelta(line.c_str(), textDelta, isDone)) {
                if (isDone) gotDone = true;
                if (!textDelta.empty()) {
                    fullAnswer += thinkFilter.FilterChunk(textDelta.c_str(), textDelta.length());
                }
            }
        }
        fullAnswer += thinkFilter.Flush();

        TEST_ASSERT(gotDone, "Stream completed with [DONE]");
        TEST_ASSERT(fullAnswer.find("<think>") == std::string::npos, "No <think> tag leaked");
        TEST_ASSERT(fullAnswer.find("optimization model") != std::string::npos, "Answer contains model description");
        TEST_ASSERT(fullAnswer.find("var x >= 0;") != std::string::npos, "Answer contains code body");

        // Code extraction on full answer
        std::string code;
        bool hasCode = GusekAiProtocol::ExtractFencedCode(fullAnswer, code);
        TEST_ASSERT(hasCode, "ExtractFencedCode succeeded on streamed response");
        TEST_ASSERT(code.find("maximize obj: 5 * x;") != std::string::npos, "Extracted code has objective function");
    }
}

// ---------------------------------------------------------------------
// Main runner
// ---------------------------------------------------------------------
#include "production_checks.h"

int main(int argc, char **argv) {
    OleInitialize(NULL);
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    printf("============================================================\n");
    printf(" GUSEK AI Assistant - Native Unit & Protocol Test Suite\n");
    printf("============================================================\n\n");

    TestJsonEscaping();
    printf("\n");
    TestThinkTagFilter();
    printf("\n");
    TestFencedCodeExtraction();
    printf("\n");
    TestSseParsing();
    printf("\n");
    TestConfigAndCourseContext();
    if (argc > 2) TestProduction(Utf8ToWide(argv[2]));

    if (argc > 1) {
        int port = atoi(argv[1]);
        if (port > 0) {
            printf("\n");
            TestNetworkIntegration(port);
        }
    }

    printf("\n============================================================\n");
    printf(" Summary: %d tests, %d passed, %d failed\n", g_testsRun, g_testsPassed, g_testsFailed);
    printf("============================================================\n");

    WSACleanup();
    OleUninitialize();
    return (g_testsFailed == 0) ? 0 : 1;
}

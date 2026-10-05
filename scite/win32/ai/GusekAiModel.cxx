#include "GusekAiModel.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

GusekAiModel::GusekAiModel()
    : m_hJob(NULL), m_hProcess(NULL), m_dwPid(0), m_isOwnedProcess(false)
{
}

GusekAiModel::~GusekAiModel() {
    Stop();
}

bool GusekAiModel::IsOwnedProcessRunning() const {
    if (!m_hProcess) return false;
    DWORD exitCode = 0;
    if (GetExitCodeProcess(m_hProcess, &exitCode)) {
        return (exitCode == STILL_ACTIVE);
    }
    return false;
}

void GusekAiModel::Stop() {
    if (m_hProcess) {
        if (IsOwnedProcessRunning()) {
            TerminateProcess(m_hProcess, 0);
            WaitForSingleObject(m_hProcess, 1000);
        }
        CloseHandle(m_hProcess);
        m_hProcess = NULL;
    }
    if (m_hJob) {
        CloseHandle(m_hJob);
        m_hJob = NULL;
    }
    m_dwPid = 0;
    m_isOwnedProcess = false;
}

bool GusekAiModel::CheckHealth(const std::string &host, int port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    // Set non-blocking or timeout
    DWORD timeout = 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = inet_addr(host.c_str());

    bool ready = false;
    if (connect(s, (sockaddr *)&addr, sizeof(addr)) == 0) {
        std::string req = "GET /health HTTP/1.1\r\nHost: " + host + ":" + std::to_string(port) + "\r\nConnection: close\r\n\r\n";
        send(s, req.c_str(), (int)req.length(), 0);

        char buf[512] = {0};
        int received = recv(s, buf, sizeof(buf) - 1, 0);
        if (received > 0) {
            buf[received] = '\0';
            if (strstr(buf, "200 OK") || strstr(buf, "HTTP/1.1 200") || strstr(buf, "HTTP/1.0 200")) {
                ready = true;
            }
        }
    }

    closesocket(s);
    WSACleanup();
    return ready;
}

static std::string ExtractLastLogError(const std::string &logPath) {
    std::ifstream in(logPath.c_str());
    if (!in.is_open()) return "";
    std::string line;
    std::string lastErr;
    while (std::getline(in, line)) {
        std::string lower = line;
        for (size_t i = 0; i < lower.length(); i++) {
            if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] += ('a' - 'A');
        }
        if (lower.find("error") != std::string::npos || lower.find("failed") != std::string::npos ||
            lower.find("exception") != std::string::npos || lower.find("fatal") != std::string::npos) {
            lastErr = line;
        }
    }
    return lastErr;
}

bool GusekAiModel::EnsureRunning(const GusekAiConfig &config, std::string &outError) {
    outError.clear();

    // 1. If server is already answering /health, use it
    if (CheckHealth(config.host, config.port)) {
        return true;
    }

    // 2. Check if files exist
    DWORD srvAttr = GetFileAttributesA(config.resolved_server_exe.c_str());
    if (srvAttr == INVALID_FILE_ATTRIBUTES || (srvAttr & FILE_ATTRIBUTE_DIRECTORY)) {
        outError = "llama-server executable not found at: " + config.resolved_server_exe;
        return false;
    }

    DWORD modAttr = GetFileAttributesA(config.resolved_model.c_str());
    if (modAttr == INVALID_FILE_ATTRIBUTES || (modAttr & FILE_ATTRIBUTE_DIRECTORY)) {
        outError = "Model file not found. First-run download required.";
        return false;
    }

    // 3. Setup Job Object
    if (!m_hJob) {
        m_hJob = CreateJobObject(NULL, NULL);
        if (m_hJob) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
            memset(&li, 0, sizeof(li));
            li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(m_hJob, JobObjectExtendedLimitInformation, &li, sizeof(li));
        }
    }

    // 4. Build command line
    std::string cmd = "\"" + config.resolved_server_exe + "\"";
    cmd += " -m \"" + config.resolved_model + "\"";
    cmd += " --host " + config.host;
    cmd += " --port " + std::to_string(config.port);
    cmd += " -c " + std::to_string(config.ctx_size);
    cmd += " -ngl " + std::to_string(config.gpu_layers);
    if (config.threads > 0) {
        cmd += " -t " + std::to_string(config.threads);
    }

    if (config.vision) {
        DWORD vAttr = GetFileAttributesA(config.resolved_vision_model.c_str());
        if (vAttr != INVALID_FILE_ATTRIBUTES && !(vAttr & FILE_ATTRIBUTE_DIRECTORY)) {
            cmd += " --mmproj \"" + config.resolved_vision_model + "\"";
            cmd += " --image-max-tokens " + std::to_string(config.image_max_tokens);
        }
    }

    if (!config.extra_args.empty()) {
        cmd += " " + config.extra_args;
    }

    // 5. Open log file for redirection
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hLog = CreateFileA(config.resolved_log_file.c_str(),
                              GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              &sa,
                              CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              NULL);

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (hLog != INVALID_HANDLE_VALUE) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = hLog;
        si.hStdError  = hLog;
        si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    }

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    // Working directory is the folder containing llama-server.exe so DLLs resolve
    std::string workDir = config.resolved_server_exe;
    size_t lastSlash = workDir.find_last_of("/\\");
    if (lastSlash != std::string::npos) workDir = workDir.substr(0, lastSlash);

    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back('\0');

    BOOL created = CreateProcessA(
        NULL,
        &cmdBuf[0],
        NULL,
        NULL,
        TRUE, // inherit handles for log
        CREATE_SUSPENDED | CREATE_NO_WINDOW,
        NULL,
        workDir.c_str(),
        &si,
        &pi);

    if (hLog != INVALID_HANDLE_VALUE) {
        CloseHandle(hLog);
    }

    if (!created) {
        outError = "Failed to launch llama-server (Windows error " + std::to_string(GetLastError()) + ")";
        return false;
    }

    if (m_hJob) {
        AssignProcessToJobObject(m_hJob, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    m_hProcess = pi.hProcess;
    m_dwPid = pi.dwProcessId;
    m_isOwnedProcess = true;

    // 6. Poll /health until ready or timeout
    int maxPolls = config.startup_timeout * 2;
    for (int i = 0; i < maxPolls; i++) {
        if (!IsOwnedProcessRunning()) {
            std::string lastErr = ExtractLastLogError(config.resolved_log_file);
            if (!lastErr.empty()) {
                outError = "Server exited: " + lastErr;
            } else {
                outError = "Server process exited unexpectedly. See log: " + config.resolved_log_file;
            }
            Stop();
            return false;
        }

        if (CheckHealth(config.host, config.port)) {
            return true;
        }
        Sleep(500);
    }

    outError = "Model server startup timed out after " + std::to_string(config.startup_timeout) + "s";
    return false;
}

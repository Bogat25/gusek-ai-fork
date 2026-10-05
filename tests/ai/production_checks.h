#include "../../scite/win32/ai/GusekAiImage.h"
#include "../../scite/win32/ai/GusekAiRender.h"
#include <richole.h>
#include <gdiplus.h>
#include <fstream>

static bool TestEncoder(const wchar_t *mime, CLSID &id) {
    UINT count = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&count, &size);
    if (!size) return false;
    std::vector<BYTE> memory(size);
    Gdiplus::ImageCodecInfo *codecs = reinterpret_cast<Gdiplus::ImageCodecInfo *>(&memory[0]);
    Gdiplus::GetImageEncoders(count, size, codecs);
    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(codecs[i].MimeType, mime) == 0) { id = codecs[i].Clsid; return true; }
    }
    return false;
}

static DWORD CALLBACK CollectTestRtf(DWORD_PTR context, LPBYTE bytes, LONG size, LONG *written) {
    reinterpret_cast<std::string *>(context)->append(reinterpret_cast<char *>(bytes), size);
    *written = size;
    return 0;
}

static void TestProduction(const std::wstring &root) {
    printf("Testing production configuration, pictures and actual RichEdit...\n");
    GusekAiConfig config;
    std::vector<ChatMessage> history;
    ChatMessage old, newer;
    old.role = newer.role = "user";
    old.content = "old turn"; newer.content = "new turn";
    old.imageDataUrls.push_back("OLD_A"); old.imageDataUrls.push_back("OLD_B");
    newer.imageDataUrls.push_back("NEW_A"); newer.imageDataUrls.push_back("NEW_B");
    history.push_back(old); history.push_back(newer);
    std::vector<std::string> current;
    current.push_back("CURRENT_A"); current.push_back("CURRENT_B");
    std::string request = GusekAiProtocol::BuildChatRequestJson("test", history, "question", current, config);
    TEST_ASSERT(request.find("OLD_A") == std::string::npos && request.find("OLD_B") == std::string::npos,
                "Picture budget drops the oldest history pictures");
    TEST_ASSERT(request.find("NEW_A") != std::string::npos && request.find("CURRENT_B") != std::string::npos,
                "Picture budget retains the four newest pictures");
    TEST_ASSERT(request.find("A picture was attached here") != std::string::npos,
                "Dropped pictures leave a history marker");
    for (int i = 0; i < 5; ++i) current.push_back("EXTRA_" + std::to_string(i));
    request = GusekAiProtocol::BuildChatRequestJson("test", history, "", current, config);
    TEST_ASSERT(request.find("EXTRA_0") == std::string::npos && request.find("EXTRA_4") != std::string::npos &&
                request.find("CURRENT_A") == std::string::npos, "Current turn keeps its newest four pictures");
    config.thinking = true;
    request = GusekAiProtocol::BuildChatRequestJson("test", history, "", current, config);
    TEST_ASSERT(request.find("\"enable_thinking\":true") != std::string::npos, "Thinking setting reaches the request");
    std::string delta;
    bool done = false;
    TEST_ASSERT(GusekAiProtocol::ParseSseDelta("data: {\"choices\":[{\"delta\":{\"content\" : \"spaced\"}}]}", delta, done) &&
                delta == "spaced", "SSE accepts legal whitespace around the content separator");
    TEST_ASSERT(GusekAiProtocol::UnescapeJsonString("\\uD800", 6) == "\xef\xbf\xbd",
                "An unpaired surrogate cannot produce invalid UTF-8");

    wchar_t profile[32768] = {};
    GetEnvironmentVariableW(L"GUSEK_AI_DATA", profile, 32768);
    std::wstring ini = std::wstring(profile) + L"\\GusekAI.ini";
    {
        std::ofstream out(ini.c_str(), std::ios::binary);
        out << "\xef\xbb\xbfhost=192.0.2.1\nport=-1\nkeep_history=-1\ncontext_max_chars=-1\nn_predict=-1\nctx_size=-1\n";
    }
    config.Load("");
    TEST_ASSERT(config.host == "127.0.0.1" && config.port == 28713,
                "Invalid remote endpoint falls back to the dedicated loopback endpoint");
    TEST_ASSERT(config.keep_history == 0 && config.context_max_chars == 0 && config.n_predict == 1 && config.ctx_size == 512,
                "Invalid resource settings cannot wrap into unbounded allocations");
    DeleteFileW(ini.c_str());
    std::wstring prompt = std::wstring(profile) + L"\\system_prompt.txt";
    { std::ofstream out(prompt.c_str(), std::ios::binary); out << "USER_PROMPT_SENTINEL"; }
    config.Load("");
    TEST_ASSERT(config.GetSystemPrompt() == "USER_PROMPT_SENTINEL", "Editable per-user prompt is actually used");
    DeleteFileW(prompt.c_str());

    std::wstring notes = root + L"\\course-\x8bfe\x7a0b";
    CreateDirectoryW(notes.c_str(), NULL);
    { std::ofstream out((notes + L"\\lesson.txt").c_str(), std::ios::binary); out << std::string(8000, 'x'); }
    { std::ofstream out((notes + L"\\README.md").c_str(), std::ios::binary); out << "DO_NOT_INCLUDE_README"; }
    { std::ofstream out((notes + L"\\oversized.txt").c_str(), std::ios::binary); out << std::string(262145, 'z'); }
    config.resolved_context_dir = WideToUtf8(notes);
    config.context_max_chars = 1000;
    std::string context = config.GetCourseContext("lesson", "");
    TEST_ASSERT(!context.empty() && context.size() <= 1000, "Course excerpt including headers respects the total budget");
    TEST_ASSERT(context.find("DO_NOT_INCLUDE_README") == std::string::npos && context.find("oversized.txt") == std::string::npos,
                "Course search skips README scaffolding and oversized inputs");
    { std::ofstream out((notes + L"\\lesson.txt").c_str(), std::ios::binary); out << "LIVE_RELOAD_SENTINEL"; }
    TEST_ASSERT(config.GetCourseContext("lesson", "").find("LIVE_RELOAD_SENTINEL") != std::string::npos,
                "Course changes are reloaded without restarting the application");

    GusekAiImage::Initialize();
    Gdiplus::Bitmap fixture(2400, 600, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&fixture);
    graphics.Clear(Gdiplus::Color::White);
    Gdiplus::Font font(L"Arial", 260, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush brush(Gdiplus::Color::Black);
    graphics.DrawString(L"42", -1, &font, Gdiplus::PointF(700, 100), &brush);
    const wchar_t *formats[] = { L"image/png", L"image/jpeg", L"image/bmp", L"image/gif", L"image/tiff" };
    const wchar_t *extensions[] = { L"png", L"jpg", L"bmp", L"gif", L"tif" };
    ChatImageAttachment png;
    for (int i = 0; i < 5; ++i) {
        CLSID encoder;
        std::wstring file = root + L"\\picture-42." + extensions[i];
        bool saved = TestEncoder(formats[i], encoder) && fixture.Save(file.c_str(), &encoder, NULL) == Gdiplus::Ok;
        ChatImageAttachment image;
        bool loaded = saved && GusekAiImage::LoadImageFromFile(file, image);
        TEST_ASSERT(loaded && image.base64Png.find("data:image/") == 0, "Supported picture format normalizes successfully");
        BITMAP dimensions = {};
        bool sized = loaded && GetObject(image.hPreview, sizeof(dimensions), &dimensions) != 0;
        TEST_ASSERT(sized && dimensions.bmWidth == 1600 && dimensions.bmHeight == 400,
                    "Picture normalization respects size and aspect ratio");
        if (i == 0) png = image; else GusekAiImage::Release(image);
    }
    {
        std::ifstream input((root + L"\\picture-42.jpg").c_str(), std::ios::binary);
        std::string jpeg((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        const BYTE exif[] = { 0xff,0xe1,0,34,'E','x','i','f',0,0,'I','I',42,0,8,0,0,0,
                             1,0,0x12,1,3,0,1,0,0,0,6,0,0,0,0,0,0,0 };
        std::wstring path = root + L"\\oriented-\x6d4b\x8bd5.jpg";
        {
            std::ofstream output(path.c_str(), std::ios::binary);
            output.write(jpeg.data(), 2);
            output.write(reinterpret_cast<const char *>(exif), sizeof(exif));
            output.write(jpeg.data() + 2, jpeg.size() - 2);
        }
        ChatImageAttachment oriented;
        BITMAP dimensions = {};
        bool loaded = GusekAiImage::LoadImageFromFile(path, oriented);
        TEST_ASSERT(loaded && GetObject(oriented.hPreview, sizeof(dimensions), &dimensions) &&
                    dimensions.bmWidth == 400 && dimensions.bmHeight == 1600,
                    "EXIF orientation and a Unicode picture filename normalize correctly");
        GusekAiImage::Release(oriented);
    }
    {
        Gdiplus::Bitmap noise(1600, 1600, PixelFormat32bppARGB);
        Gdiplus::Rect rect(0, 0, 1600, 1600);
        Gdiplus::BitmapData pixels = {};
        bool generated = noise.LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &pixels) == Gdiplus::Ok;
        if (generated) {
            unsigned int random = 123456789;
            for (int y = 0; y < 1600; ++y) {
                DWORD *row = reinterpret_cast<DWORD *>(static_cast<BYTE *>(pixels.Scan0) + y * pixels.Stride);
                for (int x = 0; x < 1600; ++x) {
                    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
                    row[x] = 0xff000000 | (random & 0xffffff);
                }
            }
            noise.UnlockBits(&pixels);
        }
        CLSID encoder;
        std::wstring path = root + L"\\large-picture.png";
        ChatImageAttachment image;
        bool loaded = generated && TestEncoder(L"image/png", encoder) &&
            noise.Save(path.c_str(), &encoder, NULL) == Gdiplus::Ok && GusekAiImage::LoadImageFromFile(path, image);
        TEST_ASSERT(loaded && image.base64Png.find("data:image/jpeg;base64,") == 0,
                    "A large encoded PNG falls back to a smaller correctly labeled JPEG");
        GusekAiImage::Release(image);
    }

    HMODULE edit = LoadLibraryW(L"msftedit.dll");
    HWND parent = CreateWindowExW(0, L"STATIC", L"Isolated test owner", WS_OVERLAPPEDWINDOW,
        0, 0, 400, 300, NULL, NULL, GetModuleHandle(NULL), NULL);
    HWND rich = CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | ES_MULTILINE | WS_VSCROLL,
        0, 0, 350, 220, parent, NULL, GetModuleHandle(NULL), NULL);
    TEST_ASSERT(edit && rich, "Actual Unicode RichEdit test control is created");
    if (rich) {
        GusekAiRender::SetupRichEdit(rich);
        GusekAiRender::AppendImageThumbnail(rich, png.name, png.hThumb);
        std::string pictureRtf;
        EDITSTREAM pictureStream = { reinterpret_cast<DWORD_PTR>(&pictureRtf), 0, CollectTestRtf };
        SendMessage(rich, EM_STREAMOUT, SF_RTF, reinterpret_cast<LPARAM>(&pictureStream));
        TEST_ASSERT(pictureStream.dwError == 0 && pictureRtf.find("\\pict") != std::string::npos &&
                    pictureRtf.find("89504e47") != std::string::npos,
                    "Transcript retains an embedded PNG rather than just an attachment label");
        SetWindowTextW(rich, L"");
        GusekAiRender::RenderMarkdownStream(rich, "# Heading\n2 * 3 and **bold**\n- item\n---\n```mod\nvar x;\n```", 0, true);
        wchar_t text[1024] = {};
        GetWindowTextW(rich, text, 1024);
        std::wstring rendered = text;
        TEST_ASSERT(rendered.find(L"2 * 3") != std::wstring::npos && rendered.find(L"```") == std::wstring::npos &&
                    rendered.find(L"---") == std::wstring::npos && rendered.find(L"\x2022") != std::wstring::npos,
                    "Markdown renders arithmetic, fences, headings, bullets and horizontal rules correctly");
        GusekAiRender::RenderMarkdownStream(rich, "prose \xf0\x9f\x98\x80 prose", 0, true);
        CHARRANGE emojiSelection = { 6, 8 };
        SendMessage(rich, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&emojiSelection));
        CHARFORMAT2W emojiFormat = {};
        emojiFormat.cbSize = sizeof(emojiFormat);
        SendMessageW(rich, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&emojiFormat));
        TEST_ASSERT(wcscmp(emojiFormat.szFaceName, L"Segoe UI Emoji") == 0,
                    "Actual RichEdit selects an explicit emoji font for emoji characters");
        emojiSelection.cpMin = 0; emojiSelection.cpMax = 5;
        SendMessage(rich, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&emojiSelection));
        SendMessageW(rich, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&emojiFormat));
        TEST_ASSERT(wcscmp(emojiFormat.szFaceName, L"Segoe UI") == 0, "Emoji font selection leaves surrounding prose unchanged");
        GusekAiRender::RenderMarkdownStream(rich, "# Heading\n2 * 3 and **bold**\n- item", 0, true);
        CHARRANGE selection = { 12, 17 };
        SendMessage(rich, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
        GusekAiRender::RenderMarkdownStream(rich, "# Heading\n2 * 3 and **bold**\n- item\nMore streamed text", 0, false);
        CHARRANGE restored;
        SendMessage(rich, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&restored));
        TEST_ASSERT(restored.cpMin == selection.cpMin && restored.cpMax == selection.cpMax,
                    "A selection within the current answer survives streaming");
        std::string longAnswer;
        for (int i = 0; i < 200; ++i) longAnswer += "Line " + std::to_string(i) + "\n";
        GusekAiRender::RenderMarkdownStream(rich, longAnswer, 0, true);
        SendMessage(rich, EM_SETSEL, 0, 0);
        SendMessage(rich, EM_LINESCROLL, 0, -100);
        POINT scrollBefore = {}, scrollAfter = {};
        SendMessage(rich, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scrollBefore));
        GusekAiRender::RenderMarkdownStream(rich, longAnswer + "More text\n", 0, false);
        SendMessage(rich, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scrollAfter));
        TEST_ASSERT(scrollBefore.y > 0 && scrollBefore.y == scrollAfter.y,
                    "Scrolling upward in a long answer remains stable as text arrives");
        GusekAiImage::ShowImageViewer(parent, png);
        HWND viewer = FindWindowW(L"GusekAiImageViewer", NULL);
        HBITMAP bitmap = viewer ? reinterpret_cast<HBITMAP>(GetWindowLongPtr(viewer, GWLP_USERDATA)) : NULL;
        GusekAiImage::Release(png);
        BITMAP dimensions;
        TEST_ASSERT(bitmap && GetObject(bitmap, sizeof(dimensions), &dimensions) && dimensions.bmWidth == 1600,
                    "Viewer owns a full-size picture after attachment removal");
        if (viewer) SendMessage(viewer, WM_KEYDOWN, VK_ESCAPE, 0);
        TEST_ASSERT(!IsWindow(viewer), "Escape closes the picture viewer");
    }
    GusekAiImage::Release(png);
    DestroyWindow(parent);
    if (edit) FreeLibrary(edit);
}

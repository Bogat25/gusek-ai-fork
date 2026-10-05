#include "GusekAiPane.h"
#include "GusekAiHttp.h"
#include <commdlg.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <uxtheme.h>

#pragma comment(lib, "comctl32.lib")

static const wchar_t PANE_CLASS_NAME[] = L"GusekAiPaneClass";

struct AiWorkerEvent {
    LONG generation;
    bool success;
    std::string text;
};

GusekAiPane::GusekAiPane(IGusekAiHost *pHost)
    : m_pHost(pHost),
      m_hWnd(NULL), m_hStatus(NULL), m_hHist(NULL), m_hInput(NULL),
      m_btnSend(NULL), m_btnStop(NULL), m_btnCopy(NULL), m_btnToEd(NULL),
      m_btnNew(NULL), m_btnAttach(NULL),
      m_hFontUi(NULL), m_hMsftEdit(NULL),
      m_busy(false), m_cancel(0), m_downloading(0), m_mouseDragging(false),
      m_currentTurnStartPos(0), m_generation(0),
      m_cancelEvent(CreateEvent(NULL, TRUE, FALSE, NULL)), m_postPending(0),
      m_hWorkerThread(NULL), m_hDownloadThread(NULL)
{
    InitializeCriticalSection(&m_cs);
    GusekAiImage::Initialize();
}

GusekAiPane::~GusekAiPane() {
    Destroy();
    GusekAiImage::Shutdown();
    if (m_cancelEvent) CloseHandle(m_cancelEvent);
    DeleteCriticalSection(&m_cs);
}

void GusekAiPane::OnHostQuit() {
    StopWork();
    m_model.Stop();
}

bool GusekAiPane::Create(HWND hParent, int x, int y, int width, int height) {
    if (m_hWnd) return true;

    if (!m_hMsftEdit) {
        m_hMsftEdit = LoadLibraryW(L"msftedit.dll");
    }

    // Load config
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::string appDir = WideToUtf8(exePath);
    size_t lastSlash = appDir.find_last_of("/\\");
    if (lastSlash != std::string::npos) appDir = appDir.substr(0, lastSlash);
    m_config.Load(appDir);

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = PANE_CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);

    m_hWnd = CreateWindowExW(
        0, PANE_CLASS_NAME, L"AI Assistant",
        WS_CHILD | WS_CLIPCHILDREN,
        x, y, width, height,
        hParent, NULL, GetModuleHandle(NULL), this);

    if (!m_hWnd) return false;

    // UI Font
    m_hFontUi = CreateFontW(
        -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    // 1. Status label
    m_hStatus = CreateWindowExW(
        0, L"STATIC", L"Ready",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
        0, 0, width, 20, m_hWnd, (HMENU)IDC_AI_STATUS, GetModuleHandle(NULL), NULL);
    SendMessage(m_hStatus, WM_SETFONT, (WPARAM)m_hFontUi, TRUE);

    // 2. Transcript RichEdit
    m_hHist = CreateWindowExW(
        WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, width, height - 160, m_hWnd, (HMENU)IDC_AI_HIST, GetModuleHandle(NULL), NULL);
    GusekAiRender::SetupRichEdit(m_hHist);
    SetWindowSubclass(m_hHist, HistSubclassProc, 1, (DWORD_PTR)this);

    // Initial welcome message
    GusekAiRender::AppendPlainText(m_hHist,
        "Local MathProg & GLPK Assistant.\r\n"
        "Ask questions about optimization models, GMPL syntax or solver errors.\r\n"
        "Use Attach to include the current model, last solver error or screenshots.\r\n"
        "Copy code or To editor inserts code into the editor. Nothing is run automatically.\r\n\r\n");

    // 3. Question box
    m_hInput = CreateWindowExW(
        WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP,
        0, 0, width, 80, m_hWnd, (HMENU)IDC_AI_INPUT, GetModuleHandle(NULL), NULL);
    GusekAiRender::SetupRichEdit(m_hInput);
    SetWindowSubclass(m_hInput, InputSubclassProc, 2, (DWORD_PTR)this);

    // 4. Buttons row
    m_btnSend = CreateWindowExW(0, L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 60, 24, m_hWnd, (HMENU)IDC_AI_SEND, GetModuleHandle(NULL), NULL);
    m_btnStop = CreateWindowExW(0, L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 50, 24, m_hWnd, (HMENU)IDC_AI_STOP, GetModuleHandle(NULL), NULL);
    m_btnCopy = CreateWindowExW(0, L"BUTTON", L"Copy code", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 75, 24, m_hWnd, (HMENU)IDC_AI_COPY, GetModuleHandle(NULL), NULL);
    m_btnToEd = CreateWindowExW(0, L"BUTTON", L"To editor", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 70, 24, m_hWnd, (HMENU)IDC_AI_TOED, GetModuleHandle(NULL), NULL);
    m_btnNew  = CreateWindowExW(0, L"BUTTON", L"New chat", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 70, 24, m_hWnd, (HMENU)IDC_AI_NEW, GetModuleHandle(NULL), NULL);
    m_btnAttach = CreateWindowExW(0, L"BUTTON", L"Attach...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 65, 24, m_hWnd, (HMENU)IDC_AI_ATTACH_BTN, GetModuleHandle(NULL), NULL);

    EnableWindow(m_btnStop, FALSE);

    HWND btns[] = { m_btnSend, m_btnStop, m_btnCopy, m_btnToEd, m_btnNew, m_btnAttach };
    for (int i = 0; i < 6; i++) {
        SendMessage(btns[i], WM_SETFONT, (WPARAM)m_hFontUi, TRUE);
    }

    LayoutChildren(width, height);
    return true;
}

void GusekAiPane::Destroy() {
    OnHostQuit();
    ClearAttachedImages();
    if (m_hWnd) {
        DestroyWindow(m_hWnd);
        m_hWnd = NULL;
    }
    if (m_hFontUi) {
        DeleteObject(m_hFontUi);
        m_hFontUi = NULL;
    }
    if (m_hMsftEdit) {
        FreeLibrary(m_hMsftEdit);
        m_hMsftEdit = NULL;
    }
}

void GusekAiPane::SetPosition(int x, int y, int width, int height) {
    if (m_hWnd) {
        SetWindowPos(m_hWnd, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutChildren(width, height);
    }
}

void GusekAiPane::Show(bool bShow) {
    if (m_hWnd) {
        ShowWindow(m_hWnd, bShow ? SW_SHOW : SW_HIDE);
        if (bShow) {
            SetFocus(m_hInput);
            if (m_config.autostart && !m_model.CheckHealth(m_config.host, m_config.port)) {
                // Background warm-up
                std::string err;
                // Will start when user asks or during first query
            }
        }
    }
}

bool GusekAiPane::IsVisible() const {
    return (m_hWnd && IsWindowVisible(m_hWnd));
}

void GusekAiPane::SetStatus(const std::string &status) {
    if (m_hStatus) {
        std::wstring w = Utf8ToWide(status);
        SetWindowTextW(m_hStatus, w.c_str());
    }
}

void GusekAiPane::LayoutChildren(int width, int height) {
    if (!m_hWnd || width <= 0 || height <= 0) return;

    int pad = 4;
    int statusH = 20;
    int btnH = 26;
    int inputH = 80;

    // Status at top
    SetWindowPos(m_hStatus, NULL, pad, pad, width - pad * 2, statusH, SWP_NOZORDER);

    // Buttons at bottom
    int btnY = height - btnH - pad;
    int bx = pad;
    HWND btns[] = { m_btnSend, m_btnStop, m_btnAttach, m_btnCopy, m_btnToEd, m_btnNew };
    int widths[] = { 50, 45, 60, 68, 62, 65 };

    for (int i = 0; i < 6; i++) {
        SetWindowPos(btns[i], NULL, bx, btnY, widths[i], btnH, SWP_NOZORDER);
        bx += widths[i] + 3;
    }

    // Input above buttons
    int inputY = btnY - inputH - pad;
    SetWindowPos(m_hInput, NULL, pad, inputY, width - pad * 2, inputH, SWP_NOZORDER);

    // Transcript takes the rest
    int histY = statusH + pad * 2;
    int histH = inputY - histY - pad;
    if (histH < 40) histH = 40;
    SetWindowPos(m_hHist, NULL, pad, histY, width - pad * 2, histH, SWP_NOZORDER);
}

void GusekAiPane::AppendTextToInput(const std::string &text) {
    if (!m_hInput || text.empty()) return;
    GETTEXTLENGTHEX gtl = { GTL_NUMCHARS | GTL_PRECISE, 1200 };
    LONG curLen = (LONG)SendMessage(m_hInput, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);

    CHARRANGE crEnd = { curLen, curLen };
    SendMessage(m_hInput, EM_EXSETSEL, 0, (LPARAM)&crEnd);

    std::wstring w = Utf8ToWide(text);
    SendMessageW(m_hInput, EM_REPLACESEL, TRUE, (LPARAM)w.c_str());
    SetFocus(m_hInput);
}

void GusekAiPane::ShowAttachMenu() {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_AI_ATTACH_ERR, L"Last solver error");
    AppendMenuW(hMenu, MF_STRING, IDM_AI_ATTACH_OUT, L"Last solver output");
    AppendMenuW(hMenu, MF_STRING, IDM_AI_ATTACH_DOC, L"Current model / document");
    AppendMenuW(hMenu, MF_STRING, IDM_AI_ATTACH_RECENT, L"Recent console output");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_AI_ATTACH_FILE, L"Picture file...");
    AppendMenuW(hMenu, MF_STRING, IDM_AI_ATTACH_CLIP, L"Picture from clipboard");
    AppendMenuW(hMenu, MF_STRING, IDM_AI_ATTACH_CLEAR, L"Remove attached pictures");

    RECT rc;
    GetWindowRect(m_btnAttach, &rc);
    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_BOTTOMALIGN, rc.left, rc.top, 0, m_hWnd, NULL);
    DestroyMenu(hMenu);
}

void GusekAiPane::AttachImageFile() {
    WCHAR szFile[MAX_PATH] = L"";
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hWnd;
    ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileNameW(&ofn)) {
        ChatImageAttachment att;
        if (GusekAiImage::LoadImageFromFile(szFile, att)) {
            m_attachedImages.push_back(att);
            SetStatus("Attached picture: " + att.name);
        } else {
            SetStatus("Failed to load picture file");
        }
    }
}

void GusekAiPane::AttachClipboardImage() {
    ChatImageAttachment att;
    if (GusekAiImage::LoadImageFromClipboard(m_hWnd, att)) {
        m_attachedImages.push_back(att);
        SetStatus("Attached image from clipboard");
    } else {
        SetStatus("No picture found on clipboard");
    }
}

void GusekAiPane::ClearAttachedImages() {
    for (size_t i = 0; i < m_attachedImages.size(); ++i) {
        if (m_attachedImages[i].hThumb) DeleteObject(m_attachedImages[i].hThumb);
    }
    m_attachedImages.clear();
    SetStatus("Attached pictures removed");
}

void GusekAiPane::CopyLastCode() {
    if (m_lastAnswer.empty()) {
        SetStatus("No answer available to copy");
        return;
    }
    std::string code;
    if (GusekAiProtocol::ExtractFencedCode(m_lastAnswer, code) && !code.empty()) {
        std::wstring wCode = Utf8ToWide(code);
        if (OpenClipboard(m_hWnd)) {
            EmptyClipboard();
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (wCode.length() + 1) * sizeof(wchar_t));
            if (hMem) {
                wchar_t *pDest = (wchar_t *)GlobalLock(hMem);
                wcscpy(pDest, wCode.c_str());
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
            CloseClipboard();
            SetStatus("Code copied to clipboard");
        }
    } else {
        SetStatus("No code block found in last answer");
    }
}

void GusekAiPane::InsertCodeToEditor() {
    if (m_lastAnswer.empty()) {
        SetStatus("No answer available to insert");
        return;
    }
    std::string code;
    if (GusekAiProtocol::ExtractFencedCode(m_lastAnswer, code) && !code.empty()) {
        if (m_pHost->InsertTextAtCaret(code.c_str())) {
            SetStatus("Code inserted to editor");
        } else if (m_pHost->CreateNewMathProgDocument(code.c_str())) {
            SetStatus("Code opened in a new UTF-8 document");
        } else {
            SetStatus("Code could not be inserted");
        }
    } else {
        SetStatus("No code block found in last answer");
    }
}

void GusekAiPane::NewChat() {
    StopWork();
    ++m_generation;
    m_history.clear();
    ClearAttachedImages();
    m_lastAnswer.clear();
    m_currentStreamingReply.clear();
    SetWindowTextW(m_hHist, L"");
    SetWindowTextW(m_hInput, L"");
    SetStatus("Ready");
}

void GusekAiPane::JoinWorkers() {
    HANDLE *threads[] = { &m_hWorkerThread, &m_hDownloadThread };
    for (int i = 0; i < 2; ++i) {
        if (*threads[i]) {
            // Workers only post messages; they never wait for the UI thread.
            WaitForSingleObject(*threads[i], INFINITE);
            CloseHandle(*threads[i]);
            *threads[i] = NULL;
        }
    }
}

void GusekAiPane::DiscardWorkerMessages() {
    if (!m_hWnd) return;
    MSG msg;
    while (PeekMessage(&msg, m_hWnd, WM_AI_DATA, WM_AI_OFFER_DOWNLOAD, PM_REMOVE)) {
        if (msg.message != WM_AI_DATA) delete reinterpret_cast<AiWorkerEvent *>(msg.lParam);
    }
}

void GusekAiPane::PostWorkerMessage(UINT message, LONG generation, bool success,
                                   const std::string &text) {
    AiWorkerEvent *event = new AiWorkerEvent();
    event->generation = generation;
    event->success = success;
    event->text = text;
    if (!PostMessage(m_hWnd, message, 0, reinterpret_cast<LPARAM>(event))) delete event;
}

void GusekAiPane::StopWork() {
    InterlockedExchange(&m_cancel, 1);
    if (m_cancelEvent) SetEvent(m_cancelEvent);
    JoinWorkers();
    DiscardWorkerMessages();
    if (m_busy) FinishTurn(false);
    InterlockedExchange(&m_downloading, 0);
    EnableWindow(m_btnSend, TRUE);
    EnableWindow(m_btnStop, FALSE);
    SetStatus("Stopped");
}

struct WorkerThreadParams {
    GusekAiPane *pPane;
    LONG generation;
    GusekAiConfig config;
    std::string question;
    std::string documentDir;
    std::vector<ChatMessage> history;
    std::vector<std::string> images;
};

void GusekAiPane::SendQuestion() {
    if (m_busy || m_downloading) {
        SetStatus("Busy; please wait or press Stop");
        return;
    }

    // Read question text from m_hInput
    GETTEXTLENGTHEX gtl = { GTL_NUMCHARS | GTL_PRECISE | GTL_USECRLF, 1200 };
    LRESULT len = SendMessage(m_hInput, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    if (len < 0) len = 0;

    std::wstring wQ((size_t)len + 1, L'\0');
    GETTEXTEX gt;
    memset(&gt, 0, sizeof(gt));
    gt.cb = (DWORD)((len + 1) * sizeof(wchar_t));
    gt.flags = GT_DEFAULT;
    gt.codepage = 1200;
    SendMessage(m_hInput, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)&wQ[0]);

    std::string q = WideToUtf8(wQ);
    // Trim
    size_t start = 0; while (start < q.length() && (unsigned char)q[start] <= ' ') start++;
    size_t end = q.length(); while (end > start && (unsigned char)q[end - 1] <= ' ') end--;
    q = q.substr(start, end - start);

    if (q.empty() && m_attachedImages.empty()) {
        return; // Nothing to send
    }

    // Clear input
    SetWindowTextW(m_hInput, L"");

    // Display user question
    GusekAiRender::AppendHeader(m_hHist, "You", RGB(160, 0, 0));
    if (!m_attachedImages.empty()) {
        for (size_t i = 0; i < m_attachedImages.size(); i++) {
            GusekAiRender::AppendImageThumbnail(m_hHist, m_attachedImages[i].name, m_attachedImages[i].hThumb);
        }
    }
    if (!q.empty()) {
        GusekAiRender::AppendPlainText(m_hHist, (q + "\r\n").c_str());
    }

    // Display assistant header
    GusekAiRender::AppendHeader(m_hHist, "GUSEK assistant", RGB(0, 32, 128));

    GETTEXTLENGTHEX gtlHist = { GTL_NUMCHARS | GTL_PRECISE, 1200 };
    m_currentTurnStartPos = (LONG)SendMessage(m_hHist, EM_GETTEXTLENGTHEX, (WPARAM)&gtlHist, 0);

    JoinWorkers();
    DiscardWorkerMessages();
    ++m_generation;
    m_busy = true;
    InterlockedExchange(&m_cancel, 0);
    ResetEvent(m_cancelEvent);
    m_currentStreamingReply.clear();
    EnableWindow(m_btnSend, FALSE);
    EnableWindow(m_btnStop, TRUE);
    SetStatus("Connecting to model...");

    WorkerThreadParams *params = new WorkerThreadParams();
    params->pPane = this;
    params->generation = m_generation;
    params->config = m_config;
    params->question = q;
    params->history = m_history;
    // Read mutable host document state on the UI thread.
    params->documentDir = m_pHost->GetActiveDocumentPath();
    size_t slash = params->documentDir.find_last_of("/\\");
    params->documentDir = slash == std::string::npos ? "" : params->documentDir.substr(0, slash);
    for (size_t i = 0; i < m_attachedImages.size(); ++i) {
        params->images.push_back(m_attachedImages[i].base64Png);
    }
    m_pendingQuestion.role = "user";
    m_pendingQuestion.content = q;
    m_pendingQuestion.imageDataUrls = params->images;
    ClearAttachedImages();

    m_hWorkerThread = CreateThread(NULL, 0, WorkerThreadProc, params, 0, NULL);
    if (!m_hWorkerThread) {
        delete params;
        FinishTurn(false);
        SetStatus("Could not start the assistant worker");
    }
}

DWORD WINAPI GusekAiPane::WorkerThreadProc(LPVOID lpParam) {
    WorkerThreadParams *p = static_cast<WorkerThreadParams *>(lpParam);
    GusekAiPane *pane = p->pPane;
    const LONG generation = p->generation;
    const GusekAiConfig &config = p->config;
    bool done = false;
    std::string error;

    DWORD attr = GetFileAttributesW(Utf8ToWide(config.resolved_model).c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        pane->PostWorkerMessage(WM_AI_OFFER_DOWNLOAD, generation, false);
        delete p;
        return 0;
    }

    pane->PostWorkerMessage(WM_AI_STATUS, generation, true, "Loading model...");
    if (!pane->m_model.EnsureRunning(config, error, pane->m_cancelEvent)) {
        pane->PostWorkerMessage(WM_AI_DONE, generation, false, "Model error: " + error);
        delete p;
        return 0;
    }

    {
        std::string sysPrompt = config.GetSystemPrompt() +
            config.GetCourseContext(p->question, p->documentDir);
        std::string body = GusekAiProtocol::BuildChatRequestJson(
            sysPrompt, p->history, p->question, p->images, config);
        DWORD timeoutMs = static_cast<DWORD>((std::max)(1, (std::min)(config.request_timeout, 86400))) * 1000;
        GusekAiHttpRequest http(pane->m_cancelEvent, &pane->m_cancel, timeoutMs);
        DWORD status = 0;
        if (!http.Open(Utf8ToWide(config.host), static_cast<INTERNET_PORT>(config.port),
                       L"POST", L"/v1/chat/completions") ||
            !http.Send(L"Content-Type: application/json\r\n", body) || !http.Receive()) {
            error = "Request failed (Windows error " + std::to_string(http.Error()) + ")";
        } else if (!http.Status(status) || status != 200) {
            error = "Model server returned HTTP status " + std::to_string(status);
        } else {
            pane->PostWorkerMessage(WM_AI_STATUS, generation, true, "Answering...");
            char buffer[8192];
            std::string lines;
            ThinkTagFilter filter;
            while (!http.IsCancelled() && !done) {
                DWORD size = 0;
                if (!http.Read(buffer, sizeof(buffer), size)) {
                    error = "Stream failed (Windows error " + std::to_string(http.Error()) + ")";
                    break;
                }
                if (!size) break;
                lines.append(buffer, size);
                if (lines.size() > 1024 * 1024) {
                    error = "Model server sent an oversized event";
                    break;
                }
                size_t nl;
                while ((nl = lines.find('\n')) != std::string::npos) {
                    std::string line = lines.substr(0, nl);
                    lines.erase(0, nl + 1);
                    std::string delta;
                    bool isDone = false;
                    if (GusekAiProtocol::ParseSseDelta(line.c_str(), delta, isDone)) {
                        std::string text = config.strip_think ?
                            filter.FilterChunk(delta.c_str(), delta.size()) : delta;
                        if (!text.empty()) {
                            EnterCriticalSection(&pane->m_cs);
                            pane->m_pendingBuffer += text;
                            LeaveCriticalSection(&pane->m_cs);
                            if (InterlockedCompareExchange(&pane->m_postPending, 1, 0) == 0)
                                PostMessage(pane->m_hWnd, WM_AI_DATA, generation, 0);
                        }
                    }
                    if (isDone) { done = true; break; }
                }
            }
            std::string tail = config.strip_think ? filter.Flush() : "";
            if (!tail.empty()) {
                EnterCriticalSection(&pane->m_cs);
                pane->m_pendingBuffer += tail;
                LeaveCriticalSection(&pane->m_cs);
            }
            if (!done && error.empty()) error = "Model stream ended before completion";
        }
    } // Close the asynchronous request and await its last callback before DONE.
    if (InterlockedCompareExchange(&pane->m_cancel, 0, 0)) {
        done = false;
        error = "Stopped";
    }
    pane->PostWorkerMessage(WM_AI_DONE, generation, done, error);
    delete p;
    return 0;
}

void GusekAiPane::DrainPendingBuffer() {
    std::string text;
    EnterCriticalSection(&m_cs);
    text = m_pendingBuffer;
    m_pendingBuffer.clear();
    InterlockedExchange(&m_postPending, 0);
    LeaveCriticalSection(&m_cs);

    if (text.empty()) return;

    m_currentStreamingReply += text;
    if (!m_mouseDragging) {
        GusekAiRender::RenderMarkdownStream(m_hHist, m_currentStreamingReply, m_currentTurnStartPos, false);
    }
}

void GusekAiPane::FinishTurn(bool success) {
    DrainPendingBuffer();
    GusekAiRender::RenderMarkdownStream(m_hHist, m_currentStreamingReply, m_currentTurnStartPos, true);

    m_lastAnswer = m_currentStreamingReply;
    if (success) {
        ChatMessage answer;
        answer.role = "assistant";
        answer.content = m_lastAnswer;
        m_history.push_back(m_pendingQuestion);
        m_history.push_back(answer);
        size_t limit = static_cast<size_t>((std::max)(0, m_config.keep_history));
        limit -= limit % 2; // Keep complete user/assistant pairs.
        if (m_history.size() > limit)
            m_history.erase(m_history.begin(), m_history.end() - limit);
    }
    m_pendingQuestion = ChatMessage();

    m_busy = false;
    EnableWindow(m_btnSend, TRUE);
    EnableWindow(m_btnStop, FALSE);
    SetStatus(success ? "Ready" : "Stopped / Error");
    SetFocus(m_hInput);
}

void GusekAiPane::TriggerFirstRunDownload(bool isVisionOnly) {
    std::wstring msg = L"The local AI model (Qwen3.5-4B, ~3.4 GB) is not installed.\n"
                       L"Would you like to download it now? It runs completely offline on your CPU.";
    if (isVisionOnly) {
        msg = L"The multimodal picture reader (~0.7 GB) is not installed.\n"
              L"Would you like to download it now to enable image understanding?";
    }

    if (MessageBoxW(m_hWnd, msg.c_str(), L"Download Local AI Model", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        JoinWorkers();
        DiscardWorkerMessages();
        ++m_generation;
        InterlockedExchange(&m_downloading, 1);
        InterlockedExchange(&m_cancel, 0);
        ResetEvent(m_cancelEvent);
        EnableWindow(m_btnSend, FALSE);
        EnableWindow(m_btnStop, TRUE);
        SetStatus("Starting download...");
        m_hDownloadThread = CreateThread(NULL, 0, DownloadThreadProc, this, 0, NULL);
        if (!m_hDownloadThread) {
            HandleDownloadDone(false);
            SetStatus("Could not start the download worker");
        }
    } else {
        SetStatus("Download declined. Model assistant unavailable.");
    }
}

static void OnDownloadProgress(unsigned __int64 downloaded, unsigned __int64 total, const char *statusMsg, void *userData) {
    GusekAiPane *pane = (GusekAiPane *)userData;
    if (statusMsg) {
        AiWorkerEvent *event = new AiWorkerEvent();
        event->generation = 0; // Download progress is checked against the active download.
        event->success = true;
        event->text = statusMsg;
        if (!PostMessage(pane->GetHWND(), WM_AI_STATUS, 0, reinterpret_cast<LPARAM>(event))) delete event;
    }
}

DWORD WINAPI GusekAiPane::DownloadThreadProc(LPVOID lpParam) {
    GusekAiPane *pane = (GusekAiPane *)lpParam;

    std::string err;
    bool ok = GusekAiDownload::DownloadWithResume(
        pane->m_config.model_url,
        pane->m_config.resolved_model,
        pane->m_config.model_sha256,
        pane->m_config.model_bytes,
        &pane->m_cancel,
        OnDownloadProgress,
        pane,
        err,
        pane->m_cancelEvent);

    if (ok && pane->m_config.vision) {
        ok = GusekAiDownload::DownloadWithResume(
            pane->m_config.vision_url,
            pane->m_config.resolved_vision_model,
            pane->m_config.vision_sha256,
            pane->m_config.vision_bytes,
            &pane->m_cancel,
            OnDownloadProgress,
            pane,
            err,
            pane->m_cancelEvent);
    }

    pane->PostWorkerMessage(WM_AI_DLDONE, pane->m_generation, ok, err);
    return 0;
}

void GusekAiPane::HandleDownloadDone(bool success) {
    InterlockedExchange(&m_downloading, 0);
    m_busy = false;
    EnableWindow(m_btnSend, TRUE);
    EnableWindow(m_btnStop, FALSE);
    SetStatus(success ? "Model ready. You can now ask questions." : "Download failed or stopped.");
}

LRESULT CALLBACK GusekAiPane::InputSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    GusekAiPane *pPane = (GusekAiPane *)dwRefData;

    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_RETURN) {
            if (GetKeyState(VK_CONTROL) < 0) {
                pPane->SendQuestion();
                return 0;
            }
        }
        break;

    case WM_PASTE:
        if (IsClipboardFormatAvailable(CF_BITMAP) || IsClipboardFormatAvailable(CF_HDROP)) {
            pPane->AttachClipboardImage();
            return 0;
        }
        break;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK GusekAiPane::HistSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    GusekAiPane *pPane = (GusekAiPane *)dwRefData;

    switch (msg) {
    case WM_LBUTTONDOWN:
        pPane->m_mouseDragging = true;
        break;
    case WM_LBUTTONUP:
        pPane->m_mouseDragging = false;
        pPane->DrainPendingBuffer();
        break;
    case WM_CAPTURECHANGED:
        pPane->m_mouseDragging = false;
        pPane->DrainPendingBuffer();
        break;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK GusekAiPane::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    GusekAiPane *pPane = (GusekAiPane *)GetWindowLongPtr(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }

    case WM_SIZE:
        if (pPane) {
            pPane->LayoutChildren(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (pPane) {
            switch (id) {
            case IDC_AI_SEND:       pPane->SendQuestion(); return 0;
            case IDC_AI_STOP:       pPane->StopWork(); return 0;
            case IDC_AI_COPY:       pPane->CopyLastCode(); return 0;
            case IDC_AI_TOED:       pPane->InsertCodeToEditor(); return 0;
            case IDC_AI_NEW:        pPane->NewChat(); return 0;
            case IDC_AI_ATTACH_BTN: pPane->ShowAttachMenu(); return 0;

            case IDM_AI_ATTACH_ERR: {
                std::string err = pPane->m_pHost->GetLastSolverError();
                if (!err.empty()) pPane->AppendTextToInput("\n" + err + "\n");
                else pPane->SetStatus("No solver error detected in recent output");
                return 0;
            }
            case IDM_AI_ATTACH_OUT: {
                std::string out = pPane->m_pHost->GetLastSolverOutput();
                if (!out.empty()) pPane->AppendTextToInput("\n" + out + "\n");
                else pPane->SetStatus("No solver output available");
                return 0;
            }
            case IDM_AI_ATTACH_DOC: {
                std::string doc = pPane->m_pHost->GetActiveDocumentText();
                if (!doc.empty()) pPane->AppendTextToInput("\n" + doc + "\n");
                else pPane->SetStatus("No active document");
                return 0;
            }
            case IDM_AI_ATTACH_RECENT: {
                std::string rec = pPane->m_pHost->GetRecentOutput(2000);
                if (!rec.empty()) pPane->AppendTextToInput("\n" + rec + "\n");
                return 0;
            }
            case IDM_AI_ATTACH_FILE:  pPane->AttachImageFile(); return 0;
            case IDM_AI_ATTACH_CLIP:  pPane->AttachClipboardImage(); return 0;
            case IDM_AI_ATTACH_CLEAR: pPane->ClearAttachedImages(); return 0;
            }
        }
        break;
    }

    case WM_AI_DATA:
        if (pPane && static_cast<LONG>(wParam) == pPane->m_generation)
            pPane->DrainPendingBuffer();
        return 0;

    case WM_AI_STATUS:
    case WM_AI_DONE:
    case WM_AI_DLDONE:
    case WM_AI_OFFER_DOWNLOAD: {
        AiWorkerEvent *event = reinterpret_cast<AiWorkerEvent *>(lParam);
        if (event && pPane &&
            (event->generation == pPane->m_generation ||
             (event->generation == 0 && pPane->m_downloading))) {
            if (msg == WM_AI_STATUS) {
                pPane->SetStatus(event->text);
            } else if (msg == WM_AI_DONE) {
                pPane->JoinWorkers();
                pPane->FinishTurn(event->success);
                if (!event->text.empty()) pPane->SetStatus(event->text);
            } else if (msg == WM_AI_DLDONE) {
                pPane->JoinWorkers();
                pPane->HandleDownloadDone(event->success);
                if (!event->text.empty()) pPane->SetStatus(event->text);
            } else {
                pPane->JoinWorkers();
                pPane->FinishTurn(false);
                pPane->TriggerFirstRunDownload(false);
            }
        }
        delete event;
        return 0;
    }



    case WM_DESTROY:
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

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
      m_hWnd(NULL), m_hStatus(NULL), m_hHist(NULL), m_hInput(NULL), m_hStrip(NULL),
      m_btnSend(NULL), m_btnStop(NULL), m_btnCopy(NULL), m_btnToEd(NULL),
      m_btnNew(NULL), m_btnAttach(NULL),
      m_hFontUi(NULL), m_hMsftEdit(NULL), m_oleInitialized(SUCCEEDED(OleInitialize(NULL))),
      m_busy(false), m_warming(false), m_downloadVisionOnly(false), m_renderPending(false), m_cancel(0), m_downloading(0), m_mouseDragging(false),
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
    if (m_oleInitialized) OleUninitialize();
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
    if (!m_config.enabled) return false;

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
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, width, height - 160, m_hWnd, (HMENU)IDC_AI_HIST, GetModuleHandle(NULL), NULL);
    GusekAiRender::SetupRichEdit(m_hHist);
    SetWindowSubclass(m_hHist, HistSubclassProc, 1, (DWORD_PTR)this);

    // Initial welcome message
    GusekAiRender::AppendPlainText(m_hHist,
        "Local MathProg & GLPK Assistant.\r\n"
        "Ask questions about optimization models, GMPL syntax or solver errors.\r\n"
        "Use Attach to include the current model, last solver error or screenshots.\r\n"
        "Copy code or To editor inserts code into the editor. Nothing is run automatically.\r\n\r\n");

    m_hStrip = CreateWindowExW(0, L"STATIC", L"Attached pictures", WS_CHILD,
        0, 0, width, 92, m_hWnd, (HMENU)IDC_AI_STRIP, GetModuleHandle(NULL), NULL);
    SetWindowSubclass(m_hStrip, StripSubclassProc, 3, reinterpret_cast<DWORD_PTR>(this));
    // 3. Question box
    m_hInput = CreateWindowExW(
        WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP,
        0, 0, width, 80, m_hWnd, (HMENU)IDC_AI_INPUT, GetModuleHandle(NULL), NULL);
    GusekAiRender::SetupRichEdit(m_hInput);
    SetWindowSubclass(m_hInput, InputSubclassProc, 2, (DWORD_PTR)this);
    SendMessage(m_hInput, EM_EXLIMITTEXT, 0, 65536);

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
    for (size_t i = 0; i < m_sentPictures.size(); ++i) GusekAiImage::Release(m_sentPictures[i].image);
    m_sentPictures.clear();
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
            WarmUp();
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
    int buttonRows = width < 390 ? 2 : 1;
    int btnH = 26 * buttonRows;
    int inputH = 80;

    // Status at top
    SetWindowPos(m_hStatus, NULL, pad, pad, width - pad * 2, statusH, SWP_NOZORDER);

    // Buttons at bottom
    int btnY = height - btnH - pad;
    int bx = pad;
    HWND btns[] = { m_btnSend, m_btnStop, m_btnAttach, m_btnCopy, m_btnToEd, m_btnNew };
    int widths[] = { 50, 45, 60, 68, 62, 65 };

    for (int i = 0; i < 6; i++) {
        int row = buttonRows == 2 && i >= 3 ? 1 : 0;
        if (i == 3 && row) bx = pad;
        SetWindowPos(btns[i], NULL, bx, btnY + row * 26, widths[i], 24, SWP_NOZORDER);
        bx += widths[i] + 3;
    }

    // Input above buttons
    int inputY = btnY - inputH - pad;
    SetWindowPos(m_hInput, NULL, pad, inputY, width - pad * 2, inputH, SWP_NOZORDER);

    // Transcript takes the rest
    int histY = statusH + pad * 2;
    int stripHeight = m_attachedImages.empty() ? 0 : 96;
    int histH = inputY - histY - pad - stripHeight;
    SetWindowPos(m_hStrip, NULL, pad, inputY - stripHeight - pad, width - pad * 2,
                 stripHeight, SWP_NOZORDER);
    ShowWindow(m_hStrip, stripHeight ? SW_SHOW : SW_HIDE);
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
    WCHAR szFile[32768] = L"";
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hWnd;
    ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = 32768;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

    if (GetOpenFileNameW(&ofn)) {
        std::wstring directory = szFile;
        const wchar_t *file = szFile + directory.size() + 1;
        if (!*file) {
            ChatImageAttachment image;
            if (GusekAiImage::LoadImageFromFile(directory, image)) AddAttachedImage(image);
            else SetStatus("Could not read the picture file");
        } else {
            for (; *file; file += wcslen(file) + 1) {
                ChatImageAttachment image;
                if (GusekAiImage::LoadImageFromFile(directory + L"\\" + file, image)) AddAttachedImage(image);
                else SetStatus("Could not read a selected picture");
            }
        }
        if (!m_busy && m_config.vision &&
            GetFileAttributesW(Utf8ToWide(m_config.resolved_model).c_str()) != INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(Utf8ToWide(m_config.resolved_vision_model).c_str()) == INVALID_FILE_ATTRIBUTES)
            TriggerFirstRunDownload(true);
    }
}

void GusekAiPane::AttachClipboardImage() {
    std::vector<ChatImageAttachment> images;
    if (GusekAiImage::LoadImagesFromClipboard(m_hWnd, images)) {
        for (size_t i = 0; i < images.size(); ++i) AddAttachedImage(images[i]);
        if (!m_busy && m_config.vision &&
            GetFileAttributesW(Utf8ToWide(m_config.resolved_model).c_str()) != INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(Utf8ToWide(m_config.resolved_vision_model).c_str()) == INVALID_FILE_ATTRIBUTES)
            TriggerFirstRunDownload(true);
    } else SetStatus("No picture found on clipboard");
}

void GusekAiPane::UpdateAttachmentStrip() {
    if (!m_hWnd || !IsWindow(m_hWnd)) return;
    RECT rc = {};
    if (!GetClientRect(m_hWnd, &rc)) return;
    LayoutChildren(rc.right, rc.bottom);
    InvalidateRect(m_hStrip, NULL, TRUE);
}

void GusekAiPane::AddAttachedImage(ChatImageAttachment &image) {
    if (m_attachedImages.size() >= 16) {
        GusekAiImage::Release(m_attachedImages[0]);
        m_attachedImages.erase(m_attachedImages.begin());
    }
    m_attachedImages.push_back(image);
    image = ChatImageAttachment();
    UpdateAttachmentStrip();
    if (m_attachedImages.size() > 4)
        SetStatus(std::to_string(m_attachedImages.size()) + " pictures attached; the answer uses the 4 newest pictures.");
    else SetStatus("Picture attached. Click its preview to open it; x removes it.");
}

LRESULT CALLBACK GusekAiPane::StripSubclassProc(HWND window, UINT message, WPARAM wp, LPARAM lp,
                                               UINT_PTR, DWORD_PTR context) {
    GusekAiPane *pane = reinterpret_cast<GusekAiPane *>(context);
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT rc;
        GetClientRect(window, &rc);
        FillRect(dc, &rc, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
        SetBkMode(dc, TRANSPARENT);
        HFONT oldFont = static_cast<HFONT>(SelectObject(dc, pane->m_hFontUi));
        int tile = (std::max)(1, static_cast<int>(rc.right) / (std::max)(1, static_cast<int>(pane->m_attachedImages.size())));
        tile = (std::min)(150, tile);
        for (size_t i = 0; i < pane->m_attachedImages.size(); ++i) {
            const ChatImageAttachment &image = pane->m_attachedImages[i];
            int left = static_cast<int>(i) * tile;
            BITMAP size;
            if (image.hThumb && GetObject(image.hThumb, sizeof(size), &size)) {
                double scale = (std::min)(static_cast<double>((std::max)(1, tile - 18)) / size.bmWidth,
                                         64.0 / size.bmHeight);
                int width = (std::max)(1, static_cast<int>(size.bmWidth * scale));
                int height = (std::max)(1, static_cast<int>(size.bmHeight * scale));
                HDC memory = CreateCompatibleDC(dc);
                HGDIOBJ previous = SelectObject(memory, image.hThumb);
                SetStretchBltMode(dc, HALFTONE);
                StretchBlt(dc, left + 2, 2, width, height, memory, 0, 0, size.bmWidth, size.bmHeight, SRCCOPY);
                SelectObject(memory, previous);
                DeleteDC(memory);
            }
            RECT label = { left + 2, 68, left + tile - 2, 90 };
            DrawTextW(dc, Utf8ToWide(image.name).c_str(), -1, &label, DT_SINGLELINE | DT_END_ELLIPSIS);
            RECT remove = { left + tile - 17, 0, left + tile, 18 };
            DrawTextW(dc, L"x", -1, &remove, DT_CENTER | DT_SINGLELINE);
        }
        SelectObject(dc, oldFont);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_LBUTTONUP && !pane->m_attachedImages.empty()) {
        RECT rc;
        GetClientRect(window, &rc);
        int tile = (std::min)(150, (std::max)(1, static_cast<int>(rc.right) / static_cast<int>(pane->m_attachedImages.size())));
        int x = static_cast<short>(LOWORD(lp)), y = static_cast<short>(HIWORD(lp));
        size_t index = x < 0 ? pane->m_attachedImages.size() : static_cast<size_t>(x / tile);
        if (index < pane->m_attachedImages.size()) {
            if (y < 18 && x % tile >= tile - 17) {
                GusekAiImage::Release(pane->m_attachedImages[index]);
                pane->m_attachedImages.erase(pane->m_attachedImages.begin() + index);
                pane->UpdateAttachmentStrip();
            } else GusekAiImage::ShowImageViewer(pane->m_hWnd, pane->m_attachedImages[index]);
        }
        return 0;
    }
    return DefSubclassProc(window, message, wp, lp);
}

void GusekAiPane::ClearAttachedImages() {
    for (size_t i = 0; i < m_attachedImages.size(); ++i) {
        GusekAiImage::Release(m_attachedImages[i]);
    }
    m_attachedImages.clear();
    UpdateAttachmentStrip();
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
    for (size_t i = 0; i < m_sentPictures.size(); ++i) GusekAiImage::Release(m_sentPictures[i].image);
    m_sentPictures.clear();
    m_renderPending = false;
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
    bool warmup;
    std::string question;
    std::string documentDir;
    std::vector<ChatMessage> history;
    std::vector<std::string> images;
};

void GusekAiPane::WarmUp() {
    if (!m_config.autostart || m_busy || m_downloading ||
        GetFileAttributesW(Utf8ToWide(m_config.resolved_model).c_str()) == INVALID_FILE_ATTRIBUTES) return;
    JoinWorkers();
    DiscardWorkerMessages();
    ++m_generation;
    InterlockedExchange(&m_cancel, 0);
    ResetEvent(m_cancelEvent);
    m_busy = m_warming = true;
    EnableWindow(m_btnSend, FALSE);
    EnableWindow(m_btnStop, TRUE);
    SetStatus("Loading model...");
    WorkerThreadParams *params = new WorkerThreadParams();
    params->pPane = this;
    params->generation = m_generation;
    params->config = m_config;
    params->warmup = true;
    m_hWorkerThread = CreateThread(NULL, 0, WorkerThreadProc, params, 0, NULL);
    if (!m_hWorkerThread) { delete params; FinishTurn(false); }
}

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

    if (!m_attachedImages.empty()) {
        if (!m_config.vision) {
            SetStatus("Picture support is disabled. Remove pictures to ask a text question.");
            return;
        }
        if (GetFileAttributesW(Utf8ToWide(m_config.resolved_model).c_str()) != INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(Utf8ToWide(m_config.resolved_vision_model).c_str()) == INVALID_FILE_ATTRIBUTES) {
            TriggerFirstRunDownload(true);
            return;
        }
    }
    if (GetFileAttributesW(Utf8ToWide(m_config.resolved_model).c_str()) == INVALID_FILE_ATTRIBUTES) {
        TriggerFirstRunDownload(false);
        return;
    }
    GusekAiRender::ScrollToBottom(m_hHist);
    // Clear input
    SetWindowTextW(m_hInput, L"");

    // Display user question
    GusekAiRender::AppendHeader(m_hHist, "You", RGB(160, 0, 0));
    if (!m_attachedImages.empty()) {
        for (size_t i = 0; i < m_attachedImages.size(); i++) {
            SentPicture picture;
            GETTEXTLENGTHEX length = { GTL_NUMCHARS | GTL_PRECISE, 1200 };
            picture.position = static_cast<LONG>(SendMessage(m_hHist, EM_GETTEXTLENGTHEX, (WPARAM)&length, 0));
            GusekAiRender::AppendImageThumbnail(m_hHist, m_attachedImages[i].name, m_attachedImages[i].hThumb);
            picture.image = m_attachedImages[i];
            m_attachedImages[i].hThumb = m_attachedImages[i].hPreview = NULL;
            if (m_sentPictures.size() >= 16) {
                GusekAiImage::Release(m_sentPictures[0].image);
                m_sentPictures.erase(m_sentPictures.begin());
            }
            m_sentPictures.push_back(picture);
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
    params->warmup = false;
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

    if (p->warmup) {
        pane->PostWorkerMessage(WM_AI_DONE, generation, true);
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
            char errorBody[4096];
            DWORD errorSize = 0;
            if (http.Read(errorBody, sizeof(errorBody), errorSize) && errorSize) {
                std::string explanation;
                bool ignored = false;
                std::string record = "data: " + std::string(errorBody, errorSize);
                size_t key = record.find("\"message\"");
                if (key != std::string::npos) record.replace(key, 9, "\"content\"");
                if (GusekAiProtocol::ParseSseDelta(record.c_str(), explanation, ignored) && !explanation.empty())
                    error += ": " + explanation;
            }
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

    m_currentStreamingReply += text;
    if (text.empty() && !m_renderPending) return;
    if (m_mouseDragging && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) m_mouseDragging = false;
    if (!m_mouseDragging) {
        m_renderPending = false;
        GusekAiRender::RenderMarkdownStream(m_hHist, m_currentStreamingReply, m_currentTurnStartPos, false);
    }
}

void GusekAiPane::FinishTurn(bool success) {
    DrainPendingBuffer();
    if (m_warming) {
        m_warming = false;
        m_busy = false;
        EnableWindow(m_btnSend, TRUE);
        EnableWindow(m_btnStop, FALSE);
        SetStatus(success ? "Ready" : "Model warm-up stopped or failed");
        return;
    }
    if (m_mouseDragging && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) m_renderPending = true;
    else GusekAiRender::RenderMarkdownStream(m_hHist, m_currentStreamingReply, m_currentTurnStartPos, true);

    m_lastAnswer = m_currentStreamingReply;
    if (success) {
        ChatMessage answer;
        answer.role = "assistant";
        answer.content = m_lastAnswer;
        m_history.push_back(m_pendingQuestion);
        m_history.push_back(answer);
        size_t pictureBudget = 4;
        for (size_t i = m_history.size(); i > 0; ) {
            --i;
            std::vector<std::string> &pictures = m_history[i].imageDataUrls;
            size_t keep = (std::min)(pictureBudget, pictures.size());
            if (keep < pictures.size()) {
                pictures.erase(pictures.begin(), pictures.end() - keep);
                m_history[i].content = "[A picture was attached here; it is no longer shown.]\n" + m_history[i].content;
            }
            pictureBudget -= keep;
        }
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
    if ((isVisionOnly ? m_config.vision_url : m_config.model_url).empty()) {
        SetStatus("Model download is disabled in the configuration");
        return;
    }
    m_downloadVisionOnly = isVisionOnly;
    std::wstring msg = L"The local AI model (Qwen3.5-4B, ~3.4 GB) is not installed.\n"
                       L"Would you like to download it now? It runs completely offline on your CPU.";
    if (isVisionOnly) {
        msg = L"The multimodal picture reader (~0.7 GB) is not installed.\n"
              L"Would you like to download it now to enable image understanding?";
    }

    if (MessageBoxW(m_hWnd, msg.c_str(), L"Download Local AI Model", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        StopWork();
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
        SetStatus(isVisionOnly ? "Picture reader download declined. Text chat is still available." : "Download declined. Model assistant unavailable.");
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
    bool ok = pane->m_downloadVisionOnly || GusekAiDownload::DownloadWithResume(
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
    if (success) m_model.Stop(); // Next request restarts an owned text-only server with the reader.
    SetStatus(success ? "Model ready. You can now ask questions." : "Download failed or stopped.");
}

LRESULT CALLBACK GusekAiPane::InputSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    GusekAiPane *pPane = (GusekAiPane *)dwRefData;

    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_INSERT && GetKeyState(VK_SHIFT) < 0 &&
            GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_MENU) >= 0) {
            SendMessage(hWnd, WM_PASTE, 0, 0);
            return 0;
        }
        if (wParam == VK_RETURN) {
            if (GetKeyState(VK_CONTROL) < 0) {
                pPane->SendQuestion();
                return 0;
            }
        }
        break;

    case WM_CHAR:
        // RichEdit handles native Ctrl+V internally without sending WM_PASTE.
        // Intercept its control character so pictures enter the attachment list
        // instead of becoming an OLE object that is absent from the model request.
        if (wParam == 22) {
            SendMessage(hWnd, WM_PASTE, 0, 0);
            return 0;
        }
        break;

    case WM_PASTE:
        if (IsClipboardFormatAvailable(CF_BITMAP) || IsClipboardFormatAvailable(CF_HDROP)) {
            pPane->AttachClipboardImage();
            return 0;
        }
        SendMessage(hWnd, EM_PASTESPECIAL, CF_UNICODETEXT, 0);
        return 0;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK GusekAiPane::HistSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    GusekAiPane *pPane = (GusekAiPane *)dwRefData;

    switch (msg) {
    case WM_LBUTTONDBLCLK: {
        POINT point = { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
        LONG position = static_cast<LONG>(SendMessage(hWnd, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&point)));
        for (size_t i = 0; i < pPane->m_sentPictures.size(); ++i) {
            if (position == pPane->m_sentPictures[i].position) {
                GusekAiImage::ShowImageViewer(pPane->m_hWnd, pPane->m_sentPictures[i].image);
                return 0;
            }
        }
        break;
    }
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
                if (out.size() > 16384) out = "[Earlier solver output omitted.]\n" + out.substr(out.size() - 16384);
                if (!out.empty()) pPane->AppendTextToInput("\n" + out + "\n");
                else pPane->SetStatus("No solver output available");
                return 0;
            }
            case IDM_AI_ATTACH_DOC: {
                std::string doc = pPane->m_pHost->GetActiveDocumentText();
                if (!doc.empty()) {
                    if (doc.size() > 16384) doc = doc.substr(0, 16384) + "\n[Document excerpt truncated.]";
                    pPane->AppendTextToInput("\nDocument: " + pPane->m_pHost->GetActiveDocumentPath() +
                        " (" + pPane->m_pHost->GetActiveDocumentFormat() + ")\n" + doc + "\n");
                }
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

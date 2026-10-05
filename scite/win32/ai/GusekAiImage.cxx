#include "GusekAiImage.h"
#include <shellapi.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

static ULONG_PTR g_gdiplusToken = 0;
static bool g_gdiplusInitialized = false;

static const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string Base64Encode(const unsigned char *data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned int val = (data[i] << 16);
        if (i + 1 < len) val |= (data[i+1] << 8);
        if (i + 2 < len) val |= data[i+2];

        out += b64_chars[(val >> 18) & 0x3F];
        out += b64_chars[(val >> 12) & 0x3F];
        out += (i + 1 < len) ? b64_chars[(val >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? b64_chars[val & 0x3F] : '=';
    }
    return out;
}

static int GetEncoderClsid(const WCHAR *format, CLSID *pClsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;

    ImageCodecInfo *pInfo = (ImageCodecInfo *)(malloc(size));
    if (!pInfo) return -1;

    GetImageEncoders(num, size, pInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pInfo[j].MimeType, format) == 0) {
            *pClsid = pInfo[j].Clsid;
            free(pInfo);
            return j;
        }
    }
    free(pInfo);
    return -1;
}

void GusekAiImage::Initialize() {
    if (!g_gdiplusInitialized) {
        GdiplusStartupInput input;
        GdiplusStartup(&g_gdiplusToken, &input, NULL);
        g_gdiplusInitialized = true;
    }
}

void GusekAiImage::Shutdown() {
    if (g_gdiplusInitialized) {
        GdiplusShutdown(g_gdiplusToken);
        g_gdiplusInitialized = false;
    }
}

static bool ProcessGdiBitmap(Bitmap *pBmp, const std::string &name, ChatImageAttachment &outAttach) {
    if (!pBmp || pBmp->GetLastStatus() != Ok) return false;

    // 1. Read EXIF orientation (PropertyTagOrientation = 0x0112)
    UINT propSize = pBmp->GetPropertyItemSize(PropertyTagOrientation);
    if (propSize > 0) {
        PropertyItem *propItem = (PropertyItem *)malloc(propSize);
        if (propItem) {
            if (pBmp->GetPropertyItem(PropertyTagOrientation, propSize, propItem) == Ok) {
                short orientation = *(short *)(propItem->value);
                switch (orientation) {
                case 3: pBmp->RotateFlip(Rotate180FlipNone); break;
                case 6: pBmp->RotateFlip(Rotate90FlipNone); break;
                case 8: pBmp->RotateFlip(Rotate270FlipNone); break;
                default: break;
                }
            }
            free(propItem);
        }
    }

    // 2. Downscale if max dimension > 1600
    UINT w = pBmp->GetWidth();
    UINT h = pBmp->GetHeight();
    Bitmap *workBmp = pBmp;
    bool deleteWorkBmp = false;

    if (w > 1600 || h > 1600) {
        double scale = 1600.0 / (double)(w > h ? w : h);
        UINT nw = (UINT)(w * scale);
        UINT nh = (UINT)(h * scale);
        if (nw < 1) nw = 1;
        if (nh < 1) nh = 1;

        Bitmap *scaled = new Bitmap(nw, nh, PixelFormat32bppARGB);
        Graphics g(scaled);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(pBmp, 0, 0, nw, nh);
        workBmp = scaled;
        deleteWorkBmp = true;
        w = nw;
        h = nh;
    }

    // 3. Encode to memory stream (PNG first, JPEG fallback if > 1.2MB)
    CLSID pngClsid, jpgClsid;
    GetEncoderClsid(L"image/png", &pngClsid);
    GetEncoderClsid(L"image/jpeg", &jpgClsid);

    IStream *pStream = NULL;
    CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    if (!pStream) {
        if (deleteWorkBmp) delete workBmp;
        return false;
    }

    workBmp->Save(pStream, &pngClsid, NULL);

    STATSTG stat;
    pStream->Stat(&stat, STATFLAG_NONAME);
    size_t size = (size_t)stat.cbSize.QuadPart;

    std::string mime = "image/png";
    if (size > 1200000) {
        // Re-encode as JPEG
        pStream->Release();
        pStream = NULL;
        CreateStreamOnHGlobal(NULL, TRUE, &pStream);

        EncoderParameters params;
        params.Count = 1;
        params.Parameter[0].Guid = EncoderQuality;
        params.Parameter[0].Type = EncoderParameterValueTypeLong;
        params.Parameter[0].NumberOfValues = 1;
        ULONG quality = 85;
        params.Parameter[0].Value = &quality;

        workBmp->Save(pStream, &jpgClsid, &params);
        pStream->Stat(&stat, STATFLAG_NONAME);
        size = (size_t)stat.cbSize.QuadPart;
        mime = "image/jpeg";
    }

    HGLOBAL hMem = NULL;
    GetHGlobalFromStream(pStream, &hMem);
    void *pBytes = GlobalLock(hMem);
    std::string b64 = Base64Encode((const unsigned char *)pBytes, size);
    GlobalUnlock(hMem);
    pStream->Release();

    // 4. Create thumbnail (max dimension ~160px for attachment strip)
    UINT tw = w, th = h;
    double tscale = 160.0 / (double)(w > h ? w : h);
    if (tscale < 1.0) {
        tw = (UINT)(w * tscale);
        th = (UINT)(h * tscale);
    }
    Bitmap thumb(tw, th, PixelFormat32bppARGB);
    Graphics tg(&thumb);
    tg.SetInterpolationMode(InterpolationModeHighQualityBilinear);
    tg.DrawImage(workBmp, 0, 0, tw, th);

    HBITMAP hThumb = NULL;
    Color bg(255, 255, 255);
    thumb.GetHBITMAP(bg, &hThumb);

    if (deleteWorkBmp) delete workBmp;

    outAttach.name = name;
    outAttach.base64Png = "data:" + mime + ";base64," + b64;
    outAttach.hThumb = hThumb;
    return true;
}

bool GusekAiImage::LoadImageFromFile(const std::wstring &path, ChatImageAttachment &outAttach) {
    Initialize();
    Bitmap *pBmp = new Bitmap(path.c_str());
    std::string name = WideToUtf8(path);
    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);

    bool ok = ProcessGdiBitmap(pBmp, name, outAttach);
    delete pBmp;
    return ok;
}

bool GusekAiImage::LoadImageFromClipboard(HWND hWndOwner, ChatImageAttachment &outAttach) {
    Initialize();
    // Open clipboard with retry (up to 500ms)
    bool opened = false;
    for (int i = 0; i < 10; i++) {
        if (OpenClipboard(hWndOwner)) {
            opened = true;
            break;
        }
        Sleep(50);
    }
    if (!opened) return false;

    bool ok = false;
    // Check CF_HDROP first (file copied from Explorer)
    if (IsClipboardFormatAvailable(CF_HDROP)) {
        HDROP hDrop = (HDROP)GetClipboardData(CF_HDROP);
        if (hDrop) {
            UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
            for (UINT i = 0; i < count; i++) {
                WCHAR filePath[MAX_PATH];
                if (DragQueryFileW(hDrop, i, filePath, MAX_PATH) > 0) {
                    CloseClipboard();
                    return LoadImageFromFile(filePath, outAttach);
                }
            }
        }
    }

    // Check CF_BITMAP
    if (IsClipboardFormatAvailable(CF_BITMAP)) {
        HBITMAP hBmp = (HBITMAP)GetClipboardData(CF_BITMAP);
        if (hBmp) {
            Bitmap *pBmp = new Bitmap(hBmp, NULL);
            ok = ProcessGdiBitmap(pBmp, "Clipboard Image", outAttach);
            delete pBmp;
        }
    }

    CloseClipboard();
    return ok;
}

// Model viewer window
static LRESULT CALLBACK ViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        HBITMAP hbm = (HBITMAP)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (hbm) {
            HDC memDC = CreateCompatibleDC(hdc);
            HGDIOBJ old = SelectObject(memDC, hbm);
            BITMAP bm;
            GetObject(hbm, sizeof(bm), &bm);
            RECT rc;
            GetClientRect(hWnd, &rc);
            SetStretchBltMode(hdc, HALFTONE);
            StretchBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
            SelectObject(memDC, old);
            DeleteDC(memDC);
        }
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void GusekAiImage::ShowImageViewer(HWND hParent, const ChatImageAttachment &attach) {
    if (!attach.hThumb) return;

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = ViewerWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"GusekAiImageViewer";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    std::wstring title = L"Image: " + Utf8ToWide(attach.name);
    HWND hViewer = CreateWindowExW(
        WS_EX_TOPMOST, L"GusekAiImageViewer", title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 450,
        hParent, NULL, GetModuleHandle(NULL), NULL);

    if (hViewer) {
        SetWindowLongPtr(hViewer, GWLP_USERDATA, (LONG_PTR)attach.hThumb);
        ShowWindow(hViewer, SW_SHOW);
    }
}

#include "GusekAiImage.h"
#include <shellapi.h>
#include <gdiplus.h>
#include <memory>
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;
static ULONG_PTR g_gdiplusToken = 0;
static const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string Base64Encode(const unsigned char *data, size_t length) {
    std::string out;
    out.reserve(((length + 2) / 3) * 4);
    for (size_t i = 0; i < length; i += 3) {
        unsigned int value = data[i] << 16;
        if (i + 1 < length) value |= data[i + 1] << 8;
        if (i + 2 < length) value |= data[i + 2];
        out += b64_chars[(value >> 18) & 63];
        out += b64_chars[(value >> 12) & 63];
        out += i + 1 < length ? b64_chars[(value >> 6) & 63] : '=';
        out += i + 2 < length ? b64_chars[value & 63] : '=';
    }
    return out;
}

static bool Encoder(const WCHAR *mime, CLSID &id) {
    UINT count = 0, size = 0;
    if (GetImageEncodersSize(&count, &size) != Ok || !size) return false;
    std::vector<BYTE> buffer(size);
    ImageCodecInfo *codecs = reinterpret_cast<ImageCodecInfo *>(&buffer[0]);
    if (GetImageEncoders(count, size, codecs) != Ok) return false;
    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(codecs[i].MimeType, mime) == 0) { id = codecs[i].Clsid; return true; }
    }
    return false;
}

void GusekAiImage::Initialize() {
    if (!g_gdiplusToken) {
        GdiplusStartupInput input;
        if (GdiplusStartup(&g_gdiplusToken, &input, NULL) != Ok) g_gdiplusToken = 0;
    }
}

void GusekAiImage::Shutdown() {
    if (g_gdiplusToken) {
        GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

void GusekAiImage::Release(ChatImageAttachment &image) {
    if (image.hThumb) DeleteObject(image.hThumb);
    if (image.hPreview) DeleteObject(image.hPreview);
    image = ChatImageAttachment();
}

static bool Encode(Bitmap &image, const WCHAR *mime, std::vector<BYTE> &bytes) {
    CLSID id;
    if (!Encoder(mime, id)) return false;
    IStream *stream = NULL;
    if (FAILED(CreateStreamOnHGlobal(NULL, TRUE, &stream))) return false;
    ULONG quality = 85;
    EncoderParameters parameters;
    parameters.Count = 1;
    parameters.Parameter[0].Guid = EncoderQuality;
    parameters.Parameter[0].Type = EncoderParameterValueTypeLong;
    parameters.Parameter[0].NumberOfValues = 1;
    parameters.Parameter[0].Value = &quality;
    bool ok = image.Save(stream, &id, wcscmp(mime, L"image/jpeg") == 0 ? &parameters : NULL) == Ok;
    STATSTG stat = {};
    HGLOBAL memory = NULL;
    if (ok) ok = SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) && stat.cbSize.QuadPart > 0 &&
        stat.cbSize.QuadPart <= 16 * 1024 * 1024 && SUCCEEDED(GetHGlobalFromStream(stream, &memory));
    if (ok) {
        BYTE *data = static_cast<BYTE *>(GlobalLock(memory));
        ok = data != NULL;
        if (data) {
            bytes.assign(data, data + static_cast<size_t>(stat.cbSize.QuadPart));
            GlobalUnlock(memory);
        }
    }
    stream->Release();
    return ok;
}

bool GusekAiImage::ThumbnailPng(HBITMAP bitmap, std::vector<BYTE> &bytes) {
    Initialize();
    Bitmap image(bitmap, NULL);
    return bitmap && image.GetLastStatus() == Ok && Encode(image, L"image/png", bytes);
}

static bool Process(Bitmap &original, const std::string &name, ChatImageAttachment &out) {
    if (original.GetLastStatus() != Ok || !original.GetWidth() || !original.GetHeight() ||
        static_cast<unsigned __int64>(original.GetWidth()) * original.GetHeight() > 64000000) return false;
    UINT propertySize = original.GetPropertyItemSize(PropertyTagOrientation);
    if (propertySize >= sizeof(PropertyItem)) {
        std::vector<BYTE> data(propertySize);
        PropertyItem *property = reinterpret_cast<PropertyItem *>(&data[0]);
        if (original.GetPropertyItem(PropertyTagOrientation, propertySize, property) == Ok &&
            property->type == PropertyTagTypeShort && property->length >= sizeof(short) && property->value) {
            short orientation = *static_cast<short *>(property->value);
            switch (orientation) {
            case 2: original.RotateFlip(RotateNoneFlipX); break;
            case 3: original.RotateFlip(Rotate180FlipNone); break;
            case 4: original.RotateFlip(Rotate180FlipX); break;
            case 5: original.RotateFlip(Rotate90FlipX); break;
            case 6: original.RotateFlip(Rotate90FlipNone); break;
            case 7: original.RotateFlip(Rotate270FlipX); break;
            case 8: original.RotateFlip(Rotate270FlipNone); break;
            }
        }
    }
    UINT width = original.GetWidth(), height = original.GetHeight();
    double scale = (std::min)(1.0, 1600.0 / (std::max)(width, height));
    UINT targetWidth = (std::max)(1U, static_cast<UINT>(width * scale));
    UINT targetHeight = (std::max)(1U, static_cast<UINT>(height * scale));
    // Composite on white so transparent source pixels remain readable in JPEG.
    Bitmap normalized(targetWidth, targetHeight, PixelFormat32bppARGB);
    Graphics graphics(&normalized);
    graphics.Clear(Color::White);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    if (graphics.DrawImage(&original, 0, 0, targetWidth, targetHeight) != Ok) return false;
    std::vector<BYTE> bytes;
    if (!Encode(normalized, L"image/png", bytes)) return false;
    const char *mime = "image/png";
    if (bytes.size() > 1200000) {
        if (!Encode(normalized, L"image/jpeg", bytes)) return false;
        mime = "image/jpeg";
    }

    double thumbScale = (std::min)(1.0, 160.0 / (std::max)(targetWidth, targetHeight));
    UINT tw = (std::max)(1U, static_cast<UINT>(targetWidth * thumbScale));
    UINT th = (std::max)(1U, static_cast<UINT>(targetHeight * thumbScale));
    Bitmap thumbnail(tw, th, PixelFormat32bppARGB);
    Graphics thumbGraphics(&thumbnail);
    thumbGraphics.Clear(Color::White);
    thumbGraphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    if (thumbGraphics.DrawImage(&normalized, 0, 0, tw, th) != Ok) return false;
    ChatImageAttachment result;
    if (thumbnail.GetHBITMAP(Color::White, &result.hThumb) != Ok ||
        normalized.GetHBITMAP(Color::White, &result.hPreview) != Ok) {
        GusekAiImage::Release(result);
        return false;
    }
    result.name = name;
    result.base64Png = std::string("data:") + mime + ";base64," + Base64Encode(&bytes[0], bytes.size());
    out = result;
    return true;
}

bool GusekAiImage::LoadImageFromFile(const std::wstring &path, ChatImageAttachment &out) {
    Initialize();
    if (!g_gdiplusToken) return false;
    Bitmap original(path.c_str());
    std::string name = WideToUtf8(path);
    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    return Process(original, name, out);
}

bool GusekAiImage::LoadImagesFromClipboard(HWND owner, std::vector<ChatImageAttachment> &out) {
    Initialize();
    bool opened = false;
    for (int i = 0; i < 10; ++i) {
        if (OpenClipboard(owner)) { opened = true; break; }
        Sleep(50);
    }
    if (!opened) return false;
    std::vector<std::wstring> files;
    HBITMAP copy = NULL;
    HDROP drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
    if (drop) {
        UINT count = (std::min)(16U, DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0));
        for (UINT i = 0; i < count; ++i) {
            UINT length = DragQueryFileW(drop, i, NULL, 0);
            std::vector<wchar_t> path(length + 1);
            if (DragQueryFileW(drop, i, &path[0], length + 1)) files.push_back(&path[0]);
        }
    } else {
        HBITMAP bitmap = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
        if (bitmap) copy = static_cast<HBITMAP>(CopyImage(bitmap, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
    }
    // Never hold the shared clipboard during decoding, scaling or encoding.
    CloseClipboard();
    for (size_t i = 0; i < files.size(); ++i) {
        ChatImageAttachment image;
        if (LoadImageFromFile(files[i], image)) out.push_back(image);
    }
    if (copy) {
        Bitmap original(copy, NULL);
        ChatImageAttachment image;
        if (Process(original, "Clipboard image", image)) out.push_back(image);
        DeleteObject(copy);
    }
    return !out.empty();
}

bool GusekAiImage::LoadImageFromClipboard(HWND owner, ChatImageAttachment &out) {
    std::vector<ChatImageAttachment> images;
    if (!LoadImagesFromClipboard(owner, images)) return false;
    out = images[0];
    for (size_t i = 1; i < images.size(); ++i) Release(images[i]);
    return true;
}

static LRESULT CALLBACK ViewerWndProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_CREATE:
        SetWindowLongPtr(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams));
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(window); return 0; }
        break;
    case WM_SIZE:
        InvalidateRect(window, NULL, TRUE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        HBITMAP bitmap = reinterpret_cast<HBITMAP>(GetWindowLongPtr(window, GWLP_USERDATA));
        if (bitmap) {
            BITMAP dimensions;
            GetObject(bitmap, sizeof(dimensions), &dimensions);
            RECT rc;
            GetClientRect(window, &rc);
            double scale = (std::min)(static_cast<double>(rc.right) / dimensions.bmWidth,
                                     static_cast<double>(rc.bottom) / dimensions.bmHeight);
            int width = (std::max)(1, static_cast<int>(dimensions.bmWidth * scale));
            int height = (std::max)(1, static_cast<int>(dimensions.bmHeight * scale));
            HDC memory = CreateCompatibleDC(dc);
            HGDIOBJ previous = SelectObject(memory, bitmap);
            SetStretchBltMode(dc, HALFTONE);
            StretchBlt(dc, (rc.right - width) / 2, (rc.bottom - height) / 2, width, height,
                memory, 0, 0, dimensions.bmWidth, dimensions.bmHeight, SRCCOPY);
            SelectObject(memory, previous);
            DeleteDC(memory);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_NCDESTROY:
        DeleteObject(reinterpret_cast<HBITMAP>(GetWindowLongPtr(window, GWLP_USERDATA)));
        SetWindowLongPtr(window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(window, message, wp, lp);
}

void GusekAiImage::ShowImageViewer(HWND parent, const ChatImageAttachment &image) {
    HBITMAP source = image.hPreview ? image.hPreview : image.hThumb;
    if (!source) return;
    HBITMAP copy = static_cast<HBITMAP>(CopyImage(source, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
    if (!copy) return;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = ViewerWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"GusekAiImageViewer";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    HWND viewer = CreateWindowExW(0, wc.lpszClassName, (L"Picture: " + Utf8ToWide(image.name)).c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 720, 540,
        parent, NULL, wc.hInstance, copy);
    if (!viewer) DeleteObject(copy);
}

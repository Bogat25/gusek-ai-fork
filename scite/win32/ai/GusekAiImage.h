#ifndef GUSEK_AI_IMAGE_H
#define GUSEK_AI_IMAGE_H

#include "GusekAiDef.h"
#include "GusekAiProtocol.h"

class GusekAiImage {
public:
    static void Initialize();
    static void Shutdown();
    static void Release(ChatImageAttachment &image);
    static bool ThumbnailPng(HBITMAP bitmap, std::vector<BYTE> &bytes);

    static bool LoadImageFromFile(const std::wstring &path, ChatImageAttachment &outAttach);
    static bool LoadImageFromClipboard(HWND hWndOwner, ChatImageAttachment &outAttach);
    static bool LoadImagesFromClipboard(HWND hWndOwner, std::vector<ChatImageAttachment> &out);
    static void ShowImageViewer(HWND hParent, const ChatImageAttachment &attach);
};

#endif // GUSEK_AI_IMAGE_H

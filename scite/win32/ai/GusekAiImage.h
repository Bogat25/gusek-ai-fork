#ifndef GUSEK_AI_IMAGE_H
#define GUSEK_AI_IMAGE_H

#include "GusekAiDef.h"
#include "GusekAiProtocol.h"

class GusekAiImage {
public:
    static void Initialize();
    static void Shutdown();

    static bool LoadImageFromFile(const std::wstring &path, ChatImageAttachment &outAttach);
    static bool LoadImageFromClipboard(HWND hWndOwner, ChatImageAttachment &outAttach);
    static void ShowImageViewer(HWND hParent, const ChatImageAttachment &attach);
};

#endif // GUSEK_AI_IMAGE_H

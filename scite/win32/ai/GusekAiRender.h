#ifndef GUSEK_AI_RENDER_H
#define GUSEK_AI_RENDER_H

#include "GusekAiDef.h"

class GusekAiRender {
public:
    static void SetupRichEdit(HWND hRichEdit);

    // Appends a sender heading ("You" or "GUSEK assistant")
    static void AppendHeader(HWND hRichEdit, const char *senderName, COLORREF color);

    // Appends plain text without formatting
    static void AppendPlainText(HWND hRichEdit, const char *u8Text);

    // Formats and appends/updates Markdown streaming buffer into the RichEdit control
    static void RenderMarkdownStream(
        HWND hRichEdit,
        const std::string &fullStreamedText,
        LONG turnStartPos,
        bool isFinal);

    // Renders thumbnail image into RichEdit transcript
    static void AppendImageThumbnail(HWND hRichEdit, const std::string &name, HBITMAP hThumb);

    // Checks if the RichEdit control is currently scrolled to the bottom
    static bool IsScrolledToBottom(HWND hRichEdit);

    // Scrolls RichEdit control to the very end
    static void ScrollToBottom(HWND hRichEdit);
};

#endif // GUSEK_AI_RENDER_H

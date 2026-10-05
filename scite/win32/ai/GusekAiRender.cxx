#include "GusekAiRender.h"
#include <sstream>

#define IMF_AUTOFONT 0x0002

void GusekAiRender::SetupRichEdit(HWND hRichEdit) {
    if (!hRichEdit) return;

    // Lesson 3: Disable automatic font binding
    LRESULT langOpt = SendMessage(hRichEdit, EM_GETLANGOPTIONS, 0, 0);
    SendMessage(hRichEdit, EM_SETLANGOPTIONS, 0, langOpt & ~IMF_AUTOFONT);

    // Set background color
    SendMessage(hRichEdit, EM_SETBKGNDCOLOR, 0, (LPARAM)RGB(255, 255, 255));

    // Default font: Segoe UI, 10pt (200 twips)
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_BACKCOLOR;
    cf.dwEffects = 0;
    cf.yHeight = 200; // 10 pt
    cf.crTextColor = RGB(30, 30, 30);
    cf.crBackColor = RGB(255, 255, 255);
    wcscpy(cf.szFaceName, L"Segoe UI");

    SendMessageW(hRichEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);

    // Word wrap
    SendMessage(hRichEdit, EM_SETTARGETDEVICE, 0, 0);
}

bool GusekAiRender::IsScrolledToBottom(HWND hRichEdit) {
    if (!hRichEdit) return true;
    SCROLLINFO si;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    if (!GetScrollInfo(hRichEdit, SB_VERT, &si)) return true;
    if (si.nMax <= 0 || si.nPage <= 0) return true;
    return (si.nPos + (int)si.nPage >= si.nMax - 15);
}

void GusekAiRender::ScrollToBottom(HWND hRichEdit) {
    if (!hRichEdit) return;
    SendMessage(hRichEdit, WM_VSCROLL, SB_BOTTOM, 0);
}

void GusekAiRender::AppendHeader(HWND hRichEdit, const char *senderName, COLORREF color) {
    if (!hRichEdit || !senderName) return;

    bool wasAtBottom = IsScrolledToBottom(hRichEdit);

    CHARRANGE crEnd = { -1, -1 };
    SendMessage(hRichEdit, EM_EXSETSEL, 0, (LPARAM)&crEnd);

    // Add newline if document is not empty
    GETTEXTLENGTHEX gtl = { GTL_NUMCHARS | GTL_PRECISE, 1200 };
    if (SendMessage(hRichEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0) > 0) {
        SendMessageW(hRichEdit, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n\r\n");
    }

    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_BACKCOLOR;
    cf.dwEffects = CFE_BOLD;
    cf.yHeight = 210; // 10.5 pt
    cf.crTextColor = color;
    cf.crBackColor = RGB(255, 255, 255);
    wcscpy(cf.szFaceName, L"Segoe UI");

    SendMessageW(hRichEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    std::wstring wSender = Utf8ToWide(senderName) + L"\r\n";
    SendMessageW(hRichEdit, EM_REPLACESEL, FALSE, (LPARAM)wSender.c_str());

    // Reset format to normal prose
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_BACKCOLOR;
    cf.dwEffects = 0;
    cf.yHeight = 200;
    cf.crTextColor = RGB(30, 30, 30);
    cf.crBackColor = RGB(255, 255, 255);
    wcscpy(cf.szFaceName, L"Segoe UI");
    SendMessageW(hRichEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    if (wasAtBottom) ScrollToBottom(hRichEdit);
}

void GusekAiRender::AppendPlainText(HWND hRichEdit, const char *u8Text) {
    if (!hRichEdit || !u8Text) return;
    bool wasAtBottom = IsScrolledToBottom(hRichEdit);
    CHARRANGE crEnd = { -1, -1 };
    SendMessage(hRichEdit, EM_EXSETSEL, 0, (LPARAM)&crEnd);
    std::wstring w = Utf8ToWide(u8Text);
    SendMessageW(hRichEdit, EM_REPLACESEL, FALSE, (LPARAM)w.c_str());
    if (wasAtBottom) ScrollToBottom(hRichEdit);
}

// Convert markdown text to RTF
static std::string MarkdownToRtf(const std::string &md) {
    std::string rtf = "{\\rtf1\\ansi\\deff0\\nouicompat";
    rtf += "{\\fonttbl{\\f0\\fnil\\fcharset0 Segoe UI;}{\\f1\\fnil\\fcharset0 Consolas;}{\\f2\\fnil\\fcharset0 Segoe UI Emoji;}}";
    rtf += "{\\colortbl ;\\red30\\green30\\blue30;\\red0\\green32\\blue128;\\red160\\green0\\blue0;\\red244\\green244\\blue244;\\red90\\green90\\blue90;}";
    rtf += "\\viewkind4\\uc1\\pard\\f0\\fs20\\cf1 ";

    bool inCodeBlock = false;
    bool inInlineCode = false;
    bool inBold = false;
    bool inItalic = false;

    size_t i = 0;
    while (i < md.length()) {
        // 1. Fenced code block (```)
        if (i + 2 < md.length() && md[i] == '`' && md[i+1] == '`' && md[i+2] == '`') {
            if (!inCodeBlock) {
                // Enter code block: skip language identifier on same line
                i += 3;
                while (i < md.length() && md[i] != '\n') i++;
                if (i < md.length() && md[i] == '\n') i++;
                inCodeBlock = true;
                rtf += "\\par\\pard\\li360\\ri360\\f1\\fs19\\highlight4\\cf0 ";
            } else {
                // Exit code block
                i += 3;
                while (i < md.length() && (md[i] == ' ' || md[i] == '\t' || md[i] == '\r')) i++;
                if (i < md.length() && md[i] == '\n') i++;
                inCodeBlock = false;
                rtf += "\\par\\pard\\f0\\fs20\\highlight0\\cf1 ";
            }
            continue;
        }

        // Inside code block: verbatim text with formatting escapes
        if (inCodeBlock) {
            char c = md[i++];
            if (c == '\\') rtf += "\\\\";
            else if (c == '{') rtf += "\\{";
            else if (c == '}') rtf += "\\}";
            else if (c == '\r') continue;
            else if (c == '\n') rtf += "\\par\n";
            else if (c == '\t') rtf += "\\tab ";
            else if ((unsigned char)c < 128) rtf += c;
            else {
                // Multi-byte UTF-8
                i--;
                int len = 1;
                unsigned char uc = (unsigned char)md[i];
                if ((uc & 0xE0) == 0xC0) len = 2;
                else if ((uc & 0xF0) == 0xE0) len = 3;
                else if ((uc & 0xF8) == 0xF0) len = 4;
                if (i + len <= md.length()) {
                    std::string u8c = md.substr(i, len);
                    std::wstring wc = Utf8ToWide(u8c);
                    for (size_t wi = 0; wi < wc.length(); wi++) {
                        short cp = (short)wc[wi];
                        rtf += "\\u" + std::to_string(cp) + "?";
                    }
                    i += len;
                } else {
                    i++;
                }
            }
            continue;
        }

        // 2. Inline code (`...`)
        if (md[i] == '`') {
            i++;
            if (!inInlineCode) {
                inInlineCode = true;
                rtf += " \\f1\\fs19\\highlight4\\cf0 ";
            } else {
                inInlineCode = false;
                rtf += "\\highlight0\\f0\\fs20\\cf1  ";
            }
            continue;
        }

        // 3. Bold (**word**)
        if (i + 1 < md.length() && md[i] == '*' && md[i+1] == '*') {
            i += 2;
            inBold = !inBold;
            rtf += inBold ? "\\b " : "\\b0 ";
            continue;
        }

        // 4. Italic (*word*) - only around letters (lesson: 2 * 3 stays arithmetic)
        if (md[i] == '*' && !inBold) {
            bool prevIsSpace = (i == 0 || md[i-1] == ' ' || md[i-1] == '\n' || md[i-1] == '(');
            bool nextIsAlpha = (i + 1 < md.length() && isalpha((unsigned char)md[i+1]));
            bool prevIsAlpha = (i > 0 && isalpha((unsigned char)md[i-1]));
            bool nextIsSpace = (i + 1 == md.length() || md[i+1] == ' ' || md[i+1] == '\n' || md[i+1] == ')');

            if (!inItalic && prevIsSpace && nextIsAlpha) {
                inItalic = true;
                rtf += "\\i ";
                i++;
                continue;
            } else if (inItalic && prevIsAlpha && nextIsSpace) {
                inItalic = false;
                rtf += "\\i0 ";
                i++;
                continue;
            }
        }

        // 5. Line break / bullet / header
        if (md[i] == '\n') {
            rtf += "\\par\n";
            i++;
            // Check for bullets "- " or "* "
            if (i + 1 < md.length() && (md[i] == '-' || md[i] == '*') && md[i+1] == ' ') {
                rtf += "\\bullet  ";
                i += 2;
            }
            // Check for header "# "
            else if (i < md.length() && md[i] == '#') {
                while (i < md.length() && md[i] == '#') i++;
                while (i < md.length() && md[i] == ' ') i++;
                rtf += "\\b ";
            }
            continue;
        }

        char c = md[i++];
        if (c == '\\') rtf += "\\\\";
        else if (c == '{') rtf += "\\{";
        else if (c == '}') rtf += "\\}";
        else if (c == '\r') continue;
        else if (c == '\t') rtf += "\\tab ";
        else if ((unsigned char)c < 128) rtf += c;
        else {
            i--;
            int len = 1;
            unsigned char uc = (unsigned char)md[i];
            if ((uc & 0xE0) == 0xC0) len = 2;
            else if ((uc & 0xF0) == 0xE0) len = 3;
            else if ((uc & 0xF8) == 0xF0) len = 4;
            if (i + len <= md.length()) {
                std::string u8c = md.substr(i, len);
                std::wstring wc = Utf8ToWide(u8c);
                for (size_t wi = 0; wi < wc.length(); wi++) {
                    short cp = (short)wc[wi];
                    rtf += "\\u" + std::to_string(cp) + "?";
                }
                i += len;
            } else {
                i++;
            }
        }
    }

    if (inCodeBlock) rtf += "\\highlight0\\f0\\fs20\\cf1 ";
    if (inBold) rtf += "\\b0 ";
    if (inItalic) rtf += "\\i0 ";

    rtf += "}";
    return rtf;
}

struct StreamInContext {
    const char *data;
    size_t len;
    size_t offset;
};

static DWORD CALLBACK StreamInCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb) {
    StreamInContext *ctx = (StreamInContext *)dwCookie;
    if (ctx->offset >= ctx->len) {
        *pcb = 0;
        return 0;
    }
    LONG toCopy = (LONG)(ctx->len - ctx->offset);
    if (toCopy > cb) toCopy = cb;
    memcpy(pbBuff, ctx->data + ctx->offset, toCopy);
    ctx->offset += toCopy;
    *pcb = toCopy;
    return 0;
}

void GusekAiRender::RenderMarkdownStream(
    HWND hRichEdit,
    const std::string &fullStreamedText,
    LONG turnStartPos,
    bool isFinal)
{
    if (!hRichEdit) return;

    bool wasAtBottom = IsScrolledToBottom(hRichEdit);

    // Save user selection
    CHARRANGE userSel;
    SendMessage(hRichEdit, EM_EXGETSEL, 0, (LPARAM)&userSel);

    // Freeze redrawing to prevent flicker
    SendMessage(hRichEdit, WM_SETREDRAW, FALSE, 0);

    // Select the turn text range
    CHARRANGE turnRange = { turnStartPos, -1 };
    SendMessage(hRichEdit, EM_EXSETSEL, 0, (LPARAM)&turnRange);

    std::string rtf = MarkdownToRtf(fullStreamedText);

    StreamInContext ctx = { rtf.c_str(), rtf.length(), 0 };
    EDITSTREAM es = { (DWORD_PTR)&ctx, 0, StreamInCallback };
    SendMessage(hRichEdit, EM_STREAMIN, SF_RTF | SFF_SELECTION, (LPARAM)&es);

    // Restore selection if user had selected text
    if (userSel.cpMin != userSel.cpMax && userSel.cpMin < turnStartPos) {
        SendMessage(hRichEdit, EM_EXSETSEL, 0, (LPARAM)&userSel);
    }

    // Unfreeze redrawing
    SendMessage(hRichEdit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hRichEdit, NULL, TRUE);

    if (wasAtBottom) {
        ScrollToBottom(hRichEdit);
    }
}

void GusekAiRender::AppendImageThumbnail(HWND hRichEdit, const std::string &name, HBITMAP hThumb) {
    if (!hRichEdit) return;
    AppendPlainText(hRichEdit, ("[Picture attached: " + name + "]\r\n").c_str());
}

#ifndef GUSEK_AI_DEF_H
#define GUSEK_AI_DEF_H

#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS 1
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <richedit.h>
#include <commctrl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <algorithm>

#ifndef IDM_AIASSISTANT
#define IDM_AIASSISTANT 470
#endif

// Windows Messages for AI worker -> UI communication
#define WM_AI_DATA              (WM_USER + 101)  // Text chunk arrived in pending buffer
#define WM_AI_STATUS            (WM_USER + 102)  // lParam: malloc'd UTF-8 status string
#define WM_AI_DONE              (WM_USER + 103)  // wParam: 1 for success, 0 for error/cancelled
#define WM_AI_DLDONE            (WM_USER + 104)  // wParam: 1 for success, 0 for error/cancelled
#define WM_AI_OFFER_DOWNLOAD    (WM_USER + 105)  // Offer model download dialog

// Control IDs inside GusekAiPane
#define IDC_AI_STATUS           3101
#define IDC_AI_HIST             3102
#define IDC_AI_STRIP            3103
#define IDC_AI_INPUT            3104
#define IDC_AI_SEND             3105
#define IDC_AI_STOP             3106
#define IDC_AI_COPY             3107
#define IDC_AI_TOED             3108
#define IDC_AI_NEW              3109
#define IDC_AI_ATTACH_BTN       3110

// Attach Menu IDs
#define IDM_AI_ATTACH_ERR       3201
#define IDM_AI_ATTACH_OUT       3202
#define IDM_AI_ATTACH_DOC       3203
#define IDM_AI_ATTACH_RECENT    3204
#define IDM_AI_ATTACH_FILE      3205
#define IDM_AI_ATTACH_CLIP      3206
#define IDM_AI_ATTACH_CLEAR     3207

// ---------------------------------------------------------------------
// Safe growable byte buffer (dynbuf)
// ---------------------------------------------------------------------
struct dynbuf {
    char  *s;
    size_t n;
    size_t cap;
};

inline void db_init(dynbuf *b) {
    b->s = NULL;
    b->n = 0;
    b->cap = 0;
}

inline int db_reserve(dynbuf *b, size_t extra) {
    const size_t half = ((size_t)-1) / 4;
    if (extra > half || b->n > half) return 0;
    size_t need = b->n + extra + 1;
    if (need <= b->cap) return 1;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < need) {
        if (cap > ((size_t)1 << 28)) { cap = need; break; }
        cap *= 2;
    }
    char *p = (char *)realloc(b->s, cap);
    if (!p) return 0;
    b->s = p;
    b->cap = cap;
    return 1;
}

inline int db_addn(dynbuf *b, const char *s, size_t n) {
    if (!n) return 1;
    if (!db_reserve(b, n)) return 0;
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = '\0';
    return 1;
}

inline int db_add(dynbuf *b, const char *s) {
    return s ? db_addn(b, s, strlen(s)) : 1;
}

inline int db_addc(dynbuf *b, char c) {
    return db_addn(b, &c, 1);
}

inline void db_free(dynbuf *b) {
    free(b->s);
    db_init(b);
}

inline char *db_detach(dynbuf *b) {
    char *p = b->s;
    db_init(b);
    return p;
}

// ---------------------------------------------------------------------
// Unicode conversion helpers
// ---------------------------------------------------------------------
inline std::wstring Utf8ToWide(const std::string &u8) {
    if (u8.empty()) return std::wstring();
    int needed = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), (int)u8.length(), NULL, 0);
    if (needed <= 0) return std::wstring();
    std::wstring w(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), (int)u8.length(), &w[0], needed);
    return w;
}

inline std::string WideToUtf8(const std::wstring &w) {
    if (w.empty()) return std::string();
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.length(), NULL, 0, NULL, NULL);
    if (needed <= 0) return std::string();
    std::string u8(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.length(), &u8[0], needed, NULL, NULL);
    return u8;
}

inline wchar_t *u8_to_wcs(const char *u8) {
    if (!u8) return NULL;
    int len = MultiByteToWideChar(CP_UTF8, 0, u8, -1, NULL, 0);
    if (len <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc(len * sizeof(wchar_t));
    if (w) MultiByteToWideChar(CP_UTF8, 0, u8, -1, w, len);
    return w;
}

inline char *wcs_to_u8(const wchar_t *w) {
    if (!w) return NULL;
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    char *u8 = (char *)malloc(len);
    if (u8) WideCharToMultiByte(CP_UTF8, 0, w, -1, u8, len, NULL, NULL);
    return u8;
}

#endif // GUSEK_AI_DEF_H

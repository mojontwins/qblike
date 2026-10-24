/* clip.c - system clipboard (text is CP437 inside QBLike) */
#include "qb.h"
#include <stdlib.h>
#include <string.h>

int cp437_to_unicode(int c);

#ifdef _WIN32
#include <windows.h>
void sys_clip_set(const char *s) {
    int n = (int)strlen(s);
    /* count line breaks to add CR */
    int extra = 0;
    for (int i = 0; i < n; i++) if (s[i] == '\n') extra++;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, sizeof(WCHAR) * (size_t)(n + extra + 1));
    if (!h) return;
    WCHAR *w = (WCHAR *)GlobalLock(h);
    int o = 0;
    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n') w[o++] = '\r';
        w[o++] = (WCHAR)cp437_to_unicode(c);
    }
    w[o] = 0;
    GlobalUnlock(h);
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        SetClipboardData(CF_UNICODETEXT, h);
        CloseClipboard();
    } else GlobalFree(h);
}
char *sys_clip_get(void) {
    if (!OpenClipboard(NULL)) return NULL;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) { CloseClipboard(); return NULL; }
    const WCHAR *w = (const WCHAR *)GlobalLock(h);
    if (!w) { CloseClipboard(); return NULL; }
    int n = 0;
    while (w[n]) n++;
    char *out = (char *)qb_alloc((size_t)n + 1);
    int o = 0;
    for (int i = 0; i < n; i++) {
        if (w[i] == '\r') continue;
        if (w[i] == '\t') { out[o++] = ' '; continue; }
        out[o++] = (char)unicode_to_cp437(w[i]);
    }
    out[o] = 0;
    GlobalUnlock(h);
    CloseClipboard();
    return out;
}
#else
#include <SDL.h>
static int clip_ok(void) {
    const char *e = getenv("QBLIKE_NOCLIP");
    return !(e && *e) && SDL_WasInit(SDL_INIT_VIDEO);
}
void sys_clip_set(const char *s) {
    if (!clip_ok()) return;
    int n = (int)strlen(s);
    char *u = (char *)qb_alloc((size_t)n * 3 + 1);
    int o = 0;
    for (int i = 0; i < n; i++) {
        int cp = cp437_to_unicode((unsigned char)s[i]);
        if (s[i] == '\n') cp = '\n';
        if (cp < 0x80) u[o++] = (char)cp;
        else if (cp < 0x800) { u[o++] = (char)(0xC0 | (cp >> 6)); u[o++] = (char)(0x80 | (cp & 0x3F)); }
        else { u[o++] = (char)(0xE0 | (cp >> 12)); u[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); u[o++] = (char)(0x80 | (cp & 0x3F)); }
    }
    u[o] = 0;
    SDL_SetClipboardText(u);
    free(u);
}
char *sys_clip_get(void) {
    if (!clip_ok() || !SDL_HasClipboardText()) return NULL;
    char *u = SDL_GetClipboardText();
    if (!u) return NULL;
    int n = (int)strlen(u);
    char *out = (char *)qb_alloc((size_t)n + 1);
    int o = 0;
    for (int i = 0; i < n;) {
        unsigned char c = (unsigned char)u[i];
        int cp, len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < n) { cp = ((c & 0x1F) << 6) | (u[i + 1] & 0x3F); len = 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < n) { cp = ((c & 0x0F) << 12) | ((u[i + 1] & 0x3F) << 6) | (u[i + 2] & 0x3F); len = 3; }
        else if ((c & 0xF8) == 0xF0 && i + 3 < n) { cp = '?'; len = 4; }
        else { cp = '?'; len = 1; }
        i += len;
        if (cp == '\r') continue;
        if (cp == '\t') { out[o++] = ' '; continue; }
        out[o++] = (char)(cp == '\n' ? '\n' : unicode_to_cp437(cp));
    }
    out[o] = 0;
    SDL_free(u);
    return out;
}
#endif

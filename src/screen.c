/* screen.c - QBasic program screen (text and graphics modes), keyboard, system time */
#include "qb.h"
#include "doslike/dos.h"
#include "fonts.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
extern int g_mouse_cursor;
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

/* ------------------------------------------------------------------ */
/* system                                                             */
/* ------------------------------------------------------------------ */
double sys_time(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER c;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}
void sys_sleep(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}
double sys_timer(void) {
#ifdef _WIN32
    SYSTEMTIME st;
    GetLocalTime(&st);
    return st.wHour * 3600.0 + st.wMinute * 60.0 + st.wSecond + st.wMilliseconds / 1000.0;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t t = tv.tv_sec;
    struct tm lt;
    localtime_r(&t, &lt);
    return lt.tm_hour * 3600.0 + lt.tm_min * 60.0 + lt.tm_sec + tv.tv_usec / 1e6;
#endif
}
void sys_datestr(char *buf) {
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    sprintf(buf, "%02d-%02d-%04d", lt->tm_mon + 1, lt->tm_mday, lt->tm_year + 1900);
}
void sys_timestr(char *buf) {
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    sprintf(buf, "%02d:%02d:%02d", lt->tm_hour, lt->tm_min, lt->tm_sec);
}
int sys_shutting_down(void) { return shuttingdown(); }

static int idle_count = 0;
static double idle_t = 0;
void sys_idle(void) {
    /* called on empty INKEY$: avoid burning a full core in polling loops */
    if (++idle_count > 2000) {
        double t = sys_time();
        if (t - idle_t < 0.02) sys_sleep(1);
        idle_t = t;
        idle_count = 0;
    }
}

/* ------------------------------------------------------------------ */
/* fonts                                                              */
/* ------------------------------------------------------------------ */
static uint8_t glyph8[256][8];
static uint8_t glyph16[256][16];
static int fonts_ready = 0;

static void decode_font(const uint32_t *font, uint8_t *out, int outh) {
    int w = (int)font[0], h = (int)font[1];
    const uint32_t *data = font + 3;
    int mod = 256 / w;
    for (int c = 0; c < 256; c++) {
        int sx = (c % mod) * w, sy = (c / mod) * h;
        for (int y = 0; y < h && y < outh; y++) {
            uint8_t row = 0;
            for (int x = 0; x < 8; x++) {
                int v = (sx + x) / 32, u = (sx + x) % 32;
                uint32_t b = data[v + (sy + y) * 8];
                if (b & (1u << u)) row |= (uint8_t)(0x80 >> x);
            }
            out[c * outh + y] = row;
        }
    }
}
static void init_fonts(void) {
    if (fonts_ready) return;
    decode_font(qb_font8x8, &glyph8[0][0], 8);
    decode_font(qb_font8x16, &glyph16[0][0], 16);
    fonts_ready = 1;
}

/* ------------------------------------------------------------------ */
/* screen state                                                       */
/* ------------------------------------------------------------------ */
typedef struct ModeInfo {
    int mode, w, h, cols, rows, cw, ch, colors, pages;
    enum videomode_t vm;
} ModeInfo;

static const ModeInfo modes[] = {
    { 0, 0, 0, 80, 25, 9, 16, 16, 8, videomode_80x25_9x16 },
    { 1, 320, 200, 40, 25, 8, 8, 4, 1, videomode_320x200 },
    { 2, 640, 200, 80, 25, 8, 8, 2, 1, videomode_640x200 },
    { 7, 320, 200, 40, 25, 8, 8, 16, 8, videomode_320x200 },
    { 8, 640, 200, 80, 25, 8, 8, 16, 4, videomode_640x200 },
    { 9, 640, 350, 80, 25, 8, 14, 16, 2, videomode_640x350 },
    { 10, 640, 350, 80, 25, 8, 14, 4, 2, videomode_640x350 },
    { 11, 640, 480, 80, 30, 8, 16, 2, 1, videomode_640x480 },
    { 12, 640, 480, 80, 30, 8, 16, 16, 1, videomode_640x480 },
    { 13, 320, 200, 40, 25, 8, 8, 256, 1, videomode_320x200 },
    { -1, 0, 0, 0, 0, 0, 0, 0, 0, videomode_80x25_9x16 }
};

typedef struct Scr {
    int mode;
    const ModeInfo *mi;
    int w, h, cols, rows, cw, ch, colors, npages;
    int apage, vpage;
    uint16_t *text[8];      /* mode 0: char | attr << 8 */
    uint8_t *pix[8];        /* graphics pages */
    uint8_t *gtext[8];      /* graphics mode char memory: char per cell */
    int cx, cy;             /* cursor 0-based, cx may equal cols (pending wrap) */
    int fg, bg, border;
    int vtop, vbot;
    int curvis, curstart, curstop;
    uint8_t rgb[256][3];    /* 6-bit DAC values */
    int attrpal[16];        /* attribute -> DAC index for 16-color modes */
    int scrolls;
    /* graphics */
    double lastx, lasty;
    int vx1, vy1, vx2, vy2, vscreen, hasview;
    double wx1, wy1, wx2, wy2;
    int haswin, winscreen;
    int drawangle, drawscale, drawcolor;
    double drawta;
    int dac_write_idx, dac_write_comp, dac_read_idx, dac_read_comp;
    int textmode_vm;
    int clx1, cly1, clx2, cly2;   /* CLIP rectangle (absolute screen coordinates) for PUT */
} Scr;

static Scr S;
static int display_owner_ide = 0;

/* default EGA attribute palette: indices into the 64-color EGA space */
static const int ega_default[16] = { 0, 1, 2, 3, 4, 5, 20, 7, 56, 57, 58, 59, 60, 61, 62, 63 };

static void ega_rgb(int c, uint8_t *r, uint8_t *g, uint8_t *b) {
    *b = (uint8_t)(((c >> 0) & 1) * 42 + ((c >> 3) & 1) * 21);
    *g = (uint8_t)(((c >> 1) & 1) * 42 + ((c >> 4) & 1) * 21);
    *r = (uint8_t)(((c >> 2) & 1) * 42 + ((c >> 5) & 1) * 21);
}

static void default_palette(void) {
    /* VGA default 256 palette from dos-like (0xBBGGRR, 8-bit) */
    for (int i = 0; i < 256; i++) {
        uint32_t c = qb_default_palette[i];
        S.rgb[i][0] = (uint8_t)(((c >> 0) & 0xFF) >> 2);
        S.rgb[i][1] = (uint8_t)(((c >> 8) & 0xFF) >> 2);
        S.rgb[i][2] = (uint8_t)(((c >> 16) & 0xFF) >> 2);
    }
    /* for 16-color modes use EGA colors in DAC 0..15 via attribute mapping */
    for (int i = 0; i < 16; i++) {
        uint8_t r, g, b;
        ega_rgb(ega_default[i], &r, &g, &b);
        S.rgb[i][0] = r; S.rgb[i][1] = g; S.rgb[i][2] = b;
        S.attrpal[i] = i;
    }
    if (S.mode == 1) {
        /* CGA palette 1: black, cyan, magenta, white */
        static const int cga[4] = { 0, 3, 5, 15 };
        for (int i = 0; i < 4; i++) {
            uint8_t r, g, b; ega_rgb(ega_default[cga[i]], &r, &g, &b);
            S.rgb[i][0] = r; S.rgb[i][1] = g; S.rgb[i][2] = b;
        }
    } else if (S.mode == 2 || S.mode == 11) {
        S.rgb[1][0] = S.rgb[1][1] = S.rgb[1][2] = 63;
    } else if (S.mode == 10) {
        S.rgb[1][0] = S.rgb[1][1] = S.rgb[1][2] = 21;
        S.rgb[2][0] = S.rgb[2][1] = S.rgb[2][2] = 42;
        S.rgb[3][0] = S.rgb[3][1] = S.rgb[3][2] = 63;
    }
}

static void free_buffers(void) {
    for (int i = 0; i < 8; i++) {
        free(S.text[i]); S.text[i] = NULL;
        free(S.pix[i]); S.pix[i] = NULL;
        free(S.gtext[i]); S.gtext[i] = NULL;
    }
}

static int text_attr(void) {
    int fg = S.fg & 15, bg = S.bg & 7;
    int blink = (S.fg & 16) ? 0x80 : 0;
    return fg | (bg << 4) | blink;
}

static void clear_text_rows(int page, int top, int bot) {
    uint16_t fill = (uint16_t)(' ' | (text_attr() << 8));
    if (S.mode == 0) {
        for (int y = top; y <= bot; y++)
            for (int x = 0; x < S.cols; x++) S.text[page][y * S.cols + x] = fill;
    } else {
        int bgc = (S.mode == 13 || S.mode == 12 || S.mode == 11 || S.mode == 2 || S.mode == 1) ? 0 : 0;
        for (int y = top * S.ch; y < (bot + 1) * S.ch && y < S.h; y++) memset(S.pix[page] + y * S.w, bgc, (size_t)S.w);
        for (int y = top; y <= bot; y++) memset(S.gtext[page] + y * S.cols, ' ', (size_t)S.cols);
    }
}

/* emulated EGA/VGA registers used for direct access to planar video memory */
static int vga_seq_idx = 0, vga_map_mask = 0x0F;   /* &H3C4/&H3C5, index 2 = map mask (planes written) */
static int vga_gc_idx = 0, vga_read_map = 0;       /* &H3CE/&H3CF, index 4 = read map select (plane read) */

static void setup_mode(int mode, int cols, int rows) {
    vga_seq_idx = 0; vga_map_mask = 0x0F; vga_gc_idx = 0; vga_read_map = 0;
    const ModeInfo *mi = NULL;
    for (int i = 0; modes[i].mode >= 0; i++) if (modes[i].mode == mode) mi = &modes[i];
    if (!mi) rt_error(5);
    free_buffers();
    S.mode = mode;
    S.mi = mi;
    S.w = mi->w; S.h = mi->h;
    S.cols = cols > 0 ? cols : mi->cols;
    S.rows = rows > 0 ? rows : mi->rows;
    S.cw = mi->cw; S.ch = mi->ch;
    S.colors = mi->colors;
    S.npages = mi->pages;
    if (mode == 0) {
        if (S.cols == 40 && S.rows == 25) { S.textmode_vm = videomode_40x25_9x16; S.cw = 9; S.ch = 16; }
        else if (S.rows == 43) { S.textmode_vm = videomode_80x43_8x8; S.cw = 8; S.ch = 8; }
        else if (S.rows == 50) { S.textmode_vm = videomode_80x50_8x8; S.cw = 8; S.ch = 8; }
        else { S.textmode_vm = videomode_80x25_9x16; S.cw = 9; S.ch = 16; }
        if (S.cols == 40) S.npages = 8;
        for (int p = 0; p < S.npages; p++) S.text[p] = (uint16_t *)qb_alloc(sizeof(uint16_t) * (size_t)(S.cols * S.rows));
    } else {
        if (mode == 12 || mode == 11) { if (S.rows == 60) S.ch = 8; else { S.rows = 30; S.ch = 16; } }
        if (mode == 9 || mode == 10) { if (S.rows == 43) S.ch = 8; else { S.rows = 25; S.ch = 14; } }
        S.cols = S.w / S.cw;
        for (int p = 0; p < S.npages; p++) {
            S.pix[p] = (uint8_t *)qb_alloc((size_t)(S.w * S.h));
            S.gtext[p] = (uint8_t *)qb_alloc((size_t)(S.cols * S.rows));
            memset(S.gtext[p], ' ', (size_t)(S.cols * S.rows));
        }
    }
    S.apage = S.vpage = 0;
    S.cx = S.cy = 0;
    S.fg = (mode == 0) ? 7 : (S.colors == 256 ? 15 : S.colors - 1);
    S.bg = 0;
    S.vtop = 0;
    S.vbot = S.rows - 2;
    if (S.vbot < 0) S.vbot = 0;
    S.curvis = 0;
    S.hasview = 0; S.haswin = 0;
    S.vx1 = 0; S.vy1 = 0; S.vx2 = S.w - 1; S.vy2 = S.h - 1;
    S.clx1 = 0; S.cly1 = 0; S.clx2 = S.w - 1; S.cly2 = S.h - 1;
    S.lastx = S.w / 2; S.lasty = S.h / 2;
    S.drawangle = 0; S.drawscale = 4; S.drawta = 0; S.drawcolor = -1;
    default_palette();
    for (int p = 0; p < S.npages; p++) {
        int sa = S.apage; S.apage = p;
        clear_text_rows(p, 0, S.rows - 1);
        S.apage = sa;
    }
}

void scr_init(void) {
    init_fonts();
    memset(&S, 0, sizeof S);
    setup_mode(0, 80, 25);
}
void scr_reset(void) {
    g_mouse_cursor = 0;
    /* QBasic keeps the output screen between runs but resets mode to text */
    if (S.mode != 0 || S.cols != 80 || S.rows != 25) setup_mode(0, 80, 25);
    S.fg = 7; S.bg = 0;
    S.vtop = 0; S.vbot = S.rows - 2;
    S.curvis = 0;
    S.apage = S.vpage = 0;
    default_palette();
}
int scr_mode(void) { return S.mode; }
int scr_cols(void) { return S.cols; }
int scr_rows(void) { return S.rows; }

void scr_setmode(int mode, int colorsw, int apage, int vpage, int setap, int setvp) {
    (void)colorsw;
    int valid = 0;
    for (int i = 0; modes[i].mode >= 0; i++) if (modes[i].mode == mode) valid = 1;
    if (!valid) rt_error(5);
    if (mode != S.mode) setup_mode(mode, mode == 0 ? 80 : 0, mode == 0 ? 25 : 0);
    else if (!setap && !setvp) {
        /* SCREEN with same mode: clears screen in QBasic only when mode changes */
    }
    if (setap) { if (apage < 0 || apage >= S.npages) rt_error(5); S.apage = apage; }
    if (setvp) { if (vpage < 0 || vpage >= S.npages) rt_error(5); S.vpage = vpage; }
    else if (setap && !setvp) { /* visual page unchanged */ }
}

void scr_width(int cols, int rows) {
    if (cols < 0) cols = S.cols;
    if (rows < 0) rows = S.rows;
    if (S.mode == 0) {
        if (cols != 40 && cols != 80) rt_error(5);
        if (rows != 25 && rows != 43 && rows != 50) rt_error(5);
        setup_mode(0, cols, rows);
    } else if (S.mode == 12 || S.mode == 11) {
        if (rows != 30 && rows != 60) rt_error(5);
        setup_mode(S.mode, 0, rows);
    } else if (S.mode == 9 || S.mode == 10) {
        if (rows != 25 && rows != 43) rt_error(5);
        setup_mode(S.mode, 0, rows);
    } else if (S.mode == 13 || S.mode == 1 || S.mode == 7) {
        if (cols != 40) rt_error(5);
    } else {
        if (cols != 80) rt_error(5);
    }
}

/* ------------------------------------------------------------------ */
/* text output                                                        */
/* ------------------------------------------------------------------ */
static void scroll_up(void) {
    int p = S.apage;
    if (S.mode == 0) {
        memmove(&S.text[p][S.vtop * S.cols], &S.text[p][(S.vtop + 1) * S.cols], sizeof(uint16_t) * (size_t)(S.cols * (S.vbot - S.vtop)));
    } else {
        int y0 = S.vtop * S.ch, y1 = (S.vbot + 1) * S.ch;
        memmove(S.pix[p] + y0 * S.w, S.pix[p] + (y0 + S.ch) * S.w, (size_t)(S.w * (y1 - y0 - S.ch)));
        memmove(S.gtext[p] + S.vtop * S.cols, S.gtext[p] + (S.vtop + 1) * S.cols, (size_t)(S.cols * (S.vbot - S.vtop)));
    }
    clear_text_rows(p, S.vbot, S.vbot);
    S.scrolls++;
}

static void put_char_at(int x, int y, unsigned char c) {
    int p = S.apage;
    if (x < 0 || y < 0 || x >= S.cols || y >= S.rows) return;
    if (S.mode == 0) {
        S.text[p][y * S.cols + x] = (uint16_t)(c | (text_attr() << 8));
        return;
    }
    S.gtext[p][y * S.cols + x] = c;
    int fg = S.fg, bg = 0;
    if (S.colors == 4 && fg > 3) fg = 3;
    if (S.colors == 2 && fg > 1) fg = 1;
    if (S.colors == 16) fg &= 15;
    const uint8_t *gl;
    int gh = S.ch;
    if (gh == 8) gl = glyph8[c];
    else gl = glyph16[c];
    int off = (gh == 14) ? 1 : 0;
    for (int yy = 0; yy < gh; yy++) {
        int py = y * S.ch + yy;
        if (py >= S.h) break;
        uint8_t row = gl[yy + off];
        uint8_t *dst = S.pix[p] + py * S.w + x * S.cw;
        for (int xx = 0; xx < 8; xx++) dst[xx] = (row & (0x80 >> xx)) ? (uint8_t)fg : (uint8_t)bg;
    }
}

static void newline_internal(void) {
    S.cx = 0;
    if (S.cy >= S.vtop && S.cy <= S.vbot) {
        if (S.cy == S.vbot) scroll_up();
        else S.cy++;
    } else if (S.cy > S.vbot) {
        /* printing on the reserved bottom line: scroll the view region */
        if (S.cy < S.rows - 1) S.cy++;
        else { scroll_up(); S.cy = S.vbot; }
    } else {
        S.cy++;
    }
}

static int echo_mode = -1;
void scr_print(const char *s, int len) {
    if (echo_mode < 0) echo_mode = getenv("QBLIKE_ECHO") != NULL;
    if (echo_mode) fwrite(s, 1, (size_t)len, stderr);
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (S.cx >= S.cols) newline_internal();
        if (c == 13) { newline_internal(); continue; }
        if (c == 10 && 0) { continue; }
        if (c == 7) { snd_beep(); continue; }
        if (c == 28 && 0) {}
        put_char_at(S.cx, S.cy, c);
        S.cx++;
    }
}
void scr_newline(void) { if (echo_mode > 0) fputc('\n', stderr); newline_internal(); }

int scr_text_fits(int len) {
    if (S.cx == 0) return 1;
    return S.cx + len <= S.cols;
}

void scr_comma(void) {
    int col = S.cx;
    int next = (col / 14 + 1) * 14;
    if (next >= S.cols) { newline_internal(); return; }
    while (S.cx < next) { put_char_at(S.cx, S.cy, ' '); S.cx++; }
}
void scr_tab(int col) {
    col--;
    if (col >= S.cols) col = col % S.cols;
    if (S.cx > col) newline_internal();
    while (S.cx < col) { put_char_at(S.cx, S.cy, ' '); S.cx++; }
}
void scr_spc(int n) {
    n = n % S.cols;
    for (int i = 0; i < n; i++) {
        if (S.cx >= S.cols) newline_internal();
        put_char_at(S.cx, S.cy, ' ');
        S.cx++;
    }
}

int scr_csrlin(void) { return S.cy + 1; }
int scr_pos(void) { return (S.cx >= S.cols ? S.cols : S.cx) + 1; }

void scr_cls(int what) {
    if (what == -1) {
        if (S.mode == 0) what = 0;
        else what = S.hasview ? 1 : 0;
    }
    if (what == 0 || (what == 2)) {
        if (what == 0) {
            if (S.mode == 0) clear_text_rows(S.apage, 0, S.rows - 1);
            else {
                memset(S.pix[S.apage], 0, (size_t)(S.w * S.h));
                memset(S.gtext[S.apage], ' ', (size_t)(S.cols * S.rows));
            }
        } else {
            clear_text_rows(S.apage, S.vtop, S.vbot);
        }
        S.cx = 0;
        S.cy = S.vtop;
    }
    if (what == 1 && S.mode != 0) {
        for (int y = S.vy1; y <= S.vy2; y++) memset(S.pix[S.apage] + y * S.w + S.vx1, 0, (size_t)(S.vx2 - S.vx1 + 1));
    }
    if (S.mode != 0) {
        /* graphics cursor to center of viewport */
        S.lastx = (S.vx1 + S.vx2) / 2.0;
        S.lasty = (S.vy1 + S.vy2) / 2.0;
        if (S.haswin) { S.lastx = (S.wx1 + S.wx2) / 2; S.lasty = (S.wy1 + S.wy2) / 2; }
    }
}

void scr_color(int fg, int bg, int border, int hasfg, int hasbg, int hasbo) {
    (void)border; (void)hasbo;
    if (S.mode == 0) {
        if (hasfg) { if (fg < 0 || fg > 31) rt_error(5); S.fg = fg; }
        if (hasbg) { if (bg < 0 || bg > 15) rt_error(5); S.bg = bg & 7; }
        return;
    }
    if (S.mode == 13 || S.mode == 12 || S.mode == 11) {
        if (hasbg) rt_error(5);
        if (hasfg) { if (fg < 0 || fg >= S.colors) rt_error(5); S.fg = fg; }
        return;
    }
    if (S.mode == 1) {
        if (hasfg) {
            /* COLOR background, palette */
            uint8_t r, g, b; ega_rgb(ega_default[fg & 15], &r, &g, &b);
            S.rgb[0][0] = r; S.rgb[0][1] = g; S.rgb[0][2] = b;
        }
        if (hasbg) {
            static const int p0[4] = { 0, 2, 4, 6 }, p1[4] = { 0, 3, 5, 15 };
            const int *pp = (bg & 1) ? p1 : p0;
            for (int i = 1; i < 4; i++) { uint8_t r, g, b; ega_rgb(ega_default[pp[i]], &r, &g, &b); S.rgb[i][0] = r; S.rgb[i][1] = g; S.rgb[i][2] = b; }
        }
        return;
    }
    if (S.mode == 2) { if (hasfg || hasbg) rt_error(5); return; }
    /* EGA modes 7, 8, 9, 10: COLOR fg, bg  (bg remaps attribute 0) */
    if (hasfg) { if (fg < 0 || fg >= S.colors) rt_error(5); S.fg = fg; }
    if (hasbg) {
        if (bg < 0 || bg > 63) rt_error(5);
        int c = (S.mode == 9) ? bg : ega_default[bg & 15];
        uint8_t r, g, b; ega_rgb(c, &r, &g, &b);
        S.rgb[0][0] = r; S.rgb[0][1] = g; S.rgb[0][2] = b;
    }
}

void scr_locate(int row, int col, int cursor, int start, int stop, int has[5]) {
    if (has[0]) {
        if (row < 1 || row > S.rows) rt_error(5);
        S.cy = row - 1;
    }
    if (has[1]) {
        if (col < 1 || col > S.cols) rt_error(5);
        S.cx = col - 1;
    }
    if (has[2]) S.curvis = cursor ? 1 : 0;
    if (has[3]) S.curstart = start;
    if (has[4]) S.curstop = stop;
}

void scr_viewprint(int top, int bottom, int has) {
    if (!has) { S.vtop = 0; S.vbot = S.rows - 2; S.cx = 0; S.cy = 0; return; }
    if (top < 1 || bottom > S.rows || top > bottom) rt_error(5);
    S.vtop = top - 1;
    S.vbot = bottom - 1;
    S.cx = 0;
    S.cy = S.vtop;
}

int scr_screenfn(int row, int col, int colorflag) {
    if (row < 1 || row > S.rows || col < 1 || col > S.cols) rt_error(5);
    int p = S.apage;
    if (S.mode == 0) {
        uint16_t c = S.text[p][(row - 1) * S.cols + (col - 1)];
        return colorflag ? (c >> 8) : (c & 0xFF);
    }
    if (colorflag) return 0;
    return S.gtext[p][(row - 1) * S.cols + (col - 1)];
}

void scr_cursor(int visible) { S.curvis = visible; }

/* ------------------------------------------------------------------ */
/* presentation                                                       */
/* ------------------------------------------------------------------ */
static int cur_vm = -1;
static double last_present = 0;

static void set_vm(int vm) {
    if (vm != cur_vm) {
        setvideomode((enum videomode_t)vm);
        cur_vm = vm;
    }
}

/* present arbitrary text cells (used by the IDE): attr low nibble fg, high nibble bg */

/* ------------------------------------------------------------------ */
/* emulated mouse cursor (drawn only into the presented image, so the  */
/* program's video memory never contains it)                           */
/* ------------------------------------------------------------------ */
int g_mouse_cursor = 0;     /* MOUSE ON / MOUSE OFF for programs (OFF by default) */

/* classic DOS mouse-driver arrow: '#' outline (black), '.' fill (white) */
static const char *arrow_shape[16] = {
    "#           ",
    "##          ",
    "#.#         ",
    "#..#        ",
    "#...#       ",
    "#....#      ",
    "#.....#     ",
    "#......#    ",
    "#.......#   ",
    "#........#  ",
    "#.....#####  ",
    "#..#..#     ",
    "#.# #..#    ",
    "##  #..#    ",
    "     #..#   ",
    "     ####   "
};

/* text cell under the pointer is shown with inverted colors */
static void overlay_text_cursor(uint16_t *cells, int cols, int rows) {
    int mx = mousex(), my = mousey();
    if (mx < 0 || my < 0 || mx >= cols || my >= rows) return;
    uint16_t c = cells[my * cols + mx];
    int fg = (c >> 8) & 15, bg = (c >> 12) & 15;
    cells[my * cols + mx] = (uint16_t)((c & 0xFF) | ((fg ^ 7) << 8) | ((bg ^ 7) << 12));
}

static int nearest_index(int n, int r, int g, int b, int use_attr) {
    int best = 0; long bd = 1L << 30;
    for (int i = 0; i < n; i++) {
        int d = use_attr ? S.attrpal[i] : i;
        long dr = S.rgb[d][0] - r, dg = S.rgb[d][1] - g, db = S.rgb[d][2] - b;
        long dist = dr * dr + dg * dg + db * db;
        if (dist < bd) { bd = dist; best = i; }
    }
    return best;
}

static void overlay_gfx_cursor(uint8_t *pix, int w, int h) {
    int mx = mousex(), my = mousey();
    if (mx < 0 || my < 0 || mx >= w || my >= h) return;
    int n = S.colors, attr = S.colors <= 16;
    int white = nearest_index(n, 63, 63, 63, attr), black = nearest_index(n, 0, 0, 0, attr);
    if (white == black) black = (white + 1) % n;
    /* in 640-wide modes with low height (SCREEN 2, 8) pixels are tall: halve rows */
    int ystep = (S.h <= 200 && S.w >= 640) ? 2 : 1;
    for (int yy = 0; yy < 16; yy += ystep)
        for (int xx = 0; arrow_shape[yy][xx]; xx++) {
            char c = arrow_shape[yy][xx];
            if (c == ' ') continue;
            int px = mx + xx, py = my + yy / ystep;
            if (px >= w || py >= h) continue;
            pix[py * w + px] = (uint8_t)(c == '#' ? black : white);
        }
}

void present_text_cells(const uint16_t *cells, int cols, int rows, int cx, int cy, int curvis) {
    int vm = videomode_80x25_9x16;
    if (rows == 43) vm = videomode_80x43_8x8;
    else if (rows == 50) vm = videomode_80x50_8x8;
    set_vm(vm);
    for (int i = 0; i < 16; i++) {
        uint8_t r, g, b; ega_rgb(ega_default[i], &r, &g, &b);
        setpal(i, r, g, b);
    }
    unsigned char *buf = screenbuffer();
    memcpy(buf, cells, sizeof(uint16_t) * (size_t)(cols * rows));
    overlay_text_cursor((uint16_t *)buf, cols, rows);    /* the IDE always shows the mouse block */
    if (curvis) { gotoxy(cx, cy); curson(); } else cursoff();
}

static int blink_phase(void) { return ((int)(sys_time() * 1000.0) / 280) & 1; }

static void present_program(void) {
    if (S.mode == 0) {
        set_vm(S.textmode_vm);
        for (int i = 0; i < 16; i++) setpal(i, S.rgb[S.attrpal[i]][0], S.rgb[S.attrpal[i]][1], S.rgb[S.attrpal[i]][2]);
        uint16_t *dst = (uint16_t *)screenbuffer();
        const uint16_t *src = S.text[S.vpage];
        int n = S.cols * S.rows;
        int bp = blink_phase();
        for (int i = 0; i < n; i++) {
            uint16_t c = src[i];
            int attr = c >> 8;
            int fg = attr & 15, bg = (attr >> 4) & 7;
            if ((attr & 0x80) && bp) fg = bg;
            dst[i] = (uint16_t)((c & 0xFF) | (fg << 8) | (bg << 12));
        }
        if (g_mouse_cursor) overlay_text_cursor(dst, S.cols, S.rows);
        if (S.curvis && S.cx < S.cols) { gotoxy(S.cx, S.cy); curson(); }
        else if (S.curvis) { gotoxy(S.cols - 1, S.cy); curson(); }
        else cursoff();
        return;
    }
    set_vm(S.mi->vm);
    if (S.colors == 256) for (int i = 0; i < 256; i++) setpal(i, S.rgb[i][0], S.rgb[i][1], S.rgb[i][2]);
    else for (int i = 0; i < 16; i++) setpal(i, S.rgb[S.attrpal[i]][0], S.rgb[S.attrpal[i]][1], S.rgb[S.attrpal[i]][2]);
    memcpy(screenbuffer(), S.pix[S.vpage], (size_t)(S.w * S.h));
    if (g_mouse_cursor) overlay_gfx_cursor(screenbuffer(), S.w, S.h);
}

void scr_present(int force) {
    double t = sys_time();
    if (!force && t - last_present < 0.012) return;
    last_present = t;
    if (display_owner_ide) return;
    present_program();
}

void scr_show_output(void) { display_owner_ide = 0; present_program(); }
void scr_set_ide_owner(int ide) { display_owner_ide = ide; }
void scr_save_output(void) {}

/* temporary cursor for INPUT */
static int input_cursor = 0;

/* ------------------------------------------------------------------ */
/* keyboard                                                           */
/* ------------------------------------------------------------------ */
#define KBQ 256
static int kbq[KBQ];        /* low 16 bits: char, or scan<<8 for extended keys; bits 16-18: shift/ctrl/alt */
static int kbh = 0, kbt = 0;
static int last_scan = 0;
static int break_flag = 0;
static uint8_t keydown_state[KEYCOUNT];
#define MQ 64
typedef struct MEv { int type, x, y; } MEv;   /* type 1 down, 2 up, 3 right-down */
static MEv mq[MQ];
static int mqh = 0, mqt = 0;
static int alt_clean = 0;
static int mouse_btn = 0;
int mouse_get(int *type, int *x, int *y) {
    if (mqh == mqt) return 0;
    *type = mq[mqh].type; *x = mq[mqh].x; *y = mq[mqh].y;
    mqh = (mqh + 1) % MQ;
    return 1;
}
int mouse_button(void) { return mouse_btn; }
static void mq_push(int type) {
    int n = (mqt + 1) % MQ;
    if (n == mqh) return;
    mq[mqt].type = type; mq[mqt].x = mousex(); mq[mqt].y = mousey();
    mqt = n;
}
static uint8_t scan_down[256];
int kb_scan_down(int sc) { return sc >= 0 && sc < 256 ? scan_down[sc] : 0; }

typedef struct KeyMap { int vk, scan; } KeyMap;
static const KeyMap scanmap[] = {
    { KEY_ESCAPE, 1 }, { KEY_1, 2 }, { KEY_2, 3 }, { KEY_3, 4 }, { KEY_4, 5 }, { KEY_5, 6 }, { KEY_6, 7 },
    { KEY_7, 8 }, { KEY_8, 9 }, { KEY_9, 10 }, { KEY_0, 11 }, { KEY_OEM_MINUS, 12 }, { KEY_OEM_PLUS, 13 },
    { KEY_BACK, 14 }, { KEY_TAB, 15 }, { KEY_Q, 16 }, { KEY_W, 17 }, { KEY_E, 18 }, { KEY_R, 19 }, { KEY_T, 20 },
    { KEY_Y, 21 }, { KEY_U, 22 }, { KEY_I, 23 }, { KEY_O, 24 }, { KEY_P, 25 }, { KEY_OEM_4, 26 }, { KEY_OEM_6, 27 },
    { KEY_RETURN, 28 }, { KEY_CONTROL, 29 }, { KEY_LCONTROL, 29 }, { KEY_RCONTROL, 29 }, { KEY_A, 30 }, { KEY_S, 31 },
    { KEY_D, 32 }, { KEY_F, 33 }, { KEY_G, 34 }, { KEY_H, 35 }, { KEY_J, 36 }, { KEY_K, 37 }, { KEY_L, 38 },
    { KEY_OEM_1, 39 }, { KEY_OEM_7, 40 }, { KEY_OEM_3, 41 }, { KEY_LSHIFT, 42 }, { KEY_SHIFT, 42 }, { KEY_OEM_5, 43 },
    { KEY_Z, 44 }, { KEY_X, 45 }, { KEY_C, 46 }, { KEY_V, 47 }, { KEY_B, 48 }, { KEY_N, 49 }, { KEY_M, 50 },
    { KEY_OEM_COMMA, 51 }, { KEY_OEM_PERIOD, 52 }, { KEY_OEM_2, 53 }, { KEY_RSHIFT, 54 }, { KEY_MULTIPLY, 55 },
    { KEY_MENU, 56 }, { KEY_LMENU, 56 }, { KEY_RMENU, 56 }, { KEY_SPACE, 57 }, { KEY_CAPITAL, 58 },
    { KEY_F1, 59 }, { KEY_F2, 60 }, { KEY_F3, 61 }, { KEY_F4, 62 }, { KEY_F5, 63 }, { KEY_F6, 64 }, { KEY_F7, 65 },
    { KEY_F8, 66 }, { KEY_F9, 67 }, { KEY_F10, 68 }, { KEY_NUMLOCK, 69 }, { KEY_SCROLL, 70 }, { KEY_HOME, 71 },
    { KEY_UP, 72 }, { KEY_PRIOR, 73 }, { KEY_SUBTRACT, 74 }, { KEY_LEFT, 75 }, { KEY_NUMPAD5, 76 }, { KEY_RIGHT, 77 },
    { KEY_ADD, 78 }, { KEY_END, 79 }, { KEY_DOWN, 80 }, { KEY_NEXT, 81 }, { KEY_INSERT, 82 }, { KEY_DELETE, 83 },
    { KEY_F11, 87 }, { KEY_F12, 88 }, { KEY_NUMPAD7, 71 }, { KEY_NUMPAD8, 72 }, { KEY_NUMPAD9, 73 },
    { KEY_NUMPAD4, 75 }, { KEY_NUMPAD6, 77 }, { KEY_NUMPAD1, 79 }, { KEY_NUMPAD2, 80 }, { KEY_NUMPAD3, 81 },
    { KEY_NUMPAD0, 82 }, { KEY_DECIMAL, 83 }, { KEY_DIVIDE, 53 }, { KEY_OEM_102, 86 },
    { 0, 0 }
};
static int vk_to_scan(int vk) {
    for (int i = 0; scanmap[i].vk; i++) if (scanmap[i].vk == vk) return scanmap[i].scan;
    return 0;
}

static void kbq_push(int code) {
    int n = (kbt + 1) % KBQ;
    if (n == kbh) return;   /* full: drop (PC would beep) */
    kbq[kbt] = code;
    kbt = n;
}

int kb_keydown(int vk) { return vk >= 0 && vk < KEYCOUNT ? keydown_state[vk] : 0; }
/* modifier state tracked from the event stream (not sampled late) */
static int m_shift(void) { return keydown_state[KEY_SHIFT] || keydown_state[KEY_LSHIFT] || keydown_state[KEY_RSHIFT]; }
static int m_altgr(void) { return keydown_state[KEY_RMENU]; }
static int m_ctrl(void) { return (keydown_state[KEY_CONTROL] || keydown_state[KEY_LCONTROL] || keydown_state[KEY_RCONTROL]) && !m_altgr(); }
static int m_alt(void) { return (keydown_state[KEY_LMENU] || (keydown_state[KEY_MENU] && !keydown_state[KEY_RMENU])) && !m_altgr(); }
static int modbits(void) { return (m_shift() ? 1 << 16 : 0) | (m_ctrl() ? 1 << 17 : 0) | (m_alt() ? 1 << 18 : 0); }

int kb_shift_state(void) {
    int v = 0;
    if (keystate(KEY_RSHIFT)) v |= 1;
    if (keystate(KEY_LSHIFT) || (keystate(KEY_SHIFT) && !keystate(KEY_RSHIFT))) v |= 2;
    if (keystate(KEY_CONTROL) || keystate(KEY_LCONTROL) || keystate(KEY_RCONTROL)) v |= 4;
    if (keystate(KEY_MENU) || keystate(KEY_LMENU) || keystate(KEY_RMENU)) v |= 8;
    return v;
}

/* translate a key-down into a QBasic extended key code (scan code), or 0 */
static int extended_code(int vk) {
    int sh = m_shift(), ct = m_ctrl(), al = m_alt();
    if (vk >= KEY_F1 && vk <= KEY_F10) {
        int n = vk - KEY_F1;
        if (al) return 104 + n;
        if (ct) return 94 + n;
        if (sh) return 84 + n;
        return 59 + n;
    }
    if (vk == KEY_F11) return al ? 139 : ct ? 137 : sh ? 135 : 133;
    if (vk == KEY_F12) return al ? 140 : ct ? 138 : sh ? 136 : 134;
    switch (vk) {
        case KEY_UP: return ct ? 141 : 72;
        case KEY_DOWN: return ct ? 145 : 80;
        case KEY_LEFT: return ct ? 115 : 75;
        case KEY_RIGHT: return ct ? 116 : 77;
        case KEY_HOME: return ct ? 119 : 71;
        case KEY_END: return ct ? 117 : 79;
        case KEY_PRIOR: return ct ? 132 : 73;
        case KEY_NEXT: return ct ? 118 : 81;
        case KEY_INSERT: return 82;
        case KEY_DELETE: return 83;
    }
    if (al) {
        int sc = vk_to_scan(vk);
        if (vk >= KEY_1 && vk <= KEY_9) return 120 + (vk - KEY_1);
        if (vk == KEY_0) return 129;
        if (vk >= KEY_A && vk <= KEY_Z) return sc;
    }
    return 0;
}

static int produces_char(int vk) {
    if (vk >= KEY_0 && vk <= KEY_9) return 1;
    if (vk >= KEY_A && vk <= KEY_Z) return 1;
    if (vk == KEY_SPACE) return 1;
    if (vk >= KEY_NUMPAD0 && vk <= KEY_DIVIDE) return 1;
    if (vk >= KEY_OEM_1 && vk <= KEY_OEM_8) return 1;
    if (vk == KEY_OEM_102) return 1;
    return 0;
}

/* decode the char stream: UTF-8 on SDL platforms, Latin-1 bytes on Windows */
static int utf8_state = 0, utf8_cp = 0;
static int decode_chars(const char *chars, int *out, int max) {
    int n = 0;
    for (int i = 0; chars && chars[i] && i < 256 && n < max; i++) {
        unsigned char c = (unsigned char)chars[i];
        if (utf8_state > 0) {
            if ((c & 0xC0) == 0x80) {
                utf8_cp = (utf8_cp << 6) | (c & 0x3F);
                if (--utf8_state == 0) out[n++] = utf8_cp;
                continue;
            }
            utf8_state = 0;
        }
        if (c < 128) { out[n++] = c; continue; }
#ifdef _WIN32
        out[n++] = c;
#else
        if ((c & 0xE0) == 0xC0) { utf8_state = 1; utf8_cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { utf8_state = 2; utf8_cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { utf8_state = 3; utf8_cp = c & 0x07; }
        else out[n++] = c;
#endif
    }
    return n;
}

static void push_cp(int cp) {
    if (cp < 32 || cp == 127) return;
    kbq_push(unicode_to_cp437(cp) | modbits());
}

void kb_poll(void) {
    enum keycode_t *keys = readkeys();
    const char *chars = readchars();
    int cps[256];
    int ncp = decode_chars(chars, cps, 256);
    int ci = 0;
    for (int i = 0; keys && keys[i] != KEY_INVALID && i < 256; i++) {
        uint32_t k = (uint32_t)keys[i];
        int released = (k & KEY_MODIFIER_RELEASED) != 0;
        int vk = (int)(k & ~KEY_MODIFIER_RELEASED);
        if (vk <= 0 || vk >= KEYCOUNT) continue;
        int sc = vk_to_scan(vk);
        if (vk == KEY_LBUTTON) { mouse_btn = !released; if (display_owner_ide) mq_push(released ? 2 : 1); continue; }
        if (vk == KEY_RBUTTON) { if (!released && display_owner_ide) mq_push(3); continue; }
        if (vk == KEY_MENU || vk == KEY_LMENU) {
            if (!released) { if (!keydown_state[vk]) alt_clean = 1; }
            else if (alt_clean && display_owner_ide && !keydown_state[KEY_RMENU]) {
                alt_clean = 0;
                keydown_state[vk] = 0;
                if (sc) { last_scan = sc | 0x80; scan_down[sc] = 0; }
                kbq_push(0xF000);
                continue;
            }
        } else if (!released) alt_clean = 0;
        if (released) {
            keydown_state[vk] = 0;
            if (sc) { last_scan = sc | 0x80; scan_down[sc] = 0; }
            continue;
        }
        keydown_state[vk] = 1;
        if (sc) { last_scan = sc; scan_down[sc] = 1; }
        if ((vk == KEY_PAUSE || vk == KEY_CANCEL || vk == KEY_SCROLL) && m_ctrl()) { break_flag = 1; continue; }
        if (vk == KEY_CANCEL) { break_flag = 1; continue; }
        int mb = modbits();
        int ext = extended_code(vk);
        if (ext) { kbq_push((ext << 8) | mb); continue; }
        int ct = m_ctrl();
        switch (vk) {
            case KEY_RETURN: kbq_push((ct ? 10 : 13) | mb); continue;
            case KEY_ESCAPE: kbq_push(27 | mb); continue;
            case KEY_BACK: kbq_push((ct ? 127 : 8) | mb); continue;
            case KEY_TAB: kbq_push((m_shift() ? (15 << 8) : 9) | mb); continue;
        }
        if (ct && vk >= KEY_A && vk <= KEY_Z) { kbq_push((vk - KEY_A + 1) | mb); continue; }
        if (produces_char(vk)) {
            /* take the character this key produced, keeping event order */
            while (ci < ncp && (cps[ci] < 32 || cps[ci] == 127)) ci++;
            if (ci < ncp) {
                int cp = cps[ci++];
                if (!m_ctrl() && !m_alt()) push_cp(cp);
            }
        }
    }
    /* characters without a matching key event (dead keys, IME, late events) */
    for (; ci < ncp; ci++) if (!m_ctrl() && !m_alt()) push_cp(cps[ci]);
}

int kb_count(void) { return (kbt - kbh + KBQ) % KBQ; }

int kb_get(char *out) {
    if (kbh == kbt) return 0;
    int code = kbq[kbh] & 0xFFFF;
    kbh = (kbh + 1) % KBQ;
    if (code & 0xFF00) { out[0] = 0; out[1] = (char)(code >> 8); return 2; }
    out[0] = (char)code;
    return 1;
}

/* raw code access for the IDE */
int kb_get_code(void) {
    if (kbh == kbt) return -1;
    int code = kbq[kbh];
    kbh = (kbh + 1) % KBQ;
    return code;
}

void kb_clear(void) { kbh = kbt = 0; }
int kb_lastscan(void) { return last_scan; }
int kb_break_pressed(void) { int b = break_flag; break_flag = 0; return b; }
void kb_push_ide_key(int code) { kbq_push(code); }

int kb_waitkey(char *out) {
    for (;;) {
        kb_poll();
        int n = kb_get(out);
        if (n) return n;
        if (break_flag) { if (rs) rs->breakreq = 1; break_flag = 0; return 0; }
        if (sys_shutting_down()) return 0;
        scr_present(0);
        sys_sleep(4);
    }
}

/* line editor used by INPUT / LINE INPUT / RANDOMIZE prompt */
int input_line(char *buf, int max, int *len) {
    int n = 0, pos = 0;
    int sx = S.cx, sy = S.cy;
    if (sx >= S.cols) { newline_internal(); sx = S.cx; sy = S.cy; }
    int oldlen = 0;
    int save_vis = S.curvis;
    S.curvis = 1;
    input_cursor = 1;
    int start_scrolls = S.scrolls;
    for (;;) {
        /* redraw */
        int sc0 = S.scrolls;
        S.cx = sx; S.cy = sy;
        scr_print(buf, n);
        int endx = S.cx, endy = S.cy;
        for (int i = n; i < oldlen; i++) scr_print(" ", 1);
        int scrolled = S.scrolls - sc0;
        if (scrolled) { sy -= scrolled; if (sy < 0) sy = 0; endy -= 0; }
        oldlen = n;
        /* place cursor at pos */
        int abs = sx + pos;
        S.cx = abs % S.cols;
        S.cy = sy + abs / S.cols;
        if (S.cy >= S.rows) S.cy = S.rows - 1;
        (void)endx; (void)endy;
        scr_present(1);
        char k[2];
        int kl = kb_waitkey(k);
        if (kl == 0) {
            /* break or shutdown */
            S.curvis = save_vis;
            input_cursor = 0;
            if (rs) rs->breakreq = 1;
            buf[n] = 0;
            *len = n;
            S.cx = sx + n; S.cy = sy;
            while (S.cx >= S.cols) { S.cx -= S.cols; S.cy++; }
            return 0;
        }
        if (kl == 1) {
            unsigned char c = (unsigned char)k[0];
            if (c == 13) break;
            if (c == 8) { if (pos > 0) { memmove(buf + pos - 1, buf + pos, (size_t)(n - pos)); pos--; n--; } continue; }
            if (c == 27) { pos = 0; n = 0; continue; }
            if (c == 9) { int sp = 8 - (pos % 8); while (sp-- > 0 && n < max) { memmove(buf + pos + 1, buf + pos, (size_t)(n - pos)); buf[pos++] = ' '; n++; } continue; }
            if (c < 32) continue;
            if (n < max) {
                memmove(buf + pos + 1, buf + pos, (size_t)(n - pos));
                buf[pos++] = (char)c;
                n++;
            }
            continue;
        }
        switch ((unsigned char)k[1]) {
            case 75: if (pos > 0) pos--; break;
            case 77: if (pos < n) pos++; break;
            case 71: pos = 0; break;
            case 79: pos = n; break;
            case 83: if (pos < n) { memmove(buf + pos, buf + pos + 1, (size_t)(n - pos - 1)); n--; } break;
            case 117: n = pos; break; /* ctrl+end: erase to end */
        }
    }
    (void)start_scrolls;
    buf[n] = 0;
    *len = n;
    /* cursor to end of input */
    int abs = sx + n;
    S.cx = abs % S.cols;
    S.cy = sy + abs / S.cols;
    if (abs > 0 && abs % S.cols == 0) { S.cx = S.cols; S.cy--; }
    if (S.cy >= S.rows) S.cy = S.rows - 1;
    S.curvis = save_vis;
    input_cursor = 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* graphics                                                           */
/* ------------------------------------------------------------------ */
static void need_gfx(void) { if (S.mode == 0) rt_error(5); }

static int clamp_color(int c) {
    if (S.colors == 256) return c & 255;
    if (c < 0) return 0;
    return c % S.colors == c ? c : (c & (S.colors - 1));
}

/* logical -> physical */
static void map_xy(double x, double y, double *px, double *py) {
    if (S.haswin) {
        double vw = S.vx2 - S.vx1, vh = S.vy2 - S.vy1;
        double fx = (x - S.wx1) / (S.wx2 - S.wx1);
        double fy;
        if (S.winscreen) fy = (y - S.wy1) / (S.wy2 - S.wy1);
        else fy = (S.wy2 - y) / (S.wy2 - S.wy1);
        double ox = S.hasview && !S.vscreen ? 0 : (S.hasview ? S.vx1 : 0);
        double oy = S.hasview && !S.vscreen ? 0 : (S.hasview ? S.vy1 : 0);
        *px = fx * vw + ox;
        *py = fy * vh + oy;
        if (S.hasview && !S.vscreen) { *px += S.vx1; *py += S.vy1; }
        return;
    }
    if (S.hasview && !S.vscreen) { *px = x + S.vx1; *py = y + S.vy1; return; }
    *px = x;
    *py = y;
}
/* physical -> logical */
static void unmap_xy(double px, double py, double *x, double *y) {
    if (S.haswin) {
        double vw = S.vx2 - S.vx1, vh = S.vy2 - S.vy1;
        double fx = (px - S.vx1) / (vw ? vw : 1), fy = (py - S.vy1) / (vh ? vh : 1);
        if (!S.hasview) { fx = px / (vw ? vw : 1); fy = py / (vh ? vh : 1); }
        *x = S.wx1 + fx * (S.wx2 - S.wx1);
        if (S.winscreen) *y = S.wy1 + fy * (S.wy2 - S.wy1);
        else *y = S.wy2 - fy * (S.wy2 - S.wy1);
        return;
    }
    if (S.hasview && !S.vscreen) { *x = px - S.vx1; *y = py - S.vy1; return; }
    *x = px; *y = py;
}

static inline void plot(int x, int y, int c) {
    if (x < S.vx1 || x > S.vx2 || y < S.vy1 || y > S.vy2) return;
    S.pix[S.apage][y * S.w + x] = (uint8_t)c;
}
static inline int getpix(int x, int y) {
    if (x < 0 || y < 0 || x >= S.w || y >= S.h) return -1;
    return S.pix[S.apage][y * S.w + x];
}

static int iround(double d) { return (int)floor(d + 0.5); }

void gfx_pset(double x, double y, int color, int hascolor, int step, int preset) {
    need_gfx();
    if (step) { x += S.lastx; y += S.lasty; }
    int c = hascolor ? clamp_color(color) : (preset ? 0 : S.fg);
    double px, py;
    map_xy(x, y, &px, &py);
    plot(iround(px), iround(py), c);
    S.lastx = x; S.lasty = y;
}

static void raw_line(int x1, int y1, int x2, int y2, int c, int style, int *bit) {
    int dx = abs(x2 - x1), dy = -abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        int on = 1;
        if (style != 0xFFFF) { on = (style >> (15 - (*bit & 15))) & 1; (*bit)++; }
        if (on) plot(x1, y1, c);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void gfx_line(double x1, double y1, double x2, double y2, int color, int hascolor,
              int box, int step1, int step2, int has1, int style, int hasstyle) {
    need_gfx();
    if (!has1) { x1 = S.lastx; y1 = S.lasty; }
    else if (step1) { x1 += S.lastx; y1 += S.lasty; }
    if (step2) { x2 += x1; y2 += y1; }
    int c = hascolor ? clamp_color(color) : S.fg;
    int st = hasstyle ? (style & 0xFFFF) : 0xFFFF;
    double ax, ay, bx, by;
    map_xy(x1, y1, &ax, &ay);
    map_xy(x2, y2, &bx, &by);
    int X1 = iround(ax), Y1 = iround(ay), X2 = iround(bx), Y2 = iround(by);
    int bit = 0;
    if (box == 2) {
        if (X1 > X2) { int t = X1; X1 = X2; X2 = t; }
        if (Y1 > Y2) { int t = Y1; Y1 = Y2; Y2 = t; }
        int cx1 = X1 < S.vx1 ? S.vx1 : X1, cx2 = X2 > S.vx2 ? S.vx2 : X2;
        int cy1 = Y1 < S.vy1 ? S.vy1 : Y1, cy2 = Y2 > S.vy2 ? S.vy2 : Y2;
        for (int y = cy1; y <= cy2; y++) if (cx2 >= cx1) memset(S.pix[S.apage] + y * S.w + cx1, c, (size_t)(cx2 - cx1 + 1));
    } else if (box == 1) {
        raw_line(X1, Y1, X2, Y1, c, st, &bit);
        raw_line(X2, Y1, X2, Y2, c, st, &bit);
        raw_line(X2, Y2, X1, Y2, c, st, &bit);
        raw_line(X1, Y2, X1, Y1, c, st, &bit);
    } else {
        raw_line(X1, Y1, X2, Y2, c, st, &bit);
    }
    S.lastx = x2; S.lasty = y2;
}

static double default_aspect(void) {
    return (4.0 / 3.0) * ((double)S.h / (double)S.w);
}

void gfx_circle(double x, double y, double r, int color, int hascolor, double start,
                double end, double aspect, int hasstart, int hasend, int hasaspect, int step) {
    need_gfx();
    if (step) { x += S.lastx; y += S.lasty; }
    int c = hascolor ? clamp_color(color) : S.fg;
    double asp = hasaspect ? aspect : default_aspect();
    double px, py;
    map_xy(x, y, &px, &py);
    /* radius in physical x units */
    double pr = r;
    if (S.haswin) pr = r * (S.vx2 - S.vx1) / fabs(S.wx2 - S.wx1);
    double rx, ry;
    if (asp <= 1) { rx = pr; ry = pr * asp; }
    else { ry = pr; rx = pr / asp; }
    S.lastx = x; S.lasty = y;
    int arc = hasstart || hasend;
    double a0 = hasstart ? start : 0, a1 = hasend ? end : 2 * M_PI;
    int line0 = 0, line1 = 0;
    if (a0 < 0) { line0 = 1; a0 = -a0; }
    if (a1 < 0) { line1 = 1; a1 = -a1; }
    if (a0 > 2 * M_PI + 1e-6 || a1 > 2 * M_PI + 1e-6) rt_error(5);
    int cx = iround(px), cy = iround(py);
    if (!arc) {
        /* midpoint ellipse */
        int a = iround(rx), b = iround(ry);
        if (a == 0 && b == 0) { plot(cx, cy, c); return; }
        if (b == 0) { int bit = 0; raw_line(cx - a, cy, cx + a, cy, c, 0xFFFF, &bit); return; }
        if (a == 0) { int bit = 0; raw_line(cx, cy - b, cx, cy + b, c, 0xFFFF, &bit); return; }
        long long a2 = (long long)a * a, b2 = (long long)b * b;
        long long xx = 0, yy = b;
        long long d1 = b2 - a2 * b + a2 / 4;
        long long dx = 2 * b2 * xx, dy = 2 * a2 * yy;
        while (dx < dy) {
            plot(cx + (int)xx, cy + (int)yy, c); plot(cx - (int)xx, cy + (int)yy, c);
            plot(cx + (int)xx, cy - (int)yy, c); plot(cx - (int)xx, cy - (int)yy, c);
            if (d1 < 0) { xx++; dx += 2 * b2; d1 += dx + b2; }
            else { xx++; yy--; dx += 2 * b2; dy -= 2 * a2; d1 += dx - dy + b2; }
        }
        long long d2 = (long long)(b2 * (xx + 0.5) * (xx + 0.5) + a2 * (yy - 1) * (yy - 1) - a2 * b2);
        while (yy >= 0) {
            plot(cx + (int)xx, cy + (int)yy, c); plot(cx - (int)xx, cy + (int)yy, c);
            plot(cx + (int)xx, cy - (int)yy, c); plot(cx - (int)xx, cy - (int)yy, c);
            if (d2 > 0) { yy--; dy -= 2 * a2; d2 += a2 - dy; }
            else { yy--; xx++; dx += 2 * b2; dy -= 2 * a2; d2 += dx - dy + a2; }
        }
        return;
    }
    /* arc: walk angles */
    double span = a1 - a0;
    if (span < 0) span += 2 * M_PI;
    double maxr = rx > ry ? rx : ry;
    int steps = (int)(span * maxr * 2) + 8;
    int lx = 0, ly = 0;
    for (int i = 0; i <= steps; i++) {
        double t = a0 + span * i / steps;
        int ex = cx + iround(cos(t) * rx);
        int ey = cy - iround(sin(t) * ry);
        if (i > 0) { int bit = 0; raw_line(lx, ly, ex, ey, c, 0xFFFF, &bit); }
        else plot(ex, ey, c);
        lx = ex; ly = ey;
    }
    int bit = 0;
    if (line0) raw_line(cx, cy, cx + iround(cos(a0) * rx), cy - iround(sin(a0) * ry), c, 0xFFFF, &bit);
    if (line1) raw_line(cx, cy, cx + iround(cos(a1) * rx), cy - iround(sin(a1) * ry), c, 0xFFFF, &bit);
}

static int tile_color(const char *tile, int tl, int x, int y) {
    if (S.colors == 256) {
        int rows = (tl + 7) / 8;
        int r = y % rows;
        int idx = r * 8 + (x % 8);
        return idx < tl ? (unsigned char)tile[idx] : 0;
    }
    if (S.colors == 16) {
        int rows = (tl + 3) / 4;
        int r = y % rows;
        int c = 0;
        for (int pl = 0; pl < 4; pl++) {
            int idx = r * 4 + pl;
            unsigned char b = idx < tl ? (unsigned char)tile[idx] : 0;
            if (b & (0x80 >> (x % 8))) c |= 1 << pl;
        }
        return c;
    }
    if (S.colors == 4) {
        unsigned char b = (unsigned char)tile[y % tl];
        return (b >> (6 - 2 * (x % 4))) & 3;
    }
    unsigned char b = (unsigned char)tile[y % tl];
    return (b >> (7 - (x % 8))) & 1;
}

void gfx_paint(double x, double y, int paint, int border, int haspaint, int hasborder, int step,
               const char *tile, int tilelen) {
    need_gfx();
    if (step) { x += S.lastx; y += S.lasty; }
    S.lastx = x; S.lasty = y;
    double px, py;
    map_xy(x, y, &px, &py);
    int sx = iround(px), sy = iround(py);
    int usetile = tile && tilelen > 0;
    int pc = haspaint && !usetile ? clamp_color(paint) : S.fg;
    int bc = hasborder ? clamp_color(border) : pc;
    if (sx < S.vx1 || sx > S.vx2 || sy < S.vy1 || sy > S.vy2) return;
    if (getpix(sx, sy) == bc) return;
    int W = S.w;
    uint8_t *vis = (uint8_t *)qb_alloc((size_t)(S.w * S.h));
    int cap = 4096, n = 0;
    int *stack = (int *)qb_alloc(sizeof(int) * (size_t)cap);
    stack[n++] = sy * W + sx;
    while (n) {
        int p = stack[--n];
        int yy = p / W, xx = p % W;
        if (vis[p]) continue;
        /* scan left */
        int l = xx, r = xx;
        while (l - 1 >= S.vx1 && !vis[yy * W + l - 1] && getpix(l - 1, yy) != bc) l--;
        while (r + 1 <= S.vx2 && !vis[yy * W + r + 1] && getpix(r + 1, yy) != bc) r++;
        for (int i = l; i <= r; i++) {
            vis[yy * W + i] = 1;
            S.pix[S.apage][yy * W + i] = (uint8_t)(usetile ? tile_color(tile, tilelen, i, yy) : pc);
        }
        for (int dy = -1; dy <= 1; dy += 2) {
            int ny = yy + dy;
            if (ny < S.vy1 || ny > S.vy2) continue;
            int in = 0;
            for (int i = l; i <= r; i++) {
                int q = ny * W + i;
                int ok = !vis[q] && getpix(i, ny) != bc;
                if (ok && !in) {
                    if (n >= cap) { cap *= 2; stack = (int *)qb_realloc(stack, sizeof(int) * (size_t)cap); }
                    stack[n++] = q;
                    in = 1;
                } else if (!ok) in = 0;
            }
        }
    }
    free(stack);
    free(vis);
}

int gfx_point(double x, double y) {
    need_gfx();
    double px, py;
    map_xy(x, y, &px, &py);
    int ix = iround(px), iy = iround(py);
    if (ix < S.vx1 || ix > S.vx2 || iy < S.vy1 || iy > S.vy2) return -1;
    return getpix(ix, iy);
}

double gfx_pointfn(int n) {
    need_gfx();
    double px, py;
    map_xy(S.lastx, S.lasty, &px, &py);
    switch (n) {
        case 0: return iround(px);
        case 1: return iround(py);
        case 2: return S.lastx;
        case 3: return S.lasty;
    }
    rt_error(5);
    return 0;
}

void gfx_view(int x1, int y1, int x2, int y2, int fill, int border, int screen, int has, int hasfill, int hasborder) {
    need_gfx();
    if (!has) {
        S.hasview = 0; S.vx1 = 0; S.vy1 = 0; S.vx2 = S.w - 1; S.vy2 = S.h - 1;
        return;
    }
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    if (x1 < 0 || y1 < 0 || x2 >= S.w || y2 >= S.h) rt_error(5);
    S.hasview = 1;
    S.vscreen = screen;
    if (hasborder) {
        int ox1 = S.vx1, oy1 = S.vy1, ox2 = S.vx2, oy2 = S.vy2;
        S.vx1 = 0; S.vy1 = 0; S.vx2 = S.w - 1; S.vy2 = S.h - 1;
        int bit = 0, bc = clamp_color(border);
        raw_line(x1 - 1, y1 - 1, x2 + 1, y1 - 1, bc, 0xFFFF, &bit);
        raw_line(x2 + 1, y1 - 1, x2 + 1, y2 + 1, bc, 0xFFFF, &bit);
        raw_line(x2 + 1, y2 + 1, x1 - 1, y2 + 1, bc, 0xFFFF, &bit);
        raw_line(x1 - 1, y2 + 1, x1 - 1, y1 - 1, bc, 0xFFFF, &bit);
        (void)ox1; (void)oy1; (void)ox2; (void)oy2;
    }
    S.vx1 = x1; S.vy1 = y1; S.vx2 = x2; S.vy2 = y2;
    if (hasfill) {
        int c = clamp_color(fill);
        for (int y = y1; y <= y2; y++) memset(S.pix[S.apage] + y * S.w + x1, c, (size_t)(x2 - x1 + 1));
    }
    S.lastx = S.vscreen ? (x1 + x2) / 2.0 : (x2 - x1) / 2.0;
    S.lasty = S.vscreen ? (y1 + y2) / 2.0 : (y2 - y1) / 2.0;
}

void gfx_window(double x1, double y1, double x2, double y2, int screen, int has) {
    need_gfx();
    if (!has) { S.haswin = 0; return; }
    if (x1 == x2 || y1 == y2) rt_error(5);
    if (x1 > x2) { double t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { double t = y1; y1 = y2; y2 = t; }
    S.haswin = 1;
    S.winscreen = screen;
    S.wx1 = x1; S.wy1 = y1; S.wx2 = x2; S.wy2 = y2;
    S.lastx = (x1 + x2) / 2; S.lasty = (y1 + y2) / 2;
}

double gfx_pmap(double v, int fn) {
    need_gfx();
    double px, py, lx, ly;
    switch (fn) {
        case 0: map_xy(v, 0, &px, &py); return iround(px);
        case 1: map_xy(0, v, &px, &py); return iround(py);
        case 2: unmap_xy(v, S.hasview ? S.vy1 : 0, &lx, &ly); return lx;
        case 3: unmap_xy(S.hasview ? S.vx1 : 0, v, &lx, &ly); return ly;
    }
    rt_error(5);
    return 0;
}

/* DRAW macro language */
static int draw_num(const char *s, int len, int *i, int *ok) {
    while (*i < len && s[*i] == ' ') (*i)++;
    int neg = 0, v = 0, any = 0;
    if (*i < len && (s[*i] == '-' || s[*i] == '+')) { neg = s[*i] == '-'; (*i)++; }
    if (*i < len && s[*i] == '=') {
        /* =VARPTR$(var); not supported */
        while (*i < len && s[*i] != ';') (*i)++;
        if (*i < len) (*i)++;
        *ok = 0;
        return 0;
    }
    while (*i < len && s[*i] >= '0' && s[*i] <= '9') { v = v * 10 + (s[*i] - '0'); (*i)++; any = 1; }
    *ok = any;
    return neg ? -v : v;
}

void gfx_draw(const char *s, int len) {
    need_gfx();
    int i = 0;
    while (i < len) {
        char c = (char)toupper((unsigned char)s[i]);
        if (c == ' ' || c == ';') { i++; continue; }
        int nodraw = 0, noupdate = 0;
        while (c == 'B' || c == 'N') {
            if (c == 'B') nodraw = 1; else noupdate = 1;
            i++;
            while (i < len && s[i] == ' ') i++;
            if (i >= len) return;
            c = (char)toupper((unsigned char)s[i]);
        }
        i++;
        int ok;
        int col = S.drawcolor >= 0 ? S.drawcolor : S.fg;
        double dx = 0, dy = 0;
        int move = 0, absolute = 0;
        double ax = 0, ay = 0;
        switch (c) {
            case 'U': case 'D': case 'L': case 'R': case 'E': case 'F': case 'G': case 'H': {
                int n = draw_num(s, len, &i, &ok);
                if (!ok) n = 1;
                switch (c) {
                    case 'U': dy = -n; break; case 'D': dy = n; break;
                    case 'L': dx = -n; break; case 'R': dx = n; break;
                    case 'E': dx = n; dy = -n; break; case 'F': dx = n; dy = n; break;
                    case 'G': dx = -n; dy = n; break; case 'H': dx = -n; dy = -n; break;
                }
                move = 1;
                break;
            }
            case 'M': {
                while (i < len && s[i] == ' ') i++;
                int rel = (i < len && (s[i] == '+' || s[i] == '-'));
                int x = draw_num(s, len, &i, &ok);
                while (i < len && s[i] == ' ') i++;
                if (i < len && s[i] == ',') i++;
                int y = draw_num(s, len, &i, &ok);
                if (rel) { dx = x; dy = y; move = 1; }
                else { ax = x; ay = y; absolute = 1; }
                break;
            }
            case 'A': { int n = draw_num(s, len, &i, &ok); S.drawangle = n & 3; S.drawta = 0; break; }
            case 'T': {
                if (i < len && toupper((unsigned char)s[i]) == 'A') i++;
                int n = draw_num(s, len, &i, &ok);
                S.drawta = n;
                S.drawangle = 0;
                break;
            }
            case 'C': { int n = draw_num(s, len, &i, &ok); S.drawcolor = clamp_color(n); break; }
            case 'S': { int n = draw_num(s, len, &i, &ok); if (n < 1) n = 4; S.drawscale = n; break; }
            case 'P': {
                int p = draw_num(s, len, &i, &ok);
                while (i < len && s[i] == ' ') i++;
                if (i < len && s[i] == ',') i++;
                int b = draw_num(s, len, &i, &ok);
                gfx_paint(S.lastx, S.lasty, p, b, 1, 1, 0, NULL, 0);
                break;
            }
            case 'X': {
                /* X + VARPTR$(string): unsupported */
                while (i < len && s[i] != ';') i++;
                break;
            }
            default:
                rt_error(5);
        }
        if (move || absolute) {
            double nx, ny;
            if (move) {
                double sc = S.drawscale / 4.0;
                dx *= sc; dy *= sc;
                double ang = S.drawangle * 90.0 + S.drawta;
                double rad = ang * M_PI / 180.0;
                double rx = dx * cos(rad) + dy * sin(rad);
                double ry = -dx * sin(rad) + dy * cos(rad);
                /* aspect correction for non-square modes */
                double asp = default_aspect();
                if (S.drawangle % 2 == 1 || S.drawta != 0) { (void)asp; }
                nx = S.lastx + rx;
                ny = S.lasty + ry;
            } else { nx = ax; ny = ay; }
            if (!nodraw) {
                double x1, y1, x2, y2;
                map_xy(S.lastx, S.lasty, &x1, &y1);
                map_xy(nx, ny, &x2, &y2);
                int bit = 0;
                raw_line(iround(x1), iround(y1), iround(x2), iround(y2), col, 0xFFFF, &bit);
            }
            if (!noupdate) { S.lastx = nx; S.lasty = ny; }
        }
    }
}

/* -------- array byte view for GET/PUT -------- */
static int elem_size(Array *a) {
    switch (a->t) { case T_INT: return 2; case T_LNG: return 4; case T_SNG: return 4; case T_DBL: return 8; }
    return 0;
}
static int arr_getbyte(Array *a, int base, long off) {
    int es = elem_size(a);
    long idx = base + off / es;
    if (idx >= a->count) rt_error(5);
    Value *v = &a->v[idx];
    unsigned char b[8];
    switch (a->t) {
        case T_INT: { int16_t x = (int16_t)v->n; memcpy(b, &x, 2); break; }
        case T_LNG: { int32_t x = (int32_t)v->n; memcpy(b, &x, 4); break; }
        case T_SNG: { float x = (float)v->n; memcpy(b, &x, 4); break; }
        case T_DBL: { double x = v->n; memcpy(b, &x, 8); break; }
    }
    return b[off % es];
}
static void arr_setbyte(Array *a, int base, long off, int val) {
    int es = elem_size(a);
    long idx = base + off / es;
    if (idx >= a->count) rt_error(5);
    Value *v = &a->v[idx];
    unsigned char b[8];
    switch (a->t) {
        case T_INT: { int16_t x = (int16_t)v->n; memcpy(b, &x, 2); b[off % es] = (unsigned char)val; memcpy(&x, b, 2); v->n = x; break; }
        case T_LNG: { int32_t x = (int32_t)v->n; memcpy(b, &x, 4); b[off % es] = (unsigned char)val; memcpy(&x, b, 4); v->n = x; break; }
        case T_SNG: { float x = (float)v->n; memcpy(b, &x, 4); b[off % es] = (unsigned char)val; memcpy(&x, b, 4); v->n = x; break; }
        case T_DBL: { double x = v->n; memcpy(b, &x, 8); b[off % es] = (unsigned char)val; memcpy(&x, b, 8); v->n = x; break; }
    }
}

void gfx_get(double x1, double y1, double x2, double y2, int step1, int step2, Array *a, int index) {
    need_gfx();
    if (a->t == T_STR || a->t == T_UDT) rt_error(13);
    if (step1) { x1 += S.lastx; y1 += S.lasty; }
    if (step2) { x2 += x1; y2 += y1; }
    double ax, ay, bx, by;
    map_xy(x1, y1, &ax, &ay);
    map_xy(x2, y2, &bx, &by);
    int X1 = iround(ax), Y1 = iround(ay), X2 = iround(bx), Y2 = iround(by);
    if (X1 > X2) { int t = X1; X1 = X2; X2 = t; }
    if (Y1 > Y2) { int t = Y1; Y1 = Y2; Y2 = t; }
    if (X1 < 0 || Y1 < 0 || X2 >= S.w || Y2 >= S.h) rt_error(5);
    int w = X2 - X1 + 1, h = Y2 - Y1 + 1;
    long o = 0;
    int bpp = S.colors == 256 ? 8 : S.colors == 16 ? 4 : S.colors == 4 ? 2 : 1;
    int wfield = (bpp == 8) ? w * 8 : (bpp == 4 ? w : w * bpp);
    arr_setbyte(a, index, o++, wfield & 0xFF); arr_setbyte(a, index, o++, (wfield >> 8) & 0xFF);
    arr_setbyte(a, index, o++, h & 0xFF); arr_setbyte(a, index, o++, (h >> 8) & 0xFF);
    for (int y = 0; y < h; y++) {
        if (bpp == 8) {
            for (int x = 0; x < w; x++) arr_setbyte(a, index, o++, getpix(X1 + x, Y1 + y));
        } else if (bpp == 4) {
            int bpr = (w + 7) / 8;
            for (int pl = 0; pl < 4; pl++)
                for (int bx2 = 0; bx2 < bpr; bx2++) {
                    int byte = 0;
                    for (int k = 0; k < 8; k++) {
                        int x = bx2 * 8 + k;
                        if (x < w && (getpix(X1 + x, Y1 + y) >> pl) & 1) byte |= 0x80 >> k;
                    }
                    arr_setbyte(a, index, o++, byte);
                }
        } else {
            int ppb = 8 / bpp;
            int bpr = (w + ppb - 1) / ppb;
            for (int bx2 = 0; bx2 < bpr; bx2++) {
                int byte = 0;
                for (int k = 0; k < ppb; k++) {
                    int x = bx2 * ppb + k;
                    if (x < w) byte |= (getpix(X1 + x, Y1 + y) & ((1 << bpp) - 1)) << (8 - bpp * (k + 1));
                }
                arr_setbyte(a, index, o++, byte);
            }
        }
    }
}

void gfx_put(double x, double y, Array *a, int index, int action, int step) {
    need_gfx();
    if (a->t == T_STR || a->t == T_UDT) rt_error(13);
    if (step) { x += S.lastx; y += S.lasty; }
    double px, py;
    map_xy(x, y, &px, &py);
    int X = iround(px), Y = iround(py);
    int bpp = S.colors == 256 ? 8 : S.colors == 16 ? 4 : S.colors == 4 ? 2 : 1;
    long o = 0;
    int wfield = arr_getbyte(a, index, o) | (arr_getbyte(a, index, o + 1) << 8); o += 2;
    int h = arr_getbyte(a, index, o) | (arr_getbyte(a, index, o + 1) << 8); o += 2;
    int w = (bpp == 8) ? wfield / 8 : (bpp == 4 ? wfield : wfield / bpp);
    /* QBLike: PUT is clipped to the CLIP rectangle (and the VIEW viewport) instead of failing */
    int cx1 = S.clx1 > S.vx1 ? S.clx1 : S.vx1, cy1 = S.cly1 > S.vy1 ? S.cly1 : S.vy1;
    int cx2 = S.clx2 < S.vx2 ? S.clx2 : S.vx2, cy2 = S.cly2 < S.vy2 ? S.cly2 : S.vy2;
    for (int yy = 0; yy < h; yy++) {
        int rowpix[2048];
        if (w > 2048) rt_error(5);
        if (bpp == 8) {
            for (int xx = 0; xx < w; xx++) rowpix[xx] = arr_getbyte(a, index, o++);
        } else if (bpp == 4) {
            int bpr = (w + 7) / 8;
            for (int xx = 0; xx < w; xx++) rowpix[xx] = 0;
            for (int pl = 0; pl < 4; pl++)
                for (int bx2 = 0; bx2 < bpr; bx2++) {
                    int byte = arr_getbyte(a, index, o++);
                    for (int k = 0; k < 8; k++) {
                        int xx = bx2 * 8 + k;
                        if (xx < w && (byte & (0x80 >> k))) rowpix[xx] |= 1 << pl;
                    }
                }
        } else {
            int ppb = 8 / bpp;
            int bpr = (w + ppb - 1) / ppb;
            for (int bx2 = 0; bx2 < bpr; bx2++) {
                int byte = arr_getbyte(a, index, o++);
                for (int k = 0; k < ppb; k++) {
                    int xx = bx2 * ppb + k;
                    if (xx < w) rowpix[xx] = (byte >> (8 - bpp * (k + 1))) & ((1 << bpp) - 1);
                }
            }
        }
        int mask = S.colors == 256 ? 255 : S.colors - 1;
        for (int xx = 0; xx < w; xx++) {
            int sx = X + xx, sy = Y + yy;
            if (sx < cx1 || sx > cx2 || sy < cy1 || sy > cy2) continue;
            int d = getpix(sx, sy), s2 = rowpix[xx], r;
            if (action == 5) { if (s2 == 0) continue; r = s2; }
            else switch (action) {
                case 0: r = s2; break;
                case 1: r = (~s2) & mask; break;
                case 2: r = d & s2; break;
                case 4: r = d | s2; break;
                default: r = d ^ s2; break;
            }
            S.pix[S.apage][sy * S.w + sx] = (uint8_t)r;
        }
    }
    S.lastx = x; S.lasty = y;
}

void gfx_palette(int attr, long color, int has) {
    if (!has) {
        default_palette();
        return;
    }
    if (S.colors == 256) {
        if (attr < 0 || attr > 255) rt_error(5);
        if (color < 0) return;
        S.rgb[attr][0] = (uint8_t)(color & 63);
        S.rgb[attr][1] = (uint8_t)((color >> 8) & 63);
        S.rgb[attr][2] = (uint8_t)((color >> 16) & 63);
        return;
    }
    if (attr < 0 || attr > 15) rt_error(5);
    if (color < 0) return;
    if (S.mode == 12 || S.mode == 11) {
        /* VGA: color = 65536*b + 256*g + r */
        int dac = 16 + attr;
        S.rgb[dac][0] = (uint8_t)(color & 63);
        S.rgb[dac][1] = (uint8_t)((color >> 8) & 63);
        S.rgb[dac][2] = (uint8_t)((color >> 16) & 63);
        S.attrpal[attr] = dac;
        return;
    }
    /* EGA-style: color is 0..63 EGA index (mode 0/9) or 0..15 (mode 7/8) */
    int c = (int)color;
    if (S.mode == 7 || S.mode == 8 || S.mode == 1 || S.mode == 2) c = ega_default[c & 15];
    int dac = 16 + attr;
    uint8_t r, g, b;
    ega_rgb(c & 63, &r, &g, &b);
    S.rgb[dac][0] = r; S.rgb[dac][1] = g; S.rgb[dac][2] = b;
    S.attrpal[attr] = dac;
}

void gfx_palette_using(Array *a, int index) {
    if (!a->alive) rt_error(5);
    int n = S.colors == 256 ? 256 : 16;
    for (int i = 0; i < n && index + i < a->count; i++) {
        long c = (long)a->v[index + i].n;
        if (c >= 0) gfx_palette(i, c, 1);
    }
}

void gfx_pcopy(int src, int dst) {
    if (src < 0 || dst < 0 || src >= S.npages || dst >= S.npages) rt_error(5);
    if (S.mode == 0) memcpy(S.text[dst], S.text[src], sizeof(uint16_t) * (size_t)(S.cols * S.rows));
    else {
        memcpy(S.pix[dst], S.pix[src], (size_t)(S.w * S.h));
        memcpy(S.gtext[dst], S.gtext[src], (size_t)(S.cols * S.rows));
    }
}

/* ---- direct video memory emulation (PEEK/POKE/BLOAD/BSAVE) ----
   SCREEN 13       A000: linear, one byte per pixel (64000 bytes)
   SCREEN 7-12     A000: planar EGA/VGA, W/8 bytes per line and plane; writes go to the planes enabled
                   in the map mask (OUT &H3C4,2: OUT &H3C5,mask), reads come from the read map plane
                   (OUT &H3CE,4: OUT &H3CF,plane). Pages follow each other at 8K/16K/32K.
   SCREEN 1, 2     B800: CGA, even lines at offset 0, odd lines at &H2000, 80 bytes per line
   SCREEN 0        B800: character / attribute pairs, one page every 2K/4K/8K                       */
static int planar_page_stride(void) {
    switch (S.mode) { case 7: return 0x2000; case 8: return 0x4000; case 9: case 10: return 0x8000; }
    return 0x10000;
}
/* locate the 8 pixels behind planar byte o; returns pointer to the first one or NULL */
static uint8_t *planar_pixels(long o) {
    int stride = planar_page_stride();
    long page = o / stride, po = o % stride;
    int bpl = S.w / 8;
    if (page >= S.npages || po >= (long)bpl * S.h) return NULL;
    return S.pix[page] + (po / bpl) * S.w + (po % bpl) * 8;
}
static uint8_t *cga_pixels(long o, int *ppb) {
    if (o >= 0x4000) return NULL;
    int bank = o >= 0x2000, r = (int)(o & 0x1FFF);
    int line = (r / 80) * 2 + bank;
    if (line >= S.h) return NULL;
    *ppb = S.mode == 1 ? 4 : 8;
    return S.pix[0] + (long)line * S.w + (r % 80) * *ppb;
}
static long text_page_stride(void) { return ((long)S.cols * S.rows * 2 + 2047) & ~2047L; }
static int is_planar_mode(void) { return S.mode >= 7 && S.mode <= 12; }

int gfx_peek(int seg, int off, int *ok) {
    *ok = 0;
    long lin = (long)seg * 16 + off;
    if (lin >= 0xA0000 && lin < 0xB0000 && S.mode == 13) {
        long o = lin - 0xA0000;
        *ok = 1;
        if (o < S.w * S.h) return S.pix[S.apage][o];
        return 0;
    }
    if (lin >= 0xA0000 && lin < 0xB0000 && is_planar_mode()) {
        uint8_t *px = planar_pixels(lin - 0xA0000);
        *ok = 1;
        if (!px) return 0;
        int v = 0;
        for (int k = 0; k < 8; k++) if ((px[k] >> vga_read_map) & 1) v |= 0x80 >> k;
        return v;
    }
    if (lin >= 0xB8000 && lin < 0xC0000 && (S.mode == 1 || S.mode == 2)) {
        int ppb; uint8_t *px = cga_pixels(lin - 0xB8000, &ppb);
        *ok = 1;
        if (!px) return 0;
        int v = 0;
        if (ppb == 4) for (int k = 0; k < 4; k++) v |= (px[k] & 3) << (6 - 2 * k);
        else for (int k = 0; k < 8; k++) if (px[k] & 1) v |= 0x80 >> k;
        return v;
    }
    if (lin >= 0xB8000 && lin < 0xC0000 && S.mode == 0) {
        long o = lin - 0xB8000;
        long st = text_page_stride();
        long pg = o / st;
        o %= st;
        *ok = 1;
        if (pg >= S.npages || o / 2 >= S.cols * S.rows) return 0;
        uint16_t c = S.text[pg][o / 2];
        if (o & 1) return c >> 8;
        return c & 0xFF;
    }
    if (lin >= 0x46C && lin <= 0x46F) {
        *ok = 1;
        uint32_t ticks = (uint32_t)(sys_timer() * 18.2065);
        return (ticks >> (8 * (lin - 0x46C))) & 0xFF;
    }
    if (lin == 0x417) { *ok = 1; return kb_shift_state(); }
    return 0;
}

void gfx_poke(int seg, int off, int v) {
    long lin = (long)seg * 16 + off;
    if (lin >= 0xA0000 && lin < 0xB0000 && S.mode == 13) {
        long o = lin - 0xA0000;
        if (o < S.w * S.h) S.pix[S.apage][o] = (uint8_t)v;
        return;
    }
    if (lin >= 0xA0000 && lin < 0xB0000 && is_planar_mode()) {
        uint8_t *px = planar_pixels(lin - 0xA0000);
        if (!px) return;
        int cmask = S.colors - 1;
        for (int k = 0; k < 8; k++) {
            int bit = (v >> (7 - k)) & 1, c = px[k];
            for (int p = 0; p < 4; p++)
                if ((vga_map_mask >> p) & 1) c = bit ? (c | (1 << p)) : (c & ~(1 << p));
            px[k] = (uint8_t)(c & cmask);
        }
        return;
    }
    if (lin >= 0xB8000 && lin < 0xC0000 && (S.mode == 1 || S.mode == 2)) {
        int ppb; uint8_t *px = cga_pixels(lin - 0xB8000, &ppb);
        if (!px) return;
        if (ppb == 4) for (int k = 0; k < 4; k++) px[k] = (uint8_t)((v >> (6 - 2 * k)) & 3);
        else for (int k = 0; k < 8; k++) px[k] = (uint8_t)((v >> (7 - k)) & 1);
        return;
    }
    if (lin >= 0xB8000 && lin < 0xC0000 && S.mode == 0) {
        long o = lin - 0xB8000;
        long st = text_page_stride();
        long pg = o / st;
        o %= st;
        if (pg >= S.npages || o / 2 >= S.cols * S.rows) return;
        uint16_t c = S.text[pg][o / 2];
        if (o & 1) c = (uint16_t)((c & 0xFF) | ((v & 0xFF) << 8));
        else c = (uint16_t)((c & 0xFF00) | (v & 0xFF));
        S.text[pg][o / 2] = c;
    }
}

void gfx_out(int port, int v) {
    if (port == 0x3C8) { S.dac_write_idx = v & 255; S.dac_write_comp = 0; return; }
    if (port == 0x3C7) { S.dac_read_idx = v & 255; S.dac_read_comp = 0; return; }
    if (port == 0x3C9) {
        int idx = S.dac_write_idx;
        int dac = idx;
        if (S.colors <= 16 && idx < 16) dac = S.attrpal[idx] == idx ? idx : S.attrpal[idx];
        S.rgb[dac][S.dac_write_comp] = (uint8_t)(v & 63);
        if (++S.dac_write_comp == 3) { S.dac_write_comp = 0; S.dac_write_idx = (S.dac_write_idx + 1) & 255; }
        return;
    }
    if (port == 0x3C4) { vga_seq_idx = v & 0x1F; return; }
    if (port == 0x3C5) { if (vga_seq_idx == 2) vga_map_mask = v & 0x0F; return; }
    if (port == 0x3CE) { vga_gc_idx = v & 0x1F; return; }
    if (port == 0x3CF) { if (vga_gc_idx == 4) vga_read_map = v & 3; return; }
    /* other ports (speaker etc.) ignored */
}

/* VGA input status register 1 (&H3DA): bit 3 = vertical retrace, emulated at 70 Hz.
   Entering retrace presents the frame, so classic VSYNC loops give tear-free output. */
static int vr_last = 0;
static int vsync_status(void) {
    const double period = 1.0 / 70.0, vr_len = 0.0011;
    double t = sys_time();
    double ph = fmod(t, period);
    int in = ph < vr_len;
    if (in && !vr_last) { kb_poll(); scr_present(1); }
    vr_last = in;
    if (!in) {
        double until = period - ph;
        if (until > 0.003) sys_sleep(1);   /* don't burn the CPU in polling loops */
    }
    return in ? 9 : 1;   /* bit 0 = display disabled (also set during retrace) */
}

int gfx_inp(int port) {
    if (port == 0x3DA || port == 0x3BA) return vsync_status();
    if (port == 0x60) return kb_lastscan();
    if (port == 0x3C4) return vga_seq_idx;
    if (port == 0x3C5) return vga_seq_idx == 2 ? vga_map_mask : 0;
    if (port == 0x3CE) return vga_gc_idx;
    if (port == 0x3CF) return vga_gc_idx == 4 ? vga_read_map : 0;
    if (port == 0x3C9) {
        int v = S.rgb[S.dac_read_idx][S.dac_read_comp];
        if (++S.dac_read_comp == 3) { S.dac_read_comp = 0; S.dac_read_idx = (S.dac_read_idx + 1) & 255; }
        return v;
    }
    return 0;
}

void gfx_waitvsync(void) {
    /* emulate waiting for vertical retrace at ~70 Hz */
    static double next = 0;
    double t = sys_time();
    double period = 1.0 / 70.0;
    if (next < t - period) next = t;
    next += period;
    kb_poll();
    scr_present(1);
    while (sys_time() < next) {
        double rem = next - sys_time();
        if (rem > 0.002) sys_sleep(1);
    }
}

/* ---- BSAVE / BLOAD ----
   File format: &HFD, segment, offset, length (16-bit little-endian words), then the data.
   Only video memory is emulated: segment &HA000 (graphics) and &HB800 (text, CGA). Other segments
   are ignored. The array form (VARPTR(array(i)) as offset) is handled by gfx_bload_array.        */
void file_native_name(const char *in, int len, char *out, int outsz, int for_read);

static FILE *bfile_open(const char *name, int namelen, int save) {
    char path[1024];
    file_native_name(name, namelen, path, sizeof path, !save);
    FILE *f = fopen(path, save ? "wb" : "rb");
    if (!f) rt_error(save ? 75 : 53);   /* Path/File access error, File not found */
    return f;
}
static void bfile_header(FILE *f, int seg, long off, long len) {
    unsigned char h[7] = { 0xFD, (unsigned char)(seg & 0xFF), (unsigned char)((seg >> 8) & 0xFF),
                           (unsigned char)(off & 0xFF), (unsigned char)((off >> 8) & 0xFF),
                           (unsigned char)(len & 0xFF), (unsigned char)((len >> 8) & 0xFF) };
    fwrite(h, 1, 7, f);
}
static void bfile_read_header(FILE *f, int *seg, long *off, long *len) {
    unsigned char h[7];
    if (fread(h, 1, 7, f) != 7 || h[0] != 0xFD) { fclose(f); rt_error(54); }   /* Bad file mode */
    *seg = h[1] | (h[2] << 8);
    *off = h[3] | (h[4] << 8);
    *len = h[5] | (h[6] << 8);
}
static int video_seg(int seg) { return seg == 0xA000 || seg == 0xB800; }

void gfx_bload(const char *name, int namelen, int has, long off, int save, long len) {
    off &= 0xFFFF;              /* &HFA00 is -1536 as an INTEGER: offsets and lengths are unsigned words */
    len &= 0xFFFF;
    int defseg = rs ? rs->defseg : 0;
    if (save) {
        FILE *f = bfile_open(name, namelen, 1);
        bfile_header(f, defseg, off, len);
        for (long i = 0; i < len; i++) {
            int ok;
            long o = off + i;
            fputc(o <= 0xFFFF ? gfx_peek(defseg, (int)o, &ok) : 0, f);
        }
        fclose(f);
        return;
    }
    FILE *f = bfile_open(name, namelen, 0);
    int fseg; long foff, flen;
    bfile_read_header(f, &fseg, &foff, &flen);
    /* with an explicit offset the data goes to DEF SEG when it points to video memory;
       otherwise to the segment stored in the file */
    int seg = (has && video_seg(defseg)) ? defseg : fseg;
    if (!has) off = foff;
    if (video_seg(seg)) {
        for (long i = 0; i < flen; i++) {
            int c = fgetc(f);
            long o = off + i;
            if (c == EOF || o > 0xFFFF) break;   /* SCREEN 13: anything past 64000 lands nowhere */
            gfx_poke(seg, (int)o, c);
        }
    }
    fclose(f);
}

/* raw bytes of a numeric array: element by element, so SINGLE/DOUBLE are rebuilt whole */
static void arr_set_raw(Array *a, long idx, const unsigned char *b) {
    Value *v = &a->v[idx];
    switch (a->t) {
        case T_INT: { int16_t x; memcpy(&x, b, 2); v->n = x; break; }
        case T_LNG: { int32_t x; memcpy(&x, b, 4); v->n = x; break; }
        case T_SNG: { float x; memcpy(&x, b, 4); v->n = x; break; }
        case T_DBL: { double x; memcpy(&x, b, 8); v->n = x; break; }
    }
}

void gfx_bload_array(const char *name, int namelen, Array *a, int index, int save, long len) {
    if (a->t == T_STR || a->t == T_UDT) rt_error(13);
    int es = elem_size(a);
    long avail = (long)(a->count - index) * es;
    if (save) {
        len &= 0xFFFF;
        if (len > avail) rt_error(5);      /* asked to save more than the array holds */
        FILE *f = bfile_open(name, namelen, 1);
        bfile_header(f, rs ? rs->defseg : 0, ((long)index * es) & 0xFFFF, len);
        for (long i = 0; i < len; i++) fputc(arr_getbyte(a, index, i), f);
        fclose(f);
        return;
    }
    FILE *f = bfile_open(name, namelen, 0);
    int fseg; long foff, flen;
    bfile_read_header(f, &fseg, &foff, &flen);
    if (flen > avail) { fclose(f); rt_error(5); }   /* like GET: the array is too small */
    unsigned char *buf = (unsigned char *)malloc((size_t)flen + 8);
    long n = (long)fread(buf, 1, (size_t)flen, f);
    fclose(f);
    for (long e = 0; e * es < n; e++) {
        unsigned char b[8];
        long take = n - e * es; if (take > es) take = es;
        for (int k = 0; k < es; k++) b[k] = (unsigned char)(k < take ? buf[e * es + k] : arr_getbyte(a, index, e * es + k));
        arr_set_raw(a, index + e, b);
    }
    free(buf);
}

/* used by the IDE to obtain program screen text for F4 when in text mode */
int scr_is_text(void) { return S.mode == 0; }

/* "Press any key to continue" */
void scr_press_any_key(void) {
    int row = S.rows - 1;
    int savevb = S.vbot;
    int savex = S.cx, savey = S.cy;
    /* remember the bottom row so it can be restored afterwards */
    uint16_t savetext[132];
    uint8_t *savepix = NULL, savegt[132];
    if (S.mode == 0) memcpy(savetext, &S.text[S.apage][row * S.cols], sizeof(uint16_t) * (size_t)S.cols);
    else {
        savepix = (uint8_t *)malloc((size_t)(S.w * S.ch));
        int y0 = row * S.ch;
        int hh = (y0 + S.ch <= S.h) ? S.ch : S.h - y0;
        if (savepix && hh > 0) memcpy(savepix, S.pix[S.apage] + y0 * S.w, (size_t)(S.w * hh));
        memcpy(savegt, S.gtext[S.apage] + row * S.cols, (size_t)S.cols);
    }
    S.cy = row;
    S.cx = 0;
    if (S.mode == 0) {
        int fg = S.fg, bg = S.bg;
        S.fg = 7; S.bg = 0;
        const char *msg = "Press any key to continue";
        for (int i = 0; i < S.cols; i++) put_char_at(i, row, ' ');
        for (int i = 0; msg[i]; i++) put_char_at(i, row, (unsigned char)msg[i]);
        S.fg = fg; S.bg = bg;
    } else {
        const char *msg = "Press any key to continue";
        int fg = S.fg;
        S.fg = S.colors == 256 ? 15 : S.colors - 1;
        for (int i = 0; msg[i] && i < S.cols; i++) put_char_at(i, row, (unsigned char)msg[i]);
        S.fg = fg;
    }
    S.vbot = savevb;
    scr_present(1);
    kb_clear();
    char k[2];
    while (!sys_shutting_down()) {
        kb_poll();
        if (kb_get(k)) break;
        if (kb_break_pressed()) break;
        scr_present(0);
        sys_sleep(10);
    }
    if (S.mode == 0) memcpy(&S.text[S.apage][row * S.cols], savetext, sizeof(uint16_t) * (size_t)S.cols);
    else {
        int y0 = row * S.ch;
        int hh = (y0 + S.ch <= S.h) ? S.ch : S.h - y0;
        if (savepix && hh > 0) memcpy(S.pix[S.apage] + y0 * S.w, savepix, (size_t)(S.w * hh));
        memcpy(S.gtext[S.apage] + row * S.cols, savegt, (size_t)S.cols);
        free(savepix);
    }
    S.cx = savex; S.cy = savey;
}

/* dump text screen to a FILE (headless tests) */
void scr_dump_text(FILE *f) {
    for (int y = 0; y < S.rows; y++) {
        char line[256];
        int n = 0;
        for (int x = 0; x < S.cols; x++) {
            int c = S.mode == 0 ? (S.text[S.vpage][y * S.cols + x] & 0xFF) : S.gtext[S.vpage][y * S.cols + x];
            line[n++] = (char)(c >= 32 && c < 127 ? c : (c == 0 ? ' ' : '?'));
        }
        while (n > 0 && line[n - 1] == ' ') n--;
        line[n] = 0;
        fprintf(f, "%s\n", line);
    }
}

/* write current visible program screen as PPM (headless tests) */
void scr_dump_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    if (S.mode == 0) {
        int W = S.cols * 8, H = S.rows * 16;
        fprintf(f, "P6\n%d %d\n255\n", W, H);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint16_t c = S.text[S.vpage][(y / 16) * S.cols + x / 8];
                int ch = c & 0xFF, at = c >> 8;
                int on = glyph16[ch][y % 16] & (0x80 >> (x % 8));
                int col = on ? (at & 15) : ((at >> 4) & 7);
                int d = S.attrpal[col];
                fputc(S.rgb[d][0] * 4, f); fputc(S.rgb[d][1] * 4, f); fputc(S.rgb[d][2] * 4, f);
            }
    } else {
        fprintf(f, "P6\n%d %d\n255\n", S.w, S.h);
        for (int i = 0; i < S.w * S.h; i++) {
            int p = S.pix[S.vpage][i];
            int d = S.colors == 256 ? p : S.attrpal[p & 15];
            fputc(S.rgb[d][0] * 4, f); fputc(S.rgb[d][1] * 4, f); fputc(S.rgb[d][2] * 4, f);
        }
    }
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* image support for LOADBMP / LOADGIF / LOADPAL                       */
/* ------------------------------------------------------------------ */
int gfx_colors(void) { return S.mode == 0 ? 0 : S.colors; }

/* draw an indexed image at logical (x, y); clipped to the viewport */
void gfx_blit_image(double x, double y, int w, int h, const uint8_t *pix) {
    need_gfx();
    double px, py;
    map_xy(x, y, &px, &py);
    int X = iround(px), Y = iround(py);
    int mask = S.colors == 256 ? 255 : S.colors - 1;
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++) {
            int sx = X + xx, sy = Y + yy;
            if (sx < S.clx1 || sx > S.clx2 || sy < S.cly1 || sy > S.cly2) continue;
            plot(sx, sy, pix[yy * w + xx] & mask);
        }
    S.lastx = x; S.lasty = y;
}

/* bytes needed to store an image in GET/PUT format for the current mode */
static long image_bytes(int w, int h) {
    int bpp = S.colors == 256 ? 8 : S.colors == 16 ? 4 : S.colors == 4 ? 2 : 1;
    long rowbytes;
    if (bpp == 8) rowbytes = w;
    else if (bpp == 4) rowbytes = 4L * ((w + 7) / 8);
    else rowbytes = (w + (8 / bpp) - 1) / (8 / bpp);
    return 4 + rowbytes * h;
}

/* store an image into a numeric array in GET/PUT format; returns elements used */
int gfx_store_image(Array *a, int index, int w, int h, const uint8_t *pix) {
    need_gfx();
    if (a->t == T_STR || a->t == T_UDT) rt_error(13);
    int es = elem_size(a);
    long need = image_bytes(w, h);
    long avail = (long)(a->count - index) * es;
    if (index < 0 || need > avail) rt_error(5);   /* like GET: array too small */
    int bpp = S.colors == 256 ? 8 : S.colors == 16 ? 4 : S.colors == 4 ? 2 : 1;
    int mask = (1 << bpp) - 1;
    long o = 0;
    int wfield = (bpp == 8) ? w * 8 : (bpp == 4 ? w : w * bpp);
    arr_setbyte(a, index, o++, wfield & 0xFF); arr_setbyte(a, index, o++, (wfield >> 8) & 0xFF);
    arr_setbyte(a, index, o++, h & 0xFF); arr_setbyte(a, index, o++, (h >> 8) & 0xFF);
    for (int y = 0; y < h; y++) {
        const uint8_t *row = pix + (long)y * w;
        if (bpp == 8) {
            for (int x = 0; x < w; x++) arr_setbyte(a, index, o++, row[x]);
        } else if (bpp == 4) {
            int bpr = (w + 7) / 8;
            for (int pl = 0; pl < 4; pl++)
                for (int bx = 0; bx < bpr; bx++) {
                    int byte = 0;
                    for (int k = 0; k < 8; k++) { int x = bx * 8 + k; if (x < w && ((row[x] & mask) >> pl) & 1) byte |= 0x80 >> k; }
                    arr_setbyte(a, index, o++, byte);
                }
        } else {
            int ppb = 8 / bpp, bpr = (w + ppb - 1) / ppb;
            for (int bx = 0; bx < bpr; bx++) {
                int byte = 0;
                for (int k = 0; k < ppb; k++) { int x = bx * ppb + k; if (x < w) byte |= (row[x] & mask) << (8 - bpp * (k + 1)); }
                arr_setbyte(a, index, o++, byte);
            }
        }
    }
    return (int)((need + es - 1) / es);
}

/* set palette entry from 8-bit RGB (LOADxxx ..., PALETTE) */
void gfx_set_palette_rgb8(int idx, int r, int g, int b) {
    need_gfx();
    if (S.colors == 256) {
        if (idx < 0 || idx > 255) return;
        S.rgb[idx][0] = (uint8_t)(r >> 2); S.rgb[idx][1] = (uint8_t)(g >> 2); S.rgb[idx][2] = (uint8_t)(b >> 2);
        return;
    }
    if (idx < 0 || idx >= S.colors) return;
    int dac = 16 + idx;
    S.rgb[dac][0] = (uint8_t)(r >> 2); S.rgb[dac][1] = (uint8_t)(g >> 2); S.rgb[dac][2] = (uint8_t)(b >> 2);
    S.attrpal[idx] = dac;
}

/* CLIP x1, y1, x2, y2  /  CLIP (reset to whole screen) */
void gfx_clip(int x1, int y1, int x2, int y2, int has) {
    need_gfx();
    if (!has) { S.clx1 = 0; S.cly1 = 0; S.clx2 = S.w - 1; S.cly2 = S.h - 1; return; }
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > S.w - 1) x2 = S.w - 1;
    if (y2 > S.h - 1) y2 = S.h - 1;
    S.clx1 = x1; S.cly1 = y1; S.clx2 = x2; S.cly2 = y2;
}
void gfx_clip_reset(void) { if (S.mode != 0) { S.clx1 = 0; S.cly1 = 0; S.clx2 = S.w - 1; S.cly2 = S.h - 1; } }

/* MOUSE x, y, buttons: position in the current mode's resolution (cells in SCREEN 0),
   -1,-1 when the pointer is outside the picture or the window; bit 0 left, bit 1 right */
void sys_mouse(int *x, int *y, int *b) {
    *x = mousex();
    *y = mousey();
    if (*x < 0 || *y < 0) { *x = -1; *y = -1; }
    else {
        int maxx = S.mode == 0 ? S.cols - 1 : S.w - 1, maxy = S.mode == 0 ? S.rows - 1 : S.h - 1;
        if (*x > maxx || *y > maxy) { *x = -1; *y = -1; }
    }
    *b = (keystate(KEY_LBUTTON) ? 1 : 0) | (keystate(KEY_RBUTTON) ? 2 : 0);
}

/* ide.c - QBasic-style integrated development environment */
#include "qb.h"
#include "source.h"
#include "doslike/dos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define getcwd _getcwd
#define chdir _chdir
#else
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

/* externals from other modules */
void present_text_cells(const uint16_t *cells, int cols, int rows, int cx, int cy, int curvis);
void scr_set_ide_owner(int ide);
void scr_show_output(void);
void scr_press_any_key(void);
int  kb_get_code(void);
int  mouse_get(int *type, int *x, int *y);
int  mouse_button(void);
int  rt_exec_immediate(Program *p, int startpc);
void truncate_code(Program *p, int n);
const char *help_lookup(const char *topic);
const char *help_index_text(void);
int  cp437_to_unicode(int c);

int g_ide_active = 0;

/* ------------------------------------------------------------------ */
/* configuration                                                      */
/* ------------------------------------------------------------------ */
static int C_TEXT_FG = 7, C_TEXT_BG = 1;
static int C_CUR_FG = 15, C_CUR_BG = 1;     /* current statement */
static int C_BRK_FG = 7, C_BRK_BG = 4;      /* breakpoint lines */
static int opt_scrollbars = 1;
static int opt_tabstops = 4;
static int opt_syntax = 1;
static int opt_rows = 25;
static int opt_highlight = 1;
static int opt_crt = 1;
static int opt_speed = 0;
extern int g_speed_limit;
static const int speed_values[4] = { 0, 60000, 250000, 1200000 };
static int C_KW = 15, C_STR = 14, C_NUM = 11, C_REM = 3, C_LABEL = 10;

#define SW 80
static int SH = 25;
static uint16_t cells[SW * 60];

/* layout */
static int ED_TOP = 2;          /* first text row of edit window */
static int ED_ROWS = 18;
static int IMM_SEP = 21;
static int IMM_TOP = 22, IMM_ROWS = 2;
static int imm_visible = 1;

/* ------------------------------------------------------------------ */
/* document state                                                     */
/* ------------------------------------------------------------------ */
static Doc doc;
static int curpart = 0;
static char filename[512] = "";
static int modified = 0;
static int code_changed = 0;     /* since last compile while suspended */

static char linebuf[1024];
static int linebuf_line = -1;    /* line in curpart being edited (or -1) */
static int linebuf_dirty = 0;

static int focus_imm = 0;
static Part imm;                 /* immediate window lines */
static int imm_cy = 0, imm_cx = 0, imm_top = 0;
static char immbuf[1024];

static int overwrite = 0;
static int sel_on = 0, sel_ay = 0, sel_ax = 0;


typedef struct BP { int part, line; } BP;
static BP *bps = NULL;
static int nbps = 0;

/* undo */
typedef struct Snap { int part; char **lines; int n; int cy, cx; } Snap;
static Snap undo_stack[64];
static int nundo = 0;

/* search */
static char find_text[256] = "";
static char change_text[256] = "";
static int find_case = 0, find_word = 0;

/* run state */
static Program *prog = NULL;
static int prog_valid = 0;       /* compiled program reflects current doc */
static int running = 0;          /* inside rt_run */
static int suspended = 0;        /* program paused in debugger */
static int cur_stmt_part = -1, cur_stmt_line = -1;
static int trace_on = 0;
static int pending_action = 0;
static int quit_requested = 0;

enum { PA_NONE, PA_NEW, PA_OPEN, PA_EXIT, PA_START, PA_LOADFILE };

/* ------------------------------------------------------------------ */
/* low-level drawing                                                  */
/* ------------------------------------------------------------------ */
static inline uint16_t cell(int ch, int fg, int bg) { return (uint16_t)((ch & 0xFF) | ((fg & 15) << 8) | ((bg & 15) << 12)); }
static void put(int x, int y, int ch, int fg, int bg) { if (x >= 0 && x < SW && y >= 0 && y < SH) cells[y * SW + x] = cell(ch, fg, bg); }
static void puts_at(int x, int y, const char *s, int fg, int bg) { for (; *s; s++, x++) put(x, y, (unsigned char)*s, fg, bg); }
static void putsn_at(int x, int y, const char *s, int n, int fg, int bg) { for (int i = 0; i < n; i++) put(x + i, y, (unsigned char)s[i], fg, bg); }
static void fill(int x, int y, int w, int h, int ch, int fg, int bg) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) put(x + i, y + j, ch, fg, bg); }
static void setattr(int x, int y, int fg, int bg) { if (x >= 0 && x < SW && y >= 0 && y < SH) cells[y * SW + x] = (uint16_t)((cells[y * SW + x] & 0xFF) | (fg << 8) | (bg << 12)); }
static void shadow(int x, int y, int w, int h) {
    for (int j = 1; j <= h; j++) { setattr(x + w, y + j, 8, 0); setattr(x + w + 1, y + j, 8, 0); }
    for (int i = 2; i < w + 2; i++) setattr(x + i, y + h, 8, 0);
}
static void box(int x, int y, int w, int h, int fg, int bg) {
    put(x, y, 218, fg, bg); put(x + w - 1, y, 191, fg, bg);
    put(x, y + h - 1, 192, fg, bg); put(x + w - 1, y + h - 1, 217, fg, bg);
    for (int i = 1; i < w - 1; i++) { put(x + i, y, 196, fg, bg); put(x + i, y + h - 1, 196, fg, bg); }
    for (int j = 1; j < h - 1; j++) { put(x, y + j, 179, fg, bg); put(x + w - 1, y + j, 179, fg, bg); }
}
/* text with &-hotkey: "&File" */
static void hot_at(int x, int y, const char *s, int fg, int bg, int hotfg) {
    int hot = 0;
    for (; *s; s++) {
        if (*s == '&' && !hot) { hot = 1; s++; put(x++, y, (unsigned char)*s, hotfg, bg); continue; }
        put(x++, y, (unsigned char)*s, fg, bg);
    }
}
static int hot_len(const char *s) { int n = 0; for (; *s; s++) if (*s != '&') n++; return n; }
static int hot_key(const char *s) { const char *a = strchr(s, '&'); return a ? toupper((unsigned char)a[1]) : 0; }

static int cur_x = 0, cur_y = 0, cur_vis = 0;
static int dlg_block_cursor = 0;   /* a dialog is on screen: steady block cursor on the focused control */
static void present(void) {
    if (dlg_block_cursor && cur_vis && cur_x >= 0 && cur_x < SW && cur_y >= 0 && cur_y < SH) {
        /* the blinking text cursor alone can be hard to see in a dialog: invert the cell as well */
        static uint16_t tmp[SW * 60];
        memcpy(tmp, cells, sizeof(uint16_t) * (size_t)(SW * SH));
        uint16_t c = tmp[cur_y * SW + cur_x];
        int fg = (c >> 8) & 15, bg = (c >> 12) & 15;
        tmp[cur_y * SW + cur_x] = (uint16_t)((c & 0xFF) | (bg << 8) | (fg << 12));
        present_text_cells(tmp, SW, SH, cur_x, cur_y, cur_vis);
        return;
    }
    present_text_cells(cells, SW, SH, cur_x, cur_y, cur_vis);
}

/* ------------------------------------------------------------------ */
/* key input                                                          */
/* ------------------------------------------------------------------ */
enum {
    K_NONE = 0,
    K_UP = 0x10000, K_DOWN, K_LEFT, K_RIGHT, K_HOME, K_END, K_PGUP, K_PGDN, K_INS, K_DEL,
    K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10, K_F11, K_F12,
    K_CLEFT, K_CRIGHT, K_CHOME, K_CEND, K_CPGUP, K_CPGDN, K_STAB, K_ALT, K_MOUSE, K_CUP, K_CDOWN
};
#define K_SHIFT 0x100000
#define K_CTRL  0x200000
#define K_ALTM  0x400000

static int shift_held(void) { return keystate(KEY_SHIFT) || keystate(KEY_LSHIFT) || keystate(KEY_RSHIFT); }
static int ctrl_held(void) { return keystate(KEY_CONTROL) || keystate(KEY_LCONTROL) || keystate(KEY_RCONTROL); }

static int mouse_type = 0, mouse_x = 0, mouse_y = 0;

static int translate_code(int raw) {
    int mods = raw >> 16;
    int code = raw & 0xFFFF;
    int sh = (mods & 1) ? K_SHIFT : 0;
    int ct = (mods & 2) ? K_CTRL : 0;
    if (code == 0xF000) return K_ALT;
    if (code & 0xFF00) {
        int sc = code >> 8;
        switch (sc) {
            case 72: return K_UP | sh; case 80: return K_DOWN | sh; case 75: return K_LEFT | sh; case 77: return K_RIGHT | sh;
            case 71: return K_HOME | sh; case 79: return K_END | sh; case 73: return K_PGUP | sh; case 81: return K_PGDN | sh;
            case 82: return K_INS | sh | ct; case 83: return K_DEL | sh | ct;
            case 115: return K_CLEFT | sh; case 116: return K_CRIGHT | sh; case 119: return K_CHOME | sh; case 117: return K_CEND | sh;
            case 132: return K_CPGUP; case 118: return K_CPGDN; case 141: return K_CUP; case 145: return K_CDOWN;
            case 15: return K_STAB;
            case 133: return K_F11; case 134: return K_F12;
        }
        if (sc >= 59 && sc <= 68) return K_F1 + (sc - 59);
        if (sc >= 84 && sc <= 93) return (K_F1 + (sc - 84)) | K_SHIFT;
        if (sc >= 94 && sc <= 103) return (K_F1 + (sc - 94)) | K_CTRL;
        if (sc >= 104 && sc <= 113) return (K_F1 + (sc - 104)) | K_ALTM;
        /* Alt+letter: scancode */
        static const char *row1 = "QWERTYUIOP", *row2 = "ASDFGHJKL", *row3 = "ZXCVBNM";
        if (sc >= 16 && sc <= 25) return K_ALTM | row1[sc - 16];
        if (sc >= 30 && sc <= 38) return K_ALTM | row2[sc - 30];
        if (sc >= 44 && sc <= 50) return K_ALTM | row3[sc - 44];
        return K_NONE;
    }
    if (code == 8 && ct) return 8 | K_CTRL;   /* Ctrl+H */
    if (code == 6 && ct && sh) return 6 | K_CTRL | K_SHIFT;   /* Ctrl+Shift+F: Format Code */
    return code;
}

static int g_idle_redraw = 0;

static int get_key(void) {
    for (;;) {
        kb_poll();
        int t, x, y;
        if (mouse_get(&t, &x, &y)) { if (getenv("QBLIKE_KEYLOG")) fprintf(stderr, "mouse t=%d x=%d y=%d\n", t, x, y); mouse_type = t; mouse_x = x; mouse_y = y; return K_MOUSE; }
        int c = kb_get_code();
        if (c >= 0) {
            int k = translate_code(c);
            if (getenv("QBLIKE_KEYLOG")) fprintf(stderr, "key code=%x -> %x\n", c, k);
            if (k != K_NONE) return k;
            continue;
        }
        if (sys_shutting_down()) return K_NONE;
        if (g_idle_redraw) return K_NONE;
        present();
        sys_sleep(8);
    }
}

/* ------------------------------------------------------------------ */
/* identifier case dictionary + line formatting                        */
/* ------------------------------------------------------------------ */
typedef struct CaseEnt { char up[64]; char sp[64]; } CaseEnt;
static CaseEnt *cdict = NULL;
static int ncdict = 0;

static void id_base(const char *s, char *base, int sz, char *suffix) {
    int l = (int)strlen(s);
    *suffix = 0;
    if (l > 0 && strchr("%&!#$", s[l - 1])) { *suffix = s[l - 1]; l--; }
    if (l >= sz) l = sz - 1;
    memcpy(base, s, (size_t)l);
    base[l] = 0;
}
static CaseEnt *cdict_find(const char *base) {
    char up[64];
    snprintf(up, sizeof up, "%s", base);
    str_upper(up);
    for (int i = 0; i < ncdict; i++) if (strcmp(cdict[i].up, up) == 0) return &cdict[i];
    return NULL;
}
static int cdict_set(const char *base, int overwrite_existing) {
    CaseEnt *e = cdict_find(base);
    if (e) {
        if (overwrite_existing && strcmp(e->sp, base) != 0) { snprintf(e->sp, sizeof e->sp, "%s", base); return 1; }
        return 0;
    }
    cdict = (CaseEnt *)qb_realloc(cdict, sizeof(CaseEnt) * (size_t)(ncdict + 1));
    e = &cdict[ncdict++];
    snprintf(e->up, sizeof e->up, "%s", base);
    str_upper(e->up);
    snprintf(e->sp, sizeof e->sp, "%s", base);
    return 0;
}

static int is_func_kw(int kw) {
    switch (kw) {
        case KW_ABS: case KW_ASC: case KW_ATN: case KW_CDBL: case KW_CHR_S: case KW_CINT: case KW_CLNG: case KW_COS:
        case KW_CSNG: case KW_CVD: case KW_CVI: case KW_CVL: case KW_CVS: case KW_CVSMBF: case KW_CVDMBF: case KW_ENVIRON_S:
        case KW_EOF: case KW_EXP: case KW_FILEATTR: case KW_FIX: case KW_FRE: case KW_HEX_S: case KW_INP: case KW_INPUT_S:
        case KW_INSTR: case KW_INT: case KW_LBOUND: case KW_LCASE_S: case KW_LEFT_S: case KW_LEN: case KW_LOC: case KW_LOF:
        case KW_LOG: case KW_LPOS: case KW_LTRIM_S: case KW_MID_S: case KW_MKD_S: case KW_MKI_S: case KW_MKL_S: case KW_MKS_S:
        case KW_MKSMBF_S: case KW_MKDMBF_S: case KW_OCT_S: case KW_PEEK: case KW_PEN: case KW_PLAY: case KW_PMAP: case KW_POINT:
        case KW_POS: case KW_RIGHT_S: case KW_RND: case KW_RTRIM_S: case KW_SADD: case KW_SCREEN: case KW_SEEK: case KW_SGN:
        case KW_SIN: case KW_SPACE_S: case KW_SPC: case KW_SQR: case KW_STICK: case KW_STR_S: case KW_STRIG: case KW_STRING_S:
        case KW_TAB: case KW_TAN: case KW_UBOUND: case KW_UCASE_S: case KW_VAL: case KW_VARPTR: case KW_VARPTR_S: case KW_VARSEG:
        case KW_IOCTL_S: case KW_ERDEV: case KW_ERDEV_S: case KW_MULTIKEY: case KW_TIMER:
            return 1;
    }
    return 0;
}
static int is_value_kw(int kw) {
    switch (kw) {
        case KW_TIMER: case KW_RND: case KW_INKEY_S: case KW_CSRLIN: case KW_DATE_S: case KW_TIME_S: case KW_ERR: case KW_ERL:
        case KW_FREEFILE: case KW_ERDEV: case KW_ERDEV_S:
            return 1;
    }
    return 0;
}
static int is_op_kw(int kw) { return kw == KW_AND || kw == KW_OR || kw == KW_XOR || kw == KW_EQV || kw == KW_IMP || kw == KW_MOD; }

enum { PT_NONE, PT_WORD, PT_KW, PT_BIN, PT_UNARY, PT_LPAR, PT_RPAR, PT_COMMA, PT_SEMI, PT_COLON, PT_HASH, PT_DOT, PT_NOTKW };

static int tokens_equal(TokLine *a, TokLine *b) {
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++) {
        Tok *x = &a->t[i], *y = &b->t[i];
        if (x->k != y->k) return 0;
        switch (x->k) {
            case TK_KW: case TK_OP: if (x->kw != y->kw) return 0; break;
            case TK_ID: if (!str_ieq(x->s, y->s)) return 0; break;
            case TK_NUM: if (x->num != y->num || x->ntype != y->ntype) return 0; break;
            case TK_STR: if (x->slen != y->slen || memcmp(x->s, y->s, (size_t)x->slen)) return 0; break;
            case TK_DATA: case TK_REM: break;
        }
    }
    return 1;
}

/* Reformat a line like the QBasic editor: keywords upper case, consistent identifier case,
   spaces around operators. Returns 1 if out differs. */
/* FILL is a QBLike statement but not a reserved word: token i of tl is that statement when it starts
   a statement, is followed by "(" or STEP, and the program has no SUB/FUNCTION called FILL */
static int fill_stmt_at(TokLine *tl, int i) {
    Tok *t = &tl->t[i];
    if (t->k != TK_ID) return 0;
    if (qb_stricmp(t->s, "VSYNC") == 0) {             /* WAIT VSYNC */
        Tok *n = i + 1 < tl->n ? &tl->t[i + 1] : NULL;
        return i > 0 && tl->t[i - 1].k == TK_KW && tl->t[i - 1].kw == KW_WAIT &&
               (!n || n->k == TK_EOL || n->k == TK_REM || (n->k == TK_OP && n->kw == ':') || (n->k == TK_KW && n->kw == KW_ELSE));
    }
    if (qb_stricmp(t->s, "FULL") == 0) {              /* SCREEN FULL */
        Tok *n = i + 1 < tl->n ? &tl->t[i + 1] : NULL;
        return i > 0 && tl->t[i - 1].k == TK_KW && tl->t[i - 1].kw == KW_SCREEN &&
               (!n || n->k == TK_EOL || n->k == TK_REM || (n->k == TK_OP && n->kw == ':') || (n->k == TK_KW && n->kw == KW_ELSE));
    }
    if (qb_stricmp(t->s, "VOLUME") == 0)               /* MUSIC VOLUME */
        return i > 0 && fill_stmt_at(tl, i - 1) && qb_stricmp(tl->t[i - 1].s, "MUSIC") == 0 &&
               i + 1 < tl->n && tl->t[i + 1].k != TK_EOL && tl->t[i + 1].k != TK_REM &&
               !(tl->t[i + 1].k == TK_OP && (tl->t[i + 1].kw == ':' || tl->t[i + 1].kw == ','));
    if (qb_stricmp(t->s, "MUSIC") == 0) {              /* MUSIC file$ | STOP | VOLUME */
        if (i > 0) {
            Tok *p = &tl->t[i - 1];
            int ok = (p->k == TK_OP && p->kw == ':') || (p->k == TK_KW && (p->kw == KW_THEN || p->kw == KW_ELSE)) ||
                     (i == 1 && p->k == TK_NUM);
            if (!ok) return 0;
        }
        if (i + 1 >= tl->n) return 0;
        Tok *n = &tl->t[i + 1];
        if (n->k == TK_EOL || n->k == TK_REM || (n->k == TK_OP && (n->kw == '=' || n->kw == ':' || n->kw == '('))) return 0;
        for (int pi = 1; pi < doc.nparts; pi++) if (qb_stricmp(doc.parts[pi].name, "MUSIC") == 0) return 0;
        return 1;
    }
    int vs = 0;
    if (qb_stricmp(t->s, "FILL") != 0) return 0;
    if (i > 0) {
        Tok *p = &tl->t[i - 1];
        int ok = (p->k == TK_OP && p->kw == ':') || (p->k == TK_KW && (p->kw == KW_THEN || p->kw == KW_ELSE)) ||
                 (i == 1 && p->k == TK_NUM);
        if (!ok) return 0;
    }
    if (i + 1 >= tl->n) return 0;
    Tok *n = &tl->t[i + 1];
    if (vs) {
        if (!(n->k == TK_EOL || n->k == TK_REM || (n->k == TK_OP && n->kw == ':') || (n->k == TK_KW && n->kw == KW_ELSE))) return 0;
    } else if (!((n->k == TK_OP && n->kw == '(') || (n->k == TK_KW && n->kw == KW_STEP))) return 0;
    for (int pi = 1; pi < doc.nparts; pi++) if (qb_stricmp(doc.parts[pi].name, vs ? "VSYNC" : "FILL") == 0) return 0;
    return 1;
}

static int format_line(const char *in, char *out, int outsz, int update_case) {
    TokLine tl;
    lex_line(in, &tl);
    if (tl.err) { lex_free(&tl); snprintf(out, (size_t)outsz, "%s", in); return 0; }
    char buf[2048];
    int o = 0;
    int ind = 0;
    while (in[ind] == ' ') ind++;
    for (int i = 0; i < ind && o < 1000; i++) buf[o++] = ' ';
    int prev = PT_NONE;
    int firstkw = 0;
    for (int i = 0; i < tl.n; i++) {
        Tok *t = &tl.t[i];
        if (t->k == TK_EOL) break;
        if (firstkw == 0 && t->k == TK_KW) firstkw = t->kw;
        char txt[1100];
        int cls = PT_WORD;
        int tight_minus = 0;
        switch (t->k) {
            case TK_KW: {
                if (t->kw == KW_PRINT && t->slen == 1 && t->s[0] == '?') snprintf(txt, sizeof txt, "PRINT");
                else snprintf(txt, sizeof txt, "%s", kw_text(t->kw));
                if (is_op_kw(t->kw)) cls = PT_BIN;
                else if (t->kw == KW_NOT) cls = PT_NOTKW;
                else if (is_value_kw(t->kw) || is_func_kw(t->kw)) cls = PT_WORD;
                else cls = PT_KW;
                break;
            }
            case TK_ID: {
                char base[64], sfx;
                if (fill_stmt_at(&tl, i)) { int k = 0; for (; t->s[k] && k < 40; k++) txt[k] = (char)toupper((unsigned char)t->s[k]); txt[k] = 0; cls = PT_KW; break; }
                if (firstkw == KW_DEFINT || firstkw == KW_DEFLNG || firstkw == KW_DEFSNG || firstkw == KW_DEFDBL || firstkw == KW_DEFSTR) {
                    /* letter ranges of DEFtype are not variable names: upper case, kept out of the case dictionary */
                    snprintf(txt, sizeof txt, "%s", t->s);
                    for (char *q = txt; *q; q++) *q = (char)toupper((unsigned char)*q);
                    break;
                }
                id_base(t->s, base, sizeof base, &sfx);
                CaseEnt *e = cdict_find(base);
                if (!e || update_case) { cdict_set(base, update_case); e = cdict_find(base); }
                snprintf(txt, sizeof txt, "%s%c", e ? e->sp : base, sfx ? sfx : '\0');
                if (!sfx) txt[strlen(e ? e->sp : base)] = 0;
                break;
            }
            case TK_NUM: {
                int l = t->endcol - t->col;
                if (l > 1000) l = 1000;
                memcpy(txt, in + t->col, (size_t)l);
                txt[l] = 0;
                for (int k = 0; txt[k]; k++) if (txt[k] == 'h' || txt[k] == 'o' || txt[k] == 'e' || txt[k] == 'd') txt[k] = (char)toupper((unsigned char)txt[k]);
                break;
            }
            case TK_STR: {
                int l = t->slen > 1000 ? 1000 : t->slen;
                txt[0] = '"';
                memcpy(txt + 1, t->s, (size_t)l);
                txt[l + 1] = '"';
                txt[l + 2] = 0;
                break;
            }
            case TK_DATA: snprintf(txt, sizeof txt, "%s", t->s); break;
            case TK_REM: {
                int gap = 1;
                if (i > 0) { gap = t->col - tl.t[i - 1].endcol; if (gap < 1) gap = 1; }
                if (prev != PT_NONE) for (int g = 0; g < gap && o < 1900; g++) buf[o++] = ' ';
                if (t->kw == 1) { snprintf(txt, sizeof txt, "REM%s", in + t->col + 3); }
                else snprintf(txt, sizeof txt, "%s", in + t->col);
                int l = (int)strlen(txt);
                if (o + l < 2000) { memcpy(buf + o, txt, (size_t)l); o += l; }
                prev = PT_WORD;
                goto next;
            }
            case TK_OP: {
                int c = t->kw;
                if (c == OP_LE) strcpy(txt, "<="); else if (c == OP_GE) strcpy(txt, ">="); else if (c == OP_NE) strcpy(txt, "<>");
                else { txt[0] = (char)c; txt[1] = 0; }
                switch (c) {
                    case '(': cls = PT_LPAR; break;
                    case ')': cls = PT_RPAR; break;
                    case ',': cls = PT_COMMA; break;
                    case ';': cls = PT_SEMI; break;
                    case ':': cls = PT_COLON; break;
                    case '#': cls = PT_HASH; break;
                    case '.': cls = PT_DOT; break;
                    case '-': case '+':
                        if (prev == PT_NONE || prev == PT_BIN || prev == PT_UNARY || prev == PT_LPAR || prev == PT_COMMA ||
                            prev == PT_SEMI || prev == PT_COLON || prev == PT_KW || prev == PT_NOTKW) cls = PT_UNARY;
                        else cls = PT_BIN;
                        if (c == '-' && (firstkw == KW_DEFINT || firstkw == KW_DEFLNG || firstkw == KW_DEFSNG || firstkw == KW_DEFDBL || firstkw == KW_DEFSTR))
                            tight_minus = 1;                  /* DEFINT A-Z */
                        if (c == '-' && prev == PT_RPAR && i + 1 < tl.n &&
                            ((tl.t[i + 1].k == TK_OP && tl.t[i + 1].kw == '(') || (tl.t[i + 1].k == TK_KW && tl.t[i + 1].kw == KW_STEP)) &&
                            (firstkw == KW_LINE || firstkw == KW_GET || firstkw == KW_VIEW || firstkw == KW_WINDOW)) tight_minus = 1;
                        break;
                    default: cls = PT_BIN; break;
                }
                break;
            }
            default: txt[0] = 0;
        }
        {
            int space = 0;
            if (prev == PT_NONE) space = 0;
            else if (tight_minus) space = 0;
            else if (cls == PT_COMMA || cls == PT_SEMI || cls == PT_COLON || cls == PT_RPAR || cls == PT_DOT) space = 0;
            else if (prev == PT_LPAR || prev == PT_DOT || prev == PT_UNARY || prev == PT_HASH) space = 0;
            else if (prev == PT_COMMA || prev == PT_SEMI || prev == PT_COLON) space = 1;
            else if (cls == PT_LPAR) space = (prev == PT_KW || prev == PT_BIN || prev == PT_NOTKW ||
                     (prev == PT_WORD && i == 2 && (firstkw == KW_SUB || firstkw == KW_FUNCTION || firstkw == KW_CALL)) ||
                     (prev == PT_WORD && i == 3 && firstkw == KW_DECLARE)) ? 1 : 0;
            else if (cls == PT_HASH) space = (prev == PT_KW) ? 1 : 0;
            else if (prev == PT_RPAR && tight_minus) space = 0;
            else space = 1;
            if (space && o < 1990) buf[o++] = ' ';
        }
        {
            int l = (int)strlen(txt);
            if (o + l < 2000) { memcpy(buf + o, txt, (size_t)l); o += l; }
        }
        if (tight_minus) cls = PT_LPAR; /* no space after either */
        prev = cls;
    next:;
    }
    buf[o] = 0;
    lex_free(&tl);
    /* verify the reformatted line means the same */
    TokLine a, b;
    lex_line(in, &a);
    lex_line(buf, &b);
    int ok = !b.err && tokens_equal(&a, &b);
    lex_free(&a);
    lex_free(&b);
    if (!ok || (int)strlen(buf) >= outsz) { snprintf(out, (size_t)outsz, "%s", in); return 0; }
    snprintf(out, (size_t)outsz, "%s", buf);
    return strcmp(in, buf) != 0;
}

/* ------------------------------------------------------------------ */
/* editor core                                                        */
/* ------------------------------------------------------------------ */
static Part *P(void) { return &doc.parts[curpart]; }

static const char *line_text(int i) {
    Part *p = P();
    if (i == linebuf_line) return linebuf;
    if (i < 0 || i >= p->n) return "";
    return p->lines[i];
}
static int nlines(void) { return P()->n; }

static void ensure_lines(void) { if (P()->n == 0) part_insert(P(), 0, ""); }

static void bp_shift(int part, int line, int delta) {
    for (int i = 0; i < nbps; i++) {
        if (bps[i].part != part) continue;
        if (delta < 0 && bps[i].line == line) { bps[i] = bps[--nbps]; i--; continue; }
        if (bps[i].line >= line) bps[i].line += delta;
    }
}

static void clear_redo(void);
static void snapshot(void) {
    clear_redo();
    Part *p = P();
    if (nundo == 64) {
        for (int i = 0; i < undo_stack[0].n; i++) free(undo_stack[0].lines[i]);
        free(undo_stack[0].lines);
        memmove(&undo_stack[0], &undo_stack[1], sizeof(Snap) * 63);
        nundo--;
    }
    Snap *s = &undo_stack[nundo++];
    s->part = curpart;
    s->n = p->n;
    s->lines = (char **)qb_alloc(sizeof(char *) * (size_t)(p->n + 1));
    for (int i = 0; i < p->n; i++) s->lines[i] = qb_strdup(i == linebuf_line ? linebuf : p->lines[i]);
    s->cy = p->cy; s->cx = p->cx;
}

typedef struct Snap Snap_;
static void clear_redo(void);
static void mark_modified(void) {
    modified = 1;
    prog_valid = 0;
    if (suspended) code_changed = 1;
}

/* commit current line buffer into the document, with formatting and syntax check */
static void show_message(const char *msg, int help);
static int syntax_check_line(int part, int line, char *msg, int msgsz, int *col);
static void handle_new_proc_line(int line);

static void commit_line(void) {
    if (linebuf_line < 0) return;
    int ln = linebuf_line;
    linebuf_line = -1;
    if (!linebuf_dirty) return;
    linebuf_dirty = 0;
    Part *p = P();
    if (ln >= p->n) return;
    char out[1100];
    /* trim trailing spaces */
    int l = (int)strlen(linebuf);
    while (l > 0 && linebuf[l - 1] == ' ') linebuf[--l] = 0;
    format_line(linebuf, out, sizeof out, 1);
    part_set(p, ln, out);
    /* re-case identifiers everywhere */
    for (int pi = 0; pi < doc.nparts; pi++) {
        Part *q = &doc.parts[pi];
        for (int i = 0; i < q->n; i++) {
            if (pi == curpart && i == ln) continue;
            char o2[1100];
            /* quick check: only lines sharing an identifier would change; format is idempotent otherwise */
            if (format_line(q->lines[i], o2, sizeof o2, 0)) {
                /* only accept case changes, not spacing reformat of untouched lines */
                if (strlen(o2) == strlen(q->lines[i]) && str_ieq(o2, q->lines[i])) part_set(q, i, o2);
            }
        }
    }
    mark_modified();
    /* SUB/FUNCTION typed in the main module creates a new procedure window */
    int c = classify_line(p->lines[ln], NULL, 0);
    if ((c == 1 || c == 2) && curpart == 0) { handle_new_proc_line(ln); return; }
    if (opt_syntax) {
        char msg[160];
        int col = 0;
        if (!syntax_check_line(curpart, ln, msg, sizeof msg, &col)) {
            p->cy = ln;
            p->cx = col;
            show_message(msg, 1);
        }
    }
}

static void load_line(int i) {
    if (linebuf_line == i) return;
    commit_line();
    if (i < 0 || i >= P()->n) return;
    snprintf(linebuf, sizeof linebuf, "%s", P()->lines[i]);
    linebuf_line = i;
    linebuf_dirty = 0;
}

static void lb_store(void) {
    /* keep document copy in sync with buffer without formatting (for drawing, search...) */
    if (linebuf_line >= 0 && linebuf_line < P()->n) part_set(P(), linebuf_line, linebuf);
}

static void clamp_cursor(void) {
    Part *p = P();
    ensure_lines();
    if (p->cy < 0) p->cy = 0;
    if (p->cy >= p->n) p->cy = p->n - 1;
    if (p->cx < 0) p->cx = 0;
    if (p->cx > 255) p->cx = 255;
    if (p->cy < p->top) p->top = p->cy;
    if (p->cy >= p->top + ED_ROWS) p->top = p->cy - ED_ROWS + 1;
    if (p->top < 0) p->top = 0;
    int vis = SW - 2 - (opt_scrollbars ? 1 : 0);
    if (p->cx < p->left) p->left = p->cx;
    if (p->cx >= p->left + vis) p->left = p->cx - vis + 1;
}

static void move_to_line(int y) {
    Part *p = P();
    if (y != p->cy) { commit_line(); }
    p->cy = y;
    clamp_cursor();
}

static void switch_part(int pi) {
    commit_line();
    if (pi < 0 || pi >= doc.nparts) return;
    curpart = pi;
    sel_on = 0;
    clamp_cursor();
}

/* selection helpers: normalized range (y0,x0)-(y1,x1), character based */
static void sel_range(int *y0, int *x0, int *y1, int *x1) {
    Part *p = P();
    int ay = sel_ay, ax = sel_ax, by = p->cy, bx = p->cx;
    if (ay > by || (ay == by && ax > bx)) { int t = ay; ay = by; by = t; t = ax; ax = bx; bx = t; }
    *y0 = ay; *x0 = ax; *y1 = by; *x1 = bx;
}
static int sel_nonempty(void) { return sel_on && (sel_ay != P()->cy || sel_ax != P()->cx); }

/* clipboard holds one string with '\n' separators */
static char *clipboard = NULL;
void sys_clip_set(const char *cp437);
char *sys_clip_get(void);

static void clip_set(const char *s) {
    free(clipboard);
    clipboard = qb_strdup(s);
    sys_clip_set(s);
}
static char *clip_get(void) {
    char *s = sys_clip_get();
    if (s) { free(clipboard); clipboard = s; }
    return clipboard;
}

/* text of a range */
static char *range_text(int y0, int x0, int y1, int x1) {
    int cap = 256, n = 0;
    char *b = (char *)qb_alloc((size_t)cap);
    for (int y = y0; y <= y1; y++) {
        const char *s = line_text(y);
        int l = (int)strlen(s);
        int a = (y == y0) ? x0 : 0;
        int e = (y == y1) ? x1 : l;
        if (a > l) a = l;
        if (e > l) e = l;
        int k = e - a;
        if (k < 0) k = 0;
        if (n + k + 2 >= cap) { cap = (n + k + 2) * 2; b = (char *)qb_realloc(b, (size_t)cap); }
        memcpy(b + n, s + a, (size_t)k);
        n += k;
        if (y < y1) b[n++] = '\n';
    }
    b[n] = 0;
    return b;
}

/* delete a range from the document (no undo snapshot) */
static void delete_range(int y0, int x0, int y1, int x1) {
    commit_line();
    linebuf_line = -1;
    Part *p = P();
    const char *a = p->lines[y0];
    const char *bl = p->lines[y1];
    int la = (int)strlen(a), lb = (int)strlen(bl);
    if (x0 > la) x0 = la;
    if (x1 > lb) x1 = lb;
    char *nl = (char *)qb_alloc((size_t)(x0 + (lb - x1) + 1));
    memcpy(nl, a, (size_t)x0);
    memcpy(nl + x0, bl + x1, (size_t)(lb - x1));
    nl[x0 + lb - x1] = 0;
    for (int y = y1; y > y0; y--) { part_delete(p, y); bp_shift(curpart, y, -1); }
    part_set(p, y0, nl);
    free(nl);
    p->cy = y0;
    p->cx = x0;
}

/* insert text (with '\n') at cursor */
static void insert_text(const char *t) {
    commit_line();
    linebuf_line = -1;
    Part *p = P();
    ensure_lines();
    const char *cur = p->lines[p->cy];
    int l = (int)strlen(cur);
    int x = p->cx;
    char *head = (char *)qb_alloc((size_t)x + 1);
    int hl = x < l ? x : l;
    memcpy(head, cur, (size_t)hl);
    for (int i = hl; i < x; i++) head[i] = ' ';
    head[x] = 0;
    char *tail = qb_strdup(x < l ? cur + x : "");
    const char *s = t;
    int y = p->cy;
    int first = 1;
    for (;;) {
        const char *e = strchr(s, '\n');
        int seg = e ? (int)(e - s) : (int)strlen(s);
        if (seg > 0 && s[seg - 1] == '\r') seg--;
        char *line;
        if (first) {
            line = (char *)qb_alloc((size_t)(strlen(head) + seg + (e ? 0 : strlen(tail)) + 1));
            strcpy(line, head);
            strncat(line, s, (size_t)seg);
            if (!e) strcat(line, tail);
            part_set(p, y, line);
        } else {
            line = (char *)qb_alloc((size_t)(seg + (e ? 0 : strlen(tail)) + 1));
            memcpy(line, s, (size_t)seg);
            line[seg] = 0;
            if (!e) strcat(line, tail);
            part_insert(p, y, line);
            bp_shift(curpart, y, 1);
        }
        if (!e) { p->cy = y; p->cx = (int)strlen(line) - (int)strlen(tail); free(line); break; }
        free(line);
        first = 0;
        y++;
        s = e + 1;
    }
    free(head);
    free(tail);
}

static void copy_selection(void) {
    if (!sel_nonempty()) return;
    commit_line();
    int y0, x0, y1, x1;
    sel_range(&y0, &x0, &y1, &x1);
    char *t = range_text(y0, x0, y1, x1);
    clip_set(t);
    free(t);
}

static void delete_selection(void) {
    if (!sel_nonempty()) { sel_on = 0; return; }
    commit_line();
    snapshot();
    int y0, x0, y1, x1;
    sel_range(&y0, &x0, &y1, &x1);
    delete_range(y0, x0, y1, x1);
    sel_on = 0;
    mark_modified();
    clamp_cursor();
}

/* Ctrl+C / Ctrl+X without a selection act on the whole current line, as in modern editors;
   pasting such a line inserts it above the cursor line */
static char *line_clip = NULL;
static void copy_line(void) {
    commit_line();
    Part *p = P();
    const char *t = line_text(p->cy);
    size_t l = strlen(t);
    char *b = (char *)qb_alloc(l + 2);
    memcpy(b, t, l); b[l] = '\n'; b[l + 1] = 0;
    clip_set(b);
    free(line_clip); line_clip = b;
}
static void cut_line(void) {
    copy_line();
    Part *p = P();
    snapshot();
    if (p->cy < p->n - 1) delete_range(p->cy, 0, p->cy + 1, 0);
    else delete_range(p->cy, 0, p->cy, (int)strlen(line_text(p->cy)));
    p->cx = 0;
    sel_on = 0;
    mark_modified();
    clamp_cursor();
}

static void format_lines(int y0, int y1) {
    Part *p = P();
    for (int y = y0; y <= y1 && y < p->n; y++) {
        char o[1100];
        if (format_line(p->lines[y], o, sizeof o, 0)) part_set(p, y, o);
    }
}

static void paste_clip(void) {
    char *t = clip_get();
    if (!t || !*t) return;
    if (sel_nonempty()) delete_selection();
    else {
        snapshot();
        /* whole line copied with Ctrl+C/X and no selection: it goes above (CR/LF-insensitive) */
        const char *a = t, *b = line_clip;
        while (b && *a && *b) { if (*a == '\r') { a++; continue; } if (*a != *b) break; a++; b++; }
        while (a && *a == '\r') a++;
        if (b && !*a && !*b) P()->cx = 0;
    }
    sel_on = 0;
    int y0 = P()->cy;
    insert_text(t);
    if (P()->cy > y0) format_lines(y0, P()->cy - 1);
    mark_modified();
    clamp_cursor();
}

/* redo stack mirrors undo */
static Snap redo_stack[64];
static int nredo = 0;

static void snap_take(Snap *s) {
    Part *p = P();
    s->part = curpart;
    s->n = p->n;
    s->lines = (char **)qb_alloc(sizeof(char *) * (size_t)(p->n + 1));
    for (int i = 0; i < p->n; i++) s->lines[i] = qb_strdup(i == linebuf_line ? linebuf : p->lines[i]);
    s->cy = p->cy; s->cx = p->cx;
}
static void snap_restore(Snap *s) {
    curpart = s->part;
    Part *p = P();
    for (int i = 0; i < p->n; i++) free(p->lines[i]);
    free(p->lines);
    p->n = s->n;
    p->cap = s->n + 1;
    p->lines = (char **)qb_realloc(s->lines, sizeof(char *) * (size_t)p->cap);
    p->cy = s->cy; p->cx = s->cx;
}

static void do_undo(void) {
    if (!nundo) return;
    commit_line();
    linebuf_line = -1;
    Snap *s = &undo_stack[--nundo];
    if (s->part >= doc.nparts) { for (int i = 0; i < s->n; i++) free(s->lines[i]); free(s->lines); return; }
    if (nredo < 64) { int save = curpart; curpart = s->part; snap_take(&redo_stack[nredo++]); curpart = save; }
    snap_restore(s);
    sel_on = 0;
    mark_modified();
    clamp_cursor();
}

static void do_redo(void) {
    if (!nredo) return;
    commit_line();
    linebuf_line = -1;
    Snap *s = &redo_stack[--nredo];
    if (s->part >= doc.nparts) { for (int i = 0; i < s->n; i++) free(s->lines[i]); free(s->lines); return; }
    if (nundo < 64) { int save = curpart; curpart = s->part; snap_take(&undo_stack[nundo++]); curpart = save; }
    snap_restore(s);
    sel_on = 0;
    mark_modified();
    clamp_cursor();
}

static int is_word_char(int c) { return isalnum(c) || c == '.' || c == '$' || c == '%' || c == '&' || c == '!' || c == '#' || c >= 128; }

/* ------------------------------------------------------------------ */
/* breakpoints                                                        */
/* ------------------------------------------------------------------ */
static int bp_index(int part, int line) {
    for (int i = 0; i < nbps; i++) if (bps[i].part == part && bps[i].line == line) return i;
    return -1;
}
int ide_is_breakpoint(int part, int line) { return nbps && bp_index(part, line) >= 0; }
static void bp_toggle(int part, int line) {
    int i = bp_index(part, line);
    if (i >= 0) { bps[i] = bps[--nbps]; return; }
    bps = (BP *)qb_realloc(bps, sizeof(BP) * (size_t)(nbps + 1));
    bps[nbps].part = part; bps[nbps].line = line; nbps++;
}

/* ------------------------------------------------------------------ */
/* commands and menus                                                 */
/* ------------------------------------------------------------------ */
enum {
    CMD_NONE, CMD_NEW, CMD_OPEN, CMD_SAVE, CMD_SAVEAS, CMD_PRINT, CMD_EXIT,
    CMD_UNDO, CMD_CUT, CMD_COPY, CMD_PASTE, CMD_CLEAR, CMD_NEWSUB, CMD_NEWFUNC, CMD_FORMAT,
    CMD_SUBS, CMD_SPLIT, CMD_OUTPUT,
    CMD_FIND, CMD_REPEAT, CMD_CHANGE,
    CMD_START, CMD_RESTART, CMD_CONTINUE,
    CMD_STEP, CMD_PSTEP, CMD_TRACE, CMD_TOGGLEBP, CMD_CLEARBP, CMD_SETNEXT,
    CMD_DISPLAY, CMD_HELPPATH, CMD_SYNTAX, CMD_SPEED,
    CMD_HELPINDEX, CMD_HELPCONTENTS, CMD_HELPTOPIC, CMD_HELPUSING, CMD_ABOUT
};

typedef struct MItem { const char *label; const char *key; int cmd; const char *help; } MItem;
typedef struct Menu { const char *title; int x; int n; MItem items[12]; } Menu;

static Menu menus[] = {
    { "&File", 2, 7, {
        { "&New", "", CMD_NEW, "Removes currently loaded file from memory" },
        { "&Open...", "", CMD_OPEN, "Loads new file into memory" },
        { "&Save", "", CMD_SAVE, "Writes current file to disk" },
        { "Save &As...", "", CMD_SAVEAS, "Saves current file with specified name" },
        { "-", "", 0, "" },
        { "&Print...", "", CMD_PRINT, "Prints specified text" },
        { "E&xit", "", CMD_EXIT, "Exits " QB_NAME " and returns to the operating system" } } },
    { "&Edit", 8, 10, {
        { "&Undo", "Alt+Backspace", CMD_UNDO, "Restores the text as it was before the last edit" },
        { "Cu&t", "Ctrl+X", CMD_CUT, "Deletes selected text (or the current line) and copies it to the clipboard" },
        { "&Copy", "Ctrl+C", CMD_COPY, "Copies selected text (or the current line) to the clipboard" },
        { "&Paste", "Ctrl+V", CMD_PASTE, "Inserts clipboard contents at current location" },
        { "Cl&ear", "Del", CMD_CLEAR, "Deletes selected text without copying it to buffer" },
        { "-", "", 0, "" },
        { "New &SUB...", "", CMD_NEWSUB, "Opens a window for a new subprogram" },
        { "New &FUNCTION...", "", CMD_NEWFUNC, "Opens a window for a new FUNCTION procedure" },
        { "-", "", 0, "" },
        { "Format C&ode...", "Ctrl+Shift+F", CMD_FORMAT, "Splits ':' statements, expands single-line IFs and re-indents the selection or view" } } },
    { "&View", 14, 3, {
        { "&SUBs...", "F2", CMD_SUBS, "Displays a loaded SUB, FUNCTION, module, or include file" },
        { "S&plit", "", CMD_SPLIT, "Divides screen into two View windows" },
        { "&Output Screen", "F4", CMD_OUTPUT, "Displays output screen" } } },
    { "&Search", 20, 3, {
        { "&Find...", "", CMD_FIND, "Finds specified text" },
        { "&Repeat Last Find", "F3", CMD_REPEAT, "Finds next occurrence of text specified in previous search" },
        { "&Change...", "", CMD_CHANGE, "Finds and changes specified text" } } },
    { "&Run", 28, 3, {
        { "&Start", "Shift+F5", CMD_START, "Runs current program" },
        { "&Restart", "", CMD_RESTART, "Clears variables in preparation for restarting single stepping" },
        { "Co&ntinue", "F5", CMD_CONTINUE, "Continues execution after a break" } } },
    { "&Debug", 33, 8, {
        { "&Step", "F8", CMD_STEP, "Executes next program statement" },
        { "&Procedure Step", "F10", CMD_PSTEP, "Executes next program statement, tracing over procedure calls" },
        { "-", "", 0, "" },
        { "&Trace On", "", CMD_TRACE, "Highlights each statement executed" },
        { "-", "", 0, "" },
        { "Toggle &Breakpoint", "F9", CMD_TOGGLEBP, "Sets/clears breakpoint at cursor location" },
        { "&Clear All Breakpoints", "", CMD_CLEARBP, "Removes all breakpoints" },
        { "Set &Next Statement", "", CMD_SETNEXT, "Makes the statement at the cursor the next statement to execute" } } },
    { "&Options", 40, 3, {
        { "&Display...", "", CMD_DISPLAY, "Changes display attributes" },
        { "Sp&eed...", "", CMD_SPEED, "Sets program speed (emulates the speed of old PCs)" },
        { "&Syntax Checking", "", CMD_SYNTAX, "Turns editor's syntax checking on or off" } } },
    { "&Help", 73, 6, {
        { "&Index", "", CMD_HELPINDEX, "Displays help index" },
        { "&Contents", "", CMD_HELPCONTENTS, "Displays help table of contents" },
        { "&Topic:", "F1", CMD_HELPTOPIC, "Displays information about the BASIC keyword the cursor is on" },
        { "&Using Help", "Shift+F1", CMD_HELPUSING, "Displays information about how to use online Help" },
        { "-", "", 0, "" },
        { "&About...", "", CMD_ABOUT, "Displays product version and copyright information" } } },
};
#define NMENUS ((int)(sizeof menus / sizeof menus[0]))

static int menu_open = -1;        /* -1 none */
static int menu_bar_active = 0;   /* Alt pressed: bar highlighted, no dropdown */
static int menu_sel = 0;
static char status_override[128] = "";

/* ------------------------------------------------------------------ */
/* drawing                                                            */
/* ------------------------------------------------------------------ */
static void layout(void) {
    SH = opt_rows;
    ED_TOP = 2;
    if (imm_visible) {
        IMM_ROWS = 2;
        IMM_SEP = SH - 1 - IMM_ROWS - 1;
        IMM_TOP = IMM_SEP + 1;
        ED_ROWS = IMM_SEP - ED_TOP - (opt_scrollbars ? 1 : 0);
    } else {
        IMM_SEP = SH - 1;
        ED_ROWS = SH - 1 - ED_TOP - (opt_scrollbars ? 1 : 0);
    }
}

static void window_title(char *out, int outsz) {
    const char *base = filename[0] ? filename : "Untitled";
    const char *slash = strrchr(base, '/');
    const char *bs = strrchr(base, '\\');
    if (bs && (!slash || bs > slash)) slash = bs;
    if (slash) base = slash + 1;
    char up[256];
    snprintf(up, sizeof up, "%s", base);
    if (filename[0]) str_upper(up);
    if (curpart > 0) {
        char pn[80];
        snprintf(pn, sizeof pn, "%s", doc.parts[curpart].name);
        str_upper(pn);
        snprintf(out, (size_t)outsz, "%s:%s", up, pn);
    } else snprintf(out, (size_t)outsz, "%s", up);
}

static void draw_menubar(void) {
    fill(0, 0, SW, 1, ' ', 0, 7);
    for (int i = 0; i < NMENUS; i++) {
        Menu *m = &menus[i];
        int sel = (menu_open == i) || (menu_bar_active && menu_sel == i);
        int fg = sel ? 7 : 0, bg = sel ? 0 : 7;
        int w = hot_len(m->title);
        fill(m->x - 1, 0, w + 2, 1, ' ', fg, bg);
        extern int kb_alt_held(void);
        int show = menu_bar_active || menu_open >= 0 || kb_alt_held();
        hot_at(m->x, 0, m->title, fg, bg, show ? 15 : fg);
    }
}

/* per-character colours for syntax highlighting */
static void syntax_colors(const char *s, unsigned char *out) {
    int l = (int)strlen(s);
    for (int i = 0; i < l; i++) out[i] = (unsigned char)C_TEXT_FG;
    TokLine tl;
    lex_line(s, &tl);
    for (int i = 0; i < tl.n; i++) {
        Tok *t = &tl.t[i];
        int c = -1;
        switch (t->k) {
            case TK_KW: c = C_KW; break;
            case TK_STR: c = C_STR; break;
            case TK_NUM: c = (i == 0) ? C_LABEL : C_NUM; break;
            case TK_REM: c = C_REM; break;
            case TK_DATA: c = C_STR; break;
            case TK_ID:
                if (i == 0 && tl.n > 1 && tl.t[1].k == TK_OP && tl.t[1].kw == ':') c = C_LABEL;
                else if (fill_stmt_at(&tl, i)) c = C_KW;
                break;
        }
        if (c < 0) continue;
        int a = t->col, b = t->k == TK_REM ? l : t->endcol;
        if (t->k == TK_REM && t->kw == 0 && a > 0 && s[a] != '\'') { /* col points at ' */ }
        for (int k = a; k < b && k < l; k++) out[k] = (unsigned char)c;
    }
    lex_free(&tl);
}

static int line_is_cur_stmt(int part, int line) { return (suspended || running) && part == cur_stmt_part && line == cur_stmt_line; }

static void draw_editor(void) {
    Part *p = P();
    int textw = SW - 2;
    int bottom = ED_TOP + ED_ROWS;
    /* frame */
    int fg = C_TEXT_FG, bg = C_TEXT_BG;
    put(0, 1, 218, fg, bg);
    for (int x = 1; x < SW - 1; x++) put(x, 1, 196, fg, bg);
    put(SW - 1, 1, 191, fg, bg);
    char title[200];
    window_title(title, sizeof title);
    char t2[210];
    snprintf(t2, sizeof t2, " %s ", title);
    int tl = (int)strlen(t2);
    int tx = (SW - tl) / 2;
    putsn_at(tx, 1, t2, tl, focus_imm ? fg : bg, focus_imm ? bg : fg);
    put(SW - 5, 1, 180, fg, bg);
    put(SW - 4, 1, 24, bg, fg);
    put(SW - 3, 1, 195, fg, bg);
    /* text */
    int y0 = 0, x0 = 0, y1 = -1, x1 = 0;
    if (sel_on) sel_range(&y0, &x0, &y1, &x1);
    for (int r = 0; r < ED_ROWS; r++) {
        int ln = p->top + r;
        int y = ED_TOP + r;
        put(0, y, 179, fg, bg);
        int lfg = C_TEXT_FG, lbg = C_TEXT_BG;
        if (ln < p->n && ide_is_breakpoint(curpart, ln)) { lfg = C_BRK_FG; lbg = C_BRK_BG; }
        if (ln < p->n && line_is_cur_stmt(curpart, ln)) { lfg = C_CUR_FG; lbg = C_CUR_BG; if (C_CUR_BG == C_TEXT_BG) { lfg = C_TEXT_BG; lbg = C_TEXT_FG; } }
        const char *s = ln < p->n ? line_text(ln) : "";
        int l = (int)strlen(s);
        unsigned char hl[1100];
        int plain = !(opt_highlight && lfg == C_TEXT_FG && lbg == C_TEXT_BG && l < 1000);
        if (!plain) syntax_colors(s, hl);
        for (int c = 0; c < textw - (opt_scrollbars ? 1 : 0); c++) {
            int col = p->left + c;
            int ch = col < l ? (unsigned char)s[col] : ' ';
            int cf = (!plain && col < l) ? hl[col] : lfg, cb = lbg;
            if (sel_on && ln < p->n && ln >= y0 && ln <= y1) {
                int insel;
                if (y0 == y1) insel = col >= x0 && col < x1;
                else if (ln == y0) insel = col >= x0;
                else if (ln == y1) insel = col < x1;
                else insel = 1;
                if (ln < y1 && col == l && col >= (ln == y0 ? x0 : 0)) insel = 1;  /* show line break */
                if (ln < y1 && col > l) insel = 0;
                if (insel) { cf = lbg; cb = lfg; }
            }
            put(1 + c, y, ch, cf, cb);
        }
        if (!opt_scrollbars) put(SW - 1, y, 179, fg, bg);
    }
    if (opt_scrollbars) {
        /* vertical */
        int vx = SW - 1;
        put(vx, ED_TOP, 24, 0, 7);
        for (int y = ED_TOP + 1; y < bottom - 1; y++) put(vx, y, 176, 0, 7);
        put(vx, bottom - 1, 25, 0, 7);
        int track = ED_ROWS - 3;
        if (track > 0) {
            int total = p->n > 1 ? p->n - 1 : 1;
            int pos = ED_TOP + 1 + (p->cy * track) / total;
            if (pos > bottom - 2) pos = bottom - 2;
            put(vx, pos, ' ', 0, 0);
        }
        /* horizontal */
        int hy = bottom;
        put(0, hy, 179, fg, bg);
        put(1, hy, 27, 0, 7);
        for (int x = 2; x < SW - 2; x++) put(x, hy, 176, 0, 7);
        put(SW - 2, hy, 26, 0, 7);
        put(SW - 1, hy, 179, fg, bg);
        int htrack = SW - 5;
        int hpos = 2 + (p->left * htrack) / 255;
        put(hpos, hy, ' ', 0, 0);
    }
}

static void draw_immediate(void) {
    if (!imm_visible) {
        return;
    }
    int fg = C_TEXT_FG, bg = C_TEXT_BG;
    put(0, IMM_SEP, 195, fg, bg);
    for (int x = 1; x < SW - 1; x++) put(x, IMM_SEP, 196, fg, bg);
    put(SW - 1, IMM_SEP, 180, fg, bg);
    const char *t = " Immediate ";
    int tl = (int)strlen(t);
    putsn_at((SW - tl) / 2, IMM_SEP, t, tl, focus_imm ? bg : fg, focus_imm ? fg : bg);
    for (int r = 0; r < IMM_ROWS; r++) {
        int y = IMM_TOP + r;
        int ln = imm_top + r;
        put(0, y, 179, fg, bg);
        put(SW - 1, y, 179, fg, bg);
        const char *s = ln == imm_cy ? immbuf : (ln < imm.n ? imm.lines[ln] : "");
        int l = (int)strlen(s);
        for (int c = 0; c < SW - 2; c++) put(1 + c, y, c < l ? (unsigned char)s[c] : ' ', fg, bg);
    }
}

static void draw_status(void) {
    int y = SH - 1;
    fill(0, y, SW, 1, ' ', 0, 3);
    if (status_override[0]) {
        puts_at(1, y, status_override, 0, 3);
        return;
    }
    if (running && !suspended) puts_at(1, y, "<Shift+F1=Help> <F5=Continue> <F9=Toggle Bkpt> <F8=Step>", 0, 3);
    else if (suspended) puts_at(1, y, "<Shift+F1=Help> <F5=Continue> <F9=Toggle Bkpt> <F8=Step>", 0, 3);
    else puts_at(1, y, "<Shift+F1=Help> <F6=Window> <F2=Subs> <F5=Run> <F8=Step>", 0, 3);
    char pos[32];
    Part *p = P();
    int ln = focus_imm ? imm_cy : p->cy;
    int cl = focus_imm ? imm_cx : p->cx;
    snprintf(pos, sizeof pos, "%c %05d:%03d", overwrite ? 'O' : ' ', ln + 1, cl + 1);
    put(SW - 15, y, 179, 0, 3);
    puts_at(SW - 13, y, pos, 0, 3);
}

static void draw_dropdown(void) {
    if (menu_open < 0) return;
    Menu *m = &menus[menu_open];
    int w = 0;
    for (int i = 0; i < m->n; i++) {
        int l = hot_len(m->items[i].label) + (int)strlen(m->items[i].key) + 4;
        if (l > w) w = l;
    }
    w += 4;
    if (w < 18) w = 18;
    int x = m->x - 2;
    if (x + w > SW - 2) x = SW - 2 - w;
    int h = m->n + 2;
    fill(x, 1, w, h, ' ', 0, 7);
    box(x, 1, w, h, 0, 7);
    for (int i = 0; i < m->n; i++) {
        MItem *it = &m->items[i];
        int y = 2 + i;
        if (it->label[0] == '-') {
            put(x, y, 195, 0, 7);
            for (int k = 1; k < w - 1; k++) put(x + k, y, 196, 0, 7);
            put(x + w - 1, y, 180, 0, 7);
            continue;
        }
        int sel = i == menu_sel;
        int fg = sel ? 7 : 0, bg = sel ? 0 : 7;
        int disabled = (it->cmd == CMD_PRINT || it->cmd == CMD_SPLIT || it->cmd == CMD_HELPPATH);
        if (disabled) fg = 8;
        fill(x + 1, y, w - 2, 1, ' ', fg, bg);
        int mark = (it->cmd == CMD_SYNTAX && opt_syntax) || (it->cmd == CMD_TRACE && trace_on);
        if (mark) put(x + 1, y, 251, fg, bg);
        hot_at(x + 2, y, it->label, fg, bg, disabled ? 8 : 15);
        if (it->key[0]) puts_at(x + w - 2 - (int)strlen(it->key), y, it->key, fg, bg);
    }
    shadow(x, 1, w, h);
}

static void draw_all(void) {
    dlg_block_cursor = 0;
    layout();
    fill(0, 0, SW, SH, ' ', C_TEXT_FG, C_TEXT_BG);
    draw_menubar();
    draw_editor();
    draw_immediate();
    draw_status();
    draw_dropdown();
    Part *p = P();
    if (menu_open >= 0 || menu_bar_active) cur_vis = 0;
    else if (focus_imm) { cur_vis = 1; cur_x = 1 + imm_cx; cur_y = IMM_TOP + (imm_cy - imm_top); if (cur_x > SW - 2) cur_x = SW - 2; }
    else { cur_vis = 1; cur_x = 1 + p->cx - p->left; cur_y = ED_TOP + p->cy - p->top; }
    if (cur_vis && overwrite && !focus_imm) {
        /* block cursor for overwrite mode */
        uint16_t c = cells[cur_y * SW + cur_x];
        int f = (c >> 8) & 15, b = (c >> 12) & 15;
        if ((((int)(sys_time() * 1000)) / 270) & 1) cells[cur_y * SW + cur_x] = (uint16_t)((c & 0xFF) | (b << 8) | (f << 12));
        cur_vis = 0;
    }
}

/* ------------------------------------------------------------------ */
/* dialogs                                                            */
/* ------------------------------------------------------------------ */
enum { CT_LABEL, CT_FIELD, CT_LIST, CT_CHECK, CT_BUTTON, CT_RADIO, CT_SEP };

typedef struct Ctl {
    int type, x, y, w, h;
    const char *label;
    char text[512];
    int pos, scroll;
    char **items;
    int nitems, sel, top;
    int checked;
    int id;
    int group;
    int selall;         /* field: whole text selected (typing replaces it) */
} Ctl;

typedef struct Dlg {
    const char *title;
    int x, y, w, h;
    Ctl c[24];
    int n;
    int focus;
    int defbutton;      /* id of default button */
    int cancel;         /* id returned by Esc */
    int (*on_list)(struct Dlg *, Ctl *, int dbl);   /* list activation hook */
    int (*on_change)(struct Dlg *, Ctl *);
} Dlg;

static Ctl *dlg_add(Dlg *d, int type, int x, int y, int w, int h, const char *label, int id) {
    Ctl *c = &d->c[d->n++];
    memset(c, 0, sizeof *c);
    c->type = type; c->x = x; c->y = y; c->w = w; c->h = h; c->label = label; c->id = id;
    return c;
}

static int focusable(Ctl *c) { return c->type == CT_FIELD || c->type == CT_LIST || c->type == CT_CHECK || c->type == CT_BUTTON || c->type == CT_RADIO; }

static void draw_dialog(Dlg *d) {
    int X = d->x, Y = d->y;
    fill(X, Y, d->w, d->h, ' ', 0, 7);
    box(X, Y, d->w, d->h, 0, 7);
    if (d->title && d->title[0]) {
        char t[100];
        snprintf(t, sizeof t, " %s ", d->title);
        puts_at(X + (d->w - (int)strlen(t)) / 2, Y, t, 0, 7);
    }
    shadow(X, Y, d->w, d->h);
    dlg_block_cursor = 1;
    int curx = -1, cury = -1;
    for (int i = 0; i < d->n; i++) {
        Ctl *c = &d->c[i];
        int x = X + c->x, y = Y + c->y;
        int foc = i == d->focus;
        switch (c->type) {
            case CT_LABEL: hot_at(x, y, c->label, 0, 7, 15); break;
            case CT_SEP:
                put(X, y, 195, 0, 7);
                for (int k = 1; k < d->w - 1; k++) put(X + k, y, 196, 0, 7);
                put(X + d->w - 1, y, 180, 0, 7);
                break;
            case CT_FIELD: {
                box(x - 1, y - 1, c->w + 2, 3, 0, 7);
                int l = (int)strlen(c->text);
                if (c->pos < c->scroll) c->scroll = c->pos;
                if (c->pos >= c->scroll + c->w) c->scroll = c->pos - c->w + 1;
                for (int k = 0; k < c->w; k++) {
                    int idx = c->scroll + k;
                    int hl = foc && c->selall && idx < l;
                    put(x + k, y, idx < l ? (unsigned char)c->text[idx] : ' ', hl ? 7 : 0, hl ? 0 : 7);
                }
                if (foc) { curx = x + c->pos - c->scroll; cury = y; }
                break;
            }
            case CT_LIST: {
                box(x - 1, y - 1, c->w + 2, c->h + 2, 0, 7);
                if (c->label) hot_at(x + (c->w - hot_len(c->label)) / 2, y - 1, c->label, 0, 7, 15);
                if (c->sel < c->top) c->top = c->sel;
                if (c->sel >= c->top + c->h) c->top = c->sel - c->h + 1;
                for (int r = 0; r < c->h; r++) {
                    int idx = c->top + r;
                    int sel = idx == c->sel && c->nitems > 0;
                    /* selected item: bright on black with the focus, white on dark gray without it */
                    int fg = sel ? 15 : 0, bg = sel ? (foc ? 0 : 8) : 7;
                    fill(x, y + r, c->w, 1, ' ', fg, bg);
                    if (idx < c->nitems) putsn_at(x + 1, y + r, c->items[idx], (int)strlen(c->items[idx]) < c->w - 1 ? (int)strlen(c->items[idx]) : c->w - 1, fg, bg);
                }
                if (foc) { curx = x + 1; cury = y + c->sel - c->top; }
                break;
            }
            case CT_CHECK:
            case CT_RADIO: {
                const char *mark = c->type == CT_CHECK ? (c->checked ? "[X]" : "[ ]") : (c->checked ? "(\x07)" : "( )");
                puts_at(x, y, mark, 0, 7);
                /* focused: label shown in reverse video */
                int lfg = foc ? 15 : 0, lbg = foc ? 0 : 7;
                if (foc) fill(x + 3, y, hot_len(c->label) + 2, 1, ' ', lfg, lbg);
                hot_at(x + 4, y, c->label, lfg, lbg, foc ? 14 : 15);
                if (foc) { curx = x + 1; cury = y; }
                break;
            }
            case CT_BUTTON: {
                int isdef = (d->c[d->focus].type == CT_BUTTON) ? foc : (c->id == d->defbutton);
                /* focused button in reverse video; the default one (Enter) has bright brackets */
                int bfg = foc ? 15 : 0, bbg = foc ? 0 : 7;
                int bl = hot_len(c->label);
                fill(x, y, bl + 4, 1, ' ', bfg, bbg);
                put(x, y, '<', (isdef || foc) ? 15 : 0, bbg);
                hot_at(x + 2, y, c->label, bfg, bbg, foc ? 14 : 15);
                put(x + 3 + bl, y, '>', (isdef || foc) ? 15 : 0, bbg);
                if (foc) { curx = x + 2; cury = y; }
                break;
            }
        }
    }
    cur_vis = curx >= 0;
    cur_x = curx; cur_y = cury;
}

static void field_set(Ctl *c, const char *s) { snprintf(c->text, sizeof c->text, "%s", s); c->pos = (int)strlen(c->text); c->scroll = 0; }

static int ctl_hit(Dlg *d, int mx, int my) {
    for (int i = 0; i < d->n; i++) {
        Ctl *c = &d->c[i];
        int x = d->x + c->x, y = d->y + c->y;
        int w = c->w;
        if (c->type == CT_BUTTON) w = hot_len(c->label) + 4;
        if (c->type == CT_CHECK || c->type == CT_RADIO) w = hot_len(c->label) + 4;
        int h = c->type == CT_LIST ? c->h : 1;
        if (mx >= x && mx < x + w && my >= y && my < y + h) return i;
    }
    return -1;
}

static void radio_select(Dlg *d, Ctl *c) {
    for (int i = 0; i < d->n; i++) if (d->c[i].type == CT_RADIO && d->c[i].group == c->group) d->c[i].checked = 0;
    c->checked = 1;
}

static void draw_background(void);

/* returns id of activated button, or d->cancel on Esc */
static int run_dialog(Dlg *d) {
    if (d->focus < 0 || d->focus >= d->n || !focusable(&d->c[d->focus])) {
        for (int i = 0; i < d->n; i++) if (focusable(&d->c[i])) { d->focus = i; break; }
    }
    double lastclick = 0;
    int lastclickctl = -1;
    int lastfocus = -1;
    for (;;) {
        if (d->focus != lastfocus) {
            /* entering a text field selects its contents, so typing replaces them */
            Ctl *fc = &d->c[d->focus];
            if (fc->type == CT_FIELD && fc->text[0]) { fc->selall = 1; fc->pos = (int)strlen(fc->text); }
            lastfocus = d->focus;
        }
        draw_background();
        draw_dialog(d);
        present();
        int k = get_key();
        if (k == K_NONE) { if (sys_shutting_down()) return d->cancel; continue; }
        Ctl *c = &d->c[d->focus];
        if (k == K_MOUSE) {
            if (mouse_type != 1) continue;
            int hit = ctl_hit(d, mouse_x, mouse_y);
            if (hit < 0) continue;
            Ctl *h = &d->c[hit];
            double now = sys_time();
            int dbl = (hit == lastclickctl && now - lastclick < 0.4);
            lastclick = now; lastclickctl = hit;
            if (!focusable(h)) continue;
            d->focus = hit;
            if (h->type == CT_BUTTON) return h->id;
            if (h->type == CT_CHECK) h->checked = !h->checked;
            if (h->type == CT_RADIO) radio_select(d, h);
            if ((h->type == CT_CHECK || h->type == CT_RADIO) && d->on_change) d->on_change(d, h);
            if (h->type == CT_LIST) {
                int idx = h->top + (mouse_y - (d->y + h->y));
                if (idx >= 0 && idx < h->nitems) {
                    h->sel = idx;
                    if (d->on_list) { int r = d->on_list(d, h, dbl); if (r) return r; }
                    else if (dbl) return d->defbutton;
                }
            }
            if (h->type == CT_FIELD) {
                int p = h->scroll + (mouse_x - (d->x + h->x));
                int l = (int)strlen(h->text);
                h->pos = p < 0 ? 0 : p > l ? l : p;
                h->selall = 0;          /* a click places the cursor */
                lastfocus = hit;
            }
            continue;
        }
        if (k == 27) return d->cancel;
        if (k == 9 || k == K_STAB) {
            int dir = k == 9 ? 1 : -1;
            int i = d->focus;
            for (int n = 0; n < d->n; n++) {
                i = (i + dir + d->n) % d->n;
                if (focusable(&d->c[i])) {
                    /* only one radio per group gets focus via tab: the checked one */
                    if (d->c[i].type == CT_RADIO && !d->c[i].checked) continue;
                    break;
                }
            }
            d->focus = i;
            continue;
        }
        if (k & K_ALTM && (k & 0xFFFF) < 256) {
            int ch = toupper(k & 0xFF);
            for (int i = 0; i < d->n; i++) {
                Ctl *x = &d->c[i];
                if (x->label && hot_key(x->label) == ch) {
                    /* label hotkey focuses next control */
                    if (x->type == CT_LABEL) { for (int j = i + 1; j < d->n; j++) if (focusable(&d->c[j])) { d->focus = j; break; } }
                    else if (x->type == CT_BUTTON) return x->id;
                    else { d->focus = i; if (x->type == CT_CHECK) x->checked = !x->checked; if (x->type == CT_RADIO) radio_select(d, x); }
                    break;
                }
            }
            continue;
        }
        if (k == 13) {
            if (c->type == CT_BUTTON) return c->id;
            if (c->type == CT_LIST && d->on_list) { int r = d->on_list(d, c, 1); if (r) return r; continue; }
            return d->defbutton;
        }
        switch (c->type) {
            case CT_FIELD: {
                int l = (int)strlen(c->text);
                int base = k & ~K_SHIFT;
                if (k == 3 || k == 24) {          /* Ctrl+C / Ctrl+X: the whole field */
                    clip_set(c->text);
                    if (k == 24) { c->text[0] = 0; c->pos = 0; c->selall = 0; if (d->on_change) d->on_change(d, c); }
                    break;
                }
                if (k == 22) {                    /* Ctrl+V: insert (or replace the selection) */
                    char *t = clip_get();
                    if (c->selall) { c->text[0] = 0; c->pos = 0; l = 0; c->selall = 0; }
                    for (; t && *t && *t != '\n' && *t != '\r' && l < (int)sizeof c->text - 1; t++, l++) {
                        memmove(c->text + c->pos + 1, c->text + c->pos, (size_t)(l - c->pos + 1));
                        c->text[c->pos++] = *t;
                    }
                    if (d->on_change) d->on_change(d, c);
                    break;
                }
                if (c->selall) {
                    c->selall = 0;
                    /* typing, Backspace or Del replace the selected text; movement keys keep it */
                    if ((k >= 32 && k < 256) || k == 8 || base == K_DEL) {
                        c->text[0] = 0; c->pos = 0; l = 0;
                        if (k == 8 || base == K_DEL) { if (d->on_change) d->on_change(d, c); break; }
                    }
                }
                if (base == K_LEFT) { if (c->pos > 0) c->pos--; }
                else if (base == K_RIGHT) { if (c->pos < l) c->pos++; }
                else if (base == K_HOME) c->pos = 0;
                else if (base == K_END) c->pos = l;
                else if (base == K_DEL) { if (c->pos < l) memmove(c->text + c->pos, c->text + c->pos + 1, (size_t)(l - c->pos)); }
                else if (k == 8) { if (c->pos > 0) { memmove(c->text + c->pos - 1, c->text + c->pos, (size_t)(l - c->pos + 1)); c->pos--; } }
                else if (k >= 32 && k < 256 && l < (int)sizeof c->text - 1) {
                    memmove(c->text + c->pos + 1, c->text + c->pos, (size_t)(l - c->pos + 1));
                    c->text[c->pos++] = (char)k;
                }
                if (d->on_change) d->on_change(d, c);
                break;
            }
            case CT_LIST: {
                int base = k & ~K_SHIFT;
                if (base == K_UP || base == K_LEFT) { if (c->sel > 0) c->sel--; }
                else if (base == K_DOWN || base == K_RIGHT) { if (c->sel < c->nitems - 1) c->sel++; }
                else if (base == K_PGUP) { c->sel -= c->h; if (c->sel < 0) c->sel = 0; }
                else if (base == K_PGDN) { c->sel += c->h; if (c->sel >= c->nitems) c->sel = c->nitems - 1; }
                else if (base == K_HOME) c->sel = 0;
                else if (base == K_END) c->sel = c->nitems - 1;
                else if (k >= 32 && k < 256) {
                    /* jump to item starting with letter */
                    for (int n = 1; n <= c->nitems; n++) {
                        int i = (c->sel + n) % c->nitems;
                        const char *s = c->items[i];
                        while (*s == ' ') s++;
                        if (toupper((unsigned char)*s) == toupper(k)) { c->sel = i; break; }
                    }
                }
                if (c->sel < 0) c->sel = 0;
                if (d->on_list) d->on_list(d, c, 0);
                break;
            }
            case CT_CHECK: if (k == ' ') c->checked = !c->checked; break;
            case CT_RADIO: {
                int base = k & ~K_SHIFT;
                if (base == K_UP || base == K_DOWN || base == K_LEFT || base == K_RIGHT) {
                    int dir = (base == K_UP || base == K_LEFT) ? -1 : 1;
                    int i = d->focus;
                    for (int n = 0; n < d->n; n++) {
                        i = (i + dir + d->n) % d->n;
                        if (d->c[i].type == CT_RADIO && d->c[i].group == c->group) { radio_select(d, &d->c[i]); d->focus = i; if (d->on_change) d->on_change(d, &d->c[i]); break; }
                    }
                }
                break;
            }
            case CT_BUTTON: {
                int base = k & ~K_SHIFT;
                if (base == K_LEFT || base == K_RIGHT || base == K_UP || base == K_DOWN) {
                    int dir = (base == K_LEFT || base == K_UP) ? -1 : 1;
                    int i = d->focus;
                    for (int n = 0; n < d->n; n++) { i = (i + dir + d->n) % d->n; if (d->c[i].type == CT_BUTTON) break; }
                    d->focus = i;
                }
                if (k == ' ') return c->id;
                break;
            }
        }
    }
}

static void dlg_center(Dlg *d, int w, int h) { d->w = w; d->h = h; d->x = (SW - w) / 2; d->y = (SH - h) / 2; if (d->y < 1) d->y = 1; }

/* message box with buttons; returns index of button */
static int message_box(const char *title, const char *msg, const char **buttons, int nb, int def) {
    Dlg d;
    memset(&d, 0, sizeof d);
    d.title = title;
    /* split message into lines */
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", msg);
    char *lines[12];
    int nl = 0, maxw = 0;
    char *s = tmp;
    while (s && nl < 12) {
        char *e = strchr(s, '\n');
        if (e) *e = 0;
        lines[nl++] = s;
        if ((int)strlen(s) > maxw) maxw = (int)strlen(s);
        s = e ? e + 1 : NULL;
    }
    int bw = 0;
    for (int i = 0; i < nb; i++) bw += hot_len(buttons[i]) + 4 + 3;
    int w = maxw + 6;
    if (bw + 4 > w) w = bw + 4;
    if (w > 76) w = 76;
    dlg_center(&d, w, nl + 4);
    for (int i = 0; i < nl; i++) dlg_add(&d, CT_LABEL, (w - (int)strlen(lines[i])) / 2, 1 + i, 0, 0, lines[i], 0);
    dlg_add(&d, CT_SEP, 0, nl + 1, 0, 0, NULL, 0);
    int x = (w - bw + 3) / 2;
    int first = d.n;
    for (int i = 0; i < nb; i++) {
        dlg_add(&d, CT_BUTTON, x, nl + 2, 0, 0, buttons[i], 100 + i);
        x += hot_len(buttons[i]) + 4 + 3;
    }
    d.focus = first + def;
    d.defbutton = 100 + def;
    d.cancel = 100 + nb - 1;
    int r = run_dialog(&d);
    return r - 100;
}

static void help_show(const char *topic);

static void show_message(const char *msg, int help) {
    const char *b2[] = { "OK", "Help" };
    const char *b1[] = { "OK" };
    int r = help ? message_box("", msg, b2, 2, 0) : message_box("", msg, b1, 1, 0);
    if (help && r == 1) help_show("Errors");
}

static void clear_redo(void) {
    for (int i = 0; i < nredo; i++) { for (int k = 0; k < redo_stack[i].n; k++) free(redo_stack[i].lines[k]); free(redo_stack[i].lines); }
    nredo = 0;
}

/* ------------------------------------------------------------------ */
/* syntax check / compile                                              */
/* ------------------------------------------------------------------ */
int compile_check(SrcPart *parts, int nparts, CompileError *err);

static void draw_background(void) { draw_all(); }

static int syntax_check_line(int part, int line, char *msg, int msgsz, int *col) {
    SrcPart *sp = doc_srcparts(&doc);
    CompileError err;
    int ok = compile_check(sp, doc.nparts, &err);
    free(sp);
    if (ok) return 1;
    if (err.part != part || err.line != line) return 1;
    if (strstr(err.msg, "without") || strstr(err.msg, "not defined") || strstr(err.msg, "Duplicate label")) return 1;
    /* "name args" may be a call to a SUB that has not been written yet */
    if (strcmp(err.msg, "Expected: =") == 0) return 1;
    snprintf(msg, (size_t)msgsz, "%s", err.msg);
    *col = err.col;
    return 0;
}

static void goto_location(int part, int line, int col) {
    if (part < 0 || part >= doc.nparts) return;
    switch_part(part);
    Part *p = P();
    p->cy = line;
    p->cx = col;
    focus_imm = 0;
    clamp_cursor();
    /* center the line */
    if (p->cy - p->top < 3 || p->cy - p->top > ED_ROWS - 3) { p->top = p->cy - ED_ROWS / 2; if (p->top < 0) p->top = 0; }
}

static int compile_doc(void) {
    commit_line();
    rt_free();
    if (prog) { free_program(prog); prog = NULL; }
    prog_valid = 0;
    SrcPart *sp = doc_srcparts(&doc);
    CompileError err;
    prog = compile_program(sp, doc.nparts, &err);
    free(sp);
    if (!prog) {
        goto_location(err.part, err.line, err.col);
        show_message(err.msg, 1);
        return 0;
    }
    prog_valid = 1;
    rt_reset(prog);
    return 1;
}

/* ------------------------------------------------------------------ */
/* procedures                                                          */
/* ------------------------------------------------------------------ */
static int find_part(const char *name) {
    for (int i = 1; i < doc.nparts; i++) if (str_ieq(doc.parts[i].name, name)) return i;
    return -1;
}

static void sort_parts(void) {
    /* QBasic keeps procedures sorted alphabetically */
    for (int i = 1; i < doc.nparts; i++)
        for (int j = i + 1; j < doc.nparts; j++) {
            char a[64], b[64];
            snprintf(a, sizeof a, "%s", doc.parts[i].name); str_upper(a);
            snprintf(b, sizeof b, "%s", doc.parts[j].name); str_upper(b);
            if (strcmp(a, b) > 0) {
                Part t = doc.parts[i]; doc.parts[i] = doc.parts[j]; doc.parts[j] = t;
                if (curpart == i) curpart = j; else if (curpart == j) curpart = i;
                for (int k = 0; k < nbps; k++) { if (bps[k].part == i) bps[k].part = j; else if (bps[k].part == j) bps[k].part = i; }
            }
        }
}

static int create_proc(const char *header, const char *name, int isfunc) {
    if (doc.nparts >= MAX_PARTS) { show_message("Too many procedures", 0); return -1; }
    int ex = find_part(name);
    if (ex >= 0) { switch_part(ex); return ex; }
    Part *p = &doc.parts[doc.nparts++];
    part_init(p);
    snprintf(p->name, sizeof p->name, "%s", name);
    { int l = (int)strlen(p->name); if (l && strchr("%&!#$", p->name[l - 1])) p->name[l - 1] = 0; }
    p->isfunc = isfunc;
    /* inherit DEFtype statements from the module like QBasic does */
    Part *m = &doc.parts[0];
    for (int i = 0; i < m->n; i++) {
        TokLine tl;
        lex_line(m->lines[i], &tl);
        int isdef = tl.t[0].k == TK_KW && (tl.t[0].kw == KW_DEFINT || tl.t[0].kw == KW_DEFLNG || tl.t[0].kw == KW_DEFSNG || tl.t[0].kw == KW_DEFDBL || tl.t[0].kw == KW_DEFSTR);
        lex_free(&tl);
        if (isdef) part_insert(p, p->n, m->lines[i]);
    }
    char h[600];
    format_line(header, h, sizeof h, 1);
    part_insert(p, p->n, h);
    int hdr = p->n - 1;
    part_insert(p, p->n, "");
    part_insert(p, p->n, isfunc ? "END FUNCTION" : "END SUB");
    p->cy = hdr + 1;
    p->cx = 4;
    int idx = doc.nparts - 1;
    char nm[64];
    snprintf(nm, sizeof nm, "%s", p->name);
    curpart = idx;
    sort_parts();
    idx = find_part(nm);
    curpart = idx;
    mark_modified();
    clamp_cursor();
    return idx;
}

static void handle_new_proc_line(int line) {
    Part *m = &doc.parts[0];
    char name[64];
    int c = classify_line(m->lines[line], name, sizeof name);
    char header[600];
    snprintf(header, sizeof header, "%s", m->lines[line]);
    const char *h = header;
    while (*h == ' ') h++;
    part_delete(m, line);
    bp_shift(0, line, -1);
    if (m->n == 0) part_insert(m, 0, "");
    if (m->cy >= m->n) m->cy = m->n - 1;
    create_proc(h, name, c == 2);
}

static int input_dialog(const char *title, const char *prompt, char *out, int outsz) {
    Dlg d;
    memset(&d, 0, sizeof d);
    d.title = title;
    dlg_center(&d, 56, 8);
    dlg_add(&d, CT_LABEL, 2, 2, 0, 0, prompt, 0);
    Ctl *f = dlg_add(&d, CT_FIELD, 3 + hot_len(prompt), 2, 50 - hot_len(prompt), 1, NULL, 1);
    field_set(f, out);
    dlg_add(&d, CT_SEP, 0, 5, 0, 0, NULL, 0);
    dlg_add(&d, CT_BUTTON, 12, 6, 0, 0, "OK", 10);
    dlg_add(&d, CT_BUTTON, 30, 6, 0, 0, "Cancel", 11);
    d.defbutton = 10; d.cancel = 11;
    d.focus = 1;
    int r = run_dialog(&d);
    if (r != 10) return 0;
    snprintf(out, (size_t)outsz, "%s", f->text);
    return 1;
}

static void new_proc_dialog(int isfunc) {
    char name[64] = "";
    if (!input_dialog(isfunc ? "New FUNCTION" : "New SUB", "&Name:", name, sizeof name)) return;
    char *s = name;
    while (*s == ' ') s++;
    if (!*s) return;
    if (!isalpha((unsigned char)*s)) { show_message("Expected: identifier", 0); return; }
    char hdr[128];
    snprintf(hdr, sizeof hdr, "%s %s", isfunc ? "FUNCTION" : "SUB", s);
    commit_line();
    create_proc(hdr, s, isfunc);
}

static int subs_list_cb(Dlg *d, Ctl *c, int dbl) { (void)d; (void)c; return dbl ? 10 : 0; }

static void subs_dialog(void) {
    commit_line();
    Dlg d;
    memset(&d, 0, sizeof d);
    d.title = "SUBs";
    dlg_center(&d, 60, 18);
    dlg_add(&d, CT_LABEL, 2, 1, 0, 0, "Choose program item to edit", 0);
    Ctl *l = dlg_add(&d, CT_LIST, 3, 3, 54, 10, NULL, 1);
    char **items = (char **)qb_alloc(sizeof(char *) * (size_t)doc.nparts);
    char title[200];
    const char *base = filename[0] ? filename : "Untitled";
    const char *sl = strrchr(base, '/'); if (!sl) sl = strrchr(base, '\\');
    snprintf(title, sizeof title, "%s", sl ? sl + 1 : base);
    if (filename[0]) str_upper(title);
    items[0] = qb_strdup(title);
    for (int i = 1; i < doc.nparts; i++) {
        char t[100];
        snprintf(t, sizeof t, "  %s", doc.parts[i].name);
        items[i] = qb_strdup(t);
    }
    l->items = items;
    l->nitems = doc.nparts;
    l->sel = curpart;
    d.on_list = subs_list_cb;
    dlg_add(&d, CT_SEP, 0, 15, 0, 0, NULL, 0);
    dlg_add(&d, CT_BUTTON, 4, 16, 0, 0, "&Edit in Active", 10);
    dlg_add(&d, CT_BUTTON, 25, 16, 0, 0, "&Delete", 12);
    dlg_add(&d, CT_BUTTON, 39, 16, 0, 0, "Cancel", 11);
    d.defbutton = 10; d.cancel = 11;
    d.focus = 1;
    int r = run_dialog(&d);
    int sel = l->sel;
    for (int i = 0; i < doc.nparts; i++) free(items[i]);
    free(items);
    if (r == 10) switch_part(sel);
    else if (r == 12 && sel > 0) {
        const char *b[] = { "&Yes", "&No" };
        char msg[128];
        snprintf(msg, sizeof msg, "Delete %s?", doc.parts[sel].name);
        if (message_box("", msg, b, 2, 1) == 0) {
            snapshot();
            part_free(&doc.parts[sel]);
            for (int i = 0; i < nbps; i++) if (bps[i].part == sel) { bps[i] = bps[--nbps]; i--; }
            for (int i = 0; i < nbps; i++) if (bps[i].part > sel) bps[i].part--;
            memmove(&doc.parts[sel], &doc.parts[sel + 1], sizeof(Part) * (size_t)(doc.nparts - sel - 1));
            doc.nparts--;
            nundo = 0;  /* undo snapshots reference part indices */
            curpart = 0;
            mark_modified();
        }
    }
}

/* ------------------------------------------------------------------ */
/* running and debugging                                              */
/* ------------------------------------------------------------------ */
static int ide_loop(int nested);
static int restart_step = 0;

static void show_output_until_key(void) {
    scr_set_ide_owner(0);
    scr_show_output();
    kb_clear();
    for (;;) {
        kb_poll();
        int c = kb_get_code();
        if (c >= 0 && c != 0xF000) break;
        int t, x, y;
        if (mouse_get(&t, &x, &y) && t == 1) break;
        if (sys_shutting_down()) break;
        scr_present(0);
        sys_sleep(10);
    }
    scr_set_ide_owner(1);
}

static void start_program(int step) {
    commit_line();
    if (!prog_valid || !prog) { if (!compile_doc()) return; }
    for (;;) {
        rt_reset(prog);
        scr_reset();
        snd_reset();
        file_closeall();
        kb_clear();
        running = 1;
        code_changed = 0;
        scr_set_ide_owner(0);
        scr_show_output();
        rs->stepmode = step ? 1 : 0;
        rs->stepframe = NULL;
        int st = rt_run(prog, 0);
        running = 0;
        suspended = 0;
        snd_end();
        opl_ports_reset();
        if (st == RUN_RESTART) {
            step = restart_step;
            restart_step = 0;
            if (!prog_valid) {
                scr_set_ide_owner(1);
                if (!compile_doc()) { cur_stmt_part = -1; return; }
            }
            continue;
        }
        cur_stmt_part = -1;
        if (st == RUN_ERROR) {
            int part = rs->errpart, line = rs->errline;
            char msg[160];
            snprintf(msg, sizeof msg, "%s", rs->errmsg);
            scr_set_ide_owner(1);
            if (part != 0xFFFF) goto_location(part, line, 0);
            cur_stmt_part = part; cur_stmt_line = line;
            show_message(msg, 1);
            cur_stmt_part = -1;
            file_closeall();
            return;
        }
        if (st == RUN_END && !sys_shutting_down()) {
            if (rs->err != -1) scr_press_any_key();
        }
        file_closeall();
        break;
    }
    scr_set_ide_owner(1);
}

/* called by the interpreter when it stops (step, breakpoint, Ctrl+Break, STOP) */
int ide_debug_break(int stmt) {
    Program *pr = rs->prog;
    Stmt *s = &pr->code[stmt];
    if (s->part == 0xFFFF) return DBG_CONTINUE;
    cur_stmt_part = s->part;
    cur_stmt_line = s->line;
    if (trace_on && rs->stepmode == 1 && !rs->breakreq) {
        /* trace: show the statement briefly */
        scr_set_ide_owner(1);
        goto_location(s->part, s->line, 0);
        suspended = 1;
        draw_all(); present();
        double until = sys_time() + 0.08;
        while (sys_time() < until) {
            kb_poll();
            if (kb_count() > 0) { trace_on = 0; break; }
            sys_sleep(5);
        }
        suspended = 0;
        if (trace_on) { scr_set_ide_owner(0); scr_show_output(); return DBG_STEP; }
    }
    extern void kb_stash(void);
    extern void kb_unstash(int restore);
    kb_stash();                       /* the program's pending keys stay out of the editor */
    suspended = 1;
    scr_set_ide_owner(1);
    goto_location(s->part, s->line, 0);
    int r = ide_loop(1);
    suspended = 0;
    int resume = r == DBG_CONTINUE || r == DBG_STEP || r == DBG_PSTEP;
    kb_unstash(resume);               /* ...and are given back if it continues */
    if (resume) { scr_set_ide_owner(0); scr_show_output(); }
    if (r == DBG_CONTINUE && trace_on) r = DBG_STEP;
    return r;
}

static int set_next_statement(void) {
    if (!suspended || !rs) return 0;
    commit_line();
    if (code_changed) { show_message("Cannot continue: the program was edited", 0); return 0; }
    Part *p = P();
    for (int i = 0; i < prog->ncode; i++) {
        Stmt *s = &prog->code[i];
        if (s->part == curpart && s->line >= p->cy && s->linestart) {
            int curpartnow = prog->code[rs->pc].part;
            if (s->part != curpartnow) { show_message("Cannot set next statement in a different procedure", 0); return 0; }
            rs->pc = i;
            cur_stmt_part = s->part;
            cur_stmt_line = s->line;
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* immediate window                                                    */
/* ------------------------------------------------------------------ */
static int imm_exec(const char *line) {
    const char *s = line;
    while (*s == ' ') s++;
    if (!*s) return 0;
    commit_line();
    if (!suspended) {
        if (!prog || !prog_valid) {
            SrcPart *sp = doc_srcparts(&doc);
            CompileError err;
            rt_free();
            if (prog) free_program(prog);
            prog = compile_program(sp, doc.nparts, &err);
            free(sp);
            if (!prog) {
                /* fall back to an empty program so immediate statements still work */
                char *empty[1] = { (char *)"" };
                SrcPart e = { empty, 1 };
                prog = compile_program(&e, 1, &err);
                prog_valid = 0;
            } else prog_valid = 1;
            rt_reset(prog);
        }
        if (!rs || !rs->global) rt_reset(prog);
    }
    int procidx = -1;
    if (suspended && rs->cur && rs->cur->proc >= 0) procidx = rs->cur->proc;
    CompileError err;
    int start;
    if (!compile_immediate(prog, s, procidx, &start, &err)) { show_message(err.msg, 1); return 0; }
    int was_running = running;
    scr_set_ide_owner(0);
    scr_show_output();
    running = 1;
    int savestep = rs->stepmode;
    int savepc = rs->pc, savecur = rs->curstmt;
    int saveretpc = rs->cur ? rs->cur->retpc : 0;
    rs->stepmode = 0;
    extern int g_debug_enabled;
    g_debug_enabled = 0;
    int st = rt_exec_immediate(prog, start);
    g_debug_enabled = 1;
    rs->stepmode = savestep;
    rs->pc = savepc; rs->curstmt = savecur;
    if (rs->cur) rs->cur->retpc = saveretpc;
    running = was_running;
    truncate_code(prog, start);
    if (st == RUN_ERROR) { scr_set_ide_owner(1); show_message(rs->errmsg, 1); return 0; }
    if (!sys_shutting_down()) scr_press_any_key();
    scr_set_ide_owner(1);
    return 1;
}

static void imm_commit_line(void) {
    while (imm.n <= imm_cy) part_insert(&imm, imm.n, "");
    part_set(&imm, imm_cy, immbuf);
}
static void imm_load_line(int y) {
    imm_cy = y;
    snprintf(immbuf, sizeof immbuf, "%s", y < imm.n ? imm.lines[y] : "");
    if (imm_cx > (int)strlen(immbuf)) imm_cx = (int)strlen(immbuf);
    if (imm_cy < imm_top) imm_top = imm_cy;
    if (imm_cy >= imm_top + IMM_ROWS) imm_top = imm_cy - IMM_ROWS + 1;
}

static void imm_key(int k) {
    int l = (int)strlen(immbuf);
    int base = k & ~K_SHIFT;
    if (base == K_LEFT) { if (imm_cx > 0) imm_cx--; return; }
    if (base == K_RIGHT) { if (imm_cx < 250) imm_cx++; return; }
    if (base == K_HOME) { imm_cx = 0; return; }
    if (base == K_END) { imm_cx = l; return; }
    if (base == K_UP) { imm_commit_line(); if (imm_cy > 0) imm_load_line(imm_cy - 1); return; }
    if (base == K_DOWN) { imm_commit_line(); if (imm_cy < imm.n) imm_load_line(imm_cy + 1); return; }
    if (k == 13) {
        imm_commit_line();
        char line[1024];
        snprintf(line, sizeof line, "%s", immbuf);
        imm_exec(line);
        if (imm_cy == imm.n - 1) part_insert(&imm, imm.n, "");
        if (imm.n > 10) { part_delete(&imm, 0); if (imm_cy > 0) imm_cy--; }
        imm_cx = 0;
        imm_load_line(imm_cy + 1 < imm.n ? imm.n - 1 : imm_cy + 1);
        return;
    }
    if (k == 8) { if (imm_cx > 0 && imm_cx <= l) { memmove(immbuf + imm_cx - 1, immbuf + imm_cx, (size_t)(l - imm_cx + 1)); imm_cx--; } else if (imm_cx > l) imm_cx--; return; }
    if (base == K_DEL) { if (imm_cx < l) memmove(immbuf + imm_cx, immbuf + imm_cx + 1, (size_t)(l - imm_cx)); return; }
    if (k == 27) { immbuf[0] = 0; imm_cx = 0; return; }
    if (k >= 32 && k < 256 && l < 250) {
        while (l < imm_cx) immbuf[l++] = ' ';
        immbuf[l] = 0;
        memmove(immbuf + imm_cx + 1, immbuf + imm_cx, (size_t)(l - imm_cx + 1));
        immbuf[imm_cx++] = (char)k;
    }
}

/* ------------------------------------------------------------------ */
/* files                                                               */
/* ------------------------------------------------------------------ */
static void doc_reset_state(void) {
    linebuf_line = -1;
    for (int i = 0; i < nundo; i++) { for (int k = 0; k < undo_stack[i].n; k++) free(undo_stack[i].lines[k]); free(undo_stack[i].lines); }
    nundo = 0;
    clear_redo();
    nbps = 0;
    curpart = 0;
    sel_on = 0;
    modified = 0;
    rt_free();
    if (prog) { free_program(prog); prog = NULL; }
    prog_valid = 0;
}

static void recase_document(void) {
    /* QBasic reformats code on load: keyword case and consistent identifier case */
    ncdict = 0;
    for (int pi = 0; pi < doc.nparts; pi++) {
        Part *p = &doc.parts[pi];
        for (int i = 0; i < p->n; i++) {
            char o[1100];
            if (format_line(p->lines[i], o, sizeof o, 0)) part_set(p, i, o);
        }
    }
}

static int save_file(const char *path) {
    commit_line();
    if (!doc_save(&doc, path)) { show_message("Path/File access error", 0); return 0; }
    modified = 0;
    return 1;
}

static int load_file(const char *path) {
    Doc nd;
    doc_init(&nd);
    int r = doc_load(&nd, path);
    if (r == 0) { doc_free(&nd); show_message("File not found", 0); return 0; }
    if (r < 0) { doc_free(&nd); show_message("File is in QuickBASIC binary format.\nSave it as text in QuickBASIC first.", 0); return 0; }
    doc_reset_state();
    doc_free(&doc);
    doc = nd;
    if (doc.parts[0].n == 0) part_insert(&doc.parts[0], 0, "");
    snprintf(filename, sizeof filename, "%s", path);
    recase_document();
    sort_parts();
    curpart = 0;
    for (int i = 0; i < doc.nparts; i++) { doc.parts[i].cy = doc.parts[i].cx = doc.parts[i].top = doc.parts[i].left = 0; }
    modified = 0;
    return 1;
}

static int save_as_dialog(void);

static int do_save(void) {
    if (!filename[0]) return save_as_dialog();
    return save_file(filename);
}

/* returns 0 if user cancelled */
static int confirm_discard(void) {
    commit_line();
    if (!modified) return 1;
    const char *b[] = { "&Yes", "&No", "Cancel" };
    int r = message_box("", "Loaded file is not saved. Save it now?", b, 3, 0);
    if (r == 0) return do_save();
    if (r == 1) return 1;
    return 0;
}

static void list_dir(const char *mask, char ***files, int *nf, char ***dirs, int *ndirs) {
    *files = NULL; *nf = 0; *dirs = NULL; *ndirs = 0;
    char m[64];
    snprintf(m, sizeof m, "%s", mask[0] ? mask : "*.BAS");
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("*", &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const char *nm = fd.cFileName;
            int isdir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (strcmp(nm, ".") == 0) continue;
            if (isdir) { *dirs = (char **)qb_realloc(*dirs, sizeof(char *) * (size_t)(*ndirs + 1)); (*dirs)[(*ndirs)++] = qb_strdup(nm); }
            else {
                extern int wild_match_pub(const char *pat, const char *s);
                if (wild_match_pub(m, nm)) { *files = (char **)qb_realloc(*files, sizeof(char *) * (size_t)(*nf + 1)); (*files)[(*nf)++] = qb_strdup(nm); }
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    /* drives */
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; i++) if (drives & (1u << i)) {
        char dn[8]; snprintf(dn, sizeof dn, "[-%c-]", 'A' + i);
        *dirs = (char **)qb_realloc(*dirs, sizeof(char *) * (size_t)(*ndirs + 1)); (*dirs)[(*ndirs)++] = qb_strdup(dn);
    }
#else
    DIR *d = opendir(".");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            const char *nm = de->d_name;
            if (strcmp(nm, ".") == 0) continue;
            if (nm[0] == '.' && strcmp(nm, "..") != 0) continue;
            struct stat st;
            int isdir = stat(nm, &st) == 0 && S_ISDIR(st.st_mode);
            if (isdir) { *dirs = (char **)qb_realloc(*dirs, sizeof(char *) * (size_t)(*ndirs + 1)); (*dirs)[(*ndirs)++] = qb_strdup(nm); }
            else {
                extern int wild_match_pub(const char *pat, const char *s);
                if (wild_match_pub(m, nm)) { *files = (char **)qb_realloc(*files, sizeof(char *) * (size_t)(*nf + 1)); (*files)[(*nf)++] = qb_strdup(nm); }
            }
        }
        closedir(d);
    }
#endif
    /* sort */
    for (int i = 0; i < *nf; i++) for (int j = i + 1; j < *nf; j++) if (qb_stricmp((*files)[i], (*files)[j]) > 0) { char *t = (*files)[i]; (*files)[i] = (*files)[j]; (*files)[j] = t; }
    for (int i = 0; i < *ndirs; i++) for (int j = i + 1; j < *ndirs; j++) {
        int ai = (*dirs)[i][0] == '[', aj = (*dirs)[j][0] == '[';
        if (ai > aj || (ai == aj && qb_stricmp((*dirs)[i], (*dirs)[j]) > 0)) { char *t = (*dirs)[i]; (*dirs)[i] = (*dirs)[j]; (*dirs)[j] = t; }
    }
}

static void free_list(char **l, int n) { for (int i = 0; i < n; i++) free(l[i]); free(l); }

typedef struct FileDlgState { Ctl *field, *files, *dirs; char mask[64]; } FileDlgState;
static FileDlgState fds;

static int filedlg_list_cb(Dlg *d, Ctl *c, int dbl) {
    (void)d;
    if (c == fds.files && c->nitems > 0) {
        field_set(fds.field, c->items[c->sel]);
        if (dbl) return 10;
    } else if (c == fds.dirs && dbl && c->nitems > 0) {
        return 20;  /* change directory */
    }
    return 0;
}

static void fill_file_lists(int with_files) {
    char **f, **dd; int nf, nd;
    list_dir(fds.mask, &f, &nf, &dd, &nd);
    if (fds.files) { free_list(fds.files->items, fds.files->nitems); fds.files->items = f; fds.files->nitems = nf; fds.files->sel = 0; fds.files->top = 0; }
    else free_list(f, nf);
    (void)with_files;
    free_list(fds.dirs->items, fds.dirs->nitems);
    fds.dirs->items = dd; fds.dirs->nitems = nd; fds.dirs->sel = 0; fds.dirs->top = 0;
}

static int has_wild(const char *s) { return strchr(s, '*') || strchr(s, '?'); }

static int change_dir_to(const char *s) {
#ifdef _WIN32
    if (s[0] == '[' && s[1] == '-') { char dr[4] = { s[2], ':', '\\', 0 }; return chdir(dr) == 0; }
#endif
    return chdir(s) == 0;
}

static int file_dialog(int saving, char *out, int outsz) {
    Dlg d;
    memset(&d, 0, sizeof d);
    d.title = saving ? "Save As" : "Open";
    dlg_center(&d, 68, 20);
    memset(&fds, 0, sizeof fds);
    snprintf(fds.mask, sizeof fds.mask, "*.BAS");
    dlg_add(&d, CT_LABEL, 2, 2, 0, 0, "File &Name:", 0);
    fds.field = dlg_add(&d, CT_FIELD, 15, 2, 50, 1, NULL, 1);
    if (saving) {
        const char *b = filename[0] ? filename : "";
        const char *sl = strrchr(b, '/'); if (!sl) sl = strrchr(b, '\\');
        field_set(fds.field, sl ? sl + 1 : b);
    } else field_set(fds.field, fds.mask);
    Ctl *cwdl = dlg_add(&d, CT_LABEL, 2, 4, 0, 0, "", 0);
    static char cwdbuf[600];
    if (!saving) {
        fds.files = dlg_add(&d, CT_LIST, 3, 7, 38, 9, "&Files", 2);
        fds.dirs = dlg_add(&d, CT_LIST, 45, 7, 20, 9, "&Dirs/Drives", 3);
    } else {
        fds.files = NULL;
        fds.dirs = dlg_add(&d, CT_LIST, 45, 7, 20, 9, "&Dirs/Drives", 3);
    }
    dlg_add(&d, CT_SEP, 0, 17, 0, 0, NULL, 0);
    dlg_add(&d, CT_BUTTON, 14, 18, 0, 0, "OK", 10);
    dlg_add(&d, CT_BUTTON, 30, 18, 0, 0, "Cancel", 11);
    d.defbutton = 10; d.cancel = 11;
    d.on_list = filedlg_list_cb;
    d.focus = 1;
    fill_file_lists(!saving);
    int result = 0;
    for (;;) {
        if (!getcwd(cwdbuf, sizeof cwdbuf - 1)) strcpy(cwdbuf, ".");
        if (strlen(cwdbuf) > 60) memmove(cwdbuf, cwdbuf + strlen(cwdbuf) - 60, 61);
        cwdl->label = cwdbuf;
        int r = run_dialog(&d);
        if (r == 11) break;
        if (r == 20) {
            if (change_dir_to(fds.dirs->items[fds.dirs->sel])) fill_file_lists(!saving);
            continue;
        }
        if (r == 10) {
            if (d.c[d.focus].type == CT_LIST && &d.c[d.focus] == fds.dirs) {
                if (fds.dirs->nitems && change_dir_to(fds.dirs->items[fds.dirs->sel])) fill_file_lists(!saving);
                continue;
            }
            char t[512];
            snprintf(t, sizeof t, "%s", fds.field->text);
            char *s = t; while (*s == ' ') s++;
            int l = (int)strlen(s); while (l > 0 && s[l - 1] == ' ') s[--l] = 0;
            if (!*s) continue;
            if (has_wild(s)) {
                char *sl = strrchr(s, '/'); if (!sl) sl = strrchr(s, '\\');
                if (sl) { *sl = 0; if (!change_dir_to(*s ? s : "/")) { show_message("Path not found", 0); continue; } snprintf(fds.mask, sizeof fds.mask, "%s", sl + 1); }
                else snprintf(fds.mask, sizeof fds.mask, "%s", s);
                field_set(fds.field, fds.mask);
                fill_file_lists(!saving);
                continue;
            }
            /* directory typed? */
#ifdef _WIN32
            DWORD at = GetFileAttributesA(s);
            int isdir = at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY);
#else
            struct stat st;
            int isdir = stat(s, &st) == 0 && S_ISDIR(st.st_mode);
#endif
            if (isdir) { change_dir_to(s); field_set(fds.field, fds.mask); fill_file_lists(!saving); continue; }
            /* add .BAS if no extension */
            const char *base = strrchr(s, '/'); if (!base) base = strrchr(s, '\\'); base = base ? base + 1 : s;
            char full[700];
            if (!strchr(base, '.')) snprintf(full, sizeof full, "%s.BAS", s);
            else snprintf(full, sizeof full, "%s", s);
            /* on case-sensitive systems, prefer an existing file with matching name */
            if (!saving) {
                FILE *tf = fopen(full, "rb");
                if (!tf && !strchr(base, '.')) {
                    char alt[700]; snprintf(alt, sizeof alt, "%s.bas", s);
                    tf = fopen(alt, "rb");
                    if (tf) snprintf(full, sizeof full, "%s", alt);
                }
                if (!tf && fds.files) {
                    for (int i = 0; i < fds.files->nitems; i++) if (str_ieq(fds.files->items[i], full)) { snprintf(full, sizeof full, "%s", fds.files->items[i]); break; }
                }
                if (tf) fclose(tf);
            }
            snprintf(out, (size_t)outsz, "%s", full);
            result = 1;
            break;
        }
    }
    if (fds.files) free_list(fds.files->items, fds.files->nitems);
    free_list(fds.dirs->items, fds.dirs->nitems);
    return result;
}

static int save_as_dialog(void) {
    char path[600];
    if (!file_dialog(1, path, sizeof path)) return 0;
    FILE *t = fopen(path, "rb");
    if (t) {
        fclose(t);
        if (!filename[0] || strcmp(path, filename) != 0) {
            const char *b[] = { "&Yes", "&No" };
            if (message_box("", "File already exists. Overwrite?", b, 2, 1) != 0) return 0;
        }
    }
    if (!save_file(path)) return 0;
    snprintf(filename, sizeof filename, "%s", path);
    return 1;
}

static void cmd_new(void) {
    if (!confirm_discard()) return;
    doc_reset_state();
    doc_free(&doc);
    doc_init(&doc);
    part_insert(&doc.parts[0], 0, "");
    filename[0] = 0;
    ncdict = 0;
}

static void cmd_open(void) {
    if (!confirm_discard()) return;
    char path[600];
    if (!file_dialog(0, path, sizeof path)) return;
    load_file(path);
}

/* ------------------------------------------------------------------ */
/* search                                                              */
/* ------------------------------------------------------------------ */
static int match_at(const char *s, int i, const char *f, int fl) {
    int l = (int)strlen(s);
    if (i + fl > l) return 0;
    for (int k = 0; k < fl; k++) {
        int a = (unsigned char)s[i + k], b = (unsigned char)f[k];
        if (!find_case) { a = toupper(a); b = toupper(b); }
        if (a != b) return 0;
    }
    if (find_word) {
        if (i > 0 && is_word_char((unsigned char)s[i - 1])) return 0;
        if (i + fl < l && is_word_char((unsigned char)s[i + fl])) return 0;
    }
    return 1;
}

static int do_find(int quiet) {
    commit_line();
    int fl = (int)strlen(find_text);
    if (!fl) return 0;
    int part = curpart;
    Part *p = P();
    int y = p->cy, x = p->cx + (sel_nonempty() ? 1 : 0);
    int total = 0;
    for (int i = 0; i < doc.nparts; i++) total += doc.parts[i].n;
    for (int scanned = 0; scanned <= total + 1; scanned++) {
        Part *q = &doc.parts[part];
        if (y < q->n) {
            const char *s = q->lines[y];
            int l = (int)strlen(s);
            for (int i = x; i <= l - fl; i++) {
                if (match_at(s, i, find_text, fl)) {
                    switch_part(part);
                    p = P();
                    p->cy = y;
                    sel_on = 1; sel_ay = y; sel_ax = i;
                    p->cx = i + fl;
                    clamp_cursor();
                    return 1;
                }
            }
        }
        x = 0;
        y++;
        if (y >= doc.parts[part].n) { part = (part + 1) % doc.nparts; y = 0; }
    }
    if (!quiet) show_message("Match not found", 0);
    return 0;
}

static void word_at_cursor(char *out, int sz) {
    out[0] = 0;
    const char *s = line_text(P()->cy);
    int l = (int)strlen(s), x = P()->cx;
    if (x > l) return;
    int a = x, b = x;
    while (a > 0 && is_word_char((unsigned char)s[a - 1])) a--;
    while (b < l && is_word_char((unsigned char)s[b])) b++;
    int n = b - a;
    if (n >= sz) n = sz - 1;
    memcpy(out, s + a, (size_t)n);
    out[n] = 0;
}

static void init_find_text(void) {
    if (sel_nonempty() && sel_ay == P()->cy) {
        int y0, x0, y1, x1;
        sel_range(&y0, &x0, &y1, &x1);
        char *t = range_text(y0, x0, y1, x1);
        snprintf(find_text, sizeof find_text, "%s", t);
        free(t);
    } else {
        char w[128];
        word_at_cursor(w, sizeof w);
        if (w[0]) snprintf(find_text, sizeof find_text, "%s", w);
    }
}

static void find_dialog(void) {
    init_find_text();
    Dlg d;
    memset(&d, 0, sizeof d);
    d.title = "Find";
    dlg_center(&d, 62, 10);
    dlg_add(&d, CT_LABEL, 2, 2, 0, 0, "Find &What:", 0);
    Ctl *f = dlg_add(&d, CT_FIELD, 14, 2, 45, 1, NULL, 1);
    field_set(f, find_text);
    Ctl *c1 = dlg_add(&d, CT_CHECK, 3, 5, 0, 0, "Match &Upper/Lowercase", 2);
    Ctl *c2 = dlg_add(&d, CT_CHECK, 33, 5, 0, 0, "Whole Wo&rd", 3);
    c1->checked = find_case; c2->checked = find_word;
    dlg_add(&d, CT_SEP, 0, 7, 0, 0, NULL, 0);
    dlg_add(&d, CT_BUTTON, 16, 8, 0, 0, "OK", 10);
    dlg_add(&d, CT_BUTTON, 32, 8, 0, 0, "Cancel", 11);
    d.defbutton = 10; d.cancel = 11; d.focus = 1;
    if (run_dialog(&d) != 10) return;
    snprintf(find_text, sizeof find_text, "%s", f->text);
    find_case = c1->checked; find_word = c2->checked;
    do_find(0);
}

static void change_dialog(void) {
    init_find_text();
    Dlg d;
    memset(&d, 0, sizeof d);
    d.title = "Change";
    dlg_center(&d, 66, 13);
    dlg_add(&d, CT_LABEL, 2, 2, 0, 0, "Find &What:", 0);
    Ctl *f = dlg_add(&d, CT_FIELD, 15, 2, 48, 1, NULL, 1);
    field_set(f, find_text);
    dlg_add(&d, CT_LABEL, 2, 5, 0, 0, "C&hange To:", 0);
    Ctl *t = dlg_add(&d, CT_FIELD, 15, 5, 48, 1, NULL, 2);
    field_set(t, change_text);
    Ctl *c1 = dlg_add(&d, CT_CHECK, 3, 8, 0, 0, "Match &Upper/Lowercase", 3);
    Ctl *c2 = dlg_add(&d, CT_CHECK, 33, 8, 0, 0, "Whole Wo&rd", 4);
    c1->checked = find_case; c2->checked = find_word;
    dlg_add(&d, CT_SEP, 0, 10, 0, 0, NULL, 0);
    dlg_add(&d, CT_BUTTON, 4, 11, 0, 0, "Find and &Verify", 10);
    dlg_add(&d, CT_BUTTON, 26, 11, 0, 0, "&Change All", 12);
    dlg_add(&d, CT_BUTTON, 43, 11, 0, 0, "Cancel", 11);
    d.defbutton = 10; d.cancel = 11; d.focus = 1;
    int r = run_dialog(&d);
    if (r == 11) return;
    snprintf(find_text, sizeof find_text, "%s", f->text);
    snprintf(change_text, sizeof change_text, "%s", t->text);
    find_case = c1->checked; find_word = c2->checked;
    if (!find_text[0]) return;
    commit_line();
    snapshot();
    int count = 0;
    int fl = (int)strlen(find_text);
    if (r == 12) {
        for (int pi = 0; pi < doc.nparts; pi++) {
            Part *q = &doc.parts[pi];
            for (int y = 0; y < q->n; y++) {
                char out[2100];
                int o = 0;
                const char *s = q->lines[y];
                int l = (int)strlen(s), changed = 0;
                for (int i = 0; i < l;) {
                    if (match_at(s, i, find_text, fl) && o + (int)strlen(change_text) < 2000) {
                        memcpy(out + o, change_text, strlen(change_text)); o += (int)strlen(change_text);
                        i += fl; changed = 1; count++;
                    } else if (o < 2000) out[o++] = s[i++];
                    else i++;
                }
                out[o] = 0;
                if (changed) part_set(q, y, out);
            }
        }
        if (count) mark_modified();
        char msg[64];
        snprintf(msg, sizeof msg, count ? "%d change(s) made" : "Match not found", count);
        show_message(msg, 0);
        return;
    }
    /* verify each */
    P()->cx = 0; P()->cy = 0;
    switch_part(0);
    while (do_find(count > 0)) {
        draw_all(); present();
        const char *b[] = { "&Change", "&Skip", "Cancel" };
        int a = message_box("Change", "Change this occurrence?", b, 3, 0);
        if (a == 2) break;
        if (a == 0) {
            int y0, x0, y1, x1;
            sel_range(&y0, &x0, &y1, &x1);
            delete_range(y0, x0, y1, x1);
            insert_text(change_text);
            sel_on = 0;
            mark_modified();
            count++;
        }
    }
    sel_on = 0;
}

/* ------------------------------------------------------------------ */
/* options                                                             */
/* ------------------------------------------------------------------ */
static void config_path(char *out, int sz) {
#ifdef _WIN32
    const char *h = getenv("APPDATA");
    snprintf(out, (size_t)sz, "%s\\qblike.ini", h ? h : ".");
#else
    const char *h = getenv("HOME");
    snprintf(out, (size_t)sz, "%s/.qblike.ini", h ? h : ".");
#endif
}
static void save_config(void) {
    char p[600];
    config_path(p, sizeof p);
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "text_fg=%d\ntext_bg=%d\ncur_fg=%d\ncur_bg=%d\nbrk_fg=%d\nbrk_bg=%d\n", C_TEXT_FG, C_TEXT_BG, C_CUR_FG, C_CUR_BG, C_BRK_FG, C_BRK_BG);
    fprintf(f, "speed=%d\ncfgver=2\n", opt_speed);
    fprintf(f, "scrollbars=%d\ntabstops=%d\nsyntax=%d\nhighlight=%d\nrows=%d\ncrt=%d\n", opt_scrollbars, opt_tabstops, opt_syntax, opt_highlight, opt_rows, opt_crt);
    fclose(f);
}
static void load_config(void) {
    char p[600];
    config_path(p, sizeof p);
    FILE *f = fopen(p, "r");
    if (!f) return;
    char line[128];
    int cfgver = 1;
    while (fgets(line, sizeof line, f)) {
        char k[64]; int v;
        if (sscanf(line, "%63[^=]=%d", k, &v) != 2) continue;
        if (!strcmp(k, "cfgver")) cfgver = v;
        else if (!strcmp(k, "text_fg")) C_TEXT_FG = v & 15;
        else if (!strcmp(k, "text_bg")) C_TEXT_BG = v & 15;
        else if (!strcmp(k, "cur_fg")) C_CUR_FG = v & 15;
        else if (!strcmp(k, "cur_bg")) C_CUR_BG = v & 15;
        else if (!strcmp(k, "brk_fg")) C_BRK_FG = v & 15;
        else if (!strcmp(k, "brk_bg")) C_BRK_BG = v & 15;
        else if (!strcmp(k, "scrollbars")) opt_scrollbars = v != 0;
        else if (!strcmp(k, "tabstops")) opt_tabstops = v >= 1 && v <= 16 ? v : 4;
        else if (!strcmp(k, "syntax")) opt_syntax = v != 0;
        else if (!strcmp(k, "highlight")) opt_highlight = v != 0;
        else if (!strcmp(k, "rows")) opt_rows = (v == 43 || v == 50) ? v : 25;
        else if (!strcmp(k, "crt")) opt_crt = v != 0;
        else if (!strcmp(k, "speed")) opt_speed = (v >= 0 && v <= 3) ? v : 0;
    }
    if (cfgver < 2 && opt_tabstops == 8) opt_tabstops = 4;   /* 8 was the old default, not a choice */
    fclose(f);
}

static const char *color_names[16] = { "Black", "Blue", "Green", "Cyan", "Red", "Magenta", "Brown", "White",
    "Gray", "BrBlue", "BrGreen", "BrCyan", "BrRed", "Pink", "Yellow", "BrWhite" };

static int display_item = 0;
static int disp_fg[3], disp_bg[3];
static Ctl *disp_fgl, *disp_bgl, *disp_r[3];
static int display_change_cb(Dlg *d, Ctl *c) {
    (void)d;
    for (int i = 0; i < 3; i++) if (c == disp_r[i]) { display_item = i; disp_fgl->sel = disp_fg[i]; disp_bgl->sel = disp_bg[i]; }
    return 0;
}
static int display_list_cb(Dlg *d, Ctl *c, int dbl) {
    (void)d; (void)dbl;
    if (c == disp_fgl) disp_fg[display_item] = c->sel;
    if (c == disp_bgl) disp_bg[display_item] = c->sel;
    return 0;
}

static void display_dialog(void) {
    Dlg d;
    memset(&d, 0, sizeof d);
    d.title = "Display";
    dlg_center(&d, 66, 20);
    disp_fg[0] = C_TEXT_FG; disp_bg[0] = C_TEXT_BG;
    disp_fg[1] = C_CUR_FG; disp_bg[1] = C_CUR_BG;
    disp_fg[2] = C_BRK_FG; disp_bg[2] = C_BRK_BG;
    dlg_add(&d, CT_LABEL, 2, 1, 0, 0, "Colors", 0);
    disp_r[0] = dlg_add(&d, CT_RADIO, 3, 3, 0, 0, "1. &Normal Text", 1);
    disp_r[1] = dlg_add(&d, CT_RADIO, 3, 5, 0, 0, "2. C&urrent Statement", 2);
    disp_r[2] = dlg_add(&d, CT_RADIO, 3, 7, 0, 0, "3. &Breakpoint Lines", 3);
    for (int i = 0; i < 3; i++) disp_r[i]->group = 1;
    disp_r[0]->checked = 1;
    char **names = (char **)qb_alloc(sizeof(char *) * 16);
    for (int i = 0; i < 16; i++) names[i] = (char *)color_names[i];
    disp_fgl = dlg_add(&d, CT_LIST, 32, 3, 12, 7, "&Foreground", 4);
    disp_bgl = dlg_add(&d, CT_LIST, 49, 3, 12, 7, "Bac&kground", 5);
    disp_fgl->items = names; disp_fgl->nitems = 16; disp_fgl->sel = disp_fg[0];
    disp_bgl->items = names; disp_bgl->nitems = 8; disp_bgl->sel = disp_bg[0];
    Ctl *sb = dlg_add(&d, CT_CHECK, 3, 12, 0, 0, "&Scroll Bars", 6);
    sb->checked = opt_scrollbars;
    Ctl *hl = dlg_add(&d, CT_CHECK, 3, 13, 0, 0, "Syntax &Highlighting", 7);
    hl->checked = opt_highlight;
    Ctl *r50 = dlg_add(&d, CT_CHECK, 3, 14, 0, 0, "&43/50 line screen", 9);
    r50->checked = opt_rows != 25;
    Ctl *crt = dlg_add(&d, CT_CHECK, 3, 15, 0, 0, "&CRT Effect", 12);
    crt->checked = opt_crt;
    dlg_add(&d, CT_LABEL, 34, 13, 0, 0, "&Tab Stops:", 0);
    Ctl *ts = dlg_add(&d, CT_FIELD, 46, 13, 4, 1, NULL, 8);
    char tsb[8]; snprintf(tsb, sizeof tsb, "%d", opt_tabstops); field_set(ts, tsb);
    dlg_add(&d, CT_SEP, 0, 17, 0, 0, NULL, 0);
    dlg_add(&d, CT_BUTTON, 16, 18, 0, 0, "OK", 10);
    dlg_add(&d, CT_BUTTON, 32, 18, 0, 0, "Cancel", 11);
    d.defbutton = 10; d.cancel = 11;
    d.on_list = display_list_cb;
    d.on_change = display_change_cb;
    display_item = 0;
    int last_item = 0;
    /* custom loop: update lists when radio changes */
    for (;;) {
        for (int i = 0; i < 3; i++) if (disp_r[i]->checked) display_item = i;
        if (display_item != last_item) { disp_fgl->sel = disp_fg[display_item]; disp_bgl->sel = disp_bg[display_item]; last_item = display_item; }
        /* run one key step by using a tiny wrapper: re-run dialog until a button */
        int r = run_dialog(&d);
        if (r == 10) {
            C_TEXT_FG = disp_fg[0]; C_TEXT_BG = disp_bg[0];
            C_CUR_FG = disp_fg[1]; C_CUR_BG = disp_bg[1];
            C_BRK_FG = disp_fg[2]; C_BRK_BG = disp_bg[2];
            opt_scrollbars = sb->checked;
            opt_highlight = hl->checked;
            opt_rows = r50->checked ? 50 : 25;
            opt_crt = crt->checked;
            setcrtmode(opt_crt);
            int t = atoi(ts->text);
            if (t >= 1 && t <= 16) opt_tabstops = t;
            save_config();
            break;
        }
        if (r == 11) break;
    }
    free(names);
}

/* ------------------------------------------------------------------ */
/* help viewer                                                         */
/* ------------------------------------------------------------------ */
static char help_history[16][64];
static int nhelp_hist = 0;

static void help_show(const char *topic) {
    char cur[64];
    snprintf(cur, sizeof cur, "%s", topic);
    int top = 0, link = -1;
    for (;;) {
        const char *text = help_lookup(cur);
        if (!text) {
            char m[120];
            snprintf(m, sizeof m, "Help not found for: %s", cur);
            show_message(m, 0);
            return;
        }
        /* split lines */
        char *buf = qb_strdup(text);
        char *lines[600];
        int nl = 0;
        for (char *s = buf; s && nl < 600;) { char *e = strchr(s, '\n'); if (e) *e = 0; lines[nl++] = s; s = e ? e + 1 : NULL; }
        /* collect links: text between \x11 and \x10 (◄ ►) */
        typedef struct L { int y, x0, x1; char t[64]; } L;
        L links[200];
        int nlinks = 0;
        for (int y = 0; y < nl; y++) {
            for (char *p = lines[y]; *p; p++) {
                if ((unsigned char)*p == 0x11) {
                    char *e = strchr(p, 0x10);
                    if (!e || nlinks >= 200) break;
                    L *l = &links[nlinks++];
                    l->y = y; l->x0 = (int)(p - lines[y]); l->x1 = (int)(e - lines[y]);
                    int n = (int)(e - p - 1); if (n > 63) n = 63;
                    memcpy(l->t, p + 1, (size_t)n); l->t[n] = 0;
                    p = e;
                }
            }
        }
        int H = SH - 2;
        int done = 0;
        while (!done) {
            fill(0, 0, SW, SH, ' ', 7, 0);
            draw_menubar();
            /* title */
            put(0, 1, 218, 7, 0);
            for (int x = 1; x < SW - 1; x++) put(x, 1, 196, 7, 0);
            put(SW - 1, 1, 191, 7, 0);
            char t[100];
            snprintf(t, sizeof t, " HELP: %s ", cur);
            puts_at((SW - (int)strlen(t)) / 2, 1, t, 0, 7);
            for (int r = 0; r < H - 1; r++) {
                int y = top + r;
                put(0, 2 + r, 179, 7, 0); put(SW - 1, 2 + r, 179, 7, 0);
                if (y >= nl) continue;
                int x = 1;
                for (char *p = lines[y]; *p && x < SW - 1; p++) {
                    unsigned char ch = (unsigned char)*p;
                    int inlink = 0, sel = 0;
                    for (int k = 0; k < nlinks; k++) if (links[k].y == y && (int)(p - lines[y]) >= links[k].x0 && (int)(p - lines[y]) <= links[k].x1) { inlink = 1; sel = k == link; }
                    int fg = inlink ? 15 : 7, bg = 0;
                    if (sel) { fg = 0; bg = 7; }
                    put(x++, 2 + r, ch, fg, bg);
                }
            }
            fill(0, SH - 1, SW, 1, ' ', 0, 3);
            puts_at(1, SH - 1, "<Esc=Close> <Tab=Next Link> <Enter=Follow> <Alt+F1=Back> <PgUp/PgDn>", 0, 3);
            cur_vis = 0;
            present();
            int k = get_key();
            int base = k & ~K_SHIFT;
            if (k == K_NONE && sys_shutting_down()) { free(buf); return; }
            if (k == 27) { free(buf); nhelp_hist = 0; return; }
            if (base == K_UP) { if (top > 0) top--; }
            else if (base == K_DOWN) { if (top < nl - 1) top++; }
            else if (base == K_PGUP) { top -= H - 2; if (top < 0) top = 0; }
            else if (base == K_PGDN) { top += H - 2; if (top > nl - 1) top = nl - 1; if (top < 0) top = 0; }
            else if (base == K_HOME || base == K_CHOME) top = 0;
            else if (k == 9 || k == K_STAB) {
                if (nlinks) {
                    link = k == 9 ? (link + 1) % nlinks : (link <= 0 ? nlinks - 1 : link - 1);
                    if (links[link].y < top) top = links[link].y;
                    if (links[link].y >= top + H - 1) top = links[link].y - H + 2;
                }
            }
            else if ((k == 13 || k == K_F1) && link >= 0) {
                if (nhelp_hist < 16) snprintf(help_history[nhelp_hist++], 64, "%s", cur);
                snprintf(cur, sizeof cur, "%s", links[link].t);
                top = 0; link = -1; done = 1;
            }
            else if (k == (K_F1 | K_ALTM) || k == 8) {
                if (nhelp_hist > 0) { snprintf(cur, sizeof cur, "%s", help_history[--nhelp_hist]); top = 0; link = -1; done = 1; }
            }
            else if (k == K_MOUSE && mouse_type == 1) {
                int y = top + mouse_y - 2, x = mouse_x - 1;
                for (int i = 0; i < nlinks; i++) if (links[i].y == y && x >= links[i].x0 && x <= links[i].x1) {
                    if (nhelp_hist < 16) snprintf(help_history[nhelp_hist++], 64, "%s", cur);
                    snprintf(cur, sizeof cur, "%s", links[i].t);
                    top = 0; link = -1; done = 1;
                    break;
                }
            }
        }
        free(buf);
    }
}

static void help_topic_at_cursor(void) {
    char w[128];
    word_at_cursor(w, sizeof w);
    if (!w[0]) { help_show("Index"); return; }
    /* keywords: try word as-is, else general help */
    if (help_lookup(w)) help_show(w);
    else {
        char m[160];
        snprintf(m, sizeof m, "No help for '%s'. Showing the index.", w);
        show_message(m, 0);
        help_show("Index");
    }
}

static void speed_dialog(void) {
    Dlg d;
    memset(&d, 0, sizeof d);
    d.title = "Speed";
    dlg_center(&d, 50, 13);
    dlg_add(&d, CT_LABEL, 2, 1, 0, 0, "Program execution speed:", 0);
    Ctl *r[4];
    r[0] = dlg_add(&d, CT_RADIO, 4, 3, 0, 0, "&Maximum (modern PC)", 1);
    r[1] = dlg_add(&d, CT_RADIO, 4, 4, 0, 0, "&386 (approx.)", 2);
    r[2] = dlg_add(&d, CT_RADIO, 4, 5, 0, 0, "&486 (approx.)", 3);
    r[3] = dlg_add(&d, CT_RADIO, 4, 6, 0, 0, "&Pentium (approx.)", 4);
    for (int i = 0; i < 4; i++) { r[i]->group = 1; r[i]->checked = i == opt_speed; }
    dlg_add(&d, CT_LABEL, 2, 8, 0, 0, "Useful for old games that use delay loops.", 0);
    dlg_add(&d, CT_SEP, 0, 10, 0, 0, NULL, 0);
    dlg_add(&d, CT_BUTTON, 12, 11, 0, 0, "OK", 10);
    dlg_add(&d, CT_BUTTON, 28, 11, 0, 0, "Cancel", 11);
    d.defbutton = 10; d.cancel = 11;
    d.focus = 1 + opt_speed;
    if (run_dialog(&d) != 10) return;
    for (int i = 0; i < 4; i++) if (r[i]->checked) opt_speed = i;
    g_speed_limit = speed_values[opt_speed];
    save_config();
}

static void about_box(void) {
    const char *b[] = { "OK" };
    message_box("About", QB_NAME " Version " QB_VERSION "\n\nA QBasic-compatible interpreter and IDE.\nThis port uses dos-like by M. Gustavsson.\n\nClean-room implementation: not affiliated with Microsoft.", b, 1, 0);
}

/* ------------------------------------------------------------------ */
/* editor key handling (modern editor conventions)                     */
/* ------------------------------------------------------------------ */
static int last_edit_kind = 0, last_edit_line = -1;

static void begin_edit(int kind) {
    Part *p = P();
    if (kind != last_edit_kind || p->cy != last_edit_line || kind == 3) snapshot();
    last_edit_kind = kind;
    last_edit_line = p->cy;
}

static void word_left(void) {
    Part *p = P();
    if (p->cx == 0) { if (p->cy > 0) { move_to_line(p->cy - 1); p->cx = (int)strlen(line_text(p->cy)); } return; }
    const char *s = line_text(p->cy);
    int l = (int)strlen(s);
    int x = p->cx > l ? l : p->cx;
    while (x > 0 && !is_word_char((unsigned char)s[x - 1])) x--;
    while (x > 0 && is_word_char((unsigned char)s[x - 1])) x--;
    p->cx = x;
}
static void word_right(void) {
    Part *p = P();
    const char *s = line_text(p->cy);
    int l = (int)strlen(s);
    if (p->cx >= l) { if (p->cy < p->n - 1) { move_to_line(p->cy + 1); p->cx = 0; } return; }
    int x = p->cx;
    while (x < l && is_word_char((unsigned char)s[x])) x++;
    while (x < l && !is_word_char((unsigned char)s[x])) x++;
    p->cx = x;
}

static void select_word_at_cursor(void) {
    Part *p = P();
    const char *s = line_text(p->cy);
    int l = (int)strlen(s), x = p->cx;
    if (x > l) x = l;
    int a = x, b = x;
    while (a > 0 && is_word_char((unsigned char)s[a - 1])) a--;
    while (b < l && is_word_char((unsigned char)s[b])) b++;
    sel_on = 1; sel_ay = p->cy; sel_ax = a; p->cx = b;
}

static void indent_lines(int dir) {
    Part *p = P();
    commit_line();
    linebuf_line = -1;
    snapshot();
    int y0 = p->cy, y1 = p->cy, x0 = 0, x1 = 0;
    if (sel_nonempty()) { sel_range(&y0, &x0, &y1, &x1); if (x1 == 0 && y1 > y0) y1--; }
    for (int y = y0; y <= y1; y++) {
        const char *s = p->lines[y];
        char buf[1100];
        if (dir > 0) {
            if (!*s) continue;
            snprintf(buf, sizeof buf, "%*s%s", opt_tabstops, "", s);
        } else {
            int n = 0;
            while (n < opt_tabstops && s[n] == ' ') n++;
            snprintf(buf, sizeof buf, "%s", s + n);
        }
        part_set(p, y, buf);
    }
    if (sel_nonempty()) { sel_ay = y0; sel_ax = 0; p->cy = y1 + 1 <= p->n - 1 && x1 == 0 ? y1 + 1 : y1; p->cx = x1 == 0 ? 0 : (int)strlen(p->lines[p->cy]); }
    mark_modified();
}

static void ed_insert_char(int ch) {
    Part *p = P();
    if (sel_nonempty()) delete_selection();
    sel_on = 0;
    begin_edit(1);
    load_line(p->cy);
    int l = (int)strlen(linebuf);
    if (p->cx > 250) return;
    while (l < p->cx) linebuf[l++] = ' ';
    linebuf[l] = 0;
    if (overwrite && p->cx < l) linebuf[p->cx] = (char)ch;
    else {
        if (l >= 255) return;
        memmove(linebuf + p->cx + 1, linebuf + p->cx, (size_t)(l - p->cx + 1));
        linebuf[p->cx] = (char)ch;
    }
    p->cx++;
    linebuf_dirty = 1;
    lb_store();
    mark_modified();
}

static void ed_enter(void) {
    Part *p = P();
    if (sel_nonempty()) delete_selection();
    sel_on = 0;
    begin_edit(3);
    load_line(p->cy);
    int l = (int)strlen(linebuf);
    int x = p->cx > l ? l : p->cx;
    int indent = 0;
    while (linebuf[indent] == ' ') indent++;
    if (indent > x) indent = x;
    char tail[1100];
    snprintf(tail, sizeof tail, "%*s%s", indent, "", linebuf + x);
    linebuf[x] = 0;
    linebuf_dirty = 1;
    lb_store();
    int y = p->cy;
    commit_line();
    /* commit may have switched to a new SUB window */
    if (P() != p) return;
    if (y >= p->n) return;
    part_insert(p, y + 1, tail);
    bp_shift(curpart, y + 1, 1);
    p->cy = y + 1;
    p->cx = indent;
    mark_modified();
}

static void ed_backspace(int word) {
    Part *p = P();
    if (sel_nonempty()) { delete_selection(); return; }
    sel_on = 0;
    if (p->cx == 0) {
        if (p->cy == 0) return;
        snapshot();
        commit_line();
        linebuf_line = -1;
        int y = p->cy;
        int pl = (int)strlen(p->lines[y - 1]);
        char buf[2200];
        snprintf(buf, sizeof buf, "%s%s", p->lines[y - 1], p->lines[y]);
        part_set(p, y - 1, buf);
        part_delete(p, y);
        bp_shift(curpart, y, -1);
        p->cy = y - 1;
        p->cx = pl;
        mark_modified();
        return;
    }
    begin_edit(2);
    load_line(p->cy);
    int l = (int)strlen(linebuf);
    if (p->cx > l) { p->cx--; return; }
    int from = p->cx - 1;
    if (word) {
        int x = p->cx;
        while (x > 0 && linebuf[x - 1] == ' ') x--;
        while (x > 0 && is_word_char((unsigned char)linebuf[x - 1])) x--;
        if (x == p->cx) x--;
        from = x;
    }
    memmove(linebuf + from, linebuf + p->cx, (size_t)(l - p->cx + 1));
    p->cx = from;
    linebuf_dirty = 1;
    lb_store();
    mark_modified();
}

static void ed_delete(int word) {
    Part *p = P();
    if (sel_nonempty()) { delete_selection(); return; }
    sel_on = 0;
    load_line(p->cy);
    int l = (int)strlen(linebuf);
    if (p->cx >= l) {
        if (p->cy >= p->n - 1) return;
        snapshot();
        while (l < p->cx) linebuf[l++] = ' ';
        linebuf[l] = 0;
        char buf[2200];
        snprintf(buf, sizeof buf, "%s%s", linebuf, p->lines[p->cy + 1]);
        snprintf(linebuf, sizeof linebuf, "%s", buf);
        linebuf_dirty = 1;
        lb_store();
        part_delete(p, p->cy + 1);
        bp_shift(curpart, p->cy + 1, -1);
        mark_modified();
        return;
    }
    begin_edit(4);
    int to = p->cx + 1;
    if (word) {
        int x = p->cx;
        while (x < l && is_word_char((unsigned char)linebuf[x])) x++;
        while (x < l && linebuf[x] == ' ') x++;
        if (x == p->cx) x++;
        to = x;
    }
    memmove(linebuf + p->cx, linebuf + to, (size_t)(l - to + 1));
    linebuf_dirty = 1;
    lb_store();
    mark_modified();
}

static void exec_command(int cmd, int nested, int *ret);

/* ------------------------------------------------------------------ */
/* Edit > Format Code (beautifier, src/beautify.c)                     */
/* ------------------------------------------------------------------ */
char **qb_beautify(char **lines, int n, int base, int indent, int (*fmt)(const char *, char *, int), int *outn);
int ide_format_text(const char *in, char *out, int outsz) { format_line(in, out, outsz, 0); return 1; }

static void cmd_format(void) {
    if (focus_imm) return;
    commit_line();
    ensure_lines();
    Part *p = P();
    int y0 = 0, y1 = p->n - 1, sel = 0;
    if (sel_nonempty()) {
        int x0, x1;
        sel_range(&y0, &x0, &y1, &x1);
        if (x1 == 0 && y1 > y0) y1--;     /* selection ending at the start of a line excludes it */
        sel = 1;
    }
    char what[120];
    if (sel) snprintf(what, sizeof what, "the %d selected line%s", y1 - y0 + 1, y1 > y0 ? "s" : "");
    else if (curpart == 0) snprintf(what, sizeof what, "the module-level code");
    else snprintf(what, sizeof what, "%s %s", p->isfunc ? "FUNCTION" : "SUB", p->name);
    char msg[400];
    snprintf(msg, sizeof msg, "Format %s?\n\nStatements joined with ':' are split into lines,\n"
             "single-line IFs with several statements become\nblock IFs, and the code is re-indented.", what);
    const char *b[] = { "OK", "Cancel" };
    if (message_box("Format Code", msg, b, 2, 0) != 0) return;
    int base = 0;
    if (sel) {                              /* keep the indentation level the selection starts at */
        int col = 0;
        for (const char *s = p->lines[y0]; *s == ' ' || *s == '\t'; s++) col += *s == '\t' ? opt_tabstops - col % opt_tabstops : 1;
        base = col / (opt_tabstops > 0 ? opt_tabstops : 1);
    }
    int on = 0;
    char **out = qb_beautify(p->lines + y0, y1 - y0 + 1, base, opt_tabstops, ide_format_text, &on);
    snapshot();
    linebuf_line = -1;
    int old = y1 - y0 + 1;
    for (int y = y1; y >= y0; y--) part_delete(p, y);
    for (int i = 0; i < on; i++) { part_insert(p, y0 + i, out[i]); free(out[i]); }
    free(out);
    for (int i = 0; i < nbps; i++) {        /* breakpoints: drop those in the block, move the ones after it */
        if (bps[i].part != curpart) continue;
        if (bps[i].line >= y0 && bps[i].line <= y1) { bps[i] = bps[--nbps]; i--; }
        else if (bps[i].line > y1) bps[i].line += on - old;
    }
    sel_on = 0;
    if (sel && on > 0) { sel_on = 1; sel_ay = y0; sel_ax = 0; p->cy = y0 + on - 1; p->cx = (int)strlen(p->lines[p->cy]); }
    else { p->cy = y0; p->cx = 0; }
    mark_modified();
    clamp_cursor();
}

/* returns >= 0 to end a nested (debugger) loop with that action */
static int editor_key(int k, int nested) {
    Part *p = P();
    if (k == (6 | K_CTRL | K_SHIFT)) { int ret = -1; exec_command(CMD_FORMAT, nested, &ret); return ret; }
    int shift = (k & K_SHIFT) != 0;
    int ctrl = (k & K_CTRL) != 0;
    int base = k & ~(K_SHIFT | K_CTRL);
    int ret = -1;
    int movement = (base == K_UP || base == K_DOWN || base == K_LEFT || base == K_RIGHT || base == K_HOME || base == K_END ||
                    base == K_PGUP || base == K_PGDN || base == K_CLEFT || base == K_CRIGHT || base == K_CHOME || base == K_CEND);
    if (movement) {
        if (shift) { if (!sel_on) { sel_on = 1; sel_ay = p->cy; sel_ax = p->cx; } }
        else if (sel_nonempty() && (base == K_LEFT || base == K_RIGHT)) {
            int y0, x0, y1, x1;
            sel_range(&y0, &x0, &y1, &x1);
            sel_on = 0;
            if (base == K_LEFT) { move_to_line(y0); p->cx = x0; } else { move_to_line(y1); p->cx = x1; }
            clamp_cursor();
            return -1;
        } else sel_on = 0;
        last_edit_kind = 0;
        switch (base) {
            case K_UP: if (p->cy > 0) move_to_line(p->cy - 1); break;
            case K_DOWN: if (p->cy < p->n - 1) move_to_line(p->cy + 1); break;
            case K_LEFT:
                if (p->cx > 0) {
                    /* at a tab stop with only spaces before it back to the previous stop, jump the whole tab */
                    const char *s = line_text(p->cy);
                    int l = (int)strlen(s), n = 1, ts = opt_tabstops;
                    if (ts > 1 && p->cx % ts == 0) {
                        n = ts;
                        for (int i = p->cx - ts; i < p->cx; i++) if (i < l && s[i] != ' ') { n = 1; break; }
                    }
                    p->cx -= n;
                } else if (p->cy > 0) { move_to_line(p->cy - 1); p->cx = (int)strlen(line_text(p->cy)); }
                break;
            case K_RIGHT: {
                int l = (int)strlen(line_text(p->cy));
                if (p->cx < l) p->cx++;
                else if (p->cy < p->n - 1) { move_to_line(p->cy + 1); p->cx = 0; }
                break;
            }
            case K_HOME: {
                const char *s = line_text(p->cy);
                int ind = 0;
                while (s[ind] == ' ') ind++;
                p->cx = (p->cx == ind) ? 0 : ind;
                break;
            }
            case K_END: p->cx = (int)strlen(line_text(p->cy)); break;
            case K_PGUP: { int y = p->cy - (ED_ROWS - 1); if (y < 0) y = 0; p->top -= ED_ROWS - 1; if (p->top < 0) p->top = 0; move_to_line(y); break; }
            case K_PGDN: { int y = p->cy + (ED_ROWS - 1); if (y > p->n - 1) y = p->n - 1; p->top += ED_ROWS - 1; if (p->top > p->n - 1) p->top = p->n - 1; move_to_line(y); break; }
            case K_CLEFT: word_left(); break;
            case K_CRIGHT: word_right(); break;
            case K_CHOME: move_to_line(0); p->cx = 0; break;
            case K_CEND: move_to_line(p->n - 1); p->cx = (int)strlen(line_text(p->cy)); break;
        }
        clamp_cursor();
        return -1;
    }
    if (k == K_CUP) { if (p->top > 0) p->top--; if (p->cy >= p->top + ED_ROWS) move_to_line(p->top + ED_ROWS - 1); return -1; }
    if (k == K_CDOWN) { if (p->top < p->n - 1) p->top++; if (p->cy < p->top) move_to_line(p->top); return -1; }

    /* control keys */
    if (k >= 1 && k <= 26) {
        switch (k) {
            case 1: /* Ctrl+A */ commit_line(); sel_on = 1; sel_ay = 0; sel_ax = 0; p->cy = p->n - 1; p->cx = (int)strlen(line_text(p->cy)); clamp_cursor(); return -1;
            case 3: exec_command(CMD_COPY, nested, &ret); return ret;
            case 24: exec_command(CMD_CUT, nested, &ret); return ret;
            case 22: exec_command(CMD_PASTE, nested, &ret); return ret;
            case 26: exec_command(CMD_UNDO, nested, &ret); return ret;
            case 25: do_redo(); return -1;
            case 19: exec_command(CMD_SAVE, nested, &ret); return ret;
            case 15: exec_command(CMD_OPEN, nested, &ret); return ret;
            case 14: exec_command(CMD_NEW, nested, &ret); return ret;
            case 6: exec_command(CMD_FIND, nested, &ret); return ret;
            case 7: { /* Ctrl+G: go to line */
                char t[16] = "";
                if (input_dialog("Go To Line", "&Line number:", t, sizeof t)) { int n = atoi(t); if (n >= 1) { move_to_line(n - 1 < p->n ? n - 1 : p->n - 1); p->cx = 0; clamp_cursor(); } }
                return -1;
            }
            case 8: break;   /* Backspace handled below */
            case 9: break;   /* Tab handled below */
            case 13: break;
            default: return -1;
        }
    }
    if (k == 9) {
        if (sel_nonempty() && sel_ay != p->cy) { indent_lines(1); return -1; }
        int n = opt_tabstops - (p->cx % opt_tabstops);
        for (int i = 0; i < n; i++) ed_insert_char(' ');
        return -1;
    }
    if (k == K_STAB) { indent_lines(-1); return -1; }
    if (k == (8 | K_CTRL)) { exec_command(CMD_CHANGE, nested, &ret); return ret; }
    if (k == 13) { ed_enter(); clamp_cursor(); return -1; }
    if (k == 8) { ed_backspace(0); clamp_cursor(); return -1; }
    if (k == 127) { ed_backspace(1); clamp_cursor(); return -1; }
    if (k == (8 | K_ALTM)) { do_undo(); return -1; }
    if (base == K_DEL) {
        if (shift) { exec_command(CMD_CUT, nested, &ret); return ret; }
        ed_delete(ctrl);
        clamp_cursor();
        return -1;
    }
    if (base == K_INS) {
        if (shift) { exec_command(CMD_PASTE, nested, &ret); return ret; }
        if (ctrl) { exec_command(CMD_COPY, nested, &ret); return ret; }
        overwrite = !overwrite;
        return -1;
    }
    if (k == 27) { sel_on = 0; return -1; }
    if (k >= 32 && k < 256) { ed_insert_char(k); clamp_cursor(); return -1; }
    return -1;
}

/* ------------------------------------------------------------------ */
/* commands                                                            */
/* ------------------------------------------------------------------ */
static void exec_command(int cmd, int nested, int *ret) {
    *ret = -1;
    switch (cmd) {
        case CMD_NEW: if (nested) { pending_action = PA_NEW; *ret = DBG_ABORT; } else cmd_new(); break;
        case CMD_OPEN: if (nested) { pending_action = PA_OPEN; *ret = DBG_ABORT; } else cmd_open(); break;
        case CMD_SAVE: do_save(); break;
        case CMD_SAVEAS: save_as_dialog(); break;
        case CMD_PRINT: break;
        case CMD_EXIT: if (nested) { pending_action = PA_EXIT; *ret = DBG_ABORT; } else if (confirm_discard()) quit_requested = 1; break;
        case CMD_UNDO: do_undo(); break;
        case CMD_CUT:
            if (focus_imm) { clip_set(immbuf); immbuf[0] = 0; imm_cx = 0; break; }
            if (sel_nonempty()) { copy_selection(); delete_selection(); } else cut_line();
            break;
        case CMD_COPY:
            if (focus_imm) { clip_set(immbuf); break; }
            if (sel_nonempty()) copy_selection(); else copy_line();
            break;
        case CMD_PASTE: if (focus_imm) { char *t = clip_get(); if (t) for (char *c = t; *c && *c != '\n'; c++) imm_key((unsigned char)*c); } else paste_clip(); break;
        case CMD_CLEAR: delete_selection(); break;
        case CMD_NEWSUB: new_proc_dialog(0); break;
        case CMD_NEWFUNC: new_proc_dialog(1); break;
        case CMD_FORMAT: cmd_format(); break;
        case CMD_SUBS: subs_dialog(); break;
        case CMD_SPLIT: break;
        case CMD_OUTPUT: show_output_until_key(); break;
        case CMD_FIND: find_dialog(); break;
        case CMD_REPEAT: if (find_text[0]) do_find(0); else find_dialog(); break;
        case CMD_CHANGE: change_dialog(); break;
        case CMD_START:
            if (nested) { commit_line(); restart_step = 0; *ret = DBG_RESTART; }
            else start_program(0);
            break;
        case CMD_RESTART:
            if (nested) { restart_step = 1; *ret = DBG_RESTART; }
            else start_program(1);
            break;
        case CMD_CONTINUE:
            commit_line();
            if (nested) {
                if (code_changed) {
                    const char *b[] = { "&Yes", "&No" };
                    if (message_box("", "You will have to restart your program\nafter this edit. Restart now?", b, 2, 0) == 0) { restart_step = 0; *ret = DBG_RESTART; }
                } else *ret = DBG_CONTINUE;
            } else start_program(0);
            break;
        case CMD_STEP:
            commit_line();
            if (nested) { if (code_changed) { restart_step = 1; *ret = DBG_RESTART; } else *ret = DBG_STEP; }
            else start_program(1);
            break;
        case CMD_PSTEP:
            commit_line();
            if (nested) { if (code_changed) { restart_step = 1; *ret = DBG_RESTART; } else *ret = DBG_PSTEP; }
            else start_program(1);
            break;
        case CMD_TRACE: trace_on = !trace_on; break;
        case CMD_TOGGLEBP: commit_line(); bp_toggle(curpart, P()->cy); break;
        case CMD_CLEARBP: nbps = 0; break;
        case CMD_SETNEXT: if (nested) set_next_statement(); break;
        case CMD_DISPLAY: display_dialog(); break;
        case CMD_SPEED: speed_dialog(); break;
        case CMD_HELPPATH: break;
        case CMD_SYNTAX: opt_syntax = !opt_syntax; save_config(); break;
        case CMD_HELPINDEX: help_show("Index"); break;
        case CMD_HELPCONTENTS: help_show("Contents"); break;
        case CMD_HELPTOPIC: help_topic_at_cursor(); break;
        case CMD_HELPUSING: help_show("Using Help"); break;
        case CMD_ABOUT: about_box(); break;
    }
}

/* ------------------------------------------------------------------ */
/* menu interaction                                                    */
/* ------------------------------------------------------------------ */
static int menu_first_item(int m) { for (int i = 0; i < menus[m].n; i++) if (menus[m].items[i].label[0] != '-') return i; return 0; }

static int menu_dropdown_geometry(int m, int *x, int *w) {
    Menu *mm = &menus[m];
    int ww = 0;
    for (int i = 0; i < mm->n; i++) { int l = hot_len(mm->items[i].label) + (int)strlen(mm->items[i].key) + 4; if (l > ww) ww = l; }
    ww += 4; if (ww < 18) ww = 18;
    int xx = mm->x - 2;
    if (xx + ww > SW - 2) xx = SW - 2 - ww;
    *x = xx; *w = ww;
    return mm->n;
}

static int run_menu(int start_menu, int open, int nested) {
    menu_sel = start_menu;
    menu_open = open ? start_menu : -1;
    menu_bar_active = !open;
    int item = open ? menu_first_item(start_menu) : 0;
    int ret = -1;
    for (;;) {
        if (menu_open >= 0) {
            snprintf(status_override, sizeof status_override, "F1=Help %c %s", 179, menus[menu_open].items[item].help);
        } else snprintf(status_override, sizeof status_override, "F1=Help %c Enter=Display Menu %c Esc=Cancel %c Arrow=Next Item", 179, 179, 179);
        int save_sel = menu_sel;
        if (menu_open >= 0) menu_sel = item;
        draw_all();
        if (menu_open >= 0) menu_sel = save_sel;
        /* draw_all uses menu_sel for the dropdown highlight and bar highlight */
        if (menu_open >= 0) {
            /* redraw bar highlight for open menu */
            draw_menubar();
            int s2 = menu_sel; menu_sel = item; draw_dropdown(); menu_sel = s2;
        }
        present();
        int k = get_key();
        if (k == K_NONE) { if (sys_shutting_down()) break; continue; }
        int base = k & ~K_SHIFT;
        if (k == 27 || k == K_ALT) break;
        if (k == K_MOUSE) {
            if (mouse_type != 1) continue;
            if (mouse_y == 0) {
                int hit = -1;
                for (int i = 0; i < NMENUS; i++) { int w = hot_len(menus[i].title); if (mouse_x >= menus[i].x - 1 && mouse_x <= menus[i].x + w) hit = i; }
                if (hit < 0) break;
                if (menu_open == hit) break;
                menu_sel = hit; menu_open = hit; menu_bar_active = 0; item = menu_first_item(hit);
                continue;
            }
            if (menu_open >= 0) {
                int x, w;
                int n = menu_dropdown_geometry(menu_open, &x, &w);
                if (mouse_x > x && mouse_x < x + w - 1 && mouse_y >= 2 && mouse_y < 2 + n) {
                    int it = mouse_y - 2;
                    if (menus[menu_open].items[it].label[0] == '-') continue;
                    int cmd = menus[menu_open].items[it].cmd;
                    menu_open = -1; menu_bar_active = 0; status_override[0] = 0;
                    exec_command(cmd, nested, &ret);
                    return ret;
                }
            }
            break;
        }
        if (base == K_LEFT || base == K_RIGHT) {
            menu_sel = (menu_sel + (base == K_LEFT ? NMENUS - 1 : 1)) % NMENUS;
            if (menu_open >= 0) { menu_open = menu_sel; item = menu_first_item(menu_sel); }
            continue;
        }
        if (menu_open < 0) {
            if (k == 13 || base == K_DOWN || base == K_UP) { menu_open = menu_sel; menu_bar_active = 0; item = menu_first_item(menu_sel); continue; }
            int ch = (k & K_ALTM) ? (k & 0xFF) : (k < 256 ? k : 0);
            ch = toupper(ch);
            for (int i = 0; i < NMENUS; i++) if (hot_key(menus[i].title) == ch) { menu_sel = i; menu_open = i; menu_bar_active = 0; item = menu_first_item(i); break; }
            continue;
        }
        Menu *m = &menus[menu_open];
        if (base == K_UP) { do { item = (item + m->n - 1) % m->n; } while (m->items[item].label[0] == '-'); continue; }
        if (base == K_DOWN) { do { item = (item + 1) % m->n; } while (m->items[item].label[0] == '-'); continue; }
        if (k == 13) {
            int cmd = m->items[item].cmd;
            menu_open = -1; menu_bar_active = 0; status_override[0] = 0;
            exec_command(cmd, nested, &ret);
            return ret;
        }
        if (k == K_F1) { help_show(hot_len(m->items[item].label) ? "Menus" : "Index"); continue; }
        int ch = (k & K_ALTM) ? (k & 0xFF) : (k < 256 ? k : 0);
        ch = toupper(ch);
        if (k & K_ALTM) {
            for (int i = 0; i < NMENUS; i++) if (hot_key(menus[i].title) == ch) { menu_sel = i; menu_open = i; item = menu_first_item(i); break; }
            continue;
        }
        for (int i = 0; i < m->n; i++) if (m->items[i].label[0] != '-' && hot_key(m->items[i].label) == ch) {
            int cmd = m->items[i].cmd;
            menu_open = -1; menu_bar_active = 0; status_override[0] = 0;
            exec_command(cmd, nested, &ret);
            return ret;
        }
    }
    menu_open = -1;
    menu_bar_active = 0;
    status_override[0] = 0;
    return ret;
}

/* ------------------------------------------------------------------ */
/* mouse in editor                                                     */
/* ------------------------------------------------------------------ */
static double last_click_t = 0;
static int last_click_x = -1, last_click_y = -1;
static int dragging = 0;

static int handle_mouse(int nested) {
    Part *p = P();
    int ret = -1;
    if (mouse_type == 1 && mouse_y == 0) {
        for (int i = 0; i < NMENUS; i++) {
            int w = hot_len(menus[i].title);
            if (mouse_x >= menus[i].x - 1 && mouse_x <= menus[i].x + w) return run_menu(i, 1, nested);
        }
        return -1;
    }
    if (mouse_y == SH - 1 && mouse_type == 1) {
        /* status bar shortcuts */
        if (mouse_x >= 1 && mouse_x < 16) exec_command(CMD_HELPUSING, nested, &ret);
        else if (mouse_x >= 17 && mouse_x < 29) { commit_line(); focus_imm = !focus_imm; }
        else if (mouse_x >= 30 && mouse_x < 40) exec_command(running ? CMD_TOGGLEBP : CMD_SUBS, nested, &ret);
        else if (mouse_x >= 41 && mouse_x < 50) exec_command(CMD_CONTINUE, nested, &ret);
        else if (mouse_x >= 51 && mouse_x < 60) exec_command(CMD_STEP, nested, &ret);
        return ret;
    }
    int in_text = mouse_y >= ED_TOP && mouse_y < ED_TOP + ED_ROWS && mouse_x >= 1 && mouse_x < SW - 1 - (opt_scrollbars ? 1 : 0);
    if (mouse_type == 1 && imm_visible && mouse_y >= IMM_TOP && mouse_y < IMM_TOP + IMM_ROWS) {
        commit_line();
        focus_imm = 1;
        imm_commit_line();
        imm_load_line(imm_top + mouse_y - IMM_TOP);
        imm_cx = mouse_x - 1;
        return -1;
    }
    if (opt_scrollbars && mouse_type == 1) {
        if (mouse_x == SW - 1 && mouse_y >= ED_TOP && mouse_y < ED_TOP + ED_ROWS) {
            if (mouse_y == ED_TOP) { if (p->top > 0) p->top--; }
            else if (mouse_y == ED_TOP + ED_ROWS - 1) { if (p->top < p->n - 1) p->top++; }
            else if (mouse_y < ED_TOP + ED_ROWS / 2) { p->top -= ED_ROWS - 1; if (p->top < 0) p->top = 0; }
            else { p->top += ED_ROWS - 1; if (p->top > p->n - 1) p->top = p->n - 1; }
            if (p->cy < p->top) move_to_line(p->top);
            if (p->cy >= p->top + ED_ROWS) move_to_line(p->top + ED_ROWS - 1);
            return -1;
        }
        if (mouse_y == ED_TOP + ED_ROWS) {
            if (mouse_x == 1) { if (p->left > 0) p->left--; }
            else if (mouse_x == SW - 2) { if (p->left < 255) p->left++; }
            return -1;
        }
    }
    if (mouse_type == 1 && in_text) {
        focus_imm = 0;
        int y = p->top + mouse_y - ED_TOP;
        if (y >= p->n) y = p->n - 1;
        int x = p->left + mouse_x - 1;
        double now = sys_time();
        int dbl = (now - last_click_t < 0.4 && mouse_x == last_click_x && mouse_y == last_click_y);
        last_click_t = now; last_click_x = mouse_x; last_click_y = mouse_y;
        if (shift_held()) { if (!sel_on) { sel_on = 1; sel_ay = p->cy; sel_ax = p->cx; } }
        else sel_on = 0;
        move_to_line(y);
        p->cx = x;
        { int l = (int)strlen(line_text(y)); if (p->cx > l) p->cx = l; }
        if (dbl) { select_word_at_cursor(); dragging = 0; }
        else {
            dragging = 1;
            if (!sel_on) { sel_on = 1; sel_ay = p->cy; sel_ax = p->cx; }
        }
        clamp_cursor();
        return -1;
    }
    if (mouse_type == 4 && dragging) {
        int my = mouse_y, mx = mouse_x;
        if (my < ED_TOP) { if (p->top > 0) p->top--; my = ED_TOP; }
        if (my >= ED_TOP + ED_ROWS) { if (p->top + ED_ROWS < p->n) p->top++; my = ED_TOP + ED_ROWS - 1; }
        int y = p->top + my - ED_TOP;
        if (y >= p->n) y = p->n - 1;
        if (y < 0) y = 0;
        int x = p->left + mx - 1;
        if (x < 0) x = 0;
        if (y != p->cy) { commit_line(); }
        p->cy = y;
        int l = (int)strlen(line_text(y));
        p->cx = x > l ? l : x;
        clamp_cursor();
        return -1;
    }
    if (mouse_type == 2) {
        dragging = 0;
        if (sel_on && !sel_nonempty()) sel_on = 0;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* main loop                                                           */
/* ------------------------------------------------------------------ */
static void handle_pending(void) {
    int a = pending_action;
    pending_action = 0;
    if (a == PA_NEW) cmd_new();
    else if (a == PA_OPEN) cmd_open();
    else if (a == PA_EXIT) { if (confirm_discard()) quit_requested = 1; }
}

static int ide_loop(int nested) {
    static int last_mx = -1, last_my = -1;
    for (;;) {
        if (quit_requested) return nested ? DBG_ABORT : 0;
        if (nested && pending_action) return DBG_ABORT;
        if (!nested && pending_action) { handle_pending(); continue; }
        if (sys_shutting_down()) return nested ? DBG_ABORT : 0;
        draw_all();
        present();
        /* mouse drag tracking */
        int k;
        g_idle_redraw = 1;
        k = get_key();
        g_idle_redraw = 0;
        if (k == K_NONE) {
            if (dragging && mouse_button()) {
                int mx = mousex(), my = mousey();
                if (mx != last_mx || my != last_my || my < ED_TOP || my >= ED_TOP + ED_ROWS) {
                    last_mx = mx; last_my = my;
                    mouse_type = 4; mouse_x = mx; mouse_y = my;
                    handle_mouse(nested);
                }
            } else if (dragging) { dragging = 0; if (sel_on && !sel_nonempty()) sel_on = 0; }
            sys_sleep(10);
            continue;
        }
        int r = -1;
        int base = k & ~K_SHIFT;
        if (k == K_MOUSE) r = handle_mouse(nested);
        else if (k == K_ALT) r = run_menu(0, 0, nested);
        else if ((k & K_ALTM) && (k & 0xFFFF) < 256 && isalpha(k & 0xFF)) {
            int ch = toupper(k & 0xFF);
            for (int i = 0; i < NMENUS; i++) if (hot_key(menus[i].title) == ch) { r = run_menu(i, 1, nested); break; }
        }
        else if (k == K_F1) exec_command(CMD_HELPTOPIC, nested, &r);
        else if (k == (K_F1 | K_SHIFT)) exec_command(CMD_HELPUSING, nested, &r);
        else if (k == K_F2) exec_command(CMD_SUBS, nested, &r);
        else if (k == (K_F2 | K_SHIFT) || k == (K_F2 | K_CTRL)) {
            if (doc.nparts > 1) switch_part((curpart + (k & K_SHIFT ? 1 : doc.nparts - 1)) % doc.nparts);
        }
        else if (k == K_F3) exec_command(CMD_REPEAT, nested, &r);
        else if (k == K_F4) exec_command(CMD_OUTPUT, nested, &r);
        else if (k == K_F5) exec_command(CMD_CONTINUE, nested, &r);
        else if (k == (K_F5 | K_SHIFT)) exec_command(CMD_START, nested, &r);
        else if (k == K_F6 || k == (K_F6 | K_SHIFT)) { commit_line(); if (imm_visible) focus_imm = !focus_imm; }
        else if (k == K_F7) {
            /* run to cursor */
            commit_line();
            bp_toggle(curpart, P()->cy);
            int tmp = bp_index(curpart, P()->cy);
            int added = tmp >= 0;
            if (!added) bp_toggle(curpart, P()->cy);  /* it existed: restore */
            exec_command(CMD_CONTINUE, nested, &r);
            if (!nested && added) { int i = bp_index(curpart, P()->cy); if (i >= 0) bps[i] = bps[--nbps]; }
        }
        else if (k == K_F8) exec_command(CMD_STEP, nested, &r);
        else if (k == K_F9) exec_command(CMD_TOGGLEBP, nested, &r);
        else if (k == K_F10) exec_command(CMD_PSTEP, nested, &r);
        else if (focus_imm) {
            if (k == 27 && 0) {}
            imm_key(k);
        }
        else r = editor_key(k, nested);
        (void)base;
        if (nested && r >= 0) return r;
    }
}

/* window closed with unsaved changes: write a recovery copy */
static void emergency_save(void) {
    if (linebuf_line >= 0 && linebuf_line < P()->n) part_set(P(), linebuf_line, linebuf);
    if (!modified) return;
    char path[700];
    if (filename[0]) {
        snprintf(path, sizeof path, "%s", filename);
        char *dot = strrchr(path, '.');
        char *sl = strrchr(path, '/'); char *bs = strrchr(path, '\\');
        if (bs > sl) sl = bs;
        if (dot && (!sl || dot > sl)) *dot = 0;
        strncat(path, ".RECOVER.BAS", sizeof path - strlen(path) - 1);
    } else snprintf(path, sizeof path, "UNTITLED.RECOVER.BAS");
    doc_save(&doc, path);
}

static void welcome(void) {
    const char *b[] = { "Press Enter to see Help", "Press ESC to clear this dialog box" };
    int r = message_box("", "Welcome to " QB_NAME "\n\nA QBasic-compatible programming environment\nfor modern Windows and Linux.\n\nThis port uses dos-like by M. Gustavsson.", b, 2, 0);
    if (r == 0) help_show("Contents");
}

/* used by main.c for /X runs: apply CRT and speed settings */
void ide_apply_config(void) {
    load_config();
    setcrtmode(opt_crt);
    g_speed_limit = speed_values[opt_speed];
}

void ide_main(int argc, char **argv) {
    g_ide_active = 1;
    load_config();
    setcrtmode(opt_crt);
    g_speed_limit = speed_values[opt_speed];
    doc_init(&doc);
    part_insert(&doc.parts[0], 0, "");
    part_init(&imm);
    part_insert(&imm, 0, "");
    imm_load_line(0);
    scr_set_ide_owner(1);
    const char *file = NULL;
    int run = 0;
    for (int i = 1; i < argc; i++) {
        if (str_ieq(argv[i], "/RUN") || str_ieq(argv[i], "-run") || str_ieq(argv[i], "--run")) run = 1;
        else if (argv[i][0] != '-' && argv[i][0] != '/') file = argv[i];
    }
    layout();
    if (file) {
        FILE *t = fopen(file, "rb");
        if (t) { fclose(t); load_file(file); }
        else {
            /* QBasic opens a new file with that name */
            char f2[600];
            const char *base = strrchr(file, '/'); base = base ? base + 1 : file;
            if (!strchr(base, '.')) snprintf(f2, sizeof f2, "%s.BAS", file); else snprintf(f2, sizeof f2, "%s", file);
            t = fopen(f2, "rb");
            if (t) { fclose(t); load_file(f2); }
            else snprintf(filename, sizeof filename, "%s", f2);
        }
        if (run) start_program(0);
    } else if (!g_headless_test) {
        draw_all();
        welcome();
    }
    ide_loop(0);
    if (sys_shutting_down()) emergency_save();
    g_ide_active = 0;
}

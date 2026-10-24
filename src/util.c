/* util.c - memory/string helpers, error texts, value helpers, number formatting */
#include "qb.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <math.h>
#include <float.h>

Program *g_prog = NULL;

void *qb_alloc(size_t n) {
    void *p = calloc(1, n ? n : 1);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(1); }
    return p;
}
void *qb_realloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) { fprintf(stderr, "out of memory\n"); exit(1); }
    return q;
}
char *qb_strdup(const char *s) { return qb_strndup(s, (int)strlen(s)); }
char *qb_strndup(const char *s, int n) {
    char *p = (char *)qb_alloc((size_t)n + 1);
    if (n > 0) memcpy(p, s, (size_t)n);
    p[n] = 0;
    return p;
}
void str_upper(char *s) { for (; *s; s++) *s = (char)toupper((unsigned char)*s); }
int qb_stricmp(const char *a, const char *b) {
    for (;; a++, b++) {
        int x = toupper((unsigned char)*a), y = toupper((unsigned char)*b);
        if (x != y || !x) return x - y;
    }
}
int str_ieq(const char *a, const char *b) {
    while (*a && *b) { if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return 0; a++; b++; }
    return *a == *b;
}

/* ------------------------------------------------------------------ */
static const struct { int code; const char *msg; } errtab[] = {
    { 1, "NEXT without FOR" }, { 2, "Syntax error" }, { 3, "RETURN without GOSUB" },
    { 4, "Out of DATA" }, { 5, "Illegal function call" }, { 6, "Overflow" },
    { 7, "Out of memory" }, { 8, "Label not defined" }, { 9, "Subscript out of range" },
    { 10, "Duplicate definition" }, { 11, "Division by zero" }, { 12, "Illegal in direct mode" },
    { 13, "Type mismatch" }, { 14, "Out of string space" }, { 16, "String formula too complex" },
    { 17, "Cannot continue" }, { 18, "Function not defined" }, { 19, "No RESUME" },
    { 20, "RESUME without error" }, { 24, "Device timeout" }, { 25, "Device fault" },
    { 26, "FOR without NEXT" }, { 27, "Out of paper" }, { 29, "WHILE without WEND" },
    { 30, "WEND without WHILE" }, { 33, "Duplicate label" }, { 35, "Subprogram not defined" },
    { 37, "Argument-count mismatch" }, { 38, "Array not defined" }, { 40, "Variable required" },
    { 50, "FIELD overflow" }, { 51, "Internal error" }, { 52, "Bad file name or number" },
    { 53, "File not found" }, { 54, "Bad file mode" }, { 55, "File already open" },
    { 56, "FIELD statement active" }, { 57, "Device I/O error" }, { 58, "File already exists" },
    { 59, "Bad record length" }, { 61, "Disk full" }, { 62, "Input past end of file" },
    { 63, "Bad record number" }, { 64, "Bad file name" }, { 67, "Too many files" },
    { 68, "Device unavailable" }, { 69, "Communication-buffer overflow" }, { 70, "Permission denied" },
    { 71, "Disk not ready" }, { 72, "Disk-media error" }, { 73, "Advanced feature unavailable" },
    { 74, "Rename across disks" }, { 75, "Path/File access error" }, { 76, "Path not found" },
    { 0, NULL }
};

const char *rt_errtext(int code) {
    for (int i = 0; errtab[i].msg; i++) if (errtab[i].code == code) return errtab[i].msg;
    return "Unprintable error";
}

/* ------------------------------------------------------------------ */
/* values                                                             */
/* ------------------------------------------------------------------ */
Rec *rec_new(int udt) {
    Udt *u = &g_prog->udts[udt];
    Rec *r = (Rec *)qb_alloc(sizeof(Rec) + sizeof(Value) * (size_t)(u->nfields > 0 ? u->nfields - 1 : 0));
    r->udt = udt;
    for (int i = 0; i < u->nfields; i++) val_init(&r->f[i], u->f[i].t, u->f[i].udt, u->f[i].fixlen);
    return r;
}
void rec_free(Rec *r) {
    if (!r) return;
    Udt *u = &g_prog->udts[r->udt];
    for (int i = 0; i < u->nfields; i++) val_free(&r->f[i]);
    free(r);
}
Rec *rec_dup(Rec *r) {
    Rec *n = rec_new(r->udt);
    Udt *u = &g_prog->udts[r->udt];
    for (int i = 0; i < u->nfields; i++) val_copy(&n->f[i], &r->f[i]);
    return n;
}

void val_init(Value *v, int t, int udt, int fixlen) {
    memset(v, 0, sizeof *v);
    v->t = (uint8_t)t;
    v->fixlen = fixlen;
    if (t == T_STR && fixlen > 0) {
        v->s = (char *)qb_alloc((size_t)fixlen + 1);
        memset(v->s, ' ', (size_t)fixlen);  /* QBasic initializes fixed strings with spaces? actually NUL */
        memset(v->s, 0, (size_t)fixlen);
        v->slen = fixlen;
    }
    if (t == T_UDT) v->r = rec_new(udt);
}
void val_free(Value *v) {
    free(v->s);
    v->s = NULL;
    v->slen = 0;
    if (v->r) { rec_free(v->r); v->r = NULL; }
}
void val_setstr(Value *v, const char *s, int len) {
    if (v->fixlen > 0) {
        int n = len < v->fixlen ? len : v->fixlen;
        if (n > 0) memmove(v->s, s, (size_t)n);
        if (n < v->fixlen) memset(v->s + n, ' ', (size_t)(v->fixlen - n));
        v->slen = v->fixlen;
        return;
    }
    char *p = (char *)qb_alloc((size_t)len + 1);
    if (len > 0) memcpy(p, s, (size_t)len);
    free(v->s);
    v->s = p;
    v->slen = len;
}
void val_copy(Value *dst, const Value *src) {
    if (dst == src) return;
    if (src->t == T_STR) { val_setstr(dst, src->s ? src->s : "", src->slen); return; }
    if (src->t == T_UDT) {
        Udt *u = &g_prog->udts[src->r->udt];
        if (!dst->r) dst->r = rec_new(src->r->udt);
        for (int i = 0; i < u->nfields; i++) val_copy(&dst->r->f[i], &src->r->f[i]);
        return;
    }
    dst->n = fit_num(dst->t, src->n);
}

double qb_round_even(double d) {
    double f = floor(d);
    double diff = d - f;
    if (diff > 0.5) return f + 1;
    if (diff < 0.5) return f;
    return fmod(f, 2.0) == 0 ? f : f + 1;
}

double fit_num(int t, double d) {
    switch (t) {
        case T_INT:
            d = qb_round_even(d);
            if (d < -32768.0 || d > 32767.0 || d != d) rt_error(6);
            return d;
        case T_LNG:
            d = qb_round_even(d);
            if (d < -2147483648.0 || d > 2147483647.0 || d != d) rt_error(6);
            return d;
        case T_SNG:
            if (d > FLT_MAX || d < -FLT_MAX || d != d) rt_error(6);
            return (double)(float)d;
        case T_DBL:
            if (d > DBL_MAX || d < -DBL_MAX || d != d) rt_error(6);
            return d;
    }
    return d;
}
void val_setnum(Value *v, double d) { v->n = fit_num(v->t, d); }

/* ------------------------------------------------------------------ */
/* number formatting compatible with QBasic PRINT / STR$              */
/* ------------------------------------------------------------------ */
int fmt_number(char *buf, double d, int t, int forprint) {
    (void)forprint;
    char *o = buf;
    if (t == T_INT || t == T_LNG) {
        return sprintf(buf, "%s%.0f", d >= 0 ? " " : "", d);
    }
    int digits = (t == T_DBL) ? 16 : 7;
    if (t == T_SNG) d = (float)d;
    if (d == 0) { strcpy(buf, " 0"); return 2; }
    *o++ = d < 0 ? '-' : ' ';
    double a = fabs(d);
    char tmp[64];
    snprintf(tmp, sizeof tmp, "%.*e", digits - 1, a);
    /* tmp: D.DDDDDDe+XX */
    char dig[32];
    int nd = 0;
    char *p = tmp;
    while (*p && *p != 'e') { if (isdigit((unsigned char)*p)) dig[nd++] = *p; p++; }
    int e10 = atoi(p + 1);
    while (nd > 1 && dig[nd - 1] == '0') nd--;
    dig[nd] = 0;
    int useexp = 0;
    if (e10 >= digits) useexp = 1;
    else if (e10 < 0) { if ((-e10 - 1) + nd > digits) useexp = 1; }
    if (useexp) {
        *o++ = dig[0];
        if (nd > 1) { *o++ = '.'; for (int i = 1; i < nd; i++) *o++ = dig[i]; }
        *o++ = (t == T_DBL) ? 'D' : 'E';
        *o++ = e10 < 0 ? '-' : '+';
        o += sprintf(o, "%02d", abs(e10));
    } else if (e10 >= 0) {
        for (int i = 0; i <= e10; i++) *o++ = i < nd ? dig[i] : '0';
        if (nd > e10 + 1) { *o++ = '.'; for (int i = e10 + 1; i < nd; i++) *o++ = dig[i]; }
    } else {
        *o++ = '.';
        for (int i = 0; i < -e10 - 1; i++) *o++ = '0';
        for (int i = 0; i < nd; i++) *o++ = dig[i];
    }
    *o = 0;
    return (int)(o - buf);
}

/* VAL(): leading blanks skipped, blanks inside the number ignored */
double qb_val(const char *s, int len) {
    char buf[128];
    int bl = 0, i = 0;
    while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    if (i + 1 < len && s[i] == '&') {
        int base = 8;
        i++;
        if (i < len && (s[i] == 'h' || s[i] == 'H')) { base = 16; i++; }
        else if (i < len && (s[i] == 'o' || s[i] == 'O')) { i++; }
        uint32_t v = 0;
        int digs = 0;
        for (; i < len; i++) {
            int c = s[i], dv;
            if (c == ' ') continue;
            if (c >= '0' && c <= '9') dv = c - '0';
            else if (base == 16 && isxdigit(c)) dv = toupper(c) - 'A' + 10;
            else break;
            if (dv >= base) break;
            v = v * (uint32_t)base + (uint32_t)dv;
            digs++;
        }
        if (v <= 0xFFFF) return (double)(int16_t)(uint16_t)v;
        return (double)(int32_t)v;
    }
    int seen_digit = 0, seen_dot = 0, seen_e = 0;
    for (; i < len && bl < 120; i++) {
        char c = s[i];
        if (c == ' ' || c == '\t') continue;
        if ((c == '+' || c == '-') && (bl == 0 || buf[bl - 1] == 'e')) { buf[bl++] = c; continue; }
        if (isdigit((unsigned char)c)) { buf[bl++] = c; seen_digit = 1; continue; }
        if (c == '.' && !seen_dot && !seen_e) { buf[bl++] = c; seen_dot = 1; continue; }
        if ((c == 'e' || c == 'E' || c == 'd' || c == 'D') && !seen_e && seen_digit) { buf[bl++] = 'e'; seen_e = 1; continue; }
        break;
    }
    buf[bl] = 0;
    (void)seen_digit;
    if (bl > 0 && buf[bl - 1] == 'e') buf[--bl] = 0;
    if (bl > 0 && (buf[bl - 1] == '+' || buf[bl - 1] == '-') && bl > 1 && buf[bl - 2] == 'e') { buf[bl - 2] = 0; }
    return strtod(buf, NULL);
}

/* ------------------------------------------------------------------ */
/* Unicode -> CP437                                                   */
/* ------------------------------------------------------------------ */
static const unsigned short cp437_hi[128] = {
    0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
    0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,
    0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
    0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
    0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
    0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
    0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,
    0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x00A0
};

int unicode_to_cp437(int cp) {
    if (cp < 128) return cp;
    for (int i = 0; i < 128; i++) if (cp437_hi[i] == cp) return 128 + i;
    /* a few fallbacks for accented capitals not in CP437 */
    switch (cp) {
        case 0x00C1: return 'A'; case 0x00CD: return 'I'; case 0x00D3: return 'O'; case 0x00DA: return 'U';
        case 0x00C0: return 'A'; case 0x00C8: return 'E'; case 0x00CC: return 'I'; case 0x00D2: return 'O'; case 0x00D9: return 'U';
        case 0x20AC: return 'E';
    }
    return '?';
}

int cp437_to_unicode(int c) {
    if (c < 128) return c;
    return cp437_hi[c - 128];
}

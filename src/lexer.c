/* lexer.c - tokenizer for QBasic source lines */
#include "qb.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <math.h>

#define KW_STR(k) #k,
static const char *kw_raw[KW_COUNT] = { "", KEYWORDS(KW_STR) };
#undef KW_STR

const char *kw_names[KW_COUNT];
static char kw_buf[KW_COUNT][16];
static int kw_ready = 0;

static void kw_init(void) {
    if (kw_ready) return;
    for (int i = 0; i < KW_COUNT; i++) {
        const char *r = kw_raw[i];
        size_t l = strlen(r);
        strcpy(kw_buf[i], r);
        if (l > 2 && r[l - 2] == '_' && r[l - 1] == 'S') {
            kw_buf[i][l - 2] = '$';
            kw_buf[i][l - 1] = 0;
        }
        kw_names[i] = kw_buf[i];
    }
    kw_ready = 1;
}

const char *kw_text(int kw) {
    kw_init();
    if (kw <= 0 || kw >= KW_COUNT) return "";
    return kw_names[kw];
}

int kw_lookup(const char *word, int len) {
    kw_init();
    char up[32];
    if (len <= 0 || len >= 31) return 0;
    for (int i = 0; i < len; i++) up[i] = (char)toupper((unsigned char)word[i]);
    up[len] = 0;
    /* binary search would be nicer; table is small */
    int lo = 1, hi = KW_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(up, kw_names[mid]);
        if (c == 0) return mid;
        if (c < 0) hi = mid - 1; else lo = mid + 1;
    }
    /* table may not be perfectly sorted after $ substitution; fallback */
    for (int i = 1; i < KW_COUNT; i++) if (strcmp(up, kw_names[i]) == 0) return i;
    return 0;
}

static void push(TokLine *tl, Tok *t, int *cap) {
    if (tl->n >= *cap) {
        *cap = *cap ? *cap * 2 : 16;
        tl->t = (Tok *)qb_realloc(tl->t, sizeof(Tok) * (size_t)*cap);
    }
    tl->t[tl->n++] = *t;
}

static int isidchar(int c) { return isalnum(c) || c == '.'; }

/* parse a decimal literal starting at p; returns chars consumed */
static int lex_number(const char *p, Tok *t) {
    const char *s = p;
    char buf[128];
    int bl = 0;
    int digits = 0, sig = 0, leading = 1;
    int hasdot = 0, hasexp = 0, dexp = 0;
    while (isdigit((unsigned char)*p) || (*p == '.' && !hasdot)) {
        if (*p == '.') { hasdot = 1; }
        else {
            digits++;
            if (*p != '0' || !leading) { leading = 0; sig++; }
        }
        if (bl < 120) buf[bl++] = *p;
        p++;
    }
    if ((*p == 'e' || *p == 'E' || *p == 'd' || *p == 'D')) {
        const char *q = p + 1;
        if (*q == '+' || *q == '-') q++;
        if (isdigit((unsigned char)*q) || 1) {
            /* QBasic accepts "1E" as 1E+0; we require digits optionally */
            hasexp = 1;
            if (*p == 'd' || *p == 'D') dexp = 1;
            if (bl < 120) buf[bl++] = 'e';
            p++;
            if (*p == '+' || *p == '-') { if (bl < 120) buf[bl++] = *p; p++; }
            int any = 0;
            while (isdigit((unsigned char)*p)) { if (bl < 120) buf[bl++] = *p; p++; any = 1; }
            if (!any && bl < 120) buf[bl++] = '0';
        }
    }
    buf[bl] = 0;
    double v = strtod(buf, NULL);
    int type;
    if (*p == '%') { type = T_INT; p++; }
    else if (*p == '&') { type = T_LNG; p++; }
    else if (*p == '!') { type = T_SNG; p++; }
    else if (*p == '#') { type = T_DBL; p++; }
    else if (dexp) type = T_DBL;
    else if (hasdot || hasexp) type = (sig > 7) ? T_DBL : T_SNG;
    else {
        if (v <= 32767) type = T_INT;
        else if (v <= 2147483647.0) type = T_LNG;
        else type = (sig > 7) ? T_DBL : T_SNG;
    }
    if (type == T_SNG) v = (float)v;
    t->k = TK_NUM;
    t->ntype = (uint8_t)type;
    t->num = v;
    return (int)(p - s);
}

static int lex_hex(const char *p, Tok *t) {
    const char *s = p;
    p++; /* & */
    int base = 8;
    if (*p == 'h' || *p == 'H') { base = 16; p++; }
    else if (*p == 'o' || *p == 'O') { base = 8; p++; }
    uint64_t v = 0;
    int any = 0;
    while (1) {
        int c = *p, d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * (uint64_t)base + (uint64_t)d;
        if (v > 0xFFFFFFFFull) v &= 0xFFFFFFFFull;
        any = 1;
        p++;
    }
    (void)any;
    int type;
    double d;
    if (*p == '&') {
        p++;
        type = T_LNG;
        d = (double)(int32_t)(uint32_t)v;
    } else if (*p == '%') {
        p++;
        type = T_INT;
        d = (double)(int16_t)(uint16_t)v;
    } else if (v <= 0xFFFF) {
        type = T_INT;
        d = (double)(int16_t)(uint16_t)v;
    } else {
        type = T_LNG;
        d = (double)(int32_t)(uint32_t)v;
    }
    t->k = TK_NUM;
    t->ntype = (uint8_t)type;
    t->num = d;
    return (int)(p - s);
}

void lex_line(const char *src, TokLine *tl) {
    int cap = 0;
    tl->t = NULL;
    tl->n = 0;
    tl->err = NULL;
    const char *p = src;
    int after_data = 0;
    while (*p) {
        Tok t;
        memset(&t, 0, sizeof t);
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') { p++; continue; }
        t.col = (int)(p - src);
        if (after_data) {
            /* raw DATA text up to ':' outside quotes */
            const char *s = p;
            int inq = 0;
            while (*p && (inq || *p != ':')) {
                if (*p == '"') inq = !inq;
                p++;
            }
            t.k = TK_DATA;
            const char *e = p;
            while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
            t.s = qb_strndup(s, (int)(e - s));
            t.slen = (int)(e - s);
            t.endcol = (int)(p - src);
            push(tl, &t, &cap);
            after_data = 0;
            continue;
        }
        if (*p == '\'') {
            t.k = TK_REM;
            t.s = qb_strdup(p + 1);
            t.slen = (int)strlen(p + 1);
            t.endcol = (int)strlen(src);
            push(tl, &t, &cap);
            break;
        }
        if (*p == '"') {
            const char *s = ++p;
            while (*p && *p != '"') p++;
            t.k = TK_STR;
            t.s = qb_strndup(s, (int)(p - s));
            t.slen = (int)(p - s);
            if (*p == '"') p++;
            t.endcol = (int)(p - src);
            push(tl, &t, &cap);
            continue;
        }
        if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1]))) {
            p += lex_number(p, &t);
            t.endcol = (int)(p - src);
            push(tl, &t, &cap);
            continue;
        }
        if (*p == '&' && (p[1] == 'h' || p[1] == 'H' || p[1] == 'o' || p[1] == 'O' || isdigit((unsigned char)p[1]))) {
            p += lex_hex(p, &t);
            t.endcol = (int)(p - src);
            push(tl, &t, &cap);
            continue;
        }
        if (isalpha((unsigned char)*p)) {
            const char *s = p;
            while (isidchar((unsigned char)*p)) p++;
            int wl = (int)(p - s);
            int hasdot = memchr(s, '.', (size_t)wl) != NULL;
            int kw = 0;
            if (!hasdot) {
                if (*p == '$') {
                    kw = kw_lookup(s, wl + 1 > 31 ? 0 : wl);
                    /* try with $ */
                    char tmp[40];
                    if (wl < 30) {
                        memcpy(tmp, s, (size_t)wl);
                        tmp[wl] = '$';
                        int k2 = kw_lookup(tmp, wl + 1);
                        if (k2) { kw = k2; p++; }
                        else if (kw) {
                            /* keyword followed by $ but no $-keyword: treat as identifier */
                            kw = 0;
                        }
                    }
                } else {
                    kw = kw_lookup(s, wl);
                }
            }
            if (kw == KW_REM) {
                t.k = TK_REM;
                const char *r = p;
                if (*r == ' ') r++;
                t.s = qb_strdup(r);
                t.slen = (int)strlen(r);
                t.kw = 1; /* marks REM keyword form */
                t.endcol = (int)strlen(src);
                push(tl, &t, &cap);
                break;
            }
            if (kw) {
                t.k = TK_KW;
                t.kw = (int16_t)kw;
                t.s = qb_strndup(s, (int)(p - s));
                t.slen = (int)(p - s);
                t.endcol = (int)(p - src);
                push(tl, &t, &cap);
                if (kw == KW_DATA) after_data = 1;
                continue;
            }
            /* identifier with optional suffix */
            if (*p == '%' || *p == '&' || *p == '!' || *p == '#' || *p == '$') {
                /* don't take & if it starts a hex literal, e.g. a&H10 is odd anyway */
                p++;
            }
            t.k = TK_ID;
            t.s = qb_strndup(s, (int)(p - s));
            t.slen = (int)(p - s);
            t.endcol = (int)(p - src);
            push(tl, &t, &cap);
            continue;
        }
        if (*p == '?') {
            t.k = TK_KW;
            t.kw = KW_PRINT;
            t.s = qb_strdup("PRINT");
            t.slen = 5;
            p++;
            t.endcol = (int)(p - src);
            push(tl, &t, &cap);
            continue;
        }
        /* operators */
        t.k = TK_OP;
        if (p[0] == '<' && p[1] == '=') { t.kw = OP_LE; p += 2; }
        else if (p[0] == '=' && p[1] == '<') { t.kw = OP_LE; p += 2; }
        else if (p[0] == '>' && p[1] == '=') { t.kw = OP_GE; p += 2; }
        else if (p[0] == '=' && p[1] == '>') { t.kw = OP_GE; p += 2; }
        else if (p[0] == '<' && p[1] == '>') { t.kw = OP_NE; p += 2; }
        else if (p[0] == '>' && p[1] == '<') { t.kw = OP_NE; p += 2; }
        else if (strchr("+-*/\\^=<>(),;:#.", *p)) { t.kw = (unsigned char)*p; p++; }
        else {
            /* unknown char */
            char m[64];
            snprintf(m, sizeof m, "Unexpected character '%c'", *p);
            if (!tl->err) tl->err = qb_strdup(m);
            t.kw = (unsigned char)*p;
            p++;
        }
        t.endcol = (int)(p - src);
        push(tl, &t, &cap);
    }
    Tok e;
    memset(&e, 0, sizeof e);
    e.k = TK_EOL;
    e.col = (int)strlen(src);
    e.endcol = e.col;
    push(tl, &e, &cap);
}

void lex_free(TokLine *tl) {
    for (int i = 0; i < tl->n; i++) free(tl->t[i].s);
    free(tl->t);
    free(tl->err);
    tl->t = NULL;
    tl->n = 0;
    tl->err = NULL;
}

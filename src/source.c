/* source.c - document model: module text split into main part + procedures */
#include "qb.h"
#include "source.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

void part_init(Part *p) { memset(p, 0, sizeof *p); }

void part_free(Part *p) {
    for (int i = 0; i < p->n; i++) free(p->lines[i]);
    free(p->lines);
    memset(p, 0, sizeof *p);
}

void part_insert(Part *p, int at, const char *text) {
    if (p->n >= p->cap) { p->cap = p->cap ? p->cap * 2 : 64; p->lines = (char **)qb_realloc(p->lines, sizeof(char *) * (size_t)p->cap); }
    if (at < 0) at = 0;
    if (at > p->n) at = p->n;
    memmove(&p->lines[at + 1], &p->lines[at], sizeof(char *) * (size_t)(p->n - at));
    p->lines[at] = qb_strdup(text);
    p->n++;
}

void part_delete(Part *p, int at) {
    if (at < 0 || at >= p->n) return;
    free(p->lines[at]);
    memmove(&p->lines[at], &p->lines[at + 1], sizeof(char *) * (size_t)(p->n - at - 1));
    p->n--;
}

void part_set(Part *p, int at, const char *text) {
    if (at < 0 || at >= p->n) return;
    free(p->lines[at]);
    p->lines[at] = qb_strdup(text);
}

void doc_init(Doc *d) { memset(d, 0, sizeof *d); d->nparts = 1; part_init(&d->parts[0]); }

void doc_free(Doc *d) {
    for (int i = 0; i < d->nparts; i++) part_free(&d->parts[i]);
    d->nparts = 0;
}

/* classify a line: returns 1 = SUB header, 2 = FUNCTION header, 3 = END SUB/FUNCTION,
   4 = comment/blank/DEFtype, 0 = other code; fills name for headers */
int classify_line(const char *line, char *name, int namesz) {
    TokLine tl;
    lex_line(line, &tl);
    int r = 0;
    int i = 0;
    Tok *t = &tl.t[0];
    if (t->k == TK_EOL || t->k == TK_REM) r = 4;
    else if (t->k == TK_KW && (t->kw == KW_DEFINT || t->kw == KW_DEFLNG || t->kw == KW_DEFSNG || t->kw == KW_DEFDBL || t->kw == KW_DEFSTR)) r = 4;
    else {
        if (t->k == TK_KW && t->kw == KW_STATIC && tl.n > 1 && tl.t[1].k == TK_KW && (tl.t[1].kw == KW_SUB || tl.t[1].kw == KW_FUNCTION)) i = 1;
        t = &tl.t[i];
        if (t->k == TK_KW && (t->kw == KW_SUB || t->kw == KW_FUNCTION) && tl.n > i + 1 && tl.t[i + 1].k == TK_ID) {
            r = t->kw == KW_SUB ? 1 : 2;
            if (name) snprintf(name, (size_t)namesz, "%s", tl.t[i + 1].s);
        } else if (t->k == TK_KW && t->kw == KW_END && tl.n > 1 && tl.t[1].k == TK_KW && (tl.t[1].kw == KW_SUB || tl.t[1].kw == KW_FUNCTION)) {
            r = 3;
        }
    }
    lex_free(&tl);
    return r;
}

static void strip_eol(char *s) {
    int n = (int)strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == 26)) s[--n] = 0;
}

/* split flat list of lines into parts */
void doc_from_lines(Doc *d, char **lines, int n) {
    doc_free(d);
    d->nparts = 1;
    part_init(&d->parts[0]);
    Part *main = &d->parts[0];
    int i = 0;
    int pending_start = -1;  /* start of comment block preceding a SUB (after an END SUB) */
    int after_proc = 0;
    while (i < n) {
        char name[64];
        int c = classify_line(lines[i], name, sizeof name);
        if (c == 1 || c == 2) {
            if (d->nparts >= MAX_PARTS) { part_insert(main, main->n, lines[i]); i++; continue; }
            Part *p = &d->parts[d->nparts++];
            part_init(p);
            snprintf(p->name, sizeof p->name, "%s", name);
            p->isfunc = c == 2;
            /* attach preceding comment/DEF lines that came after the previous procedure */
            if (after_proc && pending_start >= 0) {
                for (int k = pending_start; k < i; k++) part_insert(p, p->n, lines[k]);
            }
            pending_start = -1;
            int j = i;
            for (; j < n; j++) {
                part_insert(p, p->n, lines[j]);
                if (classify_line(lines[j], NULL, 0) == 3) break;
            }
            i = j + 1;
            after_proc = 1;
            pending_start = i;
            continue;
        }
        if (after_proc) {
            if (c == 4) { i++; continue; }   /* may belong to next proc */
            /* module code after procedures: move collected lines + this to main */
            for (int k = pending_start; k <= i; k++) part_insert(main, main->n, lines[k]);
            i++;
            pending_start = i;
            continue;
        }
        part_insert(main, main->n, lines[i]);
        i++;
    }
    /* trailing comment lines after last proc: keep in main (non-empty only) */
    if (after_proc && pending_start >= 0) {
        for (int k = pending_start; k < n; k++) {
            const char *s = lines[k];
            while (*s == ' ' || *s == '\t') s++;
            if (*s) part_insert(main, main->n, lines[k]);
        }
    }
    /* remove trailing blank lines of main */
    while (main->n > 0) {
        const char *s = main->lines[main->n - 1];
        while (*s == ' ' || *s == '\t') s++;
        if (*s) break;
        part_delete(main, main->n - 1);
    }
}

int doc_load(Doc *d, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char **lines = NULL;
    int n = 0, cap = 0;
    char buf[4096];
    int bl = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (c == '\n' || bl >= 4000) {
            buf[bl] = 0;
            strip_eol(buf);
            if (n >= cap) { cap = cap ? cap * 2 : 256; lines = (char **)qb_realloc(lines, sizeof(char *) * (size_t)cap); }
            /* expand tabs */
            char out[8192];
            int ol = 0;
            for (int k = 0; buf[k] && ol < 8000; k++) {
                if (buf[k] == '\t') { do out[ol++] = ' '; while (ol % 8); }
                else out[ol++] = buf[k];
            }
            out[ol] = 0;
            lines[n++] = qb_strdup(out);
            bl = 0;
            continue;
        }
        if (c == 26) continue;
        buf[bl++] = (char)c;
    }
    if (bl > 0) {
        buf[bl] = 0;
        strip_eol(buf);
        if (n >= cap) { cap = cap ? cap * 2 : 256; lines = (char **)qb_realloc(lines, sizeof(char *) * (size_t)cap); }
        lines[n++] = qb_strdup(buf);
    }
    fclose(f);
    /* UTF-8 files (written with modern editors) are converted to CP437 */
    int high = 0, valid = 1;
    for (int i = 0; i < n && valid; i++) {
        const unsigned char *p = (const unsigned char *)lines[i];
        while (*p) {
            if (*p < 0x80) { p++; continue; }
            high = 1;
            int len = (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : (*p & 0xF8) == 0xF0 ? 4 : 0;
            if (!len) { valid = 0; break; }
            for (int k = 1; k < len; k++) if ((p[k] & 0xC0) != 0x80) { valid = 0; break; }
            if (!valid) break;
            p += len;
        }
    }
    int is_utf8 = high && valid;
    if (is_utf8) {
        for (int i = 0; i < n; i++) {
            const unsigned char *p = (const unsigned char *)lines[i];
            char *o = (char *)qb_alloc(strlen(lines[i]) + 1);
            int k = 0;
            while (*p) {
                int cp;
                if (*p < 0x80) cp = *p++;
                else if ((*p & 0xE0) == 0xC0) { cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); p += 2; }
                else if ((*p & 0xF0) == 0xE0) { cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
                else { cp = '?'; p += 4; }
                if (cp == 0xFEFF) continue;   /* BOM */
                o[k++] = (char)unicode_to_cp437(cp);
            }
            o[k] = 0;
            free(lines[i]);
            lines[i] = o;
        }
    }
    /* detect QuickBASIC binary format */
    if (n > 0 && (unsigned char)lines[0][0] == 0xFC) {
        for (int i = 0; i < n; i++) free(lines[i]);
        free(lines);
        return -1;
    }
    doc_from_lines(d, lines, n);
    d->utf8 = is_utf8;
    for (int i = 0; i < n; i++) free(lines[i]);
    free(lines);
    return 1;
}

int cp437_to_unicode(int c);
static void put_line(FILE *f, const char *s, int utf8) {
    if (!utf8) { fprintf(f, "%s\r\n", s); return; }
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        int cp = *p < 0x80 ? *p : cp437_to_unicode(*p);
        if (cp < 0x80) fputc(cp, f);
        else if (cp < 0x800) { fputc(0xC0 | (cp >> 6), f); fputc(0x80 | (cp & 0x3F), f); }
        else { fputc(0xE0 | (cp >> 12), f); fputc(0x80 | ((cp >> 6) & 0x3F), f); fputc(0x80 | (cp & 0x3F), f); }
    }
    fputs("\r\n", f);
}

/* write file: main part, then procedures. Adds DECLARE lines for procs if missing */
int doc_save(Doc *d, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    /* collect existing DECLAREs in main */
    Part *m = &d->parts[0];
    int insert_at = 0;
    char **decls = NULL;
    int nd = 0;
    for (int pi = 1; pi < d->nparts; pi++) {
        Part *p = &d->parts[pi];
        int found = 0;
        for (int i = 0; i < m->n; i++) {
            TokLine tl;
            lex_line(m->lines[i], &tl);
            if (tl.n > 2 && tl.t[0].k == TK_KW && tl.t[0].kw == KW_DECLARE && tl.t[2].k == TK_ID) {
                char b1[64], b2[64];
                snprintf(b1, sizeof b1, "%s", tl.t[2].s);
                snprintf(b2, sizeof b2, "%s", p->name);
                /* compare without suffix */
                int l1 = (int)strlen(b1), l2 = (int)strlen(b2);
                if (l1 && strchr("%&!#$", b1[l1 - 1])) b1[--l1] = 0;
                if (l2 && strchr("%&!#$", b2[l2 - 1])) b2[--l2] = 0;
                if (str_ieq(b1, b2)) found = 1;
                if (i + 1 > insert_at) insert_at = i + 1;
            }
            lex_free(&tl);
            if (found) break;
        }
        if (!found) {
            /* build DECLARE from header line */
            for (int i = 0; i < p->n; i++) {
                int c = classify_line(p->lines[i], NULL, 0);
                if (c == 1 || c == 2) {
                    const char *h = p->lines[i];
                    while (*h == ' ') h++;
                    char tmp[1024];
                    /* drop STATIC keyword at the end */
                    snprintf(tmp, sizeof tmp, "DECLARE %s", h);
                    int tl = (int)strlen(tmp);
                    while (tl > 0 && tmp[tl - 1] == ' ') tmp[--tl] = 0;
                    if (tl > 7 && str_ieq(tmp + tl - 6, "STATIC")) { tmp[tl - 6] = 0; tl -= 6; while (tl > 0 && tmp[tl - 1] == ' ') tmp[--tl] = 0; }
                    /* remove comments */
                    char *q = strchr(tmp, '\'');
                    if (q) { *q = 0; tl = (int)strlen(tmp); while (tl > 0 && tmp[tl - 1] == ' ') tmp[--tl] = 0; }
                    decls = (char **)qb_realloc(decls, sizeof(char *) * (size_t)(nd + 1));
                    decls[nd++] = qb_strdup(tmp);
                    break;
                }
            }
        }
    }
    for (int i = 0; i < m->n; i++) {
        if (i == insert_at) for (int k = 0; k < nd; k++) put_line(f, decls[k], d->utf8);
        put_line(f, m->lines[i], d->utf8);
    }
    if (insert_at >= m->n) for (int k = 0; k < nd; k++) put_line(f, decls[k], d->utf8);
    for (int pi = 1; pi < d->nparts; pi++) {
        Part *p = &d->parts[pi];
        fprintf(f, "\r\n");
        for (int i = 0; i < p->n; i++) put_line(f, p->lines[i], d->utf8);
    }
    for (int k = 0; k < nd; k++) free(decls[k]);
    free(decls);
    fclose(f);
    return 1;
}

/* build compiler input */
SrcPart *doc_srcparts(Doc *d) {
    SrcPart *sp = (SrcPart *)qb_alloc(sizeof(SrcPart) * (size_t)d->nparts);
    for (int i = 0; i < d->nparts; i++) { sp[i].lines = d->parts[i].lines; sp[i].nlines = d->parts[i].n; }
    return sp;
}

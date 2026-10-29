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
            /* attach preceding comment/DEF lines that came after the previous procedure; blank lines
               are dropped, as in QBasic (otherwise the blank line written before each SUB on saving
               would pile up at the top of the SUB on every load) */
            if (after_proc && pending_start >= 0) {
                for (int k = pending_start; k < i; k++) {
                    const char *b = lines[k];
                    while (*b == ' ' || *b == '\t') b++;
                    if (*b) part_insert(p, p->n, lines[k]);
                }
            }
            if (!after_proc) {
                /* first procedure: the comment lines right above its header (no blank line in between)
                   are its own, as they come back from saving; move them out of the module */
                int k = main->n;
                while (k > 0) {
                    TokLine tl;
                    lex_line(main->lines[k - 1], &tl);
                    int is_comment = tl.n > 0 && tl.t[0].k == TK_REM;
                    lex_free(&tl);
                    if (!is_comment) break;
                    k--;
                }
                for (int m = k; m < main->n; m++) part_insert(p, p->n, main->lines[m]);
                while (main->n > k) part_delete(main, main->n - 1);
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
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return 0; }
    char *data = (char *)qb_alloc((size_t)sz + 1);
    long got = (long)fread(data, 1, (size_t)sz, f);
    fclose(f);
    int r = doc_load_mem(d, data, (int)got);
    free(data);
    return r;
}

/* split the text of a .BAS file into lines (tabs expanded, UTF-8 converted to CP437).
   Returns -1 for QuickBASIC binary format, else 1. */
static int text_to_lines(const char *data, int size, char ***out, int *outn, int *utf8) {
    int pos = 0;
    char **lines = NULL;
    int n = 0, cap = 0;
    char buf[4096];
    int bl = 0;
    int c;
    while (pos < size) {
        c = (unsigned char)data[pos++];
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
    *out = lines; *outn = n; *utf8 = is_utf8;
    return 1;
}

/* load a program from memory (the text of a .BAS file) */
int doc_load_mem(Doc *d, const char *data, int size) {
    char **lines = NULL;
    int n = 0, is_utf8 = 0;
    int r = text_to_lines(data, size, &lines, &n, &is_utf8);
    if (r < 0) return r;
    doc_from_lines(d, lines, n);
    d->utf8 = is_utf8;
    for (int i = 0; i < n; i++) free(lines[i]);
    free(lines);
    return 1;
}

/* ------------------------------------------------------------------ */
/* '$INCLUDE: the text of another file pasted where the directive is.  */
/* Accepted forms (comment at the start of a line):                   */
/*   '$INCLUDE "FILE.BI"    '$INCLUDE: 'FILE.BI'    REM $INCLUDE: ...  */
/* Procedures in an included file become procedures of the program.   */
/* ------------------------------------------------------------------ */
static char *inc_names[256];
static int n_inc = 0;
const char *include_name(int i) { return i >= 0 && i < n_inc ? inc_names[i] : ""; }

/* 1 = directive (file name in out), 2 = malformed directive, 0 = not a directive */
static int parse_include(const char *line, char *out, int outsz) {
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\'') p++;
    else if ((p[0] == 'R' || p[0] == 'r') && (p[1] == 'E' || p[1] == 'e') && (p[2] == 'M' || p[2] == 'm') && (p[3] == ' ' || p[3] == '\t')) p += 3;
    else return 0;
    while (*p == ' ' || *p == '\t') p++;
    static const char kw[] = "$INCLUDE";
    for (int i = 0; kw[i]; i++) if (toupper((unsigned char)p[i]) != kw[i]) return 0;
    p += 8;
    if (isalnum((unsigned char)*p) || *p == '_') return 0;   /* e.g. $INCLUDED */
    while (*p == ' ' || *p == '\t') p++;
    if (*p == ':') p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"' && *p != '\'') return 2;
    char q = *p++;
    int n = 0;
    while (*p && *p != q && n < outsz - 1) out[n++] = *p++;
    out[n] = 0;
    if (*p != q || n == 0) return 2;
    return 1;
}

typedef struct XP { SrcPart *v; int n, cap; int *lcap; } XP;

static int xp_new_part(XP *x, int docpart) {
    if (x->n == x->cap) {
        x->cap = x->cap ? x->cap * 2 : 16;
        x->v = (SrcPart *)qb_realloc(x->v, sizeof(SrcPart) * (size_t)x->cap);
        x->lcap = (int *)qb_realloc(x->lcap, sizeof(int) * (size_t)x->cap);
    }
    SrcPart *p = &x->v[x->n];
    memset(p, 0, sizeof *p);
    p->docpart = docpart;
    x->lcap[x->n] = 0;
    return x->n++;
}

static void xp_add(XP *x, int part, const char *text, int docline, int incfile, int incline) {
    SrcPart *p = &x->v[part];
    if (p->nlines == x->lcap[part]) {
        int c = x->lcap[part] = x->lcap[part] ? x->lcap[part] * 2 : 64;
        p->lines = (char **)qb_realloc(p->lines, sizeof(char *) * (size_t)c);
        p->docline = (int *)qb_realloc(p->docline, sizeof(int) * (size_t)c);
        p->incfile = (int *)qb_realloc(p->incfile, sizeof(int) * (size_t)c);
        p->incline = (int *)qb_realloc(p->incline, sizeof(int) * (size_t)c);
    }
    p->lines[p->nlines] = qb_strdup(text);
    p->docline[p->nlines] = docline;
    p->incfile[p->nlines] = incfile;
    p->incline[p->nlines] = incline;
    p->nlines++;
}

void file_native_name(const char *in, int len, char *out, int outsz, int for_read);

typedef struct XCtx {
    int docpart;      /* editor part the text belongs to */
    int fixline;      /* -1: lines are the document's own; else the directive's line */
    int incfile;      /* -1: the document; else index in inc_names */
    char dir[512];    /* directory of the file being expanded ("" = current) */
    int depth;
} XCtx;

static int expand_lines(XP *x, int target, char **lines, int n, int split, XCtx *cx, CompileError *err);

static int include_file(XP *x, int target, const char *name, int docline, XCtx *cx, CompileError *err) {
    if (cx->depth >= 16) {
        snprintf(err->msg, sizeof err->msg, "Too many nested $INCLUDE (%s)", name);
        return 0;
    }
    char joined[1100], path[1100];
    int absolute = name[0] == '/' || name[0] == '\\' || (name[0] && name[1] == ':');
    if (cx->dir[0] && !absolute) snprintf(joined, sizeof joined, "%s/%s", cx->dir, name);
    else snprintf(joined, sizeof joined, "%s", name);
    file_native_name(joined, (int)strlen(joined), path, sizeof path, 1);
    FILE *f = fopen(path, "rb");
    if (!f && cx->dir[0] && !absolute) {   /* also the current directory */
        file_native_name(name, (int)strlen(name), path, sizeof path, 1);
        f = fopen(path, "rb");
    }
    if (!f) { snprintf(err->msg, sizeof err->msg, "File not found: %s ($INCLUDE)", name); return 0; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = (char *)qb_alloc((size_t)(sz > 0 ? sz : 0) + 1);
    long got = sz > 0 ? (long)fread(data, 1, (size_t)sz, f) : 0;
    fclose(f);
    char **lines = NULL; int nl = 0, u8 = 0;
    int r = text_to_lines(data, (int)got, &lines, &nl, &u8);
    free(data);
    if (r < 0) { snprintf(err->msg, sizeof err->msg, "%s: QuickBASIC binary format ($INCLUDE)", name); return 0; }
    XCtx sub = *cx;
    sub.fixline = docline;
    if (n_inc < 256) { inc_names[n_inc] = qb_strdup(name); sub.incfile = n_inc++; }
    else sub.incfile = 255;
    snprintf(sub.dir, sizeof sub.dir, "%s", path);
    char *sl = strrchr(sub.dir, '/'), *bs = strrchr(sub.dir, '\\');
    if (bs && (!sl || bs > sl)) sl = bs;
    if (sl) *sl = 0; else sub.dir[0] = 0;
    sub.depth = cx->depth + 1;
    int ok = expand_lines(x, target, lines, nl, 1, &sub, err);
    for (int i = 0; i < nl; i++) free(lines[i]);
    free(lines);
    return ok;
}

static int expand_lines(XP *x, int target, char **lines, int n, int split, XCtx *cx, CompileError *err) {
    int cur = target;           /* where module-level lines go */
    int inproc = -1;            /* part of the procedure being collected (included files only) */
    for (int i = 0; i < n; i++) {
        int dl = cx->fixline >= 0 ? cx->fixline : i;
        int dest = inproc >= 0 ? inproc : cur;
        if (split && inproc < 0) {
            int c = classify_line(lines[i], NULL, 0);
            if (c == 1 || c == 2) { inproc = xp_new_part(x, cx->docpart); dest = inproc; }
        }
        char name[512];
        int d = parse_include(lines[i], name, sizeof name);
        if (d == 2) {
            err->part = cx->docpart; err->line = dl;
            snprintf(err->msg, sizeof err->msg, "Expected: $INCLUDE: 'file' or $INCLUDE \"file\"");
            return 0;
        }
        if (d == 1) {
            /* keep the directive as a comment that can't be expanded again */
            char done[1100];
            snprintf(done, sizeof done, "' (included: %s)", name);
            xp_add(x, dest, done, dl, cx->incfile, i);
            if (!include_file(x, dest, name, dl, cx, err)) {
                err->part = cx->docpart; err->line = dl;
                return 0;
            }
            continue;
        }
        xp_add(x, dest, lines[i], dl, cx->incfile, i);
        if (inproc >= 0 && classify_line(lines[i], NULL, 0) == 3) inproc = -1;
    }
    return 1;
}

/* the parts given to the compiler, with every '$INCLUDE expanded. basepath: the program file
   (for relative include names), or NULL. Returns NULL with err set if an include fails. */
SrcPart *doc_compile_parts(Doc *d, const char *basepath, int *nparts, CompileError *err) {
    for (int i = 0; i < n_inc; i++) free(inc_names[i]);
    n_inc = 0;
    memset(err, 0, sizeof *err);
    XP x; memset(&x, 0, sizeof x);
    XCtx cx; memset(&cx, 0, sizeof cx);
    if (basepath && basepath[0]) {
        snprintf(cx.dir, sizeof cx.dir, "%s", basepath);
        char *sl = strrchr(cx.dir, '/'), *bs = strrchr(cx.dir, '\\');
        if (bs && (!sl || bs > sl)) sl = bs;
        if (sl) *sl = 0; else cx.dir[0] = 0;
    }
    /* the document's own parts keep their indices; included procedures are added after them */
    for (int p = 0; p < d->nparts; p++) xp_new_part(&x, p);
    for (int p = 0; p < d->nparts; p++) {
        cx.docpart = p; cx.fixline = -1; cx.incfile = -1; cx.depth = 0;
        if (!expand_lines(&x, p, d->parts[p].lines, d->parts[p].n, 0, &cx, err)) {
            srcparts_free(x.v, x.n);
            free(x.lcap);
            return NULL;
        }
    }
    free(x.lcap);
    *nparts = x.n;
    return x.v;
}

void srcparts_free(SrcPart *sp, int n) {
    if (!sp) return;
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < sp[i].nlines; k++) free(sp[i].lines[k]);
        free(sp[i].lines); free(sp[i].docline); free(sp[i].incfile); free(sp[i].incline);
    }
    free(sp);
}

/* the expanded program as one text (for Make EXE: the executable doesn't need the files) */
char *srcparts_to_text(SrcPart *sp, int n, int *len) {
    size_t cap = 4096, l = 0;
    char *t = (char *)qb_alloc(cap);
    for (int i = 0; i < n; i++)
        for (int k = 0; k < sp[i].nlines; k++) {
            size_t sl = strlen(sp[i].lines[k]);
            while (l + sl + 3 > cap) { cap *= 2; t = (char *)qb_realloc(t, cap); }
            memcpy(t + l, sp[i].lines[k], sl); l += sl;
            t[l++] = '\r'; t[l++] = '\n';
        }
    t[l] = 0;
    *len = (int)l;
    return t;
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
    for (int i = 0; i < d->nparts; i++) { sp[i].lines = d->parts[i].lines; sp[i].nlines = d->parts[i].n; sp[i].docpart = i; }
    return sp;
}

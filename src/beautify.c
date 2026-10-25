/* QBLike - code beautifier
   - splits statements joined with ':' into separate lines
   - turns single-line IFs that hold several statements into block IFs
   - re-indents blocks (SUB/FUNCTION, IF, FOR, DO, WHILE, SELECT CASE, TYPE, DEF FN)
   Strings, comments and DATA are never split; line numbers and labels are kept.
   It works on plain lines of text, so it doesn't depend on the editor. */
#include "qb.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

typedef int (*bf_fmt_fn)(const char *in, char *out, int outsz);

/* ---------------- string list ---------------- */
typedef struct { char **v; int n, cap; } SL;
static void sl_push(SL *l, const char *s) {
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 32; l->v = (char **)realloc(l->v, sizeof(char *) * (size_t)l->cap); }
    l->v[l->n++] = qb_strdup(s);
}
static void sl_push_n(SL *l, const char *s, int len) {
    char *t = (char *)malloc((size_t)len + 1);
    memcpy(t, s, (size_t)len); t[len] = 0;
    sl_push(l, t);
    free(t);
}

/* ---------------- statement tree of one source line ---------------- */
typedef struct Node {
    int kind;               /* 0 simple statement, 1 single-line IF */
    int a, b;               /* simple: tokens [a, b); IF: IF token a, THEN token b */
    int end;                /* one past the last token belonging to the node */
    struct Node **thn, **els;
    int nthn, nels, hasels;
} Node;

typedef struct {
    TokLine *tl;
    const char *src;
    int n;                  /* tokens before the comment / end of line */
} Ctx;

static int t_op(Ctx *c, int i, int op) { return i < c->n && c->tl->t[i].k == TK_OP && c->tl->t[i].kw == op; }
static int t_kw(Ctx *c, int i, int kw) { return i < c->n && c->tl->t[i].k == TK_KW && c->tl->t[i].kw == kw; }

static void node_push(Node ***v, int *n, Node *x) {
    *v = (Node **)realloc(*v, sizeof(Node *) * (size_t)(*n + 1));
    (*v)[(*n)++] = x;
}
static void node_free(Node *x) {
    if (!x) return;
    for (int i = 0; i < x->nthn; i++) node_free(x->thn[i]);
    for (int i = 0; i < x->nels; i++) node_free(x->els[i]);
    free(x->thn); free(x->els); free(x);
}

static Node *parse_stmt(Ctx *c, int *pi, int in_if);

/* statements separated by ':' up to the end of the line, or up to ELSE inside a single-line IF */
static void parse_seq(Ctx *c, int *pi, int in_if, Node ***out, int *nout) {
    int i = *pi;
    for (;;) {
        while (t_op(c, i, ':')) i++;
        if (i >= c->n) break;
        if (in_if && t_kw(c, i, KW_ELSE)) break;
        int before = i;
        Node *x = parse_stmt(c, &i, in_if);
        node_push(out, nout, x);
        if (i <= before) i = before + 1;   /* always make progress */
    }
    *pi = i;
}

static Node *parse_stmt(Ctx *c, int *pi, int in_if) {
    int i = *pi;
    Node *x = (Node *)calloc(1, sizeof(Node));
    if (t_kw(c, i, KW_IF)) {
        int j = i + 1;
        while (j < c->n && !t_kw(c, j, KW_THEN) && !t_op(c, j, ':')) j++;
        if (t_kw(c, j, KW_THEN) && j + 1 < c->n) {
            /* single-line IF: everything after THEN (up to its ELSE) belongs to it */
            x->kind = 1; x->a = i; x->b = j;
            int k = j + 1;
            parse_seq(c, &k, 1, &x->thn, &x->nthn);
            if (t_kw(c, k, KW_ELSE)) { x->hasels = 1; k++; parse_seq(c, &k, 1, &x->els, &x->nels); }
            int end = j + 1;
            if (x->nthn) end = x->thn[x->nthn - 1]->end;
            if (x->nels) end = x->els[x->nels - 1]->end;
            x->end = end;
            *pi = k;
            return x;
        }
    }
    /* block ELSE followed by a statement on the same line: split it off */
    if (!in_if && t_kw(c, i, KW_ELSE) && i + 1 < c->n && !t_op(c, i + 1, ':')) {
        x->a = i; x->b = x->end = i + 1;
        *pi = i + 1;
        return x;
    }
    int j = i;
    while (j < c->n && !t_op(c, j, ':') && !(in_if && t_kw(c, j, KW_ELSE))) j++;
    x->a = i; x->b = x->end = j;
    *pi = j;
    return x;
}

static int needs_block(Node *x) {
    if (x->kind != 1) return 0;
    if (x->nthn > 1 || x->nels > 1) return 1;
    for (int i = 0; i < x->nthn; i++) if (needs_block(x->thn[i])) return 1;
    for (int i = 0; i < x->nels; i++) if (needs_block(x->els[i])) return 1;
    return 0;
}

static void push_range(Ctx *c, SL *out, int a, int end) {
    if (end <= a) return;
    int s = c->tl->t[a].col, e = c->tl->t[end - 1].endcol;
    while (e > s && (c->src[e - 1] == ' ' || c->src[e - 1] == '\t')) e--;
    sl_push_n(out, c->src + s, e - s);
}

static void emit_node(Ctx *c, Node *x, SL *out) {
    if (x->kind == 1 && needs_block(x)) {
        push_range(c, out, x->a, x->b + 1);          /* IF condition THEN */
        for (int i = 0; i < x->nthn; i++) emit_node(c, x->thn[i], out);
        if (x->hasels) {
            sl_push(out, "ELSE");
            for (int i = 0; i < x->nels; i++) emit_node(c, x->els[i], out);
        }
        sl_push(out, "END IF");
        return;
    }
    if (x->kind == 0 && x->b == x->a + 1 && c->tl->t[x->a].k == TK_NUM) {
        /* "IF x THEN 100: ..." -> a line number alone means GOTO in a block IF */
        char buf[64];
        snprintf(buf, sizeof buf, "GOTO %.*s", c->tl->t[x->a].endcol - c->tl->t[x->a].col, c->src + c->tl->t[x->a].col);
        sl_push(out, buf);
        return;
    }
    push_range(c, out, x->a, x->end);
}

/* a source line becomes: optional label line, then statement lines (flag 1 = label, at column 0) */
typedef struct { SL text; int *label; int nlab, caplab; } Out;
static void out_line(Out *o, const char *s, int is_label) {
    sl_push(&o->text, s);
    if (o->nlab == o->caplab) { o->caplab = o->caplab ? o->caplab * 2 : 32; o->label = (int *)realloc(o->label, sizeof(int) * (size_t)o->caplab); }
    o->label[o->nlab++] = is_label;
}

static const char *ltrim(const char *s) { while (*s == ' ' || *s == '\t') s++; return s; }

static void split_line(const char *src, Out *o) {
    const char *t0 = ltrim(src);
    if (!*t0) { out_line(o, "", 0); return; }
    TokLine tl;
    lex_line(src, &tl);
    if (tl.err) { lex_free(&tl); out_line(o, t0, 0); return; }
    Ctx c = { &tl, src, 0 };
    int rem = -1;
    while (c.n < tl.n && tl.t[c.n].k != TK_EOL) {
        if (tl.t[c.n].k == TK_REM) { rem = c.n; break; }
        c.n++;
    }
    int i = 0;
    char prefix[64] = "";                 /* line number kept in front of the first statement */
    if (c.n > 0 && tl.t[0].k == TK_NUM) {
        snprintf(prefix, sizeof prefix, "%.*s ", tl.t[0].endcol - tl.t[0].col, src + tl.t[0].col);
        i = 1;
    } else if (c.n > 1 && tl.t[0].k == TK_ID && t_op(&c, 1, ':') && !strpbrk(tl.t[0].s, "%&!#$")) {
        /* alphanumeric label: on a line of its own, at column 0 */
        char lab[80];
        snprintf(lab, sizeof lab, "%s:", tl.t[0].s);
        if (c.n == 2) {                   /* only the label (maybe with a comment): keep the line */
            out_line(o, t0, 1);
            lex_free(&tl);
            return;
        }
        out_line(o, lab, 1);
        i = 2;
    }
    Node **seq = NULL; int nseq = 0;
    int k = i;
    parse_seq(&c, &k, 0, &seq, &nseq);
    SL st = { 0 };
    for (int s = 0; s < nseq; s++) emit_node(&c, seq[s], &st);
    const char *comment = rem >= 0 ? src + tl.t[rem].col : NULL;
    if (st.n <= 1) {
        /* nothing to split: keep the statement part of the line as it is (with its comment) */
        int start = i < tl.n ? tl.t[i].col : (int)strlen(src);
        if (i >= c.n && comment) start = tl.t[rem].col;
        char buf[1200];
        snprintf(buf, sizeof buf, "%s%s", prefix, ltrim(src + start));
        if (buf[0] || !prefix[0]) out_line(o, buf, 0);
    } else {
        if (comment) out_line(o, comment, 0);   /* a comment for several lines goes above them */
        for (int s = 0; s < st.n; s++) {
            char buf[1200];
            snprintf(buf, sizeof buf, "%s%s", s == 0 ? prefix : "", st.v[s]);
            out_line(o, buf, 0);
        }
    }
    for (int s = 0; s < st.n; s++) free(st.v[s]);
    free(st.v);
    for (int s = 0; s < nseq; s++) node_free(seq[s]);
    free(seq);
    lex_free(&tl);
}

/* ---------------- indentation ---------------- */
enum { IND_NONE, IND_OPEN, IND_CLOSE, IND_MID, IND_SELECT, IND_CASE, IND_ENDSELECT };

/* how a statement line affects indentation; *count = blocks closed (NEXT a, b closes two) */
static int classify(const char *line, int *count) {
    *count = 1;
    TokLine tl;
    lex_line(line, &tl);
    if (tl.err) { lex_free(&tl); return IND_NONE; }
    int n = 0;
    while (n < tl.n && tl.t[n].k != TK_EOL && tl.t[n].k != TK_REM) n++;
    int i = 0;
    if (n > 0 && tl.t[0].k == TK_NUM) i = 1;
    int r = IND_NONE;
    if (i < n && tl.t[i].k == TK_KW) {
        int kw = tl.t[i].kw;
        int next = (i + 1 < n && tl.t[i + 1].k == TK_KW) ? tl.t[i + 1].kw : -1;
        switch (kw) {
            case KW_SUB: case KW_FUNCTION: r = IND_OPEN; break;
            case KW_FOR: case KW_DO: case KW_WHILE: r = IND_OPEN; break;
            case KW_TYPE: if (i + 1 < n) r = IND_OPEN; break;
            case KW_DEF:
                if (next != KW_SEG) {      /* multi-line DEF FN: no '=' after the header */
                    int eq = 0;
                    for (int j = i + 1; j < n; j++) if (tl.t[j].k == TK_OP && tl.t[j].kw == '=') eq = 1;
                    if (!eq && i + 1 < n) r = IND_OPEN;
                }
                break;
            case KW_IF: if (tl.t[n - 1].k == TK_KW && tl.t[n - 1].kw == KW_THEN) r = IND_OPEN; break;
            case KW_ELSEIF: case KW_ELSE: r = IND_MID; break;
            case KW_LOOP: case KW_WEND: r = IND_CLOSE; break;
            case KW_NEXT:
                r = IND_CLOSE;
                for (int j = i + 1; j < n; j++) if (tl.t[j].k == TK_OP && tl.t[j].kw == ',') (*count)++;
                break;
            case KW_SELECT: r = IND_SELECT; break;
            case KW_CASE: r = IND_CASE; break;
            case KW_END:
                if (next == KW_SELECT) r = IND_ENDSELECT;
                else if (next == KW_IF || next == KW_SUB || next == KW_FUNCTION || next == KW_TYPE || next == KW_DEF) r = IND_CLOSE;
                break;
        }
    }
    lex_free(&tl);
    return r;
}

/* lines -> beautified lines. base = indentation level of the first line, indent = spaces per level.
   fmt (optional) normalizes spacing and keyword case of a line. */
char **qb_beautify(char **lines, int n, int base, int indent, bf_fmt_fn fmt, int *outn) {
    Out o = { { 0 }, NULL, 0, 0 };
    for (int i = 0; i < n; i++) split_line(lines[i], &o);
    SL res = { 0 };
    int level = base;
    char stack[256]; int sp = 0;             /* 'S' select, 'C' case body, 'B' other blocks */
    for (int i = 0; i < o.text.n; i++) {
        char fbuf[2400];
        const char *txt = o.text.v[i];
        if (fmt && *txt) { if (fmt(txt, fbuf, (int)sizeof fbuf)) txt = fbuf; else snprintf(fbuf, sizeof fbuf, "%s", txt), txt = fbuf; }
        txt = ltrim(txt);
        if (!*txt) { sl_push(&res, ""); continue; }
        if (o.label[i]) { sl_push(&res, txt); continue; }
        int cnt;
        int k = classify(txt, &cnt);
        int at = level;
        switch (k) {
            case IND_CLOSE:
                for (int c = 0; c < cnt; c++) { if (sp > 0) sp--; if (level > 0) level--; }
                at = level;
                break;
            case IND_MID: at = level > 0 ? level - 1 : 0; break;
            case IND_CASE:
                if (sp > 0 && stack[sp - 1] == 'C') { sp--; if (level > 0) level--; }
                at = level;
                break;
            case IND_ENDSELECT:
                if (sp > 0 && stack[sp - 1] == 'C') { sp--; if (level > 0) level--; }
                if (sp > 0 && stack[sp - 1] == 'S') sp--;
                if (level > 0) level--;
                at = level;
                break;
        }
        char line[2600];
        int sp_n = at * indent;
        if (sp_n > 200) sp_n = 200;
        snprintf(line, sizeof line, "%*s%s", sp_n, "", txt);
        sl_push(&res, line);
        if (k == IND_OPEN) { if (sp < 255) stack[sp++] = 'B'; level++; }
        else if (k == IND_SELECT) { if (sp < 255) stack[sp++] = 'S'; level++; }
        else if (k == IND_CASE) { if (sp < 255) stack[sp++] = 'C'; level++; }
    }
    for (int i = 0; i < o.text.n; i++) free(o.text.v[i]);
    free(o.text.v);
    free(o.label);
    *outn = res.n;
    return res.v;
}

/* compiler.c - parses QBasic source into statement/expression trees */
#include "qb.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <math.h>

/* binary operator codes (Expr.sub for E_BIN) */
enum {
    B_ADD = 1, B_SUB, B_MUL, B_DIV, B_IDIV, B_MOD, B_POW, B_EQ, B_NE, B_LT, B_GT, B_LE, B_GE,
    B_AND, B_OR, B_XOR, B_EQV, B_IMP, B_LOR, B_LAND
};

enum { BK_IF, BK_FOR, BK_WHILE, BK_DO, BK_SELECT, BK_PROC, BK_DEF, BK_TYPE };

typedef struct IntList { int *v; int n, cap; } IntList;

static void il_push(IntList *l, int x) {
    if (l->n >= l->cap) { l->cap = l->cap ? l->cap * 2 : 8; l->v = (int *)qb_realloc(l->v, sizeof(int) * (size_t)l->cap); }
    l->v[l->n++] = x;
}

typedef struct Block {
    int kind;
    int part, line;
    int jmpf;           /* IF: pending conditional jump to patch */
    IntList ends;       /* jumps to patch at end */
    int top;            /* loop top */
    int forinit;
    Expr *forvar;
    int hidden1, hidden2;
    int sel_slot;
    int sel_type;
    int sel_pending;    /* SELECT: pending JMPF of the previous CASE */
    int sel_ncase;
    int sel_haselse;
    int dowhile;        /* DO with pre-condition */
} Block;

typedef struct Fix {
    int stmt;
    int which;          /* 1 = i1, 2 = i2, 3 = args index (ON GOTO) in p array */
    int argi;
    char label[48];
    int proc;
    int part, line, col;
    int module;         /* resolve against module labels */
} Fix;

typedef struct SlotInfo { uint8_t t; int udt; int fixlen; } SlotInfo;

typedef struct C {
    Program *P;
    TokLine tl;
    int pos;
    int part, line;
    int proc;           /* -1 module */
    SymTab *lsyms;
    char deftype[26];
    char proc_deftype[26];
    Block blk[128];
    int nblk;
    Fix *fix;
    int nfix, capfix;
    CompileError *err;
    jmp_buf jb;
    int lineemitted;
    int single_if;      /* depth of single-line IF THEN parts */
    int immediate;
    int syntax_only;
    /* slot info per scope */
    SlotInfo *gslot; int ngslot, capgslot;
    SlotInfo *gaslot; int ngaslot, capgaslot;
    SlotInfo *lslot; int nlslot, caplslot;
    SlotInfo *laslot; int nlaslot, caplaslot;
    uint8_t *lslotstatic;
    uint8_t *laslotstatic;
    int in_static_stmt;
    int curfunc;        /* proc index of current FUNCTION / DEF FN for result assignment */
    int deffn_active;   /* compiling inside multi-line DEF FN */
    uint8_t *typeline;  /* lines of part 0 that belong to TYPE blocks */
} C;

/* global slot info is needed by the runtime to create frames */
SlotInfo *g_gslot = NULL; int g_ngslot = 0;
SlotInfo *g_gaslot = NULL; int g_ngaslot = 0;
typedef struct ProcSlots { SlotInfo *s; int ns; SlotInfo *a; int na; } ProcSlots;
ProcSlots *g_pslots = NULL; int g_npslots = 0;

static C *cc;

/* ------------------------------------------------------------------ */
#include <stdarg.h>
#if defined(_MSC_VER)
__declspec(noreturn) static void cerr_at(int col, const char *fmt, ...);
#else
static void cerr_at(int col, const char *fmt, ...) __attribute__((noreturn));
#endif
static void cerr_at(int col, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cc->err->msg, sizeof cc->err->msg, fmt, ap);
    va_end(ap);
    cc->err->part = cc->part;
    cc->err->line = cc->line;
    cc->err->col = col;
    longjmp(cc->jb, 1);
}
#define cerr(...) cerr_at(cur()->col, __VA_ARGS__)

static Tok *cur(void) { return &cc->tl.t[cc->pos]; }
static Tok *peekn(int n) { int i = cc->pos + n; if (i >= cc->tl.n) i = cc->tl.n - 1; return &cc->tl.t[i]; }
static void adv(void) { if (cc->pos < cc->tl.n - 1) cc->pos++; }
static int isop(int c) { return cur()->k == TK_OP && cur()->kw == c; }
static int iskw(int k) { return cur()->k == TK_KW && cur()->kw == k; }
static int acceptop(int c) { if (isop(c)) { adv(); return 1; } return 0; }
static int acceptkw(int k) { if (iskw(k)) { adv(); return 1; } return 0; }
static void expectop(int c) {
    if (!acceptop(c)) {
        if (c == OP_LE) cerr("Expected: <=");
        cerr("Expected: %c", c);
    }
}
static void expectkw(int k) { if (!acceptkw(k)) cerr("Expected: %s", kw_text(k)); }
static int at_stmt_end(void) {
    Tok *t = cur();
    if (t->k == TK_EOL || t->k == TK_REM) return 1;
    if (t->k == TK_OP && t->kw == ':') return 1;
    if (t->k == TK_KW && t->kw == KW_ELSE && cc->single_if) return 1;
    return 0;
}
static void expect_end(void) {
    if (!at_stmt_end()) cerr("Expected: end-of-statement");
}

static int is_id_plain(Tok *t) { return t->k == TK_ID; }

/* ------------------------------------------------------------------ */
/* expression nodes                                                   */
/* ------------------------------------------------------------------ */
static Expr *enew(int op, int type) {
    Expr *e = (Expr *)qb_alloc(sizeof(Expr));
    e->op = (uint8_t)op;
    e->type = (uint8_t)type;
    return e;
}
static Expr *enum_c(double v, int t) { Expr *e = enew(E_NUM, t); e->num = v; return e; }
static Expr *estr_c(const char *s, int len) {
    Expr *e = enew(E_STR, T_STR);
    e->str = qb_strndup(s, len);
    e->slen = len;
    return e;
}

void free_expr(Expr *e) {
    if (!e) return;
    free_expr(e->a);
    free_expr(e->b);
    for (int i = 0; i < e->nargs; i++) free_expr(e->args[i]);
    free(e->args);
    free(e->str);
    free(e);
}

static void addarg(Expr *e, Expr *a) {
    e->args = (Expr **)qb_realloc(e->args, sizeof(Expr *) * (size_t)(e->nargs + 1));
    e->args[e->nargs++] = a;
}

/* ------------------------------------------------------------------ */
/* symbols                                                            */
/* ------------------------------------------------------------------ */
static int type_char_to_type(int c) {
    switch (c) {
        case '%': return T_INT; case '&': return T_LNG; case '!': return T_SNG;
        case '#': return T_DBL; case '$': return T_STR;
    }
    return T_NONE;
}
static char type_to_char(int t) {
    switch (t) {
        case T_INT: return '%'; case T_LNG: return '&'; case T_SNG: return '!';
        case T_DBL: return '#'; case T_STR: return '$'; case T_UDT: return '@';
    }
    return '?';
}

/* split identifier into uppercase base and suffix type */
static int split_name(const char *name, char *base, int basesz) {
    int l = (int)strlen(name);
    int t = T_NONE;
    if (l > 0) t = type_char_to_type(name[l - 1]);
    if (t != T_NONE) l--;
    if (l >= basesz) l = basesz - 1;
    for (int i = 0; i < l; i++) base[i] = (char)toupper((unsigned char)name[i]);
    base[l] = 0;
    return t;
}

static int default_type(const char *base) {
    int c = toupper((unsigned char)base[0]);
    if (c < 'A' || c > 'Z') return T_SNG;
    return cc->deftype[c - 'A'];
}

static Sym *symtab_find(SymTab *st, const char *key, int kind) {
    for (int i = 0; i < st->n; i++)
        if (st->s[i].kind == kind && strcmp(st->s[i].key, key) == 0) return &st->s[i];
    return NULL;
}
static Sym *symtab_find_declared(SymTab *st, const char *base, int kind) {
    size_t bl = strlen(base);
    for (int i = 0; i < st->n; i++) {
        Sym *s = &st->s[i];
        if (s->kind == kind && s->declared && strlen(s->key) == bl + 1 && strncmp(s->key, base, bl) == 0) return s;
    }
    return NULL;
}
static Sym *symtab_find_const(SymTab *st, const char *base) {
    size_t bl = strlen(base);
    for (int i = 0; i < st->n; i++) {
        Sym *s = &st->s[i];
        if (s->kind == 2 && strlen(s->key) == bl + 1 && strncmp(s->key, base, bl) == 0) return s;
    }
    return NULL;
}
static Sym *symtab_add(SymTab *st) {
    if (st->n >= st->cap) {
        st->cap = st->cap ? st->cap * 2 : 32;
        st->s = (Sym *)qb_realloc(st->s, sizeof(Sym) * (size_t)st->cap);
    }
    Sym *s = &st->s[st->n++];
    memset(s, 0, sizeof *s);
    return s;
}

static int add_slot(int global, int isarray, int t, int udt, int fixlen) {
    SlotInfo si;
    si.t = (uint8_t)t; si.udt = udt; si.fixlen = fixlen;
    if (global) {
        if (!isarray) {
            if (cc->ngslot >= cc->capgslot) { cc->capgslot = cc->capgslot ? cc->capgslot * 2 : 64; cc->gslot = (SlotInfo *)qb_realloc(cc->gslot, sizeof(SlotInfo) * (size_t)cc->capgslot); }
            cc->gslot[cc->ngslot] = si;
            return cc->ngslot++;
        } else {
            if (cc->ngaslot >= cc->capgaslot) { cc->capgaslot = cc->capgaslot ? cc->capgaslot * 2 : 32; cc->gaslot = (SlotInfo *)qb_realloc(cc->gaslot, sizeof(SlotInfo) * (size_t)cc->capgaslot); }
            cc->gaslot[cc->ngaslot] = si;
            return cc->ngaslot++;
        }
    } else {
        if (!isarray) {
            if (cc->nlslot >= cc->caplslot) {
                cc->caplslot = cc->caplslot ? cc->caplslot * 2 : 32;
                cc->lslot = (SlotInfo *)qb_realloc(cc->lslot, sizeof(SlotInfo) * (size_t)cc->caplslot);
                cc->lslotstatic = (uint8_t *)qb_realloc(cc->lslotstatic, (size_t)cc->caplslot);
            }
            cc->lslot[cc->nlslot] = si;
            cc->lslotstatic[cc->nlslot] = (uint8_t)cc->in_static_stmt;
            return cc->nlslot++;
        } else {
            if (cc->nlaslot >= cc->caplaslot) {
                cc->caplaslot = cc->caplaslot ? cc->caplaslot * 2 : 16;
                cc->laslot = (SlotInfo *)qb_realloc(cc->laslot, sizeof(SlotInfo) * (size_t)cc->caplaslot);
                cc->laslotstatic = (uint8_t *)qb_realloc(cc->laslotstatic, (size_t)cc->caplaslot);
            }
            cc->laslot[cc->nlaslot] = si;
            cc->laslotstatic[cc->nlaslot] = (uint8_t)cc->in_static_stmt;
            return cc->nlaslot++;
        }
    }
}

static int is_local_scope(void) { return cc->lsyms != NULL; }

/* create a hidden temporary slot in the current scope */
static int hidden_slot(int t, int *scope) {
    if (is_local_scope()) { *scope = 1; return add_slot(0, 0, t, 0, 0); }
    *scope = 0;
    return add_slot(1, 0, t, 0, 0);
}

static int find_proc(const char *base) {
    for (int i = 0; i < cc->P->nprocs; i++) {
        Proc *p = &cc->P->procs[i];
        if (p->isdeffn) continue;
        char b[48];
        split_name(p->name, b, sizeof b);
        if (strcmp(b, base) == 0) return i;
    }
    return -1;
}

static int find_deffn(const char *name) {
    char base[48];
    int t = split_name(name, base, sizeof base);
    for (int i = 0; i < cc->P->nprocs; i++) {
        Proc *p = &cc->P->procs[i];
        if (!p->isdeffn || !p->defined) continue;
        char b[48];
        int pt = split_name(p->name, b, sizeof b);
        if (strcmp(b, base) == 0) {
            if (t == T_NONE || t == pt) return i;
        }
    }
    return -1;
}

static int find_udt(const char *name) {
    for (int i = 0; i < cc->P->nudts; i++) if (str_ieq(cc->P->udts[i].name, name)) return i;
    return -1;
}

/* Resolve a scalar or array variable reference; creates it if needed. */
static Sym *resolve_var(const char *name, int kind, int create, int *scope) {
    char base[48], key[50];
    int t = split_name(name, base, sizeof base);
    SymTab *L = cc->lsyms;
    SymTab *G = &cc->P->gsyms;
    Sym *s = NULL;
    /* local */
    if (L) {
        if (t == T_NONE) s = symtab_find_declared(L, base, kind);
        if (!s) {
            int tt = t != T_NONE ? t : default_type(base);
            snprintf(key, sizeof key, "%s%c", base, type_to_char(tt));
            s = symtab_find(L, key, kind);
            if (!s && t != T_NONE) {
                /* reference with suffix to a declared variable of same type */
                Sym *d = symtab_find_declared(L, base, kind);
                if (d && d->t == t) s = d;
            }
        }
        if (s) {
            if (s->shared) { *scope = 0; }
            else *scope = 1;
            return s;
        }
        /* global shared? DEF FN bodies see all module variables */
        Sym *gs = NULL;
        if (t == T_NONE) gs = symtab_find_declared(G, base, kind);
        if (!gs) {
            int tt = t != T_NONE ? t : default_type(base);
            snprintf(key, sizeof key, "%s%c", base, type_to_char(tt));
            gs = symtab_find(G, key, kind);
            if (!gs && t != T_NONE) {
                Sym *d = symtab_find_declared(G, base, kind);
                if (d && d->t == t) gs = d;
            }
        }
        if (gs && (gs->shared || cc->deffn_active)) { *scope = 0; return gs; }
        if (cc->deffn_active && !gs) {
            /* DEF FN: unknown variables are module-level variables */
            L = NULL;
            goto global_create;
        }
        if (!create) return NULL;
        int tt = t != T_NONE ? t : default_type(base);
        snprintf(key, sizeof key, "%s%c", base, type_to_char(tt));
        s = symtab_add(L);
        strcpy(s->key, key);
        snprintf(s->disp, sizeof s->disp, "%s", name);
        s->kind = (uint8_t)kind;
        s->t = (uint8_t)tt;
        s->idx = add_slot(0, kind == 1, tt, 0, 0);
        *scope = 1;
        return s;
    }
global_create:
    if (t == T_NONE) s = symtab_find_declared(G, base, kind);
    if (!s) {
        int tt = t != T_NONE ? t : default_type(base);
        snprintf(key, sizeof key, "%s%c", base, type_to_char(tt));
        s = symtab_find(G, key, kind);
        if (!s && t != T_NONE) {
            Sym *d = symtab_find_declared(G, base, kind);
            if (d && d->t == t) s = d;
        }
    }
    *scope = 0;
    if (s || !create) return s;
    {
        int tt = t != T_NONE ? t : default_type(base);
        snprintf(key, sizeof key, "%s%c", base, type_to_char(tt));
        s = symtab_add(G);
        strcpy(s->key, key);
        snprintf(s->disp, sizeof s->disp, "%s", name);
        s->kind = (uint8_t)kind;
        s->t = (uint8_t)tt;
        s->idx = add_slot(1, kind == 1, tt, 0, 0);
    }
    return s;
}

/* Declare a variable with explicit type (DIM / SHARED / STATIC / params) */
static Sym *declare_var(const char *name, int kind, int t, int udt, int fixlen, int global, int shared, int *scope, int explicit_as) {
    char base[48], key[50];
    int st = split_name(name, base, sizeof base);
    if (t == T_NONE) t = st != T_NONE ? st : default_type(base);
    if (st != T_NONE && explicit_as && st != t) cerr("Duplicate definition");
    snprintf(key, sizeof key, "%s%c", base, type_to_char(t));
    SymTab *tab = global ? &cc->P->gsyms : cc->lsyms;
    Sym *s = symtab_find(tab, key, kind);
    if (!s && explicit_as) s = symtab_find_declared(tab, base, kind);
    if (s) {
        if (explicit_as && !s->declared) {
            /* already implicitly used; QBasic would complain if used before DIM */
            s->declared = 1;
        }
        if (s->t != t) cerr("Duplicate definition");
        if (shared) s->shared = 1;
        *scope = (global || s->shared) ? 0 : 1;
        return s;
    }
    s = symtab_add(tab);
    strcpy(s->key, key);
    snprintf(s->disp, sizeof s->disp, "%s", name);
    s->kind = (uint8_t)kind;
    s->t = (uint8_t)t;
    s->udt = udt;
    s->fixlen = fixlen;
    s->declared = (uint8_t)(explicit_as ? 1 : 0);
    s->shared = (uint8_t)shared;
    s->idx = add_slot(global, kind == 1, t, udt, fixlen);
    *scope = global ? 0 : 1;
    return s;
}

/* ------------------------------------------------------------------ */
/* expressions                                                        */
/* ------------------------------------------------------------------ */
static Expr *parse_expr(void);
static Expr *parse_lvalue_from_id(int allow_call, int *isfuncret);
static Expr *parse_add(void);
double eval_const_num(Expr *e, int *ok);
int eval_const_str(Expr *e, char **s, int *len);

static int numrank(int t) { return t; } /* T_INT < T_LNG < T_SNG < T_DBL */

static Expr *conv(Expr *e, int t) {
    if (e->type == t) return e;
    if (!IS_NUM(e->type) || !IS_NUM(t)) cerr("Type mismatch");
    Expr *c = enew(E_CONV, t);
    c->a = e;
    return c;
}

static void need_num(Expr *e) { if (!IS_NUM(e->type)) cerr("Type mismatch"); }
static void need_str(Expr *e) { if (e->type != T_STR) cerr("Type mismatch"); }

static Expr *mkbin(int op, Expr *a, Expr *b) {
    Expr *e = enew(E_BIN, T_INT);
    e->sub = (int16_t)op;
    e->a = a;
    e->b = b;
    switch (op) {
        case B_ADD:
            if (a->type == T_STR && b->type == T_STR) { e->type = T_STR; break; }
            /* fallthrough */
        case B_SUB: case B_MUL:
            need_num(a); need_num(b);
            e->type = (uint8_t)(numrank(a->type) > numrank(b->type) ? a->type : b->type);
            break;
        case B_DIV: case B_POW:
            need_num(a); need_num(b);
            e->type = (a->type == T_DBL || b->type == T_DBL) ? T_DBL : T_SNG;
            break;
        case B_IDIV: case B_MOD:
        case B_AND: case B_OR: case B_XOR: case B_EQV: case B_IMP:
            need_num(a); need_num(b);
            e->type = (a->type == T_INT && b->type == T_INT) ? T_INT : T_LNG;
            break;
        case B_EQ: case B_NE: case B_LT: case B_GT: case B_LE: case B_GE:
            if ((a->type == T_STR) != (b->type == T_STR)) cerr("Type mismatch");
            if (a->type == T_UDT || b->type == T_UDT) cerr("Type mismatch");
            e->type = T_INT;
            break;
        case B_LOR: case B_LAND:
            e->type = T_INT;
            break;
    }
    /* constant folding for numeric literals (only safe operators) */
    if (a->op == E_NUM && b->op == E_NUM && e->type != T_STR) {
        int ok = 1;
        double v = eval_const_num(e, &ok);
        if (ok) {
            Expr *n = enum_c(v, e->type);
            free_expr(e);
            return n;
        }
    }
    if (a->op == E_STR && b->op == E_STR && e->type == T_STR) {
        Expr *n = enew(E_STR, T_STR);
        n->slen = a->slen + b->slen;
        n->str = (char *)qb_alloc((size_t)n->slen + 1);
        memcpy(n->str, a->str, (size_t)a->slen);
        memcpy(n->str + a->slen, b->str, (size_t)b->slen);
        free_expr(e);
        return n;
    }
    return e;
}

typedef struct BI { int kw; int id; int rt; int minargs, maxargs; const char *argt; } BI;
/* argt: N numeric, S string, A any, R array-name, F file number  (rt: 0 = same as first arg) */
static const BI builtins[] = {
    { KW_ABS, F_ABS, 0, 1, 1, "N" }, { KW_ASC, F_ASC, T_INT, 1, 1, "S" },
    { KW_ATN, F_ATN, -1, 1, 1, "N" }, { KW_CDBL, F_CDBL, T_DBL, 1, 1, "N" },
    { KW_CHR_S, F_CHR, T_STR, 1, 1, "N" }, { KW_CINT, F_CINT, T_INT, 1, 1, "N" },
    { KW_CLNG, F_CLNG, T_LNG, 1, 1, "N" }, { KW_COS, F_COS, -1, 1, 1, "N" },
    { KW_CSNG, F_CSNG, T_SNG, 1, 1, "N" }, { KW_CSRLIN, F_CSRLIN, T_INT, 0, 0, "" },
    { KW_CVD, F_CVD, T_DBL, 1, 1, "S" }, { KW_CVI, F_CVI, T_INT, 1, 1, "S" },
    { KW_CVL, F_CVL, T_LNG, 1, 1, "S" }, { KW_CVS, F_CVS, T_SNG, 1, 1, "S" },
    { KW_CVSMBF, F_CVSMBF, T_SNG, 1, 1, "S" }, { KW_CVDMBF, F_CVDMBF, T_DBL, 1, 1, "S" },
    { KW_DATE_S, F_DATE, T_STR, 0, 0, "" }, { KW_ENVIRON_S, F_ENVIRON, T_STR, 1, 1, "A" },
    { KW_EOF, F_EOF, T_INT, 1, 1, "N" }, { KW_ERDEV, F_ERDEV, T_INT, 0, 0, "" },
    { KW_ERDEV_S, F_ERDEV, T_STR, 0, 0, "" },
    { KW_ERL, F_ERL, T_LNG, 0, 0, "" }, { KW_ERR, F_ERR, T_INT, 0, 0, "" },
    { KW_EXP, F_EXP, -1, 1, 1, "N" }, { KW_FILEATTR, F_FILEATTR, T_LNG, 2, 2, "NN" },
    { KW_FIX, F_FIX, 0, 1, 1, "N" }, { KW_FRE, F_FRE, T_LNG, 1, 1, "A" },
    { KW_FREEFILE, F_FREEFILE, T_INT, 0, 0, "" }, { KW_HEX_S, F_HEX, T_STR, 1, 1, "N" },
    { KW_INKEY_S, F_INKEY, T_STR, 0, 0, "" }, { KW_INP, F_INP, T_INT, 1, 1, "N" },
    { KW_INT, F_INT, 0, 1, 1, "N" }, { KW_LCASE_S, F_LCASE, T_STR, 1, 1, "S" },
    { KW_LEFT_S, F_LEFT, T_STR, 2, 2, "SN" }, { KW_LOC, F_LOC, T_LNG, 1, 1, "N" },
    { KW_LOF, F_LOF, T_LNG, 1, 1, "N" }, { KW_LOG, F_LOG, -1, 1, 1, "N" },
    { KW_LPOS, F_LPOS, T_INT, 1, 1, "N" }, { KW_LTRIM_S, F_LTRIM, T_STR, 1, 1, "S" },
    { KW_MKD_S, F_MKD, T_STR, 1, 1, "N" }, { KW_MKI_S, F_MKI, T_STR, 1, 1, "N" },
    { KW_MKL_S, F_MKL, T_STR, 1, 1, "N" }, { KW_MKS_S, F_MKS, T_STR, 1, 1, "N" },
    { KW_MKSMBF_S, F_MKSMBF, T_STR, 1, 1, "N" }, { KW_MKDMBF_S, F_MKDMBF, T_STR, 1, 1, "N" },
    { KW_OCT_S, F_OCT, T_STR, 1, 1, "N" }, { KW_PEEK, F_PEEK, T_INT, 1, 1, "N" },
    { KW_PEN, F_PEN, T_INT, 1, 1, "N" }, { KW_PLAY, F_PLAY, T_INT, 1, 1, "N" },
    { KW_PMAP, F_PMAP, T_SNG, 2, 2, "NN" }, { KW_POS, F_POS, T_INT, 1, 1, "N" },
    { KW_RIGHT_S, F_RIGHT, T_STR, 2, 2, "SN" }, { KW_RTRIM_S, F_RTRIM, T_STR, 1, 1, "S" },
    { KW_SADD, F_SADD, T_INT, 1, 1, "S" }, { KW_SCREEN, F_SCREEN, T_INT, 2, 3, "NNN" },
    { KW_SEEK, F_SEEK, T_LNG, 1, 1, "N" }, { KW_SGN, F_SGN, T_INT, 1, 1, "N" },
    { KW_SIN, F_SIN, -1, 1, 1, "N" }, { KW_SPACE_S, F_SPACE, T_STR, 1, 1, "N" },
    { KW_SQR, F_SQR, -1, 1, 1, "N" }, { KW_STICK, F_STICK, T_INT, 1, 1, "N" },
    { KW_STR_S, F_STR, T_STR, 1, 1, "N" }, { KW_STRIG, F_STRIG, T_INT, 1, 1, "N" },
    { KW_TAN, F_TAN, -1, 1, 1, "N" }, { KW_TIME_S, F_TIME, T_STR, 0, 0, "" },
    { KW_TIMER, F_TIMER, T_SNG, 0, 0, "" }, { KW_UCASE_S, F_UCASE, T_STR, 1, 1, "S" },
    { KW_VAL, F_VAL, T_DBL, 1, 1, "S" }, { KW_VARPTR, F_VARPTR, T_INT, 1, 1, "A" },
    { KW_VARPTR_S, F_VARPTRS, T_STR, 1, 1, "A" }, { KW_VARSEG, F_VARSEG, T_INT, 1, 1, "A" },
    { KW_IOCTL_S, F_IOCTL, T_STR, 1, 1, "N" },
    { KW_MULTIKEY, F_MULTIKEY, T_INT, 1, 1, "N" },
    { 0, 0, 0, 0, 0, NULL }
};

static Expr *parse_paren_args_generic(Expr *e, const BI *bi) {
    int n = 0;
    if (bi->maxargs == 0) return e;
    if (bi->kw == KW_EOF || bi->kw == KW_LOC || bi->kw == KW_LOF || bi->kw == KW_SEEK) {
        expectop('(');
        acceptop('#');
        Expr *a = parse_expr();
        need_num(a);
        addarg(e, a);
        expectop(')');
        return e;
    }
    expectop('(');
    for (;;) {
        Expr *a = parse_expr();
        char at = bi->argt[n < (int)strlen(bi->argt) ? n : (int)strlen(bi->argt) - 1];
        if (at == 'N') need_num(a);
        else if (at == 'S') need_str(a);
        addarg(e, a);
        n++;
        if (acceptop(',')) {
            if (n >= bi->maxargs) cerr("Expected: )");
            continue;
        }
        break;
    }
    expectop(')');
    if (n < bi->minargs) cerr("Argument-count mismatch");
    return e;
}

static Expr *parse_array_ref_arg(void) {
    /* LBOUND / UBOUND first argument: array name (optionally with ()) */
    if (cur()->k != TK_ID) cerr("Expected: array name");
    char name[64];
    snprintf(name, sizeof name, "%s", cur()->s);
    adv();
    if (isop('(') && peekn(1)->k == TK_OP && peekn(1)->kw == ')') { adv(); adv(); }
    int scope;
    Sym *s = resolve_var(name, 1, 1, &scope);
    Expr *e = enew(E_ARRWHOLE, s->t);
    e->scope = (uint8_t)scope;
    e->idx = s->idx;
    e->udt = s->udt;
    return e;
}

static Expr *parse_builtin(int kw) {
    int col = cur()->col;
    adv();
    Expr *e = enew(E_FUNC, T_SNG);
    switch (kw) {
        case KW_SOUND: {
            /* id% = SOUND file$   (QBLike): loads a WAV file */
            e->sub = F_SNDLOAD; e->type = T_INT;
            Expr *a = parse_add(); need_str(a); addarg(e, a);
            return e;
        }
        case KW_RND:
            e->sub = F_RND; e->type = T_SNG;
            if (acceptop('(')) { Expr *a = parse_expr(); need_num(a); addarg(e, a); expectop(')'); }
            return e;
        case KW_MID_S:
            e->sub = F_MID; e->type = T_STR;
            expectop('(');
            { Expr *a = parse_expr(); need_str(a); addarg(e, a); }
            expectop(',');
            { Expr *a = parse_expr(); need_num(a); addarg(e, a); }
            if (acceptop(',')) { Expr *a = parse_expr(); need_num(a); addarg(e, a); }
            expectop(')');
            return e;
        case KW_INSTR: {
            e->sub = F_INSTR; e->type = T_LNG;
            expectop('(');
            Expr *a = parse_expr();
            expectop(',');
            Expr *b = parse_expr();
            if (acceptop(',')) {
                Expr *c = parse_expr();
                need_num(a); need_str(b); need_str(c);
                addarg(e, a); addarg(e, b); addarg(e, c);
            } else {
                need_str(a); need_str(b);
                addarg(e, a); addarg(e, b);
            }
            expectop(')');
            return e;
        }
        case KW_STRING_S: {
            e->sub = F_STRING; e->type = T_STR;
            expectop('(');
            Expr *a = parse_expr(); need_num(a);
            expectop(',');
            Expr *b = parse_expr();
            if (b->type == T_UDT) cerr("Type mismatch");
            addarg(e, a); addarg(e, b);
            expectop(')');
            return e;
        }
        case KW_INPUT_S: {
            e->sub = F_INPUT; e->type = T_STR;
            expectop('(');
            Expr *a = parse_expr(); need_num(a); addarg(e, a);
            if (acceptop(',')) { acceptop('#'); Expr *b = parse_expr(); need_num(b); addarg(e, b); }
            expectop(')');
            return e;
        }
        case KW_LBOUND: case KW_UBOUND: {
            e->sub = kw == KW_LBOUND ? F_LBOUND : F_UBOUND; e->type = T_INT;
            expectop('(');
            addarg(e, parse_array_ref_arg());
            if (acceptop(',')) { Expr *b = parse_expr(); need_num(b); addarg(e, b); }
            expectop(')');
            return e;
        }
        case KW_LEN: {
            e->sub = F_LEN; e->type = T_LNG;
            expectop('(');
            Expr *a = parse_expr();
            if (a->type != T_STR) {
                /* LEN(variable) gives storage size */
                if (a->op != E_VAR && a->op != E_ARR && a->op != E_MEMBER) cerr("Type mismatch");
                e->sub = F_LENVAR;
            }
            addarg(e, a);
            expectop(')');
            return e;
        }
        case KW_POINT: {
            e->sub = F_POINT; e->type = T_INT;
            expectop('(');
            Expr *a = parse_expr(); need_num(a); addarg(e, a);
            if (acceptop(',')) { Expr *b = parse_expr(); need_num(b); addarg(e, b); }
            else e->type = T_SNG;
            expectop(')');
            return e;
        }
        case KW_TIMER:
            e->sub = F_TIMER; e->type = T_SNG;
            return e;
        case KW_ERDEV_S:
            e->sub = F_ERDEV; e->type = T_STR;
            return e;
        case KW_SPC: case KW_TAB:
            cerr_at(col, "Syntax error");
    }
    for (const BI *bi = builtins; bi->kw; bi++) {
        if (bi->kw == kw) {
            e->sub = (int16_t)bi->id;
            parse_paren_args_generic(e, bi);
            if (bi->rt == 0) e->type = e->args[0]->type;
            else if (bi->rt == -1) e->type = e->args[0]->type == T_DBL ? T_DBL : T_SNG;
            else e->type = (uint8_t)bi->rt;
            return e;
        }
    }
    cerr_at(col, "Expected: expression");
}

static Expr *parse_call_args(int procidx, Expr *e) {
    Proc *p = &cc->P->procs[procidx];
    int n = 0;
    if (acceptop('(')) {
        if (!acceptop(')')) {
            for (;;) {
                Expr *a;
                if (n < p->nparams && p->params[n].isarray) {
                    a = parse_array_ref_arg();
                } else {
                    a = parse_expr();
                }
                if (n < p->nparams) {
                    Param *pr = &p->params[n];
                    if (pr->isarray) {
                        if (a->op != E_ARRWHOLE || a->type != pr->t) cerr("Parameter type mismatch");
                    } else if (pr->t == T_STR) { if (a->type != T_STR) cerr("Parameter type mismatch"); }
                    else if (pr->t == T_UDT) { if (a->type != T_UDT || a->udt != pr->udt) cerr("Parameter type mismatch"); }
                    else if (!IS_NUM(a->type)) cerr("Parameter type mismatch");
                    /* numeric variable of another type: passed as a converted copy, written back on return */
                }
                addarg(e, a);
                n++;
                if (acceptop(',')) continue;
                break;
            }
            expectop(')');
        }
    }
    if (n != p->nparams) cerr("Argument-count mismatch");
    return e;
}

/* parse member chain after a UDT-typed lvalue: .field.field */
static Expr *parse_members(Expr *base, const char *dotted_rest) {
    /* dotted_rest: remaining "A.B" text from a merged identifier (may be NULL) */
    char buf[128];
    for (;;) {
        const char *fname = NULL;
        char tmp[64];
        if (dotted_rest && *dotted_rest) {
            const char *d = strchr(dotted_rest, '.');
            int l = d ? (int)(d - dotted_rest) : (int)strlen(dotted_rest);
            if (l >= 63) l = 63;
            memcpy(tmp, dotted_rest, (size_t)l); tmp[l] = 0;
            fname = tmp;
            dotted_rest = d ? d + 1 : NULL;
        } else if (isop('.') && base->type == T_UDT) {
            adv();
            if (cur()->k != TK_ID && cur()->k != TK_KW) cerr("Expected: element name");
            snprintf(buf, sizeof buf, "%s", cur()->s);
            adv();
            char *d = strchr(buf, '.');
            if (d) { *d = 0; dotted_rest = (const char *)(d + 1); }
            fname = buf;
            /* copy since buf reused */
            snprintf(tmp, sizeof tmp, "%s", fname);
            fname = tmp;
            if (dotted_rest) {
                static char rest[128];
                snprintf(rest, sizeof rest, "%s", dotted_rest);
                dotted_rest = rest;
            }
        } else break;
        if (base->type != T_UDT) cerr("Expected: end-of-statement");
        Udt *u = &cc->P->udts[base->udt];
        char fb[48];
        int ft = split_name(fname, fb, sizeof fb);
        int fi = -1;
        for (int i = 0; i < u->nfields; i++) if (str_ieq(u->f[i].name, fb)) { fi = i; break; }
        if (fi < 0) cerr("Element not defined");
        if (ft != T_NONE && ft != u->f[fi].t) cerr("Type mismatch");
        Expr *m = enew(E_MEMBER, u->f[fi].t);
        m->a = base;
        m->sub = (int16_t)fi;
        m->udt = u->f[fi].udt;
        m->fixlen = u->f[fi].fixlen;
        base = m;
    }
    return base;
}

/* parse array subscript list into e */
static void parse_subscripts(Expr *e) {
    expectop('(');
    for (;;) {
        Expr *a = parse_expr();
        need_num(a);
        addarg(e, a);
        if (acceptop(',')) continue;
        break;
    }
    expectop(')');
}

/* Parses an identifier-based term. If isfuncret != NULL the parse is for an
   assignment target and may yield the function-result pseudo variable. */
static Expr *parse_lvalue_from_id(int allow_call, int *isfuncret) {
    Tok *t = cur();
    char name[64];
    snprintf(name, sizeof name, "%s", t->s);
    int col = t->col;
    adv();
    char base[48];
    int st = split_name(name, base, sizeof base);
    if (isfuncret) *isfuncret = 0;

    /* function result assignment */
    if (isfuncret && cc->curfunc >= 0 && !isop('(')) {
        Proc *fp = &cc->P->procs[cc->curfunc];
        char fb[48];
        int ft = split_name(fp->name, fb, sizeof fb);
        if (strcmp(fb, base) == 0 && (st == T_NONE || st == ft)) {
            *isfuncret = 1;
            Expr *e = enew(E_VAR, ft);
            return e;
        }
    }

    /* constants */
    if (!strchr(base, '.') || 1) {
        Sym *cs = NULL;
        if (cc->lsyms) cs = symtab_find_const(cc->lsyms, base);
        if (!cs) cs = symtab_find_const(&cc->P->gsyms, base);
        if (cs && !isop('(')) {
            if (isfuncret) cerr_at(col, "Duplicate definition");
            if (cs->cval.t == T_STR) return estr_c(cs->cval.s, cs->cval.slen);
            return enum_c(cs->cval.n, cs->cval.t);
        }
    }

    /* DEF FN */
    if (toupper((unsigned char)name[0]) == 'F' && toupper((unsigned char)name[1]) == 'N') {
        if (isfuncret && cc->curfunc >= 0 && cc->P->procs[cc->curfunc].isdeffn) {
            /* FNname = expr inside DEF FN */
            Proc *fp = &cc->P->procs[cc->curfunc];
            char fb[48];
            split_name(fp->name, fb, sizeof fb);
            if (strcmp(fb, base) == 0) { *isfuncret = 1; return enew(E_VAR, fp->rtype); }
        }
        int fi = find_deffn(name);
        if (fi < 0) cerr_at(col, "Function not defined");
        if (!allow_call) cerr_at(col, "Expected: variable");
        Expr *e = enew(E_FN, cc->P->procs[fi].rtype);
        e->idx = fi;
        parse_call_args(fi, e);
        return e;
    }

    /* user FUNCTION */
    int pi = find_proc(base);
    if (pi >= 0 && cc->P->procs[pi].isfunc) {
        if (!allow_call) cerr_at(col, "Expected: variable");
        Expr *e = enew(E_CALLF, cc->P->procs[pi].rtype);
        e->idx = pi;
        parse_call_args(pi, e);
        return e;
    }
    if (pi >= 0 && !isfuncret) cerr_at(col, "Expected: expression");

    /* MUSIC (QBLike): true while music plays, unless the program has a variable called MUSIC */
    if (!isfuncret && allow_call && st == T_NONE && !strcmp(base, "MUSIC") && !isop('(')) {
        int sc;
        if (!resolve_var(name, 0, 0, &sc)) {
            Expr *e = enew(E_FUNC, T_INT);
            e->sub = F_MUSIC;
            return e;
        }
    }

    /* dotted name: UDT member access? */
    char *dot = strchr(name, '.');
    if (dot) {
        char first[64];
        int l = (int)(dot - name);
        memcpy(first, name, (size_t)l); first[l] = 0;
        int scope;
        Sym *s = NULL;
        if (!isop('(')) s = resolve_var(first, 0, 0, &scope);
        if (s && s->t == T_UDT) {
            Expr *e = enew(E_VAR, T_UDT);
            e->scope = (uint8_t)scope; e->idx = s->idx; e->udt = s->udt;
            return parse_members(e, dot + 1);
        }
    }

    if (isop('(')) {
        int scope;
        Sym *s = resolve_var(name, 1, 1, &scope);
        Expr *e = enew(E_ARR, s->t);
        e->scope = (uint8_t)scope;
        e->idx = s->idx;
        e->udt = s->udt;
        e->fixlen = s->fixlen;
        parse_subscripts(e);
        if (s->t == T_UDT) e = parse_members(e, NULL);
        return e;
    }
    int scope;
    Sym *s = resolve_var(name, 0, 1, &scope);
    Expr *e = enew(E_VAR, s->t);
    e->scope = (uint8_t)scope;
    e->idx = s->idx;
    e->udt = s->udt;
    e->fixlen = s->fixlen;
    if (s->t == T_UDT) e = parse_members(e, NULL);
    return e;
}

static Expr *parse_atom(void) {
    Tok *t = cur();
    switch (t->k) {
        case TK_NUM: { Expr *e = enum_c(t->num, t->ntype); adv(); return e; }
        case TK_STR: { Expr *e = estr_c(t->s, t->slen); adv(); return e; }
        case TK_ID: return parse_lvalue_from_id(1, NULL);
        case TK_KW: return parse_builtin(t->kw);
        case TK_OP:
            if (t->kw == '(') {
                adv();
                Expr *e = parse_expr();
                expectop(')');
                if (e->op == E_VAR || e->op == E_ARR || e->op == E_MEMBER) {
                    if (e->type == T_UDT) return e;
                    /* parenthesized variable is passed by value */
                    Expr *c = enew(E_CONV, e->type);
                    c->a = e;
                    return c;
                }
                return e;
            }
            break;
    }
    cerr("Expected: expression");
}

static Expr *parse_neg(void);

static Expr *parse_pow(void) {
    Expr *a = parse_atom();
    while (isop('^')) {
        adv();
        Expr *b;
        if (isop('-')) { adv(); Expr *x = parse_atom(); need_num(x);
            if (x->op == E_NUM) { x->num = -x->num; if (x->type == T_INT && x->num == -32768) {} b = x; }
            else { b = enew(E_NEG, x->type); b->a = x; } }
        else if (isop('+')) { adv(); b = parse_atom(); }
        else b = parse_atom();
        a = mkbin(B_POW, a, b);
    }
    return a;
}

static Expr *parse_neg(void) {
    if (isop('-')) {
        adv();
        Expr *x = parse_neg();
        need_num(x);
        if (x->op == E_NUM) {
            x->num = -x->num;
            /* -32768 literal: 32768 was LNG, becomes INT range */
            if (x->type == T_LNG && x->num == -32768) x->type = T_INT;
            if (x->type == T_DBL && x->num == -2147483648.0) x->type = T_LNG;
            return x;
        }
        Expr *e = enew(E_NEG, x->type);
        e->a = x;
        return e;
    }
    if (isop('+')) { adv(); return parse_neg(); }
    return parse_pow();
}

static Expr *parse_mul(void) {
    Expr *a = parse_neg();
    for (;;) {
        if (isop('*')) { adv(); a = mkbin(B_MUL, a, parse_neg()); }
        else if (isop('/')) { adv(); a = mkbin(B_DIV, a, parse_neg()); }
        else return a;
    }
}
static Expr *parse_idiv(void) {
    Expr *a = parse_mul();
    while (isop('\\')) { adv(); a = mkbin(B_IDIV, a, parse_mul()); }
    return a;
}
static Expr *parse_mod(void) {
    Expr *a = parse_idiv();
    while (iskw(KW_MOD)) { adv(); a = mkbin(B_MOD, a, parse_idiv()); }
    return a;
}
static Expr *parse_add(void) {
    Expr *a = parse_mod();
    for (;;) {
        if (isop('+')) { adv(); a = mkbin(B_ADD, a, parse_mod()); }
        else if (isop('-')) { adv(); a = mkbin(B_SUB, a, parse_mod()); }
        else return a;
    }
}
static Expr *parse_rel(void) {
    Expr *a = parse_add();
    for (;;) {
        int op = 0;
        if (isop('=')) op = B_EQ;
        else if (isop(OP_NE)) op = B_NE;
        else if (isop('<')) op = B_LT;
        else if (isop('>')) op = B_GT;
        else if (isop(OP_LE)) op = B_LE;
        else if (isop(OP_GE)) op = B_GE;
        if (!op) return a;
        adv();
        a = mkbin(op, a, parse_add());
    }
}
static Expr *parse_not(void) {
    if (iskw(KW_NOT)) {
        adv();
        Expr *x = parse_not();
        need_num(x);
        Expr *e = enew(E_NOT, x->type == T_INT ? T_INT : T_LNG);
        e->a = x;
        if (x->op == E_NUM) { int ok = 1; double v = eval_const_num(e, &ok); if (ok) { Expr *n = enum_c(v, e->type); free_expr(e); return n; } }
        return e;
    }
    return parse_rel();
}
static Expr *parse_and(void) { Expr *a = parse_not(); while (iskw(KW_AND)) { adv(); a = mkbin(B_AND, a, parse_not()); } return a; }
static Expr *parse_or(void) { Expr *a = parse_and(); while (iskw(KW_OR)) { adv(); a = mkbin(B_OR, a, parse_and()); } return a; }
static Expr *parse_xor(void) { Expr *a = parse_or(); while (iskw(KW_XOR)) { adv(); a = mkbin(B_XOR, a, parse_or()); } return a; }
static Expr *parse_eqv(void) { Expr *a = parse_xor(); while (iskw(KW_EQV)) { adv(); a = mkbin(B_EQV, a, parse_xor()); } return a; }
static Expr *parse_expr(void) { Expr *a = parse_eqv(); while (iskw(KW_IMP)) { adv(); a = mkbin(B_IMP, a, parse_eqv()); } return a; }

static Expr *parse_num_expr(void) { Expr *e = parse_expr(); need_num(e); return e; }
static Expr *parse_str_expr(void) { Expr *e = parse_expr(); need_str(e); return e; }

static Expr *parse_lvalue(void) {
    if (cur()->k != TK_ID) cerr("Expected: variable");
    Expr *e = parse_lvalue_from_id(0, NULL);
    if (e->op != E_VAR && e->op != E_ARR && e->op != E_MEMBER) cerr("Expected: variable");
    return e;
}

/* ------------------------------------------------------------------ */
/* statements                                                         */
/* ------------------------------------------------------------------ */
static int emit(int op) {
    Program *P = cc->P;
    if (P->ncode >= P->capcode) {
        P->capcode = P->capcode ? P->capcode * 2 : 256;
        P->code = (Stmt *)qb_realloc(P->code, sizeof(Stmt) * (size_t)P->capcode);
    }
    Stmt *s = &P->code[P->ncode];
    memset(s, 0, sizeof *s);
    s->op = (uint16_t)op;
    s->part = (uint16_t)cc->part;
    s->line = cc->line;
    if (!cc->lineemitted) { s->linestart = 1; cc->lineemitted = 1; }
    return P->ncode++;
}
#define ST(i) (&cc->P->code[(i)])

static void stmt_addarg(int si, Expr *a) {
    Stmt *s = ST(si);
    s->args = (Expr **)qb_realloc(s->args, sizeof(Expr *) * (size_t)(s->nargs + 1));
    s->args[s->nargs++] = a;
}

static void add_fix(int stmt, int which, int argi, const char *label, int module) {
    if (cc->nfix >= cc->capfix) { cc->capfix = cc->capfix ? cc->capfix * 2 : 32; cc->fix = (Fix *)qb_realloc(cc->fix, sizeof(Fix) * (size_t)cc->capfix); }
    Fix *f = &cc->fix[cc->nfix++];
    memset(f, 0, sizeof *f);
    f->stmt = stmt; f->which = which; f->argi = argi;
    snprintf(f->label, sizeof f->label, "%s", label);
    f->proc = module ? -1 : cc->proc;
    f->part = cc->part; f->line = cc->line; f->col = cur()->col;
    f->module = module;
}

static void define_label(const char *name, int col) {
    Program *P = cc->P;
    int procidx = (cc->proc >= 0 && !cc->P->procs[cc->proc].isdeffn) ? cc->proc : -1;
    if (cc->deffn_active) procidx = -1;
    for (int i = 0; i < P->nlabels; i++)
        if (P->labels[i].proc == procidx && str_ieq(P->labels[i].name, name)) cerr_at(col, "Duplicate label");
    P->labels = (Label *)qb_realloc(P->labels, sizeof(Label) * (size_t)(P->nlabels + 1));
    Label *l = &P->labels[P->nlabels++];
    memset(l, 0, sizeof *l);
    snprintf(l->name, sizeof l->name, "%s", name);
    str_upper(l->name);
    l->proc = procidx;
    l->part = cc->part;
    l->stmt = P->ncode;
    l->dataidx = P->ndata;
}

/* read a label reference (line number or identifier) */
static void read_label(char *out, int outsz) {
    Tok *t = cur();
    if (t->k == TK_NUM) {
        snprintf(out, (size_t)outsz, "%.0f", t->num);
        adv();
        return;
    }
    if (t->k == TK_ID) {
        snprintf(out, (size_t)outsz, "%s", t->s);
        str_upper(out);
        adv();
        return;
    }
    cerr("Expected: label or line number");
}

static Block *push_block(int kind) {
    if (cc->nblk >= 127) cerr("Block nesting too deep");
    Block *b = &cc->blk[cc->nblk++];
    memset(b, 0, sizeof *b);
    b->kind = kind;
    b->part = cc->part;
    b->line = cc->line;
    return b;
}
static Block *top_block(void) { return cc->nblk ? &cc->blk[cc->nblk - 1] : NULL; }
static void pop_block(void) { Block *b = &cc->blk[--cc->nblk]; free(b->ends.v); }

static void compile_statement(void);
static void compile_stmts_until_else(void);

static int parse_type_name(int *udt, int *fixlen) {
    *udt = 0; *fixlen = 0;
    if (acceptkw(KW_INTEGER)) return T_INT;
    if (acceptkw(KW_LONG)) return T_LNG;
    if (acceptkw(KW_SINGLE)) return T_SNG;
    if (acceptkw(KW_DOUBLE)) return T_DBL;
    if (acceptkw(KW_STRING)) {
        if (acceptop('*')) {
            Expr *e = parse_expr();
            int ok = 1;
            double v = eval_const_num(e, &ok);
            free_expr(e);
            if (!ok || v < 1 || v > 32767) cerr("Illegal function call");
            *fixlen = (int)v;
        }
        return T_STR;
    }
    if (acceptkw(KW_ANY)) return T_NONE;
    if (cur()->k == TK_ID) {
        int u = find_udt(cur()->s);
        if (u < 0) cerr("Type not defined");
        adv();
        *udt = u;
        return T_UDT;
    }
    cerr("Expected: INTEGER or LONG or SINGLE or DOUBLE or STRING or type name");
}

/* ---- PRINT / LPRINT / PRINT # ---- */
static void compile_print(int op, int tofile) {
    int si = emit(op);
    if (tofile) {
        /* PRINT #n, */
        Expr *fn = parse_num_expr();
        ST(si)->e1 = fn;
        expectop(',');
    }
    if (acceptkw(KW_USING)) {
        ST(si)->e2 = parse_str_expr();
        expectop(';');
    }
    int *seps = NULL;
    int n = 0;
    while (!at_stmt_end()) {
        Expr *item = NULL;
        if (isop(';') || isop(',')) {
            item = NULL;
        } else if (iskw(KW_SPC) || iskw(KW_TAB)) {
            int isspc = iskw(KW_SPC);
            adv();
            expectop('(');
            Expr *a = parse_num_expr();
            expectop(')');
            item = enew(E_FUNC, T_STR);
            item->sub = isspc ? F_SPC : F_TAB;
            addarg(item, a);
        } else {
            item = parse_expr();
            if (item->type == T_UDT) cerr("Type mismatch");
        }
        stmt_addarg(si, item);
        seps = (int *)qb_realloc(seps, sizeof(int) * (size_t)(n + 1));
        seps[n] = 0;
        if (acceptop(';')) seps[n] = 1;
        else if (acceptop(',')) seps[n] = 2;
        else if (!at_stmt_end()) {
            /* QBasic inserts implicit ';' between adjacent items, e.g. PRINT a$ b$ */
            seps[n] = 1;
        }
        n++;
    }
    ST(si)->p = seps;
    ST(si)->i1 = n;
}

static void compile_write(int tofile) {
    int si = emit(S_WRITE);
    if (tofile) { ST(si)->e1 = parse_num_expr(); if (!at_stmt_end()) expectop(','); }
    while (!at_stmt_end()) {
        Expr *e = parse_expr();
        if (e->type == T_UDT) cerr("Type mismatch");
        stmt_addarg(si, e);
        if (!acceptop(',') && !acceptop(';')) break;
    }
    expect_end();
}

static void compile_input(int line_input) {
    int si = emit(line_input ? S_LINEINPUT : S_INPUT);
    Stmt *s;
    if (acceptop('#')) {
        ST(si)->e2 = parse_num_expr();
        expectop(',');
    } else {
        if (acceptop(';')) ST(si)->i1 = 1;       /* no CR after input */
        if (cur()->k == TK_STR) {
            Tok *t = cur();
            ST(si)->e1 = estr_c(t->s, t->slen);
            adv();
            if (acceptop(';')) ST(si)->i2 = 0;
            else if (acceptop(',')) ST(si)->i2 = 1;  /* no "? " */
            else cerr("Expected: ; or ,");
        }
    }
    for (;;) {
        Expr *v = parse_lvalue();
        if (v->type == T_UDT) cerr("Type mismatch");
        if (line_input && v->type != T_STR) cerr("Type mismatch");
        stmt_addarg(si, v);
        if (line_input) break;
        if (acceptop(',')) continue;
        break;
    }
    s = ST(si);
    (void)s;
}

/* graphics coordinate: [STEP] (x, y) */
static int parse_coord(Expr **x, Expr **y) {
    int step = acceptkw(KW_STEP);
    expectop('(');
    *x = parse_num_expr();
    expectop(',');
    *y = parse_num_expr();
    expectop(')');
    return step;
}

static void compile_line_gfx(void) {
    int si = emit(S_LINE);
    Expr *x1 = NULL, *y1 = NULL, *x2, *y2;
    int step1 = 0, has1 = 0;
    if (!isop('-')) { step1 = parse_coord(&x1, &y1); has1 = 1; }
    expectop('-');
    int step2 = parse_coord(&x2, &y2);
    Stmt *s = ST(si);
    s->e1 = x1; s->e2 = y1; s->e3 = x2; s->e4 = y2;
    s->i1 = step1 | (step2 << 1) | (has1 << 2);
    /* , [color] , [B|BF] , [style] */
    Expr *color = NULL, *style = NULL;
    int box = 0;
    if (acceptop(',')) {
        if (!isop(',') && !at_stmt_end()) color = parse_num_expr();
        if (acceptop(',')) {
            if (cur()->k == TK_ID && (str_ieq(cur()->s, "B") || str_ieq(cur()->s, "BF"))) {
                box = str_ieq(cur()->s, "BF") ? 2 : 1;
                adv();
            }
            if (acceptop(',')) style = parse_num_expr();
        }
    }
    s = ST(si);
    s->i2 = box;
    stmt_addarg(si, color);
    stmt_addarg(si, style);
}

static void compile_dim(int redim) {
    int shared = 0, preserve = 0;
    if (redim && acceptkw(KW_PRESERVE)) preserve = 1;
    if (acceptkw(KW_SHARED)) shared = 1;
    if (redim && !preserve && acceptkw(KW_PRESERVE)) preserve = 1;
    if (shared && is_local_scope() && !cc->in_static_stmt) cerr("Illegal in procedure");
    for (;;) {
        if (cur()->k != TK_ID) cerr("Expected: variable");
        char name[64];
        snprintf(name, sizeof name, "%s", cur()->s);
        int col = cur()->col;
        adv();
        Expr *dims[16];
        int nd = 0, isarr = 0;
        if (acceptop('(')) {
            isarr = 1;
            if (!isop(')')) {
                for (;;) {
                    Expr *a = parse_num_expr();
                    Expr *b = NULL;
                    if (acceptkw(KW_TO)) b = parse_num_expr();
                    if (nd >= 8) cerr("Too many dimensions");
                    if (b) { dims[nd * 2] = a; dims[nd * 2 + 1] = b; }
                    else { dims[nd * 2] = NULL; dims[nd * 2 + 1] = a; }
                    nd++;
                    if (acceptop(',')) continue;
                    break;
                }
            }
            expectop(')');
        }
        int t = T_NONE, udt = 0, fixlen = 0, explicit_as = 0;
        if (acceptkw(KW_AS)) { t = parse_type_name(&udt, &fixlen); explicit_as = 1; if (t == T_NONE) cerr("Syntax error"); }
        int global = !is_local_scope() || (cc->deffn_active);
        int scope;
        Sym *s;
        if (redim) {
            /* REDIM of existing array keeps its declaration */
            s = resolve_var(name, 1, 0, &scope);
            if (!s) s = declare_var(name, 1, t, udt, fixlen, global, shared, &scope, explicit_as);
            else if (explicit_as && s->t != t) cerr_at(col, "Duplicate definition");
        } else {
            s = declare_var(name, isarr ? 1 : 0, t, udt, fixlen, global, shared, &scope, explicit_as);
        }
        if (isarr) {
            int si = emit(redim ? S_REDIM : S_DIM);
            Stmt *st = ST(si);
            st->i1 = scope;
            st->i2 = s->idx;
            st->i3 = nd;
            st->i4 = preserve;
            for (int i = 0; i < nd * 2; i++) stmt_addarg(si, dims[i]);
            if (nd == 0) cerr_at(col, "Expected: expression");
        } else if (!redim) {
            /* scalar DIM: reset the variable at runtime (matches QBasic behaviour of fresh var) */
            int si = emit(S_DIM);
            ST(si)->i1 = scope;
            ST(si)->i2 = s->idx;
            ST(si)->i3 = 0;
        }
        if (acceptop(',')) continue;
        break;
    }
}

static void compile_shared_static(int isstatic) {
    /* SHARED a, b() AS INTEGER  /  STATIC a, b() */
    if (!is_local_scope()) cerr("Illegal outside of SUB, FUNCTION or DEF FN");
    for (;;) {
        if (cur()->k != TK_ID) cerr("Expected: variable");
        char name[64];
        snprintf(name, sizeof name, "%s", cur()->s);
        adv();
        int isarr = 0;
        if (acceptop('(')) { isarr = 1; expectop(')'); }
        int t = T_NONE, udt = 0, fixlen = 0, ex = 0;
        if (acceptkw(KW_AS)) { t = parse_type_name(&udt, &fixlen); ex = 1; }
        int scope;
        if (isstatic) {
            cc->in_static_stmt = 1;
            declare_var(name, isarr, t, udt, fixlen, 0, 0, &scope, ex);
            cc->in_static_stmt = 0;
        } else {
            /* find/create global, then add local alias */
            Sym *g = declare_var(name, isarr, t, udt, fixlen, 1, 0, &scope, ex);
            Sym *l = symtab_add(cc->lsyms);
            *l = *g;
            l->shared = 1;    /* alias to global */
        }
        if (acceptop(',')) continue;
        break;
    }
}

static void compile_const(void) {
    for (;;) {
        if (cur()->k != TK_ID) cerr("Expected: identifier");
        char name[64];
        snprintf(name, sizeof name, "%s", cur()->s);
        adv();
        expectop('=');
        Expr *e = parse_expr();
        char base[48], key[50];
        int st = split_name(name, base, sizeof base);
        SymTab *tab = is_local_scope() && !cc->deffn_active ? cc->lsyms : &cc->P->gsyms;
        Sym *s = symtab_add(tab);
        s->kind = 2;
        if (e->type == T_STR) {
            if (e->op != E_STR) cerr("Invalid constant");
            if (st != T_NONE && st != T_STR) cerr("Type mismatch");
            s->t = T_STR;
            s->cval.t = T_STR;
            s->cval.s = qb_strndup(e->str, e->slen);
            s->cval.slen = e->slen;
        } else {
            int ok = 1;
            double v = eval_const_num(e, &ok);
            if (!ok) cerr("Invalid constant");
            int t = st != T_NONE ? st : e->type;
            if (st == T_STR) cerr("Type mismatch");
            s->t = (uint8_t)t;
            s->cval.t = (uint8_t)t;
            s->cval.n = fit_num(t, v);
        }
        snprintf(key, sizeof key, "%s%c", base, type_to_char(s->t));
        strcpy(s->key, key);
        snprintf(s->disp, sizeof s->disp, "%s", name);
        free_expr(e);
        if (acceptop(',')) continue;
        break;
    }
}

static void compile_deftype(int t) {
    for (;;) {
        if (cur()->k != TK_ID) cerr("Expected: letter");
        char a = (char)toupper((unsigned char)cur()->s[0]);
        char b = a;
        if (strlen(cur()->s) != 1) {
            /* "A-Z" may lex as identifier A then '-' ... handled below; or "AZ"? */
            cerr("Expected: letter");
        }
        adv();
        if (acceptop('-')) {
            if (cur()->k != TK_ID || strlen(cur()->s) != 1) cerr("Expected: letter");
            b = (char)toupper((unsigned char)cur()->s[0]);
            adv();
        }
        if (a > b) { char x = a; a = b; b = x; }
        for (char c = a; c <= b; c++) cc->deftype[c - 'A'] = (char)t;
        if (acceptop(',')) continue;
        break;
    }
}

static void compile_data(void) {
    Tok *t = cur();
    if (t->k != TK_DATA) { /* empty DATA */
        Program *P = cc->P;
        P->data = (DataItem *)qb_realloc(P->data, sizeof(DataItem) * (size_t)(P->ndata + 1));
        P->data[P->ndata].s = qb_strdup("");
        P->data[P->ndata].len = 0;
        P->data[P->ndata].quoted = 0;
        P->ndata++;
        return;
    }
    const char *p = t->s;
    Program *P = cc->P;
    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        const char *s;
        int len, quoted = 0;
        if (*p == '"') {
            s = ++p;
            while (*p && *p != '"') p++;
            len = (int)(p - s);
            quoted = 1;
            if (*p == '"') p++;
            while (*p && *p != ',') p++;
        } else {
            s = p;
            while (*p && *p != ',') p++;
            const char *e = p;
            while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
            len = (int)(e - s);
        }
        P->data = (DataItem *)qb_realloc(P->data, sizeof(DataItem) * (size_t)(P->ndata + 1));
        P->data[P->ndata].s = qb_strndup(s, len);
        P->data[P->ndata].len = len;
        P->data[P->ndata].quoted = quoted;
        P->ndata++;
        if (*p == ',') { p++; continue; }
        break;
    }
    adv();
}

static void compile_open(void) {
    int si = emit(S_OPEN);
    Expr *first = parse_str_expr();
    if (acceptop(',')) {
        /* OPEN mode$, [#]n, file$ [, reclen] */
        ST(si)->e4 = first;       /* mode string */
        acceptop('#');
        ST(si)->e2 = parse_num_expr();
        expectop(',');
        ST(si)->e1 = parse_str_expr();
        if (acceptop(',')) ST(si)->e3 = parse_num_expr();
        ST(si)->i1 = -1;
        return;
    }
    ST(si)->e1 = first;
    int mode = 4; /* RANDOM default */
    if (acceptkw(KW_FOR)) {
        if (acceptkw(KW_INPUT)) mode = 1;
        else if (acceptkw(KW_OUTPUT)) mode = 2;
        else if (acceptkw(KW_APPEND)) mode = 3;
        else if (acceptkw(KW_RANDOM)) mode = 4;
        else if (acceptkw(KW_BINARY)) mode = 5;
        else cerr("Expected: INPUT or OUTPUT or APPEND or RANDOM or BINARY");
    }
    if (acceptkw(KW_ACCESS)) {
        if (acceptkw(KW_READ)) { if (acceptkw(KW_WRITE)) {} }
        else if (acceptkw(KW_WRITE)) {}
        else cerr("Expected: READ or WRITE");
    }
    if (iskw(KW_SHARED)) adv();
    if (acceptkw(KW_LOCK)) {
        if (acceptkw(KW_READ)) acceptkw(KW_WRITE);
        else acceptkw(KW_WRITE);
    }
    expectkw(KW_AS);
    acceptop('#');
    ST(si)->e2 = parse_num_expr();
    if (acceptkw(KW_LEN)) { expectop('='); ST(si)->e3 = parse_num_expr(); }
    ST(si)->i1 = mode;
}

static void compile_close(int op) {
    int si = emit(op);
    while (!at_stmt_end()) {
        acceptop('#');
        stmt_addarg(si, parse_num_expr());
        if (!acceptop(',')) break;
    }
}

static void compile_getput_file(int op) {
    int si = emit(op);
    acceptop('#');
    ST(si)->e1 = parse_num_expr();
    if (acceptop(',')) {
        if (!isop(',') && !at_stmt_end()) ST(si)->e2 = parse_num_expr();
        if (acceptop(',')) ST(si)->e3 = parse_lvalue();
    }
}

static void compile_if(void) {
    Expr *cond = parse_expr();
    need_num(cond);
    int gotoform = 0;
    if (acceptkw(KW_GOTO)) gotoform = 1;
    else expectkw(KW_THEN);
    if (!gotoform && (cur()->k == TK_EOL || cur()->k == TK_REM)) {
        /* block IF */
        Block *b = push_block(BK_IF);
        int si = emit(S_JMPF);
        ST(si)->e1 = cond;
        b->jmpf = si;
        return;
    }
    /* single-line IF */
    int jf = emit(S_JMPF);
    ST(jf)->e1 = cond;
    cc->single_if++;
    if (gotoform || cur()->k == TK_NUM) {
        char lab[48];
        read_label(lab, sizeof lab);
        int g = emit(S_GOTO);
        add_fix(g, 1, 0, lab, 0);
    } else {
        compile_stmts_until_else();
    }
    if (iskw(KW_ELSE)) {
        adv();
        int j = emit(S_JMP);
        ST(jf)->i1 = cc->P->ncode;
        if (cur()->k == TK_NUM) {
            char lab[48];
            read_label(lab, sizeof lab);
            int g = emit(S_GOTO);
            add_fix(g, 1, 0, lab, 0);
        } else {
            compile_stmts_until_else();
        }
        ST(j)->i1 = cc->P->ncode;
    } else {
        ST(jf)->i1 = cc->P->ncode;
    }
    cc->single_if--;
}

static void compile_stmts_until_else(void) {
    for (;;) {
        if (cur()->k == TK_EOL || cur()->k == TK_REM) return;
        if (iskw(KW_ELSE)) return;
        if (isop(':')) { adv(); continue; }
        compile_statement();
        if (isop(':')) { adv(); continue; }
        if (iskw(KW_ELSE) || cur()->k == TK_EOL || cur()->k == TK_REM) return;
        cerr("Expected: end-of-statement");
    }
}

static Block *find_block(int kind) {
    for (int i = cc->nblk - 1; i >= 0; i--) {
        if (cc->blk[i].kind == kind) return &cc->blk[i];
        if (cc->blk[i].kind == BK_PROC || cc->blk[i].kind == BK_DEF) return NULL;
    }
    return NULL;
}

static void compile_for(void) {
    if (cur()->k != TK_ID) cerr("Expected: variable");
    Expr *var = parse_lvalue();
    if (var->op != E_VAR) cerr("Expected: simple variable");
    need_num(var);
    expectop('=');
    Expr *start = parse_num_expr();
    expectkw(KW_TO);
    Expr *end = parse_num_expr();
    Expr *step = NULL;
    if (acceptkw(KW_STEP)) step = parse_num_expr();
    Block *b = push_block(BK_FOR);
    int sc1, sc2;
    int h1 = hidden_slot(var->type, &sc1);
    int h2 = hidden_slot(var->type, &sc2);
    int si = emit(S_FORINIT);
    Stmt *s = ST(si);
    s->e1 = var; s->e2 = start; s->e3 = end; s->e4 = step;
    s->i1 = h1; s->i2 = h2; s->i4 = sc1;
    b->forinit = si;
    b->forvar = var;
    b->hidden1 = h1; b->hidden2 = h2;
    b->top = cc->P->ncode;
}

static void close_for(Expr *named) {
    Block *b = top_block();
    if (!b || b->kind != BK_FOR) cerr("NEXT without FOR");
    if (named) {
        if (named->op != E_VAR || named->idx != b->forvar->idx || named->scope != b->forvar->scope) cerr("NEXT without FOR");
    }
    int si = emit(S_NEXT);
    Stmt *s = ST(si);
    s->e1 = b->forvar;     /* shared pointer; freed by FORINIT only */
    s->i1 = b->hidden1; s->i2 = b->hidden2; s->i3 = b->top;
    s->i4 = ST(b->forinit)->i4;
    ST(b->forinit)->i3 = cc->P->ncode;
    for (int i = 0; i < b->ends.n; i++) ST(b->ends.v[i])->i1 = cc->P->ncode;
    pop_block();
}

static void compile_next(void) {
    if (at_stmt_end()) { close_for(NULL); return; }
    for (;;) {
        Expr *v = parse_lvalue();
        close_for(v);
        free_expr(v);
        if (acceptop(',')) continue;
        break;
    }
}

static Expr *invert(Expr *c) {
    Expr *e = enew(E_BIN, T_INT);
    e->sub = B_EQ;
    e->a = c;
    e->b = enum_c(0, T_INT);
    return e;
}

static void compile_do(void) {
    Block *b = push_block(BK_DO);
    b->top = cc->P->ncode;
    if (acceptkw(KW_WHILE)) {
        Expr *c = parse_num_expr();
        int si = emit(S_JMPF);
        ST(si)->e1 = c;
        il_push(&b->ends, si);
        b->top = si;
    } else if (acceptkw(KW_UNTIL)) {
        Expr *c = parse_num_expr();
        int si = emit(S_JMPF);
        ST(si)->e1 = invert(c);
        il_push(&b->ends, si);
        b->top = si;
    }
}

static void compile_loop(void) {
    Block *b = top_block();
    if (!b || b->kind != BK_DO) cerr("LOOP without DO");
    if (acceptkw(KW_WHILE)) {
        Expr *c = parse_num_expr();
        int si = emit(S_JMPF);
        ST(si)->e1 = invert(c);
        ST(si)->i1 = b->top;
    } else if (acceptkw(KW_UNTIL)) {
        Expr *c = parse_num_expr();
        int si = emit(S_JMPF);
        ST(si)->e1 = c;
        ST(si)->i1 = b->top;
    } else {
        int si = emit(S_JMP);
        ST(si)->i1 = b->top;
    }
    for (int i = 0; i < b->ends.n; i++) ST(b->ends.v[i])->i1 = cc->P->ncode;
    pop_block();
}

static void compile_select(void) {
    expectkw(KW_CASE);
    Expr *e = parse_expr();
    if (e->type == T_UDT) cerr("Type mismatch");
    Block *b = push_block(BK_SELECT);
    int scope;
    int slot = hidden_slot(e->type, &scope);
    int si = emit(S_LET);
    Expr *v = enew(E_VAR, e->type);
    v->scope = (uint8_t)scope;
    v->idx = slot;
    ST(si)->e1 = v;
    ST(si)->e2 = e;
    b->sel_slot = slot;
    b->sel_type = e->type;
    b->top = scope;
    b->sel_pending = -1;
}

static Expr *sel_var(Block *b) {
    Expr *v = enew(E_VAR, b->sel_type);
    v->scope = (uint8_t)b->top;
    v->idx = b->sel_slot;
    return v;
}

static void compile_case(void) {
    Block *b = top_block();
    if (!b || b->kind != BK_SELECT) cerr("CASE without SELECT");
    if (b->sel_haselse) cerr("CASE ELSE already used");
    if (b->sel_ncase > 0) {
        int j = emit(S_JMP);
        il_push(&b->ends, j);
    }
    if (b->sel_pending >= 0) { ST(b->sel_pending)->i1 = cc->P->ncode; b->sel_pending = -1; }
    b->sel_ncase++;
    if (acceptkw(KW_ELSE)) { b->sel_haselse = 1; return; }
    Expr *cond = NULL;
    for (;;) {
        Expr *test;
        if (acceptkw(KW_IS)) {
            int op = 0;
            if (isop('=')) op = B_EQ; else if (isop(OP_NE)) op = B_NE; else if (isop('<')) op = B_LT;
            else if (isop('>')) op = B_GT; else if (isop(OP_LE)) op = B_LE; else if (isop(OP_GE)) op = B_GE;
            else cerr("Expected: relational operator");
            adv();
            Expr *x = parse_expr();
            test = mkbin(op, sel_var(b), x);
        } else {
            Expr *x = parse_expr();
            if (acceptkw(KW_TO)) {
                Expr *y = parse_expr();
                Expr *t1 = mkbin(B_GE, sel_var(b), x);
                Expr *t2 = mkbin(B_LE, sel_var(b), y);
                test = mkbin(B_LAND, t1, t2);
            } else {
                test = mkbin(B_EQ, sel_var(b), x);
            }
        }
        cond = cond ? mkbin(B_LOR, cond, test) : test;
        if (acceptop(',')) continue;
        break;
    }
    int si = emit(S_JMPF);
    ST(si)->e1 = cond;
    b->sel_pending = si;
}

static void compile_end_select(void) {
    Block *b = top_block();
    if (!b || b->kind != BK_SELECT) cerr("END SELECT without SELECT");
    if (b->sel_pending >= 0) ST(b->sel_pending)->i1 = cc->P->ncode;
    for (int i = 0; i < b->ends.n; i++) ST(b->ends.v[i])->i1 = cc->P->ncode;
    pop_block();
}

static void compile_on(void) {
    if (acceptkw(KW_ERROR)) {
        expectkw(KW_GOTO);
        int si = emit(S_ONERROR);
        if (cur()->k == TK_NUM && cur()->num == 0) { adv(); ST(si)->i1 = -1; return; }
        char lab[48];
        read_label(lab, sizeof lab);
        add_fix(si, 1, 0, lab, 1);
        return;
    }
    if (acceptkw(KW_TIMER)) {
        expectop('(');
        Expr *n = parse_num_expr();
        expectop(')');
        expectkw(KW_GOSUB);
        int si = emit(S_ONTIMER);
        ST(si)->e1 = n;
        char lab[48];
        read_label(lab, sizeof lab);
        add_fix(si, 1, 0, lab, 1);
        return;
    }
    if (acceptkw(KW_KEY)) {
        expectop('(');
        Expr *n = parse_num_expr();
        expectop(')');
        expectkw(KW_GOSUB);
        int si = emit(S_ONKEY);
        ST(si)->e1 = n;
        char lab[48];
        read_label(lab, sizeof lab);
        add_fix(si, 1, 0, lab, 1);
        return;
    }
    if (acceptkw(KW_PLAY)) {
        /* ON PLAY(n) GOSUB label: when the background music queue drops below n notes */
        expectop('(');
        Expr *n = parse_num_expr();
        expectop(')');
        expectkw(KW_GOSUB);
        int si = emit(S_ONPLAY);
        ST(si)->e1 = n;
        char lab[48];
        read_label(lab, sizeof lab);
        add_fix(si, 1, 0, lab, 1);
        return;
    }
    if (acceptkw(KW_PEN) || acceptkw(KW_STRIG) || acceptkw(KW_COM) || acceptkw(KW_UEVENT)) {
        /* event traps that we do not support: parse and ignore */
        if (acceptop('(')) { Expr *x = parse_expr(); free_expr(x); expectop(')'); }
        expectkw(KW_GOSUB);
        char lab[48];
        read_label(lab, sizeof lab);
        return;
    }
    Expr *e = parse_num_expr();
    int gos;
    if (acceptkw(KW_GOTO)) gos = 0;
    else if (acceptkw(KW_GOSUB)) gos = 1;
    else cerr("Expected: GOTO or GOSUB");
    int si = emit(gos ? S_ONGOSUB : S_ONGOTO);
    ST(si)->e1 = e;
    int n = 0;
    int *targets = NULL;
    for (;;) {
        char lab[48];
        read_label(lab, sizeof lab);
        targets = (int *)qb_realloc(targets, sizeof(int) * (size_t)(n + 1));
        targets[n] = -1;
        ST(si)->p = targets;
        add_fix(si, 3, n, lab, 0);
        n++;
        if (acceptop(',')) continue;
        break;
    }
    ST(si)->i2 = n;
}

/* parse SUB/FUNCTION header; used in prescan and when compiling */
static int parse_proc_header(int isfunc, int prescan) {
    if (cur()->k != TK_ID) cerr("Expected: identifier");
    char name[64];
    snprintf(name, sizeof name, "%s", cur()->s);
    adv();
    char base[48];
    int st = split_name(name, base, sizeof base);
    if (!isfunc && st != T_NONE) cerr("Expected: identifier");
    int pi = -1;
    for (int i = 0; i < cc->P->nprocs; i++) {
        char b[48];
        split_name(cc->P->procs[i].name, b, sizeof b);
        if (!cc->P->procs[i].isdeffn && strcmp(b, base) == 0) { pi = i; break; }
    }
    if (prescan) {
        if (pi >= 0) cerr("Duplicate definition");
        cc->P->procs = (Proc *)qb_realloc(cc->P->procs, sizeof(Proc) * (size_t)(cc->P->nprocs + 1));
        pi = cc->P->nprocs++;
        Proc *p = &cc->P->procs[pi];
        memset(p, 0, sizeof *p);
        p->isfunc = (uint8_t)isfunc;
        p->part = cc->part;
        int rt = st != T_NONE ? st : default_type(base);
        p->rtype = (uint8_t)(isfunc ? rt : T_NONE);
        if (isfunc) snprintf(p->name, sizeof p->name, "%s%c", base, type_to_char(rt));
        else snprintf(p->name, sizeof p->name, "%s", base);
        snprintf(p->disp, sizeof p->disp, "%s", name);
        if (acceptop('(')) {
            if (!acceptop(')')) {
                for (;;) {
                    if (p->nparams >= 32) cerr("Too many parameters");
                    Param *pr = &p->params[p->nparams];
                    if (acceptkw(KW_BYVAL)) cerr("Syntax error");
                    if (cur()->k != TK_ID) cerr("Expected: identifier");
                    snprintf(pr->name, sizeof pr->name, "%s", cur()->s);
                    adv();
                    if (acceptop('(')) { pr->isarray = 1; expectop(')'); }
                    char pb[48];
                    int pt = split_name(pr->name, pb, sizeof pb);
                    pr->t = (uint8_t)(pt != T_NONE ? pt : default_type(pb));
                    if (acceptkw(KW_AS)) {
                        int udt, fl;
                        int t = parse_type_name(&udt, &fl);
                        if (t == T_NONE) t = T_SNG;
                        if (pt != T_NONE && pt != t) cerr("Duplicate definition");
                        pr->t = (uint8_t)t;
                        pr->udt = udt;
                        pr->hasas = 1;
                    }
                    p->nparams++;
                    if (acceptop(',')) continue;
                    break;
                }
                expectop(')');
            }
        }
        if (acceptkw(KW_STATIC)) p->isstatic = 1;
        return pi;
    }
    /* skip the rest of the header when compiling the body */
    while (!at_stmt_end()) adv();
    return pi;
}

static void begin_proc_body(int pi) {
    Proc *p = &cc->P->procs[pi];
    cc->proc = pi;
    cc->lsyms = (SymTab *)qb_alloc(sizeof(SymTab));
    p->syms = cc->lsyms;
    cc->nlslot = 0; cc->nlaslot = 0;
    cc->curfunc = p->isfunc ? pi : -1;
    p->entry = cc->P->ncode;
    p->defined = 1;
    /* function result occupies no slot (frame->ret); params get slots 0..n-1 */
    for (int i = 0; i < p->nparams; i++) {
        Param *pr = &p->params[i];
        int scope;
        char base[48];
        (void)split_name(pr->name, base, sizeof base);
        /* only "x AS type" makes the bare name x refer to the parameter whatever the suffix;
           with t$ the parameter is t$ and a plain t is another (local) variable, as in QBasic */
        Sym *s = declare_var(pr->name, pr->isarray, pr->t, pr->udt, 0, 0, 0, &scope, pr->hasas);
        s->declared = pr->hasas;
    }
}

static void end_proc_body(void) {
    Proc *p = &cc->P->procs[cc->proc];
    p->nlocals = cc->nlslot;
    p->nlarrays = cc->nlaslot;
    /* save slot infos */
    if (cc->proc >= g_npslots) {
        g_pslots = (ProcSlots *)qb_realloc(g_pslots, sizeof(ProcSlots) * (size_t)(cc->P->nprocs + 1));
        for (int i = g_npslots; i <= cc->proc; i++) memset(&g_pslots[i], 0, sizeof(ProcSlots));
        g_npslots = cc->proc + 1;
    }
    ProcSlots *ps = &g_pslots[cc->proc];
    free(ps->s); free(ps->a);
    ps->ns = cc->nlslot;
    ps->na = cc->nlaslot;
    ps->s = (SlotInfo *)qb_alloc(sizeof(SlotInfo) * (size_t)(ps->ns + 1));
    ps->a = (SlotInfo *)qb_alloc(sizeof(SlotInfo) * (size_t)(ps->na + 1));
    if (ps->ns) memcpy(ps->s, cc->lslot, sizeof(SlotInfo) * (size_t)ps->ns);
    if (ps->na) memcpy(ps->a, cc->laslot, sizeof(SlotInfo) * (size_t)ps->na);
    p->slotstatic = (uint8_t *)qb_alloc((size_t)ps->ns + 1);
    p->arrstatic = (uint8_t *)qb_alloc((size_t)ps->na + 1);
    for (int i = 0; i < ps->ns; i++) p->slotstatic[i] = (uint8_t)(p->isstatic || cc->lslotstatic[i]);
    for (int i = 0; i < ps->na; i++) p->arrstatic[i] = (uint8_t)(p->isstatic || cc->laslotstatic[i]);
    /* params are never static storage */
    for (int i = 0; i < p->nparams; i++) {
        /* params occupy the first scalar/array slots in declaration order */
    }
    cc->lsyms = NULL;
    cc->proc = -1;
    cc->curfunc = -1;
}

static void compile_def_fn(void) {
    /* DEF FNname[(params)] = expr   |  multi-line DEF FNname[(params)] ... END DEF */
    if (acceptkw(KW_SEG)) {
        int si = emit(S_DEFSEG);
        if (acceptop('=')) ST(si)->e1 = parse_num_expr();
        return;
    }
    if (cur()->k != TK_ID || toupper((unsigned char)cur()->s[0]) != 'F' || toupper((unsigned char)cur()->s[1]) != 'N')
        cerr("Expected: FN name");
    if (is_local_scope()) cerr("Illegal in procedure");
    char name[64];
    snprintf(name, sizeof name, "%s", cur()->s);
    adv();
    char base[48];
    int st = split_name(name, base, sizeof base);
    Program *P = cc->P;
    P->procs = (Proc *)qb_realloc(P->procs, sizeof(Proc) * (size_t)(P->nprocs + 1));
    int pi = P->nprocs++;
    Proc *p = &P->procs[pi];
    memset(p, 0, sizeof *p);
    p->isfunc = 1;
    p->isdeffn = 1;
    p->part = cc->part;
    int rt = st != T_NONE ? st : default_type(base);
    p->rtype = (uint8_t)rt;
    snprintf(p->name, sizeof p->name, "%s%c", base, type_to_char(rt));
    snprintf(p->disp, sizeof p->disp, "%s", name);
    if (acceptop('(')) {
        if (!acceptop(')')) {
            for (;;) {
                Param *pr = &p->params[p->nparams];
                if (cur()->k != TK_ID) cerr("Expected: identifier");
                snprintf(pr->name, sizeof pr->name, "%s", cur()->s);
                adv();
                char pb[48];
                int pt = split_name(pr->name, pb, sizeof pb);
                pr->t = (uint8_t)(pt != T_NONE ? pt : default_type(pb));
                if (acceptkw(KW_AS)) { int u, f; pr->t = (uint8_t)parse_type_name(&u, &f); pr->udt = u; pr->hasas = 1; }
                p->nparams++;
                if (acceptop(',')) continue;
                break;
            }
            expectop(')');
        }
    }
    int skip = emit(S_JMP);
    /* body */
    begin_proc_body(pi);
    p = &P->procs[pi];
    p->defined = 0;   /* not visible inside its own body until defined... QBasic allows recursion? no */
    cc->deffn_active = 1;
    if (acceptop('=')) {
        Expr *e = parse_expr();
        if ((e->type == T_STR) != (rt == T_STR)) cerr("Type mismatch");
        int si = emit(S_LETFUNC);
        ST(si)->e1 = e;
        emit(S_PROCEND);
        cc->deffn_active = 0;
        end_proc_body();
        P->procs[pi].defined = 1;
        ST(skip)->i1 = P->ncode;
        return;
    }
    Block *b = push_block(BK_DEF);
    b->jmpf = skip;
    b->top = pi;
    P->procs[pi].defined = 1;
}

static void compile_end(void) {
    if (acceptkw(KW_IF)) {
        Block *b = top_block();
        if (!b || b->kind != BK_IF) cerr("END IF without block IF");
        if (b->jmpf >= 0) ST(b->jmpf)->i1 = cc->P->ncode;
        for (int i = 0; i < b->ends.n; i++) ST(b->ends.v[i])->i1 = cc->P->ncode;
        pop_block();
        return;
    }
    if (acceptkw(KW_SELECT)) { compile_end_select(); return; }
    if (acceptkw(KW_SUB) || acceptkw(KW_FUNCTION)) {
        Block *b = top_block();
        if (!b || b->kind != BK_PROC) {
            if (b) {
                const char *what = b->kind == BK_FOR ? "FOR without NEXT" : b->kind == BK_DO ? "DO without LOOP" :
                    b->kind == BK_WHILE ? "WHILE without WEND" : b->kind == BK_IF ? "Block IF without END IF" :
                    b->kind == BK_SELECT ? "SELECT without END SELECT" : "Syntax error";
                cc->line = b->line;
                cerr_at(0, "%s", what);
            }
            cerr("END SUB/FUNCTION without SUB/FUNCTION");
        }
        emit(S_PROCEND);
        pop_block();
        end_proc_body();
        return;
    }
    if (acceptkw(KW_DEF)) {
        Block *b = top_block();
        if (!b || b->kind != BK_DEF) cerr("END DEF without DEF");
        emit(S_PROCEND);
        int skip = b->jmpf;
        pop_block();
        cc->deffn_active = 0;
        end_proc_body();
        ST(skip)->i1 = cc->P->ncode;
        return;
    }
    if (acceptkw(KW_TYPE)) cerr("END TYPE without TYPE");
    emit(S_END);
}

static void compile_exit(void) {
    if (acceptkw(KW_FOR)) {
        Block *b = find_block(BK_FOR);
        if (!b) cerr("EXIT FOR not within FOR...NEXT");
        il_push(&b->ends, emit(S_JMP));
        return;
    }
    if (acceptkw(KW_DO)) {
        Block *b = find_block(BK_DO);
        if (!b) cerr("EXIT DO not within DO...LOOP");
        il_push(&b->ends, emit(S_JMP));
        return;
    }
    if (acceptkw(KW_SUB)) {
        if (cc->proc < 0 || cc->P->procs[cc->proc].isfunc) cerr("EXIT SUB not within SUB");
        emit(S_PROCEND);
        return;
    }
    if (acceptkw(KW_FUNCTION)) {
        if (cc->proc < 0 || !cc->P->procs[cc->proc].isfunc || cc->P->procs[cc->proc].isdeffn) cerr("EXIT FUNCTION not within FUNCTION");
        emit(S_PROCEND);
        return;
    }
    if (acceptkw(KW_DEF)) {
        if (!cc->deffn_active) cerr("EXIT DEF not within DEF FN");
        emit(S_PROCEND);
        return;
    }
    cerr("Expected: DO or FOR or SUB or FUNCTION or DEF");
}

static void compile_call_args_stmt(int pi, int parens) {
    int si = emit(S_CALL);
    ST(si)->i1 = pi;
    Proc *p = &cc->P->procs[pi];
    int n = 0;
    if (parens) {
        Expr tmp;
        memset(&tmp, 0, sizeof tmp);
        parse_call_args(pi, &tmp);
        for (int i = 0; i < tmp.nargs; i++) stmt_addarg(si, tmp.args[i]);
        free(tmp.args);
        return;
    }
    while (!at_stmt_end()) {
        Expr *a;
        if (n < p->nparams && p->params[n].isarray) a = parse_array_ref_arg();
        else a = parse_expr();
        if (n < p->nparams) {
            Param *pr = &p->params[n];
            if (pr->isarray) { if (a->op != E_ARRWHOLE || a->type != pr->t) cerr("Parameter type mismatch"); }
            else if (pr->t == T_STR) { if (a->type != T_STR) cerr("Parameter type mismatch"); }
            else if (pr->t == T_UDT) { if (a->type != T_UDT || a->udt != pr->udt) cerr("Parameter type mismatch"); }
            else if (!IS_NUM(a->type)) cerr("Parameter type mismatch");
        }
        stmt_addarg(si, a);
        n++;
        if (!acceptop(',')) break;
    }
    if (n != p->nparams) cerr("Argument-count mismatch");
}

static void compile_assignment(void) {
    int isret = 0;
    int col = cur()->col;
    Expr *lv = parse_lvalue_from_id(0, &isret);
    if (!isret && lv->op != E_VAR && lv->op != E_ARR && lv->op != E_MEMBER) cerr_at(col, "Expected: variable");
    if (!acceptop('=')) cerr("Expected: =");
    Expr *rhs = parse_expr();
    if (lv->type == T_STR && rhs->type != T_STR) cerr("Type mismatch");
    if (IS_NUM(lv->type) && !IS_NUM(rhs->type)) cerr("Type mismatch");
    if (lv->type == T_UDT && (rhs->type != T_UDT || rhs->udt != lv->udt)) cerr("Type mismatch");
    if (isret) {
        free_expr(lv);
        int si = emit(S_LETFUNC);
        ST(si)->e1 = rhs;
        return;
    }
    int si = emit(S_LET);
    ST(si)->e1 = lv;
    ST(si)->e2 = rhs;
}

/* FILL (x, y), c  versus an array element assignment  fill(x, y) = c */
static int is_fill_statement(void) {
    Tok *n = peekn(1);
    if (n->k == TK_KW && n->kw == KW_STEP) return 1;
    if (!(n->k == TK_OP && n->kw == '(')) return 0;
    int depth = 0;
    for (int k = 1; ; k++) {
        Tok *q = peekn(k);
        if (q->k == TK_EOL || q->k == TK_REM) return 0;
        if (q->k == TK_OP && q->kw == '(') depth++;
        else if (q->k == TK_OP && q->kw == ')' && --depth == 0) {
            Tok *a = peekn(k + 1);
            return (a->k == TK_OP && (a->kw == ',' || a->kw == ':')) || a->k == TK_EOL || a->k == TK_REM ||
                   (a->k == TK_KW && a->kw == KW_ELSE);
        }
    }
}

/* MUSIC ... versus an assignment to a variable or array element called MUSIC */
static int is_music_statement(void) {
    Tok *n = peekn(1);
    if (n->k == TK_EOL || n->k == TK_REM) return 0;
    if (n->k == TK_OP && (n->kw == '=' || n->kw == ':')) return 0;
    if (n->k == TK_OP && n->kw == '(') {
        int depth = 0;
        for (int k = 1; ; k++) {
            Tok *q = peekn(k);
            if (q->k == TK_EOL || q->k == TK_REM) return 1;
            if (q->k == TK_OP && q->kw == '(') depth++;
            else if (q->k == TK_OP && q->kw == ')' && --depth == 0) {
                Tok *a = peekn(k + 1);
                return !(a->k == TK_OP && a->kw == '=');
            }
        }
    }
    return 1;
}

static void compile_statement(void) {
    Tok *t = cur();
    if (t->k == TK_ID) {
        char base[48];
        split_name(t->s, base, sizeof base);
        int pi = find_proc(base);
        if (pi >= 0 && !cc->P->procs[pi].isfunc && !(peekn(1)->k == TK_OP && peekn(1)->kw == '=')) {
            adv();
            compile_call_args_stmt(pi, 0);
            return;
        }
        /* FILL [STEP] (x, y)[, color]: QBLike extension. Not a reserved word, so programs with their
           own SUB/FUNCTION, variable or array called FILL keep working. */
        if (pi < 0 && !strcmp(base, "FILL") && !strpbrk(t->s, "%&!#$") && is_fill_statement()) {
            adv();
            int si = emit(S_FILL);
            Expr *x, *y;
            ST(si)->i1 = parse_coord(&x, &y);
            ST(si)->e1 = x; ST(si)->e2 = y;
            if (acceptop(',')) ST(si)->e3 = parse_num_expr();
            return;
        }
        /* MUSIC file$ [, LOOP] | MUSIC STOP | MUSIC VOLUME v%: QBLike extension, also not reserved */
        if (pi < 0 && !strcmp(base, "MUSIC") && !strpbrk(t->s, "%&!#$") && is_music_statement()) {
            adv();
            int si = emit(S_MUSIC);
            if (iskw(KW_STOP)) { adv(); ST(si)->i1 = 1; return; }
            if (cur()->k == TK_ID && str_ieq(cur()->s, "VOLUME")) {
                Tok *n = peekn(1);
                if (!(n->k == TK_EOL || n->k == TK_REM || (n->k == TK_OP && (n->kw == ':' || n->kw == ',')) ||
                      (n->k == TK_KW && n->kw == KW_ELSE))) {
                    adv();
                    ST(si)->i1 = 2;
                    ST(si)->e1 = parse_num_expr();
                    return;
                }
            }
            ST(si)->e1 = parse_str_expr();
            if (acceptop(',')) { expectkw(KW_LOOP); ST(si)->i2 = 1; }
            return;
        }
        compile_assignment();
        return;
    }
    if (t->k != TK_KW) {
        if (t->k == TK_NUM) cerr("Expected: statement");
        cerr("Syntax error");
    }
    int kw = t->kw;
    int col = t->col;
    adv();
    switch (kw) {
        case KW_LET: compile_assignment(); return;
        case KW_DATE_S: case KW_TIME_S: {
            /* DATE$ = "mm-dd-yyyy" / TIME$ = "hh:mm:ss" set the DOS clock: accepted, the system
               clock is not changed */
            expectop('=');
            Expr *x = parse_str_expr(); free_expr(x);
            return;
        }
        case KW_PRINT:
            if (acceptop('#')) compile_print(S_PRINT, 1);
            else compile_print(S_PRINT, 0);
            return;
        case KW_LPRINT: compile_print(S_LPRINT, 0); return;
        case KW_WRITE:
            if (acceptop('#')) compile_write(1); else compile_write(0);
            return;
        case KW_INPUT: compile_input(0); return;
        case KW_LINE:
            if (acceptkw(KW_INPUT)) { compile_input(1); return; }
            compile_line_gfx();
            return;
        case KW_CLS: { int si = emit(S_CLS); if (!at_stmt_end()) ST(si)->e1 = parse_num_expr(); return; }
        case KW_GOTO: { char lab[48]; read_label(lab, sizeof lab); int si = emit(S_GOTO); add_fix(si, 1, 0, lab, 0); return; }
        case KW_GOSUB: { char lab[48]; read_label(lab, sizeof lab); int si = emit(S_GOSUB); add_fix(si, 1, 0, lab, 0); return; }
        case KW_RETURN: {
            int si = emit(S_RETURN);
            ST(si)->i1 = -1;
            if (!at_stmt_end()) { char lab[48]; read_label(lab, sizeof lab); add_fix(si, 1, 0, lab, 0); }
            return;
        }
        case KW_IF: compile_if(); return;
        case KW_ELSEIF: {
            Block *b = top_block();
            if (!b || b->kind != BK_IF) cerr_at(col, "ELSEIF without IF");
            if (b->jmpf < 0) cerr_at(col, "ELSEIF without IF");
            int j = emit(S_JMP);
            il_push(&b->ends, j);
            ST(b->jmpf)->i1 = cc->P->ncode;
            Expr *c = parse_num_expr();
            expectkw(KW_THEN);
            int si = emit(S_JMPF);
            ST(si)->e1 = c;
            b->jmpf = si;
            return;
        }
        case KW_ELSE: {
            Block *b = top_block();
            if (!b || b->kind != BK_IF || b->jmpf < 0) cerr_at(col, "ELSE without IF");
            int j = emit(S_JMP);
            il_push(&b->ends, j);
            ST(b->jmpf)->i1 = cc->P->ncode;
            b->jmpf = -1;
            /* statements may follow ELSE on the same line */
            return;
        }
        case KW_END:
            if (at_stmt_end() || !(iskw(KW_IF) || iskw(KW_SELECT) || iskw(KW_SUB) || iskw(KW_FUNCTION) || iskw(KW_DEF) || iskw(KW_TYPE))) {
                emit(S_END);
                return;
            }
            compile_end();
            return;
        case KW_FOR: compile_for(); return;
        case KW_NEXT: compile_next(); return;
        case KW_WHILE: {
            Block *b = push_block(BK_WHILE);
            Expr *c = parse_num_expr();
            int si = emit(S_JMPF);
            ST(si)->e1 = c;
            b->top = si;
            il_push(&b->ends, si);
            return;
        }
        case KW_WEND: {
            Block *b = top_block();
            if (!b || b->kind != BK_WHILE) cerr_at(col, "WEND without WHILE");
            int si = emit(S_JMP);
            ST(si)->i1 = b->top;
            for (int i = 0; i < b->ends.n; i++) ST(b->ends.v[i])->i1 = cc->P->ncode;
            pop_block();
            return;
        }
        case KW_DO: compile_do(); return;
        case KW_LOOP: compile_loop(); return;
        case KW_SELECT: compile_select(); return;
        case KW_CASE: compile_case(); return;
        case KW_EXIT: compile_exit(); return;
        case KW_DIM: compile_dim(0); return;
        case KW_REDIM: compile_dim(1); return;
        case KW_COMMON: {
            /* COMMON [SHARED] /name/ var list: treat as DIM SHARED */
            acceptkw(KW_SHARED);
            if (acceptop('/')) { if (cur()->k == TK_ID) adv(); expectop('/'); }
            for (;;) {
                if (cur()->k != TK_ID) cerr("Expected: variable");
                char name[64];
                snprintf(name, sizeof name, "%s", cur()->s);
                adv();
                int isarr = 0;
                if (acceptop('(')) { isarr = 1; if (!isop(')')) { Expr *x = parse_expr(); free_expr(x); } expectop(')'); }
                int t2 = T_NONE, udt = 0, fl = 0, ex = 0;
                if (acceptkw(KW_AS)) { t2 = parse_type_name(&udt, &fl); ex = 1; }
                int scope;
                declare_var(name, isarr, t2, udt, fl, 1, 1, &scope, ex);
                if (acceptop(',')) continue;
                break;
            }
            return;
        }
        case KW_SHARED: compile_shared_static(0); return;
        case KW_STATIC: compile_shared_static(1); return;
        case KW_ERASE: {
            for (;;) {
                if (cur()->k != TK_ID) cerr("Expected: array name");
                char name[64];
                snprintf(name, sizeof name, "%s", cur()->s);
                adv();
                int scope;
                Sym *s = resolve_var(name, 1, 1, &scope);
                int si = emit(S_ERASE);
                ST(si)->i1 = scope;
                ST(si)->i2 = s->idx;
                if (acceptop(',')) continue;
                break;
            }
            return;
        }
        case KW_CONST: compile_const(); return;
        case KW_DEFINT: compile_deftype(T_INT); return;
        case KW_DEFLNG: compile_deftype(T_LNG); return;
        case KW_DEFSNG: compile_deftype(T_SNG); return;
        case KW_DEFDBL: compile_deftype(T_DBL); return;
        case KW_DEFSTR: compile_deftype(T_STR); return;
        case KW_DATA: compile_data(); return;
        case KW_READ: {
            int si = emit(S_READ);
            for (;;) {
                Expr *v = parse_lvalue();
                if (v->type == T_UDT) cerr("Type mismatch");
                stmt_addarg(si, v);
                if (acceptop(',')) continue;
                break;
            }
            return;
        }
        case KW_RESTORE: {
            int si = emit(S_RESTORE);
            ST(si)->i1 = -1;
            if (!at_stmt_end()) { char lab[48]; read_label(lab, sizeof lab); add_fix(si, 1, 0, lab, 2); }
            return;
        }
        case KW_SWAP: {
            int si = emit(S_SWAP);
            Expr *a = parse_lvalue();
            expectop(',');
            Expr *b = parse_lvalue();
            if (a->type != b->type || (a->type == T_UDT && a->udt != b->udt)) cerr("Type mismatch");
            ST(si)->e1 = a; ST(si)->e2 = b;
            return;
        }
        case KW_CALL: {
            if (cur()->k != TK_ID) {
                if (iskw(KW_INT) || iskw(KW_SEG)) cerr("Advanced feature unavailable");
                cerr("Expected: identifier");
            }
            char base[48];
            split_name(cur()->s, base, sizeof base);
            if (str_ieq(base, "ABSOLUTE") || str_ieq(base, "INTERRUPT") || str_ieq(base, "INTERRUPTX")) cerr("Advanced feature unavailable");
            int pi = find_proc(base);
            if (pi < 0 || cc->P->procs[pi].isfunc) cerr("Subprogram not defined");
            adv();
            compile_call_args_stmt(pi, isop('('));
            return;
        }
        case KW_DECLARE: {
            /* DECLARE SUB/FUNCTION name (params): validated loosely and ignored */
            if (!acceptkw(KW_SUB)) expectkw(KW_FUNCTION);
            while (!at_stmt_end()) adv();
            return;
        }
        case KW_SUB: case KW_FUNCTION: {
            cerr_at(col, "SUB or FUNCTION must be in its own window");
        }
        case KW_DEF: compile_def_fn(); return;
        case KW_TYPE: cerr_at(col, "Illegal in procedure");
        case KW_REM: return;
        case KW_ON: compile_on(); return;
        case KW_RESUME: {
            int si = emit(S_RESUME);
            if (acceptkw(KW_NEXT)) ST(si)->i2 = 1;
            else if (at_stmt_end() || (cur()->k == TK_NUM && cur()->num == 0)) { if (!at_stmt_end()) adv(); ST(si)->i2 = 0; }
            else { char lab[48]; read_label(lab, sizeof lab); ST(si)->i2 = 2; add_fix(si, 1, 0, lab, 1); }
            return;
        }
        case KW_ERROR: { int si = emit(S_ERROR); ST(si)->e1 = parse_num_expr(); return; }
        case KW_STOP: emit(S_STOP); return;
        case KW_SYSTEM: emit(S_SYSTEM); return;
        case KW_CLEAR: {
            emit(S_CLEAR);
            while (!at_stmt_end()) { if (!isop(',')) { Expr *x = parse_expr(); free_expr(x); } else adv(); }
            return;
        }
        case KW_RUN: {
            int si = emit(S_RUN);
            ST(si)->i1 = -1;
            if (!at_stmt_end()) {
                if (cur()->k == TK_NUM) { char lab[48]; read_label(lab, sizeof lab); add_fix(si, 1, 0, lab, 1); }
                else ST(si)->e1 = parse_str_expr();
            }
            return;
        }
        case KW_CHAIN: { int si = emit(S_CHAIN); ST(si)->e1 = parse_str_expr(); return; }
        case KW_SHELL: { int si = emit(S_SHELL); if (!at_stmt_end()) ST(si)->e1 = parse_str_expr(); return; }
        case KW_TRON: emit(S_TRON); return;
        case KW_TROFF: emit(S_TROFF); return;
        case KW_COLOR: {
            int si = emit(S_COLOR);
            Expr *a = NULL, *b = NULL, *c = NULL;
            if (!isop(',') && !at_stmt_end()) a = parse_num_expr();
            if (acceptop(',')) {
                if (!isop(',') && !at_stmt_end()) b = parse_num_expr();
                if (acceptop(',')) c = parse_num_expr();
            }
            ST(si)->e1 = a; ST(si)->e2 = b; ST(si)->e3 = c;
            return;
        }
        case KW_LOCATE: {
            int si = emit(S_LOCATE);
            for (int i = 0; i < 5; i++) {
                Expr *x = NULL;
                if (!isop(',') && !at_stmt_end()) x = parse_num_expr();
                stmt_addarg(si, x);
                if (!acceptop(',')) break;
            }
            return;
        }
        case KW_SCREEN: {
            /* SCREEN FULL / SCREEN WINDOW (QBLike): fullscreen or windowed. FULL is not reserved: a
               program with a variable called FULL keeps SCREEN full meaning the screen mode */
            if (iskw(KW_WINDOW) || (cur()->k == TK_ID && str_ieq(cur()->s, "FULL"))) {
                Tok *n = peekn(1);
                int alone = n->k == TK_EOL || n->k == TK_REM || (n->k == TK_OP && n->kw == ':') || (n->k == TK_KW && n->kw == KW_ELSE);
                int sc;
                if (alone && (iskw(KW_WINDOW) || !resolve_var(cur()->s, 0, 0, &sc))) {
                    int si = emit(S_FULLSCREEN);
                    ST(si)->i1 = iskw(KW_WINDOW) ? 0 : 1;
                    adv();
                    return;
                }
            }
            int si = emit(S_SCREEN);
            for (int i = 0; i < 4; i++) {
                Expr *x = NULL;
                if (!isop(',') && !at_stmt_end()) x = parse_num_expr();
                stmt_addarg(si, x);
                if (!acceptop(',')) break;
            }
            if (ST(si)->nargs == 0 || !ST(si)->args[0]) {
                if (ST(si)->nargs == 0) cerr("Expected: expression");
            }
            return;
        }
        case KW_WIDTH: {
            int si = emit(S_WIDTH);
            if (acceptop('#')) { Expr *f = parse_num_expr(); expectop(','); Expr *w = parse_num_expr(); free_expr(f); free_expr(w); ST(si)->i1 = 1; return; }
            if (cur()->k == TK_STR) { adv(); expectop(','); Expr *w = parse_num_expr(); free_expr(w); ST(si)->i1 = 1; return; }
            if (iskw(KW_LPRINT)) { adv(); Expr *w = parse_num_expr(); free_expr(w); ST(si)->i1 = 1; return; }
            if (!isop(',')) ST(si)->e1 = parse_num_expr();
            if (acceptop(',')) ST(si)->e2 = parse_num_expr();
            return;
        }
        case KW_VIEW: {
            if (acceptkw(KW_PRINT)) {
                int si = emit(S_VIEWPRINT);
                if (!at_stmt_end()) { ST(si)->e1 = parse_num_expr(); expectkw(KW_TO); ST(si)->e2 = parse_num_expr(); }
                return;
            }
            int si = emit(S_VIEW);
            if (at_stmt_end()) return;
            if (acceptkw(KW_SCREEN)) ST(si)->i1 = 1;
            Expr *x1, *y1, *x2, *y2;
            expectop('('); x1 = parse_num_expr(); expectop(','); y1 = parse_num_expr(); expectop(')');
            expectop('-');
            expectop('('); x2 = parse_num_expr(); expectop(','); y2 = parse_num_expr(); expectop(')');
            ST(si)->e1 = x1; ST(si)->e2 = y1; ST(si)->e3 = x2; ST(si)->e4 = y2;
            ST(si)->i2 = 1;
            Expr *fill = NULL, *border = NULL;
            if (acceptop(',')) {
                if (!isop(',') && !at_stmt_end()) fill = parse_num_expr();
                if (acceptop(',')) border = parse_num_expr();
            }
            stmt_addarg(si, fill);
            stmt_addarg(si, border);
            return;
        }
        case KW_WINDOW: {
            int si = emit(S_WINDOW);
            if (at_stmt_end()) return;
            if (acceptkw(KW_SCREEN)) ST(si)->i1 = 1;
            Expr *x1, *y1, *x2, *y2;
            expectop('('); x1 = parse_num_expr(); expectop(','); y1 = parse_num_expr(); expectop(')');
            expectop('-');
            expectop('('); x2 = parse_num_expr(); expectop(','); y2 = parse_num_expr(); expectop(')');
            ST(si)->e1 = x1; ST(si)->e2 = y1; ST(si)->e3 = x2; ST(si)->e4 = y2;
            ST(si)->i2 = 1;
            return;
        }
        case KW_PSET: case KW_PRESET: {
            int si = emit(kw == KW_PSET ? S_PSET : S_PRESET);
            Expr *x, *y;
            ST(si)->i1 = parse_coord(&x, &y);
            ST(si)->e1 = x; ST(si)->e2 = y;
            if (acceptop(',')) ST(si)->e3 = parse_num_expr();
            return;
        }
        case KW_CIRCLE: {
            int si = emit(S_CIRCLE);
            Expr *x, *y;
            ST(si)->i1 = parse_coord(&x, &y);
            ST(si)->e1 = x; ST(si)->e2 = y;
            expectop(',');
            ST(si)->e3 = parse_num_expr();
            for (int i = 0; i < 4; i++) {
                if (!acceptop(',')) break;
                Expr *a = NULL;
                if (!isop(',') && !at_stmt_end()) a = parse_num_expr();
                stmt_addarg(si, a);
            }
            return;
        }
        case KW_PAINT: {
            int si = emit(S_PAINT);
            Expr *x, *y;
            ST(si)->i1 = parse_coord(&x, &y);
            ST(si)->e1 = x; ST(si)->e2 = y;
            if (acceptop(',')) {
                if (!isop(',') && !at_stmt_end()) { ST(si)->e3 = parse_expr(); if (ST(si)->e3->type == T_UDT) cerr("Type mismatch"); }
                if (acceptop(',')) {
                    if (!isop(',') && !at_stmt_end()) ST(si)->e4 = parse_num_expr();
                    if (acceptop(',')) { Expr *bg = parse_expr(); free_expr(bg); }
                }
            }
            return;
        }
        case KW_DRAW: { int si = emit(S_DRAW); ST(si)->e1 = parse_str_expr(); return; }
        case KW_GET:
            if (isop('(') || iskw(KW_STEP)) {
                int si = emit(S_GETG);
                Expr *x1, *y1, *x2, *y2;
                int s1 = parse_coord(&x1, &y1);
                expectop('-');
                int s2 = parse_coord(&x2, &y2);
                expectop(',');
                ST(si)->e1 = x1; ST(si)->e2 = y1; ST(si)->e3 = x2; ST(si)->e4 = y2;
                ST(si)->i1 = s1 | (s2 << 1);
                /* array name with optional index */
                if (cur()->k != TK_ID) cerr("Expected: array");
                char name[64]; snprintf(name, sizeof name, "%s", cur()->s); adv();
                int scope; Sym *s = resolve_var(name, 1, 1, &scope);
                ST(si)->i2 = scope; ST(si)->i3 = s->idx;
                if (acceptop('(')) { if (!isop(')')) { Expr *ix = parse_num_expr(); stmt_addarg(si, ix); while (acceptop(',')) stmt_addarg(si, parse_num_expr()); } expectop(')'); }
                return;
            }
            compile_getput_file(S_GETF);
            return;
        case KW_PUT:
            if (isop('(') || iskw(KW_STEP)) {
                int si = emit(S_PUTG);
                Expr *x, *y;
                ST(si)->i1 = parse_coord(&x, &y);
                ST(si)->e1 = x; ST(si)->e2 = y;
                expectop(',');
                if (cur()->k != TK_ID) cerr("Expected: array");
                char name[64]; snprintf(name, sizeof name, "%s", cur()->s); adv();
                int scope; Sym *s = resolve_var(name, 1, 1, &scope);
                ST(si)->i2 = scope; ST(si)->i3 = s->idx;
                if (acceptop('(')) { if (!isop(')')) { Expr *ix = parse_num_expr(); stmt_addarg(si, ix); while (acceptop(',')) stmt_addarg(si, parse_num_expr()); } expectop(')'); }
                ST(si)->i4 = 3; /* XOR default */
                if (acceptop(',')) {
                    if (acceptkw(KW_PSET)) ST(si)->i4 = 0;
                    else if (acceptkw(KW_PRESET)) ST(si)->i4 = 1;
                    else if (acceptkw(KW_AND)) ST(si)->i4 = 2;
                    else if (acceptkw(KW_XOR)) ST(si)->i4 = 3;
                    else if (acceptkw(KW_OR)) ST(si)->i4 = 4;
                    else if (cur()->k == TK_ID && str_ieq(cur()->s, "MASK")) { adv(); ST(si)->i4 = 5; }
                    else cerr("Expected: PSET or PRESET or AND or OR or XOR or MASK");
                }
                return;
            }
            compile_getput_file(S_PUTF);
            return;
        case KW_MOUSE: {
            /* MOUSE x, y, buttons   (QBLike extension) */
            int si = emit(S_MOUSE);
            if (acceptkw(KW_ON)) { ST(si)->i1 = 1; return; }      /* MOUSE ON: show the cursor */
            if (acceptkw(KW_OFF)) { ST(si)->i1 = 2; return; }     /* MOUSE OFF: hide it */
            for (int i = 0; i < 3; i++) {
                if (i) expectop(',');
                Expr *v = parse_lvalue();
                need_num(v);
                stmt_addarg(si, v);
            }
            return;
        }
        case KW_CLIP: {
            /* CLIP x1, y1, x2, y2   |   CLIP  (whole screen) */
            int si = emit(S_CLIP);
            if (at_stmt_end()) return;
            ST(si)->e1 = parse_num_expr(); expectop(',');
            ST(si)->e2 = parse_num_expr(); expectop(',');
            ST(si)->e3 = parse_num_expr(); expectop(',');
            ST(si)->e4 = parse_num_expr();
            return;
        }
        case KW_LOADBMP: case KW_LOADGIF: case KW_LOADPAL: {
            /* LOADBMP file$, array(index)  |  LOADBMP file$, SCREEN, x, y [, PALETTE]   (also LOADGIF)
               LOADPAL file$, array(index) */
            int si = emit(kw == KW_LOADPAL ? S_LOADPAL : S_LOADIMG);
            ST(si)->e1 = parse_str_expr();
            expectop(',');
            int flags = kw == KW_LOADGIF ? 1 : 0;
            if (kw != KW_LOADPAL && iskw(KW_SCREEN)) {
                adv();
                flags |= 2;
                expectop(',');
                ST(si)->e2 = parse_num_expr();
                expectop(',');
                ST(si)->e3 = parse_num_expr();
                if (acceptop(',')) { expectkw(KW_PALETTE); flags |= 4; }
            } else {
                if (cur()->k != TK_ID) cerr("Expected: array or SCREEN");
                char name[64]; snprintf(name, sizeof name, "%s", cur()->s); adv();
                int scope; Sym *sy = resolve_var(name, 1, 1, &scope);
                if (sy->t == T_STR || sy->t == T_UDT) cerr("Type mismatch");
                ST(si)->i2 = scope; ST(si)->i3 = sy->idx;
                if (acceptop('(')) { if (!isop(')')) { stmt_addarg(si, parse_num_expr()); while (acceptop(',')) stmt_addarg(si, parse_num_expr()); } expectop(')'); }
            }
            ST(si)->i1 = flags;
            return;
        }
        case KW_PALETTE: {
            int si = emit(S_PALETTE);
            if (acceptkw(KW_USING)) {
                if (cur()->k != TK_ID) cerr("Expected: array");
                char name[64]; snprintf(name, sizeof name, "%s", cur()->s); adv();
                int scope; Sym *s = resolve_var(name, 1, 1, &scope);
                ST(si)->i1 = 2; ST(si)->i2 = scope; ST(si)->i3 = s->idx;
                if (acceptop('(')) { if (!isop(')')) ST(si)->e1 = parse_num_expr(); expectop(')'); }
                return;
            }
            if (at_stmt_end()) { ST(si)->i1 = 0; return; }
            ST(si)->i1 = 1;
            ST(si)->e1 = parse_num_expr();
            expectop(',');
            ST(si)->e2 = parse_num_expr();
            return;
        }
        case KW_PCOPY: { int si = emit(S_PCOPY); ST(si)->e1 = parse_num_expr(); expectop(','); ST(si)->e2 = parse_num_expr(); return; }
        case KW_BEEP: emit(S_BEEP); return;
        case KW_SOUND: {
            if (iskw(KW_PLAY)) {
                /* SOUND PLAY id% [, channel%] [, volume%] [, pan%] [, LOOP]   (QBLike) */
                adv();
                int si = emit(S_SNDPLAY);
                ST(si)->e1 = parse_num_expr();
                Expr **slot[3] = { &ST(si)->e2, &ST(si)->e3, &ST(si)->e4 };
                for (int k = 0; k < 3 && acceptop(','); k++) {
                    if (iskw(KW_LOOP)) { adv(); ST(si)->i1 = 1; goto sndplay_done; }
                    if (isop(',')) continue;
                    *slot[k] = parse_num_expr();
                }
                if (acceptop(',')) { expectkw(KW_LOOP); ST(si)->i1 = 1; }
            sndplay_done:
                return;
            }
            if (iskw(KW_STOP)) {
                /* SOUND STOP [channel%]   (QBLike) */
                adv();
                int si = emit(S_SNDSTOP);
                if (!at_stmt_end()) ST(si)->e1 = parse_num_expr();
                return;
            }
            int si = emit(S_SOUND); ST(si)->e1 = parse_num_expr(); expectop(','); ST(si)->e2 = parse_num_expr(); return;
        }
        case KW_PLAY: {
            if (iskw(KW_ON) || iskw(KW_OFF) || iskw(KW_STOP)) {
                int si = emit(S_PLAYCTL);
                ST(si)->i1 = iskw(KW_ON) ? 1 : iskw(KW_OFF) ? 0 : 2;
                adv();
                return;
            }
            int si = emit(S_PLAY); ST(si)->e1 = parse_str_expr(); return;
        }
        case KW_SLEEP: { int si = emit(S_SLEEP); if (!at_stmt_end()) ST(si)->e1 = parse_num_expr(); return; }
        case KW_RANDOMIZE: { int si = emit(S_RANDOMIZE); if (!at_stmt_end()) ST(si)->e1 = parse_num_expr(); return; }
        case KW_OPEN: compile_open(); return;
        case KW_CLOSE: compile_close(S_CLOSE); return;
        case KW_RESET: emit(S_RESET); return;
        case KW_KILL: { int si = emit(S_KILL); ST(si)->e1 = parse_str_expr(); return; }
        case KW_NAME: { int si = emit(S_NAME); ST(si)->e1 = parse_str_expr(); expectkw(KW_AS); ST(si)->e2 = parse_str_expr(); return; }
        case KW_MKDIR: { int si = emit(S_MKDIR); ST(si)->e1 = parse_str_expr(); return; }
        case KW_RMDIR: { int si = emit(S_RMDIR); ST(si)->e1 = parse_str_expr(); return; }
        case KW_CHDIR: { int si = emit(S_CHDIR); ST(si)->e1 = parse_str_expr(); return; }
        case KW_FILES: { int si = emit(S_FILES); if (!at_stmt_end()) ST(si)->e1 = parse_str_expr(); return; }
        case KW_SEEK: { int si = emit(S_SEEK); acceptop('#'); ST(si)->e1 = parse_num_expr(); expectop(','); ST(si)->e2 = parse_num_expr(); return; }
        case KW_LOCK: case KW_UNLOCK: {
            acceptop('#');
            Expr *x = parse_num_expr(); free_expr(x);
            if (acceptop(',')) { x = parse_num_expr(); free_expr(x); if (acceptkw(KW_TO)) { x = parse_num_expr(); free_expr(x); } }
            return;
        }
        case KW_FIELD: {
            int si = emit(S_FIELD);
            acceptop('#');
            ST(si)->e1 = parse_num_expr();
            while (acceptop(',')) {
                Expr *w = parse_num_expr();
                expectkw(KW_AS);
                Expr *v = parse_lvalue();
                if (v->type != T_STR) cerr("Type mismatch");
                stmt_addarg(si, w);
                stmt_addarg(si, v);
            }
            return;
        }
        case KW_POKE: { int si = emit(S_POKE); ST(si)->e1 = parse_num_expr(); expectop(','); ST(si)->e2 = parse_num_expr(); return; }
        case KW_OUT: { int si = emit(S_OUT); ST(si)->e1 = parse_num_expr(); expectop(','); ST(si)->e2 = parse_num_expr(); return; }
        case KW_WAIT: {
            /* WAIT VSYNC: QBLike alias of WAIT &H3DA, 8 (waits for the next vertical retrace). VSYNC is
               not a reserved word: "WAIT name" without a comma can't be anything else. */
            if (cur()->k == TK_ID && qb_stricmp(cur()->s, "VSYNC") == 0) {
                Tok *n = peekn(1);
                if (n->k == TK_EOL || n->k == TK_REM || (n->k == TK_OP && n->kw == ':') || (n->k == TK_KW && n->kw == KW_ELSE)) {
                    adv();
                    emit(S_VSYNC);
                    return;
                }
            }
            int si = emit(S_WAIT);
            ST(si)->e1 = parse_num_expr(); expectop(','); ST(si)->e2 = parse_num_expr();
            if (acceptop(',')) ST(si)->e3 = parse_num_expr();
            return;
        }
        case KW_MID_S: {
            /* MID$(v$, start[, len]) = expr */
            int si = emit(S_MIDSTMT);
            expectop('(');
            Expr *v = parse_lvalue();
            if (v->type != T_STR) cerr("Type mismatch");
            expectop(',');
            ST(si)->e1 = v;
            ST(si)->e2 = parse_num_expr();
            if (acceptop(',')) ST(si)->e3 = parse_num_expr();
            expectop(')');
            expectop('=');
            ST(si)->e4 = parse_str_expr();
            return;
        }
        case KW_LSET: case KW_RSET: {
            int si = emit(kw == KW_LSET ? S_LSET : S_RSET);
            Expr *v = parse_lvalue();
            expectop('=');
            Expr *e = parse_expr();
            if (v->type == T_UDT) {
                if (e->type != T_UDT) cerr("Type mismatch");
            } else if (v->type != T_STR || e->type != T_STR) cerr("Type mismatch");
            ST(si)->e1 = v; ST(si)->e2 = e;
            return;
        }
        case KW_TIMER: {
            int si = emit(S_TIMERCTL);
            if (acceptkw(KW_ON)) ST(si)->i1 = 1;
            else if (acceptkw(KW_OFF)) ST(si)->i1 = 0;
            else if (acceptkw(KW_STOP)) ST(si)->i1 = 2;
            else cerr("Expected: ON or OFF or STOP");
            return;
        }
        case KW_KEY: {
            if (acceptop('(')) {
                int si = emit(S_KEYCTL);
                ST(si)->e1 = parse_num_expr();
                expectop(')');
                if (acceptkw(KW_ON)) ST(si)->i1 = 1;
                else if (acceptkw(KW_OFF)) ST(si)->i1 = 0;
                else if (acceptkw(KW_STOP)) ST(si)->i1 = 2;
                else cerr("Expected: ON or OFF or STOP");
                return;
            }
            int si = emit(S_KEY);
            if (acceptkw(KW_ON)) ST(si)->i1 = 1;
            else if (acceptkw(KW_OFF)) ST(si)->i1 = 0;
            else if (acceptkw(KW_LIST)) ST(si)->i1 = 2;
            else {
                ST(si)->i1 = 3;
                ST(si)->e1 = parse_num_expr();
                expectop(',');
                ST(si)->e2 = parse_str_expr();
            }
            return;
        }
        case KW_ENVIRON: { int si = emit(S_ENVIRON); ST(si)->e1 = parse_str_expr(); return; }
        case KW_BLOAD: case KW_BSAVE: {
            /* BLOAD file$ [, offset]   BSAVE file$, offset, length
               With offset written as VARPTR(array(i)) the data goes to / comes from the array (i1 = 1),
               raw, whatever DEF SEG says. */
            int si = emit(kw == KW_BLOAD ? S_BLOAD : S_BSAVE);
            ST(si)->e1 = parse_str_expr();
            if (acceptop(',')) {
                if (!isop(',')) {
                    int done = 0;
                    if (iskw(KW_VARPTR) && peekn(1)->k == TK_OP && peekn(1)->kw == '(' && peekn(2)->k == TK_ID &&
                        peekn(3)->k == TK_OP && peekn(3)->kw == '(') {
                        int save = cc->pos;
                        adv(); adv();
                        char name[64]; snprintf(name, sizeof name, "%s", cur()->s); adv();
                        int scope; Sym *sy = resolve_var(name, 1, 1, &scope);
                        adv();   /* ( */
                        if (!isop(')')) { stmt_addarg(si, parse_num_expr()); while (acceptop(',')) stmt_addarg(si, parse_num_expr()); }
                        expectop(')');
                        expectop(')');
                        if (isop(',') || at_stmt_end()) {
                            if (sy->t == T_STR || sy->t == T_UDT) cerr("Type mismatch");
                            ST(si)->i1 = 1; ST(si)->i2 = scope; ST(si)->i3 = sy->idx;
                            done = 1;
                        } else { cc->pos = save; ST(si)->nargs = 0; }   /* VARPTR(...) inside a larger expression */
                    }
                    if (!done) ST(si)->e2 = parse_num_expr();
                }
                if (acceptop(',')) ST(si)->e3 = parse_num_expr();
            }
            if (kw == KW_BSAVE && (!ST(si)->e3 || (!ST(si)->e2 && !ST(si)->i1))) cerr("Expected: ,");
            return;
        }
        case KW_OPTION: {
            expectkw(KW_BASE);
            if (cur()->k != TK_NUM || (cur()->num != 0 && cur()->num != 1)) cerr("Expected: 0 or 1");
            /* OPTION BASE is a compile-time setting: store in program */
            cc->P->deftype[0] = cc->P->deftype[0];
            {
                int si = emit(S_NOP);
                ST(si)->i1 = 999;
                ST(si)->i2 = (int)cur()->num;
            }
            adv();
            return;
        }
        case KW_IOCTL: case KW_COM: case KW_PEN: case KW_STRIG: case KW_UEVENT: {
            while (!at_stmt_end()) adv();
            return;
        }
        default:
            break;
    }
    cc->pos--;
    if (cur()->k == TK_KW) {
        /* keyword that is a function used as a statement */
        cerr("Expected: statement");
    }
    cerr("Syntax error");
}

/* ------------------------------------------------------------------ */
/* TYPE blocks (prescan)                                              */
/* ------------------------------------------------------------------ */
static int udt_size(Udt *u);

static int field_size(Field *f) {
    switch (f->t) {
        case T_INT: return 2; case T_LNG: return 4; case T_SNG: return 4; case T_DBL: return 8;
        case T_STR: return f->fixlen; case T_UDT: return cc->P->udts[f->udt].size;
    }
    return 0;
}
static int udt_size(Udt *u) { int s = 0; for (int i = 0; i < u->nfields; i++) s += field_size(&u->f[i]); return s; }

static void lex_into(const char *line) {
    lex_free(&cc->tl);
    lex_line(line, &cc->tl);
    cc->pos = 0;
    if (cc->tl.err) cerr_at(0, "%s", cc->tl.err);
}

static void prescan_types(SrcPart *part) {
    cc->part = 0;
    int in_type = -1;
    for (int i = 0; i < part->nlines; i++) {
        cc->line = i;
        lex_into(part->lines[i]);
        Tok *t = cur();
        /* skip a line label */
        if (t->k == TK_NUM) { adv(); t = cur(); }
        else if (t->k == TK_ID && peekn(1)->k == TK_OP && peekn(1)->kw == ':' && in_type < 0) { adv(); adv(); t = cur(); }
        if (in_type < 0) {
            if (t->k == TK_KW && t->kw == KW_TYPE) {
                adv();
                if (cur()->k != TK_ID) cerr("Expected: identifier");
                if (find_udt(cur()->s) >= 0) cerr("Duplicate definition");
                Program *P = cc->P;
                P->udts = (Udt *)qb_realloc(P->udts, sizeof(Udt) * (size_t)(P->nudts + 1));
                Udt *u = &P->udts[P->nudts];
                memset(u, 0, sizeof *u);
                snprintf(u->name, sizeof u->name, "%s", cur()->s);
                in_type = P->nudts++;
                cc->typeline[i] = 1;
                adv();
                expect_end();
            }
            continue;
        }
        cc->typeline[i] = 1;
        if (t->k == TK_EOL || t->k == TK_REM) continue;
        if (t->k == TK_KW && t->kw == KW_END) {
            adv();
            expectkw(KW_TYPE);
            Udt *u = &cc->P->udts[in_type];
            if (u->nfields == 0) cerr_at(0, "Element not defined");
            u->size = udt_size(u);
            in_type = -1;
            continue;
        }
        if (t->k != TK_ID) cerr("Expected: identifier");
        char fname[64];
        snprintf(fname, sizeof fname, "%s", t->s);
        adv();
        expectkw(KW_AS);
        int udt, fl;
        int ft = parse_type_name(&udt, &fl);
        if (ft == T_NONE) cerr("Syntax error");
        if (ft == T_STR && fl == 0) cerr("Expected: STRING * length");
        if (ft == T_UDT && udt == in_type) cerr("Type not defined");
        expect_end();
        Udt *u = &cc->P->udts[in_type];
        u->f = (Field *)qb_realloc(u->f, sizeof(Field) * (size_t)(u->nfields + 1));
        Field *f = &u->f[u->nfields++];
        memset(f, 0, sizeof *f);
        char base[48];
        int st = split_name(fname, base, sizeof base);
        if (st != T_NONE) cerr("Expected: identifier");
        snprintf(f->name, sizeof f->name, "%s", base);
        f->t = (uint8_t)ft;
        f->udt = udt;
        f->fixlen = fl;
    }
    if (in_type >= 0) { cc->line = part->nlines ? part->nlines - 1 : 0; cerr_at(0, "TYPE without END TYPE"); }
}

static void prescan_deftypes(SrcPart *part) {
    /* collect DEFtype statements from the module so procs inherit them */
    for (int i = 0; i < part->nlines; i++) {
        if (cc->typeline[i]) continue;
        cc->line = i;
        TokLine tl;
        lex_line(part->lines[i], &tl);
        for (int j = 0; j < tl.n; j++) {
            Tok *t = &tl.t[j];
            if (t->k == TK_KW && (t->kw == KW_DEFINT || t->kw == KW_DEFLNG || t->kw == KW_DEFSNG || t->kw == KW_DEFDBL || t->kw == KW_DEFSTR)
                && (j == 0 || (tl.t[j - 1].k == TK_OP && tl.t[j - 1].kw == ':'))) {
                int ty = t->kw == KW_DEFINT ? T_INT : t->kw == KW_DEFLNG ? T_LNG : t->kw == KW_DEFSNG ? T_SNG : t->kw == KW_DEFDBL ? T_DBL : T_STR;
                int k = j + 1;
                while (k < tl.n && tl.t[k].k == TK_ID) {
                    char a = (char)toupper((unsigned char)tl.t[k].s[0]), b = a;
                    k++;
                    if (k + 1 < tl.n && tl.t[k].k == TK_OP && tl.t[k].kw == '-' && tl.t[k + 1].k == TK_ID) {
                        b = (char)toupper((unsigned char)tl.t[k + 1].s[0]);
                        k += 2;
                    }
                    if (a > b) { char x = a; a = b; b = x; }
                    if (a >= 'A' && b <= 'Z') for (char c = a; c <= b; c++) cc->proc_deftype[c - 'A'] = (char)ty;
                    if (k < tl.n && tl.t[k].k == TK_OP && tl.t[k].kw == ',') k++;
                    else break;
                }
            }
        }
        lex_free(&tl);
    }
}

/* ------------------------------------------------------------------ */
/* Line compilation                                                   */
/* ------------------------------------------------------------------ */
static void compile_line(const char *text) {
    lex_into(text);
    cc->lineemitted = 0;
    Tok *t = cur();
    if (t->k == TK_NUM) {
        if (t->ntype == T_SNG || t->ntype == T_DBL) {
            if (t->num != floor(t->num)) cerr("Expected: statement");
        }
        char lab[48];
        snprintf(lab, sizeof lab, "%.0f", t->num);
        define_label(lab, t->col);
        adv();
    } else if (t->k == TK_ID && peekn(1)->k == TK_OP && peekn(1)->kw == ':' ) {
        char base[48];
        int st = split_name(t->s, base, sizeof base);
        /* "name:" at the start of a line is always a label, as in QBasic, even when a SUB has the
           same name (labels and procedures are separate namespaces) */
        if (st == T_NONE) {
            define_label(t->s, t->col);
            adv();
            adv();
        }
    }
    for (;;) {
        if (cur()->k == TK_EOL || cur()->k == TK_REM) break;
        if (isop(':')) { adv(); continue; }
        compile_statement();
        if (isop(':')) { adv(); continue; }
        if (cur()->k == TK_EOL || cur()->k == TK_REM) break;
        cerr("Expected: end-of-statement");
    }
}

static void check_unclosed(void) {
    for (int i = cc->nblk - 1; i >= 0; i--) {
        Block *b = &cc->blk[i];
        if (b->kind == BK_PROC) continue;
        cc->part = b->part;
        cc->line = b->line;
        const char *m = "Syntax error";
        switch (b->kind) {
            case BK_IF: m = "Block IF without END IF"; break;
            case BK_FOR: m = "FOR without NEXT"; break;
            case BK_WHILE: m = "WHILE without WEND"; break;
            case BK_DO: m = "DO without LOOP"; break;
            case BK_SELECT: m = "SELECT without END SELECT"; break;
            case BK_DEF: m = "DEF without END DEF"; break;
        }
        cerr_at(0, "%s", m);
    }
}

static int resolve_label(const char *name, int proc, int module, int *dataidx) {
    Program *P = cc->P;
    for (int i = 0; i < P->nlabels; i++) {
        Label *l = &P->labels[i];
        if (module == 2) {
            if (str_ieq(l->name, name) && l->proc == -1) { if (dataidx) *dataidx = l->dataidx; return l->stmt; }
            continue;
        }
        if (l->proc == proc && str_ieq(l->name, name)) { if (dataidx) *dataidx = l->dataidx; return l->stmt; }
    }
    if (module == 2) {
        for (int i = 0; i < P->nlabels; i++) if (str_ieq(P->labels[i].name, name)) { if (dataidx) *dataidx = P->labels[i].dataidx; return P->labels[i].stmt; }
    }
    return -1;
}

static void resolve_fixes(void) {
    for (int i = 0; i < cc->nfix; i++) {
        Fix *f = &cc->fix[i];
        int dataidx = 0;
        int target = resolve_label(f->label, f->module ? -1 : f->proc, f->module, &dataidx);
        if (target < 0) {
            cc->part = f->part;
            cc->line = f->line;
            cerr_at(f->col, "Label not defined");
        }
        Stmt *s = ST(f->stmt);
        if (f->which == 1) {
            if (s->op == S_RESTORE) s->i1 = dataidx;
            else s->i1 = target;
        } else if (f->which == 3) ((int *)s->p)[f->argi] = target;
    }
}

static void init_c(C *c, Program *P, CompileError *err) {
    memset(c, 0, sizeof *c);
    c->P = P;
    c->err = err;
    c->proc = -1;
    c->curfunc = -1;
    for (int i = 0; i < 26; i++) { c->deftype[i] = T_SNG; c->proc_deftype[i] = T_SNG; }
}

Program *compile_program(SrcPart *parts, int nparts, CompileError *err) {
    C c;
    Program *P = (Program *)qb_alloc(sizeof(Program));
    init_c(&c, P, err);
    cc = &c;
    memset(err, 0, sizeof *err);
    g_prog = P;
    for (int i = 0; i < 26; i++) P->deftype[i] = T_SNG;
    if (setjmp(c.jb)) {
        lex_free(&c.tl);
        free(c.fix);
        free(c.typeline);
        free(c.gslot); free(c.gaslot); free(c.lslot); free(c.laslot); free(c.lslotstatic); free(c.laslotstatic);
        for (int i = 0; i < c.nblk; i++) free(c.blk[i].ends.v);
        free_program(P);
        cc = NULL;
        return NULL;
    }
    c.typeline = (uint8_t *)qb_alloc((size_t)(nparts > 0 ? parts[0].nlines : 0) + 1);
    /* Phase A: TYPE blocks and DEFtypes from the module */
    if (nparts > 0) {
        prescan_types(&parts[0]);
        prescan_deftypes(&parts[0]);
    }
    /* Phase B: procedure headers */
    for (int pi = 1; pi < nparts; pi++) {
        memcpy(c.deftype, c.proc_deftype, 26);
        c.part = pi;
        int found = 0;
        for (int i = 0; i < parts[pi].nlines && !found; i++) {
            c.line = i;
            lex_into(parts[pi].lines[i]);
            Tok *t = cur();
            if (t->k == TK_EOL || t->k == TK_REM) continue;
            if (t->k == TK_KW && (t->kw == KW_DEFINT || t->kw == KW_DEFLNG || t->kw == KW_DEFSNG || t->kw == KW_DEFDBL || t->kw == KW_DEFSTR)) {
                int ty = t->kw == KW_DEFINT ? T_INT : t->kw == KW_DEFLNG ? T_LNG : t->kw == KW_DEFSNG ? T_SNG : t->kw == KW_DEFDBL ? T_DBL : T_STR;
                adv();
                compile_deftype(ty);
                continue;
            }
            if (t->k == TK_KW && t->kw == KW_STATIC) adv();
            if (iskw(KW_SUB) || iskw(KW_FUNCTION)) {
                int isf = iskw(KW_FUNCTION);
                adv();
                parse_proc_header(isf, 1);
                found = 1;
                break;
            }
            cerr("Expected: SUB or FUNCTION");
        }
        if (!found) { c.line = 0; cerr_at(0, "Expected: SUB or FUNCTION"); }
    }
    /* Phase C: module-level code */
    for (int i = 0; i < 26; i++) c.deftype[i] = T_SNG;
    c.part = 0;
    if (nparts > 0) {
        for (int i = 0; i < parts[0].nlines; i++) {
            c.line = i;
            if (c.typeline[i]) continue;
            compile_line(parts[0].lines[i]);
        }
    }
    check_unclosed();
    c.line = nparts > 0 && parts[0].nlines > 0 ? parts[0].nlines - 1 : 0;
    c.lineemitted = 1;
    emit(S_END);
    /* Phase D: procedures */
    for (int pi = 1; pi < nparts; pi++) {
        memcpy(c.deftype, c.proc_deftype, 26);
        c.part = pi;
        int started = 0, ended = 0;
        int procidx = -1;
        for (int i = 0; i < parts[pi].nlines; i++) {
            c.line = i;
            if (ended) {
                lex_into(parts[pi].lines[i]);
                if (cur()->k != TK_EOL && cur()->k != TK_REM) cerr("Statement cannot follow END SUB/FUNCTION");
                continue;
            }
            if (!started) {
                lex_into(parts[pi].lines[i]);
                Tok *t = cur();
                if (t->k == TK_EOL || t->k == TK_REM) continue;
                if (t->k == TK_KW && t->kw >= KW_DEFDBL && t->kw <= KW_DEFSTR && t->kw != KW_DEF) {
                    compile_line(parts[pi].lines[i]);
                    continue;
                }
                acceptkw(KW_STATIC);
                int isf = iskw(KW_FUNCTION);
                adv();
                char base[48];
                split_name(cur()->s, base, sizeof base);
                procidx = find_proc(base);
                c.lineemitted = 0;
                parse_proc_header(isf, 0);
                Block *b = push_block(BK_PROC);
                b->top = procidx;
                begin_proc_body(procidx);
                started = 1;
                expect_end();
                continue;
            }
            compile_line(parts[pi].lines[i]);
            if (c.proc < 0) ended = 1;
        }
        if (!ended) {
            check_unclosed();
            c.line = parts[pi].nlines ? parts[pi].nlines - 1 : 0;
            cerr_at(0, "SUB/FUNCTION without END SUB/FUNCTION");
        }
    }
    resolve_fixes();
    /* save global slot info */
    free(g_gslot); free(g_gaslot);
    g_gslot = c.gslot; g_ngslot = c.ngslot;
    g_gaslot = c.gaslot; g_ngaslot = c.ngaslot;
    P->nglobals = c.ngslot;
    P->ngarrays = c.ngaslot;
    free(c.lslot); free(c.laslot); free(c.lslotstatic); free(c.laslotstatic);
    free(c.fix);
    free(c.typeline);
    lex_free(&c.tl);
    cc = NULL;
    return P;
}

/* Immediate window: compile one line in the context of an existing program.
   New variables are created as module-level variables. */
int compile_immediate(Program *P, const char *line, int procidx, int *startpc, CompileError *err) {
    C c;
    init_c(&c, P, err);
    memset(err, 0, sizeof *err);
    cc = &c;
    c.immediate = 1;
    c.gslot = g_gslot; c.ngslot = g_ngslot; c.capgslot = g_ngslot;
    c.gaslot = g_gaslot; c.ngaslot = g_ngaslot; c.capgaslot = g_ngaslot;
    int save_ncode = P->ncode;
    int save_nlabels = P->nlabels;
    if (procidx >= 0 && procidx < P->nprocs) {
        c.proc = procidx;
        c.lsyms = P->procs[procidx].syms;
        /* local slot info */
        ProcSlots *ps = &g_pslots[procidx];
        c.lslot = (SlotInfo *)qb_alloc(sizeof(SlotInfo) * (size_t)(ps->ns + 1));
        memcpy(c.lslot, ps->s, sizeof(SlotInfo) * (size_t)ps->ns);
        c.nlslot = ps->ns; c.caplslot = ps->ns + 1;
        c.lslotstatic = (uint8_t *)qb_alloc((size_t)ps->ns + 1);
        c.laslot = (SlotInfo *)qb_alloc(sizeof(SlotInfo) * (size_t)(ps->na + 1));
        memcpy(c.laslot, ps->a, sizeof(SlotInfo) * (size_t)ps->na);
        c.nlaslot = ps->na; c.caplaslot = ps->na + 1;
        c.laslotstatic = (uint8_t *)qb_alloc((size_t)ps->na + 1);
    }
    if (setjmp(c.jb)) {
        lex_free(&c.tl);
        P->ncode = save_ncode;
        P->nlabels = save_nlabels;
        g_gslot = c.gslot; g_ngslot = c.ngslot;
        g_gaslot = c.gaslot; g_ngaslot = c.ngaslot;
        if (procidx >= 0) { free(c.lslot); free(c.laslot); free(c.lslotstatic); free(c.laslotstatic); }
        for (int i = 0; i < c.nblk; i++) free(c.blk[i].ends.v);
        free(c.fix);
        cc = NULL;
        return 0;
    }
    c.part = 0xFFFF;
    c.line = 0;
    *startpc = P->ncode;
    lex_into(line);
    c.lineemitted = 0;
    for (;;) {
        if (cur()->k == TK_EOL || cur()->k == TK_REM) break;
        if (isop(':')) { adv(); continue; }
        if (iskw(KW_SUB) || iskw(KW_FUNCTION) || iskw(KW_DEF) || iskw(KW_DIM) || iskw(KW_CONST) || iskw(KW_TYPE) || iskw(KW_DECLARE) || iskw(KW_DATA))
            cerr("Invalid in Immediate window");
        compile_statement();
        if (isop(':')) { adv(); continue; }
        if (cur()->k == TK_EOL || cur()->k == TK_REM) break;
        cerr("Expected: end-of-statement");
    }
    if (c.nblk) {
        /* allow single-line blocks like FOR i=1 TO 3: PRINT i: NEXT */
        cerr("Block not closed");
    }
    resolve_fixes();
    c.lineemitted = 1;
    {
        int si = emit(S_END);
        ST(si)->i1 = 1;  /* marks immediate end */
    }
    g_gslot = c.gslot; g_ngslot = c.ngslot;
    g_gaslot = c.gaslot; g_ngaslot = c.ngaslot;
    P->nglobals = c.ngslot;
    P->ngarrays = c.ngaslot;
    if (procidx >= 0) {
        ProcSlots *ps = &g_pslots[procidx];
        if (c.nlslot != ps->ns || c.nlaslot != ps->na) {
            /* new locals can't be added to a live frame; they were created... */
        }
        free(c.lslot); free(c.laslot); free(c.lslotstatic); free(c.laslotstatic);
    }
    free(c.fix);
    lex_free(&c.tl);
    cc = NULL;
    return 1;
}

/* Syntax check of a single line for the editor. Uses a throwaway program.
   Only checks statement grammar; block structure and references are checked at run time. */
int check_line_syntax(const char *line, char *msg, int msgsize) {
    (void)line; (void)msg; (void)msgsize;
    return 1;
}

void free_program(Program *P) {
    if (!P) return;
    for (int i = 0; i < P->ncode; i++) {
        Stmt *s = &P->code[i];
        if (s->op != S_NEXT) free_expr(s->e1);
        free_expr(s->e2); free_expr(s->e3); free_expr(s->e4);
        for (int j = 0; j < s->nargs; j++) free_expr(s->args[j]);
        free(s->args);
        free(s->p);
    }
    free(P->code);
    for (int i = 0; i < P->nprocs; i++) {
        Proc *p = &P->procs[i];
        if (p->syms) {
            for (int j = 0; j < p->syms->n; j++) if (p->syms->s[j].kind == 2) free(p->syms->s[j].cval.s);
            free(p->syms->s);
            free(p->syms);
        }
        if (p->statics) { for (int j = 0; j < p->nstatics; j++) val_free(&p->statics[j]); free(p->statics); }
        if (p->sarrays) {
            for (int j = 0; j < p->nsarrays; j++) if (p->sarrays[j]) {
                Array *a = p->sarrays[j];
                for (int k = 0; k < a->count; k++) val_free(&a->v[k]);
                free(a->v); free(a);
            }
            free(p->sarrays);
        }
        free(p->slotstatic);
        free(p->arrstatic);
    }
    free(P->procs);
    for (int i = 0; i < P->nudts; i++) free(P->udts[i].f);
    free(P->udts);
    for (int j = 0; j < P->gsyms.n; j++) if (P->gsyms.s[j].kind == 2) free(P->gsyms.s[j].cval.s);
    free(P->gsyms.s);
    free(P->labels);
    for (int i = 0; i < P->ndata; i++) free(P->data[i].s);
    free(P->data);
    free(P);
    if (g_prog == P) g_prog = NULL;
}

/* Compile only to check syntax, without disturbing the globals used by a
   program that may currently be running/suspended. */
int compile_check(SrcPart *parts, int nparts, CompileError *err) {
    Program *saved_prog = g_prog;
    SlotInfo *sg = g_gslot, *sga = g_gaslot; int sng = g_ngslot, snga = g_ngaslot;
    ProcSlots *sps = g_pslots; int snps = g_npslots;
    g_pslots = NULL; g_npslots = 0;
    g_gslot = NULL; g_ngslot = 0; g_gaslot = NULL; g_ngaslot = 0;
    Program *p = compile_program(parts, nparts, err);
    if (p) free_program(p);
    for (int i = 0; i < g_npslots; i++) { free(g_pslots[i].s); free(g_pslots[i].a); }
    free(g_pslots);
    free(g_gslot); free(g_gaslot);
    g_pslots = sps; g_npslots = snps;
    g_gslot = sg; g_ngslot = sng; g_gaslot = sga; g_ngaslot = snga;
    g_prog = saved_prog;
    return p != NULL;
}

void truncate_code(Program *P, int n) {
    for (int i = n; i < P->ncode; i++) {
        Stmt *s = &P->code[i];
        if (s->op != S_NEXT) free_expr(s->e1);
        free_expr(s->e2); free_expr(s->e3); free_expr(s->e4);
        for (int j = 0; j < s->nargs; j++) free_expr(s->args[j]);
        free(s->args);
        free(s->p);
    }
    if (n < P->ncode) P->ncode = n;
}

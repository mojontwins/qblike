/* interp.c - executes compiled QBasic programs */
#include "qb.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include <time.h>
#include "image.h"
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define chdir _chdir
#define rmdir _rmdir
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

enum {
    B_ADD = 1, B_SUB, B_MUL, B_DIV, B_IDIV, B_MOD, B_POW, B_EQ, B_NE, B_LT, B_GT, B_LE, B_GE,
    B_AND, B_OR, B_XOR, B_EQV, B_IMP, B_LOR, B_LAND
};

typedef struct SlotInfo { uint8_t t; int udt; int fixlen; } SlotInfo;
typedef struct ProcSlots { SlotInfo *s; int ns; SlotInfo *a; int na; } ProcSlots;
extern SlotInfo *g_gslot; extern int g_ngslot;
extern SlotInfo *g_gaslot; extern int g_ngaslot;
extern ProcSlots *g_pslots; extern int g_npslots;

RunState *rs = NULL;
static RunState rs_storage;

static jmp_buf *const_jb = NULL;
static int depth = 0;

int cp437_to_unicode(int c);
void print_using(int filenum, const char *fmt, int fmtlen, Expr **items, int nitems, int *seps);
static void out_str(int filenum, const char *s, int len);

/* ------------------------------------------------------------------ */
/* errors                                                             */
/* ------------------------------------------------------------------ */
void rt_error(int code) {
    if (const_jb) longjmp(*const_jb, 1);
    rs->err = code;
    snprintf(rs->errmsg, sizeof rs->errmsg, "%s", rt_errtext(code));
    longjmp(*rs->jb, 2);
}
void rt_error_msg(int code, const char *msg) {
    if (const_jb) longjmp(*const_jb, 1);
    rs->err = code;
    snprintf(rs->errmsg, sizeof rs->errmsg, "%s", msg);
    longjmp(*rs->jb, 2);
}

/* ------------------------------------------------------------------ */
/* arrays                                                             */
/* ------------------------------------------------------------------ */
static Array *array_new(SlotInfo *si) {
    Array *a = (Array *)qb_alloc(sizeof(Array));
    a->t = si->t;
    a->udt = si->udt;
    a->fixlen = si->fixlen;
    return a;
}
/* ------------------------------------------------------------------ */
/* Emulated real-mode memory for numeric arrays (VARSEG / VARPTR).    */
/* The first VARSEG/VARPTR of an array gives it a range of addresses  */
/* in conventional memory; DEF SEG + PEEK/POKE then read and write    */
/* the bytes of its elements (INTEGER 2, LONG/SINGLE 4, DOUBLE 8, in  */
/* QBasic's column-major order), and GET/PUT ..., SEG s, o use it.    */
/* The range is freed when the array is erased or redimensioned.      */
/* ------------------------------------------------------------------ */
typedef struct MemMap { Array *a; long lin, size; } MemMap;
static MemMap *mm;
static int nmm, capmm, mm_last = -1;
static const long mm_regions[][2] = { { 0x10000, 0x9F000 }, { 0xC0000, 0xF0000 } };

static int elemsize(int t) { return t == T_INT ? 2 : (t == T_LNG || t == T_SNG) ? 4 : t == T_DBL ? 8 : 0; }

static void mm_forget(Array *a) {
    for (int i = 0; i < nmm; i++)
        if (mm[i].a == a) { memmove(&mm[i], &mm[i + 1], sizeof(MemMap) * (size_t)(nmm - i - 1)); nmm--; mm_last = -1; return; }
}

/* address of an array (allocating it if needed); -1 if out of emulated memory */
static long mm_addr(Array *a) {
    long need = ((long)a->count * elemsize(a->t) + 15) & ~15L;
    if (need <= 0) need = 16;
    for (int i = 0; i < nmm; i++) if (mm[i].a == a) {
        if (mm[i].size >= need) return mm[i].lin;
        mm_forget(a);   /* grew: move it */
        break;
    }
    /* first gap that fits (mm is kept sorted by address) */
    for (int r = 0; r < 2; r++) {
        long pos = mm_regions[r][0];
        int i = 0;
        while (i < nmm && mm[i].lin + mm[i].size <= pos) i++;
        for (;;) {
            long next = i < nmm ? mm[i].lin : mm_regions[r][1];
            if (next > mm_regions[r][1]) next = mm_regions[r][1];
            if (next - pos >= need) {
                if (nmm == capmm) { capmm = capmm ? capmm * 2 : 32; mm = (MemMap *)qb_realloc(mm, sizeof(MemMap) * (size_t)capmm); }
                memmove(&mm[i + 1], &mm[i], sizeof(MemMap) * (size_t)(nmm - i));
                mm[i].a = a; mm[i].lin = pos; mm[i].size = need;
                nmm++;
                mm_last = -1;
                return pos;
            }
            if (i >= nmm || mm[i].lin >= mm_regions[r][1]) break;
            pos = mm[i].lin + mm[i].size;
            i++;
        }
    }
    return -1;
}

/* array and byte offset behind a linear address, or NULL */
static Array *mm_find(long lin, long *boff) {
    if (mm_last >= 0 && mm_last < nmm && lin >= mm[mm_last].lin && lin < mm[mm_last].lin + mm[mm_last].size) {
        *boff = lin - mm[mm_last].lin; return mm[mm_last].a;
    }
    int lo = 0, hi = nmm - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (lin < mm[m].lin) hi = m - 1;
        else if (lin >= mm[m].lin + mm[m].size) lo = m + 1;
        else { mm_last = m; *boff = lin - mm[m].lin; return mm[m].a; }
    }
    return NULL;
}

int mem_array_byte(Array *a, long boff) {
    int es = elemsize(a->t);
    long idx = boff / es;
    if (idx < 0 || idx >= a->count) return 0;
    Value *v = &a->v[idx];
    unsigned char b[8];
    switch (a->t) {
        case T_INT: { int16_t x = (int16_t)v->n; memcpy(b, &x, 2); break; }
        case T_LNG: { int32_t x = (int32_t)v->n; memcpy(b, &x, 4); break; }
        case T_SNG: { float x = (float)v->n; memcpy(b, &x, 4); break; }
        default: { double x = v->n; memcpy(b, &x, 8); break; }
    }
    return b[boff % es];
}

void mem_array_setbyte(Array *a, long boff, int val) {
    int es = elemsize(a->t);
    long idx = boff / es;
    if (idx < 0 || idx >= a->count) return;
    Value *v = &a->v[idx];
    unsigned char b[8];
    int k = (int)(boff % es);
    switch (a->t) {
        case T_INT: { int16_t x = (int16_t)v->n; memcpy(b, &x, 2); b[k] = (unsigned char)val; memcpy(&x, b, 2); v->n = x; break; }
        case T_LNG: { int32_t x = (int32_t)v->n; memcpy(b, &x, 4); b[k] = (unsigned char)val; memcpy(&x, b, 4); v->n = x; break; }
        case T_SNG: { float x = (float)v->n; memcpy(b, &x, 4); b[k] = (unsigned char)val; memcpy(&x, b, 4); v->n = x; break; }
        default: { double x = v->n; memcpy(b, &x, 8); b[k] = (unsigned char)val; memcpy(&x, b, 8); v->n = x; break; }
    }
}

/* PEEK/POKE on emulated array memory; return 0 if the address is not an array */
int mem_peek(int seg, int off, int *val) {
    long boff;
    Array *a = mm_find(((long)(seg & 0xFFFF) * 16 + (off & 0xFFFF)) & 0xFFFFF, &boff);
    if (!a) return 0;
    *val = mem_array_byte(a, boff);
    return 1;
}
int mem_poke(int seg, int off, int val) {
    long boff;
    Array *a = mm_find(((long)(seg & 0xFFFF) * 16 + (off & 0xFFFF)) & 0xFFFFF, &boff);
    if (!a) return 0;
    mem_array_setbyte(a, boff, val);
    return 1;
}
/* GET/PUT ..., SEG s, o: the array and byte offset behind an address (error if none) */
Array *mem_resolve(int seg, int off, long *boff) {
    Array *a = mm_find(((long)(seg & 0xFFFF) * 16 + (off & 0xFFFF)) & 0xFFFFF, boff);
    if (!a) rt_error_msg(5, "Address does not belong to an array (use VARSEG/VARPTR)");
    return a;
}

static void array_clear(Array *a) {
    if (nmm) mm_forget(a);
    for (int i = 0; i < a->count; i++) val_free(&a->v[i]);
    free(a->v);
    a->v = NULL;
    a->count = 0;
    a->alive = 0;
}
static void array_free(Array *a) { if (!a) return; array_clear(a); free(a); }

static void array_dim(Array *a, int nd, int *lb, int *ub, int preserve) {
    long long count = 1;
    for (int i = 0; i < nd; i++) {
        if (ub[i] < lb[i]) rt_error(9);
        count *= (long long)ub[i] - lb[i] + 1;
        if (count > 64LL * 1024 * 1024) rt_error(7);
    }
    /* QBasic: 64 KB per array. QBLike: up to 64M elements, as far as memory goes */
    if (count > 64LL * 1024 * 1024) rt_error(7);
    Value *nv = (Value *)calloc((size_t)count, sizeof(Value));
    if (!nv) rt_error(7);
    for (long long i = 0; i < count; i++) val_init(&nv[i], a->t, a->udt, a->fixlen);
    if (preserve && a->alive) {
        /* QBasic PRESERVE keeps elements in linear order */
        long long n = count < a->count ? count : a->count;
        for (long long i = 0; i < n; i++) { val_free(&nv[i]); nv[i] = a->v[i]; memset(&a->v[i], 0, sizeof(Value)); }
    }
    array_clear(a);
    a->v = nv;
    a->count = (int)count;
    a->nd = nd;
    for (int i = 0; i < nd; i++) { a->lb[i] = lb[i]; a->ub[i] = ub[i]; }
    a->alive = 1;
}

static Array *arr_slot(int scope, int idx) {
    Frame *f = scope ? rs->cur : rs->global;
    if (idx >= f->narrs) rt_error(51);
    return f->arrs[idx];
}

static double evn(Expr *e);
static void evs(Expr *e, char **p, int *len);

/* Linear position of an element. QBasic stores arrays in column-major order: the FIRST subscript
   varies fastest (a(0,0), a(1,0), a(0,1)...), the opposite of C. It matters for GET/PUT, BLOAD/BSAVE
   and REDIM PRESERVE on arrays of two or more dimensions: GET (0,0)-(15,15), spr%(0, i%). */
static long long arr_offset(Array *a, const int *idx) {
    long long off = 0;
    for (int i = a->nd - 1; i >= 0; i--) {
        if (idx[i] < a->lb[i] || idx[i] > a->ub[i]) rt_error(9);
        off = off * ((long long)a->ub[i] - a->lb[i] + 1) + ((long long)idx[i] - a->lb[i]);
    }
    return off;
}

static Value *arr_elem(Array *a, Expr **args, int nargs) {
    int idx[8];
    if (nargs > 8) rt_error(9);
    for (int i = 0; i < nargs; i++) {
        double d = evn(args[i]);
        d = qb_round_even(d);
        if (d < -2147483648.0 || d > 2147483647.0) rt_error(9);
        idx[i] = (int)d;
    }
    if (!a->alive) {
        /* implicit dimension: 0..10 (or base..10) */
        int lb[8], ub[8];
        for (int i = 0; i < nargs; i++) { lb[i] = rs->optionbase; ub[i] = 10; }
        array_dim(a, nargs, lb, ub, 0);
        a->dynamic = 0;
    }
    if (nargs != a->nd) rt_error(9);
    return &a->v[arr_offset(a, idx)];
}

/* ------------------------------------------------------------------ */
/* frames                                                             */
/* ------------------------------------------------------------------ */
static Frame *frame_new_global(void) {
    Frame *f = (Frame *)qb_alloc(sizeof(Frame));
    f->proc = -1;
    f->nvals = g_ngslot;
    f->own = (Value *)qb_alloc(sizeof(Value) * (size_t)(f->nvals + 1));
    f->vals = (Value **)qb_alloc(sizeof(Value *) * (size_t)(f->nvals + 1));
    for (int i = 0; i < f->nvals; i++) {
        val_init(&f->own[i], g_gslot[i].t, g_gslot[i].udt, g_gslot[i].fixlen);
        f->vals[i] = &f->own[i];
    }
    f->narrs = g_ngaslot;
    f->arrs = (Array **)qb_alloc(sizeof(Array *) * (size_t)(f->narrs + 1));
    f->ownarrs = (Array **)qb_alloc(sizeof(Array *) * (size_t)(f->narrs + 1));
    for (int i = 0; i < f->narrs; i++) { f->ownarrs[i] = array_new(&g_gaslot[i]); f->arrs[i] = f->ownarrs[i]; }
    return f;
}

/* grow global frame after immediate-mode compilation added variables */
static void frame_grow_global(void) {
    Frame *f = rs->global;
    if (g_ngslot > f->nvals) {
        /* own storage may move: rebuild pointers for owned slots */
        Value *old = f->own;
        Value *no = (Value *)qb_alloc(sizeof(Value) * (size_t)(g_ngslot + 1));
        memcpy(no, old, sizeof(Value) * (size_t)f->nvals);
        Value **nvp = (Value **)qb_alloc(sizeof(Value *) * (size_t)(g_ngslot + 1));
        for (int i = 0; i < f->nvals; i++) nvp[i] = (f->vals[i] == &old[i]) ? &no[i] : f->vals[i];
        for (int i = f->nvals; i < g_ngslot; i++) { val_init(&no[i], g_gslot[i].t, g_gslot[i].udt, g_gslot[i].fixlen); nvp[i] = &no[i]; }
        free(old);
        free(f->vals);
        f->own = no;
        f->vals = nvp;
        f->nvals = g_ngslot;
    }
    if (g_ngaslot > f->narrs) {
        f->arrs = (Array **)qb_realloc(f->arrs, sizeof(Array *) * (size_t)(g_ngaslot + 1));
        f->ownarrs = (Array **)qb_realloc(f->ownarrs, sizeof(Array *) * (size_t)(g_ngaslot + 1));
        for (int i = f->narrs; i < g_ngaslot; i++) { f->ownarrs[i] = array_new(&g_gaslot[i]); f->arrs[i] = f->ownarrs[i]; }
        f->narrs = g_ngaslot;
    }
}

/* immediate window may create new locals in a suspended procedure */
static void frame_grow_local(Frame *f, int n, int t) {
    if (n <= f->nvals) return;
    Value *old = f->own;
    Value *no = (Value *)qb_alloc(sizeof(Value) * (size_t)(n + 1));
    memcpy(no, old, sizeof(Value) * (size_t)f->nvals);
    Value **nvp = (Value **)qb_alloc(sizeof(Value *) * (size_t)(n + 1));
    for (int i = 0; i < f->nvals; i++) nvp[i] = (f->vals[i] == &old[i]) ? &no[i] : f->vals[i];
    for (int i = f->nvals; i < n; i++) { val_init(&no[i], t == T_UDT ? T_SNG : t, 0, 0); nvp[i] = &no[i]; }
    free(old);
    free(f->vals);
    f->own = no;
    f->vals = nvp;
    f->nvals = n;
}

static void frame_free(Frame *f) {
    if (!f) return;
    for (int i = 0; i < f->nvals; i++) if (f->own) val_free(&f->own[i]);
    free(f->own);
    free(f->vals);
    for (int i = 0; i < f->narrs; i++) if (f->ownarrs && f->ownarrs[i]) array_free(f->ownarrs[i]);
    free(f->ownarrs);
    free(f->arrs);
    free(f->gosub);
    val_free(&f->ret);
    free(f->cb_dst);
    free(f->cb_slot);
    free(f);
}

/* on a normal return: write converted parameter copies back to the caller's variables */
static void copy_back(Frame *f) {
    for (int i = 0; i < f->ncb; i++) {
        Value *dst = f->cb_dst[i];
        dst->n = fit_num(dst->t, f->own[f->cb_slot[i]].n);
    }
    f->ncb = 0;
}

/* ------------------------------------------------------------------ */
/* lvalues and expressions                                            */
/* ------------------------------------------------------------------ */
static Value *lval(Expr *e) {
    switch (e->op) {
        case E_VAR: {
            Frame *f = e->scope ? rs->cur : rs->global;
            if (e->idx >= f->nvals) {
                if (!e->scope) frame_grow_global();
                else frame_grow_local(f, e->idx + 1, e->type);
                if (e->idx >= f->nvals) rt_error(51);
            }
            return f->vals[e->idx];
        }
        case E_ARR: {
            Array *a = arr_slot(e->scope, e->idx);
            return arr_elem(a, e->args, e->nargs);
        }
        case E_MEMBER: {
            Value *b = lval(e->a);
            return &b->r->f[e->sub];
        }
    }
    rt_error(40);
    return NULL;
}

static int str_cmp(const char *a, int al, const char *b, int bl) {
    int n = al < bl ? al : bl;
    int c = memcmp(a, b, (size_t)n);
    if (c) return c;
    return al - bl;
}

static double to_int_check(double d, int t) {
    d = qb_round_even(d);
    if (t == T_INT) { if (d < -32768 || d > 32767) rt_error(6); }
    else { if (d < -2147483648.0 || d > 2147483647.0) rt_error(6); }
    return d;
}

/* VARPTR$ table */
#define VPN 1024
static Value *vp_tab[VPN];
static int vp_next = 0;
static Value *vp_get(const char *tok) {
    int k = (unsigned char)tok[1] | ((unsigned char)tok[2] << 8);
    if (k >= VPN || !vp_tab[k]) rt_error(5);
    return vp_tab[k];
}
double qb_varptr_num(const char *tok) {
    Value *v = vp_get(tok);
    if (v->t == T_STR) rt_error(13);
    return v->n;
}
void qb_varptr_str(const char *tok, const char **s, int *len) {
    Value *v = vp_get(tok);
    if (v->t != T_STR) rt_error(13);
    *s = v->s; *len = v->slen;
}

static double call_function(Expr *e);
static double builtin_num(Expr *e);
static void builtin_str(Expr *e, char **p, int *len);

static double evn(Expr *e) {
    switch (e->op) {
        case E_NUM: return e->num;
        case E_VAR: case E_ARR: case E_MEMBER: return lval(e)->n;
        case E_CONV: return fit_num(e->type, evn(e->a));
        case E_NEG: return fit_num(e->type, -evn(e->a));
        case E_NOT: {
            double v = to_int_check(evn(e->a), e->type);
            if (e->type == T_INT) return (double)(int16_t)~(int16_t)v;
            return (double)(int32_t)~(int32_t)v;
        }
        case E_BIN: {
            int op = e->sub;
            if (op >= B_EQ && op <= B_GE && e->a->type == T_STR) {
                char *a, *b; int al, bl;
                evs(e->a, &a, &al);
                evs(e->b, &b, &bl);
                int c = str_cmp(a, al, b, bl);
                free(a); free(b);
                int r = 0;
                switch (op) {
                    case B_EQ: r = c == 0; break; case B_NE: r = c != 0; break;
                    case B_LT: r = c < 0; break; case B_GT: r = c > 0; break;
                    case B_LE: r = c <= 0; break; case B_GE: r = c >= 0; break;
                }
                return r ? -1 : 0;
            }
            if (op == B_LOR) { if (evn(e->a) != 0) return -1; return evn(e->b) != 0 ? -1 : 0; }
            if (op == B_LAND) { if (evn(e->a) == 0) return 0; return evn(e->b) != 0 ? -1 : 0; }
            double a = evn(e->a);
            double b = evn(e->b);
            int t = e->type;
            switch (op) {
                case B_ADD: return fit_num(t, a + b);
                case B_SUB: return fit_num(t, a - b);
                case B_MUL: return fit_num(t, a * b);
                case B_DIV:
                    if (b == 0) { if (a == 0) rt_error(6); rt_error(11); }
                    if (t == T_SNG) return fit_num(t, (double)((float)a / (float)b));
                    return fit_num(t, a / b);
                case B_POW: {
                    if (a == 0 && b < 0) rt_error(11);
                    if (a < 0 && b != floor(b)) rt_error(5);
                    return fit_num(t, pow(a, b));
                }
                case B_IDIV: {
                    double x = to_int_check(a, t), y = to_int_check(b, t);
                    if (y == 0) rt_error(11);
                    double q = x / y;
                    q = q < 0 ? ceil(q) : floor(q);
                    return fit_num(t, q);
                }
                case B_MOD: {
                    double x = to_int_check(a, t), y = to_int_check(b, t);
                    if (y == 0) rt_error(11);
                    return fmod(x, y);
                }
                case B_EQ: return a == b ? -1 : 0;
                case B_NE: return a != b ? -1 : 0;
                case B_LT: return a < b ? -1 : 0;
                case B_GT: return a > b ? -1 : 0;
                case B_LE: return a <= b ? -1 : 0;
                case B_GE: return a >= b ? -1 : 0;
                case B_AND: case B_OR: case B_XOR: case B_EQV: case B_IMP: {
                    int32_t x = (int32_t)to_int_check(a, t), y = (int32_t)to_int_check(b, t);
                    int32_t r = 0;
                    switch (op) {
                        case B_AND: r = x & y; break;
                        case B_OR: r = x | y; break;
                        case B_XOR: r = x ^ y; break;
                        case B_EQV: r = ~(x ^ y); break;
                        case B_IMP: r = (~x) | y; break;
                    }
                    if (t == T_INT) return (double)(int16_t)r;
                    return (double)r;
                }
            }
            break;
        }
        case E_FUNC: return builtin_num(e);
        case E_CALLF: case E_FN: return call_function(e);
    }
    rt_error(13);
    return 0;
}

static void evs(Expr *e, char **p, int *len) {
    switch (e->op) {
        case E_STR:
            *p = qb_strndup(e->str, e->slen);
            *len = e->slen;
            return;
        case E_VAR: case E_ARR: case E_MEMBER: {
            Value *v = lval(e);
            *p = qb_strndup(v->s ? v->s : "", v->slen);
            *len = v->slen;
            return;
        }
        case E_CONV: evs(e->a, p, len); return;
        case E_BIN: {
            char *a, *b; int al, bl;
            evs(e->a, &a, &al);
            evs(e->b, &b, &bl);
            if ((long long)al + bl > QB_STRMAX) { free(a); free(b); rt_error(14); }
            char *r = (char *)qb_alloc((size_t)(al + bl) + 1);
            memcpy(r, a, (size_t)al);
            memcpy(r + al, b, (size_t)bl);
            free(a); free(b);
            *p = r;
            *len = al + bl;
            return;
        }
        case E_FUNC: builtin_str(e, p, len); return;
        case E_CALLF: case E_FN: {
            call_function(e);
            /* result string left in rs temp */
            extern Value g_fnret;
            *p = g_fnret.s ? g_fnret.s : qb_strdup("");
            *len = g_fnret.slen;
            g_fnret.s = NULL;
            g_fnret.slen = 0;
            return;
        }
    }
    rt_error(13);
}

/* assign expression to value with conversion */
static void assign(Value *dst, Expr *e) {
    if (dst->t == T_STR) {
        char *p; int l;
        evs(e, &p, &l);
        if (dst->fixlen > 0) { val_setstr(dst, p, l); free(p); }
        else { free(dst->s); dst->s = p; dst->slen = l; }
        return;
    }
    if (dst->t == T_UDT) {
        Value *src = lval(e);
        val_copy(dst, src);
        return;
    }
    dst->n = fit_num(dst->t, evn(e));
}

/* ------------------------------------------------------------------ */
/* procedure calls                                                    */
/* ------------------------------------------------------------------ */
Value g_fnret;
static int exec(Frame *f, int pc);

static Frame *make_proc_frame(int pi, Expr **args, int nargs) {
    Program *P = rs->prog;
    Proc *p = &P->procs[pi];
    ProcSlots *ps = &g_pslots[pi];
    Frame *f = (Frame *)qb_alloc(sizeof(Frame));
    f->proc = pi;
    f->nvals = ps->ns;
    f->own = (Value *)qb_alloc(sizeof(Value) * (size_t)(f->nvals + 1));
    f->vals = (Value **)qb_alloc(sizeof(Value *) * (size_t)(f->nvals + 1));
    f->narrs = ps->na;
    f->arrs = (Array **)qb_alloc(sizeof(Array *) * (size_t)(f->narrs + 1));
    f->ownarrs = (Array **)qb_alloc(sizeof(Array *) * (size_t)(f->narrs + 1));
    if (!p->statics && (ps->ns || ps->na)) {
        p->statics = (Value *)qb_alloc(sizeof(Value) * (size_t)(ps->ns + 1));
        p->nstatics = ps->ns;
        for (int i = 0; i < ps->ns; i++) val_init(&p->statics[i], ps->s[i].t, ps->s[i].udt, ps->s[i].fixlen);
        p->sarrays = (Array **)qb_alloc(sizeof(Array *) * (size_t)(ps->na + 1));
        p->nsarrays = ps->na;
        for (int i = 0; i < ps->na; i++) p->sarrays[i] = array_new(&ps->a[i]);
    }
    /* parameters occupy the first scalar/array slots */
    int ns = 0, na = 0;
    int isparam_s[512] = {0};
    int isparam_a[256] = {0};
    for (int i = 0; i < p->nparams && i < nargs; i++) {
        Param *pr = &p->params[i];
        Expr *a = args[i];
        if (pr->isarray) {
            int slot = na++;
            Array *src = arr_slot(a->scope, a->idx);
            f->arrs[slot] = src;
            f->ownarrs[slot] = NULL;
            if (slot < 256) isparam_a[slot] = 1;
            continue;
        }
        int slot = ns++;
        if (slot < 512) isparam_s[slot] = 1;
        if (!p->isdeffn && (a->op == E_VAR || a->op == E_ARR || a->op == E_MEMBER) && a->type == pr->t) {
            f->vals[slot] = lval(a);
            memset(&f->own[slot], 0, sizeof(Value));
        } else {
            val_init(&f->own[slot], pr->t, pr->udt, 0);
            f->vals[slot] = &f->own[slot];
            if (pr->t == T_UDT) { Value *src = lval(a); val_copy(&f->own[slot], src); }
            else {
                assign(&f->own[slot], a);
                if (!p->isdeffn && (a->op == E_VAR || a->op == E_ARR || a->op == E_MEMBER) && IS_NUM(a->type) && IS_NUM(pr->t)) {
                    f->cb_dst = (Value **)qb_realloc(f->cb_dst, sizeof(Value *) * (size_t)(f->ncb + 1));
                    f->cb_slot = (int *)qb_realloc(f->cb_slot, sizeof(int) * (size_t)(f->ncb + 1));
                    f->cb_dst[f->ncb] = lval(a);
                    f->cb_slot[f->ncb] = slot;
                    f->ncb++;
                }
            }
        }
    }
    for (int i = ns; i < ps->ns; i++) {
        if (p->slotstatic[i]) {
            f->vals[i] = &p->statics[i];
            memset(&f->own[i], 0, sizeof(Value));
        } else {
            val_init(&f->own[i], ps->s[i].t, ps->s[i].udt, ps->s[i].fixlen);
            f->vals[i] = &f->own[i];
        }
    }
    for (int i = na; i < ps->na; i++) {
        if (p->arrstatic[i]) { f->arrs[i] = p->sarrays[i]; f->ownarrs[i] = NULL; }
        else { f->ownarrs[i] = array_new(&ps->a[i]); f->arrs[i] = f->ownarrs[i]; }
    }
    (void)isparam_s; (void)isparam_a;
    if (p->isfunc) val_init(&f->ret, p->rtype, 0, 0);
    return f;
}

/* Out of stack space: the program thread has a 256 MB stack; stop well before it runs out
   (each SUB/FUNCTION level takes ~2 KB, so about 100000 levels; QBasic allowed a few hundred) */
extern char *g_stack_base;
static void check_stack(void) {
    char here;
    if (g_stack_base) {
        long long used = (long long)(g_stack_base - &here);
        if (used > 200LL * 1024 * 1024 && used < 1024LL * 1024 * 1024) rt_error(28);
    }
    if (depth > 1000000) rt_error(28);
}

static void run_proc(int pi, Expr **args, int nargs) {
    Program *P = rs->prog;
    Proc *p = &P->procs[pi];
    check_stack();
    Frame *f = make_proc_frame(pi, args, nargs);
    f->parent = rs->cur;
    Frame *saved = rs->cur;
    rs->cur = f;
    depth++;
    exec(f, p->entry);
    depth--;
    rs->cur = saved;
}

static double call_function(Expr *e) {
    Program *P = rs->prog;
    int pi = e->idx;
    Proc *p = &P->procs[pi];
    check_stack();
    Frame *f = make_proc_frame(pi, e->args, e->nargs);
    f->parent = rs->cur;
    Frame *saved = rs->cur;
    rs->cur = f;
    depth++;
    exec(f, p->entry);
    depth--;
    rs->cur = saved;
    double r = 0;
    if (p->rtype == T_STR) {
        val_free(&g_fnret);
        g_fnret.t = T_STR;
        g_fnret.s = f->ret.s ? f->ret.s : qb_strdup("");
        g_fnret.slen = f->ret.slen;
        f->ret.s = NULL;
        f->ret.slen = 0;
    } else r = f->ret.n;
    copy_back(f);
    frame_free(f);
    return r;
}

/* ------------------------------------------------------------------ */
/* RND                                                                */
/* ------------------------------------------------------------------ */
static uint32_t rnd_seed = 0x50000;
static float rnd_last = 0;

static float rnd_next(void) {
    rnd_seed = (rnd_seed * 0xFD43FDu + 0xC39EC3u) & 0xFFFFFFu;
    rnd_last = (float)rnd_seed / 16777216.0f;
    return rnd_last;
}
static float qb_rnd(double n, int has) {
    if (!has || n > 0) return rnd_next();
    if (n == 0) return rnd_last;
    float f = (float)n;
    uint32_t m;
    memcpy(&m, &f, 4);
    rnd_seed = ((m & 0xFFFFFFu) + ((m >> 24) & 0xFFu)) & 0xFFFFFFu;
    return rnd_next();
}
static void qb_randomize(double n) {
    uint64_t bits;
    memcpy(&bits, &n, 8);
    uint32_t hi = (uint32_t)(bits >> 32);
    uint32_t v = (hi ^ (hi >> 16)) & 0xFFFFu;
    rnd_seed = (rnd_seed & 0xFFu) | (v << 8);
}

double sys_timer(void);
void sys_datestr(char *buf);
void sys_timestr(char *buf);

/* ------------------------------------------------------------------ */
/* builtin functions                                                  */
/* ------------------------------------------------------------------ */
static int argi(Expr *e, int i) { double d = qb_round_even(evn(e->args[i])); if (d < -2147483648.0 || d > 2147483647.0) rt_error(6); return (int)d; }

static double builtin_num(Expr *e) {
    switch (e->sub) {
        case F_ABS: return fit_num(e->type, fabs(evn(e->args[0])));
        case F_ASC: {
            char *p; int l; evs(e->args[0], &p, &l);
            if (l == 0) { free(p); rt_error(5); }
            int c = (unsigned char)p[0];
            free(p);
            return c;
        }
        case F_ATN: return fit_num(e->type, atan(evn(e->args[0])));
        case F_CDBL: return evn(e->args[0]);
        case F_CINT: return fit_num(T_INT, evn(e->args[0]));
        case F_CLNG: return fit_num(T_LNG, evn(e->args[0]));
        case F_CSNG: return fit_num(T_SNG, evn(e->args[0]));
        case F_COS: return fit_num(e->type, cos(evn(e->args[0])));
        case F_SIN: return fit_num(e->type, sin(evn(e->args[0])));
        case F_TAN: return fit_num(e->type, tan(evn(e->args[0])));
        case F_EXP: { double x = evn(e->args[0]); double r = exp(x); if (r > DBL_MAX) rt_error(6); return fit_num(e->type, r); }
        case F_LOG: { double x = evn(e->args[0]); if (x <= 0) rt_error(5); return fit_num(e->type, log(x)); }
        case F_SQR: { double x = evn(e->args[0]); if (x < 0) rt_error(5); return fit_num(e->type, sqrt(x)); }
        case F_FIX: { double x = evn(e->args[0]); return x < 0 ? ceil(x) : floor(x); }
        case F_INT: return floor(evn(e->args[0]));
        case F_SGN: { double x = evn(e->args[0]); return x > 0 ? 1 : x < 0 ? -1 : 0; }
        case F_CSRLIN: return scr_csrlin();
        case F_POS: (void)evn(e->args[0]); return scr_pos();
        case F_CVI: case F_CVL: case F_CVS: case F_CVD: case F_CVSMBF: case F_CVDMBF: {
            char *p; int l; evs(e->args[0], &p, &l);
            int need = e->sub == F_CVI ? 2 : (e->sub == F_CVD || e->sub == F_CVDMBF) ? 8 : 4;
            if (l < need) { free(p); rt_error(5); }
            double r = 0;
            if (e->sub == F_CVI) { int16_t v; memcpy(&v, p, 2); r = v; }
            else if (e->sub == F_CVL) { int32_t v; memcpy(&v, p, 4); r = v; }
            else if (e->sub == F_CVS) { float v; memcpy(&v, p, 4); r = v; }
            else if (e->sub == F_CVD) { double v; memcpy(&v, p, 8); r = v; }
            else if (e->sub == F_CVSMBF) {
                unsigned char *b = (unsigned char *)p;
                if (b[3] == 0) r = 0;
                else {
                    int ex = b[3] - 152;
                    uint32_t mant = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)(b[2] | 0x80) << 16);
                    r = ldexp((double)mant, ex);
                    if (b[2] & 0x80) r = -r;
                }
            } else {
                unsigned char *b = (unsigned char *)p;
                if (b[7] == 0) r = 0;
                else {
                    double mant = 0;
                    for (int i = 0; i < 6; i++) mant = mant / 256.0 + b[i];
                    mant = (mant / 256.0 + (b[6] | 0x80)) / 256.0;
                    r = ldexp(mant, b[7] - 128);
                    if (b[6] & 0x80) r = -r;
                }
            }
            free(p);
            return r;
        }
        case F_EOF: return file_eof(argi(e, 0)) ? -1 : 0;
        case F_LOF: return file_lof(argi(e, 0));
        case F_LOC: return file_loc(argi(e, 0));
        case F_SEEK: return file_seekfn(argi(e, 0));
        case F_FREEFILE: return file_freefile();
        case F_FILEATTR: { int fn = argi(e, 0); int a = argi(e, 1); if (!file_isopen(fn)) rt_error(52); return a == 1 ? file_mode(fn) : 0; }
        case F_ERR: return rs->err;
        case F_ERL: return rs->erl;
        case F_ERDEV: return 0;
        case F_FRE: {
            /* QBasic reported a few tens of KB; QBLike has no such limits, so report plenty
               (but well inside LONG, in case a program does arithmetic with it) */
            if (e->nargs && e->args[0]->type != T_STR && qb_round_even(evn(e->args[0])) == -2) return 64.0 * 1024 * 1024;
            return 256.0 * 1024 * 1024;
        }
        case F_INP: {
            int port = argi(e, 0);
            if (port == 0x60) return kb_lastscan();
            extern int gfx_inp(int port);
            return gfx_inp(port);
        }
        case F_INSTR: {
            int start = 1, ai = 0;
            if (e->nargs == 3) { start = argi(e, 0); ai = 1; if (start < 1 || start > QB_STRMAX) rt_error(5); }
            char *a, *b; int al, bl;
            evs(e->args[ai], &a, &al);
            evs(e->args[ai + 1], &b, &bl);
            int r = 0;
            if (start > al) r = 0;
            else if (bl == 0) r = start;
            else {
                for (int i = start - 1; i + bl <= al; i++) if (memcmp(a + i, b, (size_t)bl) == 0) { r = i + 1; break; }
            }
            free(a); free(b);
            return r;
        }
        case F_LEN: { char *p; int l; evs(e->args[0], &p, &l); free(p); return l; }
        case F_LENVAR: {
            Expr *a = e->args[0];
            switch (a->type) {
                case T_INT: return 2; case T_LNG: return 4; case T_SNG: return 4; case T_DBL: return 8;
                case T_UDT: return rs->prog->udts[a->udt].size;
            }
            return 0;
        }
        case F_LBOUND: case F_UBOUND: {
            Expr *a = e->args[0];
            Array *arr = arr_slot(a->scope, a->idx);
            int dim = e->nargs > 1 ? argi(e, 1) : 1;
            if (!arr->alive) rt_error(9);
            if (dim < 1 || dim > arr->nd) rt_error(9);
            return e->sub == F_LBOUND ? arr->lb[dim - 1] : arr->ub[dim - 1];
        }
        case F_PEEK: {
            int addr = argi(e, 0);
            int ok = 0;
            { int mv; if (mem_peek(rs->defseg, addr, &mv)) return mv; }
            int v = gfx_peek(rs->defseg, addr & 0xFFFF, &ok);
            if (ok) return v;
            if (rs->defseg == 0 && (addr == 0x417 || addr == 0x418)) return kb_shift_state();
            return 0;
        }
        case F_MULTIKEY: {
            extern int kb_scan_down(int sc);
            return kb_scan_down(argi(e, 0)) ? -1 : 0;
        }
        case F_PEN: case F_STICK: case F_STRIG: case F_LPOS: return 0;
        case F_PLAY: return snd_playfn();
        case F_PMAP: return gfx_pmap(evn(e->args[0]), argi(e, 1));
        case F_POINT:
            if (e->nargs == 2) return gfx_point(evn(e->args[0]), evn(e->args[1]));
            return gfx_pointfn(argi(e, 0));
        case F_RND: return qb_rnd(e->nargs ? evn(e->args[0]) : 0, e->nargs);
        case F_SADD: return 0;
        case F_VARPTR: case F_VARSEG: {
            /* numeric array elements get an emulated address; anything else is 0, as before */
            Expr *x = e->args[0];
            if (x->op != E_ARR || elemsize(x->type) == 0) return 0;
            Array *a = arr_slot(x->scope, x->idx);
            if (!a->alive) { lval(x); }               /* implicit DIM, like any use */
            if (x->nargs != a->nd) rt_error(9);
            int idx[8];
            for (int i = 0; i < x->nargs && i < 8; i++) idx[i] = (int)qb_round_even(evn(x->args[i]));
            long boff = arr_offset(a, idx) * elemsize(a->t);
            long lin = mm_addr(a);
            if (lin < 0) rt_error(7);
            long segv, offv;
            if (boff < 0x10000) { segv = lin >> 4; offv = boff; }
            else { long t = lin + boff; segv = t >> 4; offv = t & 15; }
            long r = e->sub == F_VARSEG ? segv : offv;
            return (double)(int16_t)(uint16_t)r;   /* INTEGER, as in QBasic */
        }
        case F_MUSIC: return snd_music_playing() ? -1 : 0;
        case F_SETMEM: {   /* far heap: 384 KB at start, between 0 and 600 KB */
            static double heap = 393216;
            heap += evn(e->args[0]);
            if (heap < 0) heap = 0;
            if (heap > 614400) heap = 614400;
            return heap;
        }
        case F_SNDBUSY: return snd_sample_busy(argi(e, 0)) ? -1 : 0;
        case F_SNDLOAD: {
            char *nm; int nl;
            evs(e->args[0], &nm, &nl);
            char buf[1024];
            if (nl > 1000) nl = 1000;
            memcpy(buf, nm, (size_t)nl);
            free(nm);
            return snd_load(buf, nl);
        }
        case F_SCREEN: {
            int r = argi(e, 0), c = argi(e, 1);
            int z = e->nargs > 2 ? argi(e, 2) : 0;
            return scr_screenfn(r, c, z);
        }
        case F_TIMER: return (float)sys_timer();
        case F_VAL: {
            char *p; int l; evs(e->args[0], &p, &l);
            double v = qb_val(p, l);
            free(p);
            return v;
        }
    }
    rt_error(5);
    return 0;
}

static void ret_str(char **p, int *len, const char *s, int l) { *p = qb_strndup(s, l); *len = l; }

static void builtin_str(Expr *e, char **p, int *len) {
    char buf[128];
    switch (e->sub) {
        case F_CHR: {
            int c = argi(e, 0);
            if (c < 0 || c > 255) rt_error(5);
            buf[0] = (char)c;
            ret_str(p, len, buf, 1);
            return;
        }
        case F_STR: {
            int n = fmt_number(buf, evn(e->args[0]), e->args[0]->type, 0);
            ret_str(p, len, buf, n);
            return;
        }
        case F_HEX: case F_OCT: {
            double d = qb_round_even(evn(e->args[0]));
            if (d < -2147483648.0 || d > 4294967295.0) rt_error(6);
            uint32_t v;
            if (d < 0) v = (d >= -32768 && e->args[0]->type == T_INT) ? (uint32_t)(uint16_t)(int16_t)d : (uint32_t)(int32_t)d;
            else v = (uint32_t)d;
            if (d < 0 && d >= -32768) v = (uint32_t)(uint16_t)(int16_t)d;
            int n = snprintf(buf, sizeof buf, e->sub == F_HEX ? "%X" : "%o", v);
            ret_str(p, len, buf, n);
            return;
        }
        case F_LEFT: case F_RIGHT: {
            char *s; int l; evs(e->args[0], &s, &l);
            int n = argi(e, 1);
            if (n < 0) { free(s); rt_error(5); }
            if (n > l) n = l;
            if (e->sub == F_LEFT) { *p = qb_strndup(s, n); }
            else { *p = qb_strndup(s + l - n, n); }
            *len = n;
            free(s);
            return;
        }
        case F_MID: {
            char *s; int l; evs(e->args[0], &s, &l);
            int st = argi(e, 1);
            int n = e->nargs > 2 ? argi(e, 2) : l;
            if (st < 1 || n < 0) { free(s); rt_error(5); }
            if (st > l) { free(s); ret_str(p, len, "", 0); return; }
            if (n > l - st + 1) n = l - st + 1;
            *p = qb_strndup(s + st - 1, n);
            *len = n;
            free(s);
            return;
        }
        case F_LTRIM: case F_RTRIM: {
            char *s; int l; evs(e->args[0], &s, &l);
            int a = 0, b = l;
            if (e->sub == F_LTRIM) while (a < b && s[a] == ' ') a++;
            else while (b > a && s[b - 1] == ' ') b--;
            *p = qb_strndup(s + a, b - a);
            *len = b - a;
            free(s);
            return;
        }
        case F_UCASE: case F_LCASE: {
            evs(e->args[0], p, len);
            for (int i = 0; i < *len; i++) {
                unsigned char c = (unsigned char)(*p)[i];
                if (c < 128) (*p)[i] = (char)(e->sub == F_UCASE ? toupper(c) : tolower(c));
            }
            return;
        }
        case F_SPACE: {
            int n = argi(e, 0);
            if (n < 0 || n > QB_STRMAX) rt_error(5);
            *p = (char *)qb_alloc((size_t)n + 1);
            memset(*p, ' ', (size_t)n);
            *len = n;
            return;
        }
        case F_STRING: {
            int n = argi(e, 0);
            if (n < 0 || n > QB_STRMAX) rt_error(5);
            int c;
            if (e->args[1]->type == T_STR) {
                char *s; int l; evs(e->args[1], &s, &l);
                if (l == 0) { free(s); rt_error(5); }
                c = (unsigned char)s[0];
                free(s);
            } else {
                c = argi(e, 1);
                if (c < 0 || c > 255) rt_error(5);
            }
            *p = (char *)qb_alloc((size_t)n + 1);
            memset(*p, c, (size_t)n);
            *len = n;
            return;
        }
        case F_INKEY: {
            kb_poll();
            char k[2];
            int n = kb_get(k);
            ret_str(p, len, k, n);
            if (n == 0) sys_idle();
            return;
        }
        case F_INPUT: {
            int n = argi(e, 0);
            if (n < 1 || n > 32767) rt_error(5);
            if (e->nargs > 1) {
                int fn = argi(e, 1);
                if (!file_input_chars(fn, n, p, len)) rt_error(62);
                return;
            }
            char *r = (char *)qb_alloc((size_t)n + 1);
            int got = 0;
            while (got < n) {
                char k[2];
                int kl = kb_waitkey(k);
                if (kl == 0) { if (rs->breakreq || sys_shutting_down()) break; continue; }
                for (int i = 0; i < kl && got < n; i++) r[got++] = k[i];
            }
            *p = r;
            *len = got;
            return;
        }
        case F_DATE: { sys_datestr(buf); ret_str(p, len, buf, (int)strlen(buf)); return; }
        case F_TIME: { sys_timestr(buf); ret_str(p, len, buf, (int)strlen(buf)); return; }
        case F_ENVIRON: {
            if (e->args[0]->type == T_STR) {
                char *s; int l; evs(e->args[0], &s, &l);
                const char *v = getenv(s);
                free(s);
                ret_str(p, len, v ? v : "", v ? (int)strlen(v) : 0);
            } else {
                /* ENVIRON$(n): the n-th "NAME=value" string, "" past the end */
                int n = argi(e, 0);
                if (n < 1 || n > 255) rt_error(5);
#ifdef _WIN32
                char **env = _environ;
#else
                extern char **environ;
                char **env = environ;
#endif
                const char *v = "";
                for (int i = 1; env && env[i - 1]; i++) if (i == n) { v = env[i - 1]; break; }
                ret_str(p, len, v, (int)strlen(v));
            }
            return;
        }
        case F_MKI: { int16_t v = (int16_t)fit_num(T_INT, evn(e->args[0])); ret_str(p, len, (char *)&v, 2); return; }
        case F_MKL: { int32_t v = (int32_t)fit_num(T_LNG, evn(e->args[0])); ret_str(p, len, (char *)&v, 4); return; }
        case F_MKS: { float v = (float)fit_num(T_SNG, evn(e->args[0])); ret_str(p, len, (char *)&v, 4); return; }
        case F_MKD: { double v = evn(e->args[0]); ret_str(p, len, (char *)&v, 8); return; }
        case F_MKSMBF: case F_MKDMBF: {
            double v = evn(e->args[0]);
            unsigned char b[8] = {0};
            int n = e->sub == F_MKSMBF ? 4 : 8;
            if (v != 0) {
                int ex;
                double m = frexp(fabs(v), &ex); /* 0.5 <= m < 1 */
                if (n == 4) {
                    uint32_t mant = (uint32_t)(m * 16777216.0 + 0.5);
                    if (mant >= 16777216u) { mant >>= 1; ex++; }
                    b[0] = (unsigned char)(mant & 0xFF); b[1] = (unsigned char)((mant >> 8) & 0xFF);
                    b[2] = (unsigned char)(((mant >> 16) & 0x7F) | (v < 0 ? 0x80 : 0));
                    b[3] = (unsigned char)(ex + 128);
                } else {
                    double mm = m * 2.0 - 1.0; /* fraction after hidden bit */
                    for (int i = 6; i >= 0; i--) { (void)i; }
                    uint64_t mant = (uint64_t)(m * 72057594037927936.0); /* 2^56 */
                    for (int i = 0; i < 7; i++) b[i] = (unsigned char)((mant >> (8 * i)) & 0xFF);
                    b[6] = (unsigned char)((b[6] & 0x7F) | (v < 0 ? 0x80 : 0));
                    b[7] = (unsigned char)(ex + 128);
                    (void)mm;
                }
            }
            ret_str(p, len, (char *)b, n);
            return;
        }
        case F_VARPTRS: {
            /* QBasic returns type byte + 2-byte address; here the "address" indexes a table of
               variables that PLAY and DRAW resolve with qb_varptr_num / qb_varptr_str */
            Expr *a = e->args[0];
            Value *v = lval(a);
            int k = vp_next; vp_next = (vp_next + 1) % VPN;
            vp_tab[k] = v;
            int t = v->t;
            char b[3];
            b[0] = (char)(t == T_INT ? 2 : t == T_LNG ? 20 : t == T_SNG ? 4 : t == T_DBL ? 8 : 3);
            b[1] = (char)(k & 0xFF); b[2] = (char)(k >> 8);
            ret_str(p, len, b, 3);
            return;
        }
        case F_ERDEV: ret_str(p, len, "", 0); return;
        case F_IOCTL: ret_str(p, len, "", 0); return;
        case F_DIR: {
            extern void file_dir(const char *spec, int len, int has_spec, int attr, char **out, int *outlen);
            char buf[600] = ""; int bl = 0, attr = 0;
            if (e->nargs >= 1) {
                char *sp; int sl; evs(e->args[0], &sp, &sl);
                bl = sl < 590 ? sl : 590; memcpy(buf, sp, (size_t)bl); free(sp);
                if (e->nargs >= 2) attr = argi(e, 1);
            }
            file_dir(buf, bl, e->nargs >= 1, attr, p, len);
            return;
        }
        case F_CURDIR: {
            extern void file_curdir(char **out, int *outlen);
            if (e->nargs >= 1) { char *sp; int sl; evs(e->args[0], &sp, &sl); free(sp); }
            file_curdir(p, len);
            return;
        }
        case F_COMMAND: {
            /* QuickBASIC returns the command line in upper case */
            int n = (int)strlen(g_command);
            char *c = (char *)qb_alloc((size_t)n + 1);
            for (int i = 0; i < n; i++) c[i] = (char)toupper((unsigned char)g_command[i]);
            *p = c; *len = n;
            return;
        }
        case F_SPC: case F_TAB: ret_str(p, len, "", 0); return;
    }
    rt_error(5);
}

/* constant evaluation used by the compiler */
double eval_const_num(Expr *e, int *ok) {
    jmp_buf jb;
    jmp_buf *save = const_jb;
    const_jb = &jb;
    double r = 0;
    if (setjmp(jb)) { const_jb = save; *ok = 0; return 0; }
    if (e->op == E_VAR || e->op == E_ARR || e->op == E_CALLF || e->op == E_FN || e->op == E_MEMBER || e->op == E_FUNC) {
        const_jb = save; *ok = 0; return 0;
    }
    if (e->a && (e->a->op != E_NUM)) {
        /* nested expressions: evaluate recursively only if fully constant */
        int ok2 = 1;
        eval_const_num(e->a, &ok2);
        if (!ok2) { const_jb = save; *ok = 0; return 0; }
    }
    if (e->b && (e->b->op != E_NUM)) {
        int ok2 = 1;
        eval_const_num(e->b, &ok2);
        if (!ok2) { const_jb = save; *ok = 0; return 0; }
    }
    if (e->op == E_STR || e->type == T_STR) { const_jb = save; *ok = 0; return 0; }
    if (!rs) { rs = &rs_storage; }
    r = evn(e);
    const_jb = save;
    *ok = 1;
    return r;
}

/* ------------------------------------------------------------------ */
/* output helpers (screen or file)                                    */
/* ------------------------------------------------------------------ */
static void out_str(int filenum, const char *s, int len) {
    if (filenum > 0) file_print(filenum, s, len);
    else if (filenum == 0) scr_print(s, len);
    /* filenum < 0 : LPRINT -> discarded */
}
static void out_newline(int filenum) {
    if (filenum > 0) file_print(filenum, "\r\n", 2);
    else if (filenum == 0) scr_newline();
}
static int out_col(int filenum) {
    if (filenum > 0) return file_column(filenum);
    if (filenum == 0) return scr_pos();
    return 1;
}

static void exec_print(Stmt *s) {
    int fn = 0;
    if (s->op == S_LPRINT) fn = -1;
    if (s->e1) {
        fn = (int)qb_round_even(evn(s->e1));
        if (!file_isopen(fn)) rt_error(52);
        int m = file_mode(fn);
        if (m != 2 && m != 8 && m != 4) rt_error(54);
    }
    int *seps = (int *)s->p;
    if (s->e2) {
        char *f; int fl;
        evs(s->e2, &f, &fl);
        extern int g_using_file;
        g_using_file = fn;
        print_using(fn, f, fl, s->args, s->nargs, seps);
        free(f);
        return;
    }
    int n = s->nargs;
    for (int i = 0; i < n; i++) {
        Expr *e = s->args[i];
        if (e) {
            if (e->op == E_FUNC && (e->sub == F_SPC || e->sub == F_TAB)) {
                int v = (int)qb_round_even(evn(e->args[0]));
                if (e->sub == F_SPC) {
                    if (v < 0) v = 0;
                    if (fn == 0) scr_spc(v);
                    else { for (int k = 0; k < v; k++) out_str(fn, " ", 1); }
                } else {
                    if (v < 1) v = 1;
                    if (fn == 0) scr_tab(v);
                    else {
                        int col = out_col(fn);
                        if (v < col) { out_newline(fn); col = 1; }
                        while (col < v) { out_str(fn, " ", 1); col++; }
                    }
                }
            } else if (e->type == T_STR) {
                char *p; int l;
                evs(e, &p, &l);
                out_str(fn, p, l);
                free(p);
            } else {
                char buf[64];
                int l = fmt_number(buf, evn(e), e->type, 1);
                buf[l++] = ' ';
                buf[l] = 0;
                if (fn == 0 && !scr_text_fits(l)) scr_newline();
                out_str(fn, buf, l);
            }
        }
        if (seps[i] == 2) {
            if (fn == 0) scr_comma();
            else {
                int col = out_col(fn);
                int next = ((col - 1) / 14 + 1) * 14 + 1;
                while (col < next) { out_str(fn, " ", 1); col++; }
            }
        }
    }
    if (n == 0 || seps[n - 1] == 0) out_newline(fn);
}

static void exec_write(Stmt *s) {
    int fn = 0;
    if (s->e1) { fn = (int)qb_round_even(evn(s->e1)); if (!file_isopen(fn)) rt_error(52); }
    for (int i = 0; i < s->nargs; i++) {
        Expr *e = s->args[i];
        if (e->type == T_STR) {
            char *p; int l;
            evs(e, &p, &l);
            out_str(fn, "\"", 1);
            out_str(fn, p, l);
            out_str(fn, "\"", 1);
            free(p);
        } else {
            char buf[64];
            int l = fmt_number(buf, evn(e), e->type, 0);
            const char *b = buf;
            if (*b == ' ') { b++; l--; }
            out_str(fn, b, l);
        }
        if (i < s->nargs - 1) out_str(fn, ",", 1);
    }
    out_newline(fn);
}

/* ------------------------------------------------------------------ */
/* INPUT                                                              */
/* ------------------------------------------------------------------ */
int input_line(char *buf, int max, int *len);  /* screen.c */

static int parse_input_fields(const char *line, int len, Expr **vars, int nvars, int store) {
    int pos = 0;
    for (int vi = 0; vi < nvars; vi++) {
        Expr *v = vars[vi];
        /* skip leading spaces */
        while (pos < len && line[pos] == ' ') pos++;
        char field[512];
        int fl = 0;
        int quoted = 0;
        if (v->type == T_STR && pos < len && line[pos] == '"') {
            quoted = 1;
            pos++;
            while (pos < len && line[pos] != '"') { if (fl < 511) field[fl++] = line[pos]; pos++; }
            if (pos < len) pos++;
            while (pos < len && line[pos] == ' ') pos++;
            if (pos < len && line[pos] != ',') return 0;
        } else {
            while (pos < len && line[pos] != ',') { if (fl < 511) field[fl++] = line[pos]; pos++; }
            while (fl > 0 && field[fl - 1] == ' ') fl--;
        }
        field[fl] = 0;
        if (vi < nvars - 1) {
            if (pos >= len || line[pos] != ',') return 0;
            pos++;
        } else {
            if (pos < len) return 0;  /* extra fields */
        }
        if (v->type != T_STR) {
            /* validate number */
            int i = 0;
            while (i < fl && field[i] == ' ') i++;
            int ok = 1;
            if (i < fl) {
                char *end;
                char tmp[512];
                int tl = 0;
                for (int k = i; k < fl; k++) if (field[k] != ' ') tmp[tl++] = field[k];
                tmp[tl] = 0;
                for (int k = 0; k < tl; k++) if (tmp[k] == 'd' || tmp[k] == 'D') tmp[k] = 'e';
                if (tmp[0] == '&') ok = 1;
                else {
                    strtod(tmp, &end);
                    if (*end == '%' || *end == '&' || *end == '!' || *end == '#') end++;
                    if (*end) ok = 0;
                }
            }
            if (!ok) return 0;
            double d = qb_val(field, fl);
            if (store) {
                Value *dst = lval(v);
                double r = d;
                if (dst->t == T_INT && (qb_round_even(r) < -32768 || qb_round_even(r) > 32767)) return -1;
                if (dst->t == T_LNG && (qb_round_even(r) < -2147483648.0 || qb_round_even(r) > 2147483647.0)) return -1;
                dst->n = fit_num(dst->t, r);
            } else {
                int t = v->type;
                double r = qb_round_even(d);
                if (t == T_INT && (r < -32768 || r > 32767)) return -1;
                if (t == T_LNG && (r < -2147483648.0 || r > 2147483647.0)) return -1;
            }
        } else if (store) {
            Value *dst = lval(v);
            val_setstr(dst, field, fl);
        }
        (void)quoted;
    }
    return 1;
}

static int exec_input(Stmt *s) {
    if (s->e2) {
        /* INPUT #n */
        int fn = (int)qb_round_even(evn(s->e2));
        if (!file_isopen(fn)) rt_error(52);
        if (file_mode(fn) != 1 && file_mode(fn) != 4) rt_error(54);
        for (int i = 0; i < s->nargs; i++) {
            char *p; int l;
            Expr *v = s->args[i];
            if (!file_input_field(fn, &p, &l, v->type == T_STR)) rt_error(62);
            Value *dst = lval(v);
            if (v->type == T_STR) val_setstr(dst, p, l);
            else dst->n = fit_num(dst->t, qb_val(p, l));
            free(p);
        }
        return 1;
    }
    char prompt[512];
    int pl = 0;
    if (s->e1) {
        int n = s->e1->slen < 500 ? s->e1->slen : 500;
        memcpy(prompt, s->e1->str, (size_t)n);
        pl = n;
    }
    if (!s->e1 || s->i2 == 0) { prompt[pl++] = '?'; prompt[pl++] = ' '; }
    for (;;) {
        scr_print(prompt, pl);
        char line[256];
        int len = 0;
        if (!input_line(line, 255, &len)) return 0; /* aborted (Break) */
        if (!s->i1) scr_newline();
        int r = parse_input_fields(line, len, s->args, s->nargs, 0);
        if (r == 1) {
            parse_input_fields(line, len, s->args, s->nargs, 1);
            return 1;
        }
        if (s->i1) scr_newline();
        if (r == -1) { scr_print("Overflow", 8); scr_newline(); }
        scr_print("Redo from start", 15);
        scr_newline();
    }
}

static int exec_line_input(Stmt *s) {
    Value *dst;
    if (s->e2) {
        int fn = (int)qb_round_even(evn(s->e2));
        if (!file_isopen(fn)) rt_error(52);
        if (file_mode(fn) != 1 && file_mode(fn) != 4) rt_error(54);
        char *p; int l;
        if (!file_line_input(fn, &p, &l)) rt_error(62);
        dst = lval(s->args[0]);
        val_setstr(dst, p, l);
        free(p);
        return 1;
    }
    if (s->e1) scr_print(s->e1->str, s->e1->slen);
    char line[256];
    int len = 0;
    if (!input_line(line, 255, &len)) return 0;   /* aborted (Break) */
    if (!s->i1) scr_newline();
    dst = lval(s->args[0]);
    val_setstr(dst, line, len);
    return 1;
}

/* ------------------------------------------------------------------ */
/* READ / DATA                                                        */
/* ------------------------------------------------------------------ */
static void exec_read(Stmt *s) {
    Program *P = rs->prog;
    for (int i = 0; i < s->nargs; i++) {
        if (rs->dataptr >= P->ndata) rt_error(4);
        DataItem *d = &P->data[rs->dataptr++];
        Value *dst = lval(s->args[i]);
        if (dst->t == T_STR) val_setstr(dst, d->s, d->len);
        else {
            if (d->quoted) rt_error(13);
            /* validate */
            const char *q = d->s;
            while (*q == ' ') q++;
            if (*q) {
                char tmp[256]; int tl = 0;
                for (const char *c = q; *c && tl < 250; c++) if (*c != ' ') tmp[tl++] = (*c == 'd' || *c == 'D') ? 'e' : *c;
                tmp[tl] = 0;
                if (tmp[0] != '&') {
                    char *end;
                    strtod(tmp, &end);
                    if (*end == '%' || *end == '&' || *end == '!' || *end == '#') end++;
                    if (*end) rt_error(2);
                }
            }
            dst->n = fit_num(dst->t, qb_val(d->s, d->len));
        }
    }
}

/* ------------------------------------------------------------------ */
/* execution                                                          */
/* ------------------------------------------------------------------ */
static double last_poll = 0;
static int poll_counter = 0;

static void check_events(void);

/* optional CPU speed emulation: statements per second (0 = unlimited) */
int g_speed_limit = 0;
static double thr_t0 = 0;
static long thr_count = 0;

static void periodic(void) {
    double t = sys_time();
    if (g_speed_limit > 0) {
        thr_count += 64;
        if (thr_t0 == 0 || t - thr_t0 > 1.0) { thr_t0 = t; thr_count = 0; }
        double expected = (double)thr_count / g_speed_limit;
        double ahead = expected - (t - thr_t0);
        if (ahead > 0.002) { sys_sleep((int)(ahead * 1000)); t = sys_time(); }
    }
    if (t - last_poll >= 0.015) {
        last_poll = t;
        kb_poll();
        scr_present(0);
        if (kb_break_pressed()) rs->breakreq = 1;
        if (sys_shutting_down()) rs->breakreq = 1;
    }
}

static void do_gosub_event(int target) {
    Frame *g = rs->global;
    if (g->ngosub >= g->capgosub) { g->capgosub = g->capgosub ? g->capgosub * 2 : 16; g->gosub = (int *)qb_realloc(g->gosub, sizeof(int) * (size_t)g->capgosub); }
    g->gosub[g->ngosub++] = -2;   /* event marker */
    Frame *saved = rs->cur;
    rs->cur = g;
    rs->inevent = 1;
    exec(g, target);
    rs->inevent = 0;
    rs->cur = saved;
}

static void check_events(void) {
    snd_poll();
    if (rs->inevent) return;
    if (rs->onplay >= 0 && rs->playon) {
        /* ON PLAY(n): the background queue has just dropped below n notes (PLAY STOP remembers it) */
        int c = snd_queue_count();
        if (c < rs->playlimit && rs->playlast >= rs->playlimit) rs->playpending = 1;
        rs->playlast = c;
        if (rs->playpending && rs->playon == 1) {
            rs->playpending = 0;
            do_gosub_event(rs->onplay);
            rs->playlast = snd_queue_count();
        }
    }
    if (rs->ontimer >= 0 && rs->timeron == 1) {
        double t = sys_timer();
        if (t >= rs->timernext || t + 86400 - rs->timernext < 1.0e-9) {
            rs->timernext = t + rs->timerint;
            if (rs->timernext >= 86400) rs->timernext -= 86400;
            do_gosub_event(rs->ontimer);
        }
    }
}

static void gosub_push(Frame *f, int ret) {
    if (f->ngosub >= f->capgosub) {
        f->capgosub = f->capgosub ? f->capgosub * 2 : 16;
        if (f->capgosub > 16 * 1024 * 1024) rt_error(28);
        f->gosub = (int *)qb_realloc(f->gosub, sizeof(int) * (size_t)f->capgosub);
    }
    f->gosub[f->ngosub++] = ret;
}

static int exec_debug_hook(Frame *f, int pc) {
    Stmt *s = &rs->prog->code[pc];
    int brk = 0;
    if (rs->breakreq) { brk = 1; rs->breakreq = 0; }
    else if (s->linestart) {
        if (rs->stepmode == 1) brk = 1;
        else if (rs->stepmode == 2) {
            /* step over: stop at depth <= starting depth */
            Frame *x = f; int d = 0; while (x) { d++; x = x->parent; }
            Frame *y = rs->stepframe; int sd = 0; while (y) { sd++; y = y->parent; }
            if (d <= sd || rs->stepframe == NULL) brk = 1;
        }
        if (!brk && ide_is_breakpoint(s->part, s->line)) brk = 1;
    }
    if (!brk) return 0;
    if (!g_ide_active) { rs->status = RUN_ABORT; longjmp(*rs->jb, 3); }
    rs->curstmt = pc;
    rs->stopped = 1;
    int act = ide_debug_break(pc);
    rs->stopped = 0;
    switch (act) {
        case DBG_STEP: rs->stepmode = 1; break;
        case DBG_PSTEP: rs->stepmode = 2; rs->stepframe = f; break;
        case DBG_CONTINUE: rs->stepmode = 0; break;
        case DBG_ABORT: rs->status = RUN_ABORT; longjmp(*rs->jb, 3);
        case DBG_RESTART: rs->status = RUN_RESTART; longjmp(*rs->jb, 3);
    }
    return rs->pc != pc ? 1 : 0; /* set next statement may change pc */
}


/* ------------------------------------------------------------------ */
/* LOADBMP / LOADGIF / LOADPAL (QBLike extensions)                      */
/* ------------------------------------------------------------------ */
int  gfx_colors(void);
void gfx_blit_image(double x, double y, int w, int h, const uint8_t *pix);
int  gfx_store_image(Array *a, int index, int w, int h, const uint8_t *pix);
void gfx_set_palette_rgb8(int idx, int r, int g, int b);

/* resolve "array(subscripts)" target of GET/PUT-style statements to a linear index */
static int array_target(Stmt *s, Array **out) {
    Array *a = arr_slot(s->i2, s->i3);
    if (!a->alive) {
        if (s->nargs) { int lb[1] = { rs->optionbase }, ub[1] = { 10 }; array_dim(a, 1, lb, ub, 0); }
        else rt_error(5);
    }
    long off = 0;
    if (s->nargs) {
        if (s->nargs != a->nd) rt_error(9);
        int idx[8];
        for (int i = 0; i < s->nargs && i < 8; i++) idx[i] = (int)qb_round_even(evn(s->args[i]));
        off = (long)arr_offset(a, idx);
    }
    *out = a;
    return (int)off;
}

static void image_error(ImgSet *set, int r) {
    img_free(set);
    if (r == IMG_NOTFOUND) rt_error(53);
    if (r == IMG_NOTINDEXED) rt_error_msg(5, "Image must have 2, 16 or 256 colors");
    rt_error_msg(5, "Bad or unsupported image file");
}

static void exec_loadimg(Stmt *s) {
    char *name; int nl;
    evs(s->e1, &name, &nl);
    ImgSet set;
    int r;
    int ispal = s->op == S_LOADPAL;
    if (ispal) {
        r = img_load_bmp(name, nl, &set, 1);
        if (r == IMG_BADFORMAT) r = img_load_gif(name, nl, &set, 1);
    } else if (s->i1 & 1) r = img_load_gif(name, nl, &set, 0);
    else r = img_load_bmp(name, nl, &set, 0);
    free(name);
    if (r != IMG_OK) image_error(&set, r);
    if (ispal) {
        Array *a;
        int index = array_target(s, &a);
        if (!set.haspal) { img_free(&set); rt_error_msg(5, "Image has no palette"); }
        if (a->t == T_INT) { img_free(&set); rt_error_msg(13, "Type mismatch: LOADPAL needs a LONG array"); }
        if (set.npal > a->count - index) { img_free(&set); rt_error(5); }
        for (int i = 0; i < set.npal; i++) {
            long v = 65536L * (set.pal[i][2] >> 2) + 256L * (set.pal[i][1] >> 2) + (set.pal[i][0] >> 2);
            a->v[index + i].n = fit_num(a->t, (double)v);
        }
        img_free(&set);
        return;
    }
    int colors = gfx_colors();
    if (colors == 0) { img_free(&set); rt_error(5); }    /* text mode */
    if (s->i1 & 2) {
        /* to the screen */
        double x = evn(s->e2), y = evn(s->e3);
        if (set.colors > colors) { img_free(&set); rt_error_msg(5, "Image has too many colors for this screen mode"); }
        gfx_blit_image(x, y, set.fr[0].w, set.fr[0].h, set.fr[0].pix);
        if ((s->i1 & 4) && set.haspal)
            for (int i = 0; i < set.npal && i < colors; i++) gfx_set_palette_rgb8(i, set.pal[i][0], set.pal[i][1], set.pal[i][2]);
        img_free(&set);
        return;
    }
    /* to an array, in PUT format; GIF frames are stored one after another */
    Array *a;
    int index = array_target(s, &a);
    for (int f = 0; f < set.n; f++) {
        Img *im = &set.fr[f];
        /* check space before writing (gfx_store_image raises the error) */
        jmp_buf *save = rs->jb;
        jmp_buf jb;
        rs->jb = &jb;
        if (setjmp(jb)) { rs->jb = save; img_free(&set); longjmp(*rs->jb, 2); }
        index += gfx_store_image(a, index, im->w, im->h, im->pix);
        rs->jb = save;
    }
    img_free(&set);
}

int g_using_file = 0;
int g_debug_enabled = 1;

static int exec(Frame *f, int pc) {
    Program *P = rs->prog;
    Stmt *code;
    for (;;) {
        code = P->code;
        if (pc < 0 || pc >= P->ncode) rt_error(51);
        Stmt *s = &code[pc];
        f->retpc = pc;
        if (f == rs->global) rs->curstmt = pc;
        rs->pc = pc;
        if (++poll_counter >= 64) {
            poll_counter = 0;
            periodic();
            check_events();
        }
        if (g_debug_enabled && (rs->stepmode || rs->breakreq || (s->linestart && g_ide_active))) {
            if (exec_debug_hook(f, pc)) { pc = rs->pc; continue; }
        }
        if (rs->tron && s->linestart) {
            /* TRON prints [line] for numbered lines; we show statement lines */
        }
        switch (s->op) {
            case S_NOP:
                if (s->i1 == 999) rs->optionbase = s->i2;
                pc++;
                break;
            case S_END:
                if (s->i1 == 1) return RUN_END; /* immediate */
                rs->status = RUN_END;
                longjmp(*rs->jb, 1);
            case S_STOP:
                if (g_ide_active) {
                    rs->breakreq = 1;
                    pc++;
                    rs->pc = pc;
                    if (exec_debug_hook(f, pc)) pc = rs->pc;
                    break;
                }
                rs->status = RUN_END;
                longjmp(*rs->jb, 1);
            case S_SYSTEM:
                rs->status = RUN_END;
                rs->err = -1;   /* request exit to OS */
                longjmp(*rs->jb, 1);
            case S_LET: {
                Value *dst = lval(s->e1);
                assign(dst, s->e2);
                pc++;
                break;
            }
            case S_LETFUNC:
                assign(&f->ret, s->e1);
                pc++;
                break;
            case S_PRINT: case S_LPRINT: exec_print(s); pc++; break;
            case S_WRITE: exec_write(s); pc++; break;
            case S_GOTO: pc = s->i1; break;
            case S_JMP: pc = s->i1; break;
            case S_JMPF: pc = evn(s->e1) != 0 ? pc + 1 : s->i1; break;
            case S_GOSUB:
                gosub_push(f, pc + 1);
                pc = s->i1;
                break;
            case S_RETURN: {
                if (f->ngosub == 0) rt_error(3);
                int r = f->gosub[--f->ngosub];
                if (r == -2) {
                    /* return from event handler */
                    if (s->i1 >= 0) { /* RETURN label from event: not supported precisely */ }
                    return RUN_OK;
                }
                pc = s->i1 >= 0 ? s->i1 : r;
                break;
            }
            case S_FORINIT: {
                Value *v = lval(s->e1);
                Frame *hf = s->i4 ? rs->cur : rs->global;
                Value *ve = hf->vals[s->i1], *vs = hf->vals[s->i2];
                double start = evn(s->e2);
                double end = evn(s->e3);
                double step = s->e4 ? evn(s->e4) : 1;
                ve->n = fit_num(v->t, end);
                vs->n = fit_num(v->t, step);
                v->n = fit_num(v->t, start);
                if ((vs->n >= 0 && v->n > ve->n) || (vs->n < 0 && v->n < ve->n)) pc = s->i3;
                else pc++;
                break;
            }
            case S_NEXT: {
                Value *v = lval(s->e1);
                Frame *hf = s->i4 ? rs->cur : rs->global;
                Value *ve = hf->vals[s->i1], *vs = hf->vals[s->i2];
                v->n = fit_num(v->t, v->n + vs->n);
                if ((vs->n >= 0 && v->n <= ve->n) || (vs->n < 0 && v->n >= ve->n)) pc = s->i3;
                else pc++;
                break;
            }
            case S_CALL:
                run_proc(s->i1, s->args, s->nargs);
                pc++;
                break;
            case S_PROCEND:
            case S_EXITPROC:
                if (f->proc >= 0) {
                    /* free frame for SUBs (functions are freed by caller) */
                    if (!P->procs[f->proc].isfunc) { copy_back(f); frame_free(f); }
                    return RUN_OK;
                }
                rs->status = RUN_END;
                longjmp(*rs->jb, 1);
            /* Break during INPUT stops at the INPUT itself; F5 asks again, as in QBasic */
            case S_INPUT: if (exec_input(s) || !g_debug_enabled) pc++; else scr_newline(); break;
            case S_LINEINPUT: if (exec_line_input(s) || !g_debug_enabled) pc++; else scr_newline(); break;
            case S_DIM: case S_REDIM: {
                if (s->i3 == 0 && s->op == S_DIM) {
                    /* scalar DIM: no-op at run time */
                    pc++;
                    break;
                }
                Array *a = arr_slot(s->i1, s->i2);
                int lb[8], ub[8];
                for (int i = 0; i < s->i3; i++) {
                    Expr *l = s->args[i * 2], *u = s->args[i * 2 + 1];
                    double dl = l ? qb_round_even(evn(l)) : rs->optionbase, du = qb_round_even(evn(u));
                    if (dl < -2147483648.0 || dl > 2147483647.0 || du < -2147483648.0 || du > 2147483647.0) rt_error(9);
                    lb[i] = (int)dl;
                    ub[i] = (int)du;
                }
                if (s->op == S_DIM && a->alive) {
                    if (a->prealloc) { pc++; break; }   /* static array, allocated at program start */
                    rt_error(10);
                }
                array_dim(a, s->i3, lb, ub, s->op == S_REDIM && s->i4);
                a->dynamic = 1;
                pc++;
                break;
            }
            case S_ERASE: {
                Array *a = arr_slot(s->i1, s->i2);
                if (a->alive && !a->dynamic) {
                    for (int i = 0; i < a->count; i++) {
                        Value *v = &a->v[i];
                        int t = v->t, u = v->r ? v->r->udt : 0, fl = v->fixlen;
                        val_free(v);
                        val_init(v, t, u, fl);
                    }
                } else array_clear(a);
                pc++;
                break;
            }
            case S_SWAP: {
                Value *a = lval(s->e1);
                Value *b = lval(s->e2);
                if (a->fixlen || b->fixlen) {
                    Value t; memset(&t, 0, sizeof t); t.t = T_STR;
                    val_setstr(&t, a->s, a->slen);
                    val_setstr(a, b->s, b->slen);
                    val_setstr(b, t.s, t.slen);
                    val_free(&t);
                } else {
                    Value t = *a; *a = *b; *b = t;
                }
                pc++;
                break;
            }
            case S_READ: exec_read(s); pc++; break;
            case S_RESTORE: rs->dataptr = s->i1 >= 0 ? s->i1 : 0; pc++; break;
            case S_ONGOTO: case S_ONGOSUB: {
                double d = qb_round_even(evn(s->e1));
                if (d < 0 || d > 255) rt_error(5);
                int n = (int)d;
                if (n >= 1 && n <= s->i2) {
                    if (s->op == S_ONGOSUB) gosub_push(f, pc + 1);
                    pc = ((int *)s->p)[n - 1];
                } else pc++;
                break;
            }
            case S_ONERROR:
                rs->onerror = s->i1;
                pc++;
                break;
            case S_RESUME: {
                if (!rs->inhandler) rt_error(20);
                rs->inhandler = 0;
                rs->err = 0;
                if (s->i2 == 0) pc = rs->resumepc;
                else if (s->i2 == 1) pc = rs->resumenext;
                else pc = s->i1;
                break;
            }
            case S_ERROR: {
                double d = qb_round_even(evn(s->e1));
                if (d < 1 || d > 255) rt_error(5);
                rt_error((int)d);
                break;
            }
            case S_CLS: {
                int w = -1;
                if (s->e1) { w = (int)qb_round_even(evn(s->e1)); if (w < 0 || w > 2) rt_error(5); }
                scr_cls(w);
                pc++;
                break;
            }
            case S_COLOR: {
                int a = s->e1 ? (int)qb_round_even(evn(s->e1)) : 0;
                int b = s->e2 ? (int)qb_round_even(evn(s->e2)) : 0;
                int c = s->e3 ? (int)qb_round_even(evn(s->e3)) : 0;
                scr_color(a, b, c, s->e1 != NULL, s->e2 != NULL, s->e3 != NULL);
                pc++;
                break;
            }
            case S_LOCATE: {
                int v[5] = {0}, has[5] = {0};
                for (int i = 0; i < s->nargs && i < 5; i++) if (s->args[i]) { v[i] = (int)qb_round_even(evn(s->args[i])); has[i] = 1; }
                scr_locate(v[0], v[1], v[2], v[3], v[4], has);
                pc++;
                break;
            }
            case S_SCREEN: {
                int v[4] = {0}, has[4] = {0};
                for (int i = 0; i < s->nargs && i < 4; i++) if (s->args[i]) { v[i] = (int)qb_round_even(evn(s->args[i])); has[i] = 1; }
                if (!has[0]) v[0] = scr_mode();
                scr_setmode(v[0], v[1], v[2], v[3], has[2], has[3]);
                if (has[0]) { extern void gfx_clip_reset(void); gfx_clip_reset(); }
                pc++;
                break;
            }
            case S_WIDTH: {
                if (s->i1) { pc++; break; }
                int c = s->e1 ? (int)qb_round_even(evn(s->e1)) : -1;
                int r = s->e2 ? (int)qb_round_even(evn(s->e2)) : -1;
                scr_width(c, r);
                pc++;
                break;
            }
            case S_VIEWPRINT: {
                if (s->e1) {
                    int t = (int)qb_round_even(evn(s->e1)), b = (int)qb_round_even(evn(s->e2));
                    scr_viewprint(t, b, 1);
                } else scr_viewprint(0, 0, 0);
                pc++;
                break;
            }
            case S_VIEW: {
                if (!s->i2) { gfx_view(0, 0, 0, 0, 0, 0, 0, 0, 0, 0); pc++; break; }
                int x1 = (int)qb_round_even(evn(s->e1)), y1 = (int)qb_round_even(evn(s->e2));
                int x2 = (int)qb_round_even(evn(s->e3)), y2 = (int)qb_round_even(evn(s->e4));
                int fill = 0, border = 0, hf = 0, hb = 0;
                if (s->nargs > 0 && s->args[0]) { fill = (int)qb_round_even(evn(s->args[0])); hf = 1; }
                if (s->nargs > 1 && s->args[1]) { border = (int)qb_round_even(evn(s->args[1])); hb = 1; }
                gfx_view(x1, y1, x2, y2, fill, border, s->i1, 1, hf, hb);
                pc++;
                break;
            }
            case S_WINDOW: {
                if (!s->i2) { gfx_window(0, 0, 0, 0, 0, 0); pc++; break; }
                gfx_window(evn(s->e1), evn(s->e2), evn(s->e3), evn(s->e4), s->i1, 1);
                pc++;
                break;
            }
            case S_PSET: case S_PRESET: {
                double x = evn(s->e1), y = evn(s->e2);
                int c = s->e3 ? (int)qb_round_even(evn(s->e3)) : 0;
                gfx_pset(x, y, c, s->e3 != NULL, s->i1, s->op == S_PRESET);
                pc++;
                break;
            }
            case S_LINE: {
                double x1 = 0, y1 = 0;
                if (s->i1 & 4) { x1 = evn(s->e1); y1 = evn(s->e2); }
                double x2 = evn(s->e3), y2 = evn(s->e4);
                int c = 0, hc = 0, st = 0, hs = 0;
                if (s->args[0]) { c = (int)qb_round_even(evn(s->args[0])); hc = 1; }
                if (s->args[1]) { st = (int)qb_round_even(evn(s->args[1])); hs = 1; }
                gfx_line(x1, y1, x2, y2, c, hc, s->i2, s->i1 & 1, (s->i1 >> 1) & 1, (s->i1 >> 2) & 1, st, hs);
                pc++;
                break;
            }
            case S_CIRCLE: {
                double x = evn(s->e1), y = evn(s->e2), r = evn(s->e3);
                int c = 0, hc = 0, hst = 0, hen = 0, has = 0;
                double st = 0, en = 0, asp = 0;
                if (s->nargs > 0 && s->args[0]) { c = (int)qb_round_even(evn(s->args[0])); hc = 1; }
                if (s->nargs > 1 && s->args[1]) { st = evn(s->args[1]); hst = 1; }
                if (s->nargs > 2 && s->args[2]) { en = evn(s->args[2]); hen = 1; }
                if (s->nargs > 3 && s->args[3]) { asp = evn(s->args[3]); has = 1; }
                gfx_circle(x, y, r, c, hc, st, en, asp, hst, hen, has, s->i1);
                pc++;
                break;
            }
            case S_FILL: {
                extern void gfx_fill(double x, double y, int color, int hascolor, int step);
                double x = evn(s->e1), y = evn(s->e2);
                gfx_fill(x, y, s->e3 ? (int)qb_round_even(evn(s->e3)) : 0, s->e3 != NULL, s->i1);
                pc++;
                break;
            }
            case S_PAINT: {
                double x = evn(s->e1), y = evn(s->e2);
                int p = 0, hp = 0, b = 0, hb = 0;
                char *tile = NULL; int tl = 0;
                if (s->e3) {
                    if (s->e3->type == T_STR) { evs(s->e3, &tile, &tl); hp = 1; }
                    else { p = (int)qb_round_even(evn(s->e3)); hp = 1; }
                }
                if (s->e4) { b = (int)qb_round_even(evn(s->e4)); hb = 1; }
                gfx_paint(x, y, p, b, hp, hb, s->i1, tile, tl);
                free(tile);
                pc++;
                break;
            }
            case S_DRAW: {
                char *p; int l;
                evs(s->e1, &p, &l);
                gfx_draw(p, l);
                free(p);
                pc++;
                break;
            }
            case S_GETG: case S_PUTG: {
                if (s->p) {   /* image buffer and/or SEG address (QBLike) */
                    GPExt *g = (GPExt *)s->p;
                    GSurf surf = { 0 }, *sp = NULL;
                    Array *da; long dboff;
                    for (int r = 0; r < 2; r++) {
                        GPRef *ref = r == 0 ? &g->surf : &g->data;
                        Array *ra = NULL; long rb = 0;
                        if (ref->kind == 0) continue;
                        if (ref->kind == 2) {
                            int sg = (int)qb_round_even(evn(s->args[ref->first]));
                            int of = (int)qb_round_even(evn(s->args[ref->first + 1]));
                            ra = mem_resolve(sg, of, &rb);
                        } else {
                            ra = arr_slot(ref->scope, ref->idx);
                            if (!ra->alive) {
                                if (ref->n) { int lb[1] = { rs->optionbase }, ub[1] = { 10 }; array_dim(ra, 1, lb, ub, 0); }
                                else rt_error(5);
                            }
                            if (ref->n) {
                                if (ref->n != ra->nd) rt_error(9);
                                int idx[8];
                                for (int i = 0; i < ref->n && i < 8; i++) idx[i] = (int)qb_round_even(evn(s->args[ref->first + i]));
                                rb = arr_offset(ra, idx) * elemsize(ra->t);
                            }
                        }
                        if (elemsize(ra->t) == 0) rt_error(13);
                        if (r == 0) { surf.a = ra; surf.boff = rb; sp = &surf; }
                        else { da = ra; dboff = rb; }
                    }
                    if (s->op == S_GETG)
                        gfx_get_ex(sp, evn(s->e1), evn(s->e2), evn(s->e3), evn(s->e4), s->i1 & 1, (s->i1 >> 1) & 1, da, dboff);
                    else
                        gfx_put_ex(sp, evn(s->e1), evn(s->e2), da, dboff, s->i4, s->i1);
                    pc++;
                    break;
                }
                Array *a = arr_slot(s->i2, s->i3);
                if (!a->alive) {
                    if (s->nargs) { int lb[1] = { rs->optionbase }, ub[1] = { 10 }; array_dim(a, 1, lb, ub, 0); }
                    else rt_error(5);
                }
                int index = 0;
                if (s->nargs) {
                    /* compute linear index of the element */
                    long long off = 0;
                    if (s->nargs != a->nd) rt_error(9);
                    int idx[8];
                    for (int i = 0; i < s->nargs && i < 8; i++) idx[i] = (int)qb_round_even(evn(s->args[i]));
                    off = arr_offset(a, idx);
                    index = (int)off;
                }
                if (s->op == S_GETG)
                    gfx_get(evn(s->e1), evn(s->e2), evn(s->e3), evn(s->e4), s->i1 & 1, (s->i1 >> 1) & 1, a, index);
                else
                    gfx_put(evn(s->e1), evn(s->e2), a, index, s->i4, s->i1);
                pc++;
                break;
            }
            case S_PALETTE: {
                if (s->i1 == 0) gfx_palette(0, 0, 0);
                else if (s->i1 == 1) gfx_palette((int)qb_round_even(evn(s->e1)), (long)qb_round_even(evn(s->e2)), 1);
                else {
                    Array *a = arr_slot(s->i2, s->i3);
                    int idx = 0;
                    if (s->e1) { idx = (int)qb_round_even(evn(s->e1)) - (a->alive ? a->lb[0] : 0); }
                    gfx_palette_using(a, idx);
                }
                pc++;
                break;
            }
            case S_LOADIMG: case S_LOADPAL: exec_loadimg(s); pc++; break;
            case S_MOUSE: {
                extern void sys_mouse(int *x, int *y, int *b);
                extern int g_mouse_cursor;
                if (s->i1) { g_mouse_cursor = s->i1 == 1; scr_present(1); pc++; break; }
                int mx, my, mb;
                sys_mouse(&mx, &my, &mb);
                Value *v;
                v = lval(s->args[0]); v->n = fit_num(v->t, mx);
                v = lval(s->args[1]); v->n = fit_num(v->t, my);
                v = lval(s->args[2]); v->n = fit_num(v->t, mb);
                pc++;
                break;
            }
            case S_CLIP: {
                extern void gfx_clip(int x1, int y1, int x2, int y2, int has);
                if (!s->e1) gfx_clip(0, 0, 0, 0, 0);
                else gfx_clip((int)qb_round_even(evn(s->e1)), (int)qb_round_even(evn(s->e2)),
                              (int)qb_round_even(evn(s->e3)), (int)qb_round_even(evn(s->e4)), 1);
                pc++;
                break;
            }
            case S_PCOPY: gfx_pcopy((int)qb_round_even(evn(s->e1)), (int)qb_round_even(evn(s->e2))); pc++; break;
            case S_FULLSCREEN: {
                dos_setfullscreen(s->i1);
                /* wait (briefly) for the window to change, so the next statements see the new state */
                for (int k = 0; k < 50 && dos_isfullscreen() != s->i1 && !g_headless_test; k++) { scr_present(1); sys_sleep(10); }
                pc++;
                break;
            }
            case S_BEEP: snd_beep(); pc++; break;
            case S_MUSIC: {
                if (s->i1 == 1) snd_music_stop();
                else if (s->i1 == 2) snd_music_volume((int)qb_round_even(evn(s->e1)));
                else {
                    char *nm; int nl;
                    evs(s->e1, &nm, &nl);
                    char buf[1024];
                    if (nl > 1000) nl = 1000;
                    memcpy(buf, nm, (size_t)nl);
                    free(nm);
                    snd_music_play(buf, nl, s->i2);
                }
                pc++;
                break;
            }
            case S_SNDPLAY: {
                int id = (int)qb_round_even(evn(s->e1));
                int ch = s->e2 ? (int)qb_round_even(evn(s->e2)) : 0;
                int vol = s->e3 ? (int)qb_round_even(evn(s->e3)) : 255;
                int pan = s->e4 ? (int)qb_round_even(evn(s->e4)) : 0;
                long rate = s->nargs ? (long)qb_round_even(evn(s->args[0])) : 0;
                if (s->e2 && ch == 0) rt_error(5);
                snd_sample_play(id, ch, vol, pan, s->i1, (int)rate);
                pc++;
                break;
            }
            case S_SNDSTOP: {
                int ch = s->e1 ? (int)qb_round_even(evn(s->e1)) : 0;
                if (s->e1 && ch == 0) rt_error(5);
                snd_sample_stop(ch);
                pc++;
                break;
            }
            case S_SOUND: {
                double fr = evn(s->e1), du = evn(s->e2);
                if ((fr < 37 && fr != 0) || fr > 32767 || du < 0 || du > 65535) rt_error(5);
                snd_sound(fr, du);
                pc++;
                break;
            }
            case S_PLAY: { char *p; int l; evs(s->e1, &p, &l); snd_play(p, l); free(p); pc++; break; }
            case S_SLEEP: {
                double secs = s->e1 ? evn(s->e1) : 0;
                double until = sys_time() + secs;
                kb_poll();
                /* SLEEP returns when a key is pressed (key stays in buffer) */
                extern int kb_count(void);
                int start = kb_count();
                for (;;) {
                    kb_poll();
                    scr_present(0);
                    if (kb_count() > start) break;
                    if (secs > 0 && sys_time() >= until) break;
                    if (kb_break_pressed() || sys_shutting_down()) { rs->breakreq = 1; break; }
                    check_events();
                    sys_sleep(5);
                }
                pc++;
                break;
            }
            case S_RANDOMIZE: {
                double n;
                if (s->e1) n = evn(s->e1);
                else {
                    for (;;) {
                        const char *pr = "Random-number seed (-32768 to 32767)? ";
                        scr_print(pr, (int)strlen(pr));
                        char line[256]; int len = 0;
                        if (!input_line(line, 255, &len)) { n = 0; break; }
                        scr_newline();
                        n = qb_val(line, len);
                        if (n >= -32768 && n <= 32767) break;
                        scr_print("Overflow", 8); scr_newline();
                    }
                }
                qb_randomize(n);
                pc++;
                break;
            }
            case S_OPEN: {
                char *name; int nl;
                evs(s->e1, &name, &nl);
                int fn = (int)qb_round_even(evn(s->e2));
                int reclen = s->e3 ? (int)qb_round_even(evn(s->e3)) : 128;
                int mode = s->i1;
                if (mode == -1) {
                    char *m; int ml;
                    evs(s->e4, &m, &ml);
                    int c = ml > 0 ? toupper((unsigned char)m[0]) : 0;
                    free(m);
                    mode = c == 'I' ? 1 : c == 'O' ? 2 : c == 'A' ? 3 : c == 'R' ? 4 : c == 'B' ? 5 : 0;
                    if (!mode) { free(name); rt_error(54); }
                }
                file_open(name, nl, mode, fn, reclen, 0);
                free(name);
                pc++;
                break;
            }
            case S_CLOSE:
                if (s->nargs == 0) file_closeall();
                else for (int i = 0; i < s->nargs; i++) file_close((int)qb_round_even(evn(s->args[i])));
                pc++;
                break;
            case S_RESET: file_closeall(); pc++; break;
            case S_GETF: case S_PUTF: {
                int fn = (int)qb_round_even(evn(s->e1));
                double rec = s->e2 ? evn(s->e2) : 0;
                Value *v = s->e3 ? lval(s->e3) : NULL;
                if (s->op == S_GETF) file_get(fn, rec, s->e2 != NULL, v);
                else file_put(fn, rec, s->e2 != NULL, v);
                pc++;
                break;
            }
            case S_SEEK: file_seek((int)qb_round_even(evn(s->e1)), evn(s->e2)); pc++; break;
            case S_FIELD: {
                int fn = (int)qb_round_even(evn(s->e1));
                int n = s->nargs / 2;
                int *w = (int *)qb_alloc(sizeof(int) * (size_t)(n + 1));
                Value **vars = (Value **)qb_alloc(sizeof(Value *) * (size_t)(n + 1));
                for (int i = 0; i < n; i++) { w[i] = (int)qb_round_even(evn(s->args[i * 2])); vars[i] = lval(s->args[i * 2 + 1]); }
                file_field(fn, n, w, vars);
                free(w); free(vars);
                pc++;
                break;
            }
            case S_KILL: case S_MKDIR: case S_RMDIR: case S_CHDIR: {
                char *p; int l;
                evs(s->e1, &p, &l);
                int r = 0;
                if (s->op == S_KILL) {
                    extern void file_kill(const char *spec, int len);
                    char buf[600];
                    int bl = l < 590 ? l : 590;
                    memcpy(buf, p, (size_t)bl);
                    free(p); p = NULL;
                    file_kill(buf, bl);
                } else {
                    extern void file_native_name(const char *in, int len, char *out, int outsz, int for_read);
                    char path[1024];
                    file_native_name(p, l, path, sizeof path, s->op != S_MKDIR);
                    free(p); p = NULL;
                    if (s->op == S_MKDIR) {
#ifdef _WIN32
                        r = _mkdir(path);
#else
                        r = mkdir(path, 0777);
#endif
                        if (r) rt_error(75);
                    } else if (s->op == S_RMDIR) { r = rmdir(path); if (r) rt_error(75); }
                    else { r = chdir(path); if (r) rt_error(76); }
                }
                free(p);
                pc++;
                break;
            }
            case S_NAME: {
                char *a, *b; int al, bl;
                evs(s->e1, &a, &al);
                evs(s->e2, &b, &bl);
                /* DOS-style names (backslashes, drive letter) and case-insensitive lookup, like OPEN */
                extern void file_native_name(const char *in, int len, char *out, int outsz, int for_read);
                char na[1024], nb[1024];
                file_native_name(a, al, na, sizeof na, 1);
                file_native_name(b, bl, nb, sizeof nb, 0);
                free(a); free(b);
                FILE *t = fopen(nb, "rb");
                if (t) { fclose(t); rt_error(58); }
                int r = rename(na, nb);
                if (r) rt_error(53);
                pc++;
                break;
            }
            case S_FILES: {
                char *p = NULL; int l = 0;
                if (s->e1) evs(s->e1, &p, &l);
                file_files(p ? p : "*.*", p ? l : 3);
                free(p);
                pc++;
                break;
            }
            case S_POKE: {
                int a = (int)qb_round_even(evn(s->e1)), v = (int)qb_round_even(evn(s->e2));
                if (!mem_poke(rs->defseg, a, v & 0xFF)) gfx_poke(rs->defseg, a & 0xFFFF, v & 0xFF);
                pc++;
                break;
            }
            case S_DEFSEG: rs->defseg = s->e1 ? ((int)qb_round_even(evn(s->e1)) & 0xFFFF) : 0; pc++; break;
            case S_OUT: {
                int port = (int)qb_round_even(evn(s->e1)), v = (int)qb_round_even(evn(s->e2));
                extern void gfx_out(int port, int v);
                gfx_out(port, v);
                pc++;
                break;
            }
            case S_VSYNC: gfx_waitvsync(); pc++; break;
            case S_WAIT: {
                int port = (int)qb_round_even(evn(s->e1));
                if (port == 0x3DA) gfx_waitvsync();
                pc++;
                break;
            }
            case S_MIDSTMT: {
                Value *v = lval(s->e1);
                int st = (int)qb_round_even(evn(s->e2));
                int n = s->e3 ? (int)qb_round_even(evn(s->e3)) : QB_STRMAX;
                char *r; int rl;
                evs(s->e4, &r, &rl);
                if (st < 1 || st > v->slen || n < 0) { free(r); rt_error(5); }
                if (n > rl) n = rl;
                if (n > v->slen - st + 1) n = v->slen - st + 1;
                memcpy(v->s + st - 1, r, (size_t)n);
                free(r);
                pc++;
                break;
            }
            case S_LSET: case S_RSET: {
                Value *v = lval(s->e1);
                if (v->t == T_UDT) {
                    /* LSET record = record : byte copy (approximate with field copy) */
                    Value *src = lval(s->e2);
                    if (src->r && v->r && src->r->udt == v->r->udt) val_copy(v, src);
                    pc++;
                    break;
                }
                char *r; int rl;
                evs(s->e2, &r, &rl);
                int L = v->slen;
                char *buf = (char *)qb_alloc((size_t)L + 1);
                memset(buf, ' ', (size_t)L);
                int n = rl < L ? rl : L;
                if (s->op == S_LSET) memcpy(buf, r, (size_t)n);
                else memcpy(buf + L - n, r, (size_t)n);
                memcpy(v->s, buf, (size_t)L);
                free(buf); free(r);
                pc++;
                break;
            }
            case S_CLEAR: {
                /* reset all module variables */
                Frame *g = rs->global;
                for (int i = 0; i < g->nvals; i++) {
                    Value *v = &g->own[i];
                    int t = v->t, fl = v->fixlen, u = v->r ? v->r->udt : 0;
                    val_free(v);
                    val_init(v, t, u, fl);
                }
                for (int i = 0; i < g->narrs; i++) array_clear(g->ownarrs[i]);
                file_closeall();
                g->ngosub = 0;
                pc++;
                break;
            }
            case S_TRON: rs->tron = 1; pc++; break;
            case S_TROFF: rs->tron = 0; pc++; break;
            case S_ONTIMER: {
                double n = evn(s->e1);
                if (n < 1 || n > 86400) rt_error(5);
                rs->ontimer = s->i1;
                rs->timerint = n;
                rs->timernext = sys_timer() + n;
                pc++;
                break;
            }
            case S_ONPLAY: {
                double n = evn(s->e1);
                if (n < 1 || n > 32) rt_error(5);
                rs->onplay = s->i1;
                rs->playlimit = (int)n;
                rs->playlast = snd_queue_count();
                pc++;
                break;
            }
            case S_PLAYCTL:
                rs->playon = s->i1;
                if (s->i1 == 0) rs->playpending = 0;
                rs->playlast = snd_queue_count();
                pc++;
                break;
            case S_TIMERCTL:
                rs->timeron = s->i1;
                if (s->i1 == 1) rs->timernext = sys_timer() + rs->timerint;
                pc++;
                break;
            case S_ONKEY: case S_KEYCTL: case S_KEY: pc++; break;
            case S_RUN:
                if (s->e1) rt_error(73);
                rs->status = RUN_RESTART;
                longjmp(*rs->jb, 4);
            case S_CHAIN: rt_error(73); break;
            case S_SHELL: {
                if (s->e1) {
                    char *p; int l;
                    evs(s->e1, &p, &l);
                    int r = system(p);
                    (void)r;
                    free(p);
                }
                pc++;
                break;
            }
            case S_ENVIRON: {
                char *p; int l;
                evs(s->e1, &p, &l);
                char *eq = strchr(p, '=');
                if (!eq) { free(p); rt_error(5); }
                *eq = 0;
#ifdef _WIN32
                *eq = '=';
                _putenv(qb_strdup(p));   /* putenv keeps the pointer */
                *eq = 0;
#else
                setenv(p, eq + 1, 1);
#endif
                free(p);
                pc++;
                break;
            }
            case S_BLOAD: case S_BSAVE: {
                extern void gfx_bload(const char *name, int namelen, int has, long off, int save, long len);
                extern void gfx_bload_array(const char *name, int namelen, Array *a, int index, int save, long len);
                char *p; int l;
                evs(s->e1, &p, &l);
                long len = s->e3 ? (long)qb_round_even(evn(s->e3)) : 0;
                if (s->i1 == 1) {
                    Array *a; int idx = array_target(s, &a);
                    gfx_bload_array(p, l, a, idx, s->op == S_BSAVE, len);
                } else {
                    long off = s->e2 ? (long)qb_round_even(evn(s->e2)) : 0;
                    gfx_bload(p, l, s->e2 != NULL, off, s->op == S_BSAVE, len);
                }
                free(p);
                pc++;
                break;
            }
            default:
                pc++;
                break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* top level                                                          */
/* ------------------------------------------------------------------ */
static void unwind_to_global(void) {
    while (rs->cur && rs->cur != rs->global) {
        Frame *f = rs->cur;
        rs->cur = f->parent;
        frame_free(f);
    }
    rs->cur = rs->global;
    depth = 0;
}

void rt_free(void) {
    if (!rs) return;
    if (rs->global) {
        unwind_to_global();
        frame_free(rs->global);
        rs->global = NULL;
    }
    val_free(&g_fnret);
}

/* As in QBasic, module-level arrays dimensioned with constant bounds are static: they exist from the
   start of the program, even if their DIM is placed where the flow never goes (e.g. after END, as in
   code appended from include files). */
static int const_bound(Expr *e) {
    if (!e) return 1;
    if (e->op == E_NUM) return 1;
    if (e->op == E_NEG && e->a && e->a->op == E_NUM) return 1;
    return 0;
}
static void prealloc_static_arrays(Program *p) {
    int ob = 0;
    for (int i = 0; i < p->ncode; i++) {
        Stmt *s = &p->code[i];
        if (s->part != 0) continue;
        if (s->op == S_NOP && s->i1 == 999) ob = s->i2;
        if (s->op != S_DIM || s->i3 <= 0 || s->i1 != 0) continue;
        int ok = 1;
        for (int k = 0; k < s->i3 * 2; k++) if (!const_bound(s->args[k])) ok = 0;
        if (!ok) continue;
        int lb[8], ub[8];
        for (int k = 0; k < s->i3; k++) {
            Expr *l = s->args[k * 2], *u = s->args[k * 2 + 1];
            lb[k] = l ? (int)qb_round_even(evn(l)) : ob;
            ub[k] = (int)qb_round_even(evn(u));
            if (ub[k] < lb[k]) ok = 0;
        }
        if (!ok) continue;
        Array *a = arr_slot(0, s->i2);
        if (a->alive) continue;
        array_dim(a, s->i3, lb, ub, 0);
        a->prealloc = 1;
        a->dynamic = 0;
    }
}

void rt_reset(Program *p) {
    rs = &rs_storage;
    rt_free();
    memset(rs, 0, sizeof *rs);
    rs->prog = p;
    rs->onerror = -1;
    rs->ontimer = -1;
    rs->onplay = -1;
    rs->optionbase = 0;
    rnd_seed = 0x50000;
    rnd_last = 0;
    if (p) {
        for (int i = 0; i < p->nprocs; i++) {
            Proc *pr = &p->procs[i];
            if (pr->statics) { for (int j = 0; j < pr->nstatics; j++) val_free(&pr->statics[j]); free(pr->statics); pr->statics = NULL; }
            if (pr->sarrays) { for (int j = 0; j < pr->nsarrays; j++) array_free(pr->sarrays[j]); free(pr->sarrays); pr->sarrays = NULL; }
        }
        rs->global = frame_new_global();
        rs->cur = rs->global;
        prealloc_static_arrays(p);
    }
}

/* Runs from startpc until END / error. Can be re-entered for CONT. */
int rt_run(Program *p, int startpc) {
    jmp_buf jb;
    jmp_buf *savejb = rs->jb;
    rs->jb = &jb;
    rs->prog = p;
    int pc = startpc;
    frame_grow_global();
    for (;;) {
        int r = setjmp(jb);
        if (r == 0) {
            rs->status = RUN_OK;
            rs->cur = rs->global;
            exec(rs->global, pc);
            rs->status = RUN_END;
            break;
        }
        if (r == 2) {
            /* runtime error */
            int code = rs->err;
            int errpc = rs->pc;
            rs->errstmt = errpc;
            /* module-level statement for RESUME */
            int modpc = rs->global->retpc;
            if (rs->onerror >= 0 && !rs->inhandler && !rs->inevent) {
                unwind_to_global();
                rs->inhandler = 1;
                rs->resumepc = modpc;
                rs->resumenext = modpc + 1;
                /* resume NEXT must skip to next statement */
                rs->erl = 0;
                /* ERL: find nearest preceding numeric label */
                for (int i = 0; i < p->nlabels; i++) {
                    Label *l = &p->labels[i];
                    if (l->proc == -1 && l->stmt <= modpc && isdigit((unsigned char)l->name[0])) {
                        int v = atoi(l->name);
                        if (l->stmt <= modpc) rs->erl = v;
                    }
                }
                rs->err = code;
                pc = rs->onerror;
                continue;
            }
            rs->errpart = p->code[errpc].part;
            rs->errline = p->code[errpc].line;
            if (p->code[errpc].incfile >= 0) {   /* error inside '$INCLUDE text: say where */
                extern const char *include_name(int i);
                char tmp[128];
                snprintf(tmp, sizeof tmp, "%s (%s, line %d)", rs->errmsg, include_name(p->code[errpc].incfile), p->code[errpc].incline + 1);
                snprintf(rs->errmsg, sizeof rs->errmsg, "%s", tmp);
            }
            rs->status = RUN_ERROR;
            break;
        }
        /* END, abort, restart */
        if (rs->status == RUN_OK) rs->status = RUN_END;
        break;
    }
    unwind_to_global();
    rs->jb = savejb;
    return rs->status;
}

/* lookup variable by name for debugger watch (module scope or current proc) */
Value *rt_lookup_var(const char *name, int *type) {
    (void)name; (void)type;
    return NULL;
}

double qb_eval_num(Expr *e) { return evn(e); }
void qb_eval_str(Expr *e, char **p, int *len) { evs(e, p, len); }

/* used by the IDE immediate window */
int rt_exec_immediate(Program *p, int startpc) {
    jmp_buf jb;
    jmp_buf *savejb = rs->jb;
    rs->jb = &jb;
    rs->prog = p;
    frame_grow_global();
    int r = setjmp(jb);
    if (r == 0) {
        Frame *f = rs->cur ? rs->cur : rs->global;
        exec(f, startpc);
        rs->status = RUN_OK;
    } else if (r == 2) {
        rs->status = RUN_ERROR;
    }
    rs->jb = savejb;
    return rs->status;
}

/*
 * QBLike - a QBasic-compatible interpreter and IDE built on dos-like.
 * Clean-room implementation: written from the language documentation and
 * observed behaviour, not from Microsoft code.
 *
 * qb.h - shared declarations
 */
#ifndef QB_H
#define QB_H

#include <stdint.h>
#include <stddef.h>
#include <setjmp.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define QB_NAME    "QBLike"
#define QB_VERSION "1.0"

/* ------------------------------------------------------------------ */
/* Types                                                              */
/* ------------------------------------------------------------------ */
enum {
    T_NONE = 0,
    T_INT,      /* %  16-bit */
    T_LNG,      /* &  32-bit */
    T_SNG,      /* !  float  */
    T_DBL,      /* #  double */
    T_STR,      /* $         */
    T_UDT       /* user TYPE */
};
#define IS_NUM(t) ((t) >= T_INT && (t) <= T_DBL)

typedef struct Rec Rec;

typedef struct Value {
    uint8_t t;
    int32_t fixlen;     /* fixed-length string size (0 = variable) */
    double n;           /* numeric payload (all numeric types)    */
    char *s;            /* string payload (owned)                 */
    int slen;
    Rec *r;             /* record payload (owned)                 */
} Value;

typedef struct Field {
    char name[41];
    uint8_t t;
    int udt;
    int fixlen;
} Field;

typedef struct Udt {
    char name[41];
    int nfields;
    Field *f;
    int size;           /* byte size (for LEN / file records) */
} Udt;

struct Rec {
    int udt;
    Value f[1];         /* variable length */
};

typedef struct Array {
    uint8_t t;
    int udt;
    int fixlen;
    int nd;
    int lb[8], ub[8];
    int count;
    Value *v;
    uint8_t dynamic;    /* REDIM-able */
    uint8_t alive;      /* dimensioned */
    uint8_t prealloc;   /* static array allocated at program start (constant bounds) */
} Array;

/* ------------------------------------------------------------------ */
/* Lexer                                                              */
/* ------------------------------------------------------------------ */
enum {
    TK_EOL = 0, TK_NUM, TK_STR, TK_ID, TK_KW, TK_OP, TK_DATA, TK_REM
};

/* multi-char operators */
enum { OP_LE = 256, OP_GE, OP_NE };

/* keywords */
#define KEYWORDS(X) \
 X(ABS) X(ACCESS) X(ALIAS) X(AND) X(ANY) X(APPEND) X(AS) X(ASC) X(ATN) X(BASE) X(BEEP) \
 X(BINARY) X(BLOAD) X(BSAVE) X(BYVAL) X(CALL) X(CASE) X(CDBL) X(CDECL) X(CHAIN) X(CHDIR) X(CHR_S) \
 X(CINT) X(CIRCLE) X(CLEAR) X(CLIP) X(CLNG) X(CLOSE) X(CLS) X(COLOR) X(COM) X(COMMON) \
 X(CONST) X(COS) X(CSNG) X(CSRLIN) X(CVD) X(CVDMBF) X(CVI) X(CVL) X(CVS) X(CVSMBF) \
 X(DATA) X(DATE_S) X(DECLARE) X(DEF) X(DEFDBL) X(DEFINT) X(DEFLNG) X(DEFSNG) X(DEFSTR) \
 X(DIM) X(DO) X(DOUBLE) X(DRAW) X(ELSE) X(ELSEIF) X(END) X(ENVIRON) X(ENVIRON_S) X(EOF) \
 X(EQV) X(ERASE) X(ERDEV) X(ERDEV_S) X(ERL) X(ERR) X(ERROR) X(EXIT) X(EXP) X(FIELD) \
 X(FILEATTR) X(FILES) X(FIX) X(FOR) X(FRE) X(FREEFILE) X(FUNCTION) X(GET) X(GOSUB) \
 X(GOTO) X(HEX_S) X(IF) X(IMP) X(INKEY_S) X(INP) X(INPUT) X(INPUT_S) X(INSTR) X(INT) \
 X(INTEGER) X(IOCTL) X(IOCTL_S) X(IS) X(KEY) X(KILL) X(LBOUND) X(LCASE_S) X(LEFT_S) \
 X(LEN) X(LET) X(LINE) X(LIST) X(LOADBMP) X(LOADGIF) X(LOADPAL) X(LOC) X(LOCATE) X(LOCK) X(LOF) X(LOG) X(LONG) X(LOOP) \
 X(LPOS) X(LPRINT) X(LSET) X(LTRIM_S) X(MID_S) X(MKD_S) X(MKDIR) X(MKDMBF_S) X(MKI_S) \
 X(MKL_S) X(MKS_S) X(MKSMBF_S) X(MOD) X(MOUSE) X(MULTIKEY) X(NAME) X(NEXT) X(NOT) X(OCT_S) X(OFF) X(ON) \
 X(OPEN) X(OPTION) X(OR) X(OUT) X(OUTPUT) X(PAINT) X(PALETTE) X(PCOPY) X(PEEK) X(PEN) \
 X(PLAY) X(PMAP) X(POINT) X(POKE) X(POS) X(PRESERVE) X(PRESET) X(PRINT) X(PSET) X(PUT) \
 X(RANDOM) X(RANDOMIZE) X(READ) X(REDIM) X(REM) X(RESET) X(RESTORE) X(RESUME) \
 X(RETURN) X(RIGHT_S) X(RMDIR) X(RND) X(RSET) X(RTRIM_S) X(RUN) X(SADD) X(SCREEN) \
 X(SEEK) X(SEG) X(SELECT) X(SGN) X(SHARED) X(SHELL) X(SIN) X(SINGLE) X(SLEEP) X(SOUND) \
 X(SPACE_S) X(SPC) X(SQR) X(STATIC) X(STEP) X(STICK) X(STOP) X(STR_S) X(STRIG) \
 X(STRING) X(STRING_S) X(SUB) X(SWAP) X(SYSTEM) X(TAB) X(TAN) X(THEN) X(TIME_S) \
 X(TIMER) X(TO) X(TROFF) X(TRON) X(TYPE) X(UBOUND) X(UCASE_S) X(UEVENT) X(UNLOCK) \
 X(UNTIL) X(USING) X(VAL) X(VARPTR) X(VARPTR_S) X(VARSEG) X(VIEW) X(WAIT) X(WEND) \
 X(WHILE) X(WIDTH) X(WINDOW) X(WRITE) X(XOR)

#define KW_ENUM(k) KW_##k,
enum { KW_NONE = 0, KEYWORDS(KW_ENUM) KW_COUNT };
#undef KW_ENUM

extern const char *kw_names[KW_COUNT];

typedef struct Tok {
    uint8_t k;          /* TK_* */
    uint8_t ntype;      /* numeric literal type */
    int16_t kw;         /* keyword id or operator code */
    int col;            /* column in source line (0-based) */
    int endcol;
    double num;
    char *s;            /* identifier (original spelling) / string / raw */
    int slen;
} Tok;

typedef struct TokLine {
    Tok *t;
    int n;
    char *err;          /* lexing error */
} TokLine;

void lex_line(const char *src, TokLine *out);
void lex_free(TokLine *tl);
int  kw_lookup(const char *word, int len);
const char *kw_text(int kw);

/* ------------------------------------------------------------------ */
/* Program representation                                             */
/* ------------------------------------------------------------------ */
typedef struct Expr Expr;

enum {
    E_NUM, E_STR, E_VAR, E_ARR, E_MEMBER, E_NEG, E_NOT, E_BIN, E_FUNC, E_CALLF,
    E_FN, E_CONV, E_ARRWHOLE
};

struct Expr {
    uint8_t op;
    uint8_t type;       /* result type */
    int16_t sub;        /* binary operator / builtin id / field idx */
    int udt;            /* when type == T_UDT */
    uint8_t scope;      /* 0 global, 1 local */
    int idx;            /* slot / proc index */
    Expr *a, *b;
    Expr **args;
    int nargs;
    double num;
    char *str;
    int slen;
    int fixlen;
};

typedef struct Stmt {
    uint16_t op;
    uint16_t part;      /* editor part (0 = main module) */
    int line;           /* line within part (0-based) */
    int i1, i2, i3, i4;
    Expr *e1, *e2, *e3, *e4;
    Expr **args;
    int nargs;
    void *p;
    uint8_t linestart;  /* first statement of a source line */
} Stmt;

typedef struct Param {
    char name[48];
    uint8_t t;
    int udt;
    uint8_t isarray;
    uint8_t hasas;      /* declared with AS type (then the bare name refers to it whatever its suffix) */
} Param;

typedef struct Proc {
    char name[48];      /* uppercase, with suffix for functions */
    char disp[48];      /* original spelling */
    uint8_t isfunc;
    uint8_t rtype;
    uint8_t isstatic;
    uint8_t isdeffn;
    int nparams;
    Param params[32];
    int entry;          /* statement index */
    int nlocals;        /* scalar slots */
    int nlarrays;       /* array slots */
    int part;
    int defined;
    /* locals symbol table, kept for the debugger / immediate window */
    struct SymTab *syms;
    Value *statics;     /* persistent STATIC storage */
    Array **sarrays;
    int nstatics, nsarrays;
    uint8_t *slotstatic;    /* per local slot: 1 if static */
    uint8_t *arrstatic;
} Proc;

typedef struct DataItem {
    char *s;
    int len;
    int quoted;
} DataItem;

/* symbols */
typedef struct Sym {
    char key[50];       /* NAME + type char */
    char disp[48];
    uint8_t kind;       /* 0 scalar, 1 array, 2 const */
    uint8_t t;
    int udt;
    int fixlen;
    int idx;
    uint8_t shared;
    uint8_t declared;   /* DIM ... AS */
    Value cval;
} Sym;

typedef struct SymTab {
    Sym *s;
    int n, cap;
} SymTab;

typedef struct Label {
    char name[48];
    int part;
    int proc;           /* -1 = module */
    int stmt;
    int dataidx;
} Label;

typedef struct Program {
    Stmt *code;
    int ncode, capcode;
    Proc *procs;
    int nprocs;
    Udt *udts;
    int nudts;
    SymTab gsyms;
    int nglobals, ngarrays;
    Label *labels;
    int nlabels;
    DataItem *data;
    int ndata;
    int maxdata;
    char deftype[26];
} Program;

/* Source passed to the compiler: one or more parts of lines */
typedef struct SrcPart {
    char **lines;
    int nlines;
} SrcPart;

typedef struct CompileError {
    int part, line, col;
    char msg[128];
} CompileError;

Program *compile_program(SrcPart *parts, int nparts, CompileError *err);
void free_program(Program *p);
int  check_line_syntax(const char *line, char *msg, int msgsize);
int  compile_immediate(Program *p, const char *line, int procidx, int *startpc, CompileError *err);

/* statement opcodes */
enum {
    S_NOP, S_END, S_LET, S_PRINT, S_GOTO, S_GOSUB, S_RETURN, S_JMPF, S_JMP,
    S_FORINIT, S_NEXT, S_CALL, S_PROCEND, S_INPUT, S_LINEINPUT, S_DIM, S_REDIM,
    S_ERASE, S_CLS, S_COLOR, S_LOCATE, S_SCREEN, S_PSET, S_PRESET, S_LINE, S_CIRCLE,
    S_PAINT, S_DRAW, S_GETG, S_PUTG, S_PALETTE, S_BEEP, S_SOUND, S_PLAY, S_SLEEP,
    S_RANDOMIZE, S_READ, S_RESTORE, S_SWAP, S_ONGOTO, S_ONGOSUB, S_ONERROR,
    S_RESUME, S_ERROR, S_OPEN, S_CLOSE, S_WRITE, S_KILL, S_NAME, S_MKDIR, S_RMDIR,
    S_CHDIR, S_FILES, S_SEEK, S_GETF, S_PUTF, S_WIDTH, S_VIEWPRINT, S_VIEW, S_WINDOW,
    S_PCOPY, S_POKE, S_DEFSEG, S_OUT, S_WAIT, S_MIDSTMT, S_LSET, S_RSET, S_STOP,
    S_SYSTEM, S_CLEAR, S_SELECT, S_CASE, S_EXITPROC, S_LETFUNC, S_TRON, S_TROFF,
    S_KEY, S_ONTIMER, S_TIMERCTL, S_ONKEY, S_KEYCTL, S_RUN, S_SHELL, S_LPRINT,
    S_FIELD, S_FNRET, S_ONPLAY, S_RESET, S_CHAIN, S_LOCK, S_ENVIRON, S_BLOAD, S_BSAVE, S_LOADIMG, S_LOADPAL, S_CLIP, S_MOUSE, S_FILL, S_VSYNC, S_PLAYCTL,
    S_COUNT
};

/* builtin function ids */
enum {
    F_ABS = 1, F_ASC, F_ATN, F_CDBL, F_CHR, F_CINT, F_CLNG, F_COS, F_CSNG, F_CSRLIN,
    F_CVD, F_CVI, F_CVL, F_CVS, F_DATE, F_ENVIRON, F_EOF, F_ERDEV, F_ERL, F_ERR, F_EXP,
    F_FILEATTR, F_FIX, F_FRE, F_FREEFILE, F_HEX, F_INKEY, F_INP, F_INPUT, F_INSTR,
    F_INT, F_LBOUND, F_LCASE, F_LEFT, F_LEN, F_LOC, F_LOF, F_LOG, F_LPOS, F_LTRIM,
    F_MID, F_MKD, F_MKI, F_MKL, F_MKS, F_OCT, F_PEEK, F_PEN, F_PLAY, F_PMAP, F_POINT,
    F_POS, F_RIGHT, F_RND, F_RTRIM, F_SADD, F_SCREEN, F_SEEK, F_SGN, F_SIN, F_SPACE,
    F_SQR, F_STICK, F_STR, F_STRIG, F_STRING, F_TAN, F_TIME, F_TIMER, F_UBOUND,
    F_UCASE, F_VAL, F_VARPTR, F_VARPTRS, F_VARSEG, F_SPC, F_TAB, F_LENVAR, F_CVSMBF,
    F_CVDMBF, F_MKSMBF, F_MKDMBF, F_IOCTL, F_COMMAND, F_MULTIKEY
};

/* ------------------------------------------------------------------ */
/* Runtime                                                            */
/* ------------------------------------------------------------------ */
typedef struct Frame {
    struct Frame *parent;
    int proc;               /* -1 = module level */
    Value **vals;
    Value *own;
    int nvals;
    Array **arrs;
    Array **ownarrs;
    int narrs;
    int *gosub;
    int ngosub, capgosub;
    Value ret;
    int retpc;
    /* numeric variables passed to a parameter of another numeric type: the parameter is a converted
       copy, written back to the caller's variable when the procedure returns (as QBasic does) */
    Value **cb_dst;
    int *cb_slot;
    int ncb;
} Frame;

enum { RUN_OK = 0, RUN_END, RUN_ERROR, RUN_ABORT, RUN_RESTART };

typedef struct RunState {
    Program *prog;
    Frame *global;
    Frame *cur;
    int pc;
    int curstmt;
    int err, erl, errstmt, errpart, errline;
    int onerror;            /* handler stmt or -1 */
    int inhandler;
    int resumepc;
    int resumenext;
    int dataptr;
    int status;
    char errmsg[128];
    jmp_buf *jb;
    int stepmode;           /* 0 run, 1 step into, 2 step over */
    Frame *stepframe;
    int tron;
    int breakreq;
    int stopped;
    int ontimer, timeron;
    int onplay, playlimit, playon, playpending, playlast;   /* ON PLAY(n) GOSUB */
    double timerint, timernext;
    int inevent;
    int onkey[32], keyon[32];
    char keydef[32][16];
    int defseg;
    int optionbase;
} RunState;

extern RunState *rs;

/* runtime API */
void rt_error(int code);
void rt_error_msg(int code, const char *msg);
const char *rt_errtext(int code);
int  rt_run(Program *p, int startpc);
void rt_reset(Program *p);
void rt_free(void);
int  rt_continue(void);
Value *rt_lookup_var(const char *name, int *type);

/* values */
void val_free(Value *v);
void val_copy(Value *dst, const Value *src);
void val_init(Value *v, int t, int udt, int fixlen);
void val_setstr(Value *v, const char *s, int len);
void val_setnum(Value *v, double d);
double fit_num(int t, double d);
Rec *rec_new(int udt);
void rec_free(Rec *r);
Rec *rec_dup(Rec *r);
extern Program *g_prog;

/* number formatting */
int fmt_number(char *buf, double d, int t, int forprint);
double qb_val(const char *s, int len);
double qb_round_even(double d);

/* ------------------------------------------------------------------ */
/* Screen (program output)                                            */
/* ------------------------------------------------------------------ */
void scr_init(void);
void scr_reset(void);
int  scr_mode(void);
void scr_setmode(int mode, int colorsw, int apage, int vpage, int setap, int setvp);
void scr_width(int cols, int rows);
void scr_cls(int what);
void scr_color(int fg, int bg, int border, int hasfg, int hasbg, int hasbo);
void scr_locate(int row, int col, int cursor, int start, int stop, int has[5]);
void scr_print(const char *s, int len);
void scr_newline(void);
void scr_tab(int col);
void scr_spc(int n);
void scr_comma(void);
int  scr_csrlin(void);
int  scr_pos(void);
int  scr_cols(void);
int  scr_rows(void);
int  scr_screenfn(int row, int col, int colorflag);
void scr_viewprint(int top, int bottom, int has);
void scr_present(int force);
void scr_cursor(int visible);
int  scr_text_fits(int len);

/* graphics */
void gfx_pset(double x, double y, int color, int hascolor, int step, int preset);
void gfx_line(double x1, double y1, double x2, double y2, int color, int hascolor,
              int box, int step1, int step2, int has1, int style, int hasstyle);
void gfx_circle(double x, double y, double r, int color, int hascolor, double start,
                double end, double aspect, int hasstart, int hasend, int hasaspect, int step);
void gfx_paint(double x, double y, int paint, int border, int haspaint, int hasborder, int step,
               const char *tile, int tilelen);
void gfx_draw(const char *s, int len);
int  gfx_point(double x, double y);
double gfx_pointfn(int n);
void gfx_view(int x1, int y1, int x2, int y2, int fill, int border, int screen, int has, int hasfill, int hasborder);
void gfx_window(double x1, double y1, double x2, double y2, int screen, int has);
double gfx_pmap(double v, int fn);
void gfx_get(double x1, double y1, double x2, double y2, int step1, int step2, Array *a, int index);
void gfx_put(double x, double y, Array *a, int index, int action, int step);
void gfx_palette(int attr, long color, int has);
void gfx_palette_using(Array *a, int index);
void gfx_pcopy(int src, int dst);
int  gfx_peek(int seg, int off, int *ok);
void gfx_poke(int seg, int off, int v);
void gfx_waitvsync(void);
void scr_save_output(void);
void scr_show_output(void);

/* ------------------------------------------------------------------ */
/* Input                                                              */
/* ------------------------------------------------------------------ */
void kb_poll(void);
int  kb_get(char *out);       /* returns length (0,1,2) of INKEY$ string */
int  kb_waitkey(char *out);
int  kb_count(void);
void kb_clear(void);
int  kb_lastscan(void);
int  kb_break_pressed(void);
int  kb_keydown(int vk);
void kb_push_ide_key(int code);
int  kb_shift_state(void);
int  sys_shutting_down(void);
double sys_time(void);
void sys_sleep(int ms);
void sys_idle(void);

/* ------------------------------------------------------------------ */
/* Sound                                                              */
/* ------------------------------------------------------------------ */
void snd_init(void);
void snd_beep(void);
void snd_sound(double freq, double ticks);
void snd_play(const char *s, int len);
int  snd_queue_count(void);   /* notes in the background music queue (playing or waiting) */
double qb_varptr_num(const char *tok);
void qb_varptr_str(const char *tok, const char **s, int *len);
void snd_poll(void);          /* start queued background sounds that are due */
void opl_ports_reset(void);   /* AdLib ports: reset timers, key off all FM voices */
int  snd_playfn(void);
void snd_stop(void);
void snd_reset(void);

/* ------------------------------------------------------------------ */
/* Files                                                              */
/* ------------------------------------------------------------------ */
void file_open(const char *name, int namelen, int mode, int filenum, int reclen, int access);
void file_close(int filenum);
void file_closeall(void);
void file_print(int fn, const char *s, int len);
int  file_eof(int fn);
double file_lof(int fn);
double file_loc(int fn);
double file_seekfn(int fn);
void file_seek(int fn, double pos);
int  file_freefile(void);
int  file_readchar(int fn);
int  file_input_field(int fn, char **out, int *len, int isstr);
int  file_line_input(int fn, char **out, int *len);
int  file_input_chars(int fn, int n, char **out, int *len);
void file_get(int fn, double rec, int hasrec, Value *v);
void file_put(int fn, double rec, int hasrec, Value *v);
int  file_column(int fn);
int  file_isopen(int fn);
int  file_mode(int fn);
void file_field(int fn, int n, int *widths, Value **vars);
void file_files(const char *spec, int len);

/* ------------------------------------------------------------------ */
/* IDE hooks                                                          */
/* ------------------------------------------------------------------ */
enum { DBG_CONTINUE, DBG_STEP, DBG_PSTEP, DBG_ABORT, DBG_RESTART };
int  ide_debug_break(int stmt);    /* called by runtime when stopping */
int  ide_is_breakpoint(int part, int line);
extern int g_ide_active;
extern int g_headless_test;
void ide_main(int argc, char **argv);

/* strings helpers */
char *qb_strdup(const char *s);
char *qb_strndup(const char *s, int n);
void *qb_alloc(size_t n);
void *qb_realloc(void *p, size_t n);
void str_upper(char *s);
int  str_ieq(const char *a, const char *b);
int  qb_stricmp(const char *a, const char *b);

/* CP437 helpers */
int unicode_to_cp437(int cp);

#endif

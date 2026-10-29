/* main.c - entry point for QBLike */
#include "qb.h"
#include "exepack.h"
#include "source.h"
#include "doslike/dos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_headless_test = 0;
char g_command[1024] = "";
void scr_dump_text(FILE *f);
void scr_dump_ppm(const char *path);
void scr_press_any_key(void);
void scr_set_ide_owner(int ide);

/* feed keys from QBLIKE_KEYS (test mode); escapes: \r \e \b \t \xNN, {F5} etc */
static void feed_test_keys(const char *k) {
    for (const char *p = k; *p; p++) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
                case 'r': kb_push_ide_key(13); break;
                case 'e': kb_push_ide_key(27); break;
                case 'b': kb_push_ide_key(8); break;
                case 't': kb_push_ide_key(9); break;
                case 'x': { int v = 0; sscanf(p + 1, "%2x", (unsigned *)&v); p += 2; kb_push_ide_key(v); break; }
                case 's': { int v = 0; sscanf(p + 1, "%2x", (unsigned *)&v); p += 2; kb_push_ide_key(v << 8); break; }
                default: kb_push_ide_key((unsigned char)*p); break;
            }
        } else kb_push_ide_key((unsigned char)*p);
    }
}

/* run a .BAS file without the IDE */
static int run_file(const char *path, int wait_end) {
    Doc d;
    doc_init(&d);
    int r = doc_load(&d, path);
    if (r <= 0) {
        fprintf(stderr, "Cannot open %s\n", path);
        if (!g_headless_test) {
            char msg[256];
            snprintf(msg, sizeof msg, "File not found: %s", path);
            scr_print(msg, (int)strlen(msg));
            scr_newline();
            scr_press_any_key();
        }
        return 1;
    }
    CompileError err;
    int nsp = 0;
    SrcPart *sp = doc_compile_parts(&d, path, &nsp, &err);
    Program *p = sp ? compile_program(sp, nsp, &err) : NULL;
    if (!p) {
        char msg[256];
        const char *pn = err.part == 0 ? "main module" : d.parts[err.part].name;
        snprintf(msg, sizeof msg, "%s in %s, line %d", err.msg, pn, err.line + 1);
        fprintf(stderr, "%s\n", msg);
        if (g_headless_test) { printf("COMPILE ERROR: %s\n", msg); fflush(stdout); }
        else { scr_print(msg, (int)strlen(msg)); scr_newline(); scr_press_any_key(); }
        srcparts_free(sp, nsp);
        doc_free(&d);
        return 2;
    }
    rt_reset(p);
    int st = rt_run(p, 0);
    int exitcode = 0;
    if (st == RUN_ERROR) {
        char msg[256];
        const char *pn = rs->errpart == 0 ? "main module" : d.parts[rs->errpart].name;
        snprintf(msg, sizeof msg, "%s in %s, line %d", rs->errmsg, pn, rs->errline + 1);
        fprintf(stderr, "Runtime error: %s\n", msg);
        if (g_headless_test) printf("RUNTIME ERROR: %s\n", msg);
        else { scr_newline(); scr_print(msg, (int)strlen(msg)); scr_newline(); }
        exitcode = 3;
    }
    int sysexit = rs->err == -1;
    file_closeall();
    if (g_headless_test) {
        const char *ppm = getenv("QBLIKE_PPM");
        if (ppm) scr_dump_ppm(ppm);
        scr_dump_text(stdout);
        fflush(stdout);
    } else if (wait_end && !sysexit && !sys_shutting_down()) {
        scr_press_any_key();
    }
    rt_free();
    free_program(p);
    srcparts_free(sp, nsp);
    doc_free(&d);
    return exitcode;
}

char *g_stack_base;   /* top of the program thread's stack, to detect "Out of stack space" */

static int make_exe_cli(int argc, char **argv);
static int g_cli_result = 0;

/* called by dos-like before the window opens. --make-exe works without opening a window. */
int qbl_prestart(int argc, char **argv, int *fullscreen) {
    (void)fullscreen;
    if (argc >= 2 && !strcmp(argv[1], "--make-exe")) { g_cli_result = make_exe_cli(argc, argv); exit(g_cli_result); }
    return 1;
}

/* qblike --make-exe PROGRAM.BAS [OUTPUT] [--fullscreen] [--target windows|linux|macos] */
static int make_exe_cli(int argc, char **argv) {
    extern int make_exe(const char *in, const char *out, int flags, int target, char *err, int errsz);
    const char *in = NULL, *out = NULL;
    int flags = 0, target = exepack_host_target();
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--fullscreen")) flags |= EXE_FULLSCREEN;
        else if (!strcmp(argv[i], "--window")) flags &= ~EXE_FULLSCREEN;
        else if (!strcmp(argv[i], "--target") && i + 1 < argc) {
            const char *t = argv[++i];
            target = str_ieq(t, "windows") ? EXE_TARGET_WINDOWS : str_ieq(t, "macos") ? EXE_TARGET_MACOS : EXE_TARGET_LINUX;
        } else if (!in) in = argv[i];
        else if (!out) out = argv[i];
    }
    if (!in) { fprintf(stderr, "usage: qblike --make-exe PROGRAM.BAS [OUTPUT] [--fullscreen] [--target windows|linux|macos]\n"); return 1; }
    char outbuf[1024];
    if (!out) {
        snprintf(outbuf, sizeof outbuf, "%s", in);
        char *dot = strrchr(outbuf, '.');
        char *sl = strrchr(outbuf, '/'), *bs = strrchr(outbuf, '\\');
        if (dot && dot > (sl ? sl : outbuf) && dot > (bs ? bs : outbuf)) *dot = 0;
        if (target == EXE_TARGET_WINDOWS) strcat(outbuf, ".exe");
        out = outbuf;
    }
    char err[1100];
    int r = make_exe(in, out, flags, target, err, sizeof err);
    if (!r) { fprintf(stderr, "%s\n", err); return 1; }
    printf("%s\n", out);
    return 0;
}

/* builds a stand-alone executable from a .BAS file (also used by File > Make EXE) */
int make_exe(const char *in, const char *out, int flags, int target, char *err, int errsz) {
    FILE *f = fopen(in, "rb");
    if (!f) { snprintf(err, (size_t)errsz, "File not found: %s", in); return 0; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = (char *)qb_alloc((size_t)n + 1);
    n = (long)fread(src, 1, (size_t)n, f);
    fclose(f);
    /* check that it compiles, so a broken program is not packed */
    Doc d;
    doc_init(&d);
    if (doc_load_mem(&d, src, (int)n) <= 0) { doc_free(&d); free(src); snprintf(err, (size_t)errsz, "Cannot read %s", in); return 0; }
    CompileError ce;
    int nsp = 0;
    SrcPart *sp = doc_compile_parts(&d, in, &nsp, &ce);
    Program *p = sp ? compile_program(sp, nsp, &ce) : NULL;
    if (!p) {
        snprintf(err, (size_t)errsz, "%s in %s, line %d", ce.msg, ce.part == 0 ? "main module" : d.parts[ce.part].name, ce.line + 1);
        srcparts_free(sp, nsp); doc_free(&d); free(src);
        return 0;
    }
    free_program(p);
    /* the executable gets the program with its '$INCLUDE files already pasted in */
    free(src);
    int tl = 0;
    src = srcparts_to_text(sp, nsp, &tl);
    n = tl;
    srcparts_free(sp, nsp);
    doc_free(&d);
    char rt[1024];
    if (!exepack_find_runtime(target, rt, sizeof rt)) {
        snprintf(err, (size_t)errsz, "Runtime not found: %s must be next to QBLike",
                 target == EXE_TARGET_WINDOWS ? "qbrun.exe" : "qbrun");
        free(src);
        return 0;
    }
    const char *base = in;
    for (const char *c = in; *c; c++) if (*c == '/' || *c == '\\') base = c + 1;
    int ok = exepack_write(rt, out, base, src, (int)n, flags, err, errsz);
    free(src);
    return ok;
}

int main(int argc, char *argv[]) {
    char stack_mark;
    g_stack_base = &stack_mark;
    const char *file = NULL;
    int run = 0, runexit = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (str_ieq(a, "/RUN") || str_ieq(a, "-run") || str_ieq(a, "--run")) run = 1;
        else if (str_ieq(a, "/X") || str_ieq(a, "-x") || str_ieq(a, "--exec")) runexit = 1;
        else if (str_ieq(a, "-w") || str_ieq(a, "--window")) continue;
        else if (a[0] != '-' || a[1] == 0) {
            if (!file) file = a;
            else {   /* what follows the program name is its COMMAND$ */
                size_t l = strlen(g_command);
                snprintf(g_command + l, sizeof g_command - l, "%s%s", l ? " " : "", a);
            }
        }
    }
    /* hidden test aid: qblike --beautify file.bas  prints the formatted program */
    if (argc >= 2 && !strcmp(argv[1], "--help-dump")) {   /* raw help text, see tools/help2md.py */
        extern void help_dump(void);
        help_dump();
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--beautify")) {
        extern char **qb_beautify(char **lines, int n, int base, int indent, int (*fmt)(const char *, char *, int), int *outn);
        extern int ide_format_text(const char *in, char *out, int outsz);
        FILE *f = fopen(argv[2], "rb");
        if (!f) return 2;
        char **lines = NULL; int n = 0, cap = 0; char buf[4096];
        while (fgets(buf, sizeof buf, f)) {
            size_t l = strlen(buf);
            while (l && (buf[l - 1] == '\n' || buf[l - 1] == '\r')) buf[--l] = 0;
            if (n == cap) { cap = cap ? cap * 2 : 256; lines = (char **)realloc(lines, sizeof(char *) * (size_t)cap); }
            lines[n++] = qb_strdup(buf);
        }
        fclose(f);
        int on = 0;
        char **out = qb_beautify(lines, n, 0, argc >= 4 ? atoi(argv[3]) : 4, ide_format_text, &on);
        for (int i = 0; i < on; i++) printf("%s\n", out[i]);
        return 0;
    }
    if (getenv("QBLIKE_TEST")) g_headless_test = 1;
    scr_init();
    snd_init();
    if (g_headless_test) {
        const char *k = getenv("QBLIKE_KEYS");
        if (k) feed_test_keys(k);
    }
    if (!runexit) g_command[0] = 0;   /* programs run from the editor get COMMAND$ = "" */
    if (runexit && file) {
        extern void ide_apply_config(void);
        if (!g_headless_test) ide_apply_config();
        scr_set_ide_owner(0);
        int r = run_file(file, 1);
        return r;
    }
    (void)run;
    ide_main(argc, argv);
    return 0;
}

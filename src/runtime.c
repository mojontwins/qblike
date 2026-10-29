/* runtime.c - entry point of qbrun, the QBLike run-time module (like BRUN45.EXE).

   qbrun contains the interpreter but not the editor. By itself it does nothing: File > Make EXE
   (or qblike --make-exe) appends a program to a copy of it, and that copy runs the program
   directly, in a window or full screen. */
#include "qb.h"
#include "exepack.h"
#include "source.h"
#include "doslike/dos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void scr_press_any_key(void);
void scr_set_ide_owner(int ide);

int g_headless_test = 0;
char g_command[1024] = "";
char *g_stack_base;
int g_ide_active = 0;

static char *prog_name, *prog_src;
static int prog_len, prog_flags;

/* no editor: Ctrl+Break and STOP end the program, there are no breakpoints */
int ide_debug_break(int stmt) { (void)stmt; return DBG_ABORT; }
int ide_is_breakpoint(int part, int line) { (void)part; (void)line; return 0; }

/* before the window opens: without an appended program, exit at once */
int qbl_prestart(int argc, char **argv, int *fullscreen) {
    if (!exepack_read_self(&prog_name, &prog_src, &prog_len, &prog_flags)) return 0;
    *fullscreen = (prog_flags & EXE_FULLSCREEN) ? 1 : 0;
    for (int i = 1; i < argc; i++) {   /* the user can still choose: PROGRAM -w / --fullscreen */
        if (!strcmp(argv[i], "--fullscreen")) *fullscreen = 1;
        if (!strcmp(argv[i], "-w") || !strcmp(argv[i], "--window")) *fullscreen = 0;
    }
    return 1;
}

static void show_error(const char *msg) {
    scr_set_ide_owner(0);
    scr_newline();
    scr_print(msg, (int)strlen(msg));
    scr_newline();
    scr_press_any_key();
}

int main(int argc, char *argv[]) {
    char stack_mark;
    g_stack_base = &stack_mark;
    if (getenv("QBLIKE_TEST")) g_headless_test = 1;
    for (int i = 1; i < argc; i++) {   /* COMMAND$ */
        if (!strcmp(argv[i], "--fullscreen") || !strcmp(argv[i], "--window") || !strcmp(argv[i], "-w")) continue;
        size_t l = strlen(g_command);
        snprintf(g_command + l, sizeof g_command - l, "%s%s", l ? " " : "", argv[i]);
    }
    scr_init();
    snd_init();
    scr_set_ide_owner(0);
    if (!prog_src) return 0;

    Doc d;
    doc_init(&d);
    if (doc_load_mem(&d, prog_src, prog_len) <= 0) { show_error("Bad program"); return 1; }
    CompileError err;
    int nsp = 0;
    SrcPart *sp = doc_compile_parts(&d, NULL, &nsp, &err);
    Program *p = sp ? compile_program(sp, nsp, &err) : NULL;
    if (!p) {
        char msg[256];
        snprintf(msg, sizeof msg, "%s in %s, line %d", err.msg, err.part == 0 ? "main module" : d.parts[err.part].name, err.line + 1);
        show_error(msg);
        return 2;
    }
    int st;
    do { rt_reset(p); scr_reset(); snd_reset(); file_closeall(); st = rt_run(p, 0); } while (st == RUN_RESTART);
    int code = 0;
    if (st == RUN_ERROR) {
        /* like a compiled QuickBASIC program: report the error and stop */
        char msg[300];
        snprintf(msg, sizeof msg, "%s in %s, line %d", rs->errmsg,
                 rs->errpart == 0 ? "main module" : d.parts[rs->errpart].name, rs->errline + 1);
        if (g_headless_test) printf("RUNTIME ERROR: %s\n", msg);
        else show_error(msg);
        code = 3;
    }
    file_closeall();
    if (g_headless_test) {
        void scr_dump_text(FILE *f);
        scr_dump_text(stdout);
        fflush(stdout);
    }
    snd_end();
    (void)prog_name;
    return code;
}

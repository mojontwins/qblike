/* main.c - entry point for QBLike */
#include "qb.h"
#include "source.h"
#include "doslike/dos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_headless_test = 0;
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
    SrcPart *sp = doc_srcparts(&d);
    CompileError err;
    Program *p = compile_program(sp, d.nparts, &err);
    if (!p) {
        char msg[256];
        const char *pn = err.part == 0 ? "main module" : d.parts[err.part].name;
        snprintf(msg, sizeof msg, "%s in %s, line %d", err.msg, pn, err.line + 1);
        fprintf(stderr, "%s\n", msg);
        if (g_headless_test) { printf("COMPILE ERROR: %s\n", msg); fflush(stdout); }
        else { scr_print(msg, (int)strlen(msg)); scr_newline(); scr_press_any_key(); }
        free(sp);
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
    free(sp);
    doc_free(&d);
    return exitcode;
}

int main(int argc, char *argv[]) {
    const char *file = NULL;
    int run = 0, runexit = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (str_ieq(a, "/RUN") || str_ieq(a, "-run") || str_ieq(a, "--run")) run = 1;
        else if (str_ieq(a, "/X") || str_ieq(a, "-x") || str_ieq(a, "--exec")) runexit = 1;
        else if (str_ieq(a, "-w") || str_ieq(a, "--window")) continue;
        else if (a[0] != '-' || a[1] == 0) file = a;
    }
    if (getenv("QBLIKE_TEST")) g_headless_test = 1;
    scr_init();
    snd_init();
    if (g_headless_test) {
        const char *k = getenv("QBLIKE_KEYS");
        if (k) feed_test_keys(k);
    }
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

/* files.c - file I/O */
#include "qb.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#endif

enum { FM_INPUT = 1, FM_OUTPUT = 2, FM_RANDOM = 4, FM_APPEND = 8, FM_BINARY = 32 };

typedef struct QFile {
    FILE *fp;
    int mode;
    int reclen;
    int col;            /* current output column (1-based) */
    long lastrec;
    char *recbuf;
    int nfields;
    int *fw;
    Value **fv;
} QFile;

static QFile files[256];

/* normalise DOS-style names; on non-Windows find case-insensitive match */
static void native_name(const char *in, int len, char *out, int outsz, int for_read) {
    int n = len < outsz - 1 ? len : outsz - 1;
    memcpy(out, in, (size_t)n);
    out[n] = 0;
    /* trim trailing spaces */
    while (n > 0 && out[n - 1] == ' ') out[--n] = 0;
#ifndef _WIN32
    for (int i = 0; i < n; i++) if (out[i] == '\\') out[i] = '/';
    /* strip drive letter */
    if (n >= 2 && isalpha((unsigned char)out[0]) && out[1] == ':') {
        memmove(out, out + 2, (size_t)(n - 1)); n -= 2;
        /* C:\GAME\SAVE.DAT: DOS drives don't exist here; if the top directory is not in the
           file system root, take the path as relative to the current directory */
        if (out[0] == '/') {
            char top[512]; int k = 1;
            while (out[k] && out[k] != '/' && k < 500) { top[k - 1] = out[k]; k++; }
            top[k - 1] = 0;
            char probe[520]; snprintf(probe, sizeof probe, "/%s", top);
            struct stat st0;
            if (top[0] && stat(probe, &st0) != 0) { memmove(out, out + 1, (size_t)n); n--; }
        }
    }
    (void)for_read;
    /* DOS names are case-insensitive: resolve each component of the path against the real
       directory entries (DATA\LEVEL1.DAT finds data/Level1.dat). Missing parts are kept as
       written, so new files and directories are created with the given name. */
    struct stat st;
    if (stat(out, &st) == 0) return;
    char res[1024];
    const char *p = out;
    int rl = 0;
    if (*p == '/') { res[rl++] = '/'; while (*p == '/') p++; }
    res[rl] = 0;
    int found_all = 1;
    while (*p) {
        const char *e = strchr(p, '/');
        int cl = e ? (int)(e - p) : (int)strlen(p);
        char comp[512];
        if (cl >= (int)sizeof comp) cl = (int)sizeof comp - 1;
        memcpy(comp, p, (size_t)cl); comp[cl] = 0;
        char use[512];
        snprintf(use, sizeof use, "%s", comp);
        if (found_all && strcmp(comp, ".") && strcmp(comp, "..")) {
            char probe[1100];
            snprintf(probe, sizeof probe, "%s%s", rl ? res : "", comp);
            if (stat(probe, &st) != 0) {
                DIR *d = opendir(rl ? res : ".");
                int hit = 0;
                if (d) {
                    struct dirent *de;
                    while ((de = readdir(d))) if (str_ieq(de->d_name, comp)) { snprintf(use, sizeof use, "%s", de->d_name); hit = 1; break; }
                    closedir(d);
                }
                if (!hit) found_all = 0;
            }
        }
        int ul = (int)strlen(use);
        if (rl + ul + 2 >= (int)sizeof res) break;
        memcpy(res + rl, use, (size_t)ul); rl += ul;
        p += cl;
        if (*p == '/') { res[rl++] = '/'; while (*p == '/') p++; }
        res[rl] = 0;
    }
    snprintf(out, (size_t)outsz, "%s", res);
#else
    (void)for_read;
#endif
}

static QFile *getf(int fn) {
    if (fn < 1 || fn > 255) rt_error(52);
    if (!files[fn].fp) rt_error(52);
    return &files[fn];
}

int file_isopen(int fn) { return fn >= 1 && fn <= 255 && files[fn].fp != NULL; }
int file_mode(int fn) { return file_isopen(fn) ? files[fn].mode : 0; }

void file_open(const char *name, int namelen, int mode, int fn, int reclen, int access) {
    (void)access;
    if (fn < 1 || fn > 255) rt_error(52);
    if (files[fn].fp) rt_error(55);
    char path[1024];
    native_name(name, namelen, path, sizeof path, mode == 1 || mode == 4 || mode == 5 || mode == 3);
    if (!path[0]) rt_error(64);
    FILE *fp = NULL;
    int fm = 0;
    switch (mode) {
        case 1: fp = fopen(path, "rb"); fm = FM_INPUT; if (!fp) rt_error(53); break;
        case 2: fp = fopen(path, "wb"); fm = FM_OUTPUT; if (!fp) rt_error(75); break;
        case 3: fp = fopen(path, "ab"); fm = FM_APPEND; if (!fp) rt_error(75); break;
        case 4: case 5:
            fp = fopen(path, "r+b");
            if (!fp) fp = fopen(path, "w+b");
            if (!fp) rt_error(75);
            fm = mode == 4 ? FM_RANDOM : FM_BINARY;
            break;
        default: rt_error(54);
    }
    QFile *f = &files[fn];
    memset(f, 0, sizeof *f);
    f->fp = fp;
    f->mode = fm;
    f->reclen = reclen > 0 ? reclen : 128;
    if (fm == FM_RANDOM && (reclen < 1 || reclen > 32767)) { fclose(fp); f->fp = NULL; rt_error(59); }
    f->col = 1;
    f->lastrec = 0;
    if (fm == FM_APPEND) fseek(fp, 0, SEEK_END);
}

void file_close(int fn) {
    if (fn < 1 || fn > 255) rt_error(52);
    QFile *f = &files[fn];
    if (!f->fp) return;
    fclose(f->fp);
    free(f->recbuf);
    free(f->fw);
    free(f->fv);
    memset(f, 0, sizeof *f);
}

void file_closeall(void) { for (int i = 1; i < 256; i++) if (files[i].fp) file_close(i); }

void file_print(int fn, const char *s, int len) {
    QFile *f = getf(fn);
    if (f->mode == FM_INPUT) rt_error(54);
    fwrite(s, 1, (size_t)len, f->fp);
    for (int i = 0; i < len; i++) {
        if (s[i] == '\n') f->col = 1;
        else if (s[i] != '\r') f->col++;
    }
}

int file_column(int fn) { return getf(fn)->col; }

int file_eof(int fn) {
    QFile *f = getf(fn);
    if (f->mode == FM_OUTPUT || f->mode == FM_APPEND) rt_error(54);
    int c = fgetc(f->fp);
    if (c == EOF) return 1;
    ungetc(c, f->fp);
    if (f->mode == FM_INPUT && c == 26) return 1;
    return 0;
}

double file_lof(int fn) {
    QFile *f = getf(fn);
    /* ftell before anything else: on Windows, fflush on a stream that was being read drops the read
       buffer without moving the file position back, and the position would be lost */
    long pos = ftell(f->fp);
    fseek(f->fp, 0, SEEK_END);
    long size = ftell(f->fp);
    fseek(f->fp, pos, SEEK_SET);
    return (double)size;
}

double file_loc(int fn) {
    QFile *f = getf(fn);
    long pos = ftell(f->fp);
    if (f->mode == FM_RANDOM) return (double)f->lastrec;
    if (f->mode == FM_BINARY) return (double)pos;
    return (double)((pos + 127) / 128);
}

double file_seekfn(int fn) {
    QFile *f = getf(fn);
    long pos = ftell(f->fp);
    if (f->mode == FM_RANDOM) return (double)(pos / f->reclen + 1);
    return (double)(pos + 1);
}

void file_seek(int fn, double p) {
    QFile *f = getf(fn);
    if (p < 1) rt_error(63);
    long pos = f->mode == FM_RANDOM ? (long)(p - 1) * f->reclen : (long)p - 1;
    fflush(f->fp);
    fseek(f->fp, pos, SEEK_SET);
}

int file_freefile(void) {
    for (int i = 1; i < 256; i++) if (!files[i].fp) return i;
    rt_error(67);
    return 0;
}

int file_readchar(int fn) { return fgetc(getf(fn)->fp); }

/* INPUT # field */
int file_input_field(int fn, char **out, int *len, int isstr) {
    QFile *f = getf(fn);
    FILE *fp = f->fp;
    int c;
    /* skip leading whitespace (and line breaks) */
    do { c = fgetc(fp); } while (c == ' ' || c == '\t' || c == '\r' || c == '\n');
    if (c == EOF || c == 26) { *out = qb_strdup(""); *len = 0; return 0; }
    int cap = 64, n = 0;
    char *b = (char *)qb_alloc((size_t)cap);
    if (c == '"' && isstr) {
        while ((c = fgetc(fp)) != EOF && c != '"' && c != '\n') {
            if (n + 1 >= cap) { cap *= 2; b = (char *)qb_realloc(b, (size_t)cap); }
            b[n++] = (char)c;
        }
        /* skip to delimiter */
        while (c != EOF && c != ',' && c != '\n') { c = fgetc(fp); if (c == '\r') { int d = fgetc(fp); if (d != '\n' && d != EOF) ungetc(d, fp); break; } }
    } else {
        while (c != EOF) {
            if (c == ',' ) break;
            if (c == '\r') { int d = fgetc(fp); if (d != '\n' && d != EOF) ungetc(d, fp); break; }
            if (c == '\n') break;
            if (!isstr && (c == ' ' || c == '\t')) {
                /* numbers end at whitespace; consume up to delimiter */
                int d;
                do { d = fgetc(fp); } while (d == ' ' || d == '\t');
                if (d == '\r') { int e = fgetc(fp); if (e != '\n' && e != EOF) ungetc(e, fp); }
                else if (d != ',' && d != '\n' && d != EOF) ungetc(d, fp);
                break;
            }
            if (n + 1 >= cap) { cap *= 2; b = (char *)qb_realloc(b, (size_t)cap); }
            b[n++] = (char)c;
            c = fgetc(fp);
        }
        while (n > 0 && (b[n - 1] == ' ' || b[n - 1] == '\t')) n--;
    }
    b[n] = 0;
    *out = b;
    *len = n;
    return 1;
}

int file_line_input(int fn, char **out, int *len) {
    QFile *f = getf(fn);
    FILE *fp = f->fp;
    int c = fgetc(fp);
    if (c == EOF || (c == 26 && f->mode == FM_INPUT)) { *out = qb_strdup(""); *len = 0; return 0; }
    int cap = 128, n = 0;
    char *b = (char *)qb_alloc((size_t)cap);
    while (c != EOF) {
        if (c == '\r') { int d = fgetc(fp); if (d != '\n' && d != EOF) ungetc(d, fp); break; }
        if (c == '\n') break;
        if (n + 1 >= cap) { cap *= 2; b = (char *)qb_realloc(b, (size_t)cap); }
        b[n++] = (char)c;
        c = fgetc(fp);
    }
    b[n] = 0;
    *out = b;
    *len = n;
    return 1;
}

int file_input_chars(int fn, int n, char **out, int *len) {
    QFile *f = getf(fn);
    char *b = (char *)qb_alloc((size_t)n + 1);
    int got = (int)fread(b, 1, (size_t)n, f->fp);
    *out = b;
    *len = got;
    return got == n;
}

/* binary serialisation of a value */
static int val_bytes(Value *v, unsigned char *buf, int cap, int random) {
    int n = 0;
    switch (v->t) {
        case T_INT: { int16_t x = (int16_t)v->n; if (cap >= 2) memcpy(buf, &x, 2); return 2; }
        case T_LNG: { int32_t x = (int32_t)v->n; if (cap >= 4) memcpy(buf, &x, 4); return 4; }
        case T_SNG: { float x = (float)v->n; if (cap >= 4) memcpy(buf, &x, 4); return 4; }
        case T_DBL: { double x = v->n; if (cap >= 8) memcpy(buf, &x, 8); return 8; }
        case T_STR:
            if (v->fixlen || !random) {
                if (v->slen <= cap) memcpy(buf, v->s, (size_t)v->slen);
                return v->slen;
            } else {
                int16_t l = (int16_t)v->slen;
                if (cap >= 2 + v->slen) { memcpy(buf, &l, 2); memcpy(buf + 2, v->s, (size_t)v->slen); }
                return 2 + v->slen;
            }
        case T_UDT: {
            Udt *u = &g_prog->udts[v->r->udt];
            for (int i = 0; i < u->nfields; i++) n += val_bytes(&v->r->f[i], buf + n, cap - n, 0);
            return n;
        }
    }
    return 0;
}

static int val_from_bytes(Value *v, const unsigned char *buf, int avail, int random) {
    int n = 0;
    switch (v->t) {
        case T_INT: { int16_t x = 0; if (avail >= 2) memcpy(&x, buf, 2); v->n = x; return 2; }
        case T_LNG: { int32_t x = 0; if (avail >= 4) memcpy(&x, buf, 4); v->n = x; return 4; }
        case T_SNG: { float x = 0; if (avail >= 4) memcpy(&x, buf, 4); v->n = x; return 4; }
        case T_DBL: { double x = 0; if (avail >= 8) memcpy(&x, buf, 8); v->n = x; return 8; }
        case T_STR:
            if (v->fixlen || !random) {
                int l = v->slen;
                char *tmp = (char *)qb_alloc((size_t)l + 1);
                int k = avail < l ? (avail > 0 ? avail : 0) : l;
                memcpy(tmp, buf, (size_t)k);
                if (v->fixlen) val_setstr(v, tmp, l);
                else { memcpy(v->s, tmp, (size_t)l); }
                free(tmp);
                return l;
            } else {
                int16_t l = 0;
                if (avail >= 2) memcpy(&l, buf, 2);
                if (l < 0) l = 0;
                if (l > avail - 2) l = (int16_t)(avail - 2 > 0 ? avail - 2 : 0);
                val_setstr(v, (const char *)buf + 2, l);
                return 2 + l;
            }
        case T_UDT: {
            Udt *u = &g_prog->udts[v->r->udt];
            for (int i = 0; i < u->nfields; i++) n += val_from_bytes(&v->r->f[i], buf + n, avail - n, 0);
            return n;
        }
    }
    return 0;
}

void file_get(int fn, double rec, int hasrec, Value *v) {
    QFile *f = getf(fn);
    if (f->mode != FM_RANDOM && f->mode != FM_BINARY) rt_error(54);
    if (hasrec) {
        if (rec < 1) rt_error(63);
        long pos = f->mode == FM_RANDOM ? (long)(rec - 1) * f->reclen : (long)rec - 1;
        fflush(f->fp);
        fseek(f->fp, pos, SEEK_SET);
    }
    if (f->mode == FM_RANDOM) {
        unsigned char *buf = (unsigned char *)qb_alloc((size_t)f->reclen + 1);
        long pos = ftell(f->fp);
        int got = (int)fread(buf, 1, (size_t)f->reclen, f->fp);
        (void)got;
        f->lastrec = pos / f->reclen + 1;
        if (v) {
            unsigned char tmp[16];
            int need = val_bytes(v, tmp, 0, 1);
            if (v->t != T_STR || v->fixlen) { if (need > f->reclen) { free(buf); rt_error(59); } }
            val_from_bytes(v, buf, f->reclen, 1);
        } else if (f->nfields) {
            int off = 0;
            for (int i = 0; i < f->nfields; i++) { val_setstr(f->fv[i], (char *)buf + off, f->fw[i]); off += f->fw[i]; }
        }
        free(buf);
        return;
    }
    if (!v) rt_error(5);
    unsigned char tmp[8];
    int need = val_bytes(v, tmp, 0, 0);
    unsigned char *buf = (unsigned char *)qb_alloc((size_t)need + 1);
    int got = (int)fread(buf, 1, (size_t)need, f->fp);
    val_from_bytes(v, buf, got, 0);
    free(buf);
}

void file_put(int fn, double rec, int hasrec, Value *v) {
    QFile *f = getf(fn);
    if (f->mode != FM_RANDOM && f->mode != FM_BINARY) rt_error(54);
    if (hasrec) {
        if (rec < 1) rt_error(63);
        long pos = f->mode == FM_RANDOM ? (long)(rec - 1) * f->reclen : (long)rec - 1;
        fflush(f->fp);
        fseek(f->fp, pos, SEEK_SET);
    } else fseek(f->fp, 0, SEEK_CUR);   /* C requires a seek between reading and writing */
    if (f->mode == FM_RANDOM) {
        unsigned char *buf = (unsigned char *)qb_alloc((size_t)f->reclen + 1);
        if (v) {
            int need = val_bytes(v, NULL, 0, 1);
            if (need > f->reclen) { free(buf); rt_error(59); }
            val_bytes(v, buf, f->reclen, 1);
        } else if (f->nfields) {
            int off = 0;
            for (int i = 0; i < f->nfields; i++) {
                Value *fv = f->fv[i];
                int l = fv->slen < f->fw[i] ? fv->slen : f->fw[i];
                memset(buf + off, ' ', (size_t)f->fw[i]);
                memcpy(buf + off, fv->s, (size_t)l);
                off += f->fw[i];
            }
        }
        long pos = ftell(f->fp);
        f->lastrec = pos / f->reclen + 1;
        fwrite(buf, 1, (size_t)f->reclen, f->fp);
        fflush(f->fp);
        free(buf);
        return;
    }
    if (!v) rt_error(5);
    int need = val_bytes(v, NULL, 0, 0);
    unsigned char *buf = (unsigned char *)qb_alloc((size_t)need + 1);
    val_bytes(v, buf, need, 0);
    fwrite(buf, 1, (size_t)need, f->fp);
    fflush(f->fp);
    free(buf);
}

void file_field(int fn, int n, int *widths, Value **vars) {
    QFile *f = getf(fn);
    if (f->mode != FM_RANDOM) rt_error(54);
    int total = 0;
    for (int i = 0; i < n; i++) total += widths[i];
    if (total > f->reclen) rt_error(50);
    free(f->fw); free(f->fv);
    f->nfields = n;
    f->fw = (int *)qb_alloc(sizeof(int) * (size_t)(n + 1));
    f->fv = (Value **)qb_alloc(sizeof(Value *) * (size_t)(n + 1));
    for (int i = 0; i < n; i++) {
        f->fw[i] = widths[i];
        f->fv[i] = vars[i];
        char *sp = (char *)qb_alloc((size_t)widths[i] + 1);
        memset(sp, ' ', (size_t)widths[i]);
        val_setstr(vars[i], sp, widths[i]);
        free(sp);
    }
}

static int wild_match(const char *pat, const char *s) {
    if (!*pat) return !*s;
    if (*pat == '*') {
        for (;;) {
            if (wild_match(pat + 1, s)) return 1;
            if (!*s) return 0;
            s++;
        }
    }
    if (!*s) return 0;
    if (*pat == '?' || toupper((unsigned char)*pat) == toupper((unsigned char)*s)) return wild_match(pat + 1, s + 1);
    return 0;
}

/* DIR$ (QuickBASIC PDS 7.1): DIR$(spec$) returns the first file that matches, DIR$ the next one,
   "" when there are no more. QBLike: DIR$(spec$, attr%) with attr% AND &H10 also returns
   directories (. and .. excluded). */
static char **dir_list = NULL;
static int dir_n = 0, dir_pos = 0;

static void dir_add(const char *nm) {
    dir_list = (char **)qb_realloc(dir_list, sizeof(char *) * (size_t)(dir_n + 1));
    dir_list[dir_n++] = qb_strdup(nm);
}

void file_dir(const char *spec, int len, int has_spec, int attr, char **out, int *outlen) {
    if (has_spec) {
        for (int i = 0; i < dir_n; i++) free(dir_list[i]);
        free(dir_list); dir_list = NULL; dir_n = 0; dir_pos = 0;
        char pat[512];
        int n = len < 500 ? len : 500;
        memcpy(pat, spec, (size_t)n); pat[n] = 0;
        while (n > 0 && pat[n - 1] == ' ') pat[--n] = 0;
        if (n == 0) strcpy(pat, "*.*");
        char dir[600] = ".", mask[256];
        char *slash = strrchr(pat, '/'), *bs = strrchr(pat, '\\');
        if (!slash || (bs && bs > slash)) slash = bs;
        if (!slash && pat[0] && pat[1] == ':') slash = pat + 1;   /* C:FILE.TXT */
        if (slash) {
            int dl = (int)(slash - pat);
            if (dl == 0) strcpy(dir, "/");
            else native_name(pat, (*slash == ':') ? dl + 1 : dl, dir, sizeof dir, 1);
            snprintf(mask, sizeof mask, "%s", slash + 1);
        } else snprintf(mask, sizeof mask, "%s", pat);
        if (!mask[0] || strcmp(mask, "*.*") == 0) strcpy(mask, "*");
        int want_dirs = (attr & 0x10) != 0;
#ifdef _WIN32
        char q[700];
        snprintf(q, sizeof q, "%s\\*", dir);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(q, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                const char *nm = fd.cFileName;
                if (!strcmp(nm, ".") || !strcmp(nm, "..")) continue;
                int isd = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                if (isd && !want_dirs) continue;
                if (wild_match(mask, nm)) dir_add(nm);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
#else
        DIR *d = opendir(dir);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d))) {
                if (de->d_name[0] == '.') continue;
                char full[1100];
                snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
                struct stat st;
                int isd = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
                if (isd && !want_dirs) continue;
                if (wild_match(mask, de->d_name)) dir_add(de->d_name);
            }
            closedir(d);
        }
#endif
    }
    if (dir_pos < dir_n) { *out = qb_strdup(dir_list[dir_pos]); *outlen = (int)strlen(*out); dir_pos++; }
    else { *out = qb_strdup(""); *outlen = 0; }
}

/* CURDIR$: the current directory */
void file_curdir(char **out, int *outlen) {
    char cwd[1024] = "";
#ifdef _WIN32
    GetCurrentDirectoryA(sizeof cwd, cwd);
#else
    if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, "/");
#endif
    *out = qb_strdup(cwd); *outlen = (int)strlen(cwd);
}

/* KILL filespec$: deletes files; wildcards * and ? allowed, as in DOS */
void file_kill(const char *spec, int len) {
    char pat[512];
    int n = len < 500 ? len : 500;
    memcpy(pat, spec, (size_t)n);
    pat[n] = 0;
    if (!strpbrk(pat, "*?")) {
        char path[1024];
        native_name(pat, n, path, sizeof path, 1);
        if (remove(path) != 0) rt_error(53);
        return;
    }
    char dir[600] = ".", mask[256];
    char *slash = strrchr(pat, '/');
    char *bs = strrchr(pat, '\\');
    if (!slash || (bs && bs > slash)) slash = bs;
    if (slash) {
        int dl = (int)(slash - pat);
        if (dl == 0) strcpy(dir, "/");
        else native_name(pat, dl, dir, sizeof dir, 1);
        snprintf(mask, sizeof mask, "%s", slash + 1);
    } else snprintf(mask, sizeof mask, "%s", pat);
    if (strcmp(mask, "*.*") == 0) strcpy(mask, "*");
    int count = 0;
#ifdef _WIN32
    char q[700];
    snprintf(q, sizeof q, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(q, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (!wild_match(mask, fd.cFileName)) continue;
            char full[1100];
            snprintf(full, sizeof full, "%s\\%s", dir, fd.cFileName);
            if (remove(full) == 0) count++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (!wild_match(mask, de->d_name)) continue;
            char full[1100];
            snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
            struct stat st;
            if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;
            if (remove(full) == 0) count++;
        }
        closedir(d);
    }
#endif
    if (count == 0) rt_error(53);
}

void file_files(const char *spec, int len) {
    char pat[512];
    int n = len < 500 ? len : 500;
    memcpy(pat, spec, (size_t)n);
    pat[n] = 0;
    if (strcmp(pat, "*.*") == 0) strcpy(pat, "*");
    char dir[512] = ".";
    char *slash = strrchr(pat, '/');
    if (!slash) slash = strrchr(pat, '\\');
    char mask[256];
    if (slash) {
        int dl = (int)(slash - pat);
        if (dl == 0) strcpy(dir, "/");
        else native_name(pat, dl, dir, sizeof dir, 1);
        snprintf(mask, sizeof mask, "%s", slash + 1);
    } else snprintf(mask, sizeof mask, "%s", pat);
    if (!mask[0] || strcmp(mask, "*.*") == 0) strcpy(mask, "*");
    char cwd[512] = "";
#ifdef _WIN32
    GetCurrentDirectoryA(sizeof cwd, cwd);
#else
    if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, ".");
#endif
    scr_print(cwd, (int)strlen(cwd));
    scr_newline();
    int count = 0;
#ifdef _WIN32
    char q[600];
    snprintf(q, sizeof q, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(q, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const char *nm = fd.cFileName;
            if (!wild_match(mask, nm)) continue;
            char item[64];
            int isdir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            snprintf(item, sizeof item, "%-12.12s%s", nm, isdir ? "<DIR> " : "      ");
            if (scr_pos() + 18 > scr_cols() + 1) scr_newline();
            scr_print(item, 18);
            count++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (de->d_name[0] == '.' && strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) continue;
            if (!wild_match(mask, de->d_name)) continue;
            char full[1100];
            snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
            struct stat st;
            int isdir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
            char item[64];
            snprintf(item, sizeof item, "%-12.12s%s", de->d_name, isdir ? "<DIR> " : "      ");
            if (scr_pos() + 18 > scr_cols() + 1) scr_newline();
            scr_print(item, 18);
            count++;
        }
        closedir(d);
    }
#endif
    if (count == 0) { rt_error(53); }
    if (scr_pos() > 1) scr_newline();
    const char *fr = " Bytes free";
    char buf[64];
    snprintf(buf, sizeof buf, " %ld", 1048576L * 64);
    scr_print(buf, (int)strlen(buf));
    scr_print(fr, (int)strlen(fr));
    scr_newline();
}

int wild_match_pub(const char *pat, const char *s) { return wild_match(pat, s); }

void file_native_name(const char *in, int len, char *out, int outsz, int for_read) { native_name(in, len, out, outsz, for_read); }

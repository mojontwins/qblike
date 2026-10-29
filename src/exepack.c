/* exepack.c - stand-alone executables: a copy of the QBLike runtime (qbrun) with the program
   appended at the end.

   Layout of a generated executable:
       [runtime executable, unchanged]
       [payload: program name, then the source, obfuscated]
       [trailer, 32 bytes]
   trailer: "QBLKRUN1", payload size, flags, key, checksum (little endian, 32 bits each),
            8 reserved bytes.
   The runtime finds its own file, reads the trailer and, if there is one, runs the program.

   The source is only obfuscated (not encrypted): it is not readable with a text editor, which
   is what an EXE of the time offered too. */
#include "qb.h"
#include "exepack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#define TRAILER 32
static const char MAGIC[8] = { 'Q', 'B', 'L', 'K', 'R', 'U', 'N', '1' };

static void put32(unsigned char *p, uint32_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }
static uint32_t get32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static void scramble(unsigned char *d, size_t n, uint32_t key) {
    uint32_t x = key ? key : 0x9E3779B9u;
    for (size_t i = 0; i < n; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;   /* xorshift32 */
        d[i] ^= (unsigned char)(x >> 11);
    }
}

static uint32_t checksum(const unsigned char *d, size_t n) {
    uint32_t h = 2166136261u;                     /* FNV-1a */
    for (size_t i = 0; i < n; i++) { h ^= d[i]; h *= 16777619u; }
    return h;
}

static unsigned char *read_all(const char *path, long *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    unsigned char *d = (unsigned char *)malloc((size_t)n + 1);
    if (!d) { fclose(f); return NULL; }
    if ((long)fread(d, 1, (size_t)n, f) != n) { fclose(f); free(d); return NULL; }
    fclose(f);
    *size = n;
    return d;
}

/* size of the runtime part of an executable (without a payload already appended) */
static long runtime_size(const unsigned char *d, long n) {
    if (n >= TRAILER && !memcmp(d + n - TRAILER, MAGIC, 8)) {
        uint32_t ps = get32(d + n - TRAILER + 8);
        if ((long)ps + TRAILER <= n) return n - TRAILER - (long)ps;
    }
    return n;
}

int exepack_write(const char *runtime_path, const char *out_path, const char *name,
                  const char *src, int srclen, int flags, char *err, int errsz) {
    long rn;
    unsigned char *rt = read_all(runtime_path, &rn);
    if (!rt) { snprintf(err, (size_t)errsz, "Runtime not found: %s", runtime_path); return 0; }
    rn = runtime_size(rt, rn);
    int nl = (int)strlen(name);
    if (nl > 255) nl = 255;
    size_t ps = 1 + (size_t)nl + (size_t)srclen;
    unsigned char *pl = (unsigned char *)malloc(ps);
    if (!pl) { free(rt); snprintf(err, (size_t)errsz, "Out of memory"); return 0; }
    pl[0] = (unsigned char)nl;
    memcpy(pl + 1, name, (size_t)nl);
    memcpy(pl + 1 + nl, src, (size_t)srclen);
    uint32_t sum = checksum(pl, ps);
    uint32_t key = sum ^ 0x51424C4Bu ^ (uint32_t)ps;
    scramble(pl, ps, key);
    unsigned char tr[TRAILER];
    memset(tr, 0, sizeof tr);
    memcpy(tr, MAGIC, 8);
    put32(tr + 8, (uint32_t)ps);
    put32(tr + 12, (uint32_t)flags);
    put32(tr + 16, key);
    put32(tr + 20, sum);
    FILE *f = fopen(out_path, "wb");
    if (!f) { free(rt); free(pl); snprintf(err, (size_t)errsz, "Cannot create %s", out_path); return 0; }
    int ok = fwrite(rt, 1, (size_t)rn, f) == (size_t)rn && fwrite(pl, 1, ps, f) == ps && fwrite(tr, 1, TRAILER, f) == TRAILER;
    ok = (fclose(f) == 0) && ok;
    free(rt); free(pl);
    if (!ok) { remove(out_path); snprintf(err, (size_t)errsz, "Error writing %s", out_path); return 0; }
#ifndef _WIN32
    chmod(out_path, 0755);
#endif
    return 1;
}

int exepack_self_path(char *out, int outsz) {
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)outsz);
    return n > 0 && n < (DWORD)outsz;
#elif defined(__APPLE__)
    uint32_t sz = (uint32_t)outsz;
    return _NSGetExecutablePath(out, &sz) == 0;
#else
    ssize_t n = readlink("/proc/self/exe", out, (size_t)outsz - 1);
    if (n <= 0) return 0;
    out[n] = 0;
    return 1;
#endif
}

int exepack_read_self(char **name, char **src, int *srclen, int *flags) {
    char self[1024];
    if (!exepack_self_path(self, sizeof self)) return 0;
    FILE *f = fopen(self, "rb");
    if (!f) return 0;
    unsigned char tr[TRAILER];
    if (fseek(f, -TRAILER, SEEK_END) != 0 || fread(tr, 1, TRAILER, f) != TRAILER || memcmp(tr, MAGIC, 8)) { fclose(f); return 0; }
    uint32_t ps = get32(tr + 8);
    long end = ftell(f);
    if (ps < 1 || (long)ps > end - TRAILER) { fclose(f); return 0; }
    unsigned char *pl = (unsigned char *)malloc(ps + 1);
    if (!pl) { fclose(f); return 0; }
    if (fseek(f, end - TRAILER - (long)ps, SEEK_SET) != 0 || fread(pl, 1, ps, f) != ps) { fclose(f); free(pl); return 0; }
    fclose(f);
    scramble(pl, ps, get32(tr + 16));
    if (checksum(pl, ps) != get32(tr + 20)) { free(pl); return 0; }
    int nl = pl[0];
    if (1 + nl > (int)ps) { free(pl); return 0; }
    *name = (char *)malloc((size_t)nl + 1);
    memcpy(*name, pl + 1, (size_t)nl);
    (*name)[nl] = 0;
    *srclen = (int)ps - 1 - nl;
    *src = (char *)malloc((size_t)*srclen + 1);
    memcpy(*src, pl + 1 + nl, (size_t)*srclen);
    (*src)[*srclen] = 0;
    *flags = (int)get32(tr + 12);
    free(pl);
    return 1;
}

/* where the runtime for a target lives: next to the running executable, or in ../windows,
   ../linux or ../macos (the layout of the QBLike package) */
int exepack_find_runtime(int target, char *out, int outsz) {
    char self[1024];
    if (!exepack_self_path(self, sizeof self)) return 0;
    char *sl = strrchr(self, '/');
#ifdef _WIN32
    char *bs = strrchr(self, '\\');
    if (!sl || (bs && bs > sl)) sl = bs;
#endif
    if (!sl) return 0;
    *sl = 0;
    const char *file = target == EXE_TARGET_WINDOWS ? "qbrun.exe" : "qbrun";
    const char *sub = target == EXE_TARGET_WINDOWS ? "windows" : target == EXE_TARGET_MACOS ? "macos" : "linux";
    for (int i = 0; i < 3; i++) {
        if (i == 0) snprintf(out, (size_t)outsz, "%s/%s", self, file);
        else if (i == 1) snprintf(out, (size_t)outsz, "%s/../%s/%s", self, sub, file);
        else snprintf(out, (size_t)outsz, "%s/%s/%s", self, sub, file);
        FILE *f = fopen(out, "rb");
        if (f) { fclose(f); return 1; }
    }
    return 0;
}

int exepack_host_target(void) {
#ifdef _WIN32
    return EXE_TARGET_WINDOWS;
#elif defined(__APPLE__)
    return EXE_TARGET_MACOS;
#else
    return EXE_TARGET_LINUX;
#endif
}

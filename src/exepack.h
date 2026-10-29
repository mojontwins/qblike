#ifndef EXEPACK_H
#define EXEPACK_H

/* flags stored in a generated executable */
#define EXE_FULLSCREEN 1

enum { EXE_TARGET_WINDOWS = 1, EXE_TARGET_LINUX = 2, EXE_TARGET_MACOS = 3 };

int exepack_write(const char *runtime_path, const char *out_path, const char *name,
                  const char *src, int srclen, int flags, char *err, int errsz);
int exepack_read_self(char **name, char **src, int *srclen, int *flags);
int exepack_self_path(char *out, int outsz);
int exepack_find_runtime(int target, char *out, int outsz);
int exepack_host_target(void);

#endif

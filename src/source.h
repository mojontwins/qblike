#ifndef SOURCE_H
#define SOURCE_H

#define MAX_PARTS 256

typedef struct Part {
    char name[64];
    int isfunc;
    char **lines;
    int n, cap;
    /* editor view state */
    int top, cy, cx, left;
} Part;

typedef struct Doc {
    Part parts[MAX_PARTS];
    int nparts;
    int utf8;           /* file was UTF-8 encoded: saved back as UTF-8 */
} Doc;

void part_init(Part *p);
void part_free(Part *p);
void part_insert(Part *p, int at, const char *text);
void part_delete(Part *p, int at);
void part_set(Part *p, int at, const char *text);
void doc_init(Doc *d);
void doc_free(Doc *d);
int  doc_load(Doc *d, const char *path);
int  doc_save(Doc *d, const char *path);
void doc_from_lines(Doc *d, char **lines, int n);
int  classify_line(const char *line, char *name, int namesz);
SrcPart *doc_srcparts(Doc *d);

#endif

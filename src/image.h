#ifndef IMAGE_H
#define IMAGE_H
#include <stdint.h>

enum { IMG_OK = 0, IMG_NOTFOUND, IMG_BADFORMAT, IMG_NOTINDEXED };

typedef struct Img { int w, h; uint8_t *pix; } Img;

typedef struct ImgSet {
    int n;              /* frames */
    Img *fr;
    int colors;         /* 2, 16, 256 (BMP: from bit depth; GIF: palette size) */
    int haspal, npal;
    uint8_t pal[256][3];    /* 8-bit RGB */
} ImgSet;

int  img_load_bmp(const char *name, int namelen, ImgSet *s, int palette_only);
int  img_load_gif(const char *name, int namelen, ImgSet *s, int palette_only);
void img_free(ImgSet *s);

#endif

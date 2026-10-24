/* image.c - BMP and GIF loading for LOADBMP / LOADGIF / LOADPAL (QBLike extensions) */
#include "qb.h"
#include "image.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define GIF_EXTR static
#include "doslike/libs/gif_load.h"

void file_native_name(const char *in, int len, char *out, int outsz, int for_read);

void img_free(ImgSet *s) {
    for (int i = 0; i < s->n; i++) free(s->fr[i].pix);
    free(s->fr);
    memset(s, 0, sizeof *s);
}

static unsigned char *read_file(const char *name, int namelen, long *size) {
    char path[1024];
    file_native_name(name, namelen, path, sizeof path, 1);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); *size = 0; return (unsigned char *)qb_alloc(1); }
    unsigned char *d = (unsigned char *)qb_alloc((size_t)n);
    if ((long)fread(d, 1, (size_t)n, f) != n) { fclose(f); free(d); return NULL; }
    fclose(f);
    *size = n;
    return d;
}

static int add_frame(ImgSet *s, int w, int h) {
    s->fr = (Img *)qb_realloc(s->fr, sizeof(Img) * (size_t)(s->n + 1));
    Img *im = &s->fr[s->n];
    im->w = w; im->h = h;
    im->pix = (uint8_t *)qb_alloc((size_t)w * (size_t)h + 1);
    return s->n++;
}

static unsigned rd16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static unsigned long rd32(const unsigned char *p) { return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24); }

/* ------------------------------------------------------------------ */
/* BMP: indexed 1, 4 or 8 bits per pixel; uncompressed, RLE8 or RLE4   */
/* ------------------------------------------------------------------ */
int img_load_bmp(const char *name, int namelen, ImgSet *s, int palette_only) {
    memset(s, 0, sizeof *s);
    long size;
    unsigned char *d = read_file(name, namelen, &size);
    if (!d) return IMG_NOTFOUND;
    int err = IMG_BADFORMAT;
    if (size < 26 || d[0] != 'B' || d[1] != 'M') goto done;
    unsigned long off = rd32(d + 10);
    unsigned long hsz = rd32(d + 14);
    long w, h;
    int bpp, comp = 0, ncolors = 0, entry = 4;
    if (hsz == 12) {                     /* OS/2 BITMAPCOREHEADER */
        w = (long)rd16(d + 18); h = (long)(int16_t)rd16(d + 20);
        bpp = (int)rd16(d + 24);
        entry = 3;
    } else if (hsz >= 40 && size >= 54) {
        w = (long)(int32_t)rd32(d + 18); h = (long)(int32_t)rd32(d + 22);
        bpp = (int)rd16(d + 28);
        comp = (int)rd32(d + 30);
        ncolors = (int)rd32(d + 46);
    } else goto done;
    if (bpp != 1 && bpp != 4 && bpp != 8) { err = IMG_NOTINDEXED; goto done; }
    if (comp != 0 && !(comp == 1 && bpp == 8) && !(comp == 2 && bpp == 4)) goto done;
    int topdown = h < 0;
    if (h < 0) h = -h;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) goto done;
    s->colors = 1 << bpp;
    /* palette (may be absent) */
    long palstart = 14 + (long)hsz;
    long avail = ((long)off - palstart) / entry;
    int maxpal = s->colors;
    if (ncolors > 0 && ncolors < maxpal) maxpal = ncolors;
    if (avail < 0) avail = 0;
    if (avail > maxpal) avail = maxpal;
    if (palstart + avail * entry > size) avail = 0;
    s->npal = (int)avail;
    s->haspal = avail > 0;
    for (int i = 0; i < s->npal; i++) {
        const unsigned char *p = d + palstart + i * entry;
        s->pal[i][0] = p[2]; s->pal[i][1] = p[1]; s->pal[i][2] = p[0];
    }
    if (palette_only) { err = IMG_OK; goto done; }
    if ((long)off >= size) goto done;
    int fi = add_frame(s, (int)w, (int)h);
    uint8_t *pix = s->fr[fi].pix;
    memset(pix, 0, (size_t)(w * h));
    const unsigned char *src = d + off, *end = d + size;
    if (comp == 0) {
        long stride = ((w * bpp + 31) / 32) * 4;
        for (long row = 0; row < h; row++) {
            long y = topdown ? row : h - 1 - row;
            const unsigned char *r = src + row * stride;
            if (r + stride > end) break;
            for (long x = 0; x < w; x++) {
                int v;
                if (bpp == 8) v = r[x];
                else if (bpp == 4) v = (r[x >> 1] >> ((x & 1) ? 0 : 4)) & 15;
                else v = (r[x >> 3] >> (7 - (x & 7))) & 1;
                pix[y * w + x] = (uint8_t)v;
            }
        }
    } else {
        /* RLE8 / RLE4 (bottom-up) */
        long x = 0, row = 0;
        const unsigned char *p = src;
        while (p + 1 < end && row < h) {
            int n = p[0], c = p[1];
            p += 2;
            if (n > 0) {
                for (int k = 0; k < n && x < w; k++, x++) {
                    int v = bpp == 8 ? c : ((k & 1) ? (c & 15) : (c >> 4));
                    pix[(h - 1 - row) * w + x] = (uint8_t)v;
                }
            } else if (c == 0) { x = 0; row++; }
            else if (c == 1) break;
            else if (c == 2) { if (p + 1 >= end) break; x += p[0]; row += p[1]; p += 2; }
            else {
                int cnt = c;
                for (int k = 0; k < cnt; k++) {
                    int v;
                    if (bpp == 8) { if (p + k >= end) break; v = p[k]; }
                    else { if (p + k / 2 >= end) break; v = (k & 1) ? (p[k / 2] & 15) : (p[k / 2] >> 4); }
                    if (x < w && row < h) pix[(h - 1 - row) * w + x] = (uint8_t)v;
                    x++;
                }
                int used = bpp == 8 ? cnt : (cnt + 1) / 2;
                p += (used + 1) & ~1;   /* word aligned */
            }
        }
    }
    err = IMG_OK;
done:
    free(d);
    if (err != IMG_OK) img_free(s);
    return err;
}

/* ------------------------------------------------------------------ */
/* GIF: every frame is composed on the logical screen, so all frames   */
/* have the same size (the GIF's logical screen size)                  */
/* ------------------------------------------------------------------ */
typedef struct GifCtx {
    ImgSet *s;
    uint8_t *canvas, *saved;
    long w, h;
    int prev_mode;
    long pfx, pfy, pfw, pfh;
    long bkgd;
    int palette_only;
} GifCtx;

static void gif_frame(void *data, struct GIF_WHDR *wh) {
    GifCtx *g = (GifCtx *)data;
    ImgSet *s = g->s;
    if (wh->clrs > s->colors) s->colors = (int)wh->clrs;
    if (wh->ifrm == 0) {
        s->npal = (int)(wh->clrs > 256 ? 256 : wh->clrs);
        s->haspal = s->npal > 0;
        for (int i = 0; i < s->npal; i++) { s->pal[i][0] = wh->cpal[i].R; s->pal[i][1] = wh->cpal[i].G; s->pal[i][2] = wh->cpal[i].B; }
    }
    if (g->palette_only) return;
    if (!g->canvas) {
        g->w = wh->xdim; g->h = wh->ydim;
        if (g->w <= 0 || g->h <= 0 || g->w > 4096 || g->h > 4096) return;
        g->canvas = (uint8_t *)qb_alloc((size_t)(g->w * g->h));
        g->saved = (uint8_t *)qb_alloc((size_t)(g->w * g->h));
        uint8_t fillc = (uint8_t)(wh->tran >= 0 ? wh->tran : wh->bkgd);
        memset(g->canvas, fillc, (size_t)(g->w * g->h));
        g->bkgd = wh->tran >= 0 ? wh->tran : wh->bkgd;
        g->prev_mode = GIF_NONE;
    }
    /* dispose of the previous frame */
    if (g->prev_mode == GIF_BKGD) {
        for (long y = g->pfy; y < g->pfy + g->pfh && y < g->h; y++)
            for (long x = g->pfx; x < g->pfx + g->pfw && x < g->w; x++) g->canvas[y * g->w + x] = (uint8_t)g->bkgd;
    } else if (g->prev_mode == GIF_PREV) {
        memcpy(g->canvas, g->saved, (size_t)(g->w * g->h));
    }
    if (wh->mode == GIF_PREV) memcpy(g->saved, g->canvas, (size_t)(g->w * g->h));
    /* draw this frame (handles interlacing, skips transparent pixels) */
    long iter = wh->intr ? 0 : 4;
    long ifin = !iter ? 4 : 5;
    long dsrc = -1;
    for (; iter < ifin; ++iter) {
        for (long yoff = 16L >> ((iter > 1) ? iter : 1), y = (8 >> iter) & 7; y < wh->fryd; y += yoff) {
            for (long x = 0; x < wh->frxd; x++) {
                uint8_t v = wh->bptr[++dsrc];
                long cx = wh->frxo + x, cy = wh->fryo + y;
                if (cx < 0 || cy < 0 || cx >= g->w || cy >= g->h) continue;
                if (wh->tran != (long)v) g->canvas[cy * g->w + cx] = v;
            }
        }
    }
    int fi = add_frame(s, (int)g->w, (int)g->h);
    memcpy(s->fr[fi].pix, g->canvas, (size_t)(g->w * g->h));
    g->prev_mode = (int)wh->mode;
    g->pfx = wh->frxo; g->pfy = wh->fryo; g->pfw = wh->frxd; g->pfh = wh->fryd;
}

int img_load_gif(const char *name, int namelen, ImgSet *s, int palette_only) {
    memset(s, 0, sizeof *s);
    long size;
    unsigned char *d = read_file(name, namelen, &size);
    if (!d) return IMG_NOTFOUND;
    if (size < 13 || memcmp(d, "GIF8", 4) != 0) { free(d); return IMG_BADFORMAT; }
    GifCtx g;
    memset(&g, 0, sizeof g);
    g.s = s;
    g.palette_only = palette_only;
    long r = GIF_Load(d, size, gif_frame, NULL, &g, 0);
    free(d);
    free(g.canvas);
    free(g.saved);
    if (palette_only) {
        if (!s->haspal && s->colors == 0) { img_free(s); return IMG_BADFORMAT; }
        return IMG_OK;
    }
    if (r <= 0 && s->n == 0) { img_free(s); return IMG_BADFORMAT; }
    if (s->n == 0) { img_free(s); return IMG_BADFORMAT; }
    return IMG_OK;
}

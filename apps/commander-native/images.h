/* images.h: picture decoders for Commander's picture view, written for r3i6nos (MIT), no libraries.
 *
 * PNG (every colour type and depth, interlaced too), JPEG (baseline, greyscale or YCbCr, any sampling; progressive and arithmetic-coded
 * JPEGs are named, not drawn), GIF (the first frame), BMP and the DIBs in ICO/CUR (1-32 bits, RLE4/RLE8, bit fields), ICO (its largest
 * picture, BMP or PNG), TGA (colour-mapped, true colour, grey, RLE), QOI, PCX (1, 4, 8 and 24 bits), and the PNM family (P1-P6).
 *
 * Each decoder takes the whole file in memory and returns ARGB pixels; anything wrong returns 0 with a reason, never reads past the
 * buffer. Pictures are capped at IMG_MAX_PIXELS so a huge one cannot take the unit's 1 GB. */
#ifndef COMMANDER_IMAGES_H
#define COMMANDER_IMAGES_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "inflate.h"

#define IMG_MAX_PIXELS (24u * 1024u * 1024u)

typedef struct {
    int w, h;
    uint32_t *px; /* w*h ARGB, row by row from the top */
} FImage;

static void img_free(FImage *im) {
    free(im->px);
    memset(im, 0, sizeof *im);
}

static int img_alloc(FImage *im, int w, int h, char *err, size_t errsz) {
    if (w <= 0 || h <= 0 || (uint64_t)w * (uint64_t)h > IMG_MAX_PIXELS) {
        snprintf(err, errsz, "%dx%d is too big to draw here", w, h);
        return -1;
    }
    im->px = (uint32_t *)calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    if (!im->px) { snprintf(err, errsz, "out of memory"); return -1; }
    im->w = w;
    im->h = h;
    return 0;
}

static uint32_t img_argb(int a, int r, int g, int b) { return (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b; }
static unsigned img_le16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t img_le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t img_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static unsigned img_be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }

/* ---- PNG ---------------------------------------------------------------------------------------------------------------------- */

static int img_paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

/* Undo the filters of one pass in place: h rows of 1+stride bytes, bpp bytes a pixel (at least 1). */
static int img_png_unfilter(uint8_t *d, size_t stride, int h, int bpp) {
    uint8_t *prev = NULL;
    for (int y = 0; y < h; y++) {
        uint8_t *row = d + (size_t)y * (stride + 1);
        int f = row[0];
        uint8_t *x = row + 1;
        for (size_t i = 0; i < stride; i++) {
            int a = i >= (size_t)bpp ? x[i - bpp] : 0, b = prev ? prev[i] : 0, c = (prev && i >= (size_t)bpp) ? prev[i - bpp] : 0;
            switch (f) {
            case 0: break;
            case 1: x[i] = (uint8_t)(x[i] + a); break;
            case 2: x[i] = (uint8_t)(x[i] + b); break;
            case 3: x[i] = (uint8_t)(x[i] + ((a + b) >> 1)); break;
            case 4: x[i] = (uint8_t)(x[i] + img_paeth(a, b, c)); break;
            default: return -1;
            }
        }
        prev = x;
    }
    return 0;
}

static int img_png(const uint8_t *buf, size_t n, FImage *im, char *err, size_t errsz) {
    if (n < 33 || memcmp(buf, "\x89PNG\r\n\x1a\n", 8) != 0) { snprintf(err, errsz, "not a PNG"); return -1; }
    int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0, npal = 0, have_trns = 0;
    uint8_t pal[256][4];
    unsigned trns_grey = 0x10000, trns_rgb[3] = {0x10000, 0x10000, 0x10000};
    for (int i = 0; i < 256; i++) { pal[i][0] = pal[i][1] = pal[i][2] = 0; pal[i][3] = 255; }
    size_t idat_len = 0, p = 8;
    uint8_t *idat = NULL;
    while (p + 12 <= n) {
        uint32_t len = img_be32(buf + p);
        const uint8_t *type = buf + p + 4, *d = buf + p + 8;
        if (len > n - p - 12) break;
        if (!memcmp(type, "IHDR", 4) && len >= 13) {
            w = (int)img_be32(d); h = (int)img_be32(d + 4);
            depth = d[8]; ctype = d[9]; interlace = d[12];
        } else if (!memcmp(type, "PLTE", 4)) {
            npal = (int)(len / 3 > 256 ? 256 : len / 3);
            for (int i = 0; i < npal; i++) { pal[i][0] = d[i * 3]; pal[i][1] = d[i * 3 + 1]; pal[i][2] = d[i * 3 + 2]; }
        } else if (!memcmp(type, "tRNS", 4)) {
            have_trns = 1;
            if (ctype == 3) for (uint32_t i = 0; i < len && i < 256; i++) pal[i][3] = d[i];
            else if (ctype == 0 && len >= 2) trns_grey = img_be16(d);
            else if (ctype == 2 && len >= 6) for (int k = 0; k < 3; k++) trns_rgb[k] = img_be16(d + 2 * k);
        } else if (!memcmp(type, "IDAT", 4)) {
            uint8_t *grown = (uint8_t *)realloc(idat, idat_len + len + 1);
            if (!grown) { free(idat); snprintf(err, errsz, "out of memory"); return -1; }
            idat = grown;
            memcpy(idat + idat_len, d, len);
            idat_len += len;
        } else if (!memcmp(type, "IEND", 4)) {
            break;
        }
        p += 12 + len;
    }
    (void)have_trns;
    int chans = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!w || !h || !chans || !idat || !(depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16) ||
        (ctype == 3 && depth > 8) || ((ctype == 2 || ctype == 4 || ctype == 6) && depth < 8)) {
        free(idat);
        snprintf(err, errsz, "a PNG this viewer cannot read");
        return -1;
    }
    if (img_alloc(im, w, h, err, errsz) != 0) { free(idat); return -1; }
    int bits = chans * depth, bpp = bits >= 8 ? bits / 8 : 1;
    static const int sx[7] = {0, 4, 0, 2, 0, 1, 0}, sy[7] = {0, 0, 4, 0, 2, 0, 1}, dx[7] = {8, 8, 4, 4, 2, 2, 1}, dy[7] = {8, 8, 8, 4, 4, 2, 2};
    int passes = interlace ? 7 : 1;
    size_t total = 0;
    for (int ps = 0; ps < passes; ps++) {
        int pw = interlace ? (w - sx[ps] + dx[ps] - 1) / dx[ps] : w, ph = interlace ? (h - sy[ps] + dy[ps] - 1) / dy[ps] : h;
        if (pw > 0 && ph > 0) total += (size_t)ph * (1 + ((size_t)pw * bits + 7) / 8);
    }
    uint8_t *raw = (uint8_t *)malloc(total ? total : 1);
    if (!raw) { free(idat); img_free(im); snprintf(err, errsz, "out of memory"); return -1; }
    int full = 0;
    long got = fi_zlib(idat, idat_len, raw, total, &full);
    free(idat);
    if (got < 0) { free(raw); img_free(im); snprintf(err, errsz, "the PNG's picture data is damaged"); return -1; }
    if ((size_t)got < total) memset(raw + got, 0, total - (size_t)got); /* a cut file: the rest stays blank */
    size_t off = 0;
    int maxv = (1 << depth) - 1;
    for (int ps = 0; ps < passes; ps++) {
        int pw = interlace ? (w - sx[ps] + dx[ps] - 1) / dx[ps] : w, ph = interlace ? (h - sy[ps] + dy[ps] - 1) / dy[ps] : h;
        if (pw <= 0 || ph <= 0) continue;
        size_t stride = ((size_t)pw * bits + 7) / 8;
        if (img_png_unfilter(raw + off, stride, ph, bpp) != 0) break;
        for (int y = 0; y < ph; y++) {
            const uint8_t *row = raw + off + (size_t)y * (stride + 1) + 1;
            int oy = interlace ? sy[ps] + y * dy[ps] : y;
            for (int x = 0; x < pw; x++) {
                int ox = interlace ? sx[ps] + x * dx[ps] : x;
                unsigned v[4] = {0, 0, 0, 0}, raw16[4] = {0, 0, 0, 0};
                for (int c = 0; c < chans; c++) {
                    if (depth == 16) {
                        const uint8_t *q = row + ((size_t)x * chans + c) * 2;
                        raw16[c] = img_be16(q);
                        v[c] = q[0];
                    } else if (depth == 8) {
                        v[c] = raw16[c] = row[(size_t)x * chans + c];
                    } else {
                        size_t bit = (size_t)x * depth;
                        unsigned s = (row[bit / 8] >> (8 - depth - (bit % 8))) & (unsigned)maxv;
                        raw16[c] = s;
                        v[c] = ctype == 3 ? s : s * 255 / (unsigned)maxv;
                    }
                }
                uint32_t out;
                switch (ctype) {
                case 0: out = img_argb(raw16[0] == trns_grey ? 0 : 255, (int)v[0], (int)v[0], (int)v[0]); break;
                case 2: out = img_argb(raw16[0] == trns_rgb[0] && raw16[1] == trns_rgb[1] && raw16[2] == trns_rgb[2] ? 0 : 255, (int)v[0], (int)v[1], (int)v[2]); break;
                case 3: out = img_argb(pal[v[0] & 255][3], pal[v[0] & 255][0], pal[v[0] & 255][1], pal[v[0] & 255][2]); break;
                case 4: out = img_argb((int)v[1], (int)v[0], (int)v[0], (int)v[0]); break;
                default: out = img_argb((int)v[3], (int)v[0], (int)v[1], (int)v[2]); break;
                }
                if (ox < w && oy < h) im->px[(size_t)oy * w + ox] = out;
            }
        }
        off += (size_t)ph * (stride + 1);
    }
    free(raw);
    return 0;
}

/* ---- BMP and DIB ---------------------------------------------------------------------------------------------------------------- */

static int img_mask_shift(uint32_t m, int *bits) {
    int s = 0, b = 0;
    if (!m) { *bits = 0; return 0; }
    while (!(m & 1)) { m >>= 1; s++; }
    while (m & 1) { m >>= 1; b++; }
    *bits = b;
    return s;
}

static int img_mask_get(uint32_t v, uint32_t m, int def) {
    int bits, s = img_mask_shift(m, &bits);
    if (!bits) return def;
    uint32_t x = (v & m) >> s, maxv = (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1);
    return (int)((uint64_t)x * 255 / maxv);
}

/* A DIB (BITMAPINFOHEADER or later, or the old CORE header) at d. icon: the height counts the AND mask too. pixel_off: where the
 * pixels start in d (0: right after the header and palette). */
static int img_dib(const uint8_t *d, size_t n, size_t pixel_off, int icon, FImage *im, char *err, size_t errsz) {
    if (n < 12) { snprintf(err, errsz, "the bitmap header is cut"); return -1; }
    uint32_t hs = img_le32(d);
    int w, h, bpp, comp = 0, ncol = 0, core = hs == 12;
    if (core) { w = (int)img_le16(d + 4); h = (int16_t)img_le16(d + 6); bpp = (int)img_le16(d + 10); }
    else {
        if (hs < 16 || n < 40) { snprintf(err, errsz, "the bitmap header is cut"); return -1; }
        w = (int32_t)img_le32(d + 4); h = (int32_t)img_le32(d + 8); bpp = (int)img_le16(d + 14); comp = (int)img_le32(d + 16);
        ncol = (int)img_le32(d + 32);
    }
    if (icon) h /= 2;
    int topdown = h < 0;
    if (topdown) h = -h;
    uint32_t mr = 0x00FF0000, mg = 0x0000FF00, mb = 0x000000FF, ma = 0;
    if (bpp == 16) { mr = 0x7C00; mg = 0x03E0; mb = 0x001F; }
    size_t palpos = hs;
    if (!core && (comp == 3 || comp == 6)) {
        if (hs >= 52 && n >= 52) { mr = img_le32(d + 40); mg = img_le32(d + 44); mb = img_le32(d + 48); if (hs >= 56 && n >= 56) ma = img_le32(d + 52); }
        else if (n >= hs + 12) { mr = img_le32(d + hs); mg = img_le32(d + hs + 4); mb = img_le32(d + hs + 8); palpos += 12; }
    } else if (bpp == 32 && !icon) {
        ma = 0; /* plain 32-bit BMPs leave alpha undefined */
    } else if (bpp == 32) {
        ma = 0xFF000000;
    }
    if (comp != 0 && comp != 1 && comp != 2 && comp != 3 && comp != 6) { snprintf(err, errsz, "a compressed BMP this viewer cannot read"); return -1; }
    if (!(bpp == 1 || bpp == 4 || bpp == 8 || bpp == 16 || bpp == 24 || bpp == 32)) { snprintf(err, errsz, "a %d-bit BMP", bpp); return -1; }
    if (bpp <= 8 && ncol == 0) ncol = 1 << bpp;
    if (ncol > 256) ncol = 256;
    uint32_t pal[256];
    int pe = core ? 3 : 4;
    for (int i = 0; i < 256; i++) pal[i] = 0xFF000000u;
    for (int i = 0; i < ncol && bpp <= 8; i++) {
        size_t q = palpos + (size_t)i * pe;
        if (q + 3 <= n) pal[i] = img_argb(255, d[q + 2], d[q + 1], d[q]);
    }
    if (!pixel_off) pixel_off = palpos + (bpp <= 8 ? (size_t)ncol * pe : 0);
    if (img_alloc(im, w, h, err, errsz) != 0) return -1;
    if (pixel_off >= n) return 0; /* nothing there: a blank picture */
    const uint8_t *px = d + pixel_off;
    size_t avail = n - pixel_off;
    if (comp == 1 || comp == 2) { /* RLE8 / RLE4, bottom-up */
        int x = 0, y = 0;
        size_t i = 0;
        while (i + 1 < avail && y < h) {
            int c = px[i], v = px[i + 1];
            i += 2;
            if (c) {
                for (int k = 0; k < c && x < w; k++) {
                    int idx = comp == 1 ? v : ((k & 1) ? (v & 15) : (v >> 4));
                    im->px[(size_t)(h - 1 - y) * w + x++] = pal[idx];
                }
            } else if (v == 0) { x = 0; y++; }
            else if (v == 1) break;
            else if (v == 2) { if (i + 1 >= avail) break; x += px[i]; y += px[i + 1]; i += 2; }
            else {
                for (int k = 0; k < v && x < w; k++) {
                    size_t at = i + (comp == 1 ? (size_t)k : (size_t)k / 2);
                    if (at >= avail) break;
                    int idx = comp == 1 ? px[at] : ((k & 1) ? (px[at] & 15) : (px[at] >> 4));
                    im->px[(size_t)(h - 1 - y) * w + x++] = pal[idx];
                }
                size_t used = comp == 1 ? (size_t)v : ((size_t)v + 1) / 2;
                i += (used + 1) & ~(size_t)1;
            }
        }
        return 0;
    }
    size_t stride = (((size_t)w * bpp + 31) / 32) * 4;
    size_t mstride = (((size_t)w + 31) / 32) * 4;
    for (int y = 0; y < h; y++) {
        size_t ro = (size_t)y * stride;
        if (ro + stride > avail) break;
        const uint8_t *row = px + ro;
        int oy = topdown ? y : h - 1 - y;
        for (int x = 0; x < w; x++) {
            uint32_t c;
            if (bpp <= 8) {
                size_t bit = (size_t)x * bpp;
                int idx = (row[bit / 8] >> (8 - bpp - (bit % 8))) & ((1 << bpp) - 1);
                c = pal[idx];
            } else if (bpp == 24) {
                c = img_argb(255, row[x * 3 + 2], row[x * 3 + 1], row[x * 3]);
            } else {
                uint32_t v = bpp == 16 ? img_le16(row + x * 2) : img_le32(row + x * 4);
                c = img_argb(ma ? img_mask_get(v, ma, 255) : 255, img_mask_get(v, mr, 0), img_mask_get(v, mg, 0), img_mask_get(v, mb, 0));
            }
            im->px[(size_t)oy * w + x] = c;
        }
    }
    if (icon && bpp < 32) { /* the AND mask: 1 is transparent */
        size_t mo = (size_t)h * stride;
        for (int y = 0; y < h; y++) {
            size_t ro = mo + (size_t)y * mstride;
            if (ro + mstride > avail) break;
            int oy = topdown ? y : h - 1 - y;
            for (int x = 0; x < w; x++)
                if ((px[ro + x / 8] >> (7 - x % 8)) & 1) im->px[(size_t)oy * w + x] &= 0x00FFFFFFu;
        }
    }
    return 0;
}

static int img_bmp(const uint8_t *buf, size_t n, FImage *im, char *err, size_t errsz) {
    if (n < 26 || buf[0] != 'B' || buf[1] != 'M') { snprintf(err, errsz, "not a BMP"); return -1; }
    size_t off = img_le32(buf + 10);
    return img_dib(buf + 14, n - 14, off >= 14 ? off - 14 : 0, 0, im, err, errsz);
}

static int img_ico(const uint8_t *buf, size_t n, FImage *im, char *err, size_t errsz) {
    if (n < 6) { snprintf(err, errsz, "the icon is cut"); return -1; }
    int count = (int)img_le16(buf + 4), best = -1;
    long best_area = -1;
    for (int i = 0; i < count && 6 + (size_t)i * 16 + 16 <= n; i++) {
        const uint8_t *e = buf + 6 + i * 16;
        long area = (long)(e[0] ? e[0] : 256) * (e[1] ? e[1] : 256);
        if (area > best_area) { best_area = area; best = i; }
    }
    if (best < 0) { snprintf(err, errsz, "the icon has no pictures"); return -1; }
    const uint8_t *e = buf + 6 + best * 16;
    uint32_t size = img_le32(e + 8), off = img_le32(e + 12);
    if (off >= n) { snprintf(err, errsz, "the icon's picture is missing"); return -1; }
    if (size > n - off) size = (uint32_t)(n - off);
    if (size >= 8 && !memcmp(buf + off, "\x89PNG", 4)) return img_png(buf + off, size, im, err, errsz);
    return img_dib(buf + off, size, 0, 1, im, err, errsz);
}

/* ---- GIF ---------------------------------------------------------------------------------------------------------------------- */

static int img_gif(const uint8_t *buf, size_t n, FImage *im, char *err, size_t errsz) {
    if (n < 13 || memcmp(buf, "GIF8", 4) != 0) { snprintf(err, errsz, "not a GIF"); return -1; }
    int sw = (int)img_le16(buf + 6), sh = (int)img_le16(buf + 8), flags = buf[10], bgi = buf[11];
    uint32_t gct[256], lct[256];
    int gsize = 0, transparent = -1;
    size_t p = 13;
    if (flags & 0x80) {
        gsize = 2 << (flags & 7);
        for (int i = 0; i < gsize; i++) gct[i] = p + (size_t)i * 3 + 3 <= n ? img_argb(255, buf[p + i * 3], buf[p + i * 3 + 1], buf[p + i * 3 + 2]) : 0xFF000000u;
        p += (size_t)gsize * 3;
    }
    (void)bgi;
    while (p < n) {
        int b = buf[p++];
        if (b == 0x21) { /* extension */
            if (p >= n) break;
            int label = buf[p++];
            while (p < n && buf[p]) {
                int len = buf[p];
                if (label == 0xF9 && len >= 4 && p + 4 < n && (buf[p + 1] & 1)) transparent = buf[p + 4];
                p += (size_t)len + 1;
            }
            p++;
        } else if (b == 0x2C) {
            if (p + 9 > n) break;
            int ix = (int)img_le16(buf + p), iy = (int)img_le16(buf + p + 2), iw = (int)img_le16(buf + p + 4), ih = (int)img_le16(buf + p + 6);
            int f = buf[p + 8];
            p += 9;
            uint32_t *ct = gct;
            int csize = gsize;
            if (f & 0x80) {
                csize = 2 << (f & 7);
                for (int i = 0; i < csize; i++) lct[i] = p + (size_t)i * 3 + 3 <= n ? img_argb(255, buf[p + i * 3], buf[p + i * 3 + 1], buf[p + i * 3 + 2]) : 0xFF000000u;
                p += (size_t)csize * 3;
                ct = lct;
            }
            if (!sw || !sh) { sw = ix + iw; sh = iy + ih; }
            if (img_alloc(im, sw, sh, err, errsz) != 0) return -1;
            if (p >= n) return 0;
            int minsize = buf[p++];
            if (minsize < 2 || minsize > 11) return 0;
            /* gather the sub-blocks */
            size_t dl = 0, q = p;
            while (q < n && buf[q]) { dl += buf[q]; q += (size_t)buf[q] + 1; }
            uint8_t *data = (uint8_t *)malloc(dl + 1);
            if (!data) { img_free(im); snprintf(err, errsz, "out of memory"); return -1; }
            dl = 0;
            q = p;
            while (q < n && buf[q]) { size_t len = buf[q]; if (q + 1 + len > n) len = n - q - 1; memcpy(data + dl, buf + q + 1, len); dl += len; q += (size_t)buf[q] + 1; }
            /* LZW */
            static uint16_t prefix[4096];
            static uint8_t suffix[4096], stack[4097];
            int clear = 1 << minsize, eoi = clear + 1, avail = clear + 2, codesize = minsize + 1, mask = (1 << codesize) - 1, old = -1, first = 0;
            for (int i = 0; i < clear; i++) { prefix[i] = 0; suffix[i] = (uint8_t)i; }
            uint32_t acc = 0;
            int bits = 0;
            size_t di = 0, pix = 0, total = (size_t)iw * ih;
            int interlaced = f & 0x40, pass = 0, row = 0, col = 0;
            static const int istart[4] = {0, 4, 2, 1}, istep[4] = {8, 8, 4, 2};
            while (pix < total) {
                while (bits < codesize && di < dl) { acc |= (uint32_t)data[di++] << bits; bits += 8; }
                if (bits < codesize) break;
                int code = (int)(acc & (uint32_t)mask);
                acc >>= codesize;
                bits -= codesize;
                if (code == clear) { codesize = minsize + 1; mask = (1 << codesize) - 1; avail = clear + 2; old = -1; continue; }
                if (code == eoi) break;
                int sp = 0, in = code;
                if (old == -1) {
                    if (code >= clear) break;
                    stack[sp++] = (uint8_t)code;
                    first = code;
                } else {
                    if (code > avail) break;
                    if (code == avail) { stack[sp++] = (uint8_t)first; code = old; }
                    while (code >= clear && sp < 4096) { stack[sp++] = suffix[code]; code = prefix[code]; }
                    stack[sp++] = suffix[code];
                    first = suffix[code];
                    if (avail < 4096) {
                        prefix[avail] = (uint16_t)old;
                        suffix[avail] = (uint8_t)first;
                        avail++;
                        if ((avail & mask) == 0 && avail < 4096) { codesize++; mask = (1 << codesize) - 1; }
                    }
                }
                old = in;
                while (sp > 0 && pix < total) {
                    int idx = stack[--sp];
                    int x = ix + col, y = iy + (interlaced ? row : (int)(pix / iw));
                    if (!interlaced) x = ix + (int)(pix % iw);
                    if (x < sw && y < sh && idx != transparent && idx < (csize ? csize : 256)) im->px[(size_t)y * sw + x] = ct[idx];
                    pix++;
                    if (interlaced && ++col >= iw) {
                        col = 0;
                        row += istep[pass];
                        while (row >= ih && pass < 3) { pass++; row = istart[pass]; }
                    }
                }
            }
            free(data);
            return 0;
        } else {
            break;
        }
    }
    snprintf(err, errsz, "the GIF has no picture");
    return -1;
}

/* ---- JPEG (baseline) ---------------------------------------------------------------------------------------------------------- */

typedef struct {
    uint8_t val[256];
    int n;
    int maxcode[18], valptr[17], mincode[17];
} JHuff;

typedef struct {
    const uint8_t *d;
    size_t n, p;
    uint32_t acc;
    int bits, marker_hit;
} JBits;

static int jb_fill(JBits *b) {
    while (b->bits <= 24) {
        int c = 0;
        if (!b->marker_hit && b->p < b->n) {
            c = b->d[b->p];
            if (c == 0xFF) {
                int nx = b->p + 1 < b->n ? b->d[b->p + 1] : 0;
                if (nx == 0) b->p += 2;
                else { b->marker_hit = 1; c = 0; }
            } else {
                b->p++;
            }
        }
        b->acc |= (uint32_t)c << (24 - b->bits);
        b->bits += 8;
    }
    return 0;
}

static int jb_get(JBits *b, int n) {
    if (n == 0) return 0;
    jb_fill(b);
    int v = (int)(b->acc >> (32 - n));
    b->acc <<= n;
    b->bits -= n;
    return v;
}

static int jb_decode(JBits *b, const JHuff *h) {
    jb_fill(b);
    int code = 0;
    for (int l = 1; l <= 16; l++) {
        code = (code << 1) | (int)(b->acc >> 31);
        b->acc <<= 1;
        b->bits--;
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l]) {
            int i = h->valptr[l] + code - h->mincode[l];
            return (i >= 0 && i < 256) ? h->val[i] : -1;
        }
        if (b->bits < 8) jb_fill(b);
    }
    return -1;
}

static int jb_extend(int v, int t) { return t == 0 ? 0 : (v < (1 << (t - 1)) ? v - (1 << t) + 1 : v); }

static void jh_build(JHuff *h, const uint8_t counts[16], const uint8_t *vals) {
    int k = 0, code = 0;
    for (int l = 1; l <= 16; l++) {
        h->valptr[l] = k;
        h->mincode[l] = code;
        for (int i = 0; i < counts[l - 1] && k < 256; i++) { h->val[k] = vals[k]; k++; code++; }
        h->maxcode[l] = counts[l - 1] ? code - 1 : -1;
        code <<= 1;
    }
    h->n = k;
}

static const uint8_t J_ZIGZAG[64] = {0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
                                     35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

static void j_idct(const float in[64], uint8_t *out, int stride) {
    static float c[8][8];
    static int ready;
    if (!ready) {
        for (int x = 0; x < 8; x++)
            for (int u = 0; u < 8; u++) c[x][u] = (float)((u == 0 ? sqrt(0.5) : 1.0) * cos((2 * x + 1) * u * 3.14159265358979323846 / 16) / 2);
        ready = 1;
    }
    float tmp[64];
    for (int y = 0; y < 8; y++)
        for (int u = 0; u < 8; u++) {
            float s = 0;
            for (int v = 0; v < 8; v++) s += c[y][v] * in[v * 8 + u];
            tmp[y * 8 + u] = s;
        }
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            float s = 0;
            for (int u = 0; u < 8; u++) s += c[x][u] * tmp[y * 8 + u];
            int v = (int)lroundf(s + 128);
            out[y * stride + x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
}

static int img_jpeg(const uint8_t *buf, size_t n, FImage *im, char *err, size_t errsz) {
    if (n < 4 || buf[0] != 0xFF || buf[1] != 0xD8) { snprintf(err, errsz, "not a JPEG"); return -1; }
    uint16_t q[4][64];
    JHuff hdc[4], hac[4];
    memset(q, 0, sizeof q);
    memset(hdc, 0, sizeof hdc);
    memset(hac, 0, sizeof hac);
    struct { int id, h, v, tq, td, ta, pred; uint8_t *plane; int pw, ph; } comp[4];
    int ncomp = 0, w = 0, h = 0, restart = 0, hmax = 1, vmax = 1, adobe_transform = -1;
    size_t p = 2;
    while (p + 4 <= n) {
        if (buf[p] != 0xFF) { p++; continue; }
        int m = buf[p + 1];
        if (m == 0xFF) { p++; continue; }
        if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) { p += 2; continue; }
        if (m == 0xD9) break;
        size_t len = img_be16(buf + p + 2);
        if (len < 2 || p + 2 + len > n) break;
        const uint8_t *s = buf + p + 4;
        size_t sl = len - 2;
        if (m == 0xDB) {
            size_t i = 0;
            while (i < sl) {
                int pq = s[i] >> 4, tq = s[i] & 3;
                i++;
                for (int k = 0; k < 64 && i < sl; k++) { q[tq][J_ZIGZAG[k]] = pq ? (uint16_t)img_be16(s + i) : s[i]; i += pq ? 2 : 1; }
            }
        } else if (m == 0xC4) {
            size_t i = 0;
            while (i + 17 <= sl) {
                int tc = s[i] >> 4, th = s[i] & 3, tot = 0;
                for (int k = 0; k < 16; k++) tot += s[i + 1 + k];
                if (i + 17 + (size_t)tot > sl || tot > 256) break;
                jh_build(tc ? &hac[th] : &hdc[th], s + i + 1, s + i + 17);
                i += 17 + (size_t)tot;
            }
        } else if (m == 0xC0 || m == 0xC1) {
            if (sl < 6) break;
            h = (int)img_be16(s + 1); w = (int)img_be16(s + 3); ncomp = s[5];
            if (ncomp != 1 && ncomp != 3) { snprintf(err, errsz, "a %d-colour-channel JPEG (CMYK?) is not drawn", ncomp); return -1; }
            if (sl < 6 + (size_t)ncomp * 3) break;
            for (int c = 0; c < ncomp; c++) {
                comp[c].id = s[6 + c * 3];
                comp[c].h = s[7 + c * 3] >> 4;
                comp[c].v = s[7 + c * 3] & 15;
                comp[c].tq = s[8 + c * 3] & 3;
                if (comp[c].h < 1 || comp[c].h > 4 || comp[c].v < 1 || comp[c].v > 4) { snprintf(err, errsz, "a JPEG with odd sampling"); return -1; }
                if (comp[c].h > hmax) hmax = comp[c].h;
                if (comp[c].v > vmax) vmax = comp[c].v;
                comp[c].plane = NULL;
            }
        } else if (m == 0xC2 || m == 0xC6 || m == 0xCA) {
            snprintf(err, errsz, "a progressive JPEG: only its details are shown");
            return -1;
        } else if ((m >= 0xC3 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            snprintf(err, errsz, "a lossless or arithmetic-coded JPEG: only its details are shown");
            return -1;
        } else if (m == 0xDD && sl >= 2) {
            restart = (int)img_be16(s);
        } else if (m == 0xEE && sl >= 12 && !memcmp(s, "Adobe", 5)) {
            adobe_transform = s[11];
        } else if (m == 0xDA) {
            if (!w || !h || !ncomp) break;
            int ns = s[0];
            if (ns != ncomp) { snprintf(err, errsz, "a JPEG in several scans: only its details are shown"); return -1; }
            for (int i = 0; i < ns && 1 + i * 2 + 1 < (int)sl; i++) {
                int cid = s[1 + i * 2], t = s[2 + i * 2];
                for (int c = 0; c < ncomp; c++)
                    if (comp[c].id == cid) { comp[c].td = t >> 4 & 3; comp[c].ta = t & 3; }
            }
            /* A photo much bigger than the screen is decoded at 1/8 size: each 8x8 block is its average (the DC term), which skips
             * the inverse DCT, by far the slowest part, and is still sharper than the screen can show. */
            int dc = w / 8 >= 320 && h / 8 >= 200, sc = dc ? 8 : 1;
            int ow = (w + sc - 1) / sc, oh = (h + sc - 1) / sc;
            if (img_alloc(im, ow, oh, err, errsz) != 0) return -1;
            int mcux = (w + 8 * hmax - 1) / (8 * hmax), mcuy = (h + 8 * vmax - 1) / (8 * vmax);
            for (int c = 0; c < ncomp; c++) {
                comp[c].pw = mcux * comp[c].h * 8 / sc;
                comp[c].ph = mcuy * comp[c].v * 8 / sc;
                comp[c].plane = (uint8_t *)malloc((size_t)comp[c].pw * comp[c].ph);
                comp[c].pred = 0;
                if (!comp[c].plane) {
                    for (int k = 0; k < c; k++) free(comp[k].plane);
                    img_free(im);
                    snprintf(err, errsz, "out of memory");
                    return -1;
                }
                memset(comp[c].plane, 128, (size_t)comp[c].pw * comp[c].ph);
            }
            JBits b = {buf, n, p + 2 + len, 0, 0, 0};
            int mcus = 0, bad = 0;
            for (int my = 0; my < mcuy && !bad; my++)
                for (int mx = 0; mx < mcux && !bad; mx++) {
                    if (restart && mcus && mcus % restart == 0) {
                        /* skip to the RST marker */
                        b.acc = 0; b.bits = 0;
                        while (b.p + 1 < b.n && !(b.d[b.p] == 0xFF && b.d[b.p + 1] >= 0xD0 && b.d[b.p + 1] <= 0xD7)) b.p++;
                        if (b.p + 1 < b.n) b.p += 2;
                        b.marker_hit = 0;
                        for (int c = 0; c < ncomp; c++) comp[c].pred = 0;
                    }
                    mcus++;
                    for (int c = 0; c < ncomp && !bad; c++)
                        for (int by = 0; by < comp[c].v && !bad; by++)
                            for (int bx = 0; bx < comp[c].h; bx++) {
                                float blk[64];
                                int coef[64];
                                memset(coef, 0, sizeof coef);
                                int t = jb_decode(&b, &hdc[comp[c].td]);
                                if (t < 0 || t > 16) { bad = 1; break; }
                                comp[c].pred += jb_extend(jb_get(&b, t), t);
                                coef[0] = comp[c].pred * q[comp[c].tq][0];
                                for (int k = 1; k < 64;) {
                                    int rs = jb_decode(&b, &hac[comp[c].ta]);
                                    if (rs < 0) { bad = 1; break; }
                                    int r = rs >> 4, sz = rs & 15;
                                    if (!sz) { if (r == 15) { k += 16; continue; } break; }
                                    k += r;
                                    if (k > 63) break;
                                    coef[J_ZIGZAG[k]] = jb_extend(jb_get(&b, sz), sz) * q[comp[c].tq][J_ZIGZAG[k]];
                                    k++;
                                }
                                if (bad) break;
                                if (dc) {
                                    int v = (int)lroundf(coef[0] / 8.0f + 128);
                                    comp[c].plane[(size_t)(my * comp[c].v + by) * comp[c].pw + (mx * comp[c].h + bx)] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
                                    continue;
                                }
                                for (int k = 0; k < 64; k++) blk[k] = (float)coef[k];
                                int ox = (mx * comp[c].h + bx) * 8, oy = (my * comp[c].v + by) * 8;
                                j_idct(blk, comp[c].plane + (size_t)oy * comp[c].pw + ox, comp[c].pw);
                            }
                }
            /* colour */
            w = ow;
            h = oh;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    int v[3];
                    for (int c = 0; c < ncomp; c++) {
                        int sx = x * comp[c].h / hmax, sy = y * comp[c].v / vmax;
                        v[c] = comp[c].plane[(size_t)sy * comp[c].pw + sx];
                    }
                    if (ncomp == 1) { im->px[(size_t)y * w + x] = img_argb(255, v[0], v[0], v[0]); continue; }
                    if (adobe_transform == 0) { im->px[(size_t)y * w + x] = img_argb(255, v[0], v[1], v[2]); continue; }
                    float Y = (float)v[0], cb = (float)v[1] - 128, cr = (float)v[2] - 128;
                    int r = (int)lroundf(Y + 1.402f * cr), g = (int)lroundf(Y - 0.344136f * cb - 0.714136f * cr), bl = (int)lroundf(Y + 1.772f * cb);
                    r = r < 0 ? 0 : r > 255 ? 255 : r;
                    g = g < 0 ? 0 : g > 255 ? 255 : g;
                    bl = bl < 0 ? 0 : bl > 255 ? 255 : bl;
                    im->px[(size_t)y * w + x] = img_argb(255, r, g, bl);
                }
            for (int c = 0; c < ncomp; c++) free(comp[c].plane);
            return 0;
        }
        p += 2 + len;
    }
    snprintf(err, errsz, "the JPEG has no picture this viewer can read");
    return -1;
}

/* ---- TGA, QOI, PCX, PNM ------------------------------------------------------------------------------------------------------- */

static int img_tga(const uint8_t *buf, size_t n, FImage *im, char *err, size_t errsz) {
    if (n < 18) { snprintf(err, errsz, "not a TGA"); return -1; }
    int idlen = buf[0], cmtype = buf[1], type = buf[2], cmfirst = (int)img_le16(buf + 3), cmlen = (int)img_le16(buf + 5), cmbits = buf[7];
    int w = (int)img_le16(buf + 12), h = (int)img_le16(buf + 14), bpp = buf[16], desc = buf[17];
    int base = type & 7, rle = type & 8;
    if (!(base == 1 || base == 2 || base == 3) || !(bpp == 8 || bpp == 15 || bpp == 16 || bpp == 24 || bpp == 32)) { snprintf(err, errsz, "a TGA this viewer cannot read"); return -1; }
    size_t p = 18 + (size_t)idlen;
    uint32_t pal[256];
    for (int i = 0; i < 256; i++) pal[i] = 0xFF000000u;
    int cmb = (cmbits + 7) / 8;
    if (cmtype == 1) {
        for (int i = 0; i < cmlen; i++) {
            size_t q = p + (size_t)i * cmb;
            int idx = cmfirst + i;
            if (q + (size_t)cmb > n || idx > 255) continue;
            if (cmb >= 3) pal[idx] = img_argb(cmb == 4 ? buf[q + 3] : 255, buf[q + 2], buf[q + 1], buf[q]);
            else if (cmb == 2) { unsigned v = img_le16(buf + q); pal[idx] = img_argb(255, (int)((v >> 10 & 31) * 255 / 31), (int)((v >> 5 & 31) * 255 / 31), (int)((v & 31) * 255 / 31)); }
        }
        p += (size_t)cmlen * cmb;
    }
    if (img_alloc(im, w, h, err, errsz) != 0) return -1;
    int pb = (bpp + 7) / 8;
    size_t total = (size_t)w * h, i = 0;
    int alpha_bits = desc & 15;
    while (i < total && p < n) {
        int count = 1, raw = 1;
        if (rle) { int hd = buf[p++]; count = (hd & 127) + 1; raw = !(hd & 128); }
        for (int k = 0; k < count && i < total; k++) {
            if (p + (size_t)pb > n) { i = total; break; }
            const uint8_t *s = buf + p;
            uint32_t c;
            if (base == 1) c = pal[s[0]];
            else if (base == 3) c = img_argb(255, s[0], s[0], s[0]);
            else if (pb == 2) { unsigned v = img_le16(s); c = img_argb(255, (int)((v >> 10 & 31) * 255 / 31), (int)((v >> 5 & 31) * 255 / 31), (int)((v & 31) * 255 / 31)); }
            else c = img_argb(pb == 4 && alpha_bits ? s[3] : 255, s[2], s[1], s[0]);
            int x = (int)(i % w), y = (int)(i / w);
            if (desc & 0x10) x = w - 1 - x;
            if (!(desc & 0x20)) y = h - 1 - y;
            im->px[(size_t)y * w + x] = c;
            i++;
            if (raw) p += pb;
        }
        if (!raw) p += pb;
    }
    return 0;
}

static int img_qoi(const uint8_t *buf, size_t n, FImage *im, char *err, size_t errsz) {
    if (n < 22 || memcmp(buf, "qoif", 4) != 0) { snprintf(err, errsz, "not a QOI picture"); return -1; }
    int w = (int)img_be32(buf + 4), h = (int)img_be32(buf + 8);
    if (img_alloc(im, w, h, err, errsz) != 0) return -1;
    uint8_t idx[64][4], px[4] = {0, 0, 0, 255};
    memset(idx, 0, sizeof idx);
    size_t p = 14, total = (size_t)w * h, i = 0;
    int run = 0;
    while (i < total) {
        if (run > 0) run--;
        else if (p < n) {
            int b = buf[p++];
            if (b == 0xFE && p + 3 <= n) { px[0] = buf[p]; px[1] = buf[p + 1]; px[2] = buf[p + 2]; p += 3; }
            else if (b == 0xFF && p + 4 <= n) { memcpy(px, buf + p, 4); p += 4; }
            else if ((b & 0xC0) == 0x00) memcpy(px, idx[b], 4);
            else if ((b & 0xC0) == 0x40) { px[0] += (uint8_t)((b >> 4 & 3) - 2); px[1] += (uint8_t)((b >> 2 & 3) - 2); px[2] += (uint8_t)((b & 3) - 2); }
            else if ((b & 0xC0) == 0x80 && p < n) {
                int dg = (b & 63) - 32, b2 = buf[p++];
                px[0] += (uint8_t)(dg - 8 + (b2 >> 4)); px[1] += (uint8_t)dg; px[2] += (uint8_t)(dg - 8 + (b2 & 15));
            } else if ((b & 0xC0) == 0xC0) run = b & 63;
            memcpy(idx[(px[0] * 3 + px[1] * 5 + px[2] * 7 + px[3] * 11) % 64], px, 4);
        } else break;
        im->px[i++] = img_argb(px[3], px[0], px[1], px[2]);
    }
    return 0;
}

static int img_pcx(const uint8_t *buf, size_t n, FImage *im, char *err, size_t errsz) {
    if (n < 128 || buf[0] != 0x0A) { snprintf(err, errsz, "not a PCX picture"); return -1; }
    int bpp = buf[3], planes = buf[65], bpl = (int)img_le16(buf + 66);
    int w = (int)img_le16(buf + 8) - (int)img_le16(buf + 4) + 1, h = (int)img_le16(buf + 10) - (int)img_le16(buf + 6) + 1;
    if (!((bpp == 8 && (planes == 1 || planes == 3 || planes == 4)) || (bpp == 1 && (planes == 1 || planes == 4)))) { snprintf(err, errsz, "a PCX this viewer cannot read"); return -1; }
    if (img_alloc(im, w, h, err, errsz) != 0) return -1;
    size_t linelen = (size_t)bpl * planes;
    uint8_t *line = (uint8_t *)malloc(linelen ? linelen : 1);
    if (!line) { img_free(im); snprintf(err, errsz, "out of memory"); return -1; }
    uint32_t pal[256];
    for (int i = 0; i < 16; i++) pal[i] = img_argb(255, buf[16 + i * 3], buf[17 + i * 3], buf[18 + i * 3]);
    if (bpp == 1 && planes == 1) { pal[0] = 0xFF000000u; pal[1] = 0xFFFFFFFFu; }
    if (bpp == 8 && planes == 1 && n >= 769 && buf[n - 769] == 12)
        for (int i = 0; i < 256; i++) pal[i] = img_argb(255, buf[n - 768 + i * 3], buf[n - 767 + i * 3], buf[n - 766 + i * 3]);
    size_t p = 128;
    for (int y = 0; y < h; y++) {
        size_t k = 0;
        while (k < linelen && p < n) {
            int b = buf[p++], cnt = 1;
            if ((b & 0xC0) == 0xC0) { cnt = b & 63; b = p < n ? buf[p++] : 0; }
            while (cnt-- && k < linelen) line[k++] = (uint8_t)b;
        }
        for (int x = 0; x < w && x < bpl * 8; x++) {
            uint32_t c;
            if (bpp == 8 && planes == 1) c = x < bpl ? pal[line[x]] : 0xFF000000u;
            else if (bpp == 8) c = x < bpl ? img_argb(planes == 4 ? line[3 * bpl + x] : 255, line[x], line[bpl + x], line[2 * bpl + x]) : 0xFF000000u;
            else {
                int idx = 0;
                for (int pl = 0; pl < planes; pl++) idx |= ((line[pl * bpl + x / 8] >> (7 - x % 8)) & 1) << pl;
                c = pal[idx & 15];
            }
            im->px[(size_t)y * w + x] = c;
        }
    }
    free(line);
    return 0;
}

static int pnm_int(const uint8_t *buf, size_t n, size_t *p) {
    for (;;) {
        while (*p < n && (buf[*p] == ' ' || buf[*p] == '\t' || buf[*p] == '\n' || buf[*p] == '\r')) (*p)++;
        if (*p < n && buf[*p] == '#') { while (*p < n && buf[*p] != '\n') (*p)++; continue; }
        break;
    }
    if (*p >= n || buf[*p] < '0' || buf[*p] > '9') return -1;
    long v = 0;
    while (*p < n && buf[*p] >= '0' && buf[*p] <= '9') { v = v * 10 + (buf[*p] - '0'); if (v > 1 << 24) return -1; (*p)++; }
    return (int)v;
}

static int img_pnm(const uint8_t *buf, size_t n, FImage *im, char *err, size_t errsz) {
    if (n < 3 || buf[0] != 'P' || buf[1] < '1' || buf[1] > '6') { snprintf(err, errsz, "not a PNM picture"); return -1; }
    int kind = buf[1] - '0';
    size_t p = 2;
    int w = pnm_int(buf, n, &p), h = pnm_int(buf, n, &p), maxv = (kind == 1 || kind == 4) ? 1 : pnm_int(buf, n, &p);
    if (w <= 0 || h <= 0 || maxv <= 0 || maxv > 65535) { snprintf(err, errsz, "the PNM header is damaged"); return -1; }
    if (img_alloc(im, w, h, err, errsz) != 0) return -1;
    p++;
    int chans = (kind == 3 || kind == 6) ? 3 : 1, wide = maxv > 255;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int v[3] = {0, 0, 0};
            for (int c = 0; c < chans; c++) {
                if (kind <= 3) {
                    if (kind == 1) { while (p < n && (buf[p] < '0' || buf[p] > '1')) p++; v[c] = p < n ? buf[p++] - '0' : 0; }
                    else { int r = pnm_int(buf, n, &p); v[c] = r < 0 ? 0 : r; }
                } else if (kind == 4) {
                    size_t at = p + (size_t)y * ((w + 7) / 8) + x / 8;
                    v[c] = at < n ? (buf[at] >> (7 - x % 8)) & 1 : 0;
                } else {
                    size_t at = p + (((size_t)y * w + x) * chans + c) * (wide ? 2 : 1);
                    v[c] = at + (wide ? 1 : 0) < n ? (wide ? (int)img_be16(buf + at) : buf[at]) : 0;
                }
            }
            if (kind == 1 || kind == 4) { int g = v[0] ? 0 : 255; v[0] = v[1] = v[2] = g; }
            else {
                for (int c = 0; c < chans; c++) v[c] = v[c] * 255 / maxv;
                if (chans == 1) v[1] = v[2] = v[0];
            }
            im->px[(size_t)y * w + x] = img_argb(255, v[0] > 255 ? 255 : v[0], v[1] > 255 ? 255 : v[1], v[2] > 255 ? 255 : v[2]);
        }
    }
    return 0;
}

/* ---- by format --------------------------------------------------------------------------------------------------------------- */

enum { IMG_NONE, IMG_PNG, IMG_JPEG, IMG_GIF, IMG_BMP, IMG_ICO, IMG_TGA, IMG_QOI, IMG_PCX, IMG_PNM };

/* Which decoder reads these bytes (TGA has no signature: only by name). */
static int img_kind(const uint8_t *b, size_t n, const char *ext) {
    if (n >= 8 && !memcmp(b, "\x89PNG\r\n\x1a\n", 8)) return IMG_PNG;
    if (n >= 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) return IMG_JPEG;
    if (n >= 6 && (!memcmp(b, "GIF87a", 6) || !memcmp(b, "GIF89a", 6))) return IMG_GIF;
    if (n >= 14 && b[0] == 'B' && b[1] == 'M') return IMG_BMP;
    if (n >= 6 && b[0] == 0 && b[1] == 0 && (b[2] == 1 || b[2] == 2) && b[3] == 0 && ext && (!strcasecmp(ext, "ico") || !strcasecmp(ext, "cur"))) return IMG_ICO;
    if (n >= 4 && !memcmp(b, "qoif", 4)) return IMG_QOI;
    if (n >= 128 && b[0] == 0x0A && b[1] <= 5 && b[2] == 1 && ext && !strcasecmp(ext, "pcx")) return IMG_PCX;
    if (n >= 3 && b[0] == 'P' && b[1] >= '1' && b[1] <= '6' && (b[2] == '\n' || b[2] == ' ' || b[2] == '\r' || b[2] == '\t')) return IMG_PNM;
    if (ext && (!strcasecmp(ext, "tga") || !strcasecmp(ext, "icb") || !strcasecmp(ext, "vda") || !strcasecmp(ext, "vst"))) return IMG_TGA;
    return IMG_NONE;
}

static int img_decode(int kind, const uint8_t *b, size_t n, FImage *im, char *err, size_t errsz) {
    memset(im, 0, sizeof *im);
    switch (kind) {
    case IMG_PNG: return img_png(b, n, im, err, errsz);
    case IMG_JPEG: return img_jpeg(b, n, im, err, errsz);
    case IMG_GIF: return img_gif(b, n, im, err, errsz);
    case IMG_BMP: return img_bmp(b, n, im, err, errsz);
    case IMG_ICO: return img_ico(b, n, im, err, errsz);
    case IMG_TGA: return img_tga(b, n, im, err, errsz);
    case IMG_QOI: return img_qoi(b, n, im, err, errsz);
    case IMG_PCX: return img_pcx(b, n, im, err, errsz);
    case IMG_PNM: return img_pnm(b, n, im, err, errsz);
    default: snprintf(err, errsz, "not a picture"); return -1;
    }
}

/* Turn a picture by its EXIF orientation (3: half turn, 6: a quarter clockwise, 8: a quarter anticlockwise). */
static void img_orient(FImage *im, int orientation) {
    if (orientation != 3 && orientation != 6 && orientation != 8) return;
    int w = im->w, h = im->h, nw = orientation == 3 ? w : h, nh = orientation == 3 ? h : w;
    uint32_t *out = (uint32_t *)malloc((size_t)w * h * sizeof(uint32_t));
    if (!out) return;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int nx, ny;
            if (orientation == 3) { nx = w - 1 - x; ny = h - 1 - y; }
            else if (orientation == 6) { nx = h - 1 - y; ny = x; }
            else { nx = y; ny = w - 1 - x; }
            out[(size_t)ny * nw + nx] = im->px[(size_t)y * w + x];
        }
    free(im->px);
    im->px = out;
    im->w = nw;
    im->h = nh;
}

/* Fit a picture into box_w x box_h: a small one grows by a whole number (crisp pixels), a big one shrinks by averaging. Alpha is
 * laid over a dark checkerboard so transparent parts show. Returns a new box_w x box_h-or-smaller picture in *out. */
static int img_fit(const FImage *in, int box_w, int box_h, FImage *out) {
    memset(out, 0, sizeof *out);
    if (in->w <= 0 || in->h <= 0) return -1;
    int ow, oh;
    if (in->w <= box_w && in->h <= box_h) {
        int k = box_w / in->w < box_h / in->h ? box_w / in->w : box_h / in->h;
        if (k < 1) k = 1;
        ow = in->w * k;
        oh = in->h * k;
    } else {
        double s = (double)box_w / in->w < (double)box_h / in->h ? (double)box_w / in->w : (double)box_h / in->h;
        ow = (int)(in->w * s);
        oh = (int)(in->h * s);
        if (ow < 1) ow = 1;
        if (oh < 1) oh = 1;
    }
    out->px = (uint32_t *)malloc((size_t)ow * oh * sizeof(uint32_t));
    if (!out->px) return -1;
    out->w = ow;
    out->h = oh;
    for (int y = 0; y < oh; y++) {
        int y0 = (int)((long long)y * in->h / oh), y1 = (int)((long long)(y + 1) * in->h / oh);
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < ow; x++) {
            int x0 = (int)((long long)x * in->w / ow), x1 = (int)((long long)(x + 1) * in->w / ow);
            if (x1 <= x0) x1 = x0 + 1;
            unsigned long r = 0, g = 0, b = 0, a = 0, cnt = 0;
            int stepy = (y1 - y0) > 8 ? (y1 - y0) / 8 : 1, stepx = (x1 - x0) > 8 ? (x1 - x0) / 8 : 1; /* sample at most 8x8 */
            for (int sy = y0; sy < y1; sy += stepy)
                for (int sx = x0; sx < x1; sx += stepx) {
                    uint32_t c = in->px[(size_t)sy * in->w + sx];
                    unsigned ca = c >> 24;
                    a += ca;
                    r += ((c >> 16) & 255) * ca;
                    g += ((c >> 8) & 255) * ca;
                    b += (c & 255) * ca;
                    cnt++;
                }
            unsigned avg_a = (unsigned)(a / cnt);
            unsigned cr = a ? (unsigned)(r / a) : 0, cg = a ? (unsigned)(g / a) : 0, cb = a ? (unsigned)(b / a) : 0;
            unsigned check = (((x / 8) + (y / 8)) & 1) ? 0x44 : 0x22;
            cr = (cr * avg_a + check * (255 - avg_a)) / 255;
            cg = (cg * avg_a + check * (255 - avg_a)) / 255;
            cb = (cb * avg_a + check * (255 - avg_a)) / 255;
            out->px[(size_t)y * ow + x] = img_argb(255, (int)cr, (int)cg, (int)cb);
        }
    }
    return 0;
}

#endif

/* ansiart.h: text-mode art for Commander's Art page, drawn with the VGA font at its own 8x16 size, 25 rows a page: ANSI (escape
 * codes in CP437, with SAUCE's width and iCE colours), plain CP437 NFO/DIZ files, BIN (character and attribute pairs), XBin (with its
 * own palette and font, packed or not), ArtWorx ADF and iCE Draw IDF. Written for r3i6nos (MIT), in the spirit of Diskette's ANSI
 * thumbnails (which come from Coupler's ANSI parser), but drawing real glyphs.
 *
 * Art wider than 80 columns is drawn with narrower cells so a page is always the screen's width; nothing scrolls sideways. */
#ifndef COMMANDER_ANSIART_H
#define COMMANDER_ANSIART_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "r3i6nos_ansiapps.h"
#include "formats.h"

#define ART_MAX_ROWS 2000
#define ART_MAX_COLS 400

typedef struct {
    int w, h, cap_rows;
    uint8_t *ch, *at;   /* w x cap_rows: code page 437 index and attribute (fg low nibble, bg high) */
    uint32_t pal[16];   /* ARGB */
    uint8_t font[256][16];
    int own_font, ice;
} Art;

static void art_free(Art *a) {
    free(a->ch);
    free(a->at);
    memset(a, 0, sizeof *a);
}

static int art_init(Art *a, int w) {
    memset(a, 0, sizeof *a);
    if (w < 1) w = 80;
    if (w > ART_MAX_COLS) w = ART_MAX_COLS;
    a->w = w;
    for (int i = 0; i < 16; i++) a->pal[i] = 0xFF000000u | (uint32_t)r3_rgb[i][0] << 16 | (uint32_t)r3_rgb[i][1] << 8 | r3_rgb[i][2];
    memcpy(a->font, r3i6nos_vga8x16, sizeof a->font);
    return 0;
}

/* Make room for row y; 0 if it is past the limit. */
static int art_row(Art *a, int y) {
    if (y >= ART_MAX_ROWS) return 0;
    if (y >= a->cap_rows) {
        int cap = a->cap_rows ? a->cap_rows : 64;
        while (cap <= y) cap *= 2;
        if (cap > ART_MAX_ROWS) cap = ART_MAX_ROWS;
        uint8_t *c = (uint8_t *)realloc(a->ch, (size_t)cap * a->w), *t = (uint8_t *)realloc(a->at, (size_t)cap * a->w);
        if (c) a->ch = c;
        if (t) a->at = t;
        if (!c || !t) return 0;
        memset(a->ch + (size_t)a->cap_rows * a->w, ' ', (size_t)(cap - a->cap_rows) * a->w);
        memset(a->at + (size_t)a->cap_rows * a->w, 0x07, (size_t)(cap - a->cap_rows) * a->w);
        a->cap_rows = cap;
    }
    if (y + 1 > a->h) a->h = y + 1;
    return 1;
}

static void art_put(Art *a, int x, int y, int ch, int attr) {
    if (x < 0 || x >= a->w || y < 0 || !art_row(a, y)) return;
    a->ch[(size_t)y * a->w + x] = (uint8_t)ch;
    a->at[(size_t)y * a->w + x] = (uint8_t)attr;
}

/* The nearest of the 16 colours to an RGB colour. */
static int art_nearest(int r, int g, int b) {
    int best = 0;
    long bd = -1;
    for (int i = 0; i < 16; i++) {
        long dr = r - r3_rgb[i][0], dg = g - r3_rgb[i][1], db = b - r3_rgb[i][2], d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (bd < 0 || d < bd) { bd = d; best = i; }
    }
    return best;
}

static int art_xterm256(int n) {
    if (n < 16) return n;
    if (n >= 232) { int v = 8 + (n - 232) * 10; return art_nearest(v, v, v); }
    n -= 16;
    static const int lv[6] = {0, 95, 135, 175, 215, 255};
    return art_nearest(lv[n / 36], lv[n / 6 % 6], lv[n % 6]);
}

/* ANSI: CP437 bytes and escape sequences. */
static void art_ansi(Art *a, const uint8_t *b, size_t n) {
    static const int ansi2vga[8] = {0, 4, 2, 6, 1, 5, 3, 7};
    int x = 0, y = 0, fg = 7, bg = 0, bold = 0, blink = 0, rev = 0, sx = 0, sy = 0;
    art_row(a, 0);
    for (size_t i = 0; i < n; i++) {
        uint8_t c = b[i];
        if (c == 0x1A) break; /* SAUCE follows */
        if (c == 27 && i + 1 < n && b[i + 1] == '[') {
            int args[16], na = 0, cur = -1;
            size_t j = i + 2;
            int priv = j < n && (b[j] == '?' || b[j] == '=');
            if (priv) j++;
            for (; j < n; j++) {
                if (b[j] >= '0' && b[j] <= '9') { cur = (cur < 0 ? 0 : cur) * 10 + (b[j] - '0'); if (cur > 100000) cur = 100000; }
                else if (b[j] == ';') { if (na < 16) args[na++] = cur; cur = -1; }
                else break;
            }
            if (cur >= 0 || na) { if (na < 16) args[na++] = cur; }
            if (j >= n) break;
            int cmd = b[j];
            i = j;
            if (priv) continue;
            int a0 = na > 0 && args[0] >= 0 ? args[0] : -1, a1 = na > 1 && args[1] >= 0 ? args[1] : -1;
            switch (cmd) {
            case 'm':
                if (na == 0) { fg = 7; bg = 0; bold = blink = rev = 0; }
                for (int k = 0; k < na; k++) {
                    int v = args[k] < 0 ? 0 : args[k];
                    if (v == 0) { fg = 7; bg = 0; bold = blink = rev = 0; }
                    else if (v == 1) bold = 1;
                    else if (v == 5 || v == 6) blink = 1;
                    else if (v == 7) rev = 1;
                    else if (v == 22) bold = 0;
                    else if (v == 25) blink = 0;
                    else if (v == 27) rev = 0;
                    else if (v >= 30 && v <= 37) fg = ansi2vga[v - 30];
                    else if (v == 39) fg = 7;
                    else if (v >= 40 && v <= 47) bg = ansi2vga[v - 40];
                    else if (v == 49) bg = 0;
                    else if (v >= 90 && v <= 97) fg = ansi2vga[v - 90] | 8;
                    else if (v >= 100 && v <= 107) bg = ansi2vga[v - 100] | 8;
                    else if ((v == 38 || v == 48) && k + 2 < na && args[k + 1] == 5) { int c2 = art_xterm256(args[k + 2] < 0 ? 0 : args[k + 2] & 255); if (v == 38) fg = c2; else bg = c2; k += 2; }
                    else if ((v == 38 || v == 48) && k + 4 < na && args[k + 1] == 2) { int c2 = art_nearest(args[k + 2] & 255, args[k + 3] & 255, args[k + 4] & 255); if (v == 38) fg = c2; else bg = c2; k += 4; }
                }
                break;
            case 'H': case 'f': y = (a0 > 0 ? a0 : 1) - 1; x = (a1 > 0 ? a1 : 1) - 1; break;
            case 'A': y -= a0 > 0 ? a0 : 1; if (y < 0) y = 0; break;
            case 'B': y += a0 > 0 ? a0 : 1; break;
            case 'C': x += a0 > 0 ? a0 : 1; if (x >= a->w) x = a->w - 1; break;
            case 'D': x -= a0 > 0 ? a0 : 1; if (x < 0) x = 0; break;
            case 'G': x = (a0 > 0 ? a0 : 1) - 1; break;
            case 's': sx = x; sy = y; break;
            case 'u': x = sx; y = sy; break;
            case 'J': if (a0 == 2) { for (int r = 0; r < a->h; r++) for (int q = 0; q < a->w; q++) art_put(a, q, r, ' ', 7); x = y = 0; } break;
            case 'K': for (int q = x; q < a->w; q++) art_put(a, q, y, ' ', bg << 4 | fg); break;
            default: break;
            }
            continue;
        }
        if (c == '\r') { x = 0; continue; }
        if (c == '\n') { y++; x = 0; if (y >= ART_MAX_ROWS) break; continue; }
        if (c == '\t') { x = (x / 8 + 1) * 8; continue; }
        if (x >= a->w) { x = 0; y++; if (y >= ART_MAX_ROWS) break; }
        int f = fg | (bold ? 8 : 0), g = bg | (blink && a->ice ? 8 : 0);
        if (rev) { int t = f; f = g; g = t; }
        art_put(a, x, y, c, (g & 15) << 4 | (f & 15));
        x++;
    }
    if (a->h == 0) art_row(a, 0);
}

static void art_pairs(Art *a, const uint8_t *b, size_t n) {
    size_t cells = n / 2;
    for (size_t i = 0; i < cells; i++) art_put(a, (int)(i % (size_t)a->w), (int)(i / (size_t)a->w), b[i * 2], b[i * 2 + 1]);
    if (a->h == 0) art_row(a, 0);
}

static void art_palette6(Art *a, const uint8_t *p, const int *map) {
    for (int i = 0; i < 16; i++) {
        const uint8_t *c = p + (map ? map[i] : i) * 3;
        a->pal[i] = 0xFF000000u | (uint32_t)((c[0] & 63) * 255 / 63) << 16 | (uint32_t)((c[1] & 63) * 255 / 63) << 8 | (uint32_t)((c[2] & 63) * 255 / 63);
    }
}

/* Load the art in f (kind from ff_identify). 0 on success. */
static int art_load(Art *a, const FFile *f, int kind, char *err, size_t errsz) {
    uint8_t rec[128];
    int sauce = ff_sauce(f, rec);
    int w = 80;
    if (sauce && rec[94] == 1 && rd16le(rec + 96) >= 1) w = (int)rd16le(rec + 96);
    if (sauce && rec[94] == 5 && rec[95]) w = rec[95] * 2;
    if (kind == K_BINART && !(sauce && rec[94] == 5)) w = 160;
    off_t size = f->size;
    if (sauce) { size -= 128; if (rec[104]) size -= 5 + 64 * rec[104]; }
    if (size < 0) size = 0;
    size_t n;
    uint8_t *b = ff_load(f, 0, (size_t)size, &n);
    if (!b && size) { snprintf(err, errsz, "cannot read the file"); return -1; }
    if (kind == K_XBIN) {
        if (n < 11) { free(b); snprintf(err, errsz, "the XBin header is cut"); return -1; }
        int xw = (int)rd16le(b + 5), xh = (int)rd16le(b + 7), fs = b[9], fl = b[10];
        art_init(a, xw);
        a->ice = (fl & 8) != 0;
        size_t p = 11;
        if (fl & 1) { if (p + 48 <= n) art_palette6(a, b + p, NULL); p += 48; }
        if (fl & 2) {
            size_t fsz = (size_t)fs * ((fl & 16) ? 512 : 256);
            if (fs == 16 && p + 4096 <= n) { memcpy(a->font, b + p, 4096); a->own_font = 1; }
            p += fsz;
        }
        if (fl & 4) {
            int x = 0, y = 0;
            while (p < n && y < xh && y < ART_MAX_ROWS) {
                int hd = b[p++], cnt = (hd & 63) + 1, type = hd >> 6;
                int ch = 0, at = 0;
                if (type == 1 && p < n) ch = b[p++];
                if (type == 2 && p < n) at = b[p++];
                if (type == 3 && p + 1 < n) { ch = b[p]; at = b[p + 1]; p += 2; }
                for (int k = 0; k < cnt; k++) {
                    int c2 = ch, a2 = at;
                    if (type == 0) { if (p + 1 >= n) break; c2 = b[p]; a2 = b[p + 1]; p += 2; }
                    else if (type == 1) { if (p >= n) break; a2 = b[p++]; }
                    else if (type == 2) { if (p >= n) break; c2 = b[p++]; }
                    art_put(a, x, y, c2, a2);
                    if (++x >= xw) { x = 0; y++; }
                }
            }
        } else if (p < n) {
            art_pairs(a, b + p, n - p);
        }
        if (a->h == 0) art_row(a, 0);
    } else if (kind == K_ADF) {
        static const int map[16] = {0, 1, 2, 3, 4, 5, 20, 7, 56, 57, 58, 59, 60, 61, 62, 63};
        art_init(a, 80);
        a->ice = 1;
        if (n >= 1 + 192 + 4096) {
            art_palette6(a, b + 1, map);
            memcpy(a->font, b + 193, 4096);
            a->own_font = 1;
            art_pairs(a, b + 193 + 4096, n - 193 - 4096);
        }
    } else if (kind == K_IDF) {
        int x2 = n >= 12 ? (int)rd16le(b + 8) : 79;
        art_init(a, x2 + 1);
        a->ice = 1;
        size_t end = n >= 4096 + 48 + 12 ? n - 4096 - 48 : n;
        if (n >= 4096 + 48 + 12) { memcpy(a->font, b + end, 4096); a->own_font = 1; art_palette6(a, b + end + 4096, NULL); }
        size_t i = 12, cell = 0;
        while (i + 1 < end) {
            if (b[i] == 1 && b[i + 1] == 0 && i + 5 < end) {
                unsigned cnt = rd16le(b + i + 2);
                for (unsigned k = 0; k < cnt && cell / (size_t)a->w < ART_MAX_ROWS; k++, cell++) art_put(a, (int)(cell % (size_t)a->w), (int)(cell / (size_t)a->w), b[i + 4], b[i + 5]);
                i += 6;
            } else {
                art_put(a, (int)(cell % (size_t)a->w), (int)(cell / (size_t)a->w), b[i], b[i + 1]);
                cell++;
                i += 2;
            }
            if (cell / (size_t)a->w >= ART_MAX_ROWS) break;
        }
        if (a->h == 0) art_row(a, 0);
    } else if (kind == K_BINART) {
        art_init(a, w);
        a->ice = sauce ? (rec[105] & 1) : 1;
        art_pairs(a, b, n);
    } else {
        art_init(a, w);
        a->ice = sauce && (rec[105] & 1);
        art_ansi(a, b, n);
    }
    free(b);
    if (!a->ch) { art_free(a); snprintf(err, errsz, "out of memory"); return -1; }
    return 0;
}

/* Cell size for the art's width: 8x16 up to 80 columns (16x32 up to 40), narrower beyond; the page is 640 x `height` pixels. */
static void art_cell(const Art *a, int *cw, int *chh) {
    int c = 640 / (a->w > 0 ? a->w : 80);
    if (c > 16) c = 16;
    if (c > 8 && c < 16) c = 8;
    if (c < 2) c = 2;
    *cw = c;
    *chh = c * 2;
}

static int art_rows_per_page(const Art *a, int height) {
    int cw, chh;
    art_cell(a, &cw, &chh);
    return height / chh > 0 ? height / chh : 1;
}

static int art_pages(const Art *a, int height) {
    int r = art_rows_per_page(a, height);
    return (a->h + r - 1) / r > 0 ? (a->h + r - 1) / r : 1;
}

/* Draw one page into px (stride 640), in the box at (x0, y0) of 640 x height. */
static void art_draw(const Art *a, int page, uint32_t *px, int y0, int height) {
    int cw, chh;
    art_cell(a, &cw, &chh);
    int rows = art_rows_per_page(a, height), first = page * rows;
    int left = (640 - a->w * cw) / 2;
    if (left < 0) left = 0;
    for (int y = 0; y < height; y++) {
        uint32_t *line = px + (size_t)(y0 + y) * 640;
        for (int x = 0; x < 640; x++) line[x] = 0xFF000000u;
    }
    for (int r = 0; r < rows && first + r < a->h; r++)
        for (int c = 0; c < a->w; c++) {
            size_t i = (size_t)(first + r) * a->w + c;
            int ch = a->ch[i], at = a->at[i];
            uint32_t fg = a->pal[at & 15], bg = a->pal[a->ice ? at >> 4 : (at >> 4) & 7];
            for (int y = 0; y < chh; y++) {
                int py = y0 + r * chh + y;
                if (py >= y0 + height) break;
                uint8_t bits = a->font[ch][y * 16 / chh];
                uint32_t *line = px + (size_t)py * 640 + left + c * cw;
                for (int x = 0; x < cw && left + c * cw + x < 640; x++) line[x] = (bits >> (7 - x * 8 / cw)) & 1 ? fg : bg;
            }
        }
}

#endif

/* r3i6nos-ansiapps for C and SDL2: the text-mode theme every r3i6nos app starts from
 * (docs/R3I6NOS-ANSIAPPS-THEME.md). The C twin of theme/r3i6nos_ansiapps.lua: the same grid, colours, widgets and
 * button map, so a native app looks and plays like the menu.
 *
 * 640x480 = 64 columns x 24 rows of 10x20 cells (the IBM VGA 8x16 font scaled up). Row 0 is the reserved top bar,
 * rows 1-20 the display view, rows 21-23 the three-row command line (reply, prompt, status). Nothing scrolls.
 *
 * Single header, static functions. Include after defining nothing; compile with -I theme and SDL2's flags.
 * The font data (theme/r3i6nos_vga8x16.h) is CC BY-SA 4.0: see theme/font/README.txt and NOTICE. */
#ifndef R3I6NOS_ANSIAPPS_H
#define R3I6NOS_ANSIAPPS_H

#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r3i6nos_vga8x16.h"

enum {
    R3_COLS = 64, R3_ROWS = 24, R3_CELL_W = 10, R3_CELL_H = 20, R3_W = 640, R3_H = 480,
    R3_TOP_ROW = 0, R3_DISPLAY_TOP = 1, R3_DISPLAY_ROWS = 20, R3_COMMAND_TOP = 21, R3_COMMAND_ROWS = 3
};

/* The 16 VGA text-mode colours. */
enum {
    R3_BLACK, R3_BLUE, R3_GREEN, R3_CYAN, R3_RED, R3_MAGENTA, R3_BROWN, R3_LIGHT_GRAY,
    R3_DARK_GRAY, R3_LIGHT_BLUE, R3_LIGHT_GREEN, R3_LIGHT_CYAN, R3_LIGHT_RED, R3_LIGHT_MAGENTA, R3_YELLOW, R3_WHITE
};
static const unsigned char r3_rgb[16][3] = {
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0xAA}, {0x00, 0xAA, 0x00}, {0x00, 0xAA, 0xAA},
    {0xAA, 0x00, 0x00}, {0xAA, 0x00, 0xAA}, {0xAA, 0x55, 0x00}, {0xAA, 0xAA, 0xAA},
    {0x55, 0x55, 0x55}, {0x55, 0x55, 0xFF}, {0x55, 0xFF, 0x55}, {0x55, 0xFF, 0xFF},
    {0xFF, 0x55, 0x55}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0x55}, {0xFF, 0xFF, 0xFF},
};

/* The pairs the theme itself uses; every one passes 4.5:1 (r3_contrast). */
enum { R3_ST_BAR, R3_ST_BODY, R3_ST_STRONG, R3_ST_SELECT, R3_ST_FRAME, R3_ST_HINT, R3_ST_OK, R3_ST_WARN, R3_ST_ERROR,
       R3_ST_DIM, R3_ST_COUNT };
static const unsigned char r3_styles[R3_ST_COUNT][2] = {
    {R3_WHITE, R3_BLUE},            /* bar: the top bar */
    {R3_LIGHT_GRAY, R3_BLACK},      /* body: ordinary text */
    {R3_WHITE, R3_BLACK},           /* strong: titles, the line typed */
    {R3_BLACK, R3_CYAN},            /* select: the highlighted row */
    {R3_LIGHT_GRAY, R3_BLACK},      /* frame: box lines */
    {R3_LIGHT_CYAN, R3_BLACK},      /* hint: keys, tips, the reply */
    {R3_LIGHT_GREEN, R3_BLACK},     /* ok: the prompt character, good news */
    {R3_YELLOW, R3_BLACK},          /* warn: caution */
    {R3_LIGHT_RED, R3_BLACK},       /* error: a mistake (always also in words) */
    {R3_BLACK, R3_LIGHT_GRAY},      /* dim: the selected row of a panel that is not active */
};

/* WCAG contrast ratio of two palette colours. */
static inline double r3_contrast(int fg, int bg) {
    double lum[2];
    int c[2] = {fg, bg};
    for (int k = 0; k < 2; k++) {
        double sum = 0, w[3] = {0.2126, 0.7152, 0.0722};
        for (int i = 0; i < 3; i++) {
            double v = r3_rgb[c[k]][i] / 255.0;
            sum += w[i] * (v <= 0.03928 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4));
        }
        lum[k] = sum;
    }
    double a = lum[0] > lum[1] ? lum[0] : lum[1], b = lum[0] > lum[1] ? lum[1] : lum[0];
    return (a + 0.05) / (b + 0.05);
}

/* ---- code page 437 --------------------------------------------------------------------------------------------- */

/* One Unicode code point as a code page 437 index; controls are spaces, unknown is '?'. */
static inline int r3_cp437_of(unsigned cp) {
    if (cp < 0x20 || cp == 0x7F) return 32;
    if (cp < 0x7F) return (int)cp;
    if (cp == 0x3B2) return 225;
    if (cp == 0x3BC) return 230;
    for (int i = 1; i < 256; i++)
        if (r3i6nos_cp437[i] == cp) return i;
    return '?';
}

/* The next code point of UTF-8 at *p (never past end); a bad or cut sequence is '?' and consumes what it can. */
static inline unsigned r3_utf8_next(const unsigned char **p, const unsigned char *end) {
    const unsigned char *s = *p;
    unsigned b = *s++, cp = b;
    if (b >= 0xC0) {
        int len = b < 0xE0 ? 2 : (b < 0xF0 ? 3 : 4);
        cp = b - (len == 2 ? 0xC0 : (len == 3 ? 0xE0 : 0xF0));
        for (int k = 1; k < len; k++) {
            if (s >= end || *s < 0x80 || *s > 0xBF) { cp = '?'; break; }
            cp = cp * 64 + (*s++ - 0x80u);
        }
    } else if (b >= 0x80) {
        cp = '?';
    }
    *p = s;
    return cp;
}

/* A UTF-8 string as code page 437 indices (at most max). Returns the count. */
static inline int r3_decode(const char *s, unsigned char *out, int max) {
    const unsigned char *p = (const unsigned char *)s, *end = p + strlen(s);
    int n = 0;
    while (p < end && n < max) out[n++] = (unsigned char)r3_cp437_of(r3_utf8_next(&p, end));
    return n;
}

/* ---- the font: 8x16 scaled to 10x20 ------------------------------------------------------------------------------ */

/* Nearest neighbour: dest column x takes source column x*8/10, dest row y takes y*16/20 (integer division). */
static inline int r3_glyph_bit(int ch, int x, int y) {
    return (r3i6nos_vga8x16[ch][(y * 16) / R3_CELL_H] >> (7 - (x * 8) / R3_CELL_W)) & 1;
}

/* FNV-1a over every pixel (0 or 1) of every 10x20 glyph: the Lua twin computes the same number, so the two
 * implementations cannot drift apart unnoticed. */
static inline uint32_t r3_glyph_hash(void) {
    uint32_t h = 2166136261u;
    for (int ch = 0; ch < 256; ch++)
        for (int y = 0; y < R3_CELL_H; y++)
            for (int x = 0; x < R3_CELL_W; x++) {
                h ^= (uint32_t)r3_glyph_bit(ch, x, y);
                h *= 16777619u;
            }
    return h;
}
#define R3_GLYPH_HASH 0x71e726c8u /* what theme.glyphHash() gives in Lua (tests/test_theme.lua) */

/* ---- the screen: 64 x 24 cells ----------------------------------------------------------------------------------- */

typedef struct {
    unsigned char ch[R3_COLS * R3_ROWS], fg[R3_COLS * R3_ROWS], bg[R3_COLS * R3_ROWS];
} R3Screen;

static inline void r3_clear(R3Screen *s) {
    memset(s->ch, 32, sizeof s->ch);
    memset(s->fg, R3_LIGHT_GRAY, sizeof s->fg);
    memset(s->bg, R3_BLACK, sizeof s->bg);
}

/* One cell; bg < 0 keeps the cell's background. Off-screen is ignored. */
static inline void r3_put(R3Screen *s, int col, int row, int ch, int fg, int bg) {
    if (col < 0 || col >= R3_COLS || row < 0 || row >= R3_ROWS) return;
    int i = row * R3_COLS + col;
    s->ch[i] = (unsigned char)ch;
    s->fg[i] = (unsigned char)fg;
    if (bg >= 0) s->bg[i] = (unsigned char)bg;
}

static inline void r3_fill(R3Screen *s, int col, int row, int w, int h, int ch, int fg, int bg) {
    for (int r = row; r < row + h; r++)
        for (int c = col; c < col + w; c++) r3_put(s, c, r, ch, fg, bg);
}

/* Code page 437 indices from col, clipped at the right edge. Returns the next column. */
static inline int r3_put_cells(R3Screen *s, int col, int row, const unsigned char *g, int n, int fg, int bg) {
    for (int i = 0; i < n && col < R3_COLS; i++, col++) r3_put(s, col, row, g[i], fg, bg);
    return col;
}

/* A UTF-8 string from col, clipped at the right edge. Returns the next column. */
static inline int r3_text(R3Screen *s, int col, int row, const char *str, int fg, int bg) {
    unsigned char g[256];
    int n = r3_decode(str, g, (int)sizeof g);
    return r3_put_cells(s, col, row, g, n, fg, bg);
}

/* The cells a string takes (at most 255). */
static inline int r3_width(const char *str) {
    unsigned char g[256];
    return r3_decode(str, g, (int)sizeof g);
}

/* A string cut to at most max cells, ending in » (175) when it was cut. Returns the count written to out. */
static inline int r3_fit(const char *str, int max, unsigned char *out) {
    unsigned char g[256];
    int n = r3_decode(str, g, (int)sizeof g);
    if (max < 0) max = 0;
    if (n <= max) { memcpy(out, g, (size_t)n); return n; }
    if (max == 0) return 0;
    memcpy(out, g, (size_t)max - 1);
    out[max - 1] = 175;
    return max;
}

/* A string cut at its START to at most max cells, beginning with « (174): the tail of a path. */
static inline int r3_fit_tail(const char *str, int max, unsigned char *out) {
    unsigned char g[256];
    int n = r3_decode(str, g, (int)sizeof g);
    if (max < 0) max = 0;
    if (n <= max) { memcpy(out, g, (size_t)n); return n; }
    if (max == 0) return 0;
    out[0] = 174;
    memcpy(out + 1, g + (n - (max - 1)), (size_t)max - 1);
    return max;
}

static inline int r3_text_fit(R3Screen *s, int col, int row, const char *str, int max, int fg, int bg) {
    unsigned char g[256];
    int n = r3_fit(str, max, g);
    return r3_put_cells(s, col, row, g, n, fg, bg);
}

/* Text whose last cell is at col. */
static inline void r3_text_right(R3Screen *s, int col, int row, const char *str, int fg, int bg) {
    unsigned char g[256];
    int n = r3_decode(str, g, (int)sizeof g);
    r3_put_cells(s, col - n + 1, row, g, n, fg, bg);
}

typedef struct {
    int dbl;                /* 1: double lines (the default for dialogs and the active panel), 0: single */
    const char *title;      /* set into the top edge */
    const char *right;      /* also in the top edge, at the right ("Page 1/2") */
    const char *foot;       /* in the bottom edge */
    int bg;                 /* inside colour, default black */
} R3Box;

/* A frame with its inside cleared. */
static inline void r3_box(R3Screen *s, int col, int row, int w, int h, const R3Box *o) {
    static const unsigned char dbl[6] = {201, 205, 187, 186, 200, 188}, sgl[6] = {218, 196, 191, 179, 192, 217};
    const unsigned char *L = o->dbl ? dbl : sgl;
    int fg = r3_styles[R3_ST_FRAME][0], bg = o->bg ? o->bg : r3_styles[R3_ST_FRAME][1];
    r3_fill(s, col, row, w, h, 32, fg, bg);
    for (int c = col + 1; c < col + w - 1; c++) { r3_put(s, c, row, L[1], fg, bg); r3_put(s, c, row + h - 1, L[1], fg, bg); }
    for (int r = row + 1; r < row + h - 1; r++) { r3_put(s, col, r, L[3], fg, bg); r3_put(s, col + w - 1, r, L[3], fg, bg); }
    r3_put(s, col, row, L[0], fg, bg); r3_put(s, col + w - 1, row, L[2], fg, bg);
    r3_put(s, col, row + h - 1, L[4], fg, bg); r3_put(s, col + w - 1, row + h - 1, L[5], fg, bg);
    int room = w - 6, strong = r3_styles[R3_ST_STRONG][0], hint = r3_styles[R3_ST_HINT][0];
    unsigned char g[256];
    int rn = o->right ? r3_width(o->right) : 0;
    if (o->title && room > 2) {
        int n = r3_fit(o->title, room - 2 - (rn ? rn + 1 : 0), g); /* never runs into the right-hand text */
        r3_put(s, col + 2, row, 32, fg, bg);
        r3_put_cells(s, col + 3, row, g, n, strong, bg);
        r3_put(s, col + 3 + n, row, 32, fg, bg);
    }
    if (o->right && room > 2) {
        int n = r3_fit(o->right, room - 2, g);
        r3_put(s, col + w - 3 - n, row, 32, fg, bg);
        r3_put_cells(s, col + w - 2 - n, row, g, n, strong, bg);
        r3_put(s, col + w - 2, row, 32, fg, bg);
    }
    if (o->foot && room > 2) {
        int n = r3_fit(o->foot, w - 6, g);
        r3_put(s, col + 2, row + h - 1, 32, fg, bg);
        r3_put_cells(s, col + 3, row + h - 1, g, n, hint, bg);
        r3_put(s, col + 3 + n, row + h - 1, 32, fg, bg);
    }
}

/* The screen as 24 lines of text for tests and logs: code page 437 above 126 shows as '?'. out: 24 x 65 bytes. */
static inline void r3_dump(const R3Screen *s, char out[R3_ROWS][R3_COLS + 1]) {
    for (int r = 0; r < R3_ROWS; r++) {
        for (int c = 0; c < R3_COLS; c++) {
            int ch = s->ch[r * R3_COLS + c];
            out[r][c] = (ch >= 32 && ch < 127) ? (char)ch : (ch == 0 ? ' ' : '?');
        }
        out[r][R3_COLS] = 0;
    }
}

/* ---- the three windows ------------------------------------------------------------------------------------------- */

#define R3_ST(id) r3_styles[id][0], r3_styles[id][1]

/* Row 0: white on blue. left from column 1; right ends at column 62. */
static inline void r3_top_bar(R3Screen *s, const char *left, const char *right) {
    r3_fill(s, 0, R3_TOP_ROW, R3_COLS, 1, 32, R3_ST(R3_ST_BAR));
    if (left) r3_text_fit(s, 1, R3_TOP_ROW, left, R3_COLS - 2, R3_ST(R3_ST_BAR));
    if (right) {
        unsigned char g[256];
        int n = r3_fit(right, R3_COLS - 2, g);
        r3_put_cells(s, R3_COLS - 1 - n, R3_TOP_ROW, g, n, R3_ST(R3_ST_BAR));
    }
}

typedef struct {
    const char *reply;      /* row 21; reply_style is an R3_ST_* (HINT, OK, WARN or ERROR) */
    int reply_style;
    char mode;              /* the mode character on row 22 (default '>') */
    const char *line;       /* the line after it */
    int caret;              /* 1: a block caret after the line */
    const char *left, *right; /* row 23, the status */
    int right_style;        /* default HINT */
} R3Cmd;

/* Rows 21-23 as KlamANSI's command line. */
static inline void r3_command_line(R3Screen *s, const R3Cmd *o) {
    int top = R3_COMMAND_TOP;
    r3_fill(s, 0, top, R3_COLS, R3_COMMAND_ROWS, 32, R3_ST(R3_ST_BODY));
    if (o->reply) r3_text_fit(s, 1, top, o->reply, R3_COLS - 2, R3_ST(o->reply_style ? o->reply_style : R3_ST_HINT));
    r3_put(s, 0, top + 1, o->mode ? o->mode : '>', R3_ST(R3_ST_OK));
    int x = r3_text_fit(s, 2, top + 1, o->line ? o->line : "", R3_COLS - 4, R3_ST(R3_ST_STRONG));
    if (o->caret) r3_put(s, x, top + 1, 219, R3_ST(R3_ST_STRONG));
    int rn = o->right ? r3_width(o->right) : 0;
    if (o->left) r3_text_fit(s, 1, top + 2, o->left, R3_COLS - 3 - rn, R3_ST(R3_ST_BODY));
    if (o->right) {
        unsigned char g[256];
        int n = r3_fit(o->right, R3_COLS - 2, g);
        r3_put_cells(s, R3_COLS - 1 - n, top + 2, g, n, R3_ST(o->right_style ? o->right_style : R3_ST_HINT));
    }
}

/* A progress bar for the status row: cells of blocks and light shade, as KlamANSI's LAPSE bar. */
static inline void r3_progress(R3Screen *s, int col, int row, int width, int percent) {
    int full = width * (percent < 0 ? 0 : (percent > 100 ? 100 : percent)) / 100;
    for (int i = 0; i < width; i++) r3_put(s, col + i, row, i < full ? 219 : 176, R3_ST(R3_ST_HINT));
}

/* ---- list dialogs ---------------------------------------------------------------------------------------------- */

/* value holds 63 bytes: a pad GUID (32) and longer readings fit whole, so a report copied from items is not cut (2026-11-09); the
 * list shows a long value cut to the row, leaving the label at least 12 cells. */
typedef struct { char label[192]; char value[64]; } R3Item;

typedef struct {
    int col, row, w, h;
    const char *title, *foot;
    const R3Item *items;
    int n, sel;                 /* sel is 0-based here */
    const char *empty[6];       /* lines shown when there are no items, NULL-terminated */
} R3List;

static inline int r3_list_rows(int h) { return h - 2; }

/* Whole pages: page and pages (1-based) for n items, per page, the selected item (0-based). */
static inline void r3_list_window(int n, int sel, int per, int *page, int *pages) {
    int p = n > 0 ? (n + per - 1) / per : 1;
    int g = (sel < 0 ? 0 : sel) / per + 1;
    *pages = p;
    *page = g > p ? p : g;
}

/* The standard list dialog: a double-line box, title and Page x/y in the top edge, one row an item (label at the
 * left, value at the right), the selected row black on cyan, a foot of keys in the bottom edge. */
static inline void r3_list_dialog(R3Screen *s, const R3List *o) {
    int per = r3_list_rows(o->h), page, pages;
    r3_list_window(o->n, o->sel, per, &page, &pages);
    char right[32];
    snprintf(right, sizeof right, "Page %d/%d", page, pages);
    R3Box b = {1, o->title, pages > 1 ? right : NULL, o->foot, 0}; /* "Page x/y" only when there is more than one page */
    r3_box(s, o->col, o->row, o->w, o->h, &b);
    if (o->n == 0) {
        for (int i = 0; i < 6 && o->empty[i]; i++) r3_text_fit(s, o->col + 2, o->row + 1 + i, o->empty[i], o->w - 4, R3_ST(R3_ST_BODY));
        return;
    }
    int first = (page - 1) * per;
    for (int i = first; i < o->n && i < first + per; i++) {
        int y = o->row + 1 + (i - first), st = i == o->sel ? R3_ST_SELECT : R3_ST_BODY;
        r3_fill(s, o->col + 1, y, o->w - 2, 1, 32, R3_ST(st));
        int vn = r3_width(o->items[i].value), room = o->w - 4 - 12 - 1; /* the value's widest: the label keeps 12 cells and a gap */
        if (room < 1) room = 1;
        if (vn > room) vn = room;
        r3_text_fit(s, o->col + 2, y, o->items[i].label, o->w - 5 - vn - (vn > 0), R3_ST(st));
        if (vn > 0) r3_text_fit(s, o->col + o->w - 2 - vn, y, o->items[i].value, vn, R3_ST(st));
    }
}

/* ---- input: the R36S's buttons and keys as actions ---------------------------------------------------------------- */

enum {
    R3_NONE, R3_UP, R3_DOWN, R3_LEFT, R3_RIGHT, R3_PAGEUP, R3_PAGEDOWN, R3_HOME, R3_END, R3_ACCEPT, R3_BACK, R3_MORE,
    R3_X, R3_Y, R3_SELECT
};

/* An SDL joystick button (0-based; LÖVE's number minus one) as an action. Raw buttons, not SDL's gamepad mapping, which
 * does not match this pad (docs/hardware-findings.md): B 0, A 1, X 2, Y 3, L1 4, R1 5, L2 6, R2 7, D-pad up 8, down 9,
 * left 10, right 11, Select 12, Start 13. */
static inline int r3_pad_action(int b) {
    switch (b) {
    case 8: return R3_UP;
    case 9: return R3_DOWN;
    case 10: return R3_LEFT;
    case 11: return R3_RIGHT;
    case 1: return R3_ACCEPT;
    case 0: return R3_BACK;
    case 4: return R3_PAGEUP;
    case 5: return R3_PAGEDOWN;
    case 6: return R3_HOME;
    case 7: return R3_END;
    case 13: return R3_MORE;
    case 2: return R3_X;
    case 3: return R3_Y;
    case 12: return R3_SELECT;
    default: return R3_NONE;
    }
}

/* A keyboard key as an action (for laptops and USB keyboards). */
static inline int r3_key_action(SDL_Keycode k) {
    switch (k) {
    case SDLK_UP: return R3_UP;
    case SDLK_DOWN: return R3_DOWN;
    case SDLK_LEFT: return R3_LEFT;
    case SDLK_RIGHT: return R3_RIGHT;
    case SDLK_PAGEUP: return R3_PAGEUP;
    case SDLK_PAGEDOWN: return R3_PAGEDOWN;
    case SDLK_HOME: return R3_HOME;
    case SDLK_END: return R3_END;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_z: return R3_ACCEPT;
    case SDLK_ESCAPE: case SDLK_x: return R3_BACK;
    case SDLK_TAB: case SDLK_F1: case SDLK_s: return R3_MORE;
    default: return R3_NONE;
    }
}

/* The list rule on a 0-based selection: moves it for up, down, left/pageup, right/pagedown, home and end (up and down wrap)
 * and returns the new selection. Accept, back and more are the caller's. */
static inline int r3_list_key(int sel, int n, int per, int action) {
    if (n <= 0) return 0;
    switch (action) {
    case R3_UP: return (sel + n - 1) % n;
    case R3_DOWN: return (sel + 1) % n;
    case R3_PAGEUP: case R3_LEFT: return sel - per < 0 ? 0 : sel - per;
    case R3_PAGEDOWN: case R3_RIGHT: return sel + per > n - 1 ? n - 1 : sel + per;
    case R3_HOME: return 0;
    case R3_END: return n - 1;
    default: return sel;
    }
}

/* ---- drawing to SDL ------------------------------------------------------------------------------------------------ */

typedef struct {
    SDL_Texture *tex;
    uint32_t *px;               /* R3_W x R3_H, ARGB8888 */
    R3Screen prev;
    int have_prev;
    int settle;                 /* presents still owed for the last change (r3_video_draw) */
} R3Video;

/* How many times a changed screen is presented. Once is not enough on the R36S (KMSDRM with the Mali blob): an app that presented
 * its first screen once and then waited for input stayed black, while LÖVE and apps that present every frame drew. Presenting the
 * same picture a few more times fills every buffer of the swap chain, so whichever one the display shows holds it. */
#define R3_VIDEO_SETTLE 4

/* Whether this call presents: always for a change (which owes R3_VIDEO_SETTLE presents in all), then once per call until the debt
 * is paid; then nothing until the next change, so an idle app does not keep the GPU busy. Pure, for tests. */
static inline int r3_video_due(R3Video *v, int changed) {
    if (changed) v->settle = R3_VIDEO_SETTLE;
    if (v->settle <= 0) return 0;
    v->settle--;
    return 1;
}

/* Paint every cell into 640x480 ARGB pixels. */
static inline void r3_render(const R3Screen *s, uint32_t *px) {
    uint32_t pal[16];
    for (int i = 0; i < 16; i++) pal[i] = 0xFF000000u | ((uint32_t)r3_rgb[i][0] << 16) | ((uint32_t)r3_rgb[i][1] << 8) | r3_rgb[i][2];
    for (int row = 0; row < R3_ROWS; row++)
        for (int col = 0; col < R3_COLS; col++) {
            int i = row * R3_COLS + col, ch = s->ch[i];
            uint32_t fg = pal[s->fg[i] & 15], bg = pal[s->bg[i] & 15];
            for (int y = 0; y < R3_CELL_H; y++) {
                uint32_t *line = px + (size_t)(row * R3_CELL_H + y) * R3_W + (size_t)col * R3_CELL_W;
                for (int x = 0; x < R3_CELL_W; x++) line[x] = r3_glyph_bit(ch, x, y) ? fg : bg;
            }
        }
}

/* Make the texture; the renderer scales the 640x480 screen by a whole number, centred, with no smoothing. */
static inline int r3_video_init(R3Video *v, SDL_Renderer *r) {
    memset(v, 0, sizeof *v);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_RenderSetLogicalSize(r, R3_W, R3_H);
    SDL_RenderSetIntegerScale(r, SDL_TRUE);
    v->tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, R3_W, R3_H);
    v->px = (uint32_t *)malloc((size_t)R3_W * R3_H * sizeof(uint32_t));
    return v->tex && v->px;
}

/* Draw the screen; the pixels are rebuilt only when a cell changed (or force is set, after an expose). Call it every pass of the main
 * loop, not only after input: a change is presented over the next R3_VIDEO_SETTLE calls (keep the loop short while v->settle > 0). */
static inline void r3_video_draw(R3Video *v, SDL_Renderer *r, const R3Screen *s, int force) {
    int changed = force || !v->have_prev || memcmp(&v->prev, s, sizeof *s) != 0;
    if (changed) {
        r3_render(s, v->px);
        SDL_UpdateTexture(v->tex, NULL, v->px, R3_W * (int)sizeof(uint32_t));
        v->prev = *s;
        v->have_prev = 1;
    }
    if (r3_video_due(v, changed)) {
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        SDL_RenderCopy(r, v->tex, NULL, NULL);
        SDL_RenderPresent(r);
    }
}

static inline void r3_video_free(R3Video *v) {
    if (v->tex) SDL_DestroyTexture(v->tex);
    free(v->px);
    memset(v, 0, sizeof *v);
}

#endif /* R3I6NOS_ANSIAPPS_H */

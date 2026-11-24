/* Font Editor: the font model, the canvas and its tools, the undo history and the files (docs/FONT-EDITOR.md). Pure C, no SDL, so the self-test
 * can drive all of it. The one font it ships with is R3i6nos Mono (the compiled tables of theme/font/r3i6nos-mono/); any monospace cell-shaped BDF
 * up to 16 x 20 opens as well (--font FILE.bdf).
 *
 * Everything is kept a code point at a time: a cell is the picture of one code point, and a NAME (R3i6nos Mono's 884 named glyphs) is a group of
 * one, two or more cells, edited together on one canvas, so a two-cell icon is drawn across its seam and not as two separate tiles. The pictures
 * the compiled tables share between code points (blank staff cells, the Greek capitals that are Latin shapes) are not shared here: an edit
 * changes the one code point, and the font builder (scripts/r3font/edits.py) lets the code points that alias it follow it.
 * Licence of what it edits: CC BY-SA 4.0 (theme/font/r3i6nos-mono/README.txt). The editor itself is MIT, like theme/. */
#ifndef FE_MODEL_H
#define FE_MODEL_H

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "font/r3i6nos-mono/r3i6nos_mono10x20.h"
#include "font/r3i6nos-mono/r3i6nos_mono_names.h"

#define FE_MAXW 16              /* the widest cell: 16 pixels */
#define FE_MAXH 20              /* the tallest: the display view's 20 rows */
#define FE_MAXCELLS 4           /* cells in one group (R3i6nos Mono's longest has 2) */
#define FE_CANVAS_W (FE_MAXCELLS * FE_MAXW)
#define FE_UNDO 256
#define FE_BUILTIN_ID "r3i6nos-mono"
#define FE_EDITS_FONT_LINE "font R3i6nos Mono 10x20"

typedef uint16_t FeRow;         /* bit w-1 is the leftmost pixel */

typedef struct {
    char name[80];              /* "icon.app.hydrolik", or "U+0041" for a code point with no name */
    int n;                      /* cells */
    int ci[FE_MAXCELLS];        /* indices into the font's code points */
} FeGroup;

typedef struct {
    char id[64], title[96];     /* "r3i6nos-mono", "R3i6nos Mono 10x20" */
    int w, h, base;             /* the cell; base is the number of rows above the baseline */
    int n;                      /* code points */
    uint32_t *cp;               /* sorted */
    FeRow (*rows)[FE_MAXH], (*orig)[FE_MAXH];
    int *group_of;              /* per code point: the first named group it is in, or -1 */
    FeGroup *groups;            /* the named groups, sorted by name */
    int ng;
    int builtin;                /* 1: R3i6nos Mono (edits go to an edits file), 0: a BDF (saved as a BDF) */
    char path[512];             /* the BDF read */
    char *bdf_head;             /* a BDF's header up to CHARS, written back as it was */
    int clipped;                /* BDF pixels that fell outside the cell when it was read */
    unsigned gen;               /* bumped by every change, so a screen knows to paint again */
} FeFont;

/* ---- small helpers ----------------------------------------------------------------------------------------------------------- */

static inline unsigned fe_utf8_next(const unsigned char **p, const unsigned char *end) {
    const unsigned char *s = *p;
    unsigned b = *s++, cp = b;
    if (b >= 0xC0) {
        int len = b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
        cp = b - (len == 2 ? 0xC0u : len == 3 ? 0xE0u : 0xF0u);
        for (int k = 1; k < len && s < end; k++) cp = cp * 64 + (*s++ & 0x3Fu);
    }
    *p = s;
    return cp;
}

static inline int fe_find(const FeFont *f, uint32_t cp) {
    int lo = 0, hi = f->n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (f->cp[mid] == cp) return mid;
        if (f->cp[mid] < cp) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

static inline void fe_free(FeFont *f) {
    free(f->cp); free(f->rows); free(f->orig); free(f->group_of); free(f->groups); free(f->bdf_head);
    memset(f, 0, sizeof *f);
}

static inline int fe_alloc(FeFont *f, int n) {
    f->n = n;
    f->cp = calloc((size_t)n, sizeof *f->cp);
    f->rows = calloc((size_t)n, sizeof *f->rows);
    f->orig = calloc((size_t)n, sizeof *f->orig);
    f->group_of = malloc((size_t)n * sizeof *f->group_of);
    if (!f->cp || !f->rows || !f->orig || !f->group_of) return -1;
    for (int i = 0; i < n; i++) f->group_of[i] = -1;
    return 0;
}

static inline int fe_cmp_group(const void *a, const void *b) { return strcmp(((const FeGroup *)a)->name, ((const FeGroup *)b)->name); }

/* ---- R3i6nos Mono, from the compiled tables ------------------------------------------------------------------------------------- */

static inline int fe_load_builtin(FeFont *f) {
    memset(f, 0, sizeof *f);
    if (fe_alloc(f, R3MONO_COUNT) != 0) return -1;
    f->builtin = 1;
    f->w = R3MONO_W; f->h = R3MONO_H; f->base = R3MONO_BASE;
    snprintf(f->id, sizeof f->id, "%s", FE_BUILTIN_ID);
    snprintf(f->title, sizeof f->title, "R3i6nos Mono %dx%d", f->w, f->h);
    for (int i = 0; i < f->n; i++) {
        f->cp[i] = r3mono_cp[i];
        for (int y = 0; y < FE_MAXH && y < R3MONO_H; y++) f->rows[i][y] = f->orig[i][y] = r3mono_glyph[r3mono_idx[i]][y];
    }
    size_t nn = sizeof r3mono_names / sizeof r3mono_names[0];
    f->groups = calloc(nn, sizeof *f->groups);
    if (!f->groups) return -1;
    for (size_t k = 0; k < nn; k++) {
        const unsigned char *p = (const unsigned char *)r3mono_names[k].utf8, *end = p + strlen(r3mono_names[k].utf8);
        FeGroup g;
        memset(&g, 0, sizeof g);
        snprintf(g.name, sizeof g.name, "%s", r3mono_names[k].name);
        int ok = 1;
        while (p < end && g.n < FE_MAXCELLS) {
            int ci = fe_find(f, fe_utf8_next(&p, end));
            if (ci < 0) { ok = 0; break; }
            g.ci[g.n++] = ci;
        }
        if (ok && g.n > 0 && p >= end) f->groups[f->ng++] = g;
    }
    qsort(f->groups, (size_t)f->ng, sizeof *f->groups, fe_cmp_group);
    for (int gi = 0; gi < f->ng; gi++)
        for (int k = 0; k < f->groups[gi].n; k++)
            if (f->group_of[f->groups[gi].ci[k]] < 0) f->group_of[f->groups[gi].ci[k]] = gi;
    return 0;
}

/* A code point on its own as a group (a named cell opens its whole name instead: fe_group_for). */
static inline void fe_group_cp(const FeFont *f, int ci, FeGroup *g) {
    memset(g, 0, sizeof *g);
    g->n = 1;
    g->ci[0] = ci;
    snprintf(g->name, sizeof g->name, "U+%04X", (unsigned)f->cp[ci]);
}

/* The group a code point belongs to for editing: its name's cells when it has a name, else itself. */
static inline void fe_group_for(const FeFont *f, int ci, FeGroup *g) {
    if (f->group_of[ci] >= 0) *g = f->groups[f->group_of[ci]];
    else fe_group_cp(f, ci, g);
}

/* Which cell of its name a code point is, 1-based (0 when it has none), and how many cells the name has. */
static inline int fe_part(const FeFont *f, int ci, int *of) {
    int gi = f->group_of[ci];
    if (gi < 0) { if (of) *of = 1; return 0; }
    for (int k = 0; k < f->groups[gi].n; k++)
        if (f->groups[gi].ci[k] == ci) { if (of) *of = f->groups[gi].n; return k + 1; }
    return 0;
}

static inline int fe_cell_changed(const FeFont *f, int ci) { return memcmp(f->rows[ci], f->orig[ci], sizeof f->rows[ci]) != 0; }
static inline int fe_group_changed(const FeFont *f, const FeGroup *g) {
    for (int k = 0; k < g->n; k++) if (fe_cell_changed(f, g->ci[k])) return 1;
    return 0;
}
static inline int fe_count_changed(const FeFont *f) {
    int n = 0;
    for (int i = 0; i < f->n; i++) n += fe_cell_changed(f, i);
    return n;
}
/* Code points whose picture is the same as this one's in the file as it was read: what an alias may follow (informational). */
static inline int fe_same_picture(const FeFont *f, int ci) {
    int n = 0;
    for (int i = 0; i < f->n; i++) if (i != ci && !memcmp(f->orig[i], f->orig[ci], sizeof f->orig[i])) n++;
    return n;
}

/* ---- the canvas: a group's pixels as one picture, n cells wide --------------------------------------------------------------- */

typedef struct { int n, w, h; uint8_t p[FE_MAXH][FE_CANVAS_W]; } FeCanvas;

static inline int fe_cw(const FeCanvas *c) { return c->n * c->w; }

static inline void fe_canvas_load(const FeFont *f, const FeGroup *g, FeCanvas *c) {
    memset(c, 0, sizeof *c);
    c->n = g->n; c->w = f->w; c->h = f->h;
    for (int k = 0; k < g->n; k++)
        for (int y = 0; y < f->h; y++)
            for (int x = 0; x < f->w; x++) c->p[y][k * f->w + x] = (f->rows[g->ci[k]][y] >> (f->w - 1 - x)) & 1;
}

/* Writes the canvas into the font; returns 1 when anything changed (and bumps gen). */
static inline int fe_canvas_store(FeFont *f, const FeGroup *g, const FeCanvas *c) {
    int changed = 0;
    for (int k = 0; k < g->n; k++)
        for (int y = 0; y < f->h; y++) {
            FeRow r = 0;
            for (int x = 0; x < f->w; x++) r = (FeRow)(r << 1 | (c->p[y][k * f->w + x] ? 1 : 0));
            if (f->rows[g->ci[k]][y] != r) { f->rows[g->ci[k]][y] = r; changed = 1; }
        }
    if (changed) f->gen++;
    return changed;
}

static inline int fe_in(const FeCanvas *c, int x, int y) { return x >= 0 && y >= 0 && x < fe_cw(c) && y < c->h; }
static inline int fe_get(const FeCanvas *c, int x, int y) { return fe_in(c, x, y) ? c->p[y][x] : 0; }
static inline void fe_put(FeCanvas *c, int x, int y, int v) { if (fe_in(c, x, y)) c->p[y][x] = v ? 1 : 0; }

static inline void fe_line(FeCanvas *c, int x0, int y0, int x1, int y1, int v) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        fe_put(c, x0, y0, v);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static inline void fe_rect(FeCanvas *c, int x0, int y0, int x1, int y1, int v, int filled) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (filled || x == x0 || x == x1 || y == y0 || y == y1) fe_put(c, x, y, v);
}

/* Flood fill (4-way, across the seam between cells) of the region of the pixel's own value. Returns the pixels set. */
static inline int fe_flood(FeCanvas *c, int x, int y, int v) {
    if (!fe_in(c, x, y)) return 0;
    int from = c->p[y][x], n = 0;
    if (from == (v ? 1 : 0)) return 0;
    static int stack[FE_MAXH * FE_CANVAS_W * 2];
    int sp = 0;
    stack[sp++] = x; stack[sp++] = y;
    while (sp > 0) {
        int py = stack[--sp], px = stack[--sp];
        if (!fe_in(c, px, py) || c->p[py][px] != from) continue;
        c->p[py][px] = v ? 1 : 0;
        n++;
        if (sp + 8 > (int)(sizeof stack / sizeof stack[0])) continue;
        stack[sp++] = px + 1; stack[sp++] = py;
        stack[sp++] = px - 1; stack[sp++] = py;
        stack[sp++] = px; stack[sp++] = py + 1;
        stack[sp++] = px; stack[sp++] = py - 1;
    }
    return n;
}

enum { FE_TOOL_PEN, FE_TOOL_LINE, FE_TOOL_BOX, FE_TOOL_RECT, FE_TOOL_FILL, FE_TOOLS };
static const char *const FE_TOOL_NAME[FE_TOOLS] = {"Pen", "Line", "Box", "Rectangle", "Fill"};
static inline int fe_tool_anchored(int tool) { return tool == FE_TOOL_LINE || tool == FE_TOOL_BOX || tool == FE_TOOL_RECT; }

/* An anchored tool's shape between two corners, into c with value v (the preview draws it into a copy). */
static inline void fe_shape(FeCanvas *c, int tool, int x0, int y0, int x1, int y1, int v) {
    if (tool == FE_TOOL_LINE) fe_line(c, x0, y0, x1, y1, v);
    else if (tool == FE_TOOL_BOX) fe_rect(c, x0, y0, x1, y1, v, 0);
    else if (tool == FE_TOOL_RECT) fe_rect(c, x0, y0, x1, y1, v, 1);
}

static inline void fe_flip_h(FeCanvas *c) {
    int cw = fe_cw(c);
    for (int y = 0; y < c->h; y++) for (int x = 0; x < cw / 2; x++) { uint8_t t = c->p[y][x]; c->p[y][x] = c->p[y][cw - 1 - x]; c->p[y][cw - 1 - x] = t; }
}
static inline void fe_flip_v(FeCanvas *c) {
    for (int y = 0; y < c->h / 2; y++) for (int x = 0; x < fe_cw(c); x++) { uint8_t t = c->p[y][x]; c->p[y][x] = c->p[c->h - 1 - y][x]; c->p[c->h - 1 - y][x] = t; }
}
static inline void fe_shift(FeCanvas *c, int dx, int dy) {
    FeCanvas o = *c;
    for (int y = 0; y < c->h; y++) for (int x = 0; x < fe_cw(c); x++) c->p[y][x] = (uint8_t)fe_get(&o, x - dx, y - dy);
}
static inline void fe_invert(FeCanvas *c) { for (int y = 0; y < c->h; y++) for (int x = 0; x < fe_cw(c); x++) c->p[y][x] ^= 1; }
static inline void fe_clear(FeCanvas *c) { for (int y = 0; y < c->h; y++) memset(c->p[y], 0, sizeof c->p[y]); }
static inline int fe_ink(const FeCanvas *c) { int n = 0; for (int y = 0; y < c->h; y++) for (int x = 0; x < fe_cw(c); x++) n += c->p[y][x]; return n; }

/* Cell-wise commands on a group of two or more cells. */
enum { FE_CELL_MIRROR_RIGHT, FE_CELL_MIRROR_LEFT, FE_CELL_COPY_RIGHT, FE_CELL_COPY_LEFT, FE_CELL_SWAP };
static inline int fe_cells_cmd(FeCanvas *c, int cmd) {
    if (c->n < 2) return 0;
    int w = c->w, last = (c->n - 1) * w;       /* the first and the last cell */
    FeCanvas o = *c;
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < w; x++) {
            int a = o.p[y][x], b = o.p[y][last + x], am = o.p[y][w - 1 - x], bm = o.p[y][last + w - 1 - x];
            switch (cmd) {
            case FE_CELL_MIRROR_RIGHT: c->p[y][last + x] = (uint8_t)am; break;     /* the last cell becomes the first, turned left to right */
            case FE_CELL_MIRROR_LEFT: c->p[y][x] = (uint8_t)bm; break;
            case FE_CELL_COPY_RIGHT: c->p[y][last + x] = (uint8_t)a; break;
            case FE_CELL_COPY_LEFT: c->p[y][x] = (uint8_t)b; break;
            case FE_CELL_SWAP: c->p[y][x] = (uint8_t)b; c->p[y][last + x] = (uint8_t)a; break;
            default: break;
            }
        }
    return 1;
}

/* ---- undo and redo: one entry a stroke or command, across glyphs ------------------------------------------------------------- */

typedef struct { FeGroup g; FeCanvas c; } FeSnap;
typedef struct {
    FeSnap *undo, *redo;
    int nu, nr;
} FeHist;

static inline int fe_hist_init(FeHist *h) {
    memset(h, 0, sizeof *h);
    h->undo = calloc(FE_UNDO, sizeof *h->undo);
    h->redo = calloc(FE_UNDO, sizeof *h->redo);
    return h->undo && h->redo ? 0 : -1;
}
static inline void fe_hist_free(FeHist *h) { free(h->undo); free(h->redo); memset(h, 0, sizeof *h); }

/* Call before a change: remembers the group as it is, and forgets the redo list. */
static inline void fe_hist_push(FeHist *h, const FeFont *f, const FeGroup *g) {
    if (h->nu == FE_UNDO) { memmove(h->undo, h->undo + 1, (FE_UNDO - 1) * sizeof *h->undo); h->nu--; }
    h->undo[h->nu].g = *g;
    fe_canvas_load(f, g, &h->undo[h->nu].c);
    h->nu++;
    h->nr = 0;
}
/* Takes back the last entry; returns the group it touched (so the screen can go to it), or NULL. */
static inline const FeGroup *fe_hist_undo(FeHist *h, FeFont *f) {
    if (h->nu == 0) return NULL;
    FeSnap *s = &h->undo[--h->nu];
    FeSnap *r = &h->redo[h->nr < FE_UNDO ? h->nr++ : FE_UNDO - 1];
    r->g = s->g;
    fe_canvas_load(f, &s->g, &r->c);
    fe_canvas_store(f, &s->g, &s->c);
    return &r->g;
}
static inline const FeGroup *fe_hist_redo(FeHist *h, FeFont *f) {
    if (h->nr == 0) return NULL;
    FeSnap *s = &h->redo[--h->nr];
    FeSnap *u = &h->undo[h->nu < FE_UNDO ? h->nu++ : FE_UNDO - 1];
    u->g = s->g;
    fe_canvas_load(f, &s->g, &u->c);
    fe_canvas_store(f, &s->g, &s->c);
    return &u->g;
}

/* Puts a group back to the picture it had when the font was read (the caller pushes history first). */
static inline int fe_revert(FeFont *f, const FeGroup *g) {
    int changed = 0;
    for (int k = 0; k < g->n; k++)
        if (fe_cell_changed(f, g->ci[k])) { memcpy(f->rows[g->ci[k]], f->orig[g->ci[k]], sizeof f->rows[0]); changed = 1; }
    if (changed) f->gen++;
    return changed;
}

/* ---- the edits file (R3i6nos Mono): the cells that differ from the compiled font, as scripts/r3font/edits.py reads them ------------ */

static inline int fe_mkdirs(const char *path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1; *p = '/'; }
    return mkdir(tmp, 0777) != 0 && errno != EEXIST ? -1 : 0;
}

/* Writes the file (through a temporary one and a rename, so a power cut leaves the old or the new). 0, or -1 with errno set. */
static inline int fe_edits_save(const FeFont *f, const char *path) {
    char tmp[PATH_MAX + 8], dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = 0; if (fe_mkdirs(dir) != 0) return -1; }
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *o = fopen(tmp, "w");
    if (!o) return -1;
    fprintf(o, "# r3i6nos font edits 1\n%s\n\n", FE_EDITS_FONT_LINE);
    for (int i = 0; i < f->n; i++) {
        if (!fe_cell_changed(f, i)) continue;
        int of = 1, part = fe_part(f, i, &of);
        fprintf(o, "cell %X %s %d/%d\n", (unsigned)f->cp[i], part ? f->groups[f->group_of[i]].name : "-", part ? part : 1, of);
        for (int y = 0; y < f->h; y++) {
            char row[FE_MAXW + 1];
            for (int x = 0; x < f->w; x++) row[x] = (f->rows[i][y] >> (f->w - 1 - x) & 1) ? '#' : '.';
            row[f->w] = 0;
            fprintf(o, "%s\n", row);
        }
        fputc('\n', o);
    }
    int bad = ferror(o);
    if (fclose(o) != 0 || bad) { remove(tmp); return -1; }
    if (rename(tmp, path) != 0) { remove(tmp); return -1; }
    return 0;
}

/* Reads a file written by fe_edits_save (or edits.txt) onto the font. Returns the cells read, or -1 with err saying why (the font is then unchanged). */
static inline int fe_edits_load(FeFont *f, const char *path, char *err, size_t cap) {
    FILE *in = fopen(path, "r");
    if (!in) { snprintf(err, cap, "cannot open %s: %s", path, strerror(errno)); return -1; }
    FeRow (*keep)[FE_MAXH] = malloc((size_t)f->n * sizeof *keep);
    if (!keep) { fclose(in); snprintf(err, cap, "out of memory"); return -1; }
    memcpy(keep, f->rows, (size_t)f->n * sizeof *keep);
    char line[256];
    int ln = 0, cells = 0, seen_font = 0, cur = -1, row = 0, rc = 0;
    while (rc == 0 && fgets(line, sizeof line, in)) {
        ln++;
        line[strcspn(line, "\r\n")] = 0;
        size_t L = strlen(line);
        if (cur >= 0 && row < f->h && (int)L == f->w && strspn(line, ".#") == L) {
            FeRow r = 0;
            for (int x = 0; x < f->w; x++) r = (FeRow)(r << 1 | (line[x] == '#'));
            f->rows[cur][row++] = r;
            continue;
        }
        if (cur >= 0 && (L == 0 || !strncmp(line, "cell ", 5))) {
            if (row != f->h) { snprintf(err, cap, "line %d: a cell has %d rows, not %d", ln, row, f->h); rc = -1; break; }
            cur = -1;
            cells++;
        }
        if (L == 0 || line[0] == '#') continue;
        if (!strncmp(line, "font ", 5)) {
            if (strcmp(line, FE_EDITS_FONT_LINE)) { snprintf(err, cap, "line %d: this file is for '%s', not R3i6nos Mono %dx%d", ln, line + 5, f->w, f->h); rc = -1; }
            seen_font = 1;
        } else if (!strncmp(line, "cell ", 5)) {
            char name[96] = "-", extra[32];
            unsigned cp = 0;
            int got = sscanf(line + 5, "%x %95s %31s", &cp, name, extra);
            int ci = got >= 1 ? fe_find(f, cp) : -1;
            if (!seen_font) { snprintf(err, cap, "line %d: no font line before the first cell", ln); rc = -1; }
            else if (ci < 0) { snprintf(err, cap, "line %d: U+%04X is not in the font", ln, cp); rc = -1; }
            else if (strcmp(name, "-") && (f->group_of[ci] < 0 || strcmp(f->groups[f->group_of[ci]].name, name))) {
                snprintf(err, cap, "line %d: U+%04X is '%s' in the font, not '%s' (an old file?)", ln, cp, f->group_of[ci] < 0 ? "unnamed" : f->groups[f->group_of[ci]].name, name);
                rc = -1;
            } else { cur = ci; row = 0; }
        } else { snprintf(err, cap, "line %d: a row is %d characters of '.' and '#'", ln, f->w); rc = -1; }
    }
    if (rc == 0 && cur >= 0) {
        if (row != f->h) { snprintf(err, cap, "the last cell has %d rows, not %d", row, f->h); rc = -1; }
        else cells++;
    }
    fclose(in);
    if (rc != 0) memcpy(f->rows, keep, (size_t)f->n * sizeof *keep);
    else f->gen++;
    free(keep);
    return rc == 0 ? cells : -1;
}

/* ---- a BDF (any monospace cell-shaped font up to 16 x 20) ----------------------------------------------------------------------- */

static inline int fe_bdf_hex(char c) { return isdigit((unsigned char)c) ? c - '0' : tolower((unsigned char)c) - 'a' + 10; }

typedef struct { uint32_t cp; FeRow r[FE_MAXH]; } Tmp;
static int fe_cmp_tmp(const void *a, const void *b) { uint32_t x = ((const Tmp *)a)->cp, y = ((const Tmp *)b)->cp; return x < y ? -1 : x > y; }

static inline int fe_load_bdf(FeFont *f, const char *path, char *err, size_t cap) {
    memset(f, 0, sizeof *f);
    FILE *in = fopen(path, "r");
    if (!in) { snprintf(err, cap, "cannot open %s: %s", path, strerror(errno)); return -1; }
    char line[512];
    int bw = 0, bh = 0, bx = 0, by = 0, nchars = 0, rc = 0, asc = -1;
    size_t head_cap = 4096, head_len = 0;
    char *head = malloc(head_cap);
    head[0] = 0;
    long data_at = -1;
    while (fgets(line, sizeof line, in)) {              /* the header, up to CHARS */
        if (!strncmp(line, "CHARS ", 6)) { nchars = atoi(line + 6); data_at = ftell(in); break; }
        if (!strncmp(line, "FONTBOUNDINGBOX ", 16)) sscanf(line + 16, "%d %d %d %d", &bw, &bh, &bx, &by);
        if (!strncmp(line, "FONT_ASCENT ", 12)) asc = atoi(line + 12);
        size_t L = strlen(line);
        if (head_len + L + 1 > head_cap) { head_cap *= 2; head = realloc(head, head_cap); }
        memcpy(head + head_len, line, L + 1);
        head_len += L;
    }
    if (data_at < 0 || nchars <= 0 || bw <= 0 || bh <= 0) { snprintf(err, cap, "%s is not a BDF with a FONTBOUNDINGBOX and CHARS", path); rc = -1; }
    else if (bw > FE_MAXW || bh > FE_MAXH) { snprintf(err, cap, "the cell is %dx%d: the editor takes up to %dx%d", bw, bh, FE_MAXW, FE_MAXH); rc = -1; }
    if (rc != 0) { free(head); fclose(in); return -1; }
    if (fe_alloc(f, nchars) != 0) { free(head); fclose(in); fe_free(f); snprintf(err, cap, "out of memory"); return -1; }
    f->w = bw; f->h = bh; f->base = asc >= 0 ? asc : bh + by;
    f->bdf_head = head;
    snprintf(f->path, sizeof f->path, "%s", path);
    const char *slash = strrchr(path, '/'), *stem = slash ? slash + 1 : path;
    snprintf(f->id, sizeof f->id, "%s", stem);
    char *dot = strrchr(f->id, '.');
    if (dot) *dot = 0;
    snprintf(f->title, sizeof f->title, "%s %dx%d", f->id, bw, bh);
    int got = 0, enc = -1, gw = 0, gh = 0, gx = 0, gy = 0, inbits = 0, brow = 0;
    FeRow tmp[FE_MAXH];
    memset(tmp, 0, sizeof tmp);
    int top = f->base;                                   /* rows above the baseline: the cell's top is `top` rows above it */
    while (rc == 0 && fgets(line, sizeof line, in)) {
        line[strcspn(line, "\r\n")] = 0;
        if (inbits) {
            if (!strcmp(line, "ENDCHAR")) {
                inbits = 0;
                if (enc >= 0 && got < f->n) { f->cp[got] = (uint32_t)enc; memcpy(f->rows[got], tmp, sizeof tmp); memcpy(f->orig[got], tmp, sizeof tmp); got++; }
            } else if (brow < gh) {
                int bytes = (gw + 7) / 8;
                for (int k = 0; k < gw; k++) {
                    int byte = k / 8 * 2 + 1 < (int)strlen(line) ? fe_bdf_hex(line[k / 8 * 2]) * 16 + fe_bdf_hex(line[k / 8 * 2 + 1]) : 0;
                    if (!(byte >> (7 - k % 8) & 1) || k / 8 >= bytes) continue;
                    int x = gx - bx + k, y = top - (gy + gh) + brow;
                    if (x < 0 || x >= f->w || y < 0 || y >= f->h) f->clipped++;
                    else tmp[y] |= (FeRow)(1u << (f->w - 1 - x));
                }
                brow++;
            }
        } else if (!strncmp(line, "STARTCHAR", 9)) { enc = -1; gw = gh = gx = gy = 0; memset(tmp, 0, sizeof tmp); }
        else if (!strncmp(line, "ENCODING ", 9)) enc = atoi(line + 9);
        else if (!strncmp(line, "BBX ", 4)) sscanf(line + 4, "%d %d %d %d", &gw, &gh, &gx, &gy);
        else if (!strcmp(line, "BITMAP")) { inbits = 1; brow = 0; }
    }
    fclose(in);
    f->n = got;
    /* a BDF's order is its own: sort by code point (insertion sort on indices would be slow for 65k; qsort a permutation) */
    Tmp *t = malloc((size_t)got * sizeof *t);
    if (!t) { snprintf(err, cap, "out of memory"); fe_free(f); return -1; }
    for (int i = 0; i < got; i++) { t[i].cp = f->cp[i]; memcpy(t[i].r, f->rows[i], sizeof t[i].r); }
    qsort(t, (size_t)got, sizeof *t, fe_cmp_tmp);
    for (int i = 0; i < got; i++) { f->cp[i] = t[i].cp; memcpy(f->rows[i], t[i].r, sizeof t[i].r); memcpy(f->orig[i], t[i].r, sizeof t[i].r); }
    free(t);
    if (got == 0) { snprintf(err, cap, "%s has no glyphs", path); fe_free(f); return -1; }
    return got;
}

/* Writes the BDF: its own header as it was, then every glyph as a whole cell. */
static inline int fe_save_bdf(const FeFont *f, const char *path) {
    char tmp[PATH_MAX + 8], dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = 0; if (fe_mkdirs(dir) != 0) return -1; }
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *o = fopen(tmp, "w");
    if (!o) return -1;
    fputs(f->bdf_head ? f->bdf_head : "STARTFONT 2.1\n", o);
    fprintf(o, "CHARS %d\n", f->n);
    int bytes = (f->w + 7) / 8, desc = f->h - f->base;
    for (int i = 0; i < f->n; i++) {
        fprintf(o, "STARTCHAR u%04X\nENCODING %u\nSWIDTH 500 0\nDWIDTH %d 0\nBBX %d %d 0 %d\nBITMAP\n", (unsigned)f->cp[i], (unsigned)f->cp[i], f->w, f->w, f->h, -desc);
        for (int y = 0; y < f->h; y++) {
            unsigned long v = (unsigned long)f->rows[i][y] << (bytes * 8 - f->w);
            fprintf(o, "%0*lX\n", bytes * 2, v);
        }
        fputs("ENDCHAR\n", o);
    }
    fputs("ENDFONT\n", o);
    int bad = ferror(o);
    if (fclose(o) != 0 || bad) { remove(tmp); return -1; }
    return rename(tmp, path) == 0 ? 0 : (remove(tmp), -1);
}

/* ---- painting a cell into 640 x 480 ARGB pixels (the screen's own glyphs are the compiled font; an edited one has to be drawn here) -- */

#define FE_SCREEN_W 640
#define FE_SCREEN_H 480
static inline void fe_blit_cell(uint32_t *px, const FeFont *f, int ci, int col, int row, int scale, uint32_t fg, uint32_t bg) {
    if (ci < 0 || ci >= f->n || scale < 1) return;
    for (int y = 0; y < f->h * scale; y++) {
        int py = row * 20 + y;
        if (py < 0 || py >= FE_SCREEN_H) continue;
        for (int x = 0; x < f->w * scale; x++) {
            int pxx = col * 10 + x;
            if (pxx < 0 || pxx >= FE_SCREEN_W) continue;
            px[(size_t)py * FE_SCREEN_W + (size_t)pxx] = (f->rows[ci][y / scale] >> (f->w - 1 - x / scale) & 1) ? fg : bg;
        }
    }
}

#endif /* FE_MODEL_H */

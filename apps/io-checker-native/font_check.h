/* The IO Checker's font checks: what only a program can answer about ANSIapps Mono at 10x20, shared by the font panels, the device report and --selftest.
 *
 *   fc_names():    every named glyph has a bitmap in each of its cells; every `.wide` and `.tall` counterpart has a base, and is the size it should be
 *                  (wide: twice the cells across, tall: twice across and twice down).
 *   fc_portrait(): the portrait maker's parts (ansiapps_portrait.h, made from the font's repository): every part is in the font, a right part is its
 *                  left part reversed (mirrored families), every listed join still holds on the 10x20 bitmaps (the touching edges are the same pixel for
 *                  pixel, or one boundary a step apart; hair over the face by the overhang rule), and no kind of neighbours lost joins in the scaling.
 *   fc_build():    a portrait built from the parts the way the maker does (each part joins its left and upper neighbours), drawn by fc_draw().
 *
 * The 10x20 size keeps the 8x16 edges exact (the font's tiling rule repeats columns 0 and 7 and rows 0, 4, 11 and 15), so an exact join stays exact; a
 * sloped (soft) join's step grows with the size, so the check uses the step times 1.25, rounded up. Needs r3i6nos_ansiapps.h (the font tables). Untested
 * on a unit: the checks are the same on any machine, but the page has not been seen on the screen. */
#ifndef IOC_FONT_CHECK_H
#define IOC_FONT_CHECK_H

#include <stdio.h>
#include <string.h>
#include "font/ansiapps-mono/ansiapps_portrait.h"

/* ---- the named glyphs ---------------------------------------------------------------------------------------------------------- */

typedef struct {
    int names;                  /* named glyphs (their first rows; `.row2` entries are not counted again) */
    int cells, cells_missing;   /* cells in all of them, and those with no bitmap */
    int cells_blank;            /* cells whose bitmap is empty (staff.blank and the like are meant to be) */
    int one, two, bigger;       /* names one cell, two cells across and one down, and anything bigger (two down, or four across) */
    int wide, wide_ok, wide_nobase;
    int tall, tall_ok, tall_nobase;
    int codes, codes_missing;   /* code points of the font, and any the table lists without a bitmap (a broken table) */
    char first_bad[96];         /* the first name that failed, for the reply line */
} FcNames;

static int fc_row_name(const char *n) {                 /* "x.row2": the next row of a tall glyph, listed in the names table as a name of its own */
    const char *d = strrchr(n, '.');
    return d && !strncmp(d, ".row", 4) && d[4] >= '2' && d[4] <= '9' && !d[5];
}

static int fc_blank(const unsigned short *g) { for (int y = 0; y < R3MONO_H; y++) if (g[y]) return 0; return 1; }

static void fc_names(FcNames *r) {
    memset(r, 0, sizeof *r);
    for (int i = 0; i < R3MONO_NAMES; i++) {
        const R3MonoName *e = &r3mono_names[i];
        if (fc_row_name(e->name)) continue;
        r->names++;
        if (e->cols == 1 && e->rows == 1) r->one++; else if (e->cols == 2 && e->rows == 1) r->two++; else r->bigger++;
        int bad = 0;
        for (int k = 0; k < e->cols * e->rows; k++) {
            const unsigned short *g = r3mono_find(e->cp + (unsigned)k);
            r->cells++;
            if (!g) { r->cells_missing++; bad = 1; } else if (fc_blank(g)) r->cells_blank++;
        }
        if (bad && !r->first_bad[0]) snprintf(r->first_bad, sizeof r->first_bad, "%s has a cell with no bitmap", e->name);
        size_t n = strlen(e->name);
        int w = n > 5 && !strcmp(e->name + n - 5, ".wide"), t = n > 5 && !strcmp(e->name + n - 5, ".tall");
        if (w || t) {
            char base[96];
            snprintf(base, sizeof base, "%.*s", (int)(n - 5), e->name);
            const R3MonoName *b = r3mono_entry(base);
            int ok = b && e->cols == 2 * b->cols && e->rows == (t ? 2 * b->rows : b->rows) && !bad;
            if (w) { r->wide++; if (!b) r->wide_nobase++; else if (ok) r->wide_ok++; }
            else { r->tall++; if (!b) r->tall_nobase++; else if (ok) r->tall_ok++; }
            if (!ok && !r->first_bad[0]) snprintf(r->first_bad, sizeof r->first_bad, "%s is not the size of %s%s", e->name, base, b ? "'s counterpart" : " (no base)");
        }
    }
    r->codes = R3MONO_COUNT;
    for (int i = 0; i < R3MONO_COUNT; i++) if (!r3mono_find(r3mono_cp[i])) r->codes_missing++;
}

/* ---- portrait parts: edges ----------------------------------------------------------------------------------------------------- */

#define FC_N 20                                        /* the longest edge (a left or right edge is 20 pixels) */

static void fc_edge(const unsigned short *g, char which, unsigned char *out, int *n) {   /* t b l r */
    if (which == 't' || which == 'b') {
        const unsigned short r = g[which == 't' ? 0 : R3MONO_H - 1];
        for (int x = 0; x < R3MONO_W; x++) out[x] = (unsigned char)(r >> (R3MONO_W - 1 - x) & 1);
        *n = R3MONO_W;
    } else {
        int sh = which == 'l' ? R3MONO_W - 1 : 0;
        for (int y = 0; y < R3MONO_H; y++) out[y] = (unsigned char)(g[y] >> sh & 1);
        *n = R3MONO_H;
    }
}

/* The one boundary between ink and paper along an edge, and which way it goes; 0 when there is not exactly one. */
static int fc_boundary(const unsigned char *b, int n, int *at, int *dir) {
    int t = -1, cnt = 0;
    for (int i = 1; i < n; i++) if (b[i] != b[i - 1]) { t = i; cnt++; }
    if (cnt != 1) return 0;
    *at = t; *dir = b[0] ? 1 : -1;
    return 1;
}

/* 0: the edges do not join. 1: the same pixel for pixel. 2: one boundary each, the same way, at most `step` apart (a sloped contour), and, for a taper,
 * the lower one no wider than the upper. */
static int fc_join_edges(const unsigned char *a, const unsigned char *b, int n, int step, int taper) {
    if (!memcmp(a, b, (size_t)n)) return 1;
    if (!step) return 0;
    int xa, xd, ya, yd;
    if (!fc_boundary(a, n, &xa, &xd) || !fc_boundary(b, n, &ya, &yd) || xd != yd || abs(xa - ya) > step) return 0;
    if (taper && !(xd == -1 ? ya >= xa : ya <= xa)) return 0;
    return 2;
}

/* Hair over the face: the upper edge's first ink starts at or outside the lower's, by at most max (a right-hand part reads from the right). */
static int fc_overhang_ok(const unsigned char *up, const unsigned char *lo, int n, int max, int right) {
    int ua = -1, la = -1;
    for (int i = 0; i < n; i++) {
        int j = right ? n - 1 - i : i;
        if (ua < 0 && up[j]) ua = i;
        if (la < 0 && lo[j]) la = i;
    }
    if (ua < 0 || la < 0) return 1;
    return ua <= la && la - ua <= max;
}

static int fc_scaled(int step8) { return (step8 * 5 + 3) / 4; }           /* 8x16 pixels to 10x20: times 1.25, rounded up */

static int fc_part_by_name(const char *name) {
    for (int i = 0; i < AP_PARTS; i++) if (!strcmp(ap_part[i].name, name)) return i;
    return -1;
}

/* a part's name with .L and .R swapped, or "" when it has neither */
static void fc_swap_side(const char *name, char *out, size_t cap) {
    size_t n = strlen(name);
    snprintf(out, cap, "%s", name);
    if (n >= 2 && name[n - 2] == '.' && (name[n - 1] == 'L' || name[n - 1] == 'R')) out[n - 1] = name[n - 1] == 'L' ? 'R' : 'L'; else out[0] = 0;
}

/* Does part a join part b (a left of or above b, dir 'h' or 'v') on the 10x20 bitmaps? 0 no, 1 exact, 2 soft, 3 hair over the face. */
static int fc_parts_join(int ia, int ib, char dir) {
    const ApPart *a = &ap_part[ia], *b = &ap_part[ib];
    const unsigned short *ga = r3mono_find(a->cp), *gb = r3mono_find(b->cp);
    if (!ga || !gb) return 0;
    unsigned char ea[FC_N], eb[FC_N];
    int na, nb;
    const ApFamily *fa = &ap_family[a->family], *fb = &ap_family[b->family];
    fc_edge(ga, dir == 'h' ? 'r' : 'b', ea, &na);
    fc_edge(gb, dir == 'h' ? 'l' : 't', eb, &nb);
    if (dir == 'v' && fa->overhang >= 0 && (fa->over >> b->family & 1))
        return fc_overhang_ok(ea, eb, na, fc_scaled(fa->overhang), strstr(a->name, ".R") != 0 && a->name[strlen(a->name) - 1] == 'R') ? 3 : 0;
    int step = fa->step > fb->step ? fa->step : fb->step;
    return fc_join_edges(ea, eb, na, fc_scaled(step), dir == 'v' && fb->taper);
}

/* ---- the portrait checks ----------------------------------------------------------------------------------------------------- */

typedef struct {
    int parts, parts_in_font, aliases;
    int mirror_pairs, mirror_ok;
    int joins, join_exact, join_soft, join_over, join_bad;
    int compat_rows, compat_lost, compat_gained, compat_count_bad;   /* the upstream matrix: per pair of neighbouring kinds, how many parts join */
    int layouts, layout_cells, layout_cells_bad;
    int builds, builds_clean;                                         /* portraits built from random seeds, and those with no broken seam */
    char first_bad[120];
} FcPortrait;

static void fc_note(FcPortrait *r, const char *fmt, const char *a, const char *b) {
    if (!r->first_bad[0]) snprintf(r->first_bad, sizeof r->first_bad, fmt, a, b);
}

typedef struct { unsigned short part[4][8]; int layout, conflicts, cells; unsigned char hair, skin, cloth; } FcPortraitImg;
static void fc_build(FcPortraitImg *p, unsigned seed, int layout);

static void fc_portrait(FcPortrait *r) {
    memset(r, 0, sizeof *r);
    r->parts = AP_PARTS;
    for (int i = 0; i < AP_PARTS; i++) {
        const ApPart *p = &ap_part[i];
        const unsigned short *g = r3mono_find(p->cp);
        if (g) r->parts_in_font++; else fc_note(r, "%s%s has no glyph in the font", p->name, "");
        r->aliases += p->alias;
        if (ap_family[p->family].mirrored) {
            size_t n = strlen(p->name);
            if (n > 2 && p->name[n - 1] == 'L') {                       /* each left part has a right one that is the same picture reversed */
                char other[160];
                fc_swap_side(p->name, other, sizeof other);
                int j = other[0] ? fc_part_by_name(other) : -1;
                r->mirror_pairs++;
                const unsigned short *h = j >= 0 ? r3mono_find(ap_part[j].cp) : 0;
                int ok = g && h;
                for (int y = 0; ok && y < R3MONO_H; y++) {
                    unsigned v = g[y], m = 0;
                    for (int x = 0; x < R3MONO_W; x++) if (v >> x & 1) m |= 1u << (R3MONO_W - 1 - x);
                    if (m != h[y]) ok = 0;
                }
                if (ok) r->mirror_ok++; else fc_note(r, "%s is not the mirror of its right part%s", p->name, "");
            }
        }
    }
    for (int k = 0; k < AP_JOINS; k++) {
        int j = fc_parts_join(ap_join[k].a, ap_join[k].b, ap_join[k].dir);
        r->joins++;
        if (j == 1) r->join_exact++; else if (j == 2) r->join_soft++; else if (j == 3) r->join_over++;
        else { r->join_bad++; fc_note(r, "%s does not join %s", ap_part[ap_join[k].a].name, ap_part[ap_join[k].b].name); }
    }
    for (int c = 0; c < AP_COMPAT; c++) {                                  /* per kind pair: no more lost than the 8x16 count (the 10x20 step is looser, so more may join) */
        const ApCompat *m = &ap_compat[c];
        int na = 0, nb = 0, ok = 0;
        for (int i = 0; i < AP_PARTS; i++) { na += ap_part[i].kind == m->a; nb += ap_part[i].kind == m->b; }
        if (na != m->parts_a || nb != m->parts_b) { r->compat_count_bad++; fc_note(r, "kinds %s and %s have a different number of parts than the font's matrix", ap_kind[m->a], ap_kind[m->b]); }
        const char ea = m->dir == 'h' ? 'r' : 'b', eb = m->dir == 'h' ? 'l' : 't';
        for (int i = 0; i < AP_PARTS; i++) {
            if (ap_part[i].kind != m->a) continue;
            const unsigned short *ga = r3mono_find(ap_part[i].cp);
            if (!ga) continue;
            unsigned char xa[FC_N], xb[FC_N];
            int n1, n2;
            fc_edge(ga, ea, xa, &n1);
            for (int j = 0; j < AP_PARTS; j++) {
                if (ap_part[j].kind != m->b) continue;
                const unsigned short *gb = r3mono_find(ap_part[j].cp);
                if (!gb) continue;
                fc_edge(gb, eb, xb, &n2);
                int sa = ap_family[ap_part[i].family].step, sb = ap_family[ap_part[j].family].step;
                if (fc_join_edges(xa, xb, n1, fc_scaled(sa > sb ? sa : sb), 0)) ok++;
            }
        }
        r->compat_rows++;
        if (ok < m->compatible) { r->compat_lost++; fc_note(r, "pairs of %s and %s lost joins at 10x20%s", ap_kind[m->a], ap_kind[m->b]); }
        else if (ok > m->compatible) r->compat_gained++;
    }
    r->layouts = AP_LAYOUTS;
    for (int l = 0; l < AP_LAYOUTS; l++)
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 8; x++) {
                r->layout_cells++;
                int ok = 0;
                for (int s = 0; s < 2; s++) { int k = ap_cells[l][y][x][s]; if (k >= 0 && k < AP_KINDS) ok = 1; }
                if (!ok) { r->layout_cells_bad++; fc_note(r, "a layout cell has no kind%s%s", "", ""); }
            }
    for (int seed = 1; seed <= 24; seed++) {                                     /* the maker, 12 seeds a layout: a portrait with every seam joined */
        FcPortraitImg img;
        fc_build(&img, (unsigned)seed * 2654435761u, seed % AP_LAYOUTS);
        r->builds++;
        if (img.conflicts == 0) r->builds_clean++;
    }
}

/* ---- the maker: a portrait from the parts -------------------------------------------------------------------------------- */

static unsigned fc_rnd(unsigned *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }

static int fc_flat(const unsigned char *e, int n) { for (int i = 1; i < n; i++) if (e[i] != e[0]) return 0; return 1; }

/* Do two parts sit well side by side (a left of or above b)? A seam between two colour models is not compared (a line on skin against a skin silhouette); hair
 * over a cheek or an ear is the overhang rule; two flat edges are a straight boundary; otherwise the edges must join, as the maker requires. */
static int fc_seam_ok(int ia, int ib, char dir) {
    const ApPart *a = &ap_part[ia], *b = &ap_part[ib];
    const ApFamily *fa = &ap_family[a->family];
    if (dir == 'v' && fa->overhang >= 0 && (fa->over >> b->family & 1)) return fc_parts_join(ia, ib, dir) != 0;
    if (ap_kind_model[a->kind] != ap_kind_model[b->kind]) return 1;
    const unsigned short *ga = r3mono_find(a->cp), *gb = r3mono_find(b->cp);
    if (!ga || !gb) return 0;
    unsigned char ea[FC_N], eb[FC_N];
    int na, nb;
    fc_edge(ga, dir == 'h' ? 'r' : 'b', ea, &na);
    fc_edge(gb, dir == 'h' ? 'l' : 't', eb, &nb);
    if (fc_flat(ea, na) && fc_flat(eb, nb)) return 1;
    return fc_parts_join(ia, ib, dir) != 0;
}

#define FC_BLANK 0xFFFF

/* Fills a portrait: row by row, each cell a part of one of the kinds its layout cell allows that joins the cell on its left and the one above. The right half
 * of the face tries the partner (the left part's mirror, or the other half of a two-cell tile) first, so a face is usually symmetric. */
static void fc_build(FcPortraitImg *p, unsigned seed, int layout) {
    unsigned s = seed ? seed : 1;
    memset(p, 0, sizeof *p);
    p->layout = layout % AP_LAYOUTS;
    for (int tries = 0; tries < 12; tries++) {
        int conflicts = 0, cells = 0;
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 8; x++) {
                int cand[AP_PARTS], nc = 0, good[AP_PARTS], ng = 0;
                for (int i = 0; i < AP_PARTS; i++) {
                    int k = ap_part[i].kind, allowed = 0;
                    for (int q = 0; q < 2; q++) if (ap_cells[p->layout][y][x][q] == k) allowed = 1;
                    if (!allowed) continue;
                    cand[nc++] = i;
                    int ok = 1;
                    if (x > 0 && p->part[y][x - 1] != FC_BLANK && !fc_seam_ok(p->part[y][x - 1], i, 'h')) ok = 0;
                    if (y > 0 && p->part[y - 1][x] != FC_BLANK && !fc_seam_ok(p->part[y - 1][x], i, 'v')) ok = 0;
                    if (ok) good[ng++] = i;
                }
                int pick = FC_BLANK;
                if (x >= 4 && p->part[y][7 - x] != FC_BLANK) {          /* the partner of the part on the other side of the face's centre */
                    char other[160];
                    fc_swap_side(ap_part[p->part[y][7 - x]].name, other, sizeof other);
                    int j = other[0] ? fc_part_by_name(other) : -1;
                    for (int g = 0; j >= 0 && g < ng; g++) if (good[g] == j) pick = j;
                }
                if (pick == FC_BLANK && ng) pick = good[fc_rnd(&s) % (unsigned)ng];
                else if (pick == FC_BLANK && nc) { pick = cand[fc_rnd(&s) % (unsigned)nc]; conflicts++; }
                if (pick != FC_BLANK) cells++;
                p->part[y][x] = (unsigned short)pick;
            }
        p->conflicts = conflicts; p->cells = cells;
        if (!conflicts) break;
    }
    static const unsigned char hairs[] = {R3_YELLOW, R3_WHITE, R3_LIGHT_GRAY, R3_LIGHT_GREEN, R3_LIGHT_CYAN, R3_LIGHT_MAGENTA};
    static const unsigned char skins[] = {R3_LIGHT_RED, R3_LIGHT_GRAY, R3_BROWN};
    p->skin = skins[fc_rnd(&s) % 3];
    do p->hair = hairs[fc_rnd(&s) % 6]; while (p->hair == p->skin);
    p->cloth = R3_CYAN;
}

/* Draws a portrait with its top left cell at (col, row). A silhouette part is skin on the backdrop, a line part ink on skin, hair ink hair on the backdrop;
 * the ink of a line is black on a light skin and white on the brown one. A cell no part fills (the neck) is the backdrop. */
static void fc_draw(R3Screen *s, int col, int row, const FcPortraitImg *p) {
    int ink = p->skin == R3_BROWN ? R3_WHITE : R3_BLACK;
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 8; x++) {
            int i = p->part[y][x];
            if (i == FC_BLANK) { r3_put(s, col + x, row + y, ' ', R3_BLACK, R3_BLACK); continue; }
            int model = ap_kind_model[ap_part[i].kind], fg, bg;
            if (model == 'h') { fg = p->hair; bg = R3_BLACK; }
            else if (model == 's') { fg = !strncmp(ap_kind[ap_part[i].kind], "neck", 4) ? p->cloth : p->skin; bg = R3_BLACK; }
            else { fg = ink; bg = p->skin; }
            r3_put_mono(s, col + x, row + y, ap_part[i].cp, fg, bg);
        }
}

#endif

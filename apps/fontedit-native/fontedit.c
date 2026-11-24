/* Font Editor (native ARM64, C and SDL2): a controller-first bitmap font editor for the R36S (docs/FONT-EDITOR.md).
 *
 * It touches up R3i6nos Mono (the 10 x 20 font of every r3i6nos app, theme/font/r3i6nos-mono/) and, with --font FILE.bdf, any monospace cell-shaped
 * BDF up to 16 x 20. Its first job is the font's MULTI-CELL glyphs: the 508 two-cell icons, key caps, shoulders, sticks, triggers, D-pads and knobs of
 * R3i6nos Mono (a name such as icon.app.hydrolik is two code points printed together). The first panel lists them, and A opens a name on ONE canvas,
 * 2 columns of text cells a pixel, 20 x 20 pixels for a pair, drawn across the seam between the cells; the side shows it at the size it is used, in
 * a line of text and enlarged. Everything else (the 884 names, every code point, what has changed) is a panel away (Fn).
 *
 *   fontedit                              the editor
 *   fontedit --font FILE.bdf              edit another bitmap font (saved as <data folder>/<name>.bdf; the file read is never written)
 *   fontedit --report                     write the device report without a window (over SSH)
 *   fontedit --selftest                   check the model, the files, the screens and the report; draw one frame; print one R3I6NOS_SMOKE line
 *   fontedit --screenshot out.bmp [scr]   draw a screen to a file (browse, edit, sample, menu, commands)
 *   fontedit --version
 *
 * Edits to R3i6nos Mono are written, at the end of every stroke and command, to <app data>/edits-r3i6nos-mono.txt; scripts/merge_font_edits.py folds that
 * file into theme/font/r3i6nos-mono/edits.txt and scripts/build-r3-font.sh puts the change in every output of the font. App data: edits-<font>.txt (or
 * <name>.bdf), settings.conf, r36s-report.txt. Overrides for tests: R3I6NOS_APP_DATA, R3I6NOS_ROMS.
 * Untested on a unit: nothing here has run on a real R36S (CLAUDE.md rule 1). */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define R3_WITH_MONO 1
#include "r3i6nos_ansiapps.h"
#include "r3i6nos_themes.h"
#include "r3i6nos_input.h"
#include "r3i6nos_mainmenu.h"
#include "r3i6nos_keymap.h"
#include "r3i6nos_report.h"
#include "r3i6nos_reportbar.h"
#include "r3i6nos_toast.h"
#include "r3i6nos_appdata.h"
#include "fe_model.h"

#define VERSION "0.1"
#ifndef R3_BUILD_COMMIT
#define R3_BUILD_COMMIT "dev"           /* scripts/build_native_app.sh passes the git commit */
#endif
#define BUILD_LINE R3_BUILD_COMMIT " " __DATE__
#define APP "Font Editor"
#define DATA_NAME "FontEdit"
#define REPORT_STEPS 6
#define LIST_PER 18                     /* entries a page of the list: whole pages, never a scroll (rule 5) */
#define MAX_OV 640

enum { SC_BROWSE, SC_EDIT };
enum { PN_MULTI, PN_NAMES, PN_CODES, PN_CHANGED, PN_COUNT };
static const char *const PN_TITLE[PN_COUNT] = {"Multi-cell glyphs", "Named glyphs", "Code points", "Changed"};
enum { EP_DRAW, EP_SAMPLE, EP_COUNT };
enum { OV_NONE, OV_MENU, OV_ABOUT, OV_THEME, OV_KEYMAP, OV_CMDS, OV_CONFIRM };
enum { C_EDIT = 1, C_EDIT_CELL, C_REVERT_G, C_REVERT_ALL, C_SAVE, C_UNDO, C_REDO, C_FLIPH, C_FLIPV, C_MIRR_R, C_MIRR_L, C_COPY_R, C_COPY_L, C_SWAP,
       C_INVERT, C_CLEAR, C_SHIFT_L, C_SHIFT_R, C_SHIFT_U, C_SHIFT_D, C_GUIDES, C_TOOL };

typedef struct { short col, row; int ci; short scale; unsigned char fg, bg; } Ov;      /* a glyph the cell grid cannot draw: an edited one */

struct Loop { SDL_Renderer *r; R3Video *video; R3Screen screen; };

typedef struct {
    FeFont font;
    FeHist hist;
    int screen, panel, epanel, overlay;
    int *list[PN_COUNT], nlist[PN_COUNT], sel[PN_COUNT];
    FeGroup g;                          /* what the editor has open */
    FeCanvas c;
    int cx, cy, tool, ax, ay, anchored, stroke, stroke_val;     /* the cursor, the tool, a corner waiting, a Pen stroke on (1 ink, 2 erase) */
    int guides;
    int theme, theme_sel;
    int cmd_sel, ncmd, cmd_id[24];
    int confirm_id, confirm_sel;
    R3MainMenu mm;
    R3Keymap km;
    R3Toast toast;
    R3Input in;
    char reply[200];
    int reply_style;
    int quit, force, pending, save_failed;
    char data_dir[PATH_MAX], edits_path[PATH_MAX];
    struct Loop *loop;
    Ov ov[MAX_OV];
    int nov;
    uint32_t ov_sig;
    unsigned last_gen;
    SDL_Joystick *js;
    int report_missed;
} App;

static App *g_app;      /* the render hook's */

/* ---- messages ----------------------------------------------------------------------------------------------------------------- */

static void say(App *a, int style, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->reply, sizeof a->reply, fmt, ap);
    va_end(ap);
    a->reply_style = style;
    a->force = 1;
}

/* ---- the data folder, settings and saving (rule 16) ---------------------------------------------------------------------------------- */

static const char *env_or(const char *name, const char *def) { const char *v = getenv(name); return v && *v ? v : def; }

static void app_data_dir(char *out, size_t cap) {
    const char *d = getenv("R3I6NOS_APP_DATA");
    if (d && *d) snprintf(out, cap, "%s", d);
    else snprintf(out, cap, "%s/%s", env_or("R3I6NOS_ROMS", "/roms"), DATA_NAME);
}

static void set_paths(App *a) {
    app_data_dir(a->data_dir, sizeof a->data_dir);
    if (a->font.builtin) snprintf(a->edits_path, sizeof a->edits_path, "%.400s/edits-%s.txt", a->data_dir, a->font.id);
    else snprintf(a->edits_path, sizeof a->edits_path, "%.400s/%s.bdf", a->data_dir, a->font.id);
}

static void settings_path(const App *a, char *out, size_t cap) { snprintf(out, cap, "%.400s/settings.conf", a->data_dir); }

static void settings_load(App *a) {
    char path[PATH_MAX + 32], line[128];
    settings_path(a, path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "theme=", 6)) { int t = r3_theme_find(line + 6); if (t >= 0) a->theme = t; }
        else if (!strncmp(line, "guides=", 7)) a->guides = atoi(line + 7) != 0;
    }
    fclose(f);
    r3_theme_apply(a->theme);
}

static int settings_save(const App *a) {
    char path[PATH_MAX + 32];
    if (fe_mkdirs(a->data_dir) != 0) return -1;
    settings_path(a, path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    int ok = fprintf(f, "theme=%s\nguides=%d\n", r3_themes[a->theme].name, a->guides) > 0;
    return fclose(f) == 0 && ok ? 0 : -1;
}

/* Writes the edits (R3i6nos Mono) or the BDF. A card that is out or full says so on the reply row and the editing goes on, in memory. */
static int autosave(App *a) {
    int rc = a->font.builtin ? fe_edits_save(&a->font, a->edits_path) : fe_save_bdf(&a->font, a->edits_path);
    if (rc != 0) {
        a->save_failed = 1;
        say(a, R3_ST_ERROR, "NOT SAVED: cannot write %s (%s). Your edits are in memory; fix the card and run Save now.", a->edits_path, strerror(errno));
        return -1;
    }
    a->save_failed = 0;
    return 0;
}

/* ---- the lists ------------------------------------------------------------------------------------------------------------------ */

static void build_lists(App *a) {
    FeFont *f = &a->font;
    for (int p = 0; p < PN_COUNT; p++) { free(a->list[p]); a->list[p] = NULL; a->nlist[p] = 0; }
    int cap = f->n > f->ng ? f->n : f->ng;
    for (int p = 0; p < PN_COUNT; p++) a->list[p] = malloc((size_t)(cap + 1) * sizeof(int));
    for (int gi = 0; gi < f->ng; gi++) {
        a->list[PN_NAMES][a->nlist[PN_NAMES]++] = gi;
        if (f->groups[gi].n >= 2) a->list[PN_MULTI][a->nlist[PN_MULTI]++] = gi;
        if (fe_group_changed(f, &f->groups[gi])) a->list[PN_CHANGED][a->nlist[PN_CHANGED]++] = gi;
    }
    for (int i = 0; i < f->n; i++) {
        a->list[PN_CODES][a->nlist[PN_CODES]++] = i;
        if (f->group_of[i] < 0 && fe_cell_changed(f, i)) a->list[PN_CHANGED][a->nlist[PN_CHANGED]++] = -i - 2;
    }
    for (int p = 0; p < PN_COUNT; p++) if (a->sel[p] >= a->nlist[p]) a->sel[p] = a->nlist[p] > 0 ? a->nlist[p] - 1 : 0;
}

/* The group of a list entry. Returns 0 when the list is empty. */
static int entry_group(const App *a, int panel, int idx, FeGroup *g) {
    if (idx < 0 || idx >= a->nlist[panel]) return 0;
    int e = a->list[panel][idx];
    if (panel == PN_CODES) fe_group_for(&a->font, e, g);
    else if (e >= 0) *g = a->font.groups[e];
    else fe_group_cp(&a->font, -e - 2, g);
    return 1;
}

/* ---- the screen's glyphs the grid cannot draw ------------------------------------------------------------------------------------ */

static void ov_add(App *a, int col, int row, int ci, int scale, int fg, int bg) {
    if (a->nov >= MAX_OV || ci < 0) return;
    a->ov[a->nov++] = (Ov){(short)col, (short)row, ci, (short)scale, (unsigned char)fg, (unsigned char)bg};
}

/* A group at (col,row), `scale` times its size (a cell of the font is one text cell at scale 1); returns the cells wide. */
static int ov_group(App *a, R3Screen *s, int col, int row, const FeGroup *g, int scale, int fg, int bg) {
    int w = 0;
    for (int k = 0; k < g->n; k++)
        for (int cc = 0; cc < scale; cc++, w++) {
            for (int rr = 0; rr < scale; rr++) r3_put(s, col + w, row + rr, 32, fg, bg);
            if (cc == 0) ov_add(a, col + w, row, g->ci[k], scale, fg, bg);
        }
    return w;
}

/* Text in the font being edited, so a changed letter shows in a line of text. A character the font lacks is a space. */
static void ov_text(App *a, R3Screen *s, int col, int row, const char *text, int fg, int bg) {
    for (int i = 0; text[i] && col + i < R3_COLS; i++) {
        r3_put(s, col + i, row, 32, fg, bg);
        ov_add(a, col + i, row, fe_find(&a->font, (unsigned char)text[i]), 1, fg, bg);
    }
}

/* A dialog is over the screen: the glyphs under it (and only those) are not painted, or they would show through. */
static void ov_clip(App *a, int col, int row, int w, int h) {
    int k = 0;
    for (int i = 0; i < a->nov; i++) {
        const Ov *o = &a->ov[i];
        if (o->col < col + w && o->col + o->scale > col && o->row < row + h && o->row + o->scale > row) continue;
        a->ov[k++] = *o;
    }
    a->nov = k;
}

static void hook_paint(uint32_t *px, void *ctx) {
    App *a = ctx;
    for (int i = 0; i < a->nov; i++) {
        const Ov *o = &a->ov[i];
        fe_blit_cell(px, &a->font, o->ci, o->col, o->row, o->scale, r3_pal[o->fg], r3_pal[o->bg]);
    }
}

static uint32_t ov_signature(const App *a) {
    uint32_t h = 2166136261u ^ a->font.gen;
    for (int i = 0; i < a->nov; i++) {
        const Ov *o = &a->ov[i];
        h = (h ^ (uint32_t)(o->col * 64 + o->row)) * 16777619u;
        h = (h ^ (uint32_t)o->ci) * 16777619u;
        h = (h ^ (uint32_t)(o->scale * 256 + o->fg * 3 + o->bg)) * 16777619u;
    }
    return h;
}

/* ---- drawing: the list ----------------------------------------------------------------------------------------------------------- */

static void put_text(R3Screen *s, int col, int row, const char *t, int w, int style) { r3_text_fit(s, col, row, t, w, R3_ST(style)); }

static void draw_info_box(App *a, R3Screen *s, int col, int row, int w, const FeGroup *g) {
    FeFont *f = &a->font;
    r3_box(s, col, row, w, 20, &(R3Box){0, "Glyph", NULL, NULL, 0});
    if (!g) { put_text(s, col + 2, row + 2, "Nothing here.", w - 4, R3_ST_BODY); return; }
    char t[96];
    put_text(s, col + 2, row + 1, g->name, w - 4, R3_ST_STRONG);
    snprintf(t, sizeof t, "U+%04X%s", (unsigned)f->cp[g->ci[0]], g->n > 1 ? " +next" : "");
    put_text(s, col + 2, row + 2, t, w - 4, R3_ST_BODY);
    snprintf(t, sizeof t, "%d cell%s, %dx%d px", g->n, g->n > 1 ? "s" : "", f->w * g->n, f->h);
    put_text(s, col + 2, row + 3, t, w - 4, R3_ST_BODY);
    int ch = fe_group_changed(f, g);
    put_text(s, col + 2, row + 4, ch ? "Changed" : "As built", w - 4, ch ? R3_ST_WARN : R3_ST_OK);
    put_text(s, col + 2, row + 6, "Real size", w - 4, R3_ST_HINT);
    ov_group(a, s, col + 2, row + 7, g, 1, R3_ST(R3_ST_STRONG));
    put_text(s, col + 2, row + 9, "Large", w - 4, R3_ST_HINT);
    ov_group(a, s, col + 2, row + 10, g, 3, R3_ST(R3_ST_STRONG));
    put_text(s, col + 2, row + 14, "In text", w - 4, R3_ST_HINT);
    {   /* H x [glyph] x H */
        int x = col + 2;
        const char *pre = "Hx", *post = "xH";
        for (int i = 0; pre[i]; i++) { r3_put(s, x, row + 15, 32, R3_ST(R3_ST_BODY)); ov_add(a, x++, row + 15, fe_find(f, (unsigned char)pre[i]), 1, R3_ST(R3_ST_BODY)); }
        x += ov_group(a, s, x, row + 15, g, 1, R3_ST(R3_ST_STRONG));
        for (int i = 0; post[i]; i++) { r3_put(s, x, row + 15, 32, R3_ST(R3_ST_BODY)); ov_add(a, x++, row + 15, fe_find(f, (unsigned char)post[i]), 1, R3_ST(R3_ST_BODY)); }
    }
    int same = fe_same_picture(f, g->ci[0]);
    if (same > 0) { snprintf(t, sizeof t, "Same as %d more", same); put_text(s, col + 2, row + 17, t, w - 4, R3_ST_BODY); }
}

static void compose_browse(App *a, R3Screen *s) {
    FeFont *f = &a->font;
    int p = a->panel, n = a->nlist[p], sel = a->sel[p], page, pages;
    char t[160];
    r3_clear(s);
    snprintf(t, sizeof t, "%s%s", f->title, a->save_failed ? "  NOT SAVED" : "");
    r3_top_bar(s, APP, t);
    r3_list_window(n, sel, LIST_PER, &page, &pages);
    snprintf(t, sizeof t, "Page %d/%d", page, pages);
    r3_box(s, 0, 1, 42, 20, &(R3Box){1, PN_TITLE[p], pages > 1 ? t : NULL, "A edit  Fn panel  Y more", 0});
    if (n == 0) {
        if (p == PN_CHANGED) put_text(s, 2, 3, "Nothing is changed yet.", 38, R3_ST_BODY);
        else if (f->ng == 0) { put_text(s, 2, 3, "This font has no names.", 38, R3_ST_BODY); put_text(s, 2, 4, "Fn: Code points.", 38, R3_ST_HINT); }
        else put_text(s, 2, 3, "Nothing here.", 38, R3_ST_BODY);
    }
    for (int i = (page - 1) * LIST_PER; i < n && i < page * LIST_PER; i++) {
        int row = 2 + i - (page - 1) * LIST_PER, st = i == sel ? R3_ST_SELECT : R3_ST_BODY;
        FeGroup g;
        entry_group(a, p, i, &g);
        r3_fill(s, 1, row, 40, 1, 32, R3_ST(st));
        if (p == PN_CODES) {       /* a code point row: its own cell only */
            int ci = a->list[p][i];
            FeGroup one;
            fe_group_cp(f, ci, &one);
            r3_put(s, 2, row, fe_cell_changed(f, ci) ? '*' : ' ', R3_ST(st));
            ov_group(a, s, 4, row, &one, 1, R3_ST(st));
            int of = 1, part = fe_part(f, ci, &of);
            if (part) snprintf(t, sizeof t, "U+%04X %s %d/%d", (unsigned)f->cp[ci], f->groups[f->group_of[ci]].name, part, of);
            else snprintf(t, sizeof t, "U+%04X", (unsigned)f->cp[ci]);
            r3_text_fit(s, 6, row, t, 34, R3_ST(st));
        } else {
            r3_put(s, 2, row, fe_group_changed(f, &g) ? '*' : ' ', R3_ST(st));
            int w = ov_group(a, s, 4, row, &g, 1, R3_ST(st));
            r3_text_fit(s, 5 + (w > 2 ? w : 2), row, g.name, 41 - 5 - (w > 2 ? w : 2), R3_ST(st));
        }
    }
    FeGroup cur;
    draw_info_box(a, s, 42, 1, 22, entry_group(a, p, sel, &cur) ? &cur : NULL);
    snprintf(t, sizeof t, "%s: %d of %d", PN_TITLE[p], n ? sel + 1 : 0, n);
    char st[96];
    snprintf(st, sizeof st, "Panel %d/%d   %d changed", p + 1, PN_COUNT, fe_count_changed(f));
    R3Cmd cmd = {a->reply[0] ? a->reply : NULL, a->reply_style, '>', t, 0, "A edit  X revert  Y more  Fn panel", st, 0};
    r3_command_line(s, &cmd);
}

/* ---- drawing: the editor ---------------------------------------------------------------------------------------------------------- */

static int canvas_pw(const FeCanvas *c) { return fe_cw(c) * 2 <= 42 ? 2 : 1; }     /* text cells a pixel */

static void draw_canvas(App *a, R3Screen *s) {
    const FeCanvas *c = &a->c;
    int cw = fe_cw(c), pw = canvas_pw(c), col0 = 2;
    FeCanvas mask;
    memset(&mask, 0, sizeof mask);
    mask.n = c->n; mask.w = c->w; mask.h = c->h;
    if (a->anchored) fe_shape(&mask, a->tool, a->ax, a->ay, a->cx, a->cy, 1);
    int cap = a->font.base - 12, xh = a->font.base - 8, bl = a->font.base - 1;
    for (int y = 0; y < c->h; y++) {
        int guide = a->guides && (y == cap || y == xh || y == bl);
        for (int x = 0; x < cw; x++) {
            int k = x / c->w, on = c->p[y][x], pend = a->anchored && mask.p[y][x], here = x == a->cx && y == a->cy;
            int st = on ? (k % 2 ? R3_ST_OK : R3_ST_STRONG) : (k % 2 ? R3_ST_HINT : R3_ST_BODY);
            unsigned char ch0 = on ? 219 : guide ? 196 : 250, ch1 = on ? 219 : guide ? 196 : 32;
            if (pend) { st = R3_ST_WARN; ch0 = ch1 = 178; }
            if (here) { st = R3_ST_WARN; if (on) ch0 = ch1 = 219; else { ch0 = 222; ch1 = 221; } }
            if (pw == 1) { r3_put(s, col0 + x, 1 + y, here && !on ? 254 : ch0, R3_ST(st)); continue; }
            r3_put(s, col0 + 2 * x, 1 + y, ch0, R3_ST(st));
            r3_put(s, col0 + 2 * x + 1, 1 + y, ch1, R3_ST(st));
        }
    }
    if (a->guides) {
        if (cap >= 0 && cap < c->h) r3_put(s, 0, 1 + cap, 'C', R3_ST(R3_ST_HINT));
        if (xh >= 0 && xh < c->h) r3_put(s, 0, 1 + xh, 'x', R3_ST(R3_ST_HINT));
        if (bl >= 0 && bl < c->h) r3_put(s, 0, 1 + bl, 'b', R3_ST(R3_ST_HINT));
    }
}

static void compose_edit(App *a, R3Screen *s) {
    FeFont *f = &a->font;
    char t[160], title[96];
    r3_clear(s);
    snprintf(t, sizeof t, "%s%s", a->epanel == EP_DRAW ? "Draw" : "Sample", a->save_failed ? "  NOT SAVED" : "");
    snprintf(title, sizeof title, "%s", a->g.name);
    r3_top_bar(s, title, t);
    if (a->epanel == EP_DRAW) {
        draw_canvas(a, s);
        int bx = 44, w = 20;
        r3_box(s, bx, 1, w, 20, &(R3Box){0, "Glyph", NULL, NULL, 0});
        put_text(s, bx + 2, 2, a->g.name, w - 4, R3_ST_STRONG);
        snprintf(t, sizeof t, "U+%04X%s %dx%d", (unsigned)f->cp[a->g.ci[0]], a->g.n > 1 ? "+" : "", f->w * a->g.n, f->h);
        put_text(s, bx + 2, 3, t, w - 4, R3_ST_BODY);
        snprintf(t, sizeof t, "Tool: %s%s", FE_TOOL_NAME[a->tool], a->anchored ? " +" : "");
        put_text(s, bx + 2, 4, t, w - 4, R3_ST_HINT);
        int cell = a->cx / f->w + 1;
        snprintf(t, sizeof t, "x%d y%d cell %d/%d", a->cx, a->cy, cell, a->g.n);
        put_text(s, bx + 2, 5, t, w - 4, R3_ST_BODY);
        snprintf(t, sizeof t, "Pixel %s", fe_get(&a->c, a->cx, a->cy) ? "ink" : "paper");
        put_text(s, bx + 2, 6, t, w - 4, R3_ST_BODY);
        put_text(s, bx + 2, 7, fe_group_changed(f, &a->g) ? "Changed" : "As built", w - 4, fe_group_changed(f, &a->g) ? R3_ST_WARN : R3_ST_OK);
        put_text(s, bx + 2, 9, "Real size", w - 4, R3_ST_HINT);
        ov_group(a, s, bx + 2, 10, &a->g, 1, R3_ST(R3_ST_STRONG));
        {
            int x = bx + 2;
            const char *pre = "Hx", *post = "xH";
            for (int i = 0; pre[i]; i++) { r3_put(s, x, 11, 32, R3_ST(R3_ST_BODY)); ov_add(a, x++, 11, fe_find(f, (unsigned char)pre[i]), 1, R3_ST(R3_ST_BODY)); }
            x += ov_group(a, s, x, 11, &a->g, 1, R3_ST(R3_ST_STRONG));
            for (int i = 0; post[i]; i++) { r3_put(s, x, 11, 32, R3_ST(R3_ST_BODY)); ov_add(a, x++, 11, fe_find(f, (unsigned char)post[i]), 1, R3_ST(R3_ST_BODY)); }
        }
        put_text(s, bx + 2, 13, "Large", w - 4, R3_ST_HINT);
        ov_group(a, s, bx + 2, 14, &a->g, 3, R3_ST(R3_ST_STRONG));
        put_text(s, bx + 2, 18, "A ink  X erase", w - 4, R3_ST_BODY);
        put_text(s, bx + 2, 19, "Y tool B back", w - 4, R3_ST_BODY);
    } else {
        r3_box(s, 0, 1, 64, 20, &(R3Box){1, "Sample", NULL, "Fn draw", 0});
        put_text(s, 2, 2, "Twice the size", 30, R3_ST_HINT);
        int x = 2;
        for (int i = 0; i < 3; i++) x += ov_group(a, s, x, 3, &a->g, 2, R3_ST(R3_ST_STRONG)) + 1;
        put_text(s, 2, 6, "Joined, as in a row", 30, R3_ST_HINT);
        x = 2;
        for (int i = 0; i < 8 && x + a->g.n < 62; i++) x += ov_group(a, s, x, 7, &a->g, 1, R3_ST(R3_ST_STRONG));
        put_text(s, 2, 9, "In text", 30, R3_ST_HINT);
        ov_text(a, s, 2, 10, "The quick brown fox jumps over the lazy dog", R3_ST(R3_ST_BODY));
        ov_group(a, s, 46, 10, &a->g, 1, R3_ST(R3_ST_STRONG));
        ov_text(a, s, 2, 11, "THE QUICK BROWN FOX 0123456789 {}[]()<>=+-*/", R3_ST(R3_ST_BODY));
        ov_group(a, s, 46, 11, &a->g, 1, R3_ST(R3_ST_STRONG));
        put_text(s, 2, 13, "Three times the size", 30, R3_ST_HINT);
        x = 2;
        for (int i = 0; i < 2; i++) x += ov_group(a, s, x, 14, &a->g, 3, R3_ST(R3_ST_STRONG)) + 1;
    }
    snprintf(t, sizeof t, "%s: %s", a->g.name, FE_TOOL_NAME[a->tool]);
    char st[96];
    snprintf(st, sizeof st, "Panel %d/%d  undo %d", a->epanel + 1, EP_COUNT, a->hist.nu);
    R3Cmd cmd = {a->reply[0] ? a->reply : NULL, a->reply_style, '>', t, 0, "Sel/Sta prev/next  Y tool  L2+R2 commands  L1+R1 menu", st, 0};
    r3_command_line(s, &cmd);
}

/* ---- drawing: overlays ------------------------------------------------------------------------------------------------------------ */

static void build_commands(App *a) {
    int n = 0;
    if (a->screen == SC_BROWSE) {
        FeGroup g;
        int has = entry_group(a, a->panel, a->sel[a->panel], &g);
        if (has) { a->cmd_id[n++] = C_EDIT; if (g.n > 1 || a->panel == PN_CODES) a->cmd_id[n++] = C_EDIT_CELL; a->cmd_id[n++] = C_REVERT_G; }
        a->cmd_id[n++] = C_REVERT_ALL;
        a->cmd_id[n++] = C_SAVE;
    } else {
        int multi = a->g.n >= 2;
        static const int base[] = {C_UNDO, C_REDO, C_TOOL, C_FLIPH, C_FLIPV, C_INVERT, C_CLEAR, C_SHIFT_L, C_SHIFT_R, C_SHIFT_U, C_SHIFT_D};
        for (size_t i = 0; i < sizeof base / sizeof base[0]; i++) a->cmd_id[n++] = base[i];
        if (multi) { a->cmd_id[n++] = C_MIRR_R; a->cmd_id[n++] = C_MIRR_L; a->cmd_id[n++] = C_COPY_R; a->cmd_id[n++] = C_COPY_L; a->cmd_id[n++] = C_SWAP; }
        a->cmd_id[n++] = C_REVERT_G;
        a->cmd_id[n++] = C_GUIDES;
        a->cmd_id[n++] = C_SAVE;
    }
    a->ncmd = n;
    if (a->cmd_sel >= n) a->cmd_sel = 0;
}

static void cmd_label(const App *a, int id, char *label, size_t lc, char *value, size_t vc) {
    const char *l = "?";
    value[0] = 0;
    switch (id) {
    case C_EDIT: l = "Edit the glyph"; break;
    case C_EDIT_CELL: l = "Edit this cell alone"; break;
    case C_REVERT_G: l = "Revert the glyph"; break;
    case C_REVERT_ALL: l = "Revert every change"; break;
    case C_SAVE: l = "Save now"; break;
    case C_UNDO: l = "Undo"; snprintf(value, vc, "%d", a->hist.nu); break;
    case C_REDO: l = "Redo"; snprintf(value, vc, "%d", a->hist.nr); break;
    case C_TOOL: l = "Next tool"; snprintf(value, vc, "%s", FE_TOOL_NAME[a->tool]); break;
    case C_FLIPH: l = a->g.n > 1 ? "Flip left-right (the whole glyph)" : "Flip left-right"; break;
    case C_FLIPV: l = "Flip up-down"; break;
    case C_INVERT: l = "Invert"; break;
    case C_CLEAR: l = "Clear"; break;
    case C_SHIFT_L: l = "Shift left 1 px"; break;
    case C_SHIFT_R: l = "Shift right 1 px"; break;
    case C_SHIFT_U: l = "Shift up 1 px"; break;
    case C_SHIFT_D: l = "Shift down 1 px"; break;
    case C_MIRR_R: l = "Mirror first cell onto last"; break;
    case C_MIRR_L: l = "Mirror last cell onto first"; break;
    case C_COPY_R: l = "Copy first cell onto last"; break;
    case C_COPY_L: l = "Copy last cell onto first"; break;
    case C_SWAP: l = "Swap first and last cell"; break;
    case C_GUIDES: l = "Guides"; snprintf(value, vc, "%s", a->guides ? "on" : "off"); break;
    default: break;
    }
    snprintf(label, lc, "%s", l);
}

static void draw_overlay(App *a, R3Screen *s) {
    if (a->overlay == OV_MENU) r3_mm_draw(s, &a->mm, r3_themes[a->theme].name);
    else if (a->overlay == OV_ABOUT) {
        static const char *const lines[] = {"Edits the R3i6nos Mono font and other bitmap fonts,", "with its multi-cell glyphs first.",
            "Font: CC BY-SA 4.0 (theme/font/). App: MIT.", "Untested on a real R36S.", NULL};
        r3_about_draw(s, APP, VERSION, BUILD_LINE, lines);
    } else if (a->overlay == OV_THEME) r3_theme_pick_draw(s, a->theme_sel, a->theme);
    else if (a->overlay == OV_KEYMAP) r3_km_draw(s, &a->km);
    else if (a->overlay == OV_CMDS) {
        R3Item items[24];
        memset(items, 0, sizeof items);
        for (int i = 0; i < a->ncmd; i++) cmd_label(a, a->cmd_id[i], items[i].label, sizeof items[i].label, items[i].value, sizeof items[i].value);
        int h = a->ncmd + 2 > R3_DISPLAY_ROWS ? R3_DISPLAY_ROWS : a->ncmd + 2;
        R3List l = {12, R3_DISPLAY_TOP + (R3_DISPLAY_ROWS - h) / 2, 40, h, a->screen == SC_EDIT ? "Commands" : "More", "A do it  B close", items, a->ncmd, a->cmd_sel, {NULL}};
        r3_list_dialog(s, &l);
    } else if (a->overlay == OV_CONFIRM) {
        R3Item items[2] = {{"No", ""}, {"Yes", ""}};
        const char *what = a->confirm_id == C_REVERT_ALL ? "Revert every change to every glyph?" : "Revert this glyph to how the font was built?";
        r3_box(s, 8, 7, 48, 8, &(R3Box){1, "Sure?", NULL, "A choose  B no", 0});
        put_text(s, 11, 8, what, 42, R3_ST_STRONG);
        put_text(s, 11, 9, "Undo brings it back until you quit.", 42, R3_ST_BODY);
        R3List l = {16, 11, 32, 4, NULL, NULL, items, 2, a->confirm_sel, {NULL}};
        r3_list_dialog(s, &l);
    }
}

static void compose(App *a, R3Screen *s) {
    a->nov = 0;
    if (a->screen == SC_BROWSE) compose_browse(a, s); else compose_edit(a, s);
    if (a->overlay) {
        draw_overlay(a, s);
        if (a->overlay == OV_KEYMAP) ov_clip(a, 2, 1, 60, 20); else ov_clip(a, 8, 1, 48, 20);
    }
}

/* ---- the editor: opening, moving, drawing --------------------------------------------------------------------------------------- */

static void open_group(App *a, const FeGroup *g) {
    a->g = *g;
    fe_canvas_load(&a->font, &a->g, &a->c);
    a->screen = SC_EDIT;
    a->cx = fe_cw(&a->c) / 2;
    a->cy = a->font.h / 2;
    a->anchored = 0;
    a->stroke = 0;
    a->force = 1;
}

static void open_entry(App *a) {
    FeGroup g;
    if (entry_group(a, a->panel, a->sel[a->panel], &g)) open_group(a, &g);
}

static void reload_canvas(App *a) { fe_canvas_load(&a->font, &a->g, &a->c); a->force = 1; }

/* A discrete change: history, the change, a save when something changed; with nothing changed the history entry is dropped. */
static int do_edit(App *a, void (*fn)(FeCanvas *, void *), void *arg) {
    fe_hist_push(&a->hist, &a->font, &a->g);
    fn(&a->c, arg);
    if (!fe_canvas_store(&a->font, &a->g, &a->c)) { a->hist.nu--; reload_canvas(a); return 0; }
    autosave(a);
    a->force = 1;
    return 1;
}

static void op_flip_h(FeCanvas *c, void *x) { (void)x; fe_flip_h(c); }
static void op_flip_v(FeCanvas *c, void *x) { (void)x; fe_flip_v(c); }
static void op_invert(FeCanvas *c, void *x) { (void)x; fe_invert(c); }
static void op_clear(FeCanvas *c, void *x) { (void)x; fe_clear(c); }
static void op_shift(FeCanvas *c, void *x) { const int *d = x; fe_shift(c, d[0], d[1]); }
static void op_cells(FeCanvas *c, void *x) { fe_cells_cmd(c, *(int *)x); }
static void op_flood(FeCanvas *c, void *x) { const int *d = x; fe_flood(c, d[0], d[1], d[2]); }
static void op_shape(FeCanvas *c, void *x) { const int *d = x; fe_shape(c, d[0], d[1], d[2], d[3], d[4], d[5]); }

static void stroke_end(App *a) {
    if (!a->stroke) return;
    a->stroke = 0;
    FeCanvas before = a->hist.undo[a->hist.nu - 1].c;
    if (a->hist.nu > 0 && !memcmp(&before, &a->c, sizeof before)) { a->hist.nu--; return; }   /* the stroke changed nothing */
    fe_canvas_store(&a->font, &a->g, &a->c);
    autosave(a);
}

static void pen_at(App *a, int v) {
    if (!fe_in(&a->c, a->cx, a->cy)) return;
    fe_put(&a->c, a->cx, a->cy, v);
    fe_canvas_store(&a->font, &a->g, &a->c);      /* shows at once; the save is at the end of the stroke */
    a->force = 1;
}

static void move_cursor(App *a, int dx, int dy) {
    int cw = fe_cw(&a->c);
    a->cx = a->cx + dx < 0 ? 0 : a->cx + dx >= cw ? cw - 1 : a->cx + dx;
    a->cy = a->cy + dy < 0 ? 0 : a->cy + dy >= a->c.h ? a->c.h - 1 : a->cy + dy;
    if (a->stroke) pen_at(a, a->stroke == 1);
    a->force = 1;
}

/* A or X in the editor: v = 1 ink, 0 erase. */
static void act_paint(App *a, int v) {
    if (a->tool == FE_TOOL_PEN) {
        if (!a->stroke) { fe_hist_push(&a->hist, &a->font, &a->g); a->stroke = v ? 1 : 2; }
        pen_at(a, v);
    } else if (a->tool == FE_TOOL_FILL) {
        int d[3] = {a->cx, a->cy, v};
        do_edit(a, op_flood, d);
    } else if (!a->anchored) { a->anchored = 1; a->ax = a->cx; a->ay = a->cy; say(a, R3_ST_HINT, "Corner at %d,%d. Move, then A inks or X erases; B cancels.", a->cx, a->cy); }
    else {
        int d[6] = {a->tool, a->ax, a->ay, a->cx, a->cy, v};
        a->anchored = 0;
        do_edit(a, op_shape, d);
    }
    a->force = 1;
}

static void leave_edit(App *a) {
    stroke_end(a);
    a->screen = SC_BROWSE;
    a->anchored = 0;
    build_lists(a);
    a->force = 1;
}

static void step_glyph(App *a, int d) {
    stroke_end(a);
    int n = a->nlist[a->panel];
    if (n <= 1) { say(a, R3_ST_HINT, "That is the only glyph in this list."); return; }
    a->sel[a->panel] = (a->sel[a->panel] + n + d) % n;
    open_entry(a);
}

static void run_command(App *a, int id) {
    FeFont *f = &a->font;
    switch (id) {
    case C_EDIT: open_entry(a); break;
    case C_EDIT_CELL: {
        FeGroup g, one;
        if (!entry_group(a, a->panel, a->sel[a->panel], &g)) break;
        fe_group_cp(f, a->panel == PN_CODES ? a->list[a->panel][a->sel[a->panel]] : g.ci[0], &one);
        open_group(a, &one);
        break;
    }
    case C_REVERT_G:
    case C_REVERT_ALL: a->overlay = OV_CONFIRM; a->confirm_id = id; a->confirm_sel = 0; return;
    case C_SAVE:
        if (autosave(a) == 0) say(a, R3_ST_OK, "Saved: %s", a->edits_path);
        break;
    case C_UNDO: case C_REDO: {
        stroke_end(a);
        const FeGroup *g = id == C_UNDO ? fe_hist_undo(&a->hist, f) : fe_hist_redo(&a->hist, f);
        if (!g) { say(a, R3_ST_HINT, "Nothing to %s.", id == C_UNDO ? "undo" : "redo"); break; }
        if (a->screen == SC_EDIT) { if (strcmp(g->name, a->g.name)) { a->g = *g; } reload_canvas(a); }
        autosave(a);
        say(a, R3_ST_OK, "%s: %s", id == C_UNDO ? "Undone" : "Redone", g->name);
        break;
    }
    case C_TOOL: a->tool = (a->tool + 1) % FE_TOOLS; a->anchored = 0; say(a, R3_ST_HINT, "Tool: %s", FE_TOOL_NAME[a->tool]); break;
    case C_FLIPH: do_edit(a, op_flip_h, NULL); break;
    case C_FLIPV: do_edit(a, op_flip_v, NULL); break;
    case C_INVERT: do_edit(a, op_invert, NULL); break;
    case C_CLEAR: do_edit(a, op_clear, NULL); break;
    case C_SHIFT_L: case C_SHIFT_R: case C_SHIFT_U: case C_SHIFT_D: {
        int d[2] = {id == C_SHIFT_L ? -1 : id == C_SHIFT_R ? 1 : 0, id == C_SHIFT_U ? -1 : id == C_SHIFT_D ? 1 : 0};
        do_edit(a, op_shift, d);
        break;
    }
    case C_MIRR_R: case C_MIRR_L: case C_COPY_R: case C_COPY_L: case C_SWAP: {
        int cmd = id == C_MIRR_R ? FE_CELL_MIRROR_RIGHT : id == C_MIRR_L ? FE_CELL_MIRROR_LEFT : id == C_COPY_R ? FE_CELL_COPY_RIGHT : id == C_COPY_L ? FE_CELL_COPY_LEFT : FE_CELL_SWAP;
        do_edit(a, op_cells, &cmd);
        break;
    }
    case C_GUIDES: a->guides = !a->guides; settings_save(a); a->force = 1; break;
    default: break;
    }
}

static void confirm_run(App *a) {
    FeFont *f = &a->font;
    if (a->confirm_id == C_REVERT_G) {
        FeGroup g = a->screen == SC_EDIT ? a->g : (FeGroup){0};
        if (a->screen == SC_BROWSE && !entry_group(a, a->panel, a->sel[a->panel], &g)) return;
        fe_hist_push(&a->hist, f, &g);
        if (!fe_revert(f, &g)) { a->hist.nu--; say(a, R3_ST_HINT, "%s is as built already.", g.name); return; }
        if (a->screen == SC_EDIT) reload_canvas(a);
        autosave(a);
        build_lists(a);
        say(a, R3_ST_OK, "Reverted %s.", g.name);
    } else {
        int n = 0;
        for (int i = 0; i < f->n; i++) {      /* one history entry a changed group: undo goes back through them */
            if (!fe_cell_changed(f, i)) continue;
            FeGroup g;
            fe_group_for(f, i, &g);
            fe_hist_push(&a->hist, f, &g);
            n += fe_revert(f, &g);
        }
        if (a->screen == SC_EDIT) reload_canvas(a);
        autosave(a);
        build_lists(a);
        say(a, R3_ST_OK, "Reverted %d cell(s).", n);
    }
    a->force = 1;
}

/* ---- the report (rule 9, 14, 18) ------------------------------------------------------------------------------------------------- */

static void report_bar_window(App *a, R3ReportBar *bar) {
    bar->video = a->loop ? a->loop->video : NULL;
    bar->r = a->loop ? a->loop->r : NULL;
    bar->app = APP;
}

static void drain_events(void) { SDL_Event e; while (SDL_PollEvent(&e)) { /* a report in progress takes no input */ } }

static void idle_turn(void *arg) {
    /* one pass of the main loop with nothing pressed: compose, draw only if something changed, nap (100 ms, as nap_ms) */
    App *a = arg;
    R3Screen *s = &a->loop->screen;
    drain_events();
    compose(a, s);
    r3_video_draw(a->loop->video, a->loop->r, s, 0);
    SDL_Delay(100);
}

static double ms_now(void) { return r3_report_ms(); }

static void write_report(App *a, const char *how) {
    char dir[PATH_MAX];
    app_data_dir(dir, sizeof dir);
    R3Report r;
    memset(&r, 0, sizeof r);
    r.quiet = 1;
    if (r3_report_open_named(&r, dir, "r36s-report.txt", APP, VERSION, "r36s-fontedit", how, "r3i6nos") != 0) { say(a, R3_ST_ERROR, "Could not write the report in %s", dir); return; }
    R3ReportBar bar;
    report_bar_window(a, &bar);
    r3_reportbar_begin(&bar, &r, REPORT_STEPS);
    FeFont *f = &a->font;

    r3_report_at(&r, 1, "System");
    r3_report_system(&r);

    r3_report_at(&r, 2, "The font");
    r3_report_section(&r, "font");
    r3_report_line(&r, R3_INFO, "font", "%s (%s)", f->title, f->builtin ? "R3i6nos Mono, compiled in" : f->path);
    r3_report_line(&r, R3_INFO, "code points", "%d", f->n);
    int multi = 0, cells = 0, biggest = 0;
    for (int i = 0; i < f->ng; i++) { if (f->groups[i].n >= 2) { multi++; cells += f->groups[i].n; } if (f->groups[i].n > biggest) biggest = f->groups[i].n; }
    r3_report_line(&r, R3_INFO, "names", "%d, of which %d multi-cell (%d cells), the longest %d cells", f->ng, multi, cells, biggest);
    r3_report_line(&r, f->builtin ? (f->ng == 884 && f->n == 2919 ? R3_PASS : R3_FAIL) : R3_SKIP, "font tables", f->builtin ? "2919 code points and 884 names expected" : "not R3i6nos Mono");
    r3_report_line(&r, R3_INFO, "changed cells", "%d", fe_count_changed(f));

    r3_report_at(&r, 3, "Saving");
    r3_report_section(&r, "files");
    r3_report_line(&r, R3_INFO, "data folder", "%s", a->data_dir);
    {
        char probe[PATH_MAX + 32], err[200];
        snprintf(probe, sizeof probe, "%s/.report-probe.txt", a->data_dir);
        FeFont copy;
        double t0 = ms_now();
        int ok = f->builtin ? fe_edits_save(f, probe) == 0 && fe_load_builtin(&copy) == 0 && fe_edits_load(&copy, probe, err, sizeof err) >= 0 : 0;
        if (f->builtin) {
            int same = ok && !memcmp(copy.rows, f->rows, (size_t)f->n * sizeof *f->rows);
            r3_report_line(&r, same ? R3_PASS : R3_FAIL, "edits round trip", "%s, %.1f ms (written, read back, every pixel compared)", same ? "same" : ok ? "different" : "failed", ms_now() - t0);
            if (ok) fe_free(&copy);
            remove(probe);
        } else r3_report_line(&r, R3_SKIP, "edits round trip", "a BDF is saved as a BDF");
    }
    r3_report_card(&r, a->data_dir);

    r3_report_at(&r, 4, "Speed");
    r3_report_section(&r, "speed");
    {
        uint32_t *px = malloc((size_t)R3_W * R3_H * sizeof *px);
        R3Screen s;
        double t0 = ms_now();
        int frames = 20;
        unsigned long sum = 0;
        for (int i = 0; i < frames && px; i++) { compose(a, &s); r3_render(&s, px); sum += px[(size_t)(i * 7919) % ((size_t)R3_W * R3_H)]; }
        double per = px ? (ms_now() - t0) / frames : -1;
        (void)sum;
        r3_report_line(&r, px && per < 12.0 ? R3_PASS : px ? R3_FAIL : R3_SKIP, "full redraw", "%.2f ms a frame (goal under 12 ms)", per);
        FeCanvas c;
        fe_canvas_load(f, a->g.n ? &a->g : &f->groups[0], &c);
        t0 = ms_now();
        for (int i = 0; i < 2000; i++) { fe_flood(&c, 0, 0, i & 1); fe_flip_h(&c); }
        r3_report_line(&r, R3_INFO, "canvas ops", "%.3f ms for a fill and a flip", (ms_now() - t0) / 2000);
        free(px);
    }

    r3_report_at(&r, 5, "Memory and power");
    r3_report_memory(&r);
    r3_report_power_section(&r);
    R3PowerResult idle;
    memset(&idle, 0, sizeof idle);
    if (a->loop) {
        r3_report_at(&r, 6, "Power: idle, nothing pressed");
        r3_report_power_idle(&r, r3_report_power_seconds(10), idle_turn, a, &idle);
        r3_report_power_advice(&r, &idle, NULL);
    } else {
        r3_report_at(&r, 6, "Power: no window");
        r3_report_line(&r, R3_SKIP, "idle", "no window (--report): open Device report in the app to measure the app itself");
    }
    r3_reportbar_end(&r);
    int pass = r.pass, fail = r.fail, skip = r.skip;
    int closed = r3_report_close(&r);
    a->report_missed = r.step_missed;
    if (closed == 0) say(a, fail ? R3_ST_WARN : R3_ST_OK, "Written: %s/r36s-report.txt (PASS %d FAIL %d SKIP %d)", a->data_dir, pass, fail, skip);
    else say(a, R3_ST_ERROR, "Could not finish the report in %s", dir);
    a->force = 1;
}

/* ---- input ---------------------------------------------------------------------------------------------------------------------- */

static const R3KmRow KM_BROWSE[] = {
    {"Up/Down", "Move through the list"}, {"Left/Right", "Previous / next panel"}, {"R1+Up/Down", "Page up / down"}, {"R1+Left/Right", "First / last entry"},
    {"A", "Edit the glyph (a name opens whole)"}, {"X", "Revert the glyph (asked first)"}, {"Y", "More: edit one cell, revert, save"}, {"Fn", "Next panel: multi-cell, names, code points, changed"},
};
static const R3KmRow KM_EDIT[] = {
    {"D-pad", "Move the cursor; hold A or X to draw as you go"}, {"A", "Ink (Pen); set a corner, then finish (Line, Box); fill"}, {"X", "Erase, the same way"},
    {"Y", "Next tool: Pen, Line, Box, Rectangle, Fill"}, {"B", "Cancel a corner; else back to the list"}, {"Select", "Previous glyph in the list"}, {"Start", "Next glyph in the list"},
    {"R1+Up/Down", "Cursor 5 rows"}, {"R1+Left/Right", "Cursor to the left / right edge"}, {"Fn", "Draw / Sample"}, {"L2+R2", "Commands: undo, redo, flip, shift, mirror, copy..."},
};
static const R3KmPage KM_PAGES[] = {{"The list", KM_BROWSE, (int)(sizeof KM_BROWSE / sizeof KM_BROWSE[0])}, {"The editor", KM_EDIT, (int)(sizeof KM_EDIT / sizeof KM_EDIT[0])}};

static void list_action(App *a, int action) {
    int p = a->panel, n = a->nlist[p];
    switch (action) {
    case R3_UP: case R3_DOWN: case R3_PAGEUP: case R3_PAGEDOWN: case R3_HOME: case R3_END:
        a->sel[p] = r3_list_step(a->sel[p], n, LIST_PER, action);
        break;
    case R3_LEFT: a->panel = (p + PN_COUNT - 1) % PN_COUNT; break;
    case R3_RIGHT: case R3_PANEL_NEXT: a->panel = (p + 1) % PN_COUNT; break;
    case R3_PANEL_FIRST: a->panel = PN_MULTI; break;
    case R3_PANEL_PREV: a->panel = (p + PN_COUNT - 1) % PN_COUNT; break;
    case R3_ACCEPT: if (n) open_entry(a); break;
    case R3_X: if (n) { a->confirm_id = C_REVERT_G; a->confirm_sel = 0; a->overlay = OV_CONFIRM; } break;
    case R3_Y: case R3_CONTEXT: a->cmd_sel = 0; build_commands(a); a->overlay = OV_CMDS; break;
    default: break;
    }
    a->force = 1;
}

static void edit_action(App *a, int action) {
    switch (action) {
    case R3_UP: move_cursor(a, 0, -1); break;
    case R3_DOWN: move_cursor(a, 0, 1); break;
    case R3_LEFT: move_cursor(a, -1, 0); break;
    case R3_RIGHT: move_cursor(a, 1, 0); break;
    case R3_PAGEUP: move_cursor(a, 0, -5); break;
    case R3_PAGEDOWN: move_cursor(a, 0, 5); break;
    case R3_HOME: move_cursor(a, -fe_cw(&a->c), 0); break;
    case R3_END: move_cursor(a, fe_cw(&a->c), 0); break;
    case R3_ACCEPT: if (a->epanel == EP_DRAW) act_paint(a, 1); break;
    case R3_X: if (a->epanel == EP_DRAW) act_paint(a, 0); break;
    case R3_Y: run_command(a, C_TOOL); break;
    case R3_BACK:
        if (a->anchored) { a->anchored = 0; say(a, R3_ST_HINT, "Corner cancelled."); }
        else leave_edit(a);
        break;
    case R3_SELECT: step_glyph(a, -1); break;
    case R3_START: step_glyph(a, 1); break;
    case R3_PANEL_NEXT: stroke_end(a); a->epanel = (a->epanel + 1) % EP_COUNT; break;
    case R3_PANEL_FIRST: a->epanel = EP_DRAW; break;
    case R3_PANEL_PREV: a->epanel = (a->epanel + EP_COUNT - 1) % EP_COUNT; break;
    case R3_CONTEXT: stroke_end(a); a->cmd_sel = 0; build_commands(a); a->overlay = OV_CMDS; break;
    default: break;
    }
    a->force = 1;
}

static void overlay_key(App *a, int action) {
    if (action == R3_NONE) return;
    if (a->overlay == OV_ABOUT) { a->overlay = OV_NONE; a->force = 1; return; }
    if (a->overlay == OV_KEYMAP) { if (r3_km_key(&a->km, action)) a->overlay = OV_NONE; a->force = 1; return; }
    if (a->overlay == OV_THEME) {
        if (action == R3_ACCEPT) {
            a->theme = a->theme_sel;
            r3_theme_apply(a->theme);
            if (settings_save(a) == 0) say(a, R3_ST_OK, "Colour theme: %s", r3_themes[a->theme].name);
            else say(a, R3_ST_WARN, "Colour theme: %s (not saved: cannot write the settings)", r3_themes[a->theme].name);
            a->overlay = OV_NONE;
        } else if (action == R3_BACK || action == R3_MENU) { r3_theme_apply(a->theme); a->overlay = OV_NONE; }
        else { a->theme_sel = r3_list_step(a->theme_sel, R3_THEME_COUNT, r3_list_rows(R3_DISPLAY_ROWS - 2), action); r3_theme_apply(a->theme_sel); }
        a->force = 1;
        return;
    }
    if (a->overlay == OV_CMDS) {
        if (action == R3_BACK || action == R3_CONTEXT || action == R3_MENU) a->overlay = OV_NONE;
        else if (action == R3_ACCEPT) { int id = a->cmd_id[a->cmd_sel]; a->overlay = OV_NONE; run_command(a, id); }
        else a->cmd_sel = r3_list_step(a->cmd_sel, a->ncmd, r3_list_rows(a->ncmd + 2), action);
        a->force = 1;
        return;
    }
    if (a->overlay == OV_CONFIRM) {
        if (action == R3_BACK || action == R3_MENU) a->overlay = OV_NONE;
        else if (action == R3_ACCEPT) { a->overlay = OV_NONE; if (a->confirm_sel == 1) confirm_run(a); }
        else if (action == R3_UP || action == R3_DOWN) a->confirm_sel = !a->confirm_sel;
        a->force = 1;
        return;
    }
    int id = r3_mm_key(&a->mm, action);
    if (id == R3MM_NOTHING) { a->force = 1; return; }
    a->overlay = OV_NONE;
    a->force = 1;
    switch (id) {
    case R3MM_ABOUT: a->overlay = OV_ABOUT; break;
    case R3MM_THEME: a->overlay = OV_THEME; a->theme_sel = a->theme; break;
    case R3MM_KEYMAP: r3_km_open(&a->km, KM_PAGES, 2, a->screen == SC_EDIT ? 1 : 0); a->overlay = OV_KEYMAP; break;
    case R3MM_REPORT: a->pending = 1; say(a, R3_ST_WARN, "Writing the device report..."); break;
    case R3MM_DEMO: say(a, R3_ST_WARN, "Rec demo is not built for the Font Editor yet."); break;
    case R3MM_QUIT: a->quit = 1; break;
    default: break;
    }
}

static void dispatch(App *a, int action) {
    if (action == R3_NONE) return;
    if (a->overlay) { overlay_key(a, action); return; }
    switch (action) {
    case R3_MENU: stroke_end(a); r3_mm_open(&a->mm); a->overlay = OV_MENU; a->force = 1; return;
    case R3_L3: say(a, R3_ST_HINT, "The Font Editor has no virtual mouse."); return;
    case R3_R3: say(a, R3_ST_HINT, "The Font Editor has no virtual keyboard."); return;
    default: break;
    }
    if (a->screen == SC_BROWSE) list_action(a, action); else edit_action(a, action);
}

/* A button or key, down or up, through the mapping's reader. A and X held while the Pen moves draw and erase; letting go ends the stroke. */
static void press(App *a, int b, int down) {
    int ac = r3_input_button(&a->in, b, down);
    if (a->overlay == OV_KEYMAP) r3_km_button(&a->km, b, down, NULL);
    if (!down) {
        if ((b == R3_BTN_A || b == R3_BTN_X) && a->stroke) { stroke_end(a); a->force = 1; }
        return;
    }
    dispatch(a, ac);
}

static void handle(App *a, const SDL_Event *e) {
    switch (e->type) {
    case SDL_QUIT: break;                       /* only the main menu's Quit leaves (rule 17) */
    case SDL_JOYDEVICEADDED: if (!a->js) a->js = SDL_JoystickOpen(e->jdevice.which); break;
    case SDL_JOYDEVICEREMOVED:
        if (a->js && SDL_JoystickInstanceID(a->js) == e->jdevice.which) { SDL_JoystickClose(a->js); a->js = NULL; }
        break;
    case SDL_JOYBUTTONDOWN: case SDL_JOYBUTTONUP: {
        int down = e->type == SDL_JOYBUTTONDOWN;
        if (down && r3_toast_key(&a->toast)) break;
        press(a, e->jbutton.button, down);
        break;
    }
    case SDL_JOYAXISMOTION: r3_input_axis(&a->in, e->jaxis.axis, e->jaxis.value / 32768.0f); break;
    case SDL_KEYDOWN: case SDL_KEYUP: {
        if (e->key.repeat) break;
        int down = e->type == SDL_KEYDOWN;
        if (down && r3_toast_key(&a->toast)) break;
        SDL_Keycode k = e->key.keysym.sym;
        int kb = r3_key_button(k);
        if (kb >= 0) { press(a, kb, down); break; }
        if (!down) break;
        int ac = r3_input_key(k);
        if (r3_input_held(&a->in, R3_BTN_R1)) ac = k == SDLK_UP ? R3_PAGEUP : k == SDLK_DOWN ? R3_PAGEDOWN : k == SDLK_LEFT ? R3_HOME : k == SDLK_RIGHT ? R3_END : ac;
        dispatch(a, ac);
        break;
    }
    default: break;
    }
}

static void tick(App *a, uint32_t now) {
    int acts[8], n = r3_input_tick(&a->in, now, acts, 8);
    for (int i = 0; i < n; i++) dispatch(a, acts[i]);
}

static unsigned nap_ms(const App *a, const R3Video *v) {
    if (r3_input_stick_active(&a->in) || a->stroke) return 16;
    if (a->force || v->settle > 0 || r3_toast_active(&a->toast) || a->pending) return 16;
    return 100;     /* nothing is moving: about 10 wake-ups a second (CLAUDE.md rule 14) */
}

static void draw_frame(App *a) {
    R3Screen *s = &a->loop->screen;
    compose(a, s);
    r3_toast_draw(&a->toast, s);
    r3_reportbar_overlay(s);
    uint32_t sig = ov_signature(a);
    if (sig != a->ov_sig) { a->ov_sig = sig; a->force = 1; }       /* the cells alone do not show an edited pixel */
    r3_video_draw(a->loop->video, a->loop->r, s, a->force);
    a->force = 0;
}

/* ---- self-test ------------------------------------------------------------------------------------------------------------------- */

static int bad;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "fontedit selftest: "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); bad++; } } while (0)

static int screen_has(const R3Screen *s, const char *want) {
    char rows[R3_ROWS][R3_COLS + 1];
    r3_dump(s, rows);
    for (int i = 0; i < R3_ROWS; i++) if (strstr(rows[i], want)) return 1;
    return 0;
}

static void save_bmp(const R3Screen *s, const char *path) {
    uint32_t *px = malloc((size_t)R3_W * R3_H * sizeof *px);
    SDL_Surface *surf = px ? SDL_CreateRGBSurfaceWithFormatFrom(px, R3_W, R3_H, 32, R3_W * 4, SDL_PIXELFORMAT_ARGB8888) : NULL;
    if (surf) { r3_render(s, px); SDL_SaveBMP(surf, path); SDL_FreeSurface(surf); }
    free(px);
}

/* The edits file the Python side writes and reads (tests/test_fontedit_edits.py checks these very lines against scripts/r3font/edits.py): one cell of
 * icon.app.hydrolik, the first row all ink and the last row one pixel. Each line is a row of the file. */
/* FIXTURE BEGIN */
static const char FIXTURE[] =
    "# r3i6nos font edits 1\n"
    "font R3i6nos Mono 10x20\n"
    "\n"
    "cell 10FD14 icon.app.hydrolik 1/2\n"
    "##########\n"
    "..........\n"
    "..........\n"
    "..........\n"
    "..........\n"
    "..........\n"
    "..........\n"
    "..........\n"
    "..........\n"
    "..........\n"
    "..........\n"
    "..........\n"
    "..........\n"
    "..........\n"
    "..........\n"
    "..........\n"
    "..........\n"
    "..........\n"
    "..........\n"
    ".........#\n"
    "\n";
/* FIXTURE END */

static void tmp_dir(char *out, size_t cap, const char *what) {
    snprintf(out, cap, "%s/fontedit-%s-XXXXXX", env_or("TMPDIR", "/tmp"), what);
    if (!mkdtemp(out)) out[0] = 0;
}

static void rm_rf(const char *dir) {
    char cmd[PATH_MAX + 16];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", dir);
}

static int find_group(const FeFont *f, const char *name) {
    for (int i = 0; i < f->ng; i++) if (!strcmp(f->groups[i].name, name)) return i;
    return -1;
}

static void selftest_model(void) {
    FeFont f;
    CHECK(fe_load_builtin(&f) == 0, "R3i6nos Mono loads");
    CHECK(f.n == 2919 && f.ng == 884 && f.w == 10 && f.h == 20, "the font has %d code points, %d names, %dx%d", f.n, f.ng, f.w, f.h);
    int multi = 0, three = 0;
    for (int i = 0; i < f.ng; i++) { multi += f.groups[i].n >= 2; three += f.groups[i].n > 2; }
    CHECK(multi == 508 && three == 0, "multi-cell names: %d (expected 508, none longer than 2)", multi);
    int hy = find_group(&f, "icon.app.hydrolik");
    CHECK(hy >= 0 && f.groups[hy].n == 2 && f.cp[f.groups[hy].ci[1]] == f.cp[f.groups[hy].ci[0]] + 1, "icon.app.hydrolik is two consecutive code points");
    for (int i = 1; i < f.ng; i++) if (strcmp(f.groups[i - 1].name, f.groups[i].name) >= 0) { CHECK(0, "names not sorted at %d", i); break; }
    CHECK(fe_count_changed(&f) == 0, "a fresh font has no changes");

    /* the canvas and its tools, on the two cells of one name */
    FeGroup g = f.groups[hy];
    FeCanvas c;
    fe_canvas_load(&f, &g, &c);
    CHECK(c.n == 2 && fe_cw(&c) == 20 && c.h == 20, "a pair is a 20 x 20 canvas");
    fe_put(&c, 9, 5, 1); fe_put(&c, 10, 5, 1);               /* either side of the seam */
    CHECK(fe_get(&c, 9, 5) && fe_get(&c, 10, 5), "pixels either side of the seam");
    fe_clear(&c);
    fe_line(&c, 2, 2, 17, 2, 1);
    CHECK(fe_ink(&c) == 16, "a line across the seam is 16 pixels, got %d", fe_ink(&c));
    fe_clear(&c);
    fe_rect(&c, 8, 8, 11, 11, 1, 0);
    CHECK(fe_ink(&c) == 12, "a 4x4 box across the seam is 12 pixels, got %d", fe_ink(&c));
    CHECK(fe_flood(&c, 9, 9, 1) == 4, "the fill of the inside of that box is 4 pixels");
    fe_clear(&c);
    fe_put(&c, 1, 3, 1);
    fe_flip_h(&c);
    CHECK(fe_get(&c, 18, 3) == 1 && fe_ink(&c) == 1, "flip left-right moves a pixel from the first cell to the mirrored place in the last");
    fe_flip_v(&c);
    CHECK(fe_get(&c, 18, 16) == 1, "flip up-down");
    fe_shift(&c, -3, 2);
    CHECK(fe_get(&c, 15, 18) == 1 && fe_ink(&c) == 1, "shift");
    fe_clear(&c);
    fe_put(&c, 0, 0, 1); fe_put(&c, 9, 4, 1);
    fe_cells_cmd(&c, FE_CELL_COPY_RIGHT);
    CHECK(fe_get(&c, 10, 0) && fe_get(&c, 19, 4), "copy the first cell onto the last");
    fe_clear(&c);
    fe_put(&c, 0, 0, 1);
    fe_cells_cmd(&c, FE_CELL_MIRROR_RIGHT);
    CHECK(fe_get(&c, 19, 0) && !fe_get(&c, 10, 0), "mirror the first cell onto the last");
    fe_cells_cmd(&c, FE_CELL_SWAP);
    CHECK(fe_get(&c, 9, 0) && fe_get(&c, 10, 0) && !fe_get(&c, 0, 0) && !fe_get(&c, 19, 0) && fe_ink(&c) == 2, "swap the cells");

    /* history: a change is undone and redone, and the edit is one code point's, not every code point that shared the picture */
    FeHist h;
    CHECK(fe_hist_init(&h) == 0, "history");
    int ci0 = g.ci[0];
    fe_canvas_load(&f, &g, &c);
    fe_hist_push(&h, &f, &g);
    fe_put(&c, 3, 3, !fe_get(&c, 3, 3));
    CHECK(fe_canvas_store(&f, &g, &c) == 1 && fe_cell_changed(&f, ci0) && !fe_cell_changed(&f, g.ci[1]), "an edit changes that cell only");
    CHECK(fe_count_changed(&f) == 1, "one changed cell");
    CHECK(fe_hist_undo(&h, &f) != NULL && !fe_cell_changed(&f, ci0), "undo restores");
    CHECK(fe_hist_redo(&h, &f) != NULL && fe_cell_changed(&f, ci0), "redo repeats");
    CHECK(fe_hist_undo(&h, &f) != NULL && fe_hist_undo(&h, &f) == NULL, "nothing left to undo");
    fe_hist_free(&h);

    /* the edits file: written, read back, byte for byte; refused when wrong, and a refusal leaves the font alone */
    char dir[PATH_MAX], path[PATH_MAX + 32], err[200];
    tmp_dir(dir, sizeof dir, "model");
    CHECK(dir[0] != 0, "a temporary folder");
    if (dir[0]) {
        snprintf(path, sizeof path, "%s/sub/edits.txt", dir);
        fe_canvas_load(&f, &g, &c);
        fe_put(&c, 0, 0, 1); fe_put(&c, 10, 19, 1);
        fe_canvas_store(&f, &g, &c);
        CHECK(fe_edits_save(&f, path) == 0, "the edits file is written (and its folder made): %s", strerror(errno));
        FeFont f2;
        fe_load_builtin(&f2);
        CHECK(fe_edits_load(&f2, path, err, sizeof err) == 2 && !memcmp(f2.rows, f.rows, (size_t)f.n * sizeof *f.rows), "the edits file reads back the same: %s", err);
        char path2[PATH_MAX + 40];
        snprintf(path2, sizeof path2, "%s/again.txt", dir);
        CHECK(fe_edits_save(&f2, path2) == 0, "saved again");
        char a1[8192] = "", a2[8192] = "";
        FILE *x = fopen(path, "r"), *y = fopen(path2, "r");
        size_t n1 = x ? fread(a1, 1, sizeof a1 - 1, x) : 0, n2 = y ? fread(a2, 1, sizeof a2 - 1, y) : 0;
        if (x) fclose(x);
        if (y) fclose(y);
        CHECK(n1 > 100 && n1 == n2 && !memcmp(a1, a2, n1), "saving what was read is the same file, byte for byte");
        CHECK(strstr(a1, "cell 10FD14 icon.app.hydrolik 1/2") != NULL, "the cell line names the glyph and the cell");
        fe_free(&f2);

        /* the Python side's file */
        fe_load_builtin(&f2);
        FILE *w = fopen(path, "w");
        if (w) { fputs(FIXTURE, w); fclose(w); }
        CHECK(fe_edits_load(&f2, path, err, sizeof err) == 1, "the fixture reads: %s", err);
        int ci = fe_find(&f2, 0x10FD14);
        CHECK(ci >= 0 && f2.rows[ci][0] == 0x3FF && f2.rows[ci][19] == 1 && f2.rows[ci][1] == 0, "the fixture's pixels (first row all ink, last row the right pixel)");
        snprintf(path2, sizeof path2, "%s/fixture-out.txt", dir);
        CHECK(fe_edits_save(&f2, path2) == 0, "fixture saved");
        char b[8192] = "";
        y = fopen(path2, "r");
        size_t nb = y ? fread(b, 1, sizeof b - 1, y) : 0;
        if (y) fclose(y);
        CHECK(nb == strlen(FIXTURE) && !memcmp(b, FIXTURE, nb), "what C writes is what Python writes (the fixture), byte for byte");

        const char *wrong[][2] = {
            {"stale name", "# r3i6nos font edits 1\nfont R3i6nos Mono 10x20\n\ncell 10FD14 icon.app.klamansi 1/2\n"},
            {"not in the font", "# r3i6nos font edits 1\nfont R3i6nos Mono 10x20\n\ncell 10FFFF - 1/1\n"},
            {"other font", "# r3i6nos font edits 1\nfont Other 8x16\n"},
            {"short cell", "# r3i6nos font edits 1\nfont R3i6nos Mono 10x20\n\ncell 41 - 1/1\n..........\n..........\n\n"},
            {"bad row", "# r3i6nos font edits 1\nfont R3i6nos Mono 10x20\n\ncell 41 - 1/1\n.....x....\n"},
        };
        for (size_t i = 0; i < sizeof wrong / sizeof wrong[0]; i++) {
            fe_free(&f2);
            fe_load_builtin(&f2);
            w = fopen(path, "w");
            if (w) { fputs(wrong[i][1], w); fclose(w); }
            CHECK(fe_edits_load(&f2, path, err, sizeof err) == -1 && err[0] && fe_count_changed(&f2) == 0, "a file with a %s is refused with a reason and changes nothing", wrong[i][0]);
        }
        fe_free(&f2);
        rm_rf(dir);
    }

    /* another font: R3i6nos Mono's own BDF, read back as the same pictures (looked for beside the repository; skipped where it is not) */
    static const char *const bdfs[] = {"theme/font/r3i6nos-mono/R3i6nosMono-Regular.bdf", "/opt/loveos/theme/font/r3i6nos-mono/R3i6nosMono-Regular.bdf"};
    int tried = 0;
    for (size_t i = 0; i < 2; i++) {
        if (access(bdfs[i], R_OK) != 0) continue;
        tried = 1;
        FeFont b;
        int n = fe_load_bdf(&b, bdfs[i], err, sizeof err);
        CHECK(n == 2919 && b.w == 10 && b.h == 20 && b.base == 15 && b.clipped == 0, "the BDF reads: %d glyphs, %dx%d, base %d, %d clipped (%s)", n, b.w, b.h, b.base, b.clipped, err);
        int diff = 0;
        for (int k = 0; k < b.n && k < f.n; k++) if (b.cp[k] != f.cp[k] || memcmp(b.rows[k], f.rows[k], sizeof b.rows[k])) diff++;
        CHECK(diff == fe_count_changed(&f), "the BDF's pictures are the compiled font's, except the two cells edited above (%d differ)", diff);
        char d2[PATH_MAX], p2[PATH_MAX + 40];
        tmp_dir(d2, sizeof d2, "bdf");
        if (d2[0]) {
            snprintf(p2, sizeof p2, "%s/out.bdf", d2);
            b.rows[100][4] ^= 0x2A;
            CHECK(fe_save_bdf(&b, p2) == 0, "the BDF is written");
            FeFont b2;
            CHECK(fe_load_bdf(&b2, p2, err, sizeof err) == 2919 && !memcmp(b2.rows, b.rows, (size_t)b.n * sizeof *b.rows), "the BDF written reads back the same: %s", err);
            CHECK(b2.bdf_head && strstr(b2.bdf_head, "FAMILY_NAME") != NULL, "the BDF keeps its header");
            fe_free(&b2);
            rm_rf(d2);
        }
        fe_free(&b);
        break;
    }
    if (!tried) fprintf(stderr, "fontedit selftest: NOT RUN: the R3i6nos Mono BDF is not beside this binary (run from the repository to read it)\n");
    {   /* a BDF the editor cannot take, and one that is not a BDF */
        char d3[PATH_MAX], p3[PATH_MAX + 40];
        tmp_dir(d3, sizeof d3, "badbdf");
        if (d3[0]) {
            FeFont b;
            snprintf(p3, sizeof p3, "%s/big.bdf", d3);
            FILE *w = fopen(p3, "w");
            if (w) { fputs("STARTFONT 2.1\nFONTBOUNDINGBOX 24 32 0 -8\nCHARS 1\nSTARTCHAR a\nENCODING 65\nBBX 24 32 0 -8\nBITMAP\nENDCHAR\nENDFONT\n", w); fclose(w); }
            CHECK(fe_load_bdf(&b, p3, err, sizeof err) < 0 && strstr(err, "24x32"), "a 24x32 BDF is refused and says its size: %s", err);
            w = fopen(p3, "w");
            if (w) { fputs("not a font\n", w); fclose(w); }
            CHECK(fe_load_bdf(&b, p3, err, sizeof err) < 0, "a file that is not a BDF is refused");
            rm_rf(d3);
        }
    }
    fe_free(&f);
}

static void tap(App *a, int b) { press(a, b, 1); press(a, b, 0); }

static void selftest_app(const char *shots, SDL_Renderer *rend, R3Video *video) {
    char dir[PATH_MAX], path[PATH_MAX + 64];
    tmp_dir(dir, sizeof dir, "app");
    if (!dir[0]) { CHECK(0, "a temporary folder"); return; }
    setenv("R3I6NOS_APP_DATA", dir, 1);
    App *a = calloc(1, sizeof *a);
    struct Loop loop;
    memset(&loop, 0, sizeof loop);
    loop.r = rend;
    loop.video = video;
    a->loop = &loop;
    fe_load_builtin(&a->font);
    fe_hist_init(&a->hist);
    r3_input_init(&a->in);
    r3_mm_init(&a->mm, 0);
    r3_toast_init(&a->toast);
    a->guides = 1;
    set_paths(a);
    build_lists(a);
    g_app = a;
    r3_render_hook = hook_paint;
    r3_render_hook_ctx = a;
    R3Screen *s = &loop.screen;

    compose(a, s);
    CHECK(screen_has(s, "Multi-cell glyphs") && screen_has(s, "Font Editor") && screen_has(s, "Panel 1/4"), "the browser opens on the multi-cell panel");
    CHECK(a->nlist[PN_MULTI] == 508 && a->nlist[PN_NAMES] == 884 && a->nlist[PN_CODES] == 2919 && a->nlist[PN_CHANGED] == 0, "list sizes %d %d %d %d", a->nlist[0], a->nlist[1], a->nlist[2], a->nlist[3]);
    CHECK(a->nov >= 3, "the list has glyphs to paint (%d)", a->nov);

    /* find the Hydrolik icon by stepping down the list: the pages are whole, nothing scrolls */
    int hy = -1;
    for (int i = 0; i < a->nlist[PN_MULTI]; i++) if (!strcmp(a->font.groups[a->list[PN_MULTI][i]].name, "icon.app.hydrolik")) hy = i;
    CHECK(hy > 0, "icon.app.hydrolik is in the multi-cell list");
    for (int i = 0; i < hy; i++) tap(a, R3_BTN_DOWN);
    compose(a, s);
    CHECK(a->sel[PN_MULTI] == hy && screen_has(s, "icon.app.hydrolik") && screen_has(s, "2 cells, 20x20 px"), "the selection reaches it and the side says 2 cells, 20x20 px");

    tap(a, R3_BTN_A);
    compose(a, s);
    CHECK(a->screen == SC_EDIT && a->g.n == 2 && !strcmp(a->g.name, "icon.app.hydrolik") && screen_has(s, "Draw"), "A opens the name on one canvas");
    CHECK(canvas_pw(&a->c) == 2 && fe_cw(&a->c) == 20, "two text cells a pixel, 20 pixels wide");
    /* the canvas itself is cells: every pixel is two of them, so a 20x20 pair is 40 columns */
    {
        int on = 0;
        for (int y = 0; y < 20; y++) for (int x = 0; x < 20; x++) on += fe_get(&a->c, x, y);
        int blocks = 0;
        for (int row = 1; row <= 20; row++) for (int col = 2; col < 42; col++) blocks += s->ch[row * R3_COLS + col] == 219;
        CHECK(blocks >= on * 2 - 2 * 1 && blocks <= on * 2 + 2, "the canvas draws every ink pixel as two block cells (%d blocks, %d pixels)", blocks, on);
    }
    /* the cursor starts in the middle; draw across the seam by holding A and moving right */
    a->cx = 8; a->cy = 0;
    int before = fe_count_changed(&a->font);
    int was9 = fe_get(&a->c, 9, 0), was10 = fe_get(&a->c, 10, 0), was11 = fe_get(&a->c, 11, 0), was8 = fe_get(&a->c, 8, 0);
    CHECK(!was8 && !was9 && !was10 && !was11, "the top row of the icon is blank where the test draws");
    press(a, R3_BTN_A, 1);
    press(a, R3_BTN_RIGHT, 1); press(a, R3_BTN_RIGHT, 0);
    press(a, R3_BTN_RIGHT, 1); press(a, R3_BTN_RIGHT, 0);
    press(a, R3_BTN_RIGHT, 1); press(a, R3_BTN_RIGHT, 0);
    press(a, R3_BTN_A, 0);
    CHECK(fe_get(&a->c, 8, 0) && fe_get(&a->c, 9, 0) && fe_get(&a->c, 10, 0) && fe_get(&a->c, 11, 0), "holding A and moving draws a stroke across the seam");
    CHECK(a->hist.nu == 1, "one stroke is one history entry (%d)", a->hist.nu);
    CHECK(fe_count_changed(&a->font) > before, "the font has changed");
    snprintf(path, sizeof path, "%s/edits-r3i6nos-mono.txt", dir);
    {
        char txt[8192] = "";
        FILE *f = fopen(path, "r");
        size_t n = f ? fread(txt, 1, sizeof txt - 1, f) : 0;
        if (f) fclose(f);
        CHECK(n > 0 && strstr(txt, "cell 10FD14 icon.app.hydrolik 1/2") && strstr(txt, "cell 10FD15 icon.app.hydrolik 2/2"), "the end of the stroke saved the edits file with both cells");
    }
    /* undo through the commands menu */
    press(a, R3_BTN_L2, 1); press(a, R3_BTN_R2, 1); press(a, R3_BTN_R2, 0); press(a, R3_BTN_L2, 0);
    CHECK(a->overlay == OV_CMDS, "L2+R2 opens the commands");
    compose(a, s);
    CHECK(screen_has(s, "Commands") && screen_has(s, "Undo") && screen_has(s, "Mirror first cell onto last") && screen_has(s, "Swap first and last cell"), "the commands include the cell commands for a pair");
    if (shots) { snprintf(path, sizeof path, "%s/fontedit-commands.bmp", shots); save_bmp(s, path); }
    tap(a, R3_BTN_A);       /* Undo is the first item */
    CHECK(a->overlay == OV_NONE && a->hist.nu == 0 && fe_get(&a->c, 8, 0) == was8 && fe_get(&a->c, 9, 0) == was9 && fe_get(&a->c, 10, 0) == was10 && fe_get(&a->c, 11, 0) == was11, "Undo takes the stroke back");
    CHECK(fe_count_changed(&a->font) == 0, "and the font is as built again");

    /* the pair's tools across the seam: a Box from 8,8 to 11,11 */
    tap(a, R3_BTN_Y);
    tap(a, R3_BTN_Y);
    CHECK(a->tool == FE_TOOL_BOX, "Y steps the tool: Pen, Line, Box");
    a->cx = 8; a->cy = 8;
    tap(a, R3_BTN_A);
    CHECK(a->anchored, "A sets a corner");
    for (int i = 0; i < 3; i++) { tap(a, R3_BTN_RIGHT); tap(a, R3_BTN_DOWN); }
    compose(a, s);
    CHECK(a->cx == 11 && a->cy == 11, "the cursor is at the other corner");
    if (shots) { snprintf(path, sizeof path, "%s/fontedit-box.bmp", shots); save_bmp(s, path); }
    tap(a, R3_BTN_A);
    CHECK(!a->anchored && fe_get(&a->c, 8, 8) && fe_get(&a->c, 11, 8) && fe_get(&a->c, 11, 11), "the second A draws the box across the seam");
    tap(a, R3_BTN_B);
    CHECK(a->screen == SC_BROWSE, "B with no corner waiting leaves the editor");
    open_entry(a);
    CHECK(a->screen == SC_EDIT && !strcmp(a->g.name, "icon.app.hydrolik") && fe_get(&a->c, 8, 8), "and A opens the same glyph again, with the box still there");

    /* the picture painted over the cells is the model's, pixel for pixel */
    {
        compose(a, s);
        uint32_t *px = calloc((size_t)R3_W * R3_H, sizeof *px);
        r3_render(s, px);
        int wrong = 0, seen = 0;
        for (int i = 0; i < a->nov; i++) {
            const Ov *o = &a->ov[i];
            if (o->scale != 1 || o->ci != a->g.ci[0]) continue;
            seen++;
            for (int y = 0; y < 20; y++)
                for (int x = 0; x < 10; x++) {
                    uint32_t want = (a->font.rows[o->ci][y] >> (9 - x) & 1) ? r3_pal[o->fg] : r3_pal[o->bg];
                    if (px[(size_t)(o->row * 20 + y) * R3_W + (size_t)(o->col * 10 + x)] != want) wrong++;
                }
        }
        CHECK(seen >= 1 && wrong == 0, "the real-size picture in the side panel is the edited cell, pixel for pixel (%d cells, %d wrong)", seen, wrong);
        free(px);
        uint32_t s1 = ov_signature(a);
        fe_put(&a->c, 0, 0, !fe_get(&a->c, 0, 0));
        fe_canvas_store(&a->font, &a->g, &a->c);
        compose(a, s);
        CHECK(ov_signature(a) != s1, "an edited pixel changes the signature that makes the video repaint");
    }

    /* the panels of the editor, and the prev and next glyph */
    tap(a, R3_BTN_FN);
    compose(a, s);
    CHECK(a->epanel == EP_SAMPLE && screen_has(s, "Sample") && screen_has(s, "Joined, as in a row"), "Fn turns to the Sample panel");
    if (shots) { snprintf(path, sizeof path, "%s/fontedit-sample.bmp", shots); save_bmp(s, path); }
    tap(a, R3_BTN_FN);
    CHECK(a->epanel == EP_DRAW, "Fn turns round to Draw");
    char was_name[80];
    snprintf(was_name, sizeof was_name, "%s", a->g.name);
    tap(a, R3_BTN_START);
    CHECK(strcmp(a->g.name, was_name) != 0 && a->screen == SC_EDIT, "Start opens the next glyph in the list (%s -> %s)", was_name, a->g.name);
    tap(a, R3_BTN_SELECT);
    CHECK(!strcmp(a->g.name, was_name), "Select goes back");

    /* the changed panel and revert, asked first */
    tap(a, R3_BTN_B);
    CHECK(a->screen == SC_BROWSE && a->nlist[PN_CHANGED] >= 1, "back in the list, the Changed panel has the glyph");
    tap(a, R3_BTN_FN); tap(a, R3_BTN_FN); tap(a, R3_BTN_FN);
    compose(a, s);
    CHECK(a->panel == PN_CHANGED && screen_has(s, "Changed") && screen_has(s, "icon.app.hydrolik"), "Fn x3 reaches the Changed panel");
    if (shots) { snprintf(path, sizeof path, "%s/fontedit-changed.bmp", shots); save_bmp(s, path); }
    tap(a, R3_BTN_X);
    CHECK(a->overlay == OV_CONFIRM && a->confirm_sel == 0, "X asks first, and starts on No");
    tap(a, R3_BTN_A);
    CHECK(fe_count_changed(&a->font) > 0, "No leaves it");
    tap(a, R3_BTN_X); tap(a, R3_BTN_DOWN); tap(a, R3_BTN_A);
    CHECK(fe_count_changed(&a->font) == 0 && a->nlist[PN_CHANGED] == 0, "Yes reverts it and the list empties");
    {
        char txt[8192] = "x";
        snprintf(path, sizeof path, "%s/edits-r3i6nos-mono.txt", dir);
        FILE *f = fopen(path, "r");
        size_t n = f ? fread(txt, 1, sizeof txt - 1, f) : 0;
        txt[n] = 0;
        if (f) fclose(f);
        CHECK(!strstr(txt, "cell "), "and the saved file has no cells left");
    }
    a->panel = PN_MULTI;

    /* the main menu: the fixed items in their order, the About box, the keymap, the colour theme */
    char mm[200];
    CHECK(r3_mm_selftest(&a->mm, mm, sizeof mm) == 0, "%s", mm);
    CHECK(r3_km_selftest(KM_PAGES, 2, mm, sizeof mm) == 0, "%s", mm);
    press(a, R3_BTN_L1, 1); press(a, R3_BTN_R1, 1); press(a, R3_BTN_R1, 0); press(a, R3_BTN_L1, 0);
    CHECK(a->overlay == OV_MENU, "L1+R1 opens the main menu");
    compose(a, s);
    CHECK(screen_has(s, "About") && screen_has(s, "Colour theme") && screen_has(s, "Keymap") && screen_has(s, "Device report") && screen_has(s, "Rec demo") && screen_has(s, "Quit") && !screen_has(s, "Benchmark"), "the menu has the fixed items, no Benchmark");
    tap(a, R3_BTN_A);
    compose(a, s);
    CHECK(a->overlay == OV_ABOUT && screen_has(s, "Font Editor") && screen_has(s, "v" VERSION), "About names the app and its version");
    tap(a, R3_BTN_A);
    r3_mm_open(&a->mm); a->overlay = OV_MENU;
    tap(a, R3_BTN_DOWN); tap(a, R3_BTN_DOWN); tap(a, R3_BTN_A);
    CHECK(a->overlay == OV_KEYMAP, "Keymap opens");
    compose(a, s);
    CHECK(screen_has(s, "Keymap") && screen_has(s, "The list"), "the keymap opens on the list's page");
    press(a, R3_BTN_L1, 1); press(a, R3_BTN_R1, 1); press(a, R3_BTN_R1, 0); press(a, R3_BTN_L1, 0);
    CHECK(a->overlay == OV_NONE, "L1+R1 closes the keymap");
    r3_mm_open(&a->mm); a->overlay = OV_MENU;
    tap(a, R3_BTN_DOWN); tap(a, R3_BTN_A);
    CHECK(a->overlay == OV_THEME, "Colour theme opens");
    tap(a, R3_BTN_DOWN); tap(a, R3_BTN_A);
    CHECK(a->theme == 1 && a->overlay == OV_NONE, "a theme is kept");
    {
        char sp[PATH_MAX + 32], txt[200] = "";
        settings_path(a, sp, sizeof sp);
        FILE *f = fopen(sp, "r");
        size_t n = f ? fread(txt, 1, sizeof txt - 1, f) : 0;
        txt[n] = 0;
        if (f) fclose(f);
        CHECK(strstr(txt, "theme=Dark") != NULL, "settings.conf has the theme");
    }
    a->theme = 0; r3_theme_apply(0);
    r3_mm_open(&a->mm); a->overlay = OV_MENU;
    tap(a, R3_BTN_DOWN); tap(a, R3_BTN_DOWN); tap(a, R3_BTN_DOWN); tap(a, R3_BTN_DOWN); tap(a, R3_BTN_A);   /* Rec demo */
    CHECK(strstr(a->reply, "not built") != NULL && !a->quit, "Rec demo says it is not built yet");
    r3_mm_open(&a->mm); a->overlay = OV_MENU;
    press(a, R3_BTN_R1, 1); tap(a, R3_BTN_RIGHT); press(a, R3_BTN_R1, 0);      /* R1 + Right is End: the last item, Quit */
    tap(a, R3_BTN_A);
    CHECK(a->quit == 1, "only the main menu's Quit leaves");
    a->quit = 0;
    CHECK(r3_input_key(SDLK_ESCAPE) != R3_MENU, "Esc is not a way out");

    /* the card out: a folder that cannot be written says so and the editing goes on in memory */
    snprintf(a->data_dir, sizeof a->data_dir, "%.3000s/not-a-folder/inside", dir);
    snprintf(path, sizeof path, "%s/not-a-folder", dir);
    FILE *blocker = fopen(path, "w");
    if (blocker) { fputs("x", blocker); fclose(blocker); }
    snprintf(a->edits_path, sizeof a->edits_path, "%.3900s/edits-r3i6nos-mono.txt", a->data_dir);
    FeGroup g;
    fe_group_cp(&a->font, fe_find(&a->font, 'A'), &g);
    open_group(a, &g);
    CHECK(!fe_get(&a->c, 0, 0), "the corner of the letter A is paper");
    a->cx = 0; a->cy = 0; a->tool = FE_TOOL_PEN;
    tap(a, R3_BTN_A);
    tap(a, R3_BTN_B);
    CHECK(a->save_failed && strstr(a->reply, "NOT SAVED") != NULL && fe_count_changed(&a->font) == 1, "a card that cannot be written says NOT SAVED and the edit stays in memory");
    compose(a, s);
    CHECK(screen_has(s, "NOT SAVED"), "and the top bar says it too");
    app_data_dir(a->data_dir, sizeof a->data_dir);
    snprintf(a->edits_path, sizeof a->edits_path, "%.3900s/edits-r3i6nos-mono.txt", a->data_dir);
    run_command(a, C_SAVE);
    CHECK(!a->save_failed, "Save now works once the card is back");
    a->confirm_id = C_REVERT_ALL;
    confirm_run(a);
    CHECK(fe_count_changed(&a->font) == 0, "Revert every change");

    /* the screens, for the screenshots */
    if (shots) {
        a->screen = SC_BROWSE; a->panel = PN_MULTI; a->sel[PN_MULTI] = hy;
        compose(a, s);
        snprintf(path, sizeof path, "%s/fontedit-list.bmp", shots);
        save_bmp(s, path);
        open_entry(a);
        compose(a, s);
        snprintf(path, sizeof path, "%s/fontedit-edit.bmp", shots);
        save_bmp(s, path);
    }

    /* the report: every step reached (rule 18), the sections and the power section there */
    setenv("R3I6NOS_REPORT_POWER_S", "0.2", 1);
    write_report(a, "selftest");
    CHECK(a->report_missed == 0, "the report reached step %d of %d (missed %d)", REPORT_STEPS, REPORT_STEPS, a->report_missed);
    {
        char txt[32768] = "";
        snprintf(path, sizeof path, "%s/r36s-report.txt", dir);
        FILE *f = fopen(path, "r");
        size_t n = f ? fread(txt, 1, sizeof txt - 1, f) : 0;
        txt[n] = 0;
        if (f) fclose(f);
        CHECK(strstr(txt, "== font ==") && strstr(txt, "PASS font tables") && strstr(txt, "PASS edits round trip") && strstr(txt, "== speed ==") && strstr(txt, "== power ==") &&
              strstr(txt, "full redraw") && strstr(txt, "\nend") != NULL, "the report has its font, saving, speed and power sections and ends with `end`");
        /* the editor's own sections must not fail; the system section's pad count fails off the unit (no pad here), and the speed and power goals mean nothing under an emulator */
        char first[160] = "";
        const char *end_own = strstr(txt, "== speed ==");
        for (const char *p = txt; (p = strstr(p, "\nFAIL ")) != NULL && (!end_own || p < end_own); p++)
            if (strncmp(p + 6, "pads:", 5) != 0 && !first[0]) snprintf(first, sizeof first, "%.*s", (int)strcspn(p + 1, "\n"), p + 1);
        CHECK(!first[0], "the report has a FAIL line in its own sections: %s", first);
    }
    r3_render_hook = NULL;
    fe_hist_free(&a->hist);
    for (int p = 0; p < PN_COUNT; p++) free(a->list[p]);
    fe_free(&a->font);
    free(a);
    g_app = NULL;
    unsetenv("R3I6NOS_APP_DATA");
    rm_rf(dir);
}

/* ---- screenshot and headless report ----------------------------------------------------------------------------------------------- */

static int save_screenshot(const char *out, const char *which, const char *fontfile) {
    App *a = calloc(1, sizeof *a);
    char err[300];
    struct Loop loop;
    memset(&loop, 0, sizeof loop);
    if (fontfile ? fe_load_bdf(&a->font, fontfile, err, sizeof err) < 0 : fe_load_builtin(&a->font) < 0) { fprintf(stderr, "fontedit: %s\n", fontfile ? err : "the font"); return 1; }
    fe_hist_init(&a->hist);
    r3_input_init(&a->in);
    r3_mm_init(&a->mm, 0);
    r3_toast_init(&a->toast);
    a->guides = 1;
    a->loop = &loop;
    set_paths(a);
    build_lists(a);
    r3_render_hook = hook_paint;
    r3_render_hook_ctx = a;
    int ci = fe_find(&a->font, 0x10FD14);
    const char *w = which ? which : "browse";
    if (ci >= 0 && a->font.builtin) { for (int i = 0; i < a->nlist[PN_MULTI]; i++) if (a->font.groups[a->list[PN_MULTI][i]].n == 2 && !strcmp(a->font.groups[a->list[PN_MULTI][i]].name, "icon.app.hydrolik")) a->sel[PN_MULTI] = i; }
    if (!a->font.builtin || a->font.ng == 0) a->panel = PN_CODES;
    if (!strcmp(w, "edit") || !strcmp(w, "sample") || !strcmp(w, "commands")) { open_entry(a); if (!strcmp(w, "sample")) a->epanel = EP_SAMPLE; if (!strcmp(w, "commands")) { build_commands(a); a->overlay = OV_CMDS; } }
    if (!strcmp(w, "menu")) { r3_mm_open(&a->mm); a->overlay = OV_MENU; }
    R3Screen s;
    compose(a, &s);
    if (SDL_Init(0) != 0) return 1;
    save_bmp(&s, out);
    printf("fontedit: %s -> %s\n", w, out);
    SDL_Quit();
    return 0;
}

static int headless_report(const char *fontfile) {
    App *a = calloc(1, sizeof *a);
    char err[300];
    if (fontfile ? fe_load_bdf(&a->font, fontfile, err, sizeof err) < 0 : fe_load_builtin(&a->font) < 0) { fprintf(stderr, "fontedit: %s\n", fontfile ? err : "the font"); return 1; }
    fe_hist_init(&a->hist);
    a->guides = 1;
    set_paths(a);
    if (a->font.builtin) { char e2[200]; if (access(a->edits_path, R_OK) == 0) fe_edits_load(&a->font, a->edits_path, e2, sizeof e2); }
    build_lists(a);
    write_report(a, "fontedit --report (no window: nothing was drawn or pressed)");
    printf("%s\n", a->reply);
    return a->reply_style == R3_ST_ERROR ? 1 : 0;
}

int main(int argc, char **argv) {
    int selftest = 0;
    const char *shot = NULL, *shot_screen = NULL, *fontfile = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) selftest = 1;
        else if (!strcmp(argv[i], "--report")) return headless_report(fontfile);
        else if (!strcmp(argv[i], "--font") && i + 1 < argc) fontfile = argv[++i];
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) { shot = argv[++i]; if (i + 1 < argc && argv[i + 1][0] != '-') shot_screen = argv[++i]; }
        else if (!strcmp(argv[i], "--version")) { puts("fontedit " VERSION); return 0; }
        else { fprintf(stderr, "usage: %s [--font FILE.bdf] [--report | --selftest | --screenshot out.bmp [browse|edit|sample|commands|menu] | --version]\n", argv[0]); return 2; }
    }
    if (shot) return save_screenshot(shot, shot_screen, fontfile);

    if (selftest) {
        selftest_model();
        if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "fontedit: SDL_Init: %s\n", SDL_GetError()); return 1; }
        const char *shots = getenv("R3I6NOS_FE_SHOTS");
        if (shots && *shots) mkdir(shots, 0777);
        SDL_Window *tw = SDL_CreateWindow(APP, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, R3_W, R3_H, 0);
        SDL_Renderer *tr = tw ? SDL_CreateRenderer(tw, -1, SDL_RENDERER_SOFTWARE) : NULL;
        R3Video tv;
        memset(&tv, 0, sizeof tv);
        if (!tr || !r3_video_init(&tv, tr)) { fprintf(stderr, "fontedit: no window for the self-test: %s\n", SDL_GetError()); return 1; }
        selftest_app(shots && *shots ? shots : NULL, tr, &tv);
        r3_video_free(&tv);
        SDL_DestroyRenderer(tr);
        SDL_DestroyWindow(tw);
        SDL_version v;
        SDL_GetVersion(&v);
        if (bad) { fprintf(stderr, "fontedit: %d selftest check(s) failed\n", bad); return 1; }
        printf("R3I6NOS_SMOKE ok fontedit=%s sdl=%d.%d.%d renderer=%s\n", VERSION, v.major, v.minor, v.patch, SDL_GetCurrentVideoDriver());
        SDL_Quit();
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "fontedit: SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_ShowCursor(SDL_DISABLE);
    SDL_Window *win = SDL_CreateWindow(APP, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, R3_W, R3_H, SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!win) { fprintf(stderr, "fontedit: window: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    SDL_Renderer *r = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!r) r = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!r) { fprintf(stderr, "fontedit: renderer: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    R3Video video;
    if (!r3_video_init(&video, r)) { fprintf(stderr, "fontedit: texture: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

    App *a = calloc(1, sizeof *a);
    struct Loop loop;
    memset(&loop, 0, sizeof loop);
    loop.r = r;
    loop.video = &video;
    a->loop = &loop;
    r3_input_init(&a->in);
    r3_mm_init(&a->mm, 0);          /* Benchmark is hidden: a drawing tool has nothing to measure against a goal but the report's redraw time */
    r3_toast_init(&a->toast);
    a->guides = 1;
    g_app = a;
    if (SDL_NumJoysticks() > 0) a->js = SDL_JoystickOpen(0);

    /* the splash first (rule 12), at once, before anything heavy; then where the data lives (rule 16) */
    R3Screen splash;
    r3_splash(&splash, APP, VERSION, BUILD_LINE, "Bitmap font editor", "Loading the font", 20);
    for (int i = 0; i < R3_VIDEO_SETTLE; i++) r3_video_draw(&video, r, &splash, i == 0);
    char err[400] = "";
    int loaded = fontfile ? fe_load_bdf(&a->font, fontfile, err, sizeof err) : fe_load_builtin(&a->font);
    if (loaded < 0) {
        r3_splash(&splash, APP, VERSION, BUILD_LINE, "Bitmap font editor", err[0] ? err : "Could not load the font", 20);
        for (int i = 0; i < 400; i++) { r3_video_draw(&video, r, &splash, i == 0); SDL_Delay(10); }     /* a load error is shown on the splash, never a blank window */
        SDL_Quit();
        return 1;
    }
    R3AppData data;
    memset(&data, 0, sizeof data);
    R3AppDataUI adui = {r, &video, &a->toast, NULL, NULL, NULL};
    r3_appdata_init(&data, DATA_NAME, &adui);
    if (data.quit) { SDL_Quit(); return 0; }
    r3_splash(&splash, APP, VERSION, BUILD_LINE, "Bitmap font editor", "Reading your edits", 70);
    r3_video_draw(&video, r, &splash, 1);
    fe_hist_init(&a->hist);
    set_paths(a);
    if (data.notice[0]) say(a, R3_ST_WARN, "%s", data.notice);
    settings_load(a);
    if (a->font.builtin && access(a->edits_path, R_OK) == 0) {
        int n = fe_edits_load(&a->font, a->edits_path, err, sizeof err);
        if (n < 0) {        /* never overwrite a file we could not read: put it aside, say why, and go on */
            char bad_path[PATH_MAX + 16];
            snprintf(bad_path, sizeof bad_path, "%s.bad", a->edits_path);
            rename(a->edits_path, bad_path);
            say(a, R3_ST_ERROR, "The edits file was not read (%s). Kept as %s.bad.", err, a->edits_path);
        } else say(a, R3_ST_OK, "%d changed cell%s read from your edits.", n, n == 1 ? "" : "s");
    } else if (!a->font.builtin && access(a->edits_path, R_OK) == 0 && strcmp(a->edits_path, a->font.path)) {
        say(a, R3_ST_HINT, "Saving to %s; the file read is never written.", a->edits_path);
    }
    build_lists(a);
    if (a->font.ng == 0) a->panel = PN_CODES;
    r3_render_hook = hook_paint;
    r3_render_hook_ctx = a;
    r3_video_draw(&video, r, &splash, 1);

    uint32_t last = SDL_GetTicks();
    while (!a->quit) {
        uint32_t now = SDL_GetTicks();
        SDL_Event e;
        while (SDL_PollEvent(&e)) handle(a, &e);
        tick(a, now);
        r3_toast_poll(&a->toast, now);
        draw_frame(a);
        if (a->pending && video.settle == 0) {      /* after "Writing the device report..." has been presented in full */
            a->pending = 0;
            write_report(a, "Device report (main menu)");
            continue;
        }
        SDL_Delay(nap_ms(a, &video));
        last = now;
    }
    (void)last;
    if (a->stroke) stroke_end(a);
    if (a->js) SDL_JoystickClose(a->js);
    r3_video_free(&video);
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

/* r3i6nos main menu for native apps (C): rule 17, docs/r3i6nos-mapping.md ("Every app has a main menu") and docs/R3I6NOS-ANSIAPPS-THEME.md. MIT.
 *
 * L1 + R1 opens it. Its first items are fixed, in this order: About, Colour theme, Keymap, Device report, Rec demo, Benchmark, Quit; the app's own items
 * come after, under the six. Benchmark alone is hidden, when the app has nothing to measure (has_bench = 0). This file owns the order and the
 * hiding, draws the list, the About box and the colour theme picker, and r3_mm_selftest() checks "the items are there and in this order" so an
 * app's own self-test calls it once. The app does what each item means (runs its report, its demo, its benchmark) and leaves on Quit.
 * Needs r3i6nos_ansiapps.h, r3i6nos_themes.h and r3i6nos_input.h first. Untested on a unit. */
#ifndef R3I6NOS_MAINMENU_H
#define R3I6NOS_MAINMENU_H

#include <stdio.h>

enum { R3MM_ABOUT, R3MM_THEME, R3MM_KEYMAP, R3MM_REPORT, R3MM_DEMO, R3MM_BENCH, R3MM_QUIT, R3MM_FIXED, R3MM_OWN0 = 100 };
enum { R3MM_NOTHING = -1, R3MM_CLOSED = -2 };   /* what r3_mm_key returns besides an item's id */
#define R3MM_MAX_OWN 8

static const char *const R3MM_LABEL[R3MM_FIXED] = {"About", "Colour theme", "Keymap", "Device report", "Rec demo", "Benchmark", "Quit"};

typedef struct {
    int open, sel;
    int has_bench;                          /* 0: Benchmark is hidden */
    const char *own[R3MM_MAX_OWN];          /* the app's own items, after Quit (ids R3MM_OWN0 + i) */
    int n_own;
} R3MainMenu;

static inline void r3_mm_init(R3MainMenu *m, int has_bench) { memset(m, 0, sizeof *m); m->has_bench = has_bench; }

/* The ids shown, in order; returns how many. */
static inline int r3_mm_ids(const R3MainMenu *m, int ids[R3MM_FIXED + R3MM_MAX_OWN]) {
    int n = 0;
    for (int i = 0; i < R3MM_FIXED; i++) if (i != R3MM_BENCH || m->has_bench) ids[n++] = i;
    for (int i = 0; i < m->n_own && i < R3MM_MAX_OWN; i++) ids[n++] = R3MM_OWN0 + i;
    return n;
}

static inline const char *r3_mm_label(const R3MainMenu *m, int id) { return id >= R3MM_OWN0 ? m->own[id - R3MM_OWN0] : R3MM_LABEL[id]; }

static inline void r3_mm_open(R3MainMenu *m) { m->open = 1; m->sel = 0; }

/* One action while the menu is open. A moves to the item's id (and closes the menu); B closes (R3MM_CLOSED); the rest moves the selection. */
static inline int r3_mm_key(R3MainMenu *m, int action) {
    int ids[R3MM_FIXED + R3MM_MAX_OWN], n = r3_mm_ids(m, ids);
    if (action == R3_BACK || action == R3_MENU) { m->open = 0; return R3MM_CLOSED; }
    if (action == R3_ACCEPT) { m->open = 0; return ids[m->sel < n ? m->sel : 0]; }
    m->sel = r3_list_step(m->sel, n, r3_list_rows(R3_DISPLAY_ROWS - 2), action);
    return R3MM_NOTHING;
}

/* The menu as the standard list dialog, centred in the display view. theme_name is Colour theme's value. */
static inline void r3_mm_draw(R3Screen *s, const R3MainMenu *m, const char *theme_name) {
    int ids[R3MM_FIXED + R3MM_MAX_OWN], n = r3_mm_ids(m, ids);
    R3Item items[R3MM_FIXED + R3MM_MAX_OWN];
    memset(items, 0, sizeof items);
    for (int i = 0; i < n; i++) {
        snprintf(items[i].label, sizeof items[i].label, "%s", r3_mm_label(m, ids[i]));
        if (ids[i] == R3MM_THEME && theme_name) snprintf(items[i].value, sizeof items[i].value, "%s", theme_name);
    }
    int h = n + 2 > R3_DISPLAY_ROWS - 2 ? R3_DISPLAY_ROWS - 2 : n + 2, w = 40;
    R3List l = {(R3_COLS - w) / 2, R3_DISPLAY_TOP + (R3_DISPLAY_ROWS - h) / 2, w, h, "Menu", "A choose  B close", items, n, m->sel, {NULL}};
    r3_list_dialog(s, &l);
}

/* "The items are there and in this order": 0 when the menu with and without Benchmark is right, else 1 and msg says what is wrong. */
static inline int r3_mm_selftest(const R3MainMenu *m, char *msg, size_t cap) {
    static const int want_all[R3MM_FIXED] = {R3MM_ABOUT, R3MM_THEME, R3MM_KEYMAP, R3MM_REPORT, R3MM_DEMO, R3MM_BENCH, R3MM_QUIT};
    for (int bench = 0; bench < 2; bench++) {
        R3MainMenu t = *m;
        t.has_bench = bench;
        int ids[R3MM_FIXED + R3MM_MAX_OWN], n = r3_mm_ids(&t, ids), k = 0;
        for (int i = 0; i < R3MM_FIXED; i++) {
            if (want_all[i] == R3MM_BENCH && !bench) continue;
            if (k >= n || ids[k] != want_all[i]) { snprintf(msg, cap, "main menu item %d should be %s (benchmark %s)", k + 1, R3MM_LABEL[want_all[i]], bench ? "on" : "off"); return 1; }
            k++;
        }
        if (n != k + (t.n_own < R3MM_MAX_OWN ? t.n_own : R3MM_MAX_OWN)) { snprintf(msg, cap, "main menu has %d items, expected %d", n, k + t.n_own); return 1; }
        for (int i = 0; i < t.n_own && i < R3MM_MAX_OWN; i++)
            if (ids[k + i] != R3MM_OWN0 + i) { snprintf(msg, cap, "the app's own items must follow Quit"); return 1; }
    }
    return 0;
}

/* The About box: the name, "v<version> <build>" (the splash's version line), then up to 8 lines of licence and credits (NULL-terminated). */
static inline void r3_about_draw(R3Screen *s, const char *app, const char *version, const char *build, const char *const *lines) {
    char line[96];
    int n = 0;
    while (lines && lines[n] && n < 8) n++;
    int h = n + 6, w = 56, col = (R3_COLS - w) / 2, row = R3_DISPLAY_TOP + (R3_DISPLAY_ROWS - h) / 2;
    r3_box(s, col, row, w, h, &(R3Box){1, "About", NULL, "any key closes", 0});
    r3_text_fit(s, col + 2, row + 1, app, w - 4, R3_ST(R3_ST_STRONG));
    snprintf(line, sizeof line, "v%s %s", version ? version : "?", build ? build : "");
    r3_text_fit(s, col + 2, row + 2, line, w - 4, R3_ST(R3_ST_HINT));
    for (int i = 0; i < n; i++) r3_text_fit(s, col + 2, row + 4 + i, lines[i], w - 4, R3_ST(R3_ST_BODY));
}

/* The colour theme picker: the list of r3_themes with the one in use marked. Moving the selection shows it at once (the caller applies
 * r3_theme_apply(sel) after each move); A keeps it, B goes back to the one in use. */
static inline void r3_theme_pick_draw(R3Screen *s, int sel, int current) {
    R3Item items[R3_THEME_COUNT];
    memset(items, 0, sizeof items);
    for (int i = 0; i < R3_THEME_COUNT; i++) {
        snprintf(items[i].label, sizeof items[i].label, "%s", r3_themes[i].name);
        snprintf(items[i].value, sizeof items[i].value, "%s%s", r3_themes[i].look, i == current ? " *" : "");
    }
    int h = R3_THEME_COUNT + 2 > R3_DISPLAY_ROWS - 2 ? R3_DISPLAY_ROWS - 2 : R3_THEME_COUNT + 2, w = 40;
    R3List l = {(R3_COLS - w) / 2, R3_DISPLAY_TOP + (R3_DISPLAY_ROWS - h) / 2, w, h, "Colour theme", "A keep  B back", items, R3_THEME_COUNT, sel, {NULL}};
    r3_list_dialog(s, &l);
}

#endif /* R3I6NOS_MAINMENU_H */

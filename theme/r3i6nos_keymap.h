/* r3i6nos Keymap for native apps (C): the main menu's compulsory Keymap item (rule 17, docs/r3i6nos-mapping.md, "The Keymap"). MIT.
 *
 * What is built here (2026-11-20): the **help pages** (the system's inputs, then one page of one-touch buttons for each panel the app describes) and the **test page**
 * (every press with what it does in the panel the Keymap was opened over, the buttons held; it acts on nothing, and L1 + R1 leaves it). **Remapping is not built in
 * the C theme yet** (FM1-NTT's host/fm1ntt.c has the first: a permutation of the ten bindable buttons, refusal of the system's, conflict asked, keymap.txt): an app
 * that needs a changed button waits for it, and says so in its README.
 * The app owns the data: R3KmPage tables (a button, what it does there), passed to r3_km_open. This file draws, turns pages and records the test.
 * Needs r3i6nos_ansiapps.h and r3i6nos_input.h first. Untested on a unit. */
#ifndef R3I6NOS_KEYMAP_H
#define R3I6NOS_KEYMAP_H

#include <stdio.h>
#include <string.h>

#define R3KM_MAX_PAGES 8
#define R3KM_ROWS 14                       /* rows a page shows: more is a second page, never a scroll */

typedef struct { const char *btn, *what; } R3KmRow;
typedef struct { const char *title; const R3KmRow *rows; int n; } R3KmPage;

typedef struct {
    int open, page, npages;                  /* the last page is the test */
    R3KmPage pages[R3KM_MAX_PAGES];          /* the system's page, then the app's; the test page is drawn here */
    uint8_t held[R3_BTN_COUNT];
    int last_btn;                            /* the last button pressed on the test page, or -1 */
    char last_what[64];                      /* what it does in the panel the Keymap was opened over */
    int test_panel;                          /* which of the app's pages the test reads (the one it was opened over, 1-based after the system page) */
} R3Keymap;

#ifdef R3_WITH_MONO
/* The R3i6nos Mono glyph of a button as the Keymap names it ("A", "L1", "Up", "Select", "Fn"...), `on` for pressed; NULL for a name that is not a
 * button. A, B, X and Y are one cell, every other button two (a two-cell glyph is printed as one string, never split). */
static inline const char *r3_km_glyph(const char *btn, int on) {
    char key[40];
    static const char *const shoulders[] = {"L1", "R1", "L2", "R2", "L3", "R3"};
    const char *st = on ? "on" : "off";
    if (!btn || !*btn) return NULL;
    if (strlen(btn) == 1 && strchr("ABXY", btn[0])) snprintf(key, sizeof key, "pad.btn.%c.%s", btn[0], st);
    else if (!strcmp(btn, "Select")) snprintf(key, sizeof key, "pad.pill.sel.%s", st);
    else if (!strcmp(btn, "Start")) snprintf(key, sizeof key, "pad.pill.sta.%s", st);
    else if (!strcmp(btn, "Fn")) snprintf(key, sizeof key, "pad.pill.fn.%s", st);
    else if (!strcmp(btn, "Up") || !strcmp(btn, "Down") || !strcmp(btn, "Left") || !strcmp(btn, "Right")) {
        snprintf(key, sizeof key, "pad.dpad.%c%s", btn[0] + 32, btn + 1);   /* the D-pad glyph shows the direction pressed: no off state */
    } else {
        int found = 0;
        for (size_t i = 0; i < sizeof shoulders / sizeof shoulders[0]; i++) if (!strcmp(btn, shoulders[i])) found = 1;
        if (!found) return NULL;
        snprintf(key, sizeof key, "pad.shoulder.%c%c.%s", btn[0] + 32, btn[1], st);
    }
    return r3mono_name(key);
}

/* A label made only of button names joined by "+" ("L1+R1", "R1+Fn") as glyphs with a "+" between, from (col, row); returns the cells used, 0 when the label
 * is anything else (the caller draws it as text). */
static inline int r3_km_chord_glyphs(R3Screen *s, int col, int row, const char *label, int on, int fg, int bg) {
    char part[24];
    const char *p = label;
    const char *g[4];
    int n = 0;
    while (*p) {
        size_t len = strcspn(p, "+");
        if (len == 0 || len >= sizeof part || n == 4) return 0;
        memcpy(part, p, len);
        part[len] = 0;
        if (!(g[n] = r3_km_glyph(part, on))) return 0;
        n++;
        p += len;
        if (*p == '+') p++;
    }
    int used = 0;
    for (int i = 0; i < n; i++) {
        if (i) { r3_put(s, col + used, row, '+', fg, bg); used++; }
        used += r3_puts_mono(s, col + used, row, g[i], fg, bg);
    }
    return used;
}
#endif

/* The system's inputs: the same in every app. */
static const R3KmRow R3KM_SYSTEM[] = {
    {"L1+R1", "Main menu (keyboard: ` backquote)"}, {"L2+R2", "Contextual menu (keyboard: \\ backslash)"}, {"Fn", "Next panel (a carousel)"},
    {"L1+Fn", "First panel"}, {"R1+Fn", "Previous panel (the one shown before)"}, {"R1 held", "Increase: Up/Down page, Left/Right home/end"},
    {"D-pad", "Moves the selection (R1: pages, home, end)"}, {"Right stick", "The same as the D-pad, at up to 6 steps a second"}, {"Left stick", "The pointer, in an app that has one"},
    {"L3 / R3", "Virtual mouse / keyboard, in an app that has them"}, {"Quit", "Only the main menu's Quit leaves: no button, no Esc"},
};

/* Opens over the app's pages (up to R3KM_MAX_PAGES - 2: the system's page and the test take two); `over` is the app page (0-based) shown first and read by the test. */
static inline void r3_km_open(R3Keymap *k, const R3KmPage *app_pages, int n_app, int over) {
    memset(k, 0, sizeof *k);
    k->open = 1;
    k->last_btn = -1;
    k->pages[0] = (R3KmPage){"System inputs", R3KM_SYSTEM, (int)(sizeof R3KM_SYSTEM / sizeof R3KM_SYSTEM[0])};
    int n = 1;
    for (int i = 0; i < n_app && n < R3KM_MAX_PAGES - 1; i++) k->pages[n++] = app_pages[i];
    k->pages[n++] = (R3KmPage){"Test", NULL, 0};
    k->npages = n;
    k->test_panel = over >= 0 && over < n_app ? over : 0;
    k->page = over >= 0 && over < n_app ? 1 + over : 0;
}

static inline int r3_km_on_test(const R3Keymap *k) { return k->page == k->npages - 1; }

/* A raw button, down or up, while the Keymap is open: the test page records it. `what` is what the button does in the panel under test; NULL looks it up in that panel's page
 * by the button's name (a row whose button is, say, "A"). */
static inline void r3_km_button(R3Keymap *k, int b, int down, const char *what) {
    if (b < 0 || b >= R3_BTN_COUNT) return;
    k->held[b] = down ? 1 : 0;
    if (!down || !r3_km_on_test(k)) return;
    k->last_btn = b;
    if (!what && k->npages > 2) {            /* the row of the panel under test whose button is this one's name (rows such as "R1+Up" are chords, not looked up) */
        const R3KmPage *p = &k->pages[1 + k->test_panel];
        for (int i = 0; i < p->n; i++) if (!strcmp(p->rows[i].btn, r3_button_name(b))) { what = p->rows[i].what; break; }
    }
    snprintf(k->last_what, sizeof k->last_what, "%s", what ? what : "");
}

/* One action; returns 1 when the Keymap closed. On the test page nothing but L1 + R1 closes it, and nothing turns the page. */
static inline int r3_km_key(R3Keymap *k, int action) {
    if (action == R3_MENU) { k->open = 0; return 1; }
    if (r3_km_on_test(k)) return 0;            /* the test acts on nothing: every button is only shown */
    if (action == R3_BACK) { k->open = 0; return 1; }
    if (action == R3_LEFT) k->page = k->page > 0 ? k->page - 1 : k->npages - 1;
    else if (action == R3_RIGHT) k->page = (k->page + 1) % k->npages;
    else if (action == R3_HOME) k->page = 0;
    else if (action == R3_END) k->page = k->npages - 1;
    return 0;
}

static inline void r3_km_draw(R3Screen *s, const R3Keymap *k) {
    int w = 60, h = R3KM_ROWS + 5, col = (R3_COLS - w) / 2, row = R3_DISPLAY_TOP + (R3_DISPLAY_ROWS - h) / 2 + 1;
    if (row < R3_DISPLAY_TOP) row = R3_DISPLAY_TOP;
    char title[64], foot[48];
    snprintf(title, sizeof title, "Keymap: %s", k->pages[k->page].title);
    snprintf(foot, sizeof foot, "Page %d/%d  %s", k->page + 1, k->npages, r3_km_on_test(k) ? "L1+R1 closes" : "Left/Right page  B close");
    r3_box(s, col, row, w, h, &(R3Box){1, title, NULL, foot, 0});
    const R3KmPage *p = &k->pages[k->page];
    if (!r3_km_on_test(k)) {
        for (int i = 0; i < p->n && i < R3KM_ROWS; i++) {
#ifdef R3_WITH_MONO     /* an app that opted into R3i6nos Mono sees the buttons as they are on the pad: a chord of button names is glyphs */
            int used = r3_km_chord_glyphs(s, col + 2, row + 1 + i, p->rows[i].btn, 0, R3_ST(R3_ST_STRONG));
            if (!used) r3_text_fit(s, col + 2, row + 1 + i, p->rows[i].btn, 12, R3_ST(R3_ST_STRONG));
#else
            r3_text_fit(s, col + 2, row + 1 + i, p->rows[i].btn, 12, R3_ST(R3_ST_STRONG));
#endif
            r3_text_fit(s, col + 15, row + 1 + i, p->rows[i].what, w - 17, R3_ST(R3_ST_BODY));
        }
        return;
    }
    char line[96];
    r3_text_fit(s, col + 2, row + 1, "Press any button: nothing is done, only shown.", w - 4, R3_ST(R3_ST_HINT));
    if (k->last_btn >= 0) snprintf(line, sizeof line, "Last: %s  ->  %s", r3_button_name(k->last_btn), k->last_what[0] ? k->last_what : "(nothing in this panel)");
    else snprintf(line, sizeof line, "Last: none yet");
    r3_text_fit(s, col + 2, row + 3, line, w - 4, R3_ST(R3_ST_STRONG));
    char held[128] = "Held: ";
    int any = 0;
    for (int b = 0; b < R3_BTN_COUNT; b++) if (k->held[b]) { strncat(held, r3_button_name(b), sizeof held - strlen(held) - 2); strcat(held, " "); any = 1; }
    if (!any) strcat(held, "nothing");
    r3_text_fit(s, col + 2, row + 5, held, w - 4, R3_ST(R3_ST_BODY));
#ifdef R3_WITH_MONO     /* ...and the held buttons again as the pad's own glyphs, in their pressed state, on the next row */
    int x = col + 2;
    for (int b = 0; b < R3_BTN_COUNT; b++) {
        const char *g = k->held[b] ? r3_km_glyph(r3_button_name(b), 1) : NULL;
        if (g && x + 3 < col + w - 2) x += r3_puts_mono(s, x, row + 7, g, R3_ST(R3_ST_HINT)) + 1;
    }
#endif
}

/* "The help pages are in order and the test is last": 0, else 1 with msg. */
static inline int r3_km_selftest(const R3KmPage *app_pages, int n_app, char *msg, size_t cap) {
    R3Keymap k;
    r3_km_open(&k, app_pages, n_app, 0);
    if (k.npages != 2 + (n_app < R3KM_MAX_PAGES - 2 ? n_app : R3KM_MAX_PAGES - 2)) { snprintf(msg, cap, "keymap has %d pages", k.npages); return 1; }
    if (strcmp(k.pages[0].title, "System inputs") || !r3_km_on_test(&(R3Keymap){.page = k.npages - 1, .npages = k.npages})) { snprintf(msg, cap, "keymap: the system page first and the test last"); return 1; }
    if (n_app > 0 && k.page != 1) { snprintf(msg, cap, "keymap does not open on the panel's page"); return 1; }
    for (int i = 0; i < k.npages; i++) if (k.pages[i].n > R3KM_ROWS) { snprintf(msg, cap, "keymap page %d has more than %d rows: it must be split, never scrolled", i + 1, R3KM_ROWS); return 1; }
    r3_km_button(&k, R3_BTN_A, 1, "x");
    if (k.last_btn != -1 && !r3_km_on_test(&k)) { snprintf(msg, cap, "keymap: only the test page records presses"); return 1; }
    return 0;
}

#endif /* R3I6NOS_KEYMAP_H */

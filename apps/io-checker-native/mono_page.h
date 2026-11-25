/* The IO Checker's ANSIapps Mono page: a screen test of the 10x20 font (docs/ANSIAPPS-MONO-FONT.md), twelve panels. The first eight draw the glyphs the way an
 * app should use them; the last four are for looking at all of it: the wide and tall counterparts, every named glyph, every code point, and the portrait
 * maker with its checks (font_check.h). The rules the panels show:
 *   - a glyph is printed by name (r3mono_name("pad.btn.A.on"), or the R3I_ macro when the name is known at build time) and never by code point;
 *   - a two-cell icon (app, system and file icons, triggers, the D-pad, sticks, key caps, switches, waveforms, big knobs) is ONE string of two cells, printed
 *     together on one row: the halves are never separated, never coloured differently, never given a different background;
 *   - controls have explicit glyphs for every state (off, on, disabled, a level 0-8), so a state never rests on colour alone;
 *   - an ordinary character (a letter on a key, a label) is a plain cell beside the glyphs, in the same colours.
 * Needs R3_WITH_MONO before r3i6nos_ansiapps.h, and the L_ button enum (io_checker.c). Untested on a unit. */
#ifndef IOC_MONO_PAGE_H
#define IOC_MONO_PAGE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font_check.h"

enum { MONO_TEXT, MONO_PAD, MONO_KEYS, MONO_PIANO, MONO_UI, MONO_MUSIC, MONO_ICONS_A, MONO_ICONS_B, MONO_WIDETALL, MONO_NAMES, MONO_CODES, MONO_PORTRAIT, MONO_PANELS };
static const char *const MONO_PANEL_NAME[MONO_PANELS] = {"Text mode", "Gamepad", "Keyboard", "Piano", "UI widgets", "Music", "Icons: apps, files", "Icons: system",
                                                          "Wide and tall", "Named glyphs", "Code points", "Portrait maker"};

typedef struct {
    int panel;
    int kx, ky, layer;              /* the keyboard panel: cursor and layer */
    char typed[40];
    int pcur;                       /* the piano panel: the cursor, a semitone from the first C (0-36) */
    unsigned long long notes;       /* the piano's notes that are on (bit = semitone) */
    int wt_page;                    /* wide and tall: the page */
    int nm_set, nm_group, nm_cur;   /* named glyphs: the size set (one cell, two across, bigger), the name group (0 = all), the cursor in that list */
    int cp_cur;                     /* code points: the cursor (an index into the font's code points) */
    unsigned pt_seed;               /* portrait maker: the seed of the portrait shown, its layout, and the colours' seed */
    int pt_layout;
} MonoState;

typedef struct {                    /* what the pad is doing now, filled by the app */
    unsigned char down[18];         /* indexed by the L_ enum */
    unsigned char dpad;             /* bits: 1 up, 2 down, 4 left, 8 right (buttons and hat together) */
    float ax[4];                    /* left x, left y, right x, right y */
    int have_pad;
} MonoLive;

#define MONO_KEYS_W 10
static const char *const MONO_KB[3][4] = {{"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm., "},
                                          {"1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM., "},
                                          {"!@#$%^&*()", "-_=+[]{};:", "'\",.<>/?\\|", "`~ .,;:!?#"}};
static const char *const MONO_SPECIAL[6] = {"shift", "space", "bksp", "enter", "ok", "cancel"};
#define MONO_PIANO_WHITE 22                     /* three octaves and the next C */
#define MONO_PIANO_SEMIS 37

/* ---- helpers -------------------------------------------------------------------------------------------------------------- */

/* A glyph by name; a missing name draws a red ? so a test and the eye both see it. Returns the cells used. */
static int mp(R3Screen *s, int col, int row, const char *name, int fg, int bg) {
    const char *u = r3mono_name(name);
    if (!u) { r3_text(s, col, row, "?", R3_ST(R3_ST_ERROR)); return 1; }
    return r3_puts_mono(s, col, row, u, fg, bg);
}
#define MP(s, c, r, name) mp(s, c, r, name, R3_ST(R3_ST_BODY))
#define MPS(s, c, r, name, st) mp(s, c, r, name, R3_ST(st))
/* UTF-8 text through the font, so characters CP437 lacks print too. */
static int mt(R3Screen *s, int col, int row, const char *utf8, int style) { return r3_puts_mono(s, col, row, utf8, R3_ST(style)); }

static const char *const MONO_NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static int mono_white_semi(int wi) { static const int pc[7] = {0, 2, 4, 5, 7, 9, 11}; return (wi / 7) * 12 + pc[wi % 7]; }
static const char *mono_white_kind(int wi) {
    if (wi == MONO_PIANO_WHITE - 1) return "N";                   /* the last key has no black key beside it */
    static const char *const k[7] = {"R", "LR", "L", "R", "LR", "LR", "L"};
    return k[wi % 7];
}
static int mono_note_on(const MonoState *m, int semis) { return semis >= 0 && semis < MONO_PIANO_SEMIS && (m->notes >> semis & 1); }
static int mono_stick_q(float v) { return v < -0.8f ? -2 : v < -0.35f ? -1 : v > 0.8f ? 2 : v > 0.35f ? 1 : 0; }

static void mono_frame(R3Screen *s, int n) {
    char right[48];
    snprintf(right, sizeof right, "Panel %d/%d  %s", n + 1, MONO_PANELS, MONO_PANEL_NAME[n]);
    r3_box(s, 0, R3_DISPLAY_TOP, R3_COLS, R3_DISPLAY_ROWS, &(R3Box){1, "ANSIapps Mono", right, "Fn next panel  B back", 0});
}

/* ---- panel 1: text mode ---------------------------------------------------------------------------------------------------- */

static void mono_text(R3Screen *s) {
    r3_text(s, 2, 2, "CP437, all 256", R3_ST(R3_ST_STRONG));
    for (int i = 0; i < 256; i++) r3_put(s, 2 + (i % 16) * 2, 3 + i / 16, i, R3_ST(R3_ST_BODY));
    mt(s, 36, 2, "Lines, blocks, text", R3_ST_STRONG);
    mt(s, 36, 3, "╭─────╮ ┏━━━━━┓ ╔═════╗", R3_ST_BODY);
    mt(s, 36, 4, "│     │ ┃     ┃ ║     ║", R3_ST_BODY);
    mt(s, 36, 5, "╰─────╯ ┗━━━━━┛ ╚═════╝", R3_ST_BODY);
    mt(s, 36, 6, "▁▂▃▄▅▆▇█ ▏▎▍▌▋▊▉█", R3_ST_BODY);
    mt(s, 36, 7, "░▒▓█ ▖▗▘▝▚▞▙▛▜▟ ▀▄", R3_ST_BODY);
    mt(s, 36, 8, "\U0001FB00\U0001FB01\U0001FB02\U0001FB03\U0001FB04\U0001FB05\U0001FB06 \U0001CD00\U0001CD40 ⠁⠃⠇⡇⣇⣧⣷⣿", R3_ST_BODY);
    mt(s, 36, 9, "⣀⣤⣶⣿⣶⣤⣀⠤⠒⠉⠒⠤⣀⣤⣶⣿⣶⣤⣀", R3_ST_BODY);
    mt(s, 36, 10, "ÀÉÎÕÜ ąčęł őű øđ ß ŁŞŽ", R3_ST_BODY);
    mt(s, 36, 11, "“Quotes” – — … € ™ © ® ¾", R3_ST_BODY);
    mt(s, 36, 12, "≠ ≤ ≥ ≈ → ⇒ ∀ ∃ ∑ ∫ √ ∞ Δ", R3_ST_BODY);
    r3_text(s, 36, 13, " main ", R3_WHITE, R3_BLUE);                         /* Powerline: the separator's colours are the two segments' */
    r3_put_mono(s, 42, 13, 0xE0B0, R3_BLUE, R3_GREEN);
    r3_put_mono(s, 43, 13, 0xE0A0, R3_BLACK, R3_GREEN);
    r3_text(s, 44, 13, " dev ", R3_BLACK, R3_GREEN);
    r3_put_mono(s, 49, 13, 0xE0B0, R3_GREEN, R3_BLACK);
    r3_put_mono(s, 51, 13, 0xE0B4, R3_CYAN, R3_BLACK);
    r3_text(s, 52, 13, " ok ", R3_BLACK, R3_CYAN);
    r3_put_mono(s, 56, 13, 0xE0B6, R3_CYAN, R3_BLACK);
    mt(s, 36, 14, "↖↗↘↙ ←↑→↓ ↔↕ ⇐⇒⇔ ⇑⇓", R3_ST_BODY);
    mt(s, 36, 15, "●○■□▪▫▲▼◆ ♪♫ ⠿", R3_ST_BODY);
    r3_text(s, 36, 16, "Not in the font yet:", R3_ST(R3_ST_HINT));
    r3_text(s, 36, 17, "check marks, ballot boxes,", R3_ST(R3_ST_HINT));
    r3_text(s, 36, 18, "media keys, flats, sharps", R3_ST(R3_ST_HINT));
    r3_text(s, 36, 19, "0O1lI| rn m {}[]() <>", R3_ST(R3_ST_STRONG));
}

/* ---- panel 2: the gamepad, live ---------------------------------------------------------------------------------------------- */

static void mono_btn(R3Screen *s, int col, int row, const char *lab, int on) {
    char n[48];
    snprintf(n, sizeof n, "pad.btn.%s.%s", lab, on ? "on" : "off");
    MPS(s, col, row, n, on ? R3_ST_OK : R3_ST_BODY);
}
static void mono_pair(R3Screen *s, int col, int row, const char *fmt_name, int on) { MPS(s, col, row, fmt_name, on ? R3_ST_OK : R3_ST_BODY); }

static void mono_pad(R3Screen *s, const MonoLive *l) {
    const unsigned char *d = l->down;
    if (!l->have_pad) r3_text(s, 2, 2, "No pad found: everything shows released.", R3_ST(R3_ST_WARN));
    else r3_text(s, 2, 2, "Live. Filled = down, ring = up, dotted = disabled.", R3_ST(R3_ST_BODY));
    char n[64];
    snprintf(n, sizeof n, "pad.trig.l.%d", d[L_L2] ? 8 : 0); mono_pair(s, 4, 4, n, d[L_L2]);
    mono_pair(s, 7, 4, d[L_L1] ? "pad.shoulder.l1.on" : "pad.shoulder.l1.off", d[L_L1]);
    mono_pair(s, 53, 4, d[L_R1] ? "pad.shoulder.r1.on" : "pad.shoulder.r1.off", d[L_R1]);
    snprintf(n, sizeof n, "pad.trig.r.%d", d[L_R2] ? 8 : 0); mono_pair(s, 56, 4, n, d[L_R2]);
    const char *dn = l->dpad == 1 ? "up" : l->dpad == 2 ? "down" : l->dpad == 4 ? "left" : l->dpad == 8 ? "right" : l->dpad == 5 ? "upleft" : l->dpad == 9 ? "upright" : l->dpad == 6 ? "downleft" : l->dpad == 10 ? "downright" : "neutral";
    snprintf(n, sizeof n, "pad.dpad.%s", dn); mono_pair(s, 6, 6, n, l->dpad);
    mono_pair(s, 20, 6, d[L_SELECT] ? "pad.pill.sel.on" : "pad.pill.sel.off", d[L_SELECT]);
    mono_pair(s, 26, 6, d[L_FN] ? "pad.pill.fn.on" : "pad.pill.fn.off", d[L_FN]);
    mono_pair(s, 32, 6, d[L_START] ? "pad.pill.sta.on" : "pad.pill.sta.off", d[L_START]);
    mono_btn(s, 52, 5, "X", d[L_X]); mono_btn(s, 50, 6, "Y", d[L_Y]); mono_btn(s, 54, 6, "A", d[L_A]); mono_btn(s, 52, 7, "B", d[L_B]);
    for (int k = 0; k < 2; k++) {
        int col = k ? 44 : 12, ax = k ? 2 : 0, pressed = d[k ? L_R3 : L_L3];
        if (pressed) snprintf(n, sizeof n, "pad.stick.press");
        else snprintf(n, sizeof n, "pad.stick.%d.%d", mono_stick_q(l->ax[ax]), mono_stick_q(l->ax[ax + 1]));
        mono_pair(s, col, 9, n, pressed || mono_stick_q(l->ax[ax]) || mono_stick_q(l->ax[ax + 1]));
        r3_text(s, col - 3, 10, k ? "R stick" : "L stick", R3_ST(R3_ST_HINT));
    }
    r3_text(s, 2, 12, "One button, every state:", R3_ST(R3_ST_STRONG));
    MP(s, 28, 12, "pad.btn.A.off"); MP(s, 29, 12, "pad.btn.A.on"); MP(s, 30, 12, "pad.btn.A.dis");
    MP(s, 32, 12, "pad.btn.tri.off"); MP(s, 33, 12, "pad.btn.cir.on"); MP(s, 34, 12, "pad.btn.cro.off"); MP(s, 35, 12, "pad.btn.squ.on");
    static const char *const more[] = {"C", "Z", "1", "2", "3", "4", "5", "6", "7", "8"};
    for (int i = 0; i < 10; i++) { snprintf(n, sizeof n, "pad.btn.%s.%s", more[i], i % 2 ? "on" : "off"); MP(s, 37 + i, 12, n); }
    r3_text(s, 2, 13, "L2 / R2 level 0 to 8:", R3_ST(R3_ST_STRONG));
    for (int i = 0; i < 9; i++) { snprintf(n, sizeof n, "pad.trig.%c.%d", i & 1 ? 'r' : 'l', i); MP(s, 28 + 2 * i, 13, n); }
    r3_text(s, 2, 14, "D-pad, nine states:", R3_ST(R3_ST_STRONG));
    static const char *const dp[9] = {"neutral", "up", "down", "left", "right", "upleft", "upright", "downleft", "downright"};
    for (int i = 0; i < 9; i++) { snprintf(n, sizeof n, "pad.dpad.%s", dp[i]); MP(s, 28 + 2 * i, 14, n); }
    r3_text(s, 2, 15, "Stick x -2..2, y -2..2:", R3_ST(R3_ST_STRONG));
    for (int i = -2; i <= 2; i++) { snprintf(n, sizeof n, "pad.stick.%d.0", i); MP(s, 28 + 2 * (i + 2), 15, n); snprintf(n, sizeof n, "pad.stick.0.%d", i); MP(s, 40 + 2 * (i + 2), 15, n); }
    MP(s, 52, 15, "pad.stick.press");
    r3_text(s, 2, 16, "Pills, off / on / disabled:", R3_ST(R3_ST_STRONG));
    static const char *const pill[] = {"sel", "sta", "fn", "menu"};
    for (int i = 0; i < 4; i++) for (int st = 0; st < 3; st++) { snprintf(n, sizeof n, "pad.pill.%s.%s", pill[i], st == 0 ? "off" : st == 1 ? "on" : "dis"); MP(s, 28 + i * 7 + st * 2, 16, n); }
    r3_text(s, 2, 17, "Two cells = one icon:", R3_ST(R3_ST_STRONG));
    r3_text(s, 28, 17, "each is one string", R3_ST(R3_ST_HINT));
}

/* ---- panel 3: the on-screen keyboard -------------------------------------------------------------------------------------------- */

static void mono_key(R3Screen *s, int col, int row, const char *base, int pressed) {
    char n[64];
    snprintf(n, sizeof n, "key.%s.%s", base, pressed ? "dn" : "up");
    MP(s, col, row, n);
}

static void mono_keys(R3Screen *s, const MonoState *m) {
    r3_text(s, 2, 2, "Every key is a two-cell cap; the focused key is pressed.", R3_ST(R3_ST_BODY));
    char base[24];
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < MONO_KEYS_W; x++) {
            unsigned char c = (unsigned char)MONO_KB[m->layer][y][x];
            if (c == ' ') snprintf(base, sizeof base, "space"); else snprintf(base, sizeof base, "u%04x", c);
            mono_key(s, 3 + x * 3, 4 + y, base, m->ky == y && m->kx == x);
        }
    for (int x = 0; x < 6; x++) mono_key(s, 3 + x * 3, 8, MONO_SPECIAL[x], m->ky == 4 && m->kx == x);
    r3_text(s, 3, 10, "Typed:", R3_ST(R3_ST_STRONG));
    char line[64];
    snprintf(line, sizeof line, "%s_", m->typed);
    r3_text_fit(s, 10, 10, line, 40, R3_ST(R3_ST_BODY));
    r3_text(s, 3, 12, "D-pad moves   A types   X deletes   Y changes the layer", R3_ST(R3_ST_HINT));
    static const char *const lay[3] = {"abc", "ABC", "#+="};
    char ls[48];
    snprintf(ls, sizeof ls, "layer %s    raised and pressed:", lay[m->layer]);
    r3_text(s, 3, 13, ls, R3_ST(R3_ST_BODY));
    MP(s, 36, 13, "key.u0041.up"); MP(s, 39, 13, "key.u0041.dn");
    static const char *const sp[] = {"esc", "tab", "ctrl", "alt", "fn", "del", "ins", "home", "end", "pgup", "pgdn", "caps", "abc", "sym", "up", "down", "left", "right",
                                     "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12", "show", "hide"};
    for (int i = 0; i < 32; i++) mono_key(s, 3 + (i % 8) * 3, 15 + i / 8, sp[i], 0);
    r3_text(s, 30, 15, "Special keys: raised caps", R3_ST(R3_ST_HINT));
}

/* ---- panel 4: the piano --------------------------------------------------------------------------------------------------------- */

static void mono_piano(R3Screen *s, const MonoState *m) {
    r3_text(s, 2, 2, "A virtual piano: black-key indicators above, keys below.", R3_ST(R3_ST_BODY));
    char n[48];
    for (int wi = 0; wi < MONO_PIANO_WHITE; wi++) {
        int semi = mono_white_semi(wi), pc = wi % 7;
        const char *kind = mono_white_kind(wi);
        int l = strchr(kind, 'L') && mono_note_on(m, semi - 1), r = strchr(kind, 'R') && mono_note_on(m, semi + 1);
        snprintf(n, sizeof n, "piano.%s.%d%d%d", kind, mono_note_on(m, semi), l, r);
        MPS(s, 2 + wi, 5, n, R3_ST_STRONG);
        if (pc == 0 || pc == 1 || pc == 3 || pc == 4 || pc == 5) {     /* a black key between this key and the next: its indicator is a pair over both cells */
            if (wi + 1 < MONO_PIANO_WHITE) MPS(s, 2 + wi, 4, mono_note_on(m, semi + 1) ? "piano.bk.on" : "piano.bk.off", mono_note_on(m, semi + 1) ? R3_ST_OK : R3_ST_BODY);
        }
        snprintf(n, sizeof n, "piano.name.%s", MONO_NOTE[semi % 12]);
        MP(s, 2 + wi, 6, n);
        if (pc == 0) { char o[4]; snprintf(o, sizeof o, "%d", 3 + wi / 7); r3_text(s, 2 + wi, 7, o, R3_ST(R3_ST_HINT)); }
    }
    int cwhite = -1;                                   /* a black key's caret sits under the cell on its right */
    for (int wi = 0; wi < MONO_PIANO_WHITE; wi++) if (mono_white_semi(wi) == m->pcur) cwhite = wi;
    if (cwhite < 0) for (int wi = 0; wi < MONO_PIANO_WHITE; wi++) if (mono_white_semi(wi) == m->pcur - 1) cwhite = wi + 1;
    if (cwhite >= 0) r3_text(s, 2 + cwhite, 8, "^", R3_ST(R3_ST_STRONG));
    char cn[16];
    snprintf(cn, sizeof cn, "%s%d", MONO_NOTE[m->pcur % 12], 3 + m->pcur / 12);
    char line[96];
    snprintf(line, sizeof line, "cursor %-4s %s", cn, mono_note_on(m, m->pcur) ? "ON " : "off");
    r3_text(s, 2, 10, line, R3_ST(mono_note_on(m, m->pcur) ? R3_ST_OK : R3_ST_BODY));
    char list[160] = "";
    int cnt = 0;
    for (int i = 0; i < MONO_PIANO_SEMIS; i++) if (mono_note_on(m, i)) {
        char t[12];
        snprintf(t, sizeof t, "%s%s%d", cnt ? " " : "", MONO_NOTE[i % 12], 3 + i / 12);
        if (strlen(list) + strlen(t) < sizeof list - 1) strcat(list, t);
        cnt++;
    }
    r3_text(s, 2, 11, "Notes on:", R3_ST(R3_ST_STRONG));
    r3_text_fit(s, 12, 11, cnt ? list : "none", 48, R3_ST(R3_ST_BODY));
    r3_text(s, 2, 13, "Left/right a semitone  A on/off  Y chord  X all off", R3_ST(R3_ST_HINT));
    r3_text(s, 2, 15, "Key off", R3_ST(R3_ST_BODY));  MP(s, 10, 15, "piano.R.000");
    r3_text(s, 14, 15, "on", R3_ST(R3_ST_BODY));       MP(s, 17, 15, "piano.N.100");
    r3_text(s, 21, 15, "black off", R3_ST(R3_ST_BODY)); MP(s, 31, 15, "piano.bk.off");
    r3_text(s, 35, 15, "on", R3_ST(R3_ST_BODY));       MP(s, 38, 15, "piano.bk.on");
    r3_text(s, 2, 17, "A black key straddles two cells: its indicator is a", R3_ST(R3_ST_HINT));
    r3_text(s, 2, 18, "pair printed on the row above, over the same two cells.", R3_ST(R3_ST_HINT));
}

/* ---- panel 5: UI widgets -------------------------------------------------------------------------------------------------------- */

static void mono_ui(R3Screen *s, unsigned now) {
    int v = (int)(now / 40 % 220);                              /* 0..219 px of a 200 px bar, then a pause at full */
    int px = v > 200 ? 200 : v;
    r3_text(s, 2, 2, "Progress, rounded, one pixel steps:", R3_ST(R3_ST_STRONG));
    MP(s, 2, 3, px > 0 ? "ui.bar.l.full" : "ui.bar.l.empty");
    char n[32];
    for (int i = 0; i < 20; i++) {
        int f = px - i * 10; f = f < 0 ? 0 : f > 10 ? 10 : f;
        snprintf(n, sizeof n, "ui.bar.m.%d", f);
        MPS(s, 3 + i, 3, n, R3_ST_OK);
    }
    MP(s, 23, 3, px >= 200 ? "ui.bar.r.full" : "ui.bar.r.empty");
    char pc[16]; snprintf(pc, sizeof pc, "%3d%%", px / 2); r3_text(s, 26, 3, pc, R3_ST(R3_ST_BODY));
    r3_text(s, 2, 5, "Slider, sub-cell thumb:", R3_ST(R3_ST_STRONG));
    int tp = (int)(now / 30 % 360); tp = tp > 180 ? 360 - tp : tp;      /* 0..180 px and back */
    MP(s, 2, 6, "ui.slider.end.l");
    for (int i = 0; i < 18; i++) {
        int k = tp - i * 10;
        if (k >= 0 && k < 10) { snprintf(n, sizeof n, "ui.slider.thumb.%d", k > 8 ? 8 : k); MPS(s, 3 + i, 6, n, R3_ST_STRONG); }
        else MP(s, 3 + i, 6, "ui.slider.track");
    }
    MP(s, 21, 6, "ui.slider.end.r");
    r3_text(s, 2, 8, "Switches, spinner, ring:", R3_ST(R3_ST_STRONG));
    MP(s, 2, 9, "ui.switch.off"); MP(s, 5, 9, "ui.switch.on"); MP(s, 8, 9, "ui.switch.dis");
    snprintf(n, sizeof n, "ui.spinner.%d", (int)(now / 100 % 8)); MPS(s, 12, 9, n, R3_ST_OK);
    snprintf(n, sizeof n, "ui.ring.%d", (int)(now / 250 % 9)); MPS(s, 14, 9, n, R3_ST_OK);
    snprintf(n, sizeof n, "ui.wait.hourglass.%d", (int)(now / 400 % 8)); MPS(s, 2, 10, n, R3_ST_OK);   // 20x20 wait glyphs: two cells, one string
    snprintf(n, sizeof n, "ui.wait.orbit.%d", (int)(now / 100 % 8)); MPS(s, 5, 10, n, R3_ST_OK);
    MP(s, 17, 9, "ui.radio.off"); MP(s, 18, 9, "ui.radio.on"); MP(s, 20, 9, "ui.check.mixed");
    MP(s, 26, 9, "ui.badge.dot"); MP(s, 27, 9, "ui.badge.ring"); MP(s, 29, 9, "ui.chevron.up"); MP(s, 30, 9, "ui.chevron.down");
    MP(s, 31, 9, "ui.chevron.left"); MP(s, 32, 9, "ui.chevron.right"); MP(s, 34, 9, "ui.grip");
    r3_text(s, 2, 11, "Pills and a hairline frame:", R3_ST(R3_ST_STRONG));
    MP(s, 2, 12, "ui.pill.line.l"); MP(s, 3, 12, "ui.pill.line.m"); MP(s, 4, 12, "ui.pill.line.m"); MP(s, 5, 12, "ui.pill.line.r");
    MPS(s, 7, 12, "ui.pill.solid.l", R3_ST_OK); MPS(s, 8, 12, "ui.pill.solid.m", R3_ST_OK); MPS(s, 9, 12, "ui.pill.solid.m", R3_ST_OK); MPS(s, 10, 12, "ui.pill.solid.r", R3_ST_OK);
    MP(s, 14, 12, "ui.frame.thin.round.tl"); MP(s, 15, 12, "ui.frame.thin.h"); MP(s, 16, 12, "ui.frame.thin.h"); MP(s, 17, 12, "ui.frame.thin.round.tr");
    MP(s, 14, 13, "ui.frame.thin.v"); MP(s, 17, 13, "ui.frame.thin.v");
    MP(s, 14, 14, "ui.frame.thin.round.bl"); MP(s, 15, 14, "ui.frame.thin.h"); MP(s, 16, 14, "ui.frame.thin.h"); MP(s, 17, 14, "ui.frame.thin.round.br");
    r3_text(s, 20, 12, "scrollbar:", R3_ST(R3_ST_BODY));
    int th = (int)(now / 300 % 4);
    for (int y = 0; y < 5; y++) { int on = y >= th && y <= th + 2; MP(s, 31, 12 + y - 0, on ? (y == th ? "ui.scroll.thumb.top" : y == th + 2 ? "ui.scroll.thumb.bot" : "ui.scroll.thumb.mid") : "ui.scroll.track"); }
    r3_text(s, 2, 15, "Knobs, 13 positions (small, then big pairs):", R3_ST(R3_ST_STRONG));
    int kn = (int)(now / 120 % 24); kn = kn > 12 ? 24 - kn : kn;
    for (int i = 0; i < 13; i++) { snprintf(n, sizeof n, "mus.knob.s.%d", i); MPS(s, 2 + i, 16, n, i == kn ? R3_ST_OK : R3_ST_BODY); }
    for (int i = 0; i < 7; i++) { snprintf(n, sizeof n, "mus.knob.l.%d", i * 2); MPS(s, 20 + i * 3, 16, n, i * 2 == (kn & ~1) ? R3_ST_OK : R3_ST_BODY); }
    r3_text(s, 2, 18, "Bars and sliders step by pixels: ten glyphs a cell.", R3_ST(R3_ST_HINT));
}

/* ---- panel 6: music ----------------------------------------------------------------------------------------------------------------- */

static void mono_staff(R3Screen *s, int col, int row, const char *clef, const char *time, const int *pitch, const char *kind, int n) {
    char nm[48];
    MP(s, col, row, "staff.blank");
    MP(s, col + 1, row, clef);
    if (time) MP(s, col + 2, row, time); else MP(s, col + 2, row, "staff.blank");
    int x = col + 3;
    for (int i = 0; i < n; i++) {
        if (pitch[i] == 99) MP(s, x, row, "staff.bar");
        else if (pitch[i] == 98) MP(s, x, row, "staff.bar.end");
        else if (pitch[i] == 97) MP(s, x, row, "staff.rest.quarter");
        else { snprintf(nm, sizeof nm, "staff.note.%c.%d", kind[i], pitch[i] + 2); MP(s, x, row, nm); }
        x++;
        MP(s, x++, row, "staff.blank");
    }
}

static void mono_music(R3Screen *s, unsigned now) {
    r3_text(s, 2, 2, "A staff is a row of cells: clef, time, notes, bars.", R3_ST(R3_ST_STRONG));
    static const int melody[] = {-2, 0, 2, 5, 99, 4, 2, 0, 97, 99, 2, 2, 5, 7, 98};
    static const char mk[] = "qqqq?qqqq?hhhh?";
    mono_staff(s, 2, 3, "staff.clef.treble", "staff.time.4_4", melody, mk, 15);
    static const int bass[] = {-2, 0, 99, 2, 98};
    mono_staff(s, 2, 5, "staff.clef.bass", "staff.time.3_4", bass, "hhwh?", 5);
    r3_text(s, 2, 7, "Heads on 13 pitches (q, h), rests, bars, clefs, time:", R3_ST(R3_ST_HINT));
    char n[48];
    for (int i = 0; i < 13; i++) { snprintf(n, sizeof n, "staff.note.q.%d", i); MP(s, 2 + i, 8, n); snprintf(n, sizeof n, "staff.note.h.%d", i); MP(s, 17 + i, 8, n); }
    MP(s, 32, 8, "staff.rest.whole"); MP(s, 33, 8, "staff.rest.half"); MP(s, 34, 8, "staff.rest.quarter"); MP(s, 35, 8, "staff.rest.eighth"); MP(s, 36, 8, "staff.rest.sixteenth");
    MP(s, 38, 8, "staff.bar"); MP(s, 39, 8, "staff.bar.double"); MP(s, 40, 8, "staff.bar.end"); MP(s, 41, 8, "staff.repeat.start"); MP(s, 42, 8, "staff.repeat.end"); MP(s, 43, 8, "staff.repeat.both");
    MP(s, 45, 8, "staff.clef.alto"); MP(s, 46, 8, "staff.clef.perc"); MP(s, 47, 8, "staff.time.common"); MP(s, 48, 8, "staff.time.cut"); MP(s, 49, 8, "staff.time.6_8");
    r3_text(s, 2, 10, "In running text:", R3_ST(R3_ST_STRONG));
    mt(s, 20, 10, "♪ ♫   (the rest of the notation is the staff cells below)", R3_ST_BODY);
    mt(s, 20, 11, "Cmaj7  Fm7  G7  BΔ  A°  Esus4", R3_ST_BODY);
    r3_text(s, 2, 13, "Marks:", R3_ST(R3_ST_STRONG));
    static const char *const mk2[] = {"accent", "marcato", "staccato", "tenuto", "fermata", "trill", "mordent", "turn", "segno", "coda", "caesura", "breath", "flat", "natural", "sharp", "ped"};
    for (int i = 0; i < 16; i++) { snprintf(n, sizeof n, "mus.%s", mk2[i]); MP(s, 9 + i, 13, n); }
    MP(s, 27, 13, "mus.slur.up.l"); MP(s, 28, 13, "mus.slur.up.m"); MP(s, 29, 13, "mus.slur.up.m"); MP(s, 30, 13, "mus.slur.up.r");
    MP(s, 32, 13, "mus.mute"); MP(s, 33, 13, "mus.solo"); MP(s, 34, 13, "mus.recarm"); MP(s, 35, 13, "mus.loop"); MP(s, 36, 13, "mus.shuffle"); MP(s, 37, 13, "mus.repeat"); MP(s, 38, 13, "mus.metronome");
    r3_text(s, 2, 15, "Oscillators and curves (pairs):", R3_ST(R3_ST_STRONG));
    static const char *const w[] = {"sine", "tri", "saw", "square", "pulse12", "pulse25", "pulse75", "noise", "ramp"};
    int hot = (int)(now / 500 % 9);
    for (int i = 0; i < 9; i++) { snprintf(n, sizeof n, "mus.wave.%s", w[i]); MPS(s, 2 + i * 2, 16, n, i == hot ? R3_ST_OK : R3_ST_BODY); }
    static const char *const c[] = {"adsr", "lp", "hp", "bp", "notch"};
    for (int i = 0; i < 5; i++) { snprintf(n, sizeof n, "mus.curve.%s", c[i]); MP(s, 24 + i * 2, 16, n); }
    r3_text(s, 2, 18, "Tracker text: C-4 01 40 ... --- .. .. === ^^^ is plain.", R3_ST(R3_ST_HINT));
}

/* ---- panels 7 and 8: icons ----------------------------------------------------------------------------------------------------------- */

static void mono_icon_grid(R3Screen *s, int row, const char *const *names, const char *prefix, int n) {
    char nm[64];
    for (int i = 0; i < n; i++) {
        int col = 2 + (i % 6) * 10, r = row + i / 6;
        snprintf(nm, sizeof nm, "%s%s", prefix, names[i]);
        MP(s, col, r, nm);                                                  /* ONE string: both halves, side by side */
        r3_text_fit(s, col + 3, r, names[i], 7, R3_ST(R3_ST_BODY));
    }
}

static void mono_icons_a(R3Screen *s) {
    static const char *const apps[] = {"r3i6nos", "menu", "klamansi", "nocogama", "composer", "painter", "animator", "crunchy", "chordica", "tinyntt", "hydrolik", "musicue",
                                       "neumetik", "commander", "iochecker", "portmaster", "pocketide", "fm1ntt", "firmware"};
    static const char *const files[] = {"folder", "folder.open", "file.text", "file.code", "file.image", "file.audio", "file.video", "file.archive", "file.disk", "file.doc",
                                        "file.font", "file.exec", "file.music", "file.tracker", "file.midi", "file.data", "file.link"};
    r3_text(s, 2, 2, "Apps (each icon is two cells, 20 x 20 px):", R3_ST(R3_ST_STRONG));
    mono_icon_grid(s, 3, apps, "icon.app.", 19);
    r3_text(s, 2, 7, "Files:", R3_ST(R3_ST_STRONG));
    char nm[64];
    for (int i = 0; i < 17; i++) {
        int col = 2 + (i % 6) * 10, r = 8 + i / 6;
        snprintf(nm, sizeof nm, "icon.%s%s", strncmp(files[i], "file.", 5) ? "file." : "", files[i]);
        MP(s, col, r, nm);
        r3_text_fit(s, col + 3, r, files[i] + (strncmp(files[i], "file.", 5) ? 0 : 5), 7, R3_ST(R3_ST_BODY));
    }
    r3_text(s, 2, 12, "How to print one:", R3_ST(R3_ST_STRONG));
    r3_text(s, 2, 13, "r3_puts_mono(s, col, row, R3I_ICON_APP_HYDROLIK, fg, bg);", R3_ST(R3_ST_BODY));
    r3_text(s, 2, 14, "right: both halves in one string", R3_ST(R3_ST_OK));
    MP(s, 46, 14, "icon.app.hydrolik");
    r3_text(s, 2, 15, "wrong: halves apart, or in two colours", R3_ST(R3_ST_ERROR));
    const char *u = r3mono_name("icon.app.hydrolik");
    if (u) {                                                                /* the two halves, split apart on purpose, to show what a mistake looks like */
        const unsigned char *p = (const unsigned char *)u, *end = p + strlen(u);
        unsigned a = r3_utf8_next(&p, end), b = r3_utf8_next(&p, end);
        r3_put_mono(s, 46, 15, a, R3_ST(R3_ST_ERROR));
        r3_put_mono(s, 48, 15, b, R3_ST(R3_ST_ERROR));
        r3_put_mono(s, 52, 15, a, R3_ST(R3_ST_ERROR));
        r3_put_mono(s, 53, 15, b, R3_ST(R3_ST_OK));
    }
    r3_text(s, 2, 17, "An icon is its name: its code points never move.", R3_ST(R3_ST_HINT));
}

static void mono_icons_b(R3Screen *s) {
    static const char *const sysn[] = {"settings", "network", "ssh", "battery", "sound", "datacard", "clock", "report", "about", "update", "upload", "keyboard", "mirror", "power",
                                       "packages", "saved", "globe", "bug", "trash", "search", "star", "heart", "home", "user", "lock", "mail", "image", "add", "close", "check",
                                       "warning", "pad"};
    r3_text(s, 2, 2, "System (two cells):", R3_ST(R3_ST_STRONG));
    mono_icon_grid(s, 3, sysn, "icon.sys.", 32);
    r3_text(s, 2, 9, "Status, one cell each:", R3_ST(R3_ST_STRONG));
    static const char *const st[] = {"headphones", "usb", "dc", "card", "pad", "lock", "unlock", "clock", "bluetooth", "bat.0", "bat.1", "bat.2", "bat.3", "bat.4", "bat.charge",
                                     "wifi.0", "wifi.1", "wifi.2", "wifi.3", "vol.0", "vol.1", "vol.2", "vol.3", "mute", "bright", "download", "upload", "sd.out"};
    char nm[48];
    for (int i = 0; i < 28; i++) { snprintf(nm, sizeof nm, "sys.%s", st[i]); MP(s, 2 + i * 2, 10, nm); }
    r3_text(s, 2, 12, "Coding, one cell each:", R3_ST(R3_ST_STRONG));
    static const char *const cd[] = {"bp.on", "bp.off", "bp.cond", "bp.hit", "step.over", "step.into", "step.out", "restart", "error", "warn", "info", "hint", "ok", "todo",
                                     "commit", "merge", "tag", "add", "del", "mod", "ren", "conflict", "folder", "folder.open", "file", "link", "fold.open", "fold.closed", "branch", "cursor"};
    for (int i = 0; i < 30; i++) { snprintf(nm, sizeof nm, "code.%s", cd[i]); MP(s, 2 + i * 2, 13, nm); }
    r3_text(s, 2, 15, "Diagnostics in a gutter:", R3_ST(R3_ST_STRONG));
    MPS(s, 2, 16, "code.error", R3_ST_ERROR); r3_text(s, 4, 16, "main.c:12  expected ';'", R3_ST(R3_ST_BODY));
    MPS(s, 2, 17, "code.warn", R3_ST_WARN); r3_text(s, 4, 17, "main.c:20  unused variable", R3_ST(R3_ST_BODY));
    MPS(s, 2, 18, "code.bp.on", R3_ST_ERROR); r3_text(s, 4, 18, "main.c:31  breakpoint", R3_ST(R3_ST_BODY));
    r3_put_mono(s, 36, 16, 0xE0A0, R3_ST(R3_ST_STRONG)); r3_text(s, 38, 16, "main  +2 ~1", R3_ST(R3_ST_BODY));
    MPS(s, 36, 17, "code.fold.open", R3_ST_BODY); r3_text(s, 38, 17, "fold open", R3_ST(R3_ST_BODY));
    MPS(s, 36, 18, "code.fold.closed", R3_ST_BODY); r3_text(s, 38, 18, "fold closed", R3_ST(R3_ST_BODY));
}

/* ---- the font checks, run once -------------------------------------------------------------------------------------------------- */

static FcNames mp_names;
static FcPortrait mp_por;
static int mp_checked;
static void mono_checks(void) {
    if (mp_checked) return;
    fc_names(&mp_names);
    fc_portrait(&mp_por);
    mp_checked = 1;
}

/* A glyph of any size from its name's entry: every cell of every row, left to right and top to bottom. */
static void mp_put(R3Screen *s, int col, int row, const R3MonoName *e, int fg, int bg) {
    for (int r = 0; r < e->rows; r++)
        for (int c = 0; c < e->cols; c++) r3_put_mono(s, col + c, row + r, e->cp + (unsigned)(r * e->cols + c), fg, bg);
}

/* ---- panel 9: the wide and tall counterparts ----------------------------------------------------------------------------------------- */

/* One line for each base name that has a counterpart: the one-cell glyph, its `.wide` (twice across) and its `.tall` (twice across and down). */
typedef struct { const R3MonoName *base, *wide, *tall; } MonoWT;
#define MONO_WT_MAX 200
static MonoWT mp_wt[MONO_WT_MAX];
static int mp_wt_n, mp_wt_pages, mp_wt_start[40];
#define MONO_WT_ROWS 14

static int mp_cmp_cp(const void *a, const void *b) {
    const R3MonoName *x = *(const R3MonoName *const *)a, *y = *(const R3MonoName *const *)b;
    return x->cp < y->cp ? -1 : x->cp > y->cp;
}

static void mono_wt_build(void) {
    if (mp_wt_n) return;
    const R3MonoName *bases[MONO_WT_MAX];
    int nb = 0;
    for (int i = 0; i < R3MONO_NAMES && nb < MONO_WT_MAX; i++) {
        const R3MonoName *e = &r3mono_names[i];
        size_t n = strlen(e->name);
        if (n > 5 && (!strcmp(e->name + n - 5, ".wide") || !strcmp(e->name + n - 5, ".tall"))) {
            char base[96];
            snprintf(base, sizeof base, "%.*s", (int)(n - 5), e->name);
            const R3MonoName *b = r3mono_entry(base);
            int seen = 0;
            for (int k = 0; k < nb; k++) if (bases[k] == b) seen = 1;
            if (b && !seen) bases[nb++] = b;
        }
    }
    qsort(bases, (size_t)nb, sizeof bases[0], mp_cmp_cp);
    for (int k = 0; k < nb; k++) {
        char nm[112];
        snprintf(nm, sizeof nm, "%s.wide", bases[k]->name); mp_wt[k].wide = r3mono_entry(nm);
        snprintf(nm, sizeof nm, "%s.tall", bases[k]->name); mp_wt[k].tall = r3mono_entry(nm);
        mp_wt[k].base = bases[k];
    }
    mp_wt_n = nb;
    int row = 0, pg = 0;                                    /* pages: a line is one row, two with a tall glyph */
    mp_wt_start[0] = 0;
    for (int k = 0; k < nb; k++) {
        int h = mp_wt[k].tall ? mp_wt[k].tall->rows : 1;
        if (row + h > MONO_WT_ROWS) { row = 0; if (pg + 1 < 39) mp_wt_start[++pg] = k; }
        row += h;
    }
    mp_wt_pages = pg + 1;
    mp_wt_start[mp_wt_pages] = nb;
}

static void mono_widetall(R3Screen *s, const MonoState *m) {
    mono_checks();
    mono_wt_build();
    int wp = m->wt_page < 0 ? 0 : m->wt_page >= mp_wt_pages ? mp_wt_pages - 1 : m->wt_page;
    r3_text(s, 2, 2, "Base", R3_ST(R3_ST_STRONG)); r3_text(s, 7, 2, "Wide", R3_ST(R3_ST_STRONG)); r3_text(s, 13, 2, "Tall", R3_ST(R3_ST_STRONG)); r3_text(s, 20, 2, "Name", R3_ST(R3_ST_STRONG));
    int row = 3;
    for (int k = mp_wt_start[wp]; k < mp_wt_start[wp + 1]; k++) {
        const MonoWT *w = &mp_wt[k];
        int h = w->tall ? w->tall->rows : 1;
        if (w->base) mp_put(s, 2, row, w->base, R3_ST(R3_ST_BODY));
        if (w->wide) mp_put(s, 7, row, w->wide, R3_ST(R3_ST_BODY));
        if (w->tall) mp_put(s, 13, row, w->tall, R3_ST(R3_ST_BODY));
        r3_text_fit(s, 20, row, w->base->name, 42, R3_ST(R3_ST_BODY));
        row += h;
    }
    char t[96];
    int ok = mp_names.wide_ok == mp_names.wide && mp_names.tall_ok == mp_names.tall && !mp_names.wide_nobase && !mp_names.tall_nobase;
    snprintf(t, sizeof t, "%d wide, %d tall: %s the right size", mp_names.wide, mp_names.tall, ok ? "all" : "NOT ALL");
    r3_text_fit(s, 2, 18, t, 60, R3_ST(ok ? R3_ST_OK : R3_ST_ERROR));
    snprintf(t, sizeof t, "Page %d/%d   Left and right turn the page", wp + 1, mp_wt_pages);
    r3_text(s, 2, 19, t, R3_ST(R3_ST_HINT));
}

/* ---- panel 10: every named glyph --------------------------------------------------------------------------------------------------- */

/* The list a size set and a name group give, in code point order. Set 0: one cell, 1: two across, 2: anything bigger. Group 0: all, else the names that begin with a word. */
#define MONO_SETS 3
#define MONO_GROUPS_MAX 48
static const R3MonoName *mp_list[2400];
static int mp_list_n, mp_list_set = -1, mp_list_group = -1;
static const char *mp_group_word[MONO_GROUPS_MAX];
static int mp_group_len[MONO_GROUPS_MAX];
static int mp_group_n;

static int mp_set_of(const R3MonoName *e) { return e->cols == 1 && e->rows == 1 ? 0 : e->cols == 2 && e->rows == 1 ? 1 : 2; }
static int mp_word_len(const char *n) { const char *d = strchr(n, '.'); return d ? (int)(d - n) : (int)strlen(n); }

static void mono_groups_build(void) {
    if (mp_group_n) return;
    mp_group_word[0] = "all";
    mp_group_len[0] = 3;
    mp_group_n = 1;
    for (int i = 0; i < R3MONO_NAMES; i++) {                /* names are sorted, so a word's names are together */
        const R3MonoName *e = &r3mono_names[i];
        if (fc_row_name(e->name)) continue;
        int L = mp_word_len(e->name), known = 0;
        for (int g = 1; g < mp_group_n; g++) if (mp_group_len[g] == L && !strncmp(mp_group_word[g], e->name, (size_t)L)) known = 1;
        if (!known && mp_group_n < MONO_GROUPS_MAX) { mp_group_word[mp_group_n] = e->name; mp_group_len[mp_group_n++] = L; }     /* the word is the start of this name, L long */
    }
}

static int mp_in_group(const R3MonoName *e, int g) {
    if (g <= 0) return 1;
    int L = mp_group_len[g];
    return mp_word_len(e->name) == L && !strncmp(e->name, mp_group_word[g], (size_t)L);
}

static void mono_list_build(int set, int group) {
    mono_groups_build();
    if (mp_list_set == set && mp_list_group == group) return;
    mp_list_n = 0;
    for (int i = 0; i < R3MONO_NAMES; i++) {
        const R3MonoName *e = &r3mono_names[i];
        if (fc_row_name(e->name) || mp_set_of(e) != set || !mp_in_group(e, group)) continue;
        if (mp_list_n < (int)(sizeof mp_list / sizeof mp_list[0])) mp_list[mp_list_n++] = e;
    }
    qsort(mp_list, (size_t)mp_list_n, sizeof mp_list[0], mp_cmp_cp);
    mp_list_set = set; mp_list_group = group;
}

/* Where item i of a set goes: columns across the page, rows of bands, the pitch of one item, and so how many fit. */
typedef struct { int px, py, across, down; } MonoGrid;
static MonoGrid mono_grid(int set) {
    static const MonoGrid g[MONO_SETS] = {{2, 1, 30, 16}, {3, 1, 20, 16}, {5, 3, 12, 5}};
    return g[set];
}

static void mono_names(R3Screen *s, const MonoState *m) {
    mono_checks();
    int set = m->nm_set < 0 || m->nm_set >= MONO_SETS ? 0 : m->nm_set;
    mono_groups_build();
    int group = m->nm_group < 0 || m->nm_group >= mp_group_n ? 0 : m->nm_group;
    mono_list_build(set, group);
    MonoGrid g = mono_grid(set);
    int per = g.across * g.down;
    int cur = m->nm_cur >= mp_list_n ? mp_list_n - 1 : m->nm_cur;
    if (cur < 0) cur = 0;
    int page = cur / per, pages = mp_list_n ? (mp_list_n + per - 1) / per : 1;
    for (int k = 0; k < per && page * per + k < mp_list_n; k++) {
        const R3MonoName *e = mp_list[page * per + k];
        int col = 2 + (k % g.across) * g.px, row = 2 + (k / g.across) * g.py;
        if (page * per + k == cur) mp_put(s, col, row, e, R3_BLACK, R3_CYAN);
        else mp_put(s, col, row, e, R3_ST(R3_ST_BODY));
    }
    if (!mp_list_n) r3_text(s, 2, 2, "No names in this set and group.", R3_ST(R3_ST_WARN));
    char t[128];
    const R3MonoName *c = mp_list_n ? mp_list[cur] : NULL;
    if (c) snprintf(t, sizeof t, "%s  U+%X  %dx%d", c->name, c->cp, c->cols, c->rows); else t[0] = 0;
    r3_text_fit(s, 2, 18, t, 60, R3_ST(R3_ST_STRONG));
    static const char *const setn[MONO_SETS] = {"one cell", "two across", "bigger"};
    snprintf(t, sizeof t, "%s, %.*s: %d of %d  page %d/%d  A size  Y group  X page", setn[set], mp_group_len[group], mp_group_word[group], mp_list_n ? cur + 1 : 0, mp_list_n, page + 1, pages);
    r3_text_fit(s, 2, 19, t, 60, R3_ST(R3_ST_HINT));
}

/* ---- panel 11: every code point ---------------------------------------------------------------------------------------------------- */

#define MONO_CP_ACROSS 48
#define MONO_CP_DOWN 16

static void mono_codes(R3Screen *s, const MonoState *m) {
    int cur = m->cp_cur < 0 ? 0 : m->cp_cur >= R3MONO_COUNT ? R3MONO_COUNT - 1 : m->cp_cur;
    int per = MONO_CP_ACROSS * MONO_CP_DOWN, page = cur / per, pages = (R3MONO_COUNT + per - 1) / per;
    char t[128];
    for (int r = 0; r < MONO_CP_DOWN; r++) {
        int i0 = page * per + r * MONO_CP_ACROSS;
        if (i0 >= R3MONO_COUNT) break;
        snprintf(t, sizeof t, "%06X", r3mono_cp[i0]);
        r3_text(s, 2, 2 + r, t, R3_ST(R3_ST_HINT));
        for (int c = 0; c < MONO_CP_ACROSS && i0 + c < R3MONO_COUNT; c++) {
            int i = i0 + c;
            if (i == cur) r3_put_mono(s, 10 + c, 2 + r, r3mono_cp[i], R3_BLACK, R3_CYAN);
            else r3_put_mono(s, 10 + c, 2 + r, r3mono_cp[i], R3_ST(R3_ST_BODY));
        }
    }
    unsigned cp = r3mono_cp[cur];
    const char *nm = "";
    for (int i = 0; i < R3MONO_NAMES; i++) if (r3mono_names[i].cp == cp) { nm = r3mono_names[i].name; break; }
    snprintf(t, sizeof t, "U+%04X  %s", cp, nm);
    r3_text_fit(s, 2, 18, t, 60, R3_ST(R3_ST_STRONG));
    snprintf(t, sizeof t, "%d of %d  page %d/%d  Arrows move  X Y page", cur + 1, R3MONO_COUNT, page + 1, pages);
    r3_text_fit(s, 2, 19, t, 60, R3_ST(R3_ST_HINT));
}

/* ---- panel 12: the portrait maker --------------------------------------------------------------------------------------------------- */

static void mp_check_line(R3Screen *s, int row, int ok, const char *fmt, int a, int b, int c) {
    char t[96];
    snprintf(t, sizeof t, fmt, a, b, c);
    r3_text(s, 19, row, ok ? "PASS" : "FAIL", R3_ST(ok ? R3_ST_OK : R3_ST_ERROR));
    r3_text_fit(s, 24, row, t, 38, R3_ST(R3_ST_BODY));
}

static void mono_portrait(R3Screen *s, const MonoState *m) {
    mono_checks();
    FcPortraitImg img;
    fc_build(&img, m->pt_seed * 2654435761u + 12345u, m->pt_layout);
    r3_text(s, 2, 2, "8 x 4 cells", R3_ST(R3_ST_STRONG));
    fc_draw(s, 3, 3, &img);
    char t[64];
    snprintf(t, sizeof t, "layout %s", ap_layout_name[img.layout]);
    r3_text(s, 2, 8, t, R3_ST(R3_ST_BODY));
    snprintf(t, sizeof t, "seed %u", m->pt_seed);
    r3_text(s, 2, 9, t, R3_ST(R3_ST_BODY));
    r3_text(s, 2, 10, img.conflicts ? "seam broken" : "seams join", R3_ST(img.conflicts ? R3_ST_ERROR : R3_ST_OK));
    static const int pick[4][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 1}};
    static const char *const lab[4] = {"hair", "eye", "nose", "jaw"};
    for (int k = 0; k < 4; k++) {
        int i = img.part[pick[k][0]][pick[k][1]];
        const char *n = i == FC_BLANK ? "-" : strchr(ap_part[i].name + 9, '.');      /* after "portrait.<family>" */
        snprintf(t, sizeof t, "%s %s", lab[k], n ? n + 1 : "-");
        r3_text_fit(s, 2, 12 + k, t, 16, R3_ST(R3_ST_BODY));
    }
    const FcPortrait *p = &mp_por;
    r3_text(s, 19, 2, "Checks on the 10x20 bitmaps", R3_ST(R3_ST_STRONG));
    mp_check_line(s, 3, p->parts_in_font == p->parts, "%d of %d parts are in the font", p->parts_in_font, p->parts, 0);
    mp_check_line(s, 4, p->mirror_ok == p->mirror_pairs, "%d of %d right parts mirror left", p->mirror_ok, p->mirror_pairs, 0);
    mp_check_line(s, 5, p->join_bad == 0, "%d of %d listed joins hold", p->joins - p->join_bad, p->joins, 0);
    mp_check_line(s, 6, p->join_bad == 0, "%d exact %d sloped %d hair over", p->join_exact, p->join_soft, p->join_over);
    mp_check_line(s, 7, p->compat_lost == 0 && p->compat_count_bad == 0, "%d of %d kind pairs lost no join", p->compat_rows - p->compat_lost, p->compat_rows, 0);
    mp_check_line(s, 8, p->layout_cells_bad == 0, "%d layouts, every cell has a kind", p->layouts, 0, 0);
    mp_check_line(s, 9, p->builds_clean == p->builds, "%d of %d portraits join every seam", p->builds_clean, p->builds, 0);
    mp_check_line(s, 10, mp_names.cells_missing == 0 && mp_names.codes_missing == 0, "%d names, none lacks a bitmap", mp_names.names, 0, 0);
    mp_check_line(s, 11, mp_names.wide_ok == mp_names.wide && mp_names.tall_ok == mp_names.tall, "%d wide, %d tall: the right size", mp_names.wide, mp_names.tall, 0);
    if (p->first_bad[0]) r3_text_fit(s, 19, 13, p->first_bad, 43, R3_ST(R3_ST_ERROR));
    else if (mp_names.first_bad[0]) r3_text_fit(s, 19, 13, mp_names.first_bad, 43, R3_ST(R3_ST_ERROR));
    r3_text(s, 19, 15, "A  a new portrait", R3_ST(R3_ST_HINT));
    r3_text(s, 19, 16, "Y  the other layout (mouth high or low)", R3_ST(R3_ST_HINT));
    r3_text(s, 19, 17, "X  the portrait before", R3_ST(R3_ST_HINT));
}

/* ---- the page ------------------------------------------------------------------------------------------------------------------------ */

static void mono_draw(R3Screen *s, const MonoState *m, const MonoLive *l, unsigned now) {
    mono_frame(s, m->panel);
    switch (m->panel) {
    case MONO_TEXT: mono_text(s); break;
    case MONO_PAD: mono_pad(s, l); break;
    case MONO_KEYS: mono_keys(s, m); break;
    case MONO_PIANO: mono_piano(s, m); break;
    case MONO_UI: mono_ui(s, now); break;
    case MONO_MUSIC: mono_music(s, now); break;
    case MONO_ICONS_A: mono_icons_a(s); break;
    case MONO_ICONS_B: mono_icons_b(s); break;
    case MONO_WIDETALL: mono_widetall(s, m); break;
    case MONO_NAMES: mono_names(s, m); break;
    case MONO_CODES: mono_codes(s, m); break;
    default: mono_portrait(s, m);
    }
}

/* Does a panel change on its own (a bar, a spinner, the live pad), so the page must redraw every frame? */
static int mono_live(const MonoState *m) { return m->panel == MONO_PAD || m->panel == MONO_UI || m->panel == MONO_MUSIC; }

/* One action from the reader. Returns 1 when the page used it (B and the menus are the app's). */
static int mono_act(MonoState *m, int action) {
    switch (action) {
    case R3_PANEL_NEXT: case R3_PAGEDOWN: m->panel = (m->panel + 1) % MONO_PANELS; return 1;
    case R3_PANEL_PREV: case R3_PAGEUP: m->panel = (m->panel + MONO_PANELS - 1) % MONO_PANELS; return 1;
    case R3_PANEL_FIRST: m->panel = 0; return 1;
    default: break;
    }
    if (m->panel == MONO_KEYS) {
        int rowlen = m->ky == 4 ? 6 : MONO_KEYS_W;
        switch (action) {
        case R3_LEFT: m->kx = (m->kx + rowlen - 1) % rowlen; return 1;
        case R3_RIGHT: m->kx = (m->kx + 1) % rowlen; return 1;
        case R3_UP: m->ky = (m->ky + 4) % 5; if (m->ky == 4 && m->kx > 5) m->kx = 5; return 1;
        case R3_DOWN: m->ky = (m->ky + 1) % 5; if (m->ky == 4 && m->kx > 5) m->kx = 5; return 1;
        case R3_Y: m->layer = (m->layer + 1) % 3; return 1;
        case R3_X: { size_t n = strlen(m->typed); if (n) m->typed[n - 1] = 0; return 1; }
        case R3_ACCEPT: {
            size_t n = strlen(m->typed);
            if (m->ky < 4) { if (n + 1 < sizeof m->typed) { m->typed[n] = MONO_KB[m->layer][m->ky][m->kx]; m->typed[n + 1] = 0; } }
            else if (m->kx == 0) m->layer = (m->layer + 1) % 3;
            else if (m->kx == 1) { if (n + 1 < sizeof m->typed) { m->typed[n] = ' '; m->typed[n + 1] = 0; } }
            else if (m->kx == 2) { if (n) m->typed[n - 1] = 0; }
            else if (m->kx == 5) m->typed[0] = 0;
            return 1;
        }
        default: return 0;
        }
    }
    if (m->panel == MONO_WIDETALL) {
        mono_wt_build();
        switch (action) {
        case R3_LEFT: case R3_X: m->wt_page = (m->wt_page + mp_wt_pages - 1) % mp_wt_pages; return 1;
        case R3_RIGHT: case R3_Y: case R3_ACCEPT: m->wt_page = (m->wt_page + 1) % mp_wt_pages; return 1;
        default: return 0;
        }
    }
    if (m->panel == MONO_NAMES) {
        mono_groups_build();
        mono_list_build(m->nm_set, m->nm_group);
        MonoGrid g = mono_grid(m->nm_set);
        int per = g.across * g.down, n = mp_list_n;
        switch (action) {
        case R3_LEFT: if (m->nm_cur > 0) m->nm_cur--; return 1;
        case R3_RIGHT: if (m->nm_cur + 1 < n) m->nm_cur++; return 1;
        case R3_UP: m->nm_cur = m->nm_cur >= g.across ? m->nm_cur - g.across : m->nm_cur; return 1;
        case R3_DOWN: m->nm_cur = m->nm_cur + g.across < n ? m->nm_cur + g.across : m->nm_cur; return 1;
        case R3_X: m->nm_cur = m->nm_cur >= per ? m->nm_cur - per : 0; return 1;
        case R3_ACCEPT: m->nm_set = (m->nm_set + 1) % MONO_SETS; m->nm_cur = 0; return 1;
        case R3_Y: m->nm_group = (m->nm_group + 1) % mp_group_n; m->nm_cur = 0; return 1;
        default: return 0;
        }
    }
    if (m->panel == MONO_CODES) {
        int per = MONO_CP_ACROSS * MONO_CP_DOWN;
        switch (action) {
        case R3_LEFT: if (m->cp_cur > 0) m->cp_cur--; return 1;
        case R3_RIGHT: if (m->cp_cur + 1 < R3MONO_COUNT) m->cp_cur++; return 1;
        case R3_UP: if (m->cp_cur >= MONO_CP_ACROSS) m->cp_cur -= MONO_CP_ACROSS; return 1;
        case R3_DOWN: if (m->cp_cur + MONO_CP_ACROSS < R3MONO_COUNT) m->cp_cur += MONO_CP_ACROSS; return 1;
        case R3_X: m->cp_cur = m->cp_cur >= per ? m->cp_cur - per : 0; return 1;
        case R3_Y: case R3_ACCEPT: m->cp_cur = m->cp_cur + per < R3MONO_COUNT ? m->cp_cur + per : R3MONO_COUNT - 1; return 1;
        default: return 0;
        }
    }
    if (m->panel == MONO_PORTRAIT) {
        switch (action) {
        case R3_ACCEPT: m->pt_seed++; return 1;
        case R3_X: if (m->pt_seed) m->pt_seed--; return 1;
        case R3_Y: m->pt_layout = (m->pt_layout + 1) % AP_LAYOUTS; return 1;
        default: return 0;
        }
    }
    if (m->panel == MONO_PIANO) {
        switch (action) {
        case R3_LEFT: m->pcur = (m->pcur + MONO_PIANO_SEMIS - 1) % MONO_PIANO_SEMIS; return 1;
        case R3_RIGHT: m->pcur = (m->pcur + 1) % MONO_PIANO_SEMIS; return 1;
        case R3_UP: m->pcur = m->pcur + 12 < MONO_PIANO_SEMIS ? m->pcur + 12 : m->pcur; return 1;
        case R3_DOWN: m->pcur = m->pcur >= 12 ? m->pcur - 12 : m->pcur; return 1;
        case R3_ACCEPT: m->notes ^= 1ull << m->pcur; return 1;
        case R3_X: m->notes = 0; return 1;
        case R3_Y: { static const int major[3] = {0, 4, 7}; for (int i = 0; i < 3; i++) if (m->pcur + major[i] < MONO_PIANO_SEMIS) m->notes |= 1ull << (m->pcur + major[i]); return 1; }
        default: return 0;
        }
    }
    return 0;
}

#endif

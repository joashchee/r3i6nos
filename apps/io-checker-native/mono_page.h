/* The IO Checker's R3i6nos Mono page: a screen test of the 10x20 font (docs/R3I6NOS-MONO-FONT.md), eight panels, each drawing its glyphs the way an app
 * should use them. The rules the panels show:
 *   - a glyph is printed by name (r3mono_name("pad.btn.A.on"), or the R3I_ macro when the name is known at build time) and never by code point;
 *   - a two-cell icon (app, system and file icons, triggers, the D-pad, sticks, key caps, switches, waveforms, big knobs) is ONE string of two cells, printed
 *     together on one row: the halves are never separated, never coloured differently, never given a different background;
 *   - controls have explicit glyphs for every state (off, on, disabled, a level 0-8), so a state never rests on colour alone;
 *   - an ordinary character (a letter on a key, a label) is a plain cell beside the glyphs, in the same colours.
 * Needs R3_WITH_MONO before r3i6nos_ansiapps.h, and the L_ button enum (io_checker.c). Untested on a unit. */
#ifndef IOC_MONO_PAGE_H
#define IOC_MONO_PAGE_H

#include <stdio.h>
#include <string.h>

enum { MONO_TEXT, MONO_PAD, MONO_KEYS, MONO_PIANO, MONO_UI, MONO_MUSIC, MONO_ICONS_A, MONO_ICONS_B, MONO_PANELS };
static const char *const MONO_PANEL_NAME[MONO_PANELS] = {"Text mode", "Gamepad", "Keyboard", "Piano", "UI widgets", "Music", "Icons: apps, files", "Icons: system"};

typedef struct {
    int panel;
    int kx, ky, layer;              /* the keyboard panel: cursor and layer */
    char typed[40];
    int pcur;                       /* the piano panel: the cursor, a semitone from the first C (0-36) */
    unsigned long long notes;       /* the piano's notes that are on (bit = semitone) */
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
    r3_box(s, 0, R3_DISPLAY_TOP, R3_COLS, R3_DISPLAY_ROWS, &(R3Box){1, "R3i6nos Mono", right, "Fn next panel  B back", 0});
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
    mt(s, 36, 8, "\U0001FB00\U0001FB01\U0001FB02\U0001FB03\U0001FB04\U0001FB05\U0001FB06 ⠁⠃⠇⡇⣇⣧⣷⣿", R3_ST_BODY);
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
    mt(s, 36, 14, "↖↗↘↙ ⇦⇨⇧⇩ ↺↻ ➔➤ ⏴⏵⏶⏷", R3_ST_BODY);
    mt(s, 36, 15, "✓✔✗✘ ★☆ ♡ ⚠⚡ ☐☑☒ ●◉◐◑◒◓ ◖◗", R3_ST_BODY);
    mt(s, 36, 16, "♩♪♫♬ ♭♮♯ \U0001D11E \U0001D122 \U0001D10B \U0001D10C", R3_ST_BODY);
    mt(s, 36, 17, "⠿ ⋮ ⋯ ☰ ⚙ ⌕ ⌘ ⌫ ⏎ ␣ ①②③ ❶❷❸", R3_ST_BODY);
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
    MP(s, 17, 9, "ui.radio.off"); MP(s, 18, 9, "ui.radio.on"); MP(s, 20, 9, "ui.check.mixed"); mt(s, 22, 9, "☐☑☒", R3_ST_BODY);
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
    mt(s, 20, 10, "♩ ♪ ♫ ♬ ♭ ♮ ♯   \U0001D11E \U0001D122 \U0001D12A \U0001D12B \U0001D110 \U0001D10B", R3_ST_BODY);
    mt(s, 20, 11, "⏮ ⏪ ⏵ ⏸ ⏹ ⏺ ⏩ ⏭ ⏯", R3_ST_BODY);
    mt(s, 20, 12, "Cmaj7  F♯m7♭5  G7♭9  B♭Δ  Dø  A°  Esus4", R3_ST_BODY);
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
    default: mono_icons_b(s);
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

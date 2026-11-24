/* r3i6nos on-screen keyboard for native apps (C): the virtual keyboard of docs/r3i6nos-mapping.md ("The virtual keyboard"). MIT, like the rest of theme/.
 *
 * The same keyboard as the menu's Wi-Fi password entry (menu/osk.lua), written once for C: a 10 by 4 grid of keys in three layers (abc, ABC, #+=), the
 * text shown (or hidden), a length limit. Printable ASCII only. The pad drives it through the actions the reader (r3i6nos_input.h) makes:
 *   D-pad and right stick  move over the keys (wrapping); R1+Left / R1+Right (Home / End) the first and last key of the row, R1+Up / R1+Down the first and last row
 *   A                      types the key under the cursor
 *   X                      deletes the last character
 *   Y                      changes the layer
 *   Select                 shows or hides the text (for a password)
 *   Start                  done (when the text is valid: length min..max)
 *   B or R3                closes without keeping the text (R3 opened it, R3 closes it)
 * A USB keyboard types straight in: r3_osk_type for SDL_TEXTINPUT text, r3_osk_sdlkey for Backspace, Return and Escape (and to swallow the printable
 * keys, which would otherwise reach the app as pad buttons).
 * An app owns an R3Osk, calls r3_osk_new when it opens, feeds every action to r3_osk_press while it shows, and draws it with r3_osk_draw over its own screen.
 * Needs r3i6nos_ansiapps.h first (and SDL for r3_osk_sdlkey). Untested on a unit. */
#ifndef R3I6NOS_OSK_H
#define R3I6NOS_OSK_H

#include <stdio.h>
#include <string.h>

#define R3_OSK_COLS 10
#define R3_OSK_ROWS 4
#define R3_OSK_LAYERS 3
#define R3_OSK_MAXTEXT 127

enum { R3_OSK_OPEN, R3_OSK_DONE, R3_OSK_CANCEL };     /* what r3_osk_press and r3_osk_sdlkey return */

typedef struct {
    char title[48], prompt[64];
    int min, max;                       /* the text's length limits; max is at most R3_OSK_MAXTEXT */
    char text[R3_OSK_MAXTEXT + 1];
    int layer, row, col;
    int hidden;                         /* the text shows as asterisks */
    char error[64];                     /* why the last Start or A was refused; cleared by the next action */
} R3Osk;

static const char *const R3_OSK_KEYS[R3_OSK_LAYERS][R3_OSK_ROWS] = {
    {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm., "},
    {"1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM., "},
    {"!@#$%^&*()", "-_=+[]{};:", "'\",.<>/?\\|", "`~ .,;:!?#"},
};
static const char *const R3_OSK_LAYER_NAMES[R3_OSK_LAYERS] = {"abc", "ABC", "#+="};

/* A new entry: max 0 means 63; initial may be NULL. */
static inline void r3_osk_new(R3Osk *o, const char *title, const char *prompt, int min, int max, const char *initial) {
    memset(o, 0, sizeof *o);
    snprintf(o->title, sizeof o->title, "%s", title ? title : "Type");
    snprintf(o->prompt, sizeof o->prompt, "%s", prompt ? prompt : "");
    o->min = min < 0 ? 0 : min;
    o->max = max <= 0 ? 63 : max > R3_OSK_MAXTEXT ? R3_OSK_MAXTEXT : max;
    if (initial) snprintf(o->text, sizeof o->text, "%.*s", o->max, initial);
}

/* The character under the cursor. */
static inline char r3_osk_key(const R3Osk *o) { return R3_OSK_KEYS[o->layer][o->row][o->col]; }

/* Is the text acceptable? 1, or 0 with the reason in why. */
static inline int r3_osk_valid(const R3Osk *o, char *why, size_t n) {
    size_t len = strlen(o->text);
    if ((int)len < o->min) { if (why) snprintf(why, n, "At least %d characters (now %d)", o->min, (int)len); return 0; }
    if ((int)len > o->max) { if (why) snprintf(why, n, "At most %d characters", o->max); return 0; }
    return 1;
}

/* Append typed text (a real keyboard). Anything that is not printable ASCII is ignored. Returns how many characters were kept. */
static inline int r3_osk_type(R3Osk *o, const char *s) {
    int kept = 0;
    size_t len = strlen(o->text);
    for (; s && *s; s++)
        if ((unsigned char)*s >= 32 && (unsigned char)*s < 127 && (int)len < o->max) { o->text[len++] = *s; o->text[len] = 0; kept++; }
    return kept;
}

/* Feed one action (the R3_* of the reader). Returns R3_OSK_DONE (the text is valid), R3_OSK_CANCEL, or R3_OSK_OPEN; a refused Start stays open with o->error set. */
static inline int r3_osk_press(R3Osk *o, int action) {
    o->error[0] = 0;
    switch (action) {
    case R3_LEFT: o->col = (o->col + R3_OSK_COLS - 1) % R3_OSK_COLS; break;
    case R3_RIGHT: o->col = (o->col + 1) % R3_OSK_COLS; break;
    case R3_UP: o->row = (o->row + R3_OSK_ROWS - 1) % R3_OSK_ROWS; break;
    case R3_DOWN: o->row = (o->row + 1) % R3_OSK_ROWS; break;
    case R3_HOME: o->col = 0; break;
    case R3_END: o->col = R3_OSK_COLS - 1; break;
    case R3_PAGEUP: o->row = 0; break;
    case R3_PAGEDOWN: o->row = R3_OSK_ROWS - 1; break;
    case R3_ACCEPT: {
        char k[2] = {r3_osk_key(o), 0};
        if (!r3_osk_type(o, k)) snprintf(o->error, sizeof o->error, "At most %d characters", o->max);
        break;
    }
    case R3_X: { size_t n = strlen(o->text); if (n) o->text[n - 1] = 0; break; }
    case R3_Y: o->layer = (o->layer + 1) % R3_OSK_LAYERS; break;
    case R3_SELECT: o->hidden = !o->hidden; break;
    case R3_START: case R3_MORE:
        if (r3_osk_valid(o, o->error, sizeof o->error)) return R3_OSK_DONE;
        break;
    case R3_BACK: case R3_R3: return R3_OSK_CANCEL;
    default: break;
    }
    return R3_OSK_OPEN;
}

/* A key on a real keyboard while the entry shows: 1 when it was used (Backspace, Return, Escape, and every printable key, which SDL_TEXTINPUT types).
 * *result gets R3_OSK_DONE or R3_OSK_CANCEL when Return or Escape ended it, else R3_OSK_OPEN. */
static inline int r3_osk_sdlkey(R3Osk *o, SDL_Keycode k, int *result) {
    *result = R3_OSK_OPEN;
    if (k == SDLK_BACKSPACE) { r3_osk_press(o, R3_X); return 1; }
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) { *result = r3_osk_press(o, R3_START); return 1; }
    if (k == SDLK_ESCAPE) { *result = R3_OSK_CANCEL; return 1; }
    return k >= 32 && k < 127;
}

#ifdef R3_WITH_MONO
/* The two-cell key cap of R3i6nos Mono for a key (raised, or pressed for the cursor), or NULL. */
static inline const char *r3_osk_cap(char ch, int pressed) {
    char name[32];
    if (ch == ' ') snprintf(name, sizeof name, "key.space.%s", pressed ? "dn" : "up");
    else snprintf(name, sizeof name, "key.u%04x.%s", (unsigned)(unsigned char)ch, pressed ? "dn" : "up");
    return r3mono_name(name);
}
#endif

/* Draw the entry as a box over the display view: the prompt, the text with its caret, the error, the keys with the cursor, the layer and the keys' help.
 * The cursor key uses the theme's select pair, so every pair passes 4.5:1 in every theme. Rows 4 to 16. */
static inline void r3_osk_draw(R3Screen *s, const R3Osk *o) {
    const int bx = 9, by = 4, bw = 46, bh = 13;
    R3Box b = {1, o->title, NULL, "Start done  B cancel", 0};
    r3_box(s, bx, by, bw, bh, &b);
    if (o->prompt[0]) r3_text_fit(s, bx + 2, by + 1, o->prompt, bw - 4, R3_ST(R3_ST_BODY));
    char shown[R3_OSK_MAXTEXT + 1];
    size_t n = strlen(o->text);
    for (size_t i = 0; i < n; i++) shown[i] = o->hidden ? '*' : o->text[i];
    shown[n] = 0;
    r3_fill(s, bx + 2, by + 3, bw - 4, 1, 32, R3_ST(R3_ST_STRONG));
    int x = r3_text_fit(s, bx + 2, by + 3, shown, bw - 6, R3_ST(R3_ST_STRONG));     /* a long text shows its head; the caret is at its end */
    if (n && (int)n > bw - 6) { unsigned char g[R3_COLS]; int m = r3_fit_tail(shown, bw - 6, g); x = r3_put_cells(s, bx + 2, by + 3, g, m, R3_ST(R3_ST_STRONG)); }
    r3_put(s, x, by + 3, 219, R3_ST(R3_ST_STRONG));
    char count[24];
    snprintf(count, sizeof count, "%d/%d", (int)n, o->max);
    r3_text_right(s, bx + bw - 3, by + 4, count, R3_ST(R3_ST_HINT));
    if (o->error[0]) r3_text_fit(s, bx + 2, by + 4, o->error, bw - 14, R3_ST(R3_ST_ERROR));
    for (int r = 0; r < R3_OSK_ROWS; r++)
        for (int c = 0; c < R3_OSK_COLS; c++) {
            char ch = R3_OSK_KEYS[o->layer][r][c];
            int sel = r == o->row && c == o->col, col = bx + 3 + c * 4;
#ifdef R3_WITH_MONO     /* an app that opted into R3i6nos Mono draws every key as a cap, the cursor's pressed */
            const char *cap = r3_osk_cap(ch, sel);
            if (cap) {
                if (sel) r3_puts_mono(s, col, by + 6 + r, cap, R3_ST(R3_ST_SELECT));
                else r3_puts_mono(s, col, by + 6 + r, cap, R3_ST(R3_ST_BODY));
                continue;
            }
#endif
            if (sel) { r3_put(s, col, by + 6 + r, 32, R3_ST(R3_ST_SELECT)); r3_put(s, col + 1, by + 6 + r, ch == ' ' ? 250 : (unsigned char)ch, R3_ST(R3_ST_SELECT)); r3_put(s, col + 2, by + 6 + r, 32, R3_ST(R3_ST_SELECT)); }
            else r3_put(s, col + 1, by + 6 + r, ch == ' ' ? 250 : (unsigned char)ch, R3_ST(R3_ST_BODY));
        }
    char line[64];
    snprintf(line, sizeof line, "Layer %s", R3_OSK_LAYER_NAMES[o->layer]);
    r3_text_fit(s, bx + 2, by + 11, line, 14, R3_ST(R3_ST_OK));
    r3_text_fit(s, bx + 16, by + 11, "A type  X delete  Y layer  Select show", bw - 18, R3_ST(R3_ST_HINT));
}

#endif /* R3I6NOS_OSK_H */

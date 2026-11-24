/* theme/r3i6nos_keymap.h with R3i6nos Mono (R3_WITH_MONO): the Keymap shows buttons as the pad's own glyphs. Run by scripts/check.sh. */
#include <SDL.h>
#define R3_WITH_MONO
#include "r3i6nos_ansiapps.h"
#include "r3i6nos_themes.h"
#include "r3i6nos_input.h"
#include "r3i6nos_keymap.h"
#include "r3i6nos_osk.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; fprintf(stderr, "FAIL line %d: ", __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

int main(void) {
    /* every button the Keymap names has a glyph, in both states; two cells but for A B X Y; the D-pad shows its direction */
    for (int b = 0; b < R3_BTN_COUNT; b++) {
        const char *n = r3_button_name(b), *off = r3_km_glyph(n, 0), *on = r3_km_glyph(n, 1);
        CHECK(off && on, "button %s has a glyph", n);
        if (!off || !on) continue;
        int cells = 0;
        for (const unsigned char *p = (const unsigned char *)on; *p; p++) if ((*p & 0xC0) != 0x80) cells++;
        int one = strlen(n) == 1;
        CHECK(cells == (one ? 1 : 2), "%s is %d cell(s)", n, cells);
        if (strcmp(n, "Up") && strcmp(n, "Down") && strcmp(n, "Left") && strcmp(n, "Right")) CHECK(strcmp(off, on) != 0, "%s: pressed differs from released", n);
    }
    CHECK(r3_km_glyph("D-pad", 0) == NULL && r3_km_glyph("Quit", 0) == NULL && r3_km_glyph("", 0) == NULL && r3_km_glyph(NULL, 0) == NULL, "other words are not buttons");

    /* a chord is glyphs with a plus between; anything else is text */
    static R3Screen s;
    r3_clear(&s);
    CHECK(r3_km_chord_glyphs(&s, 2, 3, "L1+R1", 0, R3_LIGHT_GRAY, R3_BLACK) == 5, "L1+R1 is two two-cell glyphs and a plus");
    CHECK(s.ux[3 * R3_COLS + 2] != 0 && s.ch[3 * R3_COLS + 4] == '+' && s.ux[3 * R3_COLS + 5] != 0, "the layout is glyph, plus, glyph");
    CHECK(r3_km_chord_glyphs(&s, 2, 4, "L1+Fn", 0, 7, 0) == 5, "L1+Fn");
    CHECK(r3_km_chord_glyphs(&s, 2, 5, "R1 held", 0, 7, 0) == 0 && r3_km_chord_glyphs(&s, 2, 5, "L1+Dance", 0, 7, 0) == 0, "not a chord of buttons: the caller draws text");
    CHECK(r3_km_chord_glyphs(&s, 2, 6, "A", 1, 7, 0) == 1, "a single face button");

    /* the help page draws the system's chords as glyphs, the test page the held buttons */
    R3Keymap k;
    r3_km_open(&k, NULL, 0, 0);
    r3_clear(&s);
    r3_km_draw(&s, &k);
    int glyph_cells = 0;
    for (int i = 0; i < R3_COLS * R3_ROWS; i++) if (s.ux[i]) glyph_cells++;
    CHECK(glyph_cells >= 4 * 4, "the system page shows the L1+R1, L2+R2, L1+Fn and R1+Fn chords as glyphs (%d cells)", glyph_cells);
    k.page = k.npages - 1;
    r3_km_button(&k, R3_BTN_A, 1, "x");
    r3_km_button(&k, R3_BTN_L1, 1, "x");
    r3_clear(&s);
    r3_km_draw(&s, &k);
    glyph_cells = 0;
    for (int i = 0; i < R3_COLS * R3_ROWS; i++) if (s.ux[i]) glyph_cells++;
    CHECK(glyph_cells == 3, "the test page shows A (one cell) and L1 (two) pressed (%d cells)", glyph_cells);

    /* the on-screen keyboard draws every key of every layer as a two-cell cap, the cursor's pressed */
    for (int l = 0; l < R3_OSK_LAYERS; l++)
        for (int r = 0; r < R3_OSK_ROWS; r++)
            for (int c = 0; c < R3_OSK_COLS; c++) {
                char ch = R3_OSK_KEYS[l][r][c];
                const char *up = r3_osk_cap(ch, 0), *dn = r3_osk_cap(ch, 1);
                CHECK(up && dn && strcmp(up, dn) != 0, "key '%c' (layer %d) has a raised and a pressed cap", ch, l);
            }
    R3Osk o;
    r3_osk_new(&o, "Name", "", 0, 20, NULL);
    r3_clear(&s);
    r3_osk_draw(&s, &o);
    int caps = 0;
    for (int i = 0; i < R3_COLS * R3_ROWS; i++) if (s.ux[i]) caps++;
    CHECK(caps == 2 * R3_OSK_ROWS * R3_OSK_COLS, "the keyboard is %d cells of caps (%d)", 2 * R3_OSK_ROWS * R3_OSK_COLS, caps);

    if (fails) return 1;
    puts("test_keymap_glyphs: ok");
    return 0;
}

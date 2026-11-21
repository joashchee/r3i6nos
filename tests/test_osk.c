/* theme/r3i6nos_osk.h: the on-screen keyboard (docs/r3i6nos-mapping.md, "The virtual keyboard"). Run by scripts/check.sh. */
#include <SDL.h>
#include "r3i6nos_ansiapps.h"
#include "r3i6nos_themes.h"
#include "r3i6nos_osk.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; fprintf(stderr, "FAIL line %d: ", __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

int main(void) {
    R3Osk o;
    r3_osk_new(&o, "Name", "Name the marker", 1, 8, NULL);
    CHECK(!strcmp(o.title, "Name") && o.max == 8 && o.text[0] == 0 && o.layer == 0 && o.row == 0 && o.col == 0, "a new entry is empty on the first key");
    CHECK(r3_osk_key(&o) == '1', "the cursor starts on 1");
    /* moving wraps in all four directions */
    r3_osk_press(&o, R3_LEFT); CHECK(o.col == 9, "Left wraps to the last column");
    r3_osk_press(&o, R3_RIGHT); CHECK(o.col == 0, "Right wraps to the first");
    r3_osk_press(&o, R3_UP); CHECK(o.row == 3, "Up wraps to the last row");
    r3_osk_press(&o, R3_DOWN); CHECK(o.row == 0, "Down wraps to the first");
    r3_osk_press(&o, R3_END); r3_osk_press(&o, R3_PAGEDOWN); CHECK(o.col == 9 && o.row == 3 && r3_osk_key(&o) == ' ', "End and Page down reach the space key");
    r3_osk_press(&o, R3_HOME); r3_osk_press(&o, R3_PAGEUP); CHECK(o.col == 0 && o.row == 0, "Home and Page up reach the first key");
    /* typing, layers, delete */
    r3_osk_press(&o, R3_DOWN);                                   /* q */
    CHECK(r3_osk_press(&o, R3_ACCEPT) == R3_OSK_OPEN && !strcmp(o.text, "q"), "A types the key");
    r3_osk_press(&o, R3_Y);
    CHECK(o.layer == 1, "Y changes to ABC");
    r3_osk_press(&o, R3_ACCEPT); CHECK(!strcmp(o.text, "qQ"), "the capital layer types capitals");
    r3_osk_press(&o, R3_Y); r3_osk_press(&o, R3_Y); CHECK(o.layer == 0, "three layers, then round again");
    r3_osk_press(&o, R3_X); CHECK(!strcmp(o.text, "q"), "X deletes the last character");
    r3_osk_press(&o, R3_X); r3_osk_press(&o, R3_X); CHECK(o.text[0] == 0, "X on an empty text does nothing");
    /* the symbol layer has a space on the third key of the last row */
    r3_osk_press(&o, R3_Y); r3_osk_press(&o, R3_Y); o.row = 3; o.col = 2;
    CHECK(r3_osk_key(&o) == ' ', "the symbols layer has a space");
    /* limits */
    r3_osk_new(&o, "Name", "", 2, 4, NULL);
    CHECK(r3_osk_press(&o, R3_START) == R3_OSK_OPEN && strstr(o.error, "At least 2"), "Start on too short a text stays open and says why (%s)", o.error);
    r3_osk_press(&o, R3_ACCEPT); r3_osk_press(&o, R3_ACCEPT);
    CHECK(r3_osk_press(&o, R3_START) == R3_OSK_DONE && !strcmp(o.text, "11"), "Start with a valid text is done");
    r3_osk_press(&o, R3_ACCEPT); r3_osk_press(&o, R3_ACCEPT); r3_osk_press(&o, R3_ACCEPT);
    CHECK(!strcmp(o.text, "1111") && !strcmp(o.error, "At most 4 characters"), "A past the limit says so and keeps the text (%s)", o.error);
    r3_osk_press(&o, R3_LEFT); CHECK(o.error[0] == 0, "the next action clears the error");
    /* cancel: B and R3 */
    CHECK(r3_osk_press(&o, R3_BACK) == R3_OSK_CANCEL && r3_osk_press(&o, R3_R3) == R3_OSK_CANCEL, "B and R3 close without keeping");
    /* hidden text */
    r3_osk_press(&o, R3_SELECT); CHECK(o.hidden, "Select hides the text");
    r3_osk_press(&o, R3_SELECT); CHECK(!o.hidden, "and shows it again");
    /* a real keyboard */
    r3_osk_new(&o, "Name", "", 0, 6, "Chorus");
    CHECK(!strcmp(o.text, "Chorus") && r3_osk_type(&o, "x") == 0, "an initial text is kept and the limit holds for typed text");
    r3_osk_new(&o, "Name", "", 0, 10, NULL);
    CHECK(r3_osk_type(&o, "Verse \xc3\xa9 2\n") == 8 && !strcmp(o.text, "Verse  2"), "typed text keeps printable ASCII only (%s)", o.text);
    int res;
    CHECK(r3_osk_sdlkey(&o, SDLK_BACKSPACE, &res) && !strcmp(o.text, "Verse  "), "Backspace deletes");
    CHECK(r3_osk_sdlkey(&o, SDLK_RETURN, &res) && res == R3_OSK_DONE, "Return is done");
    CHECK(r3_osk_sdlkey(&o, SDLK_ESCAPE, &res) && res == R3_OSK_CANCEL, "Escape cancels");
    CHECK(r3_osk_sdlkey(&o, SDLK_a, &res) && res == R3_OSK_OPEN, "a printable key is swallowed (SDL_TEXTINPUT types it)");
    CHECK(!r3_osk_sdlkey(&o, SDLK_LEFT, &res), "an arrow is left to the pad path");
    /* drawing: in every theme every drawn cell passes 4.5:1, with a short, an empty, a full and a hidden text, and an error */
    static R3Screen s;
    for (int t = 0; t < R3_THEME_COUNT; t++) {
        r3_theme_apply(t);
        for (int v = 0; v < 4; v++) {
            r3_osk_new(&o, "Name the marker", "A name for this marker.", 3, 20, v == 1 ? "Chorus 1" : v == 2 ? "abcdefghijklmnopqrst" : NULL);
            if (v == 0) r3_osk_press(&o, R3_START);                 /* an error line */
            if (v == 3) { r3_osk_type(&o, "secret"); r3_osk_press(&o, R3_SELECT); o.layer = 2; o.row = 3; o.col = 2; }
            r3_clear(&s);
            r3_osk_draw(&s, &o);
            double worst = 99;
            for (int i = 0; i < R3_COLS * R3_ROWS; i++) {
                if (s.ch[i] == 32 || s.ch[i] == 0) continue;
                double c = r3_contrast_pal(s.fg[i], s.bg[i]);
                if (c < worst) worst = c;
            }
            CHECK(worst >= 4.5, "%s: the keyboard (variant %d) has a cell at %.2f:1", r3_themes[t].name, v, worst);
        }
    }
    r3_theme_apply(0);
    r3_osk_new(&o, "Name", "", 0, 20, "Chorus");
    r3_clear(&s);
    r3_osk_draw(&s, &o);
    char dump[R3_ROWS][R3_COLS + 1];
    r3_dump(&s, dump);
    CHECK(strstr(dump[7], "Chorus") && strstr(dump[10], "1") && strstr(dump[11], "q"), "the text and the keys are on the screen:\n%s\n%s", dump[7], dump[11]);
    if (fails) { fprintf(stderr, "test_osk: %d failed\n", fails); return 1; }
    puts("test_osk: ok");
    return 0;
}

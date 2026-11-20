/* theme/r3i6nos_input.h and theme/r3i6nos_mainmenu.h: the input mapping's reader (docs/r3i6nos-mapping.md) and the main menu (rule 17).
 * Run by scripts/check.sh. */
#include <SDL.h>
#include "r3i6nos_ansiapps.h"
#include "r3i6nos_themes.h"
#include "r3i6nos_input.h"
#include "r3i6nos_mainmenu.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; fprintf(stderr, "FAIL line %d: ", __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static int press(R3Input *in, int b) { int a = r3_input_button(in, b, 1); r3_input_button(in, b, 0); return a; }

int main(void) {
    R3Input in;
    r3_input_init(&in);
    /* the plain inputs */
    CHECK(press(&in, R3_BTN_A) == R3_ACCEPT && press(&in, R3_BTN_B) == R3_BACK, "A accepts and B backs");
    CHECK(press(&in, R3_BTN_X) == R3_X && press(&in, R3_BTN_Y) == R3_Y, "X and Y");
    CHECK(press(&in, R3_BTN_SELECT) == R3_SELECT && press(&in, R3_BTN_START) == R3_START, "Select and Start");
    CHECK(press(&in, R3_BTN_L3) == R3_L3 && press(&in, R3_BTN_R3) == R3_R3, "L3 and R3");
    CHECK(press(&in, R3_BTN_UP) == R3_UP && press(&in, R3_BTN_DOWN) == R3_DOWN && press(&in, R3_BTN_LEFT) == R3_LEFT && press(&in, R3_BTN_RIGHT) == R3_RIGHT, "the D-pad");
    CHECK(press(&in, R3_BTN_FN) == R3_PANEL_NEXT, "Fn alone is the next panel");
    for (int b = R3_BTN_L1; b <= R3_BTN_R2; b++) CHECK(press(&in, b) == R3_NONE, "a modifier alone does nothing (%s)", r3_button_name(b));
    /* R1 held is increase */
    r3_input_button(&in, R3_BTN_R1, 1);
    CHECK(press(&in, R3_BTN_UP) == R3_PAGEUP && press(&in, R3_BTN_DOWN) == R3_PAGEDOWN, "R1 + up and down page");
    CHECK(press(&in, R3_BTN_LEFT) == R3_HOME && press(&in, R3_BTN_RIGHT) == R3_END, "R1 + left and right are Home and End");
    CHECK(press(&in, R3_BTN_FN) == R3_PANEL_PREV, "R1 + Fn is the previous panel");
    CHECK(press(&in, R3_BTN_A) == R3_ACCEPT, "R1 does not change A");
    r3_input_button(&in, R3_BTN_R1, 0);
    CHECK(press(&in, R3_BTN_UP) == R3_UP, "R1 released: up is up again");
    /* the chords fire when the second input goes down, in either order */
    r3_input_button(&in, R3_BTN_L1, 1);
    CHECK(r3_input_button(&in, R3_BTN_R1, 1) == R3_MENU, "L1 then R1 opens the main menu");
    r3_input_button(&in, R3_BTN_R1, 0); r3_input_button(&in, R3_BTN_L1, 0);
    r3_input_button(&in, R3_BTN_R1, 1);
    CHECK(r3_input_button(&in, R3_BTN_L1, 1) == R3_MENU, "R1 then L1 opens it too");
    r3_input_button(&in, R3_BTN_R1, 0); r3_input_button(&in, R3_BTN_L1, 0);
    r3_input_button(&in, R3_BTN_L2, 1);
    CHECK(r3_input_button(&in, R3_BTN_R2, 1) == R3_CONTEXT, "L2 + R2 opens the contextual menu");
    r3_input_button(&in, R3_BTN_R2, 0); r3_input_button(&in, R3_BTN_L2, 0);
    r3_input_button(&in, R3_BTN_L1, 1);
    CHECK(r3_input_button(&in, R3_BTN_FN, 1) == R3_PANEL_FIRST, "L1 + Fn is the first panel");
    r3_input_button(&in, R3_BTN_FN, 0); r3_input_button(&in, R3_BTN_L1, 0);
    CHECK(!in.held[R3_BTN_L1] && !in.held[R3_BTN_FN], "releases are tracked");
    CHECK(r3_input_button(&in, 99, 1) == R3_NONE && r3_input_button(&in, -1, 1) == R3_NONE, "an unknown button does nothing");

    /* the stick: dead zone 0.35, 6 steps a second at full */
    CHECK(r3_stick_rate(0.35f) == 0 && r3_stick_rate(-0.2f) == 0, "nothing inside the dead zone");
    CHECK(fabsf(r3_stick_rate(1.0f) - 6.0f) < 1e-4f && fabsf(r3_stick_rate(-1.0f) - 6.0f) < 1e-4f, "6 steps a second at full");
    CHECK(fabsf(r3_stick_rate(0.675f) - 3.0f) < 1e-3f, "half way between the dead zone and full is 3 a second");
    CHECK(fabsf(r3_pointer_speed(1.0f, 0) - 120.0f) < 1e-3f && fabsf(r3_pointer_speed(1.0f, 1) - 360.0f) < 1e-3f, "the pointer is 120 px/s, 360 with R1");
    CHECK(r3_pointer_speed(0.3f, 1) == 0, "no pointer movement inside the dead zone");
    int out[16];
    r3_input_init(&in);
    CHECK(r3_input_tick(&in, 1000, out, 16) == 0, "a centred stick steps nothing");
    r3_input_axis(&in, R3_AXIS_RY, 1.0f);
    int n = r3_input_tick(&in, 1016, out, 16);
    CHECK(n == 1 && out[0] == R3_DOWN, "a push steps once at once (%d)", n);
    int steps = 0;
    for (uint32_t t = 1016 + 16; t <= 1016 + 1000; t += 16) { n = r3_input_tick(&in, t, out, 16); for (int i = 0; i < n; i++) { steps++; CHECK(out[i] == R3_DOWN, "down"); } }
    CHECK(steps >= 5 && steps <= 7, "about 6 steps in the next second at full (%d)", steps);
    r3_input_axis(&in, R3_AXIS_RY, -0.675f);
    steps = 0;
    r3_input_tick(&in, 3000, out, 16);
    for (uint32_t t = 3016; t <= 4016; t += 16) { n = r3_input_tick(&in, t, out, 16); for (int i = 0; i < n; i++) { steps++; CHECK(out[i] == R3_UP, "up"); } }
    CHECK(steps >= 2 && steps <= 4, "about 3 a second half way (%d)", steps);
    r3_input_axis(&in, R3_AXIS_RY, 0);
    CHECK(r3_input_tick(&in, 5000, out, 16) == 0 && !r3_input_stick_active(&in), "released: nothing");
    r3_input_axis(&in, R3_AXIS_RY, 0.2f);
    CHECK(r3_input_tick(&in, 5016, out, 16) == 0, "a nudge inside the dead zone does nothing");
    /* R1 held: the stick pages, and Home and End fire once a push */
    r3_input_init(&in);
    r3_input_button(&in, R3_BTN_R1, 1);
    r3_input_axis(&in, R3_AXIS_RY, -1.0f);
    n = r3_input_tick(&in, 100, out, 16);
    CHECK(n == 1 && out[0] == R3_PAGEUP, "R1 + the stick up is Page up");
    r3_input_axis(&in, R3_AXIS_RY, 0);
    r3_input_axis(&in, R3_AXIS_RX, 1.0f);
    n = r3_input_tick(&in, 200, out, 16);
    CHECK(n == 1 && out[0] == R3_END, "R1 + the stick right is End");
    for (uint32_t t = 216; t < 2000; t += 16) CHECK(r3_input_tick(&in, t, out, 16) == 0, "End fires once for a push, not repeatedly");
    r3_input_axis(&in, R3_AXIS_RX, -1.0f);
    CHECK(r3_input_tick(&in, 2100, out, 16) == 1 && out[0] == R3_HOME, "pushed the other way: Home");
    /* a stalled frame is not a burst */
    r3_input_init(&in);
    r3_input_axis(&in, R3_AXIS_RY, 1.0f);
    r3_input_tick(&in, 0, out, 16);
    n = r3_input_tick(&in, 60000, out, 16);
    CHECK(n <= 2, "a long stall gives at most a step or two (%d)", n);
    /* the left stick's pointer */
    r3_input_init(&in);
    r3_input_axis(&in, R3_AXIS_LX, 1.0f); r3_input_axis(&in, R3_AXIS_LY, -1.0f);
    float dx, dy;
    r3_input_pointer(&in, 1.0f, &dx, &dy);
    CHECK(fabsf(dx - 120) < 1e-3f && fabsf(dy + 120) < 1e-3f, "120 px in a second at full");
    r3_input_button(&in, R3_BTN_R1, 1);
    r3_input_pointer(&in, 1.0f, &dx, &dy);
    CHECK(fabsf(dx - 360) < 1e-3f, "360 px with R1");

    /* the list rule */
    CHECK(r3_list_step(0, 5, 3, R3_UP) == 4 && r3_list_step(4, 5, 3, R3_DOWN) == 0, "up and down wrap");
    CHECK(r3_list_step(1, 20, 5, R3_PAGEDOWN) == 6 && r3_list_step(18, 20, 5, R3_PAGEDOWN) == 19 && r3_list_step(2, 20, 5, R3_PAGEUP) == 0, "pages stop at the ends");
    CHECK(r3_list_step(7, 20, 5, R3_HOME) == 0 && r3_list_step(7, 20, 5, R3_END) == 19, "home and end");
    CHECK(r3_list_step(7, 20, 5, R3_LEFT) == 7 && r3_list_step(7, 20, 5, R3_RIGHT) == 7, "left and right are the app's, not paging");

    /* the main menu */
    R3MainMenu m;
    char msg[160];
    r3_mm_init(&m, 1);
    CHECK(r3_mm_selftest(&m, msg, sizeof msg) == 0, "%s", msg);
    int ids[R3MM_FIXED + R3MM_MAX_OWN];
    CHECK(r3_mm_ids(&m, ids) == 6 && ids[0] == R3MM_ABOUT && ids[5] == R3MM_QUIT, "six items with a benchmark");
    r3_mm_init(&m, 0);
    CHECK(r3_mm_ids(&m, ids) == 5 && ids[4] == R3MM_QUIT && ids[3] == R3MM_DEMO, "Benchmark hidden: five items, the order unchanged");
    m.own[0] = "Own"; m.n_own = 1;
    CHECK(r3_mm_ids(&m, ids) == 6 && ids[5] == R3MM_OWN0 && r3_mm_selftest(&m, msg, sizeof msg) == 0, "own items follow Quit");
    r3_mm_open(&m);
    CHECK(r3_mm_key(&m, R3_DOWN) == R3MM_NOTHING && m.sel == 1 && r3_mm_key(&m, R3_ACCEPT) == R3MM_THEME && !m.open, "down, then A, chooses Colour theme and closes");
    r3_mm_open(&m);
    CHECK(r3_mm_key(&m, R3_UP) == R3MM_NOTHING && r3_mm_label(&m, ids[m.sel]) != NULL && r3_mm_key(&m, R3_ACCEPT) == R3MM_OWN0, "up wraps to the last item");
    r3_mm_open(&m);
    CHECK(r3_mm_key(&m, R3_BACK) == R3MM_CLOSED && !m.open, "B closes");
    m.has_bench = 1; m.n_own = 0;
    R3Screen s;
    char d[R3_ROWS][R3_COLS + 1];
    r3_clear(&s);
    r3_mm_open(&m);
    r3_mm_draw(&s, &m, "VGA Classic");
    r3_dump(&s, d);
    int rows_with[6] = {0}, last = -1;
    const char *want[6] = {"About", "Colour theme", "Device report", "Rec demo", "Benchmark", "Quit"};
    for (int r = 0; r < R3_ROWS; r++) for (int i = 0; i < 6; i++) if (strstr(d[r], want[i])) rows_with[i] = r;
    for (int i = 0; i < 6; i++) { CHECK(rows_with[i] > last, "the menu draws %s below the one before", want[i]); last = rows_with[i]; }
    CHECK(strstr(d[rows_with[1]], "VGA Classic") != NULL, "Colour theme shows the theme's name");
    /* the true-colour pairs still pass in the menu: every theme, every cell of the menu, the About box and the picker */
    for (int t = 0; t < R3_THEME_COUNT; t++) {
        r3_theme_apply(t);
        R3Screen v[3];
        const char *credits[] = {"MIT", NULL};
        for (int k = 0; k < 3; k++) r3_clear(&v[k]);
        r3_mm_draw(&v[0], &m, r3_themes[t].name);
        r3_about_draw(&v[1], "App", "1.0", "abc 2026-11-20", credits);
        r3_theme_pick_draw(&v[2], t, t);
        for (int k = 0; k < 3; k++) for (int i = 0; i < R3_COLS * R3_ROWS; i++) {
            if (v[k].ch[i] == 32) continue;
            double c = r3_contrast_rgb(r3_pal[v[k].fg[i]], r3_pal[v[k].bg[i]]);
            CHECK(c >= 4.5, "theme %s, screen %d, cell %d: contrast %.2f", r3_themes[t].name, k, i, c);
        }
    }
    r3_theme_apply(0);
    if (fails) { fprintf(stderr, "%d check(s) failed\n", fails); return 1; }
    puts("test_input: ok");
    return 0;
}

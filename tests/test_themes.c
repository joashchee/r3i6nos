/* theme/r3i6nos_ansiapps.h's palette (true colour) and theme/r3i6nos_themes.h, with SDL's dummy video. Run by scripts/check.sh.
 * Every pair of colours the theme's own styles use, the toast's two grounds and each file kind on the page must pass 4.5:1 in every theme. */
#include "r3i6nos_ansiapps.h"
#include "r3i6nos_themes.h"

static int bad;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "test_themes: "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); bad++; } } while (0)

int main(void) {
    /* the default palette is the VGA one, so an app that never themes looks as it always did */
    r3_pal_reset();
    for (int i = 0; i < 16; i++)
        CHECK(r3_pal[i] == (0xFF000000u | (uint32_t)r3_rgb[i][0] << 16 | (uint32_t)r3_rgb[i][1] << 8 | r3_rgb[i][2]), "slot %d is VGA by default", i);
    CHECK(r3_near[R3_LIGHT_CYAN] == R3_LIGHT_CYAN && r3_near[200] == R3_LIGHT_GRAY, "the nearest VGA colour of a VGA slot is itself");
    unsigned g = r3_pal_gen;
    r3_pal_set(40, 0x123456);
    CHECK(r3_pal_gen == g + 1 && r3_pal[40] == 0xFF123456u, "a slot takes any 24-bit colour and counts the change");
    r3_pal_set(256, 0xFFFFFF);
    r3_pal_set(-1, 0xFFFFFF);
    CHECK(r3_pal_gen == g + 1, "a slot out of range is ignored");
    CHECK(r3_nearest_vga(0xFEFEFE) == R3_WHITE && r3_nearest_vga(0x010101) == R3_BLACK && r3_nearest_vga(0xFF5050) == R3_LIGHT_RED, "nearest VGA colour");
    CHECK(fabs(r3_contrast_rgb(0x000000, 0xFFFFFF) - 21.0) < 1e-9 && fabs(r3_contrast_pal(R3_WHITE, R3_BLACK) - 21.0) < 1e-9, "black on white is 21:1");

    /* the pixels come from the palette: a cell of a full block in slot 100 paints 0x336699 */
    r3_pal_set(100, 0x336699);
    R3Screen s;
    r3_clear(&s);
    r3_put(&s, 3, 2, 219, 100, R3_BLACK);
    static uint32_t px[R3_W * R3_H];
    r3_render(&s, px);
    CHECK(px[(2 * R3_CELL_H + 5) * R3_W + 3 * R3_CELL_W + 4] == 0xFF336699u, "r3_render paints a cell from its slot");
    CHECK(px[0] == r3_pal[R3_BLACK], "and the page from slot 0");

    /* the mirror sends the nearest VGA colour of each slot */
    CHECK(r3_mirror_attr(&s, 2 * R3_COLS + 3) == (unsigned char)(r3_nearest_vga(0x336699) | (R3_BLACK << 4)), "the mirror's attribute is the nearest VGA colours");

    CHECK(R3_THEME_COUNT >= 8, "a list of common themes: %d", R3_THEME_COUNT);
    int dark = 0, light = 0, mono = 0;
    for (int t = 0; t < R3_THEME_COUNT; t++) {
        const R3Theme *th = &r3_themes[t];
        dark += !strcmp(th->look, "dark"); light += !strcmp(th->look, "light"); mono += !strcmp(th->look, "mono");
        r3_theme_apply(t);
        for (int u = t + 1; u < R3_THEME_COUNT; u++) CHECK(strcasecmp(th->name, r3_themes[u].name) != 0, "two themes called %s", th->name);
        for (int st = 0; st < R3_ST_COUNT; st++)
            CHECK(r3_contrast_pal(r3_styles[st][0], r3_styles[st][1]) >= 4.5, "%s: style %d is %.2f:1", th->name, st, r3_contrast_pal(r3_styles[st][0], r3_styles[st][1]));
        CHECK(r3_contrast_pal(R3_BLACK, R3_LIGHT_GREEN) >= 4.5, "%s: the toast's ok ground", th->name);
        CHECK(r3_contrast_pal(R3_WHITE, R3_RED) >= 4.5, "%s: the toast's error ground", th->name);
        for (int k = 1; k < R3_FK_COUNT; k++)
            CHECK(r3_contrast_pal(r3_fk_slot(k), R3_BLACK) >= 4.5, "%s: file kind %d is %.2f:1 on the page", th->name, k, r3_contrast_pal(r3_fk_slot(k), R3_BLACK));
        CHECK(r3_pal[R3_BLACK] != r3_pal[R3_LIGHT_GRAY], "%s: page and text differ", th->name);
        {   /* the wait screen (every Device report, Rec demo and long job): every drawn cell passes 4.5:1, at 0, half and 100% */
            static R3Screen ws;
            for (int pct = 0; pct <= 100; pct += 50) {
                r3_wait_screen(&ws, "App", "Device report", "Device report", "The unit is measuring itself.", "No input is read.", "Power: idle", pct, "step 3 of 8, 12 s", "foot");
                double worst = 99;
                for (int i = 0; i < R3_COLS * R3_ROWS; i++) {
                    if (ws.ch[i] == 32 || ws.ch[i] == 0) continue;
                    double c = r3_contrast_pal(ws.fg[i], ws.bg[i]);
                    if (c < worst) worst = c;
                }
                CHECK(worst >= 4.5, "%s: the wait screen at %d%% has a cell at %.2f:1", th->name, pct, worst);
            }
            for (int tick = 0; tick < 3; tick++) {      /* the unknown-total form: the moving indicator */
                r3_wait_screen_t(&ws, "App", "Song", "Opening", "Reading the song.", NULL, "3 s of music read", -1, tick * 20, "12 s", NULL);
                double worst = 99;
                for (int i = 0; i < R3_COLS * R3_ROWS; i++) {
                    if (ws.ch[i] == 32 || ws.ch[i] == 0) continue;
                    double c = r3_contrast_pal(ws.fg[i], ws.bg[i]);
                    if (c < worst) worst = c;
                }
                CHECK(worst >= 4.5, "%s: the moving wait screen (tick %d) has a cell at %.2f:1", th->name, tick * 20, worst);
            }
        }
    }
    CHECK(dark >= 4 && light >= 2 && mono >= 3, "dark %d, light %d, monochrome %d themes", dark, light, mono);
    r3_theme_apply(-5);
    CHECK(r3_pal[R3_BLUE] == (0xFF000000u | r3_themes[0].vga[R3_BLUE]), "an index out of range is the first theme");
    CHECK(r3_theme_find("MONO GREEN") >= 0 && r3_theme_find("") < 0 && r3_theme_find(NULL) < 0, "find by name");
    CHECK(r3_fk_slot(R3_FK_PLAIN) == R3_LIGHT_GRAY && r3_fk_slot(R3_FK_DIR) == 16 && r3_fk_slot(R3_FK_HIDDEN) == 26, "the file kinds' slots");

    if (bad) { fprintf(stderr, "test_themes: %d failed\n", bad); return 1; }
    puts("test_themes: ok");
    return 0;
}

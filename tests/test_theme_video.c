/* theme/r3i6nos_ansiapps.h's presenting rule (r3_video_due, r3_video_draw), with SDL's dummy video and a software renderer.
 * Run by scripts/check.sh. Why the rule exists: on the R36S, a screen presented once stayed black (Commander, the IO Checker). */
#include "r3i6nos_ansiapps.h"

static int bad;
#define CHECK(c, what) do { if (!(c)) { fprintf(stderr, "test_theme_video: %s\n", what); bad++; } } while (0)

int main(void) {
    R3Video v;
    memset(&v, 0, sizeof v);
    int n = 0;
    for (int i = 0; i < 10; i++) n += r3_video_due(&v, i == 0);
    CHECK(n == R3_VIDEO_SETTLE, "a change is presented R3_VIDEO_SETTLE times, then not again");
    CHECK(R3_VIDEO_SETTLE >= 3, "at least three presents: once is what stayed black on the unit");
    n = 0;
    for (int i = 0; i < 10; i++) n += r3_video_due(&v, i == 0 || i == 2);
    CHECK(n == 2 + R3_VIDEO_SETTLE, "a second change restarts the count");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "test_theme_video: SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window *w = SDL_CreateWindow("t", 0, 0, R3_W, R3_H, 0);
    SDL_Renderer *r = w ? SDL_CreateRenderer(w, -1, SDL_RENDERER_SOFTWARE) : NULL;
    CHECK(r != NULL, "a software renderer");
    if (r) {
        CHECK(r3_video_init(&v, r), "r3_video_init");
        R3Screen s;
        r3_clear(&s);
        r3_top_bar(&s, "test", NULL);
        r3_video_draw(&v, r, &s, 0);
        CHECK(v.settle == R3_VIDEO_SETTLE - 1, "the first draw presents and owes the rest");
        for (int i = 0; i < 10; i++) r3_video_draw(&v, r, &s, 0);
        CHECK(v.settle == 0, "the same screen drawn again pays the debt, then stops");
        r3_put(&s, 5, 5, 'x', R3_WHITE, R3_BLACK);
        r3_video_draw(&v, r, &s, 0);
        CHECK(v.settle == R3_VIDEO_SETTLE - 1, "a changed cell owes presents again");
        r3_video_free(&v);
        SDL_DestroyRenderer(r);
    }
    if (w) SDL_DestroyWindow(w);
    SDL_Quit();
    if (!bad) puts("theme video: ok");
    return bad != 0;
}

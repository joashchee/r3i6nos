/* theme/r3i6nos_toast.h: the hot-plug toast (docs/HOTPLUG.md). Run by scripts/check.sh. */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <SDL.h>
#include <sys/stat.h>
#include <unistd.h>
#include "r3i6nos_ansiapps.h"
#include "r3i6nos_toast.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; fprintf(stderr, "FAIL line %d: ", __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static char events[600];
static long long clock_ns = 1000;

static void add(const char *level, const char *text) {
    FILE *f = fopen(events, "a");
    fprintf(f, "%lld\t%s\t%s\n", clock_ns++, level, text);
    fclose(f);
}

static int row0_has(const R3Toast *t, const char *text) {
    static R3Screen s;
    char d[R3_ROWS][R3_COLS + 1];
    r3_clear(&s);
    r3_top_bar(&s, "App", "1.0");
    r3_toast_draw(t, &s);
    r3_dump(&s, d);
    return strstr(d[0], text) != NULL;
}

int main(void) {
    char dir[512];
    snprintf(dir, sizeof dir, "%s/r3-toast-%ld", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (long)getpid());
    if (mkdir(dir, 0700) != 0) { perror("mkdir"); return 1; }
    setenv("R3I6NOS_HOTPLUG_DIR", dir, 1);
    snprintf(events, sizeof events, "%s/events", dir);

    add("info", "Headphones connected");        /* before the app started: history */
    R3Toast t;
    r3_toast_init(&t);
    r3_toast_poll(&t, 0);
    CHECK(!t.showing && !r3_toast_active(&t), "what was in the file at start is not shown");

    add("info", "USB audio connected: Zeta Headset");
    r3_toast_poll(&t, 1000);
    CHECK(t.showing && !t.error && row0_has(&t, "USB audio connected: Zeta Headset"), "a new change is a toast over the top bar");
    CHECK(!row0_has(&t, "App"), "it covers the bar's text");
    CHECK(r3_toast_key(&t) == 0 && t.showing, "a key does not clear a change toast, and is not used up");
    r3_toast_poll(&t, 3900);
    CHECK(t.showing, "still up at 2.9 s");
    r3_toast_poll(&t, 4001);
    CHECK(!t.showing && !r3_toast_active(&t) && !row0_has(&t, "Zeta"), "gone after 3 seconds, the top bar back");

    add("info", "DC power connected");
    add("info", "Headphones removed");
    r3_toast_poll(&t, 5000);
    CHECK(t.showing && row0_has(&t, "DC power connected"), "several at once show in order: the first");
    r3_toast_poll(&t, 8100);
    CHECK(t.showing && row0_has(&t, "Headphones removed"), "then the next, each for its 3 seconds");
    r3_toast_poll(&t, 11200);
    CHECK(!t.showing, "and then nothing");

    add("error", "A USB device did not start: its cable or the device may be bad");
    r3_toast_poll(&t, 12000);
    CHECK(t.showing && t.error && row0_has(&t, "did not start") && row0_has(&t, "Press a key"), "a failure says what and how to clear it");
    r3_toast_poll(&t, 600000);
    CHECK(t.showing && t.error, "a failure stays however long it takes");
    CHECK(r3_toast_key(&t) == 1 && !t.showing, "a key clears it and is used up");
    CHECK(r3_toast_key(&t) == 0, "with nothing up a key is the app's own");

    /* a failure interrupts a change toast, and goes first in the queue */
    add("info", "USB storage connected: Stick");
    r3_toast_poll(&t, 700000);
    CHECK(t.showing && !t.error, "a change toast");
    add("error", "USB audio Dead DAC gives no sound output");
    r3_toast_poll(&t, 700400);
    CHECK(t.showing && t.error && row0_has(&t, "Dead DAC"), "a failure does not wait for a flash to end");
    r3_toast_key(&t);
    r3_toast_poll(&t, 700500);
    CHECK(!t.showing, "the interrupted change is not brought back");

    /* many at once: a full queue drops the oldest change, never a failure */
    for (int i = 0; i < 12; i++) add("info", "noise");
    add("error", "the one that matters");
    r3_toast_poll(&t, 800000);
    CHECK(t.showing && t.error && row0_has(&t, "the one that matters"), "the failure is kept when the queue overflows");

    /* held (a Rec demo records): nothing shows, a change is dropped, a failure waits and shows when the hold ends */
    r3_toast_key(&t); r3_toast_poll(&t, 900000);
    r3_toast_hold(&t, 1);
    add("info", "Headphones connected");
    add("error", "TF2 card removed while in use: its last changes may be lost");
    r3_toast_poll(&t, 900500);
    CHECK(!t.showing && !row0_has(&t, "Headphones") && !row0_has(&t, "TF2"), "held: no toast is drawn");
    char errs[4][96];
    CHECK(r3_toast_errors(&t, errs, 4) == 1 && strstr(errs[0], "TF2 card removed"), "the failure waits, the change was dropped");
    CHECK(r3_toast_key(&t) == 0, "held: a key is the app's own");
    r3_toast_hold(&t, 0);
    r3_toast_poll(&t, 901000);
    CHECK(t.showing && t.error && row0_has(&t, "TF2 card removed"), "when the hold ends the failure shows, until a key");
    r3_toast_hold(&t, 1);
    r3_toast_hold(&t, 0);
    r3_toast_poll(&t, 902000);
    CHECK(t.showing && t.error, "a failure that was up when the hold began is up again after it");
    r3_toast_key(&t);

    /* the toast's colours pass the theme's 4.5:1 */
    CHECK(r3_contrast(R3_BLACK, R3_LIGHT_GREEN) >= 4.5 && r3_contrast(R3_WHITE, R3_RED) >= 4.5, "the toast's two colour pairs are readable");

    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd) != 0) fprintf(stderr, "test_toast: could not remove %s\n", dir);
    if (fails) return 1;
    puts("test_toast: ok");
    return 0;
}

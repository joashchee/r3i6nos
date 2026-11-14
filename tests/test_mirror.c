/* theme/r3i6nos_mirror.h: the datagrams the screen mirror's daemon (overlay/usr/local/bin/r3i6nos-mirror) reads, and when they are sent;
 * the C twin of tests/test_mirror.lua. Build: cc -std=c11 -Wall -Wextra -I theme $(sdl2-config --cflags) tests/test_mirror.c $(sdl2-config --libs) -lm */
#include "r3i6nos_ansiapps.h"

static int failures;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } } while (0)

/* The daemon's side: apply a datagram to state[3072], as r3i6nos-mirror's parse_datagram does. 0 when it is malformed. */
static int apply(unsigned char *state, const unsigned char *d, size_t n) {
    if (n >= 1 && d[0] == 'F') { if (n != R3_MIRROR_FULL) return 0; memcpy(state, d + 1, R3_MIRROR_CELLS * 2); return 1; }
    if (n < 2 || d[0] != 'D') return 0;
    size_t i = 1;
    while (i < n) {
        if (i + 3 > n) return 0;
        int start = d[i] * 256 + d[i + 1], count = d[i + 2];
        i += 3;
        if (count == 0 || start + count > R3_MIRROR_CELLS || i + (size_t)count * 2 > n) return 0;
        memcpy(state + start * 2, d + i, (size_t)count * 2);
        i += (size_t)count * 2;
    }
    return 1;
}

static void show(const R3Screen *s, unsigned char *out) {
    for (int i = 0; i < R3_MIRROR_CELLS; i++) { out[2 * i] = s->ch[i]; out[2 * i + 1] = (unsigned char)(s->fg[i] | (s->bg[i] << 4)); }
}

/* the hooks' world */
static int hook_on, sent_n;
static time_t hook_now = 1000;
static unsigned char sent[8][R3_MIRROR_MAX];
static size_t sent_len[8];
static int looks;
static int on_hook(void) { looks++; return hook_on; }
static time_t now_hook(void) { return hook_now; }
static void send_hook(const unsigned char *d, size_t n) { if (sent_n < 8) { memcpy(sent[sent_n], d, n); sent_len[sent_n] = n; } sent_n++; }

int main(void) {
    static R3Screen a, b;
    static unsigned char buf[R3_MIRROR_MAX], st[R3_MIRROR_CELLS * 2], want[R3_MIRROR_CELLS * 2];

    r3_clear(&a);
    r3_top_bar(&a, "r3i6nos", "v1");
    CHECK(r3_mirror_full(&a, buf) == R3_MIRROR_FULL, "a full frame is 3073 bytes");
    CHECK(apply(st, buf, R3_MIRROR_FULL), "a full frame applies");
    show(&a, want);
    CHECK(memcmp(st, want, sizeof st) == 0, "a full frame is the screen");
    CHECK(st[1] == (R3_WHITE | (R3_BLUE << 4)), "cell 0 of the top bar is white on blue");

    b = a;
    CHECK(r3_mirror_delta(&a, &b, buf) == 0, "nothing changed, nothing sent");

    r3_put(&a, 5, 2, 'A', R3_YELLOW, R3_RED);
    size_t n = r3_mirror_delta(&a, &b, buf);
    CHECK(n == 6, "one changed cell is 6 bytes");
    CHECK(buf[1] * 256 + buf[2] == 2 * 64 + 5 && buf[3] == 1, "the run starts at the cell and holds one");
    show(&b, st);
    CHECK(apply(st, buf, n), "a delta applies");
    show(&a, want);
    CHECK(memcmp(st, want, sizeof st) == 0, "a delta brings the page to the screen");

    /* one unchanged cell between two changes is carried along; a wider gap starts a new run */
    r3_clear(&a); b = a;
    a.ch[10] = 'A'; a.ch[12] = 'B';
    CHECK(r3_mirror_delta(&a, &b, buf) == 1 + 3 + 3 * 2, "one run of three cells");
    r3_clear(&a);
    a.ch[10] = 'A'; a.ch[14] = 'B';
    CHECK(r3_mirror_delta(&a, &b, buf) == 1 + 2 * (3 + 2), "two runs of one cell");

    /* a run never passes 255 cells; most of the screen changing is sent whole */
    r3_clear(&a);
    for (int i = 0; i < 600; i++) a.ch[i] = 'B';
    n = r3_mirror_delta(&a, &b, buf);
    CHECK(buf[0] == 'D' && buf[3] == 255, "a long run is cut at 255");
    show(&b, st);
    CHECK(apply(st, buf, n), "and applies");
    show(&a, want);
    CHECK(memcmp(st, want, sizeof st) == 0, "to the same screen");
    for (int i = 0; i < 1000; i++) a.ch[i] = 'C';
    CHECK(r3_mirror_delta(&a, &b, buf) == R3_MIRROR_FULL && buf[0] == 'F', "more than half changed: the whole screen");

    /* a fuzz: any change from any screen replays to the same screen */
    srand(7);
    for (int round = 0; round < 300; round++) {
        for (int i = 0; i < R3_MIRROR_CELLS; i++) { a.ch[i] = (unsigned char)(rand() % 256); a.fg[i] = (unsigned char)(rand() % 16); a.bg[i] = (unsigned char)(rand() % 16); }
        b = a;
        int edits = 1 + rand() % 400;
        for (int k = 0; k < edits; k++) {
            int i = rand() % R3_MIRROR_CELLS;
            b.ch[i] = (unsigned char)(rand() % 256); b.fg[i] = (unsigned char)(rand() % 16); b.bg[i] = (unsigned char)(rand() % 16);
        }
        n = r3_mirror_delta(&b, &a, buf);
        if (n) {
            show(&a, st);
            if (!apply(st, buf, n)) { CHECK(0, "a fuzz datagram is malformed"); break; }
            show(&b, want);
            if (memcmp(st, want, sizeof st) != 0) { CHECK(0, "a fuzz delta replays to another screen"); break; }
        }
    }

    /* when they are sent */
    r3_mirror_st.on_hook = on_hook; r3_mirror_st.now_hook = now_hook; r3_mirror_st.send_hook = send_hook;
    r3_clear(&a);
    r3_mirror_tap(&a, NULL);
    CHECK(sent_n == 0 && looks == 1, "no mirror, nothing sent");
    b = a; b.ch[0] = 'A';
    r3_mirror_tap(&b, &a);
    CHECK(looks == 1, "the flag is looked at once a second at most, not on every change");
    hook_now++;
    r3_mirror_tap(&b, &a);
    CHECK(looks == 2 && sent_n == 0, "a second later it looks again, still off");

    hook_on = 1; hook_now++;
    R3Screen c = b; c.ch[1] = 'B';
    r3_mirror_tap(&c, &b);
    CHECK(sent_n == 1 && sent[0][0] == 'F', "a mirror that has just come on is given the whole screen first");
    R3Screen d = c; d.ch[2] = 'C';
    r3_mirror_tap(&d, &c);
    CHECK(sent_n == 2 && sent[1][0] == 'D' && sent_len[1] == 6, "then only what changed");
    r3_mirror_tap(&a, NULL);
    CHECK(sent_n == 3 && sent[2][0] == 'F', "an app's first screen (no previous one) is sent whole");

    hook_on = 0; hook_now++;
    r3_mirror_tap(&d, &c);
    CHECK(sent_n == 3, "the flag gone: nothing more is sent");
    hook_on = 1; hook_now++;
    r3_mirror_tap(&d, &c);
    CHECK(sent_n == 4 && sent[3][0] == 'F', "and when it comes back, the whole screen again");

    if (failures) { printf("test_mirror: %d failure(s)\n", failures); return 1; }
    printf("ok: test_mirror.c\n");
    return 0;
}

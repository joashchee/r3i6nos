/* The screen mirror's tap for C and SDL2 (docs/SCREEN-MIRROR.md, ROADMAP section 28). The C twin of theme/r3i6nos_mirror.lua: when a
 * mirror is on, hand the CELLS THAT CHANGED to the daemon r3i6nos-mirror as UDP datagrams to 127.0.0.1:
 *   'F' + 3072 bytes                                       the whole screen: cell i is bytes 2i (character), 2i+1 (fg | bg << 4)
 *   'D' + runs of (u16 big-endian start, u8 count, count x 2 bytes)   the changed cells
 * A screen is presented (and so tapped) only when a cell changed; with no mirror the cost of that is one stat() of the flag file, at most
 * once a second: no timer, no keyframe on a timer, no polling. Nothing here may break an app: a failed send is ignored.
 *
 * Not self-contained: r3i6nos_ansiapps.h includes it after R3Screen, and r3_video_draw calls r3_mirror_tap. Tests replace the world through the
 * hooks (tests/test_mirror.c). Overrides: R3I6NOS_MIRROR_DIR (the flag's folder), R3I6NOS_MIRROR_INGEST (the daemon's UDP port). */
#ifndef R3I6NOS_MIRROR_H
#define R3I6NOS_MIRROR_H

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { R3_MIRROR_CELLS = R3_COLS * R3_ROWS, R3_MIRROR_FULL = 1 + R3_MIRROR_CELLS * 2, R3_MIRROR_MAX = 8192 };

typedef struct {
    int fd;                     /* the UDP socket, made on first use (-1 none, -2 could not be made) */
    int on;                     /* the last answer of the flag test */
    int sent_full;              /* the daemon holds a whole screen of ours since the mirror came on */
    int checked_set;
    time_t checked;
    /* hooks, for tests: NULL means the real thing */
    int (*on_hook)(void);
    time_t (*now_hook)(void);
    void (*send_hook)(const unsigned char *data, size_t n);
} R3Mirror;

static R3Mirror r3_mirror_st = { -1, 0, 0, 0, 0, NULL, NULL, NULL };

static inline unsigned char r3_mirror_attr(const R3Screen *s, int i) { return (unsigned char)((s->fg[i] & 15) | ((s->bg[i] & 15) << 4)); }

/* The whole screen into out (R3_MIRROR_FULL bytes). */
static inline size_t r3_mirror_full(const R3Screen *s, unsigned char *out) {
    out[0] = 'F';
    for (int i = 0; i < R3_MIRROR_CELLS; i++) { out[1 + 2 * i] = s->ch[i]; out[2 + 2 * i] = r3_mirror_attr(s, i); }
    return R3_MIRROR_FULL;
}

static inline int r3_mirror_differs(const R3Screen *s, const R3Screen *p, int i) {
    return s->ch[i] != p->ch[i] || s->fg[i] != p->fg[i] || s->bg[i] != p->bg[i];
}

/* The cells that differ from prev into out (R3_MIRROR_MAX bytes); 0 when none do. A run is contiguous changed cells, at most 255; one
 * unchanged cell between two changes is carried along (a new run costs 3 bytes, a cell 2). More than half changed: the whole screen. */
static inline size_t r3_mirror_delta(const R3Screen *s, const R3Screen *prev, unsigned char *out) {
    size_t n = 1;
    int changed = 0;
    out[0] = 'D';
    for (int i = 0; i < R3_MIRROR_CELLS;) {
        if (!r3_mirror_differs(s, prev, i)) { i++; continue; }
        int first = i, last = i, j = i + 1;
        while (j < R3_MIRROR_CELLS && j - first < 255) {
            if (r3_mirror_differs(s, prev, j)) last = j;
            else if (!(j + 1 < R3_MIRROR_CELLS && j + 1 - first < 255 && r3_mirror_differs(s, prev, j + 1))) break;
            j++;
        }
        int count = last - first + 1;
        if (n + 3 + (size_t)count * 2 > R3_MIRROR_MAX) return r3_mirror_full(s, out);
        out[n++] = (unsigned char)(first >> 8);
        out[n++] = (unsigned char)(first & 255);
        out[n++] = (unsigned char)count;
        for (int k = first; k <= last; k++) { out[n++] = s->ch[k]; out[n++] = r3_mirror_attr(s, k); }
        changed += count;
        i = last + 1;
    }
    if (changed == 0) return 0;
    if (changed > R3_MIRROR_CELLS / 2) return r3_mirror_full(s, out);
    return n;
}

static inline int r3_mirror_flag(void) {
    if (r3_mirror_st.on_hook) return r3_mirror_st.on_hook();
    const char *d = getenv("R3I6NOS_MIRROR_DIR");
    char path[512];
    struct stat st;
    snprintf(path, sizeof path, "%s/on", d && *d ? d : "/run/loveos/mirror");
    return stat(path, &st) == 0;
}

static inline void r3_mirror_send(const unsigned char *data, size_t n) {
    if (r3_mirror_st.send_hook) { r3_mirror_st.send_hook(data, n); return; }
    if (r3_mirror_st.fd == -1) {
        r3_mirror_st.fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (r3_mirror_st.fd < 0) r3_mirror_st.fd = -2;
        else (void)fcntl(r3_mirror_st.fd, F_SETFL, O_NONBLOCK); /* a full buffer drops the datagram: it never makes the app wait */
    }
    if (r3_mirror_st.fd < 0) return;
    const char *p = getenv("R3I6NOS_MIRROR_INGEST");
    struct sockaddr_in to;
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons((uint16_t)(p && *p ? atoi(p) : 36364));
    to.sin_addr.s_addr = inet_addr("127.0.0.1"); /* (no INADDR_LOOPBACK: a strict _POSIX_C_SOURCE hides it) */
    (void)sendto(r3_mirror_st.fd, data, n, 0, (struct sockaddr *)&to, sizeof to); /* a daemon that is gone is not our problem */
}

/* Is a mirror on? Looks at the flag file at most once a second. */
static inline int r3_mirror_active(void) {
    time_t now = r3_mirror_st.now_hook ? r3_mirror_st.now_hook() : time(NULL);
    if (!r3_mirror_st.checked_set || now - r3_mirror_st.checked >= 1 || now < r3_mirror_st.checked) {
        int was = r3_mirror_st.on;
        r3_mirror_st.checked = now;
        r3_mirror_st.checked_set = 1;
        r3_mirror_st.on = r3_mirror_flag() ? 1 : 0;
        if (r3_mirror_st.on && !was) r3_mirror_st.sent_full = 0; /* a mirror just came on: it needs the whole screen first */
    }
    return r3_mirror_st.on;
}

/* Call when a screen has changed and is about to be remembered as prev (NULL on an app's first screen). */
static inline void r3_mirror_tap(const R3Screen *s, const R3Screen *prev) {
    if (!r3_mirror_active()) return;
    unsigned char buf[R3_MIRROR_MAX];
    size_t n;
    if (prev && r3_mirror_st.sent_full) n = r3_mirror_delta(s, prev, buf);
    else { n = r3_mirror_full(s, buf); r3_mirror_st.sent_full = 1; }
    if (n) r3_mirror_send(buf, n);
}

#endif

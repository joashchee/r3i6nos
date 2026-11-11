/* r3i6nos hot-plug toast for native apps (C, SDL2): docs/HOTPLUG.md. MIT, like the rest of theme/.
 *
 * r3i6nos-hotplug (a root service) writes one line for every change of what is plugged in (headphones in the jack, a USB device in the OTG port,
 * the DC power port) to /run/r3i6nos-hotplug/events: "<nanoseconds>\t<info|error>\t<text>". This header reads that file and shows each new line as a toast
 * over the top bar (row 0): an info toast flashes for 3 seconds; an error (something failed) stays until a key is pressed, and that key is
 * consumed, so it cannot also press a button in the app.
 *
 *   R3Toast toast;
 *   r3_toast_init(&toast);                                  // at start: what is already in the file is history, not shown
 *   ...each frame:   r3_toast_poll(&toast, SDL_GetTicks()); // reads new lines, moves on when 3 seconds are up
 *   ...every key or pad button, first:  if (r3_toast_key(&toast)) continue;   // true: it cleared an error toast and is used up
 *   ...after drawing the screen:        r3_toast_draw(&toast, &screen);       // over row 0 when one is showing
 *   r3_toast_active(&toast) tells the loop to keep redrawing (a toast has to disappear on time).
 *
 * R3I6NOS_HOTPLUG_DIR overrides the folder (tests). Include r3i6nos_ansiapps.h first. */
#ifndef R3I6NOS_TOAST_H
#define R3I6NOS_TOAST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define R3_TOAST_MS 3000
#define R3_TOAST_QUEUE 8
#define R3_TOAST_LOOK_MS 250            /* how often the file is looked at */

typedef struct { int error; char text[96]; } R3ToastLine;

typedef struct {
    char path[512];
    long long seen;                     /* the last event number taken: older ones are history */
    double last_look;
    R3ToastLine queue[R3_TOAST_QUEUE];
    int n;
    int showing, error;                 /* a toast is up; it is a failure */
    char text[96];
    double until;                       /* an info toast's end, in the caller's milliseconds */
    int held;                           /* Rec demo: nothing is shown; changes are dropped, failures wait in the queue for the end */
} R3Toast;

/* Reads the lines newer than `seen` into `out` (up to `cap`), updates `*seen` and returns how many. The text is cut to what fits. */
static inline int r3_toast_read(const char *path, long long *seen, R3ToastLine *out, int cap) {
    FILE *f = fopen(path, "r");
    char line[400];
    int n = 0;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char *tab1 = strchr(line, '\t'), *tab2 = tab1 ? strchr(tab1 + 1, '\t') : NULL;
        if (!tab2) continue;
        *tab1 = 0;
        *tab2 = 0;
        long long ns = atoll(line);
        if (ns <= *seen) continue;
        *seen = ns;
        if (n == cap) { memmove(out, out + 1, (size_t)(cap - 1) * sizeof *out); n = cap - 1; }
        out[n].error = !strcmp(tab1 + 1, "error");
        snprintf(out[n].text, sizeof out[n].text, "%s", tab2 + 1);
        out[n].text[strcspn(out[n].text, "\r\n")] = 0;
        n++;
    }
    fclose(f);
    return n;
}

static inline void r3_toast_init(R3Toast *t) {
    const char *dir = getenv("R3I6NOS_HOTPLUG_DIR");
    R3ToastLine skip[R3_TOAST_QUEUE];
    memset(t, 0, sizeof *t);
    snprintf(t->path, sizeof t->path, "%s/events", dir && *dir ? dir : "/run/r3i6nos-hotplug");
    t->last_look = -1e9;
    r3_toast_read(t->path, &t->seen, skip, R3_TOAST_QUEUE);   /* the history is not news */
}

static inline int r3_toast_active(const R3Toast *t) { return t->showing || t->n > 0; }

static inline void r3_toast_next(R3Toast *t, double now_ms) {
    int pick = -1;
    if (t->n == 0) { t->showing = 0; return; }
    for (int i = 0; i < t->n; i++) if (t->queue[i].error) { pick = i; break; }   /* a failure goes first */
    if (pick < 0) pick = 0;
    t->showing = 1;
    t->error = t->queue[pick].error;
    snprintf(t->text, sizeof t->text, "%s", t->queue[pick].text);
    t->until = now_ms + R3_TOAST_MS;
    memmove(&t->queue[pick], &t->queue[pick + 1], (size_t)(t->n - pick - 1) * sizeof t->queue[0]);
    t->n--;
}

/* While a Rec demo records the toast is held: no toast is shown (the recording is the app's own frames, and the unit is locked), a change is
 * dropped, and a failure waits in the queue. When the hold ends the failures show, one at a time, each until a key. */
static inline void r3_toast_hold(R3Toast *t, int on) {
    if (on && !t->held && t->showing && t->error && t->n < R3_TOAST_QUEUE) {   /* a failure already up goes back to the front of the queue */
        memmove(&t->queue[1], &t->queue[0], (size_t)t->n * sizeof t->queue[0]);
        t->queue[0].error = 1;
        snprintf(t->queue[0].text, sizeof t->queue[0].text, "%s", t->text);
        t->n++;
    }
    if (on) {
        int k = 0;
        for (int i = 0; i < t->n; i++) if (t->queue[i].error) t->queue[k++] = t->queue[i];   /* queued changes are dropped, failures kept */
        t->n = k;
        t->showing = 0;
    }
    t->held = on;
}

/* The failures waiting in the queue (texts into `out`, at most `cap`): how a recorder that holds a toast of its own learns to stop. */
static inline int r3_toast_errors(const R3Toast *t, char out[][96], int cap) {
    int k = 0;
    for (int i = 0; i < t->n && k < cap; i++) if (t->queue[i].error) snprintf(out[k++], 96, "%s", t->queue[i].text);
    return k;
}

/* Call every frame with a millisecond clock. */
static inline void r3_toast_poll(R3Toast *t, double now_ms) {
    if (now_ms - t->last_look >= R3_TOAST_LOOK_MS) {
        R3ToastLine fresh[R3_TOAST_QUEUE];
        int got;
        t->last_look = now_ms;
        got = r3_toast_read(t->path, &t->seen, fresh, R3_TOAST_QUEUE);
        for (int i = 0; i < got; i++) {
            if (t->held && !fresh[i].error) continue;
            if (t->n == R3_TOAST_QUEUE) {   /* full: drop the oldest info, never a failure */
                int drop = -1;
                for (int k = 0; k < t->n && drop < 0; k++) if (!t->queue[k].error) drop = k;
                if (drop < 0) break;
                memmove(&t->queue[drop], &t->queue[drop + 1], (size_t)(t->n - drop - 1) * sizeof t->queue[0]);
                t->n--;
            }
            t->queue[t->n++] = fresh[i];
        }
    }
    if (t->held) { t->showing = 0; return; }
    if (t->showing && !t->error && now_ms >= t->until) t->showing = 0;
    if (!t->showing) r3_toast_next(t, now_ms);
    else if (!t->error) {   /* a failure does not wait for a flash to end */
        for (int i = 0; i < t->n; i++) if (t->queue[i].error) { r3_toast_next(t, now_ms); break; }
    }
}

/* A key or pad button went down. 1 when it cleared a failure toast (use it up: it must not also act); 0 otherwise. */
static inline int r3_toast_key(R3Toast *t) {
    if (!t->showing || !t->error) return 0;
    t->showing = 0;
    return 1;
}

/* Over the top bar (row 0): black on light green for a change, white on red for a failure, which says how to clear it. */
static inline void r3_toast_draw(const R3Toast *t, R3Screen *s) {
    if (!t->showing) return;
    int fg = t->error ? R3_WHITE : R3_BLACK, bg = t->error ? R3_RED : R3_LIGHT_GREEN;
    for (int c = 0; c < R3_COLS; c++) r3_put(s, c, R3_TOP_ROW, ' ', fg, bg);
    r3_text_fit(s, 1, R3_TOP_ROW, t->text, t->error ? R3_COLS - 18 : R3_COLS - 2, fg, bg);
    if (t->error) r3_text_fit(s, R3_COLS - 16, R3_TOP_ROW, "Press a key", 15, fg, bg);
}

#endif /* R3I6NOS_TOAST_H */

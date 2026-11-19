/* wave.h: MusiCue's waveform (header-only, MIT). The WHOLE song across the screen's width (no scrolling): WAVE_W columns of 1 pixel, each the lowest and
 * highest sample and the RMS of its slice of the song, drawn as a symmetric shape with a gradient from the loud core to the quiet edge. Played and
 * not-yet-played parts use two colour pairs, markers are thin lines with a flag, an A/B loop is a tinted span between two lines, and a bright playhead
 * moves across. It is the one part of MusiCue that is drawn in pixels instead of text cells (everything else is the text-mode theme); it fills the 6
 * text rows under the top bar (rows 1-6 = 640 x 120 pixels), so a pixel column is one tenth of a text column and a text row can carry the markers' names
 * in step with it. Pure C (no SDL): it writes ARGB pixels, so the tests draw it into a buffer and look at the pixels. */
#ifndef MUSICUE_WAVE_H
#define MUSICUE_WAVE_H

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define WAVE_W 640
#define WAVE_H 120
#define WAVE_TOP 9                  /* pixel rows kept free above the shape for the marker flags */
#define WAVE_MAX_MARKS 512

typedef struct {
    int16_t mn[WAVE_W], mx[WAVE_W];     /* the lowest and highest mono sample in the column (the two channels averaged) */
    uint16_t rms[WAVE_W];
    int peak;                           /* the loudest sample of the song */
    int cols;                           /* WAVE_W, or 0 when there is no sound */
} WavePeaks;

/* Peaks over the whole of `pcm` (frames of left, right, 16-bit). Long songs are sampled every few frames inside a column, which keeps a 12-minute song
 * to about a tenth of a second on a fast core (untested on the unit). */
static void wave_peaks(const int16_t *pcm, int64_t frames, WavePeaks *w) {
    memset(w, 0, sizeof *w);
    if (!pcm || frames < 1) return;
    w->cols = WAVE_W;
    for (int c = 0; c < WAVE_W; c++) {
        int64_t f0 = frames * c / WAVE_W, f1 = frames * (c + 1) / WAVE_W;
        if (f1 <= f0) f1 = f0 + 1;
        if (f1 > frames) f1 = frames;
        int64_t step = (f1 - f0) / 2048;
        if (step < 1) step = 1;
        int mn = 32767, mx = -32768;
        double sum2 = 0;
        int64_t cnt = 0;
        for (int64_t f = f0; f < f1; f += step) {
            int m = (pcm[f * 2] + pcm[f * 2 + 1]) / 2;
            if (m < mn) mn = m;
            if (m > mx) mx = m;
            sum2 += (double)m * m;
            cnt++;
        }
        if (cnt == 0) { mn = mx = 0; cnt = 1; }
        w->mn[c] = (int16_t)mn;
        w->mx[c] = (int16_t)mx;
        w->rms[c] = (uint16_t)sqrt(sum2 / (double)cnt);
        int a = abs(mn) > abs(mx) ? abs(mn) : abs(mx);
        if (a > w->peak) w->peak = a;
    }
}

/* ---- colour helpers (ARGB, 0xAARRGGBB) --------------------------------------------------------------------------------------------------- */

static inline uint32_t wv_lerp(uint32_t a, uint32_t b, double t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    uint32_t out = 0xFF000000u;
    for (int sh = 0; sh <= 16; sh += 8) {
        double x = (double)(a >> sh & 255) * (1 - t) + (double)(b >> sh & 255) * t;
        out |= (uint32_t)(x + 0.5) << sh;
    }
    return out;
}

typedef struct {
    uint32_t bg;                    /* the background behind the shape; it fades a little towards the top and bottom edges */
    uint32_t played_core, played_edge;      /* the loud middle and the quiet outline of the part already played */
    uint32_t todo_core, todo_edge;          /* the same for the part still to come (dimmer) */
    uint32_t centre;                /* the centre line */
    uint32_t playhead;
    uint32_t loop_fill, loop_line;
} WaveStyle;

typedef struct { double t; uint32_t color; } WaveMark;

/* The pixel column of a time in a song of `dur` seconds (0 .. WAVE_W-1). */
static inline int wave_x(double t, double dur) {
    if (dur <= 0) return 0;
    int x = (int)(t / dur * (WAVE_W - 1) + 0.5);
    return x < 0 ? 0 : x > WAVE_W - 1 ? WAVE_W - 1 : x;
}

/* Draw the waveform into px (stride in pixels; WAVE_W x WAVE_H are written). `now` is the playhead in seconds; loop_on with loop_a..loop_b seconds
 * tints the loop; marks are drawn as lines with a flag in their colour. With no sound (w->cols == 0) it draws the background and a flat line. */
static void wave_render(uint32_t *px, int stride, const WavePeaks *w, const WaveStyle *st, double dur, double now,
                        const WaveMark *marks, int nmarks, int loop_on, double loop_a, double loop_b) {
    const int cy = WAVE_H / 2, half = cy - WAVE_TOP - 1;
    double scale = (double)half / (w->peak > 3000 ? w->peak : 3000);
    int playx = wave_x(now, dur);
    int la = loop_on ? wave_x(loop_a, dur) : -1, lb = loop_on ? wave_x(loop_b, dur) : -1;
    for (int x = 0; x < WAVE_W; x++) {
        double top = 0, bot = 0, rms = 0;
        if (w->cols) {
            top = w->mx[x] * scale;                 /* up is positive */
            bot = w->mn[x] * scale;
            rms = w->rms[x] * scale;
            if (top < 0.5) top = 0.5;
            if (bot > -0.5) bot = -0.5;
        }
        int played = x <= playx;
        uint32_t core = played ? st->played_core : st->todo_core, edge = played ? st->played_edge : st->todo_edge;
        int in_loop = loop_on && x >= la && x <= lb;
        for (int y = 0; y < WAVE_H; y++) {
            double dy = (double)(cy - y) - 0.5;           /* distance above the centre, at the pixel's middle */
            double fade = fabs(dy) / (double)cy;
            uint32_t c = wv_lerp(st->bg, 0xFF000000u, 0.35 * fade * fade);
            if (w->cols && dy <= top && dy >= bot) {
                double reach = dy >= 0 ? top : -bot;      /* how far this column goes on this side */
                double e = dy >= 0 ? top - dy : dy - bot;
                double cov = e < 1.0 ? e : 1.0;           /* the outline's last pixel is partly covered */
                double d = fabs(dy);
                uint32_t shape;
                if (d <= rms) shape = wv_lerp(core, wv_lerp(core, edge, 0.4), rms > 0 ? d / rms : 0);                    /* the loud core, a little dimmer outward */
                else shape = wv_lerp(wv_lerp(core, edge, 0.65), edge, reach > rms ? (d - rms) / (reach - rms) : 1);      /* the halo out to the peak */
                c = wv_lerp(c, shape, cov);
            }
            if (y == cy || y == cy - 1) c = wv_lerp(c, st->centre, 0.55);
            if (in_loop) c = wv_lerp(c, st->loop_fill, 0.22);
            px[(size_t)y * (size_t)stride + (size_t)x] = c;
        }
        if (loop_on && (x == la || x == lb)) for (int y = 0; y < WAVE_H; y++) px[(size_t)y * (size_t)stride + (size_t)x] = wv_lerp(px[(size_t)y * (size_t)stride + (size_t)x], st->loop_line, 0.85);
    }
    for (int i = 0; i < nmarks; i++) {                      /* a marker: a thin line under a small flag */
        int x = wave_x(marks[i].t, dur);
        for (int y = 6; y < WAVE_H; y++) {
            uint32_t *p = &px[(size_t)y * (size_t)stride + (size_t)x];
            *p = wv_lerp(*p, marks[i].color, 0.55);
        }
        for (int fy = 0; fy < 6; fy++)
            for (int fx = 0; fx <= 5 - fy; fx++) {         /* a right-angled flag, pointing right */
                int xx = x + fx;
                if (xx < WAVE_W) px[(size_t)fy * (size_t)stride + (size_t)xx] = marks[i].color;
            }
    }
    for (int dx = -2; dx <= 2; dx++) {                      /* the playhead: a bright line with a soft edge, and a mark at both ends */
        int x = playx + dx;
        if (x < 0 || x >= WAVE_W) continue;
        double a = dx == 0 ? 1.0 : (abs(dx) == 1 ? 0.55 : 0.18);
        for (int y = 0; y < WAVE_H; y++) {
            uint32_t *p = &px[(size_t)y * (size_t)stride + (size_t)x];
            *p = wv_lerp(*p, st->playhead, a);
        }
    }
    for (int k = 0; k < 5; k++)
        for (int dx = -(4 - k); dx <= 4 - k; dx++) {
            int x = playx + dx;
            if (x < 0 || x >= WAVE_W) continue;
            px[(size_t)k * (size_t)stride + (size_t)x] = st->playhead;
            px[(size_t)(WAVE_H - 1 - k) * (size_t)stride + (size_t)x] = st->playhead;
        }
}

#endif /* MUSICUE_WAVE_H */

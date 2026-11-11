/* r3i6nos Rec demo for native apps (C, SDL2): docs/DEMO-RECORDINGS.md. MIT, like the rest of theme/.
 *
 * A port's Rec demo runs its real app through a scripted tour and saves, in /roms/<App>/Demo/: numbered PNG screenshots of what the
 * app drew, demo-<YYYYMMDD-HHMMSS>.mp4 (the same frames and the app's own audio), a copy of the demo file and rec-demo.txt (the log, in the device
 * report's grammar). This header is the recorder; the tour is the port's.
 *
 *   R3Demo d;
 *   r3_demo_open(&d, "/roms/TinyNTT/Demo", "TinyNTT", "0.1.0", "r36s-tinyntt", "Files > Rec demo", "TinyNTT", 44100);
 *   // for every 1/30 s of the tour, in this order (the app's clock is stepped by the tour, not read):
 *   r3_demo_build(&d, "v0.1.0 a1b2c3d 2026-11-11");  // right after open
 *   r3_demo_audio(&d, samples, 44100 / 30);      // stereo float, exactly rate / R3_DEMO_FPS frames
 *   r3_demo_frame(&d, &screen);                  // the screen as the app composed it
 *   r3_demo_shot(&d, &screen, "song-playing");   // 01-song-playing.png, numbered in order
 *   r3_demo_caption(&d, "loop-recording", "Fn arms, Start counts a bar in");
 *   r3_demo_finish(&d, "/path/to/demo-file.ntt");   // the copy, the encode, the summary, "end"
 *
 * Frames are kept as the 64 x 24 cells (a run of equal screens is one record), the audio as 16-bit WAV, both in Demo/ while the
 * tour runs; the encode renders each cell screen to 640 x 480 and pipes it to ffmpeg (libx264 and AAC) with the WAV. The recorder
 * needs ffmpeg only at the end: without it the log says FAIL, the two files stay, and `r3_demo_encode(dir)` (the port's
 * --encode-demo) makes the video later on a computer that has it. R3I6NOS_FFMPEG names another ffmpeg (tests, a port's own build).
 *
 * Include r3i6nos_ansiapps.h and r3i6nos_report.h first. Define the POSIX/GNU feature macros before any system header. */
#ifndef R3I6NOS_DEMO_H
#define R3I6NOS_DEMO_H

#include <dirent.h>
#include <signal.h>
#include <time.h>
#include "r3i6nos_toast.h"

#define R3_DEMO_FPS 30

typedef struct {
    char dir[PATH_MAX];
    R3Report log;
    FILE *frames, *wav;
    char frames_path[PATH_MAX + 32], wav_path[PATH_MAX + 32];
    int rate;
    long n_frames, n_audio;         /* video frames and audio frames written */
    int n_shots;
    R3Screen last;
    uint32_t run;                   /* how many frames the last screen has been showing, not written yet */
    int have_last, err;
    double t0;
    char stamp[20];                 /* YYYYMMDD-HHMMSS, when the recording started: names the kept files and the video */
    void (*on_progress)(void *ctx, int percent);   /* optional: 0-100 of the encode, while demo-<stamp>.mp4 is made */
    void *progress_ctx;
    int cancelled;                  /* the tour was cancelled: finish keeps the files, logs FAIL and does not encode */
    R3Toast watch;                  /* held: it only collects the hot-plug failures that arrive while recording (docs/HOTPLUG.md) */
    int n_failures;
    char failure[4][96];            /* what failed, for the log and for the screen after the recording is closed */
} R3Demo;

/* ---- PNG: a screen as 4-bit or 8-bit indexed colour (16 VGA colours, so 150 KB), stored, no library -------------------------- */

static inline uint32_t r3_demo_crc(uint32_t crc, const unsigned char *p, size_t n) {
    static uint32_t T[256];
    static int init;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            T[i] = c;
        }
        init = 1;
    }
    crc = ~crc;
    while (n--) crc = T[(crc ^ *p++) & 255] ^ (crc >> 8);
    return ~crc;
}

static inline void r3_demo_be32(unsigned char *o, uint32_t v) { o[0] = (unsigned char)(v >> 24); o[1] = (unsigned char)(v >> 16); o[2] = (unsigned char)(v >> 8); o[3] = (unsigned char)v; }

static inline int r3_demo_chunk(FILE *f, const char *type, const unsigned char *data, size_t n) {
    unsigned char h[8], c[4];
    r3_demo_be32(h, (uint32_t)n);
    memcpy(h + 4, type, 4);
    uint32_t crc = r3_demo_crc(0, h + 4, 4);
    if (n) crc = r3_demo_crc(crc, data, n);
    r3_demo_be32(c, crc);
    return fwrite(h, 1, 8, f) == 8 && (!n || fwrite(data, 1, n, f) == n) && fwrite(c, 1, 4, f) == 4;
}

/* The screen as a PNG at `path` (written whole to a temporary name, then renamed). 0, or -1. */
static inline int r3_demo_png(const char *path, const R3Screen *s) {
    size_t npx = (size_t)R3_W * R3_H;
    uint32_t *px = malloc(npx * sizeof *px);
    unsigned char *idx = malloc(npx);
    if (!px || !idx) { free(px); free(idx); return -1; }
    r3_render(s, px);
    uint32_t pal[256];
    int np = 0, last = 0, paletted = 1;
    for (size_t i = 0; i < npx && paletted; i++) {
        uint32_t c = px[i] & 0xFFFFFFu;
        int j;
        if (np && pal[last] == c) j = last;
        else {
            for (j = 0; j < np && pal[j] != c; j++) {}
            if (j == np) { if (np == 256) { paletted = 0; break; } pal[np++] = c; }
        }
        idx[i] = (unsigned char)j;
        last = j;
    }
    int depth = np <= 16 ? 4 : 8;
    size_t row = paletted ? (depth == 4 ? (R3_W + 1) / 2 : R3_W) : (size_t)R3_W * 3;
    size_t rawn = (row + 1) * R3_H;
    unsigned char *raw = calloc(1, rawn);
    if (!raw) { free(px); free(idx); return -1; }
    for (int y = 0; y < R3_H; y++) {
        unsigned char *o = raw + (size_t)y * (row + 1);
        o[0] = 0; /* filter: none */
        o++;
        for (int x = 0; x < R3_W; x++) {
            size_t i = (size_t)y * R3_W + x;
            if (!paletted) { o[x * 3] = (unsigned char)(px[i] >> 16); o[x * 3 + 1] = (unsigned char)(px[i] >> 8); o[x * 3 + 2] = (unsigned char)px[i]; }
            else if (depth == 8) o[x] = idx[i];
            else o[x / 2] = (unsigned char)(o[x / 2] | (x % 2 ? idx[i] : idx[i] << 4));
        }
    }
    /* zlib, stored blocks */
    size_t blocks = (rawn + 65534) / 65535;
    size_t zn = 2 + rawn + blocks * 5 + 4;
    unsigned char *z = malloc(zn);
    if (!z) { free(px); free(idx); free(raw); return -1; }
    size_t zo = 0;
    z[zo++] = 0x78;
    z[zo++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < rawn; i++) { a = (a + raw[i]) % 65521u; b = (b + a) % 65521u; }
    for (size_t at = 0; at < rawn;) {
        size_t n = rawn - at > 65535 ? 65535 : rawn - at;
        z[zo++] = at + n == rawn ? 1 : 0;
        z[zo++] = (unsigned char)(n & 255); z[zo++] = (unsigned char)(n >> 8);
        z[zo++] = (unsigned char)(~n & 255); z[zo++] = (unsigned char)((~n >> 8) & 255);
        memcpy(z + zo, raw + at, n);
        zo += n;
        at += n;
    }
    r3_demo_be32(z + zo, (b << 16) | a);
    zo += 4;
    char tmp[PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    int ok = f != NULL;
    if (f) {
        static const unsigned char SIG[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
        unsigned char ihdr[13];
        r3_demo_be32(ihdr, R3_W);
        r3_demo_be32(ihdr + 4, R3_H);
        ihdr[8] = paletted ? (unsigned char)depth : 8;
        ihdr[9] = paletted ? 3 : 2;
        ihdr[10] = ihdr[11] = ihdr[12] = 0;
        ok = fwrite(SIG, 1, 8, f) == 8 && r3_demo_chunk(f, "IHDR", ihdr, 13);
        if (ok && paletted) {
            unsigned char p3[256 * 3];
            for (int i = 0; i < np; i++) { p3[i * 3] = (unsigned char)(pal[i] >> 16); p3[i * 3 + 1] = (unsigned char)(pal[i] >> 8); p3[i * 3 + 2] = (unsigned char)pal[i]; }
            ok = r3_demo_chunk(f, "PLTE", p3, (size_t)np * 3);
        }
        ok = ok && r3_demo_chunk(f, "IDAT", z, zo) && r3_demo_chunk(f, "IEND", NULL, 0);
        ok = (fflush(f) == 0) && ok;
        ok = (fclose(f) == 0) && ok;
    }
    free(px); free(idx); free(raw); free(z);
    if (!ok || rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

/* ---- the lock: while a recording runs, nothing else starts -------------------------------------------------------------------- */

/* $R3I6NOS_RUN (default /run/loveos)/demo/lock holds the pid of the recording. The menu and every helper refuse to start while it names a live process. */
static inline void r3_demo_lock_path(char *out, size_t cap) {
    const char *run = getenv("R3I6NOS_RUN");
    snprintf(out, cap, "%s/demo/lock", run && *run ? run : "/run/loveos");
}

/* 1 when taken, 0 when another recording holds it (a stale lock of a dead process is taken over). A folder that cannot be made is not a refusal: 1. */
static inline int r3_demo_lock(void) {
    char p[PATH_MAX], dir[PATH_MAX];
    r3_demo_lock_path(p, sizeof p);
    snprintf(dir, sizeof dir, "%s", p);
    *strrchr(dir, '/') = 0;
    if (r3_report_mkdirs(dir) != 0) return 1;
    FILE *f = fopen(p, "r");
    if (f) {
        long pid = 0;
        if (fscanf(f, "%ld", &pid) != 1) pid = 0;
        fclose(f);
        if (pid > 0 && pid != (long)getpid() && (kill((pid_t)pid, 0) == 0 || errno == EPERM)) return 0;
    }
    f = fopen(p, "w");
    if (f) { fprintf(f, "%ld\n", (long)getpid()); fclose(f); }
    {   /* the last run's progress and done are old news */
        char q[PATH_MAX + 16];
        snprintf(q, sizeof q, "%s/progress", dir); unlink(q);
        snprintf(q, sizeof q, "%s/done", dir); unlink(q);
    }
    return 1;
}

static inline void r3_demo_unlock(void) {
    char p[PATH_MAX];
    r3_demo_lock_path(p, sizeof p);
    unlink(p);
}

/* ---- the status files: the menu's Settings > Rec demos and SSH read them (docs/DEVICE-REPORTS.md, "Progress", the same three) ------- */

/* $R3I6NOS_RUN (default /run/loveos)/demo/{running,progress,done}. `running` is the app's name while it records; `progress` is
 * step=N, steps=100, doing=..., file=... whole each time (a temporary file renamed over it); `done` is exit=N, when it has ended.
 * A recorder that cannot write them (no /run on a Mac) goes on without. */
static inline void r3_demo_status_write(const char *name, const char *text) {
    char lock[PATH_MAX], dir[PATH_MAX], p[PATH_MAX + 32], t[PATH_MAX + 40];
    r3_demo_lock_path(lock, sizeof lock);
    snprintf(dir, sizeof dir, "%s", lock);
    *strrchr(dir, '/') = 0;
    snprintf(p, sizeof p, "%s/%s", dir, name);
    snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "w");
    if (!f) return;
    int ok = fputs(text, f) >= 0;
    ok = (fclose(f) == 0) && ok;
    if (!ok || rename(t, p) != 0) unlink(t);
}

/* A recording is under way: `app`'s name, how far (0-100), what it is doing, where its files will be. */
static inline void r3_demo_status(const char *app, int percent, const char *doing, const char *file) {
    char text[PATH_MAX + 400];
    snprintf(text, sizeof text, "%s\n", app);
    r3_demo_status_write("running", text);
    snprintf(text, sizeof text, "step=%d\nsteps=100\ndoing=%s\nfile=%s\n", percent < 0 ? 0 : (percent > 100 ? 100 : percent), doing ? doing : "", file ? file : "");
    r3_demo_status_write("progress", text);
}

/* The recording has ended: exit=N, and `running` is removed. Call it before r3_demo_unlock. */
static inline void r3_demo_status_done(int code) {
    char text[32], p[PATH_MAX], dir[PATH_MAX];
    snprintf(text, sizeof text, "exit=%d\n", code);
    r3_demo_status_write("done", text);
    r3_demo_lock_path(p, sizeof p);
    snprintf(dir, sizeof dir, "%s", p);
    *strrchr(dir, '/') = 0;
    snprintf(p, sizeof p, "%s/running", dir);
    unlink(p);
}

/* ---- the checklist: which hero features a run shows -------------------------------------------------------------------------- */

#define R3_CHECK_MAX 16
typedef struct {
    int n, sel;
    int on[R3_CHECK_MAX];
    const char *const *name, *const *title, *const *blurb;   /* the port's own tables: log names, what a person reads, a few words on each */
} R3Checklist;

/* Everything ticked, the first line selected. */
static inline void r3_checklist_init(R3Checklist *c, int n, const char *const *name, const char *const *title, const char *const *blurb) {
    memset(c, 0, sizeof *c);
    c->n = n > R3_CHECK_MAX ? R3_CHECK_MAX : n;
    c->name = name;
    c->title = title;
    c->blurb = blurb;
    for (int i = 0; i < c->n; i++) c->on[i] = 1;
}
static inline void r3_checklist_move(R3Checklist *c, int d) { c->sel = (c->sel + c->n + d) % c->n; }
static inline void r3_checklist_toggle(R3Checklist *c) { c->on[c->sel] = !c->on[c->sel]; }
static inline unsigned r3_checklist_mask(const R3Checklist *c) { unsigned m = 0; for (int i = 0; i < c->n; i++) if (c->on[i]) m |= 1u << i; return m; }

/* "all", or feature names separated by commas, as a mask: 0 when a name is not one (a port's `--features LIST`). */
static inline unsigned r3_checklist_parse(const R3Checklist *c, const char *list) {
    if (!strcmp(list, "all")) return (1u << c->n) - 1;
    unsigned mask = 0;
    char copy[256];
    snprintf(copy, sizeof copy, "%s", list);
    for (char *save = NULL, *t = strtok_r(copy, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
        int i = 0;
        while (i < c->n && strcmp(t, c->name[i])) i++;
        if (i == c->n) return 0;
        mask |= 1u << i;
    }
    return mask;
}

/* The dialog, at the theme's list-dialog look: "[x] Title   blurb" lines, then two lines of what Start does. Up to R3_CHECK_MAX lines in one
 * page: a port with more must page it (rule 5). */
static inline void r3_checklist_draw(R3Screen *s, const R3Checklist *c, int row) {
    static R3Item items[R3_CHECK_MAX];
    for (int i = 0; i < c->n; i++) {
        snprintf(items[i].label, sizeof items[i].label, "[%c] %s", c->on[i] ? 'x' : ' ', c->title[i]);
        snprintf(items[i].value, sizeof items[i].value, "%s", c->blurb[i]);
    }
    R3List l = {6, row, 52, c->n + 2, "Rec demo: what to show", "A: tick  Start: record  B: back", items, c->n, c->sel, {"", NULL}};
    r3_list_dialog(s, &l);
    r3_text_fit(s, 8, row + c->n + 3, "The video starts on the splash and the version.", 48, R3_ST(R3_ST_HINT));
    r3_text_fit(s, 8, row + c->n + 4, "The unit is locked until it is done.", 48, R3_ST(R3_ST_HINT));
}

/* ---- the recorder ------------------------------------------------------------------------------------------------------------ */

/* Deletes what an earlier run left in Demo/ (the screenshots, log, demo file and the kept frames and audio; NEVER a demo-*.mp4: every video stays). */
static inline void r3_demo_clean(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        const char *n = e->d_name;
        size_t len = strlen(n);
        int mine = (len > 4 && !strcmp(n + len - 4, ".png")) || (len > 8 && !strcmp(n + len - 8, ".png.tmp")) || (!strncmp(n, "demo-", 5) && (strstr(n, "-frames.r3f") || (len > 4 && !strcmp(n + len - 4, ".wav")))) ||
                   !strcmp(n, "demo-ffmpeg.log") || !strncmp(n, "demo-file.", 10) || !strncmp(n, "rec-demo.txt", 12);
        if (!mine) continue;
        char p[PATH_MAX + 300];
        snprintf(p, sizeof p, "%s/%s", dir, n);
        unlink(p);
    }
    closedir(d);
}

static inline void r3_demo_le(unsigned char *o, uint32_t v, int bytes) { for (int i = 0; i < bytes; i++) o[i] = (unsigned char)(v >> (8 * i)); }

static inline void r3_demo_wav_header(FILE *f, int rate, uint32_t frames) {
    unsigned char h[44];
    memcpy(h, "RIFF", 4); r3_demo_le(h + 4, 36 + frames * 4, 4);
    memcpy(h + 8, "WAVEfmt ", 8); r3_demo_le(h + 16, 16, 4); r3_demo_le(h + 20, 1, 2); r3_demo_le(h + 22, 2, 2);
    r3_demo_le(h + 24, (uint32_t)rate, 4); r3_demo_le(h + 28, (uint32_t)rate * 4, 4); r3_demo_le(h + 32, 4, 2); r3_demo_le(h + 34, 16, 2);
    memcpy(h + 36, "data", 4); r3_demo_le(h + 40, frames * 4, 4);
    fwrite(h, 1, 44, f);
}

/* Starts the recording in `dir` (made if missing; an earlier run's files are removed). `rate` must be a multiple of R3_DEMO_FPS.
 * 0, or -1 (errno set). */
static inline int r3_demo_open(R3Demo *d, const char *dir, const char *app, const char *version, const char *package, const char *how, const char *repo, int rate) {
    int quiet = d->log.quiet;
    memset(d, 0, sizeof *d);
    d->log.quiet = quiet;
    if (rate <= 0 || rate % R3_DEMO_FPS) { errno = EINVAL; return -1; }
    d->rate = rate;
    snprintf(d->dir, sizeof d->dir, "%s", dir);
    if (r3_report_mkdirs(dir) != 0) return -1;
    r3_demo_clean(dir);
    {
        time_t now = time(NULL);
        struct tm tmv;
        const char *fixed = getenv("R3I6NOS_DEMO_STAMP"); /* tests */
        if (fixed && strlen(fixed) == 15) snprintf(d->stamp, sizeof d->stamp, "%s", fixed);
        else { localtime_r(&now, &tmv); strftime(d->stamp, sizeof d->stamp, "%Y%m%d-%H%M%S", &tmv); }
    }
    snprintf(d->frames_path, sizeof d->frames_path, "%s/demo-%s-frames.r3f", dir, d->stamp);
    snprintf(d->wav_path, sizeof d->wav_path, "%s/demo-%s.wav", dir, d->stamp);
    d->frames = fopen(d->frames_path, "wb");
    d->wav = fopen(d->wav_path, "wb");
    if (!d->frames || !d->wav) {
        int e = errno;
        if (d->frames) fclose(d->frames);
        if (d->wav) fclose(d->wav);
        errno = e;
        return -1;
    }
    r3_demo_wav_header(d->wav, rate, 0);
    /* the log is opened in the app's folder as Demo/rec-demo.txt, so its "for:" line names the right path on the card */
    const char *slash = strrchr(dir, '/');
    char parent[PATH_MAX], file[300];
    snprintf(parent, sizeof parent, "%.*s", slash ? (int)(slash - dir) : 0, dir);
    snprintf(file, sizeof file, "%s/rec-demo.txt", slash ? slash + 1 : dir);
    if (slash && r3_report_open_named(&d->log, parent, file, app, version, package, how, repo) != 0) return -1;
    if (!slash && r3_report_open_named(&d->log, ".", file, app, version, package, how, repo) != 0) return -1;
    d->t0 = r3_report_ms();
    r3_toast_init(&d->watch);
    r3_toast_hold(&d->watch, 1);
    r3_report_section(&d->log, "recording");
    return 0;
}

/* The build that made this recording, "v0.1.0 a1b2c3d 2026-11-11" (version, short commit, commit date): into the log, which the splash shows too. */
static inline void r3_demo_build(R3Demo *d, const char *build) { r3_report_line(&d->log, R3_INFO, "build", "%s", build); }

static inline void r3_demo_flush_run(R3Demo *d) {
    if (!d->have_last || !d->run) return;
    if (fwrite(&d->run, sizeof d->run, 1, d->frames) != 1 || fwrite(&d->last, sizeof d->last, 1, d->frames) != 1) d->err = 1;
    d->run = 0;
}

/* One video frame (1/30 s) showing `s`. */
static inline void r3_demo_frame(R3Demo *d, const R3Screen *s) {
    if (d->have_last && !memcmp(&d->last, s, sizeof *s)) d->run++;
    else { r3_demo_flush_run(d); d->last = *s; d->have_last = 1; d->run = 1; }
    d->n_frames++;
}

/* `frames` of the app's audio (interleaved stereo float): rate / R3_DEMO_FPS of them with each video frame. */
static inline void r3_demo_audio(R3Demo *d, const float *stereo, size_t frames) {
    unsigned char buf[4096 * 4];
    for (size_t at = 0; at < frames;) {
        size_t n = frames - at > 4096 ? 4096 : frames - at;
        for (size_t i = 0; i < n * 2; i++) {
            float v = stereo[at * 2 + i];
            v = v > 1 ? 1 : (v < -1 ? -1 : v);
            int16_t q = (int16_t)(v * 32767.0f + (v < 0 ? -0.5f : 0.5f));
            buf[i * 2] = (unsigned char)(q & 255);
            buf[i * 2 + 1] = (unsigned char)((q >> 8) & 255);
        }
        if (fwrite(buf, 4, n, d->wav) != n) d->err = 1;
        at += n;
    }
    d->n_audio += (long)frames;
}

/* Call about every frame: 1 once a hot-plug failure (a USB device that would not start, a card pulled out in use, a critical battery) has arrived. The tour
 * must then stop: finish closes the recording, logs each failure as FAIL and does not encode. A plain change (headphones, a stick) is not a failure and is
 * ignored: a recording has no toasts. */
static inline int r3_demo_failed(R3Demo *d) {
    if (d->n_failures) return 1;
    r3_toast_poll(&d->watch, r3_report_ms());
    d->n_failures = r3_toast_errors(&d->watch, d->failure, 4);
    return d->n_failures > 0;
}

/* Seconds of the tour so far. */
static inline double r3_demo_seconds(const R3Demo *d) { return (double)d->n_frames / R3_DEMO_FPS; }

/* The next numbered screenshot, "NN-name.png". Returns 0, or -1. */
static inline int r3_demo_shot(R3Demo *d, const R3Screen *s, const char *name) {
    char clean[64], p[PATH_MAX + 96];
    size_t n = 0;
    for (const char *c = name; *c && n + 1 < sizeof clean; c++) clean[n++] = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-' ? *c : '-';
    clean[n] = 0;
    d->n_shots++;
    snprintf(p, sizeof p, "%s/%02d-%s.png", d->dir, d->n_shots, clean);
    int rc = r3_demo_png(p, s);
    char key[96];
    snprintf(key, sizeof key, "screenshot %02d-%s.png", d->n_shots, clean);
    if (rc == 0) r3_report_line(&d->log, R3_PASS, key, "at %.1f s", r3_demo_seconds(d));
    else r3_report_line(&d->log, R3_FAIL, key, "could not be written: %s", strerror(errno));
    return rc;
}

/* What the tour is showing now and which hero feature it belongs to, into the log (an INFO line stamped with the time). */
static inline void r3_demo_caption(R3Demo *d, const char *hero, const char *fmt, ...) {
    char text[300], key[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    snprintf(key, sizeof key, "%.1f s", r3_demo_seconds(d));
    r3_report_line(&d->log, R3_INFO, key, "[%s] %s", hero, text);
}

/* ---- the encode --------------------------------------------------------------------------------------------------------------- */

static inline int r3_demo_find_ffmpeg(char *out, size_t cap) {
    const char *e = getenv("R3I6NOS_FFMPEG");
    if (e && *e) { snprintf(out, cap, "%s", e); return access(e, X_OK) == 0; }
    const char *path = getenv("PATH");
    char list[4096];
    snprintf(list, sizeof list, "%s:/opt/homebrew/bin:/usr/local/bin:/usr/bin", path ? path : "");
    for (char *save = NULL, *dir = strtok_r(list, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        snprintf(out, cap, "%s/ffmpeg", dir);
        if (access(out, X_OK) == 0) return 1;
    }
    return 0;
}

/* `s` in single quotes for /bin/sh. */
static inline void r3_demo_quote(char *out, size_t cap, const char *s) {
    size_t n = 0;
    if (n + 1 < cap) out[n++] = '\'';
    for (; *s && n + 5 < cap; s++) {
        if (*s == '\'') { memcpy(out + n, "'\\''", 4); n += 4; }
        else out[n++] = *s;
    }
    if (n + 2 <= cap) out[n++] = '\'';
    out[n] = 0;
}

/* Makes dir/demo-<stamp>.mp4 from the newest dir/demo-<stamp>-frames.r3f and demo-<stamp>.wav. 0 when made; else -1 and `msg` says why. On success the two
 * files and the ffmpeg log are removed. Also the port's `--encode-demo DIR`, for a card whose unit has no ffmpeg. */
static inline int r3_demo_encode_p(const char *dir, char *msg, size_t cap, void (*on_progress)(void *, int), void *ctx) {
    char ff[PATH_MAX], qdir[PATH_MAX * 2], q1[PATH_MAX * 2], q2[PATH_MAX * 2], q3[PATH_MAX * 2], cmd[PATH_MAX * 8], p[PATH_MAX + 64], stamp[24] = "";
    {   /* the newest kept recording: the greatest demo-<stamp>-frames.r3f (the stamps sort) */
        DIR *dd = opendir(dir);
        struct dirent *e;
        while (dd && (e = readdir(dd))) {
            size_t l = strlen(e->d_name);
            if (l == 5 + 15 + 11 && !strncmp(e->d_name, "demo-", 5) && !strcmp(e->d_name + l - 11, "-frames.r3f") && strncmp(e->d_name + 5, stamp, 15) > 0) snprintf(stamp, sizeof stamp, "%.15s", e->d_name + 5);
        }
        if (dd) closedir(dd);
    }
    if (!r3_demo_find_ffmpeg(ff, sizeof ff)) { snprintf(msg, cap, "ffmpeg is not installed here (the frames and the .wav are kept in Demo/; run --encode-demo on a computer that has ffmpeg)"); return -1; }
    snprintf(p, sizeof p, "%s/demo-%s-frames.r3f", dir, stamp);
    FILE *fr = stamp[0] ? fopen(p, "rb") : NULL;
    if (!fr) { snprintf(msg, cap, "no demo-<time>-frames.r3f in %s", dir); return -1; }
    long records = 0;
    if (on_progress) { fseek(fr, 0, SEEK_END); records = ftell(fr) / (long)(sizeof(uint32_t) + sizeof(R3Screen)); rewind(fr); }
    r3_demo_quote(qdir, sizeof qdir, ff);
    snprintf(p, sizeof p, "%s/demo-%s.wav", dir, stamp);
    r3_demo_quote(q1, sizeof q1, p);
    snprintf(p, sizeof p, "%s/demo-%s.mp4", dir, stamp);
    r3_demo_quote(q2, sizeof q2, p);
    snprintf(p, sizeof p, "%s/demo-ffmpeg.log", dir);
    r3_demo_quote(q3, sizeof q3, p);
    snprintf(cmd, sizeof cmd,
             "%s -y -loglevel error -f rawvideo -pixel_format bgra -video_size %dx%d -framerate %d -i pipe:0 -i %s -c:v libx264 -preset veryfast -crf 18 "
             "-tune animation -pix_fmt yuv420p -c:a aac -b:a 128k -shortest -movflags +faststart %s 2>%s",
             qdir, R3_W, R3_H, R3_DEMO_FPS, q1, q2, q3);
    void (*old)(int) = signal(SIGPIPE, SIG_IGN);
    FILE *pipe = popen(cmd, "w");
    int ok = pipe != NULL;
    uint32_t *px = malloc((size_t)R3_W * R3_H * sizeof *px);
    R3Screen *s = malloc(sizeof *s);
    if (!px || !s) ok = 0;
    uint32_t run;
    long rec_at = 0;
    while (ok && fread(&run, sizeof run, 1, fr) == 1 && fread(s, sizeof *s, 1, fr) == 1) {
        if (on_progress && records && rec_at % 8 == 0) on_progress(ctx, (int)(rec_at * 100 / records));
        rec_at++;
        r3_render(s, px);
        for (uint32_t i = 0; i < run && ok; i++) ok = fwrite(px, sizeof *px, (size_t)R3_W * R3_H, pipe) == (size_t)R3_W * R3_H;
    }
    free(px);
    free(s);
    fclose(fr);
    int st = pipe ? pclose(pipe) : -1;
    signal(SIGPIPE, old);
    if (on_progress) on_progress(ctx, 100);
    snprintf(p, sizeof p, "%s/demo-%s.mp4", dir, stamp);
    struct stat sb;
    if (!ok || st != 0 || stat(p, &sb) != 0 || sb.st_size < 1000) {
        char line[200] = "";
        snprintf(p, sizeof p, "%s/demo-ffmpeg.log", dir);
        FILE *lf = fopen(p, "r");
        if (lf) { if (!fgets(line, sizeof line, lf)) line[0] = 0; fclose(lf); }
        line[strcspn(line, "\r\n")] = 0;
        snprintf(msg, cap, "ffmpeg failed (status %d): %s", st, line[0] ? line : "no message");
        return -1;
    }
    snprintf(msg, cap, "demo-%s.mp4, %ld bytes", stamp, (long)sb.st_size);
    snprintf(p, sizeof p, "%s/demo-%s-frames.r3f", dir, stamp); unlink(p);
    snprintf(p, sizeof p, "%s/demo-%s.wav", dir, stamp); unlink(p);
    snprintf(p, sizeof p, "%s/demo-ffmpeg.log", dir); unlink(p);
    return 0;
}

static inline int r3_demo_encode(const char *dir, char *msg, size_t cap) { return r3_demo_encode_p(dir, msg, cap, NULL, NULL); }

static inline int r3_demo_copy(const char *from, const char *to) {
    FILE *in = fopen(from, "rb"), *out = in ? fopen(to, "wb") : NULL;
    char buf[8192];
    size_t n;
    int ok = in && out;
    while (ok && (n = fread(buf, 1, sizeof buf, in)) > 0) ok = fwrite(buf, 1, n, out) == n;
    if (in) fclose(in);
    if (out) ok = (fclose(out) == 0) && ok;
    return ok ? 0 : -1;
}

/* The end of the tour: the demo file copied to Demo/demo-file.<ext> (NULL: none), the WAV's header finished, the video encoded, the
 * log's summary and "end". 0 when the video is there and the log is on the card. `audio` is 0 for an app that makes no sound (the
 * track is silent and the log says so). */
static inline int r3_demo_finish(R3Demo *d, const char *demo_file, int audio) {
    char msg[400], p[PATH_MAX + 300];
    r3_demo_flush_run(d);
    if (d->frames) { if (fclose(d->frames) != 0) d->err = 1; d->frames = NULL; }
    if (d->wav) {
        fflush(d->wav);
        rewind(d->wav);
        r3_demo_wav_header(d->wav, d->rate, (uint32_t)d->n_audio);
        if (fclose(d->wav) != 0) d->err = 1;
        d->wav = NULL;
    }
    r3_report_section(&d->log, "video");
    double secs = r3_demo_seconds(d);
    r3_report_line(&d->log, secs >= 30 && secs <= 90 ? R3_PASS : R3_FAIL, "length", "%.1f s, %ld frames at %d fps (goal 30 to 90 s)", secs, d->n_frames, R3_DEMO_FPS);
    r3_report_line(&d->log, d->n_audio == d->n_frames * (d->rate / R3_DEMO_FPS) ? R3_PASS : R3_FAIL, "audio in step", "%ld audio frames for %ld video frames (%d each)", d->n_audio,
                   d->n_frames, d->rate / R3_DEMO_FPS);
    if (audio) r3_report_line(&d->log, R3_INFO, "audio", "the app's own mix, %d Hz stereo, tapped before the sound device", d->rate);
    else r3_report_line(&d->log, R3_SKIP, "audio", "this app makes no sound: the track is silent");
    r3_report_line(&d->log, d->n_shots > 0 ? R3_PASS : R3_FAIL, "screenshots", "%d PNG files in Demo/", d->n_shots);
    if (demo_file) {
        const char *base = strrchr(demo_file, '/');
        base = base ? base + 1 : demo_file;
        const char *dot = strrchr(base, '.');
        snprintf(p, sizeof p, "%s/demo-file%s", d->dir, dot ? dot : "");
        if (r3_demo_copy(demo_file, p) == 0) r3_report_line(&d->log, R3_PASS, "demo file", "copied to Demo/demo-file%s", dot ? dot : "");
        else r3_report_line(&d->log, R3_FAIL, "demo file", "could not be copied: %s", strerror(errno));
    }
    r3_demo_failed(d);   /* one last look: a failure in the final moments counts */
    for (int i = 0; i < d->n_failures; i++) r3_report_line(&d->log, R3_FAIL, "hotplug failure", "%s", d->failure[i]);
    int enc = d->err || d->cancelled || d->n_failures ? -1 : r3_demo_encode_p(d->dir, msg, sizeof msg, d->on_progress, d->progress_ctx);
    if (d->n_failures) snprintf(msg, sizeof msg, "stopped by a failure: %s (the files so far are kept, no video was made)", d->failure[0]);
    else if (d->cancelled) snprintf(msg, sizeof msg, "cancelled: the files so far are kept, no video was made");
    else if (d->err) snprintf(msg, sizeof msg, "a write failed during the tour (is the card full?)");
    if (enc == 0) r3_report_line(&d->log, R3_PASS, "video", "%s, H.264 and AAC, %dx%d", msg, R3_W, R3_H);
    else r3_report_line(&d->log, R3_FAIL, d->cancelled ? "cancelled" : "video", "%s", msg);
    r3_report_line(&d->log, R3_INFO, "took", "%.1f s to record and encode (on this machine)", (r3_report_ms() - d->t0) / 1000.0);
    int closed = r3_report_close(&d->log);
    return enc == 0 && closed == 0 ? 0 : -1;
}

#endif /* R3I6NOS_DEMO_H */

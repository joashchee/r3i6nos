/* MusiCue: markers and synced cues on a song you have, for the R36S (native C and SDL2, MIT).
 *
 * The R36S port of the maintainer's web app Cue, rebuilt around a file on the card instead of a YouTube player: docs/MUSICUE.md. A song is an audio
 * file in <app data>/media/ (yours, or one the sync helper fetched); its markers and cues come from Cue's own files (<videoId>.json and .srt, or a
 * .cuebundle) in <app data>/cues/. The whole song is decoded into RAM, so a jump is instant and an A/B loop has no gap.
 *
 * The screen is the r3i6nos-ansiapps text mode (64x24 cells of 10x20 pixels) in TRUE COLOUR, with one exception, said here as docs/R3I6NOS-ANSIAPPS-THEME.md
 * asks of an app that leaves the rules: rows 1-6 (640x120 pixels) are the WAVEFORM of the whole song, drawn in pixels, with a moving playhead. Everything
 * else is cells: palette slots 32-255 hold gradients and shades made from the chosen theme (bars, marker colours, the fading cue list), every text cell
 * on a gradient takes black or white by contrast, so every pair passes 4.5:1 (--selftest checks all of them for every theme).
 *
 * `--selftest` runs the checks and prints one R3I6NOS_SMOKE line; `--screenshot out.bmp [--screen library|player|menu] [--theme NAME]` draws a screen
 * with a synthetic demo song and no files; `--make-demo DIR` writes that song and its cue files; `--version`.
 * The R36S's rules still open for this app (device report with a power section, Rec demo, hot-plug toast wiring in the sync job): ROADMAP section 36.
 * Nothing here has run on a real R36S. */

#define _GNU_SOURCE
#define VERSION "0.1"

/* GCC warns that a path built from two long parts could be cut at PATH_MAX. Every such snprintf here feeds fopen, stat or opendir, and a cut path
 * simply fails to open (nothing is ever written to a different file by it), so the warning is silenced for that one case. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "r3i6nos_ansiapps.h"
#include "r3i6nos_themes.h"
#include "r3i6nos_toast.h"
#include "r3i6nos_appdata.h"
#include "cuedata.h"
#include "audio.h"
#include "wave.h"

/* ---- colours: a palette made from the theme ---------------------------------------------------------------------------------------------- */

enum { RAMP = 32, P_BAR0 = 32, P_ACC0 = 64, P_HUE0 = 96, P_HUED0 = 128, P_NEU0 = 160, P_BLACK = 240, P_WHITE = 241 };

static int g_light;                 /* the theme's page is light */

static uint32_t rgb_of(int slot) { return r3_pal[slot & 255] & 0xFFFFFFu; }

static uint32_t mix(uint32_t a, uint32_t b, double t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) out |= (uint32_t)((double)(a >> sh & 255) * (1 - t) + (double)(b >> sh & 255) * t + 0.5) << sh;
    return out;
}

static uint32_t hsv(double h, double s, double v) {
    double r, g, b;
    int i = (int)(h / 60.0) % 6;
    double f = h / 60.0 - (int)(h / 60.0), p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch (i) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return (uint32_t)(r * 255 + 0.5) << 16 | (uint32_t)(g * 255 + 0.5) << 8 | (uint32_t)(b * 255 + 0.5);
}

/* Build the app's own slots from the theme in the palette now (look is the theme's "dark", "light" or "mono"): bar and accent ramps, 32 marker hues (bright and dim), a text-to-page fade whose every
 * step still has 4.5:1 against the page, and pure black and white for text on gradients. */
static void app_palette(const char *look) {
    uint32_t page = rgb_of(R3_BLACK), text = rgb_of(R3_LIGHT_GRAY), bar = rgb_of(R3_BLUE), acc = rgb_of(R3_CYAN);
    g_light = r3_contrast_rgb(0xFFFFFF, page) < r3_contrast_rgb(0x000000, page);
    for (int i = 0; i < RAMP; i++) {
        double t = i / (double)(RAMP - 1);
        r3_pal_set(P_BAR0 + i, mix(bar, mix(bar, g_light ? 0xFFFFFF : 0x000000, 0.45), t));
        r3_pal_set(P_ACC0 + i, mix(acc, mix(acc, bar, 0.8), t));
        double hue = i * 360.0 / RAMP;
        uint32_t bright = hsv(hue, g_light ? 0.70 : 0.55, g_light ? 0.62 : 0.92);
        if (look && !strcmp(look, "mono")) bright = mix(page, text, 0.30 + 0.70 * i / (RAMP - 1));   /* a one-colour theme: sections differ by brightness, not hue */
        r3_pal_set(P_HUE0 + i, bright);
        r3_pal_set(P_HUED0 + i, mix(bright, page, g_light ? 0.55 : 0.62));
    }
    double lo = 0, hi = 1;                          /* the largest fade of the text towards the page that still passes 4.6:1 against the page */
    for (int k = 0; k < 20; k++) {
        double mid = (lo + hi) / 2;
        if (r3_contrast_rgb(mix(text, page, mid), page) >= 4.6) lo = mid; else hi = mid;
    }
    for (int i = 0; i < RAMP; i++) r3_pal_set(P_NEU0 + i, mix(text, page, lo * i / (RAMP - 1)));
    r3_pal_set(P_BLACK, 0x000000);
    r3_pal_set(P_WHITE, 0xFFFFFF);
}

/* The slot (black or white) that reads best on a background slot: always at least 4.58:1. */
static int pick_fg(int bg) { return r3_contrast_pal(P_WHITE, bg) >= r3_contrast_pal(P_BLACK, bg) ? P_WHITE : P_BLACK; }

static void put_auto(R3Screen *s, int col, int row, int ch, int bg) {      /* a cell on a gradient: its own text colour by contrast */
    if (bg < 0) bg = s->bg[row * R3_COLS + col];
    r3_put(s, col, row, ch, pick_fg(bg), bg);
}

/* Text from col that keeps each cell's background and picks its colour by contrast (fg < 0), or uses fg. Returns the next column. */
static int gtext(R3Screen *s, int col, int row, const char *str, int max, int fg) {
    unsigned char g[256];
    int n = r3_fit(str, max, g);
    for (int i = 0; i < n && col + i < R3_COLS; i++) {
        int bg = s->bg[row * R3_COLS + col + i];
        r3_put(s, col + i, row, g[i], fg >= 0 ? fg : pick_fg(bg), -1);
    }
    return col + n;
}

/* gtext for a line that may be too long: it scrolls (the theme's marquee, KlamANSI's method) in colours chosen as gtext does. Returns the next column. */
static int gscroll(R3Screen *s, int col, int row, const char *str, int max, int fg) {
    unsigned char g[256], m[256];
    if (max > 255) max = 255;
    int n = r3_marquee_cells(col, row, str, max, g, m);
    for (int i = 0; i < n && col + i < R3_COLS; i++) {
        int bg = s->bg[row * R3_COLS + col + i];
        r3_put(s, col + i, row, g[i], fg >= 0 ? fg : pick_fg(bg), -1);
    }
    return col + n;
}

/* A horizontal gradient of ramp slots under cells (the characters stay). */
static void grad_h(R3Screen *s, int col, int row, int w, int ramp0, int from, int to) {
    for (int i = 0; i < w; i++) {
        int k = w > 1 ? from + (to - from) * i / (w - 1) : from;
        if (k < 0) k = 0;
        if (k > RAMP - 1) k = RAMP - 1;
        int bg = ramp0 + k;
        if (col + i < R3_COLS) { int c = s->ch[row * R3_COLS + col + i]; r3_put(s, col + i, row, c, pick_fg(bg), bg); }
    }
}

/* The hue of a marker's label: the same name (without its number) always has the same colour, so a song's structure reads at a glance. */
static int hue_of(const char *label) {
    char norm[64];
    size_t n = 0;
    for (const char *p = label; *p && n < sizeof norm - 1; p++) {
        if (isdigit((unsigned char)*p)) continue;
        norm[n++] = (char)tolower((unsigned char)*p);
    }
    while (n && norm[n - 1] == ' ') n--;
    norm[n] = 0;
    static const struct { const char *name; int hue; } known[] = {
        {"intro", 11}, {"verse", 19}, {"pre-chorus", 15}, {"prechorus", 15}, {"pre chorus", 15}, {"chorus", 2}, {"bridge", 25}, {"outro", 11},
        {"end", 11}, {"solo", 4}, {"instrumental", 28}, {"interlude", 28}, {"tag", 7}, {"hook", 2}, {"refrain", 2}, {"drop", 0}, {"break", 23}};
    for (size_t i = 0; i < sizeof known / sizeof *known; i++) if (!strcmp(norm, known[i].name)) return known[i].hue;
    unsigned h = 5381;
    for (size_t i = 0; i < n; i++) h = h * 33 + (unsigned char)norm[i];
    return (int)(h % RAMP);
}

/* ---- the songs ----------------------------------------------------------------------------------------------------------------------------- */

typedef struct { char path[PATH_MAX]; char name[96]; char id[16]; int has_cues; int nmarkers; } Song;

enum { M_LIBRARY, M_PLAYER, M_MENU, M_JOB, M_NOTE, M_ID };

typedef struct { char label[48]; char value[40]; int id; } MItem;
typedef struct { char title[40]; MItem it[14]; int n, sel; } Menu;

typedef struct {
    char data[PATH_MAX];                    /* <app data>: media/, cues/, inbox/, export/, settings.conf */
    int theme, any_license;
    Song *songs; int nsongs, lsel;
    int mode, back_mode;
    /* the open song */
    int open;                               /* index into songs, or -1 */
    Track track; Engine eng; WavePeaks peaks; CueSet cues;
    double offset;                          /* seconds the audio runs after the cue data's time */
    int msel;                               /* the marker the cursor is on */
    int loop_state; double loop_a, loop_b;  /* 0 none, 1 A set, 2 A and B (looping) */
    int next_label;
    int demo;                               /* the synthetic demo song (no files) */
    Menu menu;
    char reply[160]; int reply_style;
    int quit, select_held, select_used;
    /* the sync job */
    pid_t job_pid; int job_fd; char job_buf[512]; int job_len; char job_lines[6][96]; int job_nlines; double job_frac; int job_done;
    char idbuf[12]; int idpos;              /* Start > Add: the 11 characters of a YouTube id, 0 for an empty slot */
    char job_want[16];                      /* a sync for one id: open that song when it is done */
    char note[3][72];                       /* a message box (M_NOTE) */
    unsigned wave_sig;
} App;

static void say(App *a, int style, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void say(App *a, int style, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->reply, sizeof a->reply, fmt, ap);
    va_end(ap);
    a->reply_style = style;
}

static void mkdirs(const char *path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(tmp, 0777); *p = '/'; }
    mkdir(tmp, 0777);
}

static void sub_path(const App *a, const char *sub, const char *file, char *out, size_t cap) {
    if (file && *file) snprintf(out, cap, "%s/%s/%s", a->data, sub, file);
    else snprintf(out, cap, "%s/%s", a->data, sub);
}

static int has_ext(const char *name, const char *const *exts) {
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    for (; *exts; exts++) if (!strcasecmp(dot + 1, *exts)) return 1;
    return 0;
}
static const char *const AUDIO_EXT[] = {"m4a", "mp3", "ogg", "opus", "flac", "wav", "aac", "wma", "mka", "webm", "mp4", NULL};

/* A video id in a file name, the way yt-dlp writes it: "Title [dQw4w9WgXcQ].m4a". Else a stable local id from the name. */
static void song_id(const char *file, char *out, size_t cap) {
    const char *lb = strrchr(file, '['), *rb = lb ? strchr(lb, ']') : NULL;
    if (lb && rb && rb - lb - 1 >= 6 && rb - lb - 1 <= 15) {
        char id[16];
        snprintf(id, sizeof id, "%.*s", (int)(rb - lb - 1), lb + 1);
        if (cue_videoid_ok(id)) { snprintf(out, cap, "%s", id); return; }
    }
    unsigned h = 2166136261u;
    for (const char *p = file; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
    snprintf(out, cap, "local-%08x", h);
}

static int song_cmp(const void *x, const void *y) { return strcasecmp(((const Song *)x)->name, ((const Song *)y)->name); }

static void scan_songs(App *a) {
    free(a->songs);
    a->songs = NULL; a->nsongs = 0;
    char dir[PATH_MAX];
    sub_path(a, "media", NULL, dir, sizeof dir);
    DIR *d = opendir(dir);
    if (!d) return;
    int cap = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || !has_ext(e->d_name, AUDIO_EXT)) continue;
        if (a->nsongs == cap) { cap = cap ? cap * 2 : 16; Song *ns = (Song *)realloc(a->songs, (size_t)cap * sizeof *ns); if (!ns) break; a->songs = ns; }
        Song *s = &a->songs[a->nsongs++];
        memset(s, 0, sizeof *s);
        snprintf(s->path, sizeof s->path, "%s/%s", dir, e->d_name);
        snprintf(s->name, sizeof s->name, "%s", e->d_name);
        char *dot = strrchr(s->name, '.');
        if (dot) *dot = 0;
        song_id(e->d_name, s->id, sizeof s->id);
        char cp[PATH_MAX];
        snprintf(cp, sizeof cp, "%s/cues/%s.json", a->data, s->id);
        s->has_cues = access(cp, R_OK) == 0;
    }
    closedir(d);
    if (a->nsongs > 1) qsort(a->songs, (size_t)a->nsongs, sizeof *a->songs, song_cmp);
}

/* ---- settings ------------------------------------------------------------------------------------------------------------------------------ */

static void settings_path(const App *a, char *out, size_t cap) {
    const char *o = getenv("R3I6NOS_MUSICUE_CONF");
    if (o && *o) snprintf(out, cap, "%s", o); else snprintf(out, cap, "%s/settings.conf", a->data);
}

static void load_settings(App *a) {
    char p[PATH_MAX];
    settings_path(a, p, sizeof p);
    char *t = cue_read_file(p, NULL);
    if (!t) return;
    for (char *line = strtok(t, "\n"); line; line = strtok(NULL, "\n")) {
        if (!strncmp(line, "theme=", 6)) { int i = r3_theme_find(line + 6); if (i >= 0) a->theme = i; }
        else if (!strncmp(line, "any_license=", 12)) a->any_license = atoi(line + 12) ? 1 : 0;
    }
    free(t);
}

static void save_settings(const App *a) {
    char p[PATH_MAX], buf[256];
    settings_path(a, p, sizeof p);
    snprintf(buf, sizeof buf, "theme=%s\nany_license=%d\n", r3_themes[a->theme].name, a->any_license);
    cue_write_file(p, buf, strlen(buf));
}

static void apply_theme(App *a) {
    r3_theme_apply(a->theme);
    app_palette(r3_themes[a->theme].look);
}

/* ---- the open song ------------------------------------------------------------------------------------------------------------------------- */

static void fmt_time(double t, char *out, size_t cap) {
    if (t < 0) t = 0;
    int ds = (int)(t * 10 + 0.5);
    snprintf(out, cap, "%d:%02d.%d", ds / 600, ds / 10 % 60, ds % 10);
}

static void fmt_time_s(double t, char *out, size_t cap) {
    int s = (int)(t + 0.5);
    snprintf(out, cap, "%d:%02d", s / 60, s % 60);
}

static void cue_path(const App *a, const char *id, const char *ext, char *out, size_t cap) {
    snprintf(out, cap, "%s/cues/%s.%s", a->data, id, ext);
}

static void close_song(App *a) {
    if (a->open < 0 && !a->demo) return;
    engine_play(&a->eng, 0);
    engine_close(&a->eng);
    track_free(&a->track);
    cue_free(&a->cues);
    memset(&a->peaks, 0, sizeof a->peaks);
    a->open = -1;
    a->demo = 0;
    a->loop_state = 0;
}

static int save_cues(App *a) {
    if (a->open < 0 || !a->cues.dirty) return 0;
    char dir[PATH_MAX], path[PATH_MAX];
    sub_path(a, "cues", NULL, dir, sizeof dir);
    mkdirs(dir);
    cue_path(a, a->songs[a->open].id, "json", path, sizeof path);
    CueBuf b = {0};
    cue_write_version(&a->cues, &b, time(NULL));
    int rc = b.bad ? -1 : cue_write_file(path, b.p, b.n);
    free(b.p);
    if (rc == 0) { a->cues.dirty = 0; a->songs[a->open].has_cues = 1; }
    return rc;
}

static int load_progress(void *ud, double secs) { (void)ud; (void)secs; SDL_PumpEvents(); return 0; }

static void set_loop_engine(App *a) {
    if (a->loop_state == 2) engine_loop_set(&a->eng, a->loop_a, a->loop_b); else engine_loop_clear(&a->eng);
}

static double dur_of(const App *a) { return a->track.pcm ? track_seconds(&a->track) : 0; }

/* Open song i: decode it, read its cue files. Returns 1, or 0 with the reason in the reply row. */
static int open_song(App *a, int i) {
    close_song(a);
    Song *s = &a->songs[i];
    char err[160] = "";
    if (!track_load(&a->track, s->path, TRACK_MAX_SECONDS, load_progress, a, err, sizeof err)) {
        say(a, R3_ST_ERROR, "Cannot play %s: %s", s->name, err[0] ? err : "unknown error");
        return 0;
    }
    a->open = i;
    wave_peaks(a->track.pcm, a->track.frames, &a->peaks);
    cue_init(&a->cues);
    snprintf(a->cues.videoid, sizeof a->cues.videoid, "%s", s->id);
    char path[PATH_MAX];
    cue_path(a, s->id, "json", path, sizeof path);
    size_t n;
    char *t = cue_read_file(path, &n);
    char e2[96];
    if (t) { if (!cue_load_json(&a->cues, t, n, 1, e2)) say(a, R3_ST_WARN, "The cue file is damaged: %s", e2); free(t); }
    if (!a->cues.nc) {                       /* a markers-only file (Cue's server .json): its .srt beside it holds the captions and the metadata */
        cue_path(a, s->id, "srt", path, sizeof path);
        t = cue_read_file(path, &n);
        if (t) { cue_load_srt(&a->cues, t, n, e2); free(t); }
    }
    a->cues.dirty = 0;
    a->offset = 0;
    cue_path(a, s->id, "offset", path, sizeof path);
    if ((t = cue_read_file(path, NULL))) { a->offset = atof(t); free(t); if (fabs(a->offset) > 30) a->offset = 0; }
    s->nmarkers = a->cues.nm;
    a->msel = 0;
    a->loop_state = 0;
    a->next_label = 0;
    engine_open(&a->eng, &a->track);
    if (a->track.truncated) say(a, R3_ST_WARN, "Only the first %d minutes are loaded.", TRACK_MAX_SECONDS / 60);
    else if (!a->cues.nm && !a->cues.nc) say(a, R3_ST_HINT, "No cues for this song yet: Select marks the playhead.");
    else say(a, R3_ST_HINT, "%d markers, %d cues. A plays, X jumps, L1/R1 step.", a->cues.nm, a->cues.nc);
    a->mode = M_PLAYER;
    return 1;
}

/* ---- the demo song: original, synthetic, no files ----------------------------------------------------------------------------------------- */

static uint32_t demo_rng(uint32_t *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }

/* 96 seconds at 100 bpm: intro, verse, pre-chorus, chorus, verse, chorus, bridge, outro, with a chord pad, a bass, a beat and a bit of hiss, louder in
 * the choruses. Made here so the screenshots, the tests and a Rec demo need no file and no one's music. */
static int demo_track_make(Track *t) {
    const double dur = 96.0;
    int64_t n = (int64_t)(dur * TRACK_RATE);
    int16_t *pcm = (int16_t *)calloc((size_t)n, 4);
    if (!pcm) return 0;
    static const double chord[4][3] = {{261.63, 329.63, 392.0}, {196.0, 246.94, 293.66}, {220.0, 261.63, 329.63}, {174.61, 220.0, 261.63}};   /* C G Am F */
    static const double bass[4] = {65.41, 49.0, 55.0, 43.65};
    static const double edges[] = {0, 8, 24, 32, 48, 64, 76, 88, 96};
    static const double level[] = {0.30, 0.55, 0.70, 1.00, 0.55, 1.00, 0.45, 0.35};
    uint32_t rng = 2463534242u;
    double beat = 60.0 / 100.0;
    for (int64_t i = 0; i < n; i++) {
        double sec = (double)i / TRACK_RATE;
        int sec_i = 0;
        while (sec_i < 7 && sec >= edges[sec_i + 1]) sec_i++;
        double lv = level[sec_i];
        if (sec_i == 7) lv *= 1.0 - (sec - 88.0) / 8.0 * 0.85;
        int bar = (int)(sec / (4 * beat));
        const double *ch = chord[bar % 4];
        double bt = fmod(sec, beat) / beat, bp = fmod(sec, 4 * beat) / (4 * beat);
        double pad = 0;
        for (int k = 0; k < 3; k++) pad += sin(2 * M_PI * ch[k] * sec) + 0.4 * sin(2 * M_PI * ch[k] * 2.003 * sec);
        double bs = sin(2 * M_PI * bass[bar % 4] * sec) * exp(-bt * 2.2);
        double kick = bt < 0.12 ? sin(2 * M_PI * (120.0 - 600.0 * bt) * bt) * (1 - bt / 0.12) : 0;
        double hat = ((int)(sec / (beat / 2)) % 2) ? ((double)(demo_rng(&rng) & 0xFFFF) / 32768.0 - 1.0) * exp(-fmod(sec, beat / 2) * 40) * 0.5 : 0;
        double tone = (0.16 * pad * (0.6 + 0.4 * (1 - bp)) + 0.55 * bs + 0.5 * kick * (sec_i == 0 ? 0 : 1) + hat) * lv * (sec_i == 0 ? 0.8 : 1.0);
        double l = tone + 0.03 * sin(2 * M_PI * 3.0 * sec) * lv, r = tone - 0.03 * sin(2 * M_PI * 3.0 * sec) * lv;
        pcm[i * 2] = (int16_t)fmax(-32000, fmin(32000, l * 15000));
        pcm[i * 2 + 1] = (int16_t)fmax(-32000, fmin(32000, r * 15000));
    }
    memset(t, 0, sizeof *t);
    t->pcm = pcm; t->frames = n;
    snprintf(t->via, sizeof t->via, "demo");
    return 1;
}

/* The demo song, made once a run and copied (the synthesis takes a moment, and the tests and the Demo song menu ask for it again and again). */
static int demo_track(Track *t) {
    static int16_t *cache;
    static int64_t frames;
    if (!cache) {
        Track made;
        if (!demo_track_make(&made)) return 0;
        cache = made.pcm;
        frames = made.frames;
    }
    int16_t *copy = (int16_t *)malloc((size_t)frames * 4);
    if (!copy) return 0;
    memcpy(copy, cache, (size_t)frames * 4);
    memset(t, 0, sizeof *t);
    t->pcm = copy;
    t->frames = frames;
    snprintf(t->via, sizeof t->via, "demo");
    return 1;
}

static void demo_cues(CueSet *c) {
    static const struct { double t; const char *l; } mk[] = {{0, "Intro"}, {8, "Verse 1"}, {24, "Pre-Chorus"}, {32, "Chorus"}, {48, "Verse 2"}, {64, "Chorus"}, {76, "Bridge"}, {88, "Outro"}};
    static const char *prog = "C   G   Am  F";
    cue_init(c);
    snprintf(c->videoid, sizeof c->videoid, "demo");
    for (size_t i = 0; i < sizeof mk / sizeof *mk; i++) cue_add_marker(c, mk[i].t, mk[i].l, 0);
    const double bar = 4 * 60.0 / 100.0;
    for (int b = 0; b * bar < 96; b++) {
        char txt[64];
        double st = b * bar;
        if (b % 4 == 0) snprintf(txt, sizeof txt, "%s", prog);
        else snprintf(txt, sizeof txt, "bar %d", b + 1);
        if (b % 4 == 0) cue_add_caption(c, st, 1, st + 4 * bar, txt);
        else if (b % 4 == 2 && st > 8 && st < 88) cue_add_caption(c, st, 1, st + 2 * bar, "(hold the chord)");
    }
    cue_meta_set(c, "key", "C");
    cue_meta_set(c, "tempo", "100");
    cue_meta_set(c, "sig", "4/4");
    cue_meta_set(c, "scripter", "MusiCue demo");
    cue_sort(c);
    c->dirty = 0;
}

static void open_demo(App *a) {
    close_song(a);
    demo_track(&a->track);
    wave_peaks(a->track.pcm, a->track.frames, &a->peaks);
    demo_cues(&a->cues);
    a->open = -1;
    a->demo = 1;
    a->offset = 0;
    a->msel = 0;
    a->loop_state = 0;
    engine_open(&a->eng, &a->track);
    a->mode = M_PLAYER;
}

/* ---- composing the screens ----------------------------------------------------------------------------------------------------------------- */

#define MARK_ROWS 10        /* markers a page: the box is 12 rows with its frame */

static int cur_marker(const App *a, double vt) {
    int cur = -1;
    for (int i = 0; i < a->cues.nm; i++) if (a->cues.m[i].time <= vt + 0.0005) cur = i; else break;
    return cur;
}

static int cur_caption(const App *a, double vt) {
    int cur = -1;
    for (int i = 0; i < a->cues.nc; i++) if (a->cues.c[i].start <= vt + 0.0005) cur = i; else break;
    return cur;
}

static uint32_t argb_of(int slot) { return 0xFF000000u | rgb_of(slot); }

static void wave_style(WaveStyle *st) {
    uint32_t page = rgb_of(R3_BLACK), hint = rgb_of(R3_LIGHT_CYAN), acc = rgb_of(R3_CYAN), blue = rgb_of(R3_BLUE);
    st->bg = 0xFF000000u | mix(page, blue, g_light ? 0.10 : 0.22);
    st->played_core = 0xFF000000u | mix(hint, 0xFFFFFF, g_light ? 0 : 0.15);
    st->played_edge = 0xFF000000u | acc;
    st->todo_core = 0xFF000000u | mix(mix(hint, acc, 0.5), page, 0.55);
    st->todo_edge = 0xFF000000u | mix(blue, page, 0.30);
    st->centre = 0xFF000000u | mix(acc, page, 0.35);
    st->playhead = g_light ? 0xFF101010u : 0xFFFFFFFFu;
    st->loop_fill = argb_of(R3_YELLOW);
    st->loop_line = argb_of(R3_YELLOW);
}

/* The marks the waveform draws, in the song's own time (the cue data's time plus the offset). Returns how many. */
static int wave_marks(const App *a, WaveMark *out, int cap) {
    int n = 0;
    for (int i = 0; i < a->cues.nm && n < cap; i++) {
        out[n].t = a->cues.m[i].time + a->offset;
        out[n].color = argb_of(P_HUE0 + hue_of(a->cues.m[i].label));
        n++;
    }
    return n;
}

static void compose_topbar(App *a, R3Screen *s, const char *left, const char *right) {
    r3_fill(s, 0, 0, R3_COLS, 1, 32, R3_WHITE, P_BAR0);
    grad_h(s, 0, 0, R3_COLS, P_BAR0, 0, RAMP - 1);
    gtext(s, 1, 0, left, R3_COLS - 2, -1);
    if (right) {
        int n = r3_width(right);
        gtext(s, R3_COLS - 1 - n, 0, right, n, -1);
    }
    (void)a;
}

static void compose_library(App *a, R3Screen *s) {
    r3_clear(s);
    compose_topbar(a, s, "MusiCue", VERSION);
    /* a banner: the name over a sweep of the marker hues, so the true-colour range is the first thing seen */
    for (int row = 1; row <= 3; row++)
        for (int c = 0; c < R3_COLS; c++) {
            int hue = (c * RAMP / R3_COLS + (row - 1) * 0) % RAMP;
            int bg = (row == 2 ? P_HUE0 : P_HUED0) + hue;
            r3_put(s, c, row, 32, pick_fg(bg), bg);
        }
    gtext(s, 2, 2, "M U S I C U E   markers and cues on the songs you have", 60, -1);
    char right[32] = "";
    if (a->nsongs) {
        int per = 11, page, pages;
        r3_list_window(a->nsongs, a->lsel, per, &page, &pages);
        if (pages > 1) snprintf(right, sizeof right, "Page %d/%d", page, pages);
        R3Box b = {1, "Songs", right[0] ? right : NULL, "A open   Start menu   B quit", 0};
        r3_box(s, 0, 4, R3_COLS, 13, &b);
        int first = (page - 1) * per;
        for (int i = first; i < a->nsongs && i < first + per; i++) {
            int y = 5 + i - first;
            if (i == a->lsel) { r3_fill(s, 1, y, R3_COLS - 2, 1, 32, R3_BLACK, P_ACC0); grad_h(s, 1, y, R3_COLS - 2, P_ACC0, 0, RAMP - 1); }
            char mark[24] = "";
            if (a->songs[i].has_cues) snprintf(mark, sizeof mark, "cues");
            int mw = r3_width(mark);
            int fgs = i == a->lsel ? -1 : R3_LIGHT_GRAY;
            put_auto(s, 2, y, 14, i == a->lsel ? s->bg[y * R3_COLS + 2] : R3_BLACK);
            if (i != a->lsel) r3_put(s, 2, y, 14, R3_LIGHT_CYAN, R3_BLACK);
            (i == a->lsel ? gscroll : gtext)(s, 4, y, a->songs[i].name, R3_COLS - 8 - mw - (mw ? 2 : 0), fgs);
            if (mw) gtext(s, R3_COLS - 2 - mw, y, mark, mw, i == a->lsel ? -1 : R3_LIGHT_GREEN);
        }
    } else {
        R3Box b = {1, "Songs", NULL, "Start menu   B quit", 0};
        r3_box(s, 0, 4, R3_COLS, 13, &b);
        r3_text_fit(s, 2, 6, "No songs yet.", 58, R3_ST(R3_ST_STRONG));
        r3_text_fit(s, 2, 8, "Put audio files in MusiCue/media/ on the card (m4a, mp3,", 58, R3_ST(R3_ST_BODY));
        r3_text_fit(s, 2, 9, "ogg, opus, flac, wav), and Cue's .cuebundle files in", 58, R3_ST(R3_ST_BODY));
        r3_text_fit(s, 2, 10, "MusiCue/inbox/. Start > Import reads them.", 58, R3_ST(R3_ST_BODY));
        r3_text_fit(s, 2, 12, "Start > Demo song plays a built-in one.", 58, R3_ST(R3_ST_HINT));
    }
    r3_fill(s, 0, 17, R3_COLS, 4, 32, R3_LIGHT_GRAY, R3_BLACK);
    r3_text_fit(s, 1, 18, a->nsongs ? "A song is decoded into memory when opened: jumps and loops are instant." : "", 62, R3_ST(R3_ST_BODY));
    R3Cmd cmd = {a->reply, a->reply_style, '>', "", 0, a->nsongs ? "Songs: media/ on the data card" : "Nothing in media/", "Start: menu", R3_ST_HINT};
    r3_command_line(s, &cmd);
}

static void menu_clear(Menu *m, const char *title) { memset(m, 0, sizeof *m); snprintf(m->title, sizeof m->title, "%s", title); }
static void menu_add(Menu *m, int id, const char *label, const char *value) {
    if (m->n >= 14) return;
    snprintf(m->it[m->n].label, sizeof m->it[m->n].label, "%s", label);
    snprintf(m->it[m->n].value, sizeof m->it[m->n].value, "%s", value ? value : "");
    m->it[m->n++].id = id;
}

static void compose_menu_box(const Menu *m, R3Screen *s, int top) {
    int w = 44, h = m->n + 2, col = (R3_COLS - w) / 2, row = top;
    R3Box b = {1, m->title, NULL, "A choose   Left/Right change   B close", 0};
    r3_box(s, col, row, w, h, &b);
    for (int i = 0; i < m->n; i++) {
        int y = row + 1 + i;
        if (i == m->sel) { r3_fill(s, col + 1, y, w - 2, 1, 32, R3_BLACK, P_ACC0); grad_h(s, col + 1, y, w - 2, P_ACC0, 0, RAMP - 1); }
        int vn = r3_width(m->it[i].value);
        gtext(s, col + 2, y, m->it[i].label, w - 5 - vn - (vn ? 1 : 0), i == m->sel ? -1 : R3_LIGHT_GRAY);
        if (vn) gtext(s, col + w - 2 - vn, y, m->it[i].value, vn, i == m->sel ? -1 : R3_LIGHT_CYAN);
    }
}

static void compose_player(App *a, R3Screen *s) {
    r3_clear(s);
    const double dur = dur_of(a), pos = engine_pos_sec(&a->eng), vt = pos - a->offset;
    const char *title = a->demo ? "Demo song" : (a->open >= 0 ? a->songs[a->open].name : "");
    char t1[24], t2[24], right[48];
    fmt_time_s(pos, t1, sizeof t1);
    fmt_time_s(dur, t2, sizeof t2);
    snprintf(right, sizeof right, "%s / %s", t1, t2);
    compose_topbar(a, s, "MusiCue", right);
    int tw = r3_width(right);
    gscroll(s, 11, 0, title, R3_COLS - 14 - tw, -1);

    /* rows 1-6: the waveform (pixels, drawn by the presenter); the cells under it are its colour, for anything that mirrors the text */
    int wbg = P_NEU0;
    (void)wbg;
    r3_fill(s, 0, 1, R3_COLS, 6, 32, R3_LIGHT_GRAY, R3_BLACK);

    /* row 7: the song's sections as a strip, one cell a tenth of the waveform's width: a section's colour comes from its name */
    int cm = cur_marker(a, vt);
    for (int c = 0; c < R3_COLS; c++) {
        double t = (c + 0.5) / R3_COLS * dur - a->offset;
        int k = cur_marker(a, t);
        int played = (c + 0.5) / R3_COLS * dur <= pos;
        int bg = k < 0 ? P_NEU0 + RAMP - 1 : (played ? P_HUE0 : P_HUED0) + hue_of(a->cues.m[k].label);
        if (k < 0) bg = R3_BLACK;
        r3_put(s, c, 7, 32, pick_fg(bg), bg);
    }
    for (int i = 0; i < a->cues.nm; i++) {            /* a section's name at its first cell, if it fits before the next marker */
        int c0 = (int)((a->cues.m[i].time + a->offset) / (dur > 0 ? dur : 1) * R3_COLS);
        int c1 = i + 1 < a->cues.nm ? (int)((a->cues.m[i + 1].time + a->offset) / dur * R3_COLS) : R3_COLS;
        if (c0 < 0 || c0 >= R3_COLS) continue;
        gtext(s, c0, 7, a->cues.m[i].label, c1 - c0 > 1 ? c1 - c0 - (c1 - c0 > 3) : 1, -1);
    }
    int px = (int)(pos / (dur > 0 ? dur : 1) * R3_COLS);
    if (px >= 0 && px < R3_COLS) { int bg = s->bg[7 * R3_COLS + px]; r3_put(s, px, 7, 31, pick_fg(bg), -1); (void)bg; }

    /* row 8: the transport */
    char ts[32], line[96];
    fmt_time(pos, ts, sizeof ts);
    r3_put(s, 1, 8, a->eng.playing ? 16 : 186, a->eng.playing ? R3_LIGHT_GREEN : R3_YELLOW, R3_BLACK);
    snprintf(line, sizeof line, "%s", ts);
    r3_text_fit(s, 3, 8, line, 10, R3_ST(R3_ST_STRONG));
    if (a->loop_state) {
        char la[16], lb[16];
        fmt_time(a->loop_a, la, sizeof la);
        if (a->loop_state == 2) fmt_time(a->loop_b, lb, sizeof lb); else snprintf(lb, sizeof lb, "...");
        snprintf(line, sizeof line, "%c A %s  B %s", a->loop_state == 2 ? 'L' : ' ', la, lb);
        r3_text_fit(s, 16, 8, line, 30, R3_ST(R3_ST_WARN));
    }
    if (a->offset != 0) { snprintf(line, sizeof line, "offset %+.2fs", a->offset); r3_text_right(s, 62, 8, line, R3_ST(R3_ST_HINT)); }

    /* rows 9-20, left: the markers, a page of ten, the cursor on a bar of the accent gradient, the marker playing now flagged */
    int per = MARK_ROWS, page, pages;
    r3_list_window(a->cues.nm, a->msel, per, &page, &pages);
    char pg[24] = "";
    if (pages > 1) snprintf(pg, sizeof pg, "Page %d/%d", page, pages);
    R3Box mb = {1, "Markers", pg[0] ? pg : NULL, NULL, 0};
    r3_box(s, 0, 9, 26, 12, &mb);
    if (!a->cues.nm) {
        r3_text_fit(s, 2, 11, "No markers yet.", 22, R3_ST(R3_ST_BODY));
        r3_text_fit(s, 2, 12, "Select adds one", 22, R3_ST(R3_ST_HINT));
        r3_text_fit(s, 2, 13, "at the playhead.", 22, R3_ST(R3_ST_HINT));
    }
    int first = (page - 1) * per;
    for (int i = first; i < a->cues.nm && i < first + per; i++) {
        int y = 10 + i - first, hue = hue_of(a->cues.m[i].label);
        char tm[16];
        fmt_time_s(a->cues.m[i].time, tm, sizeof tm);
        if (i == a->msel) { r3_fill(s, 1, y, 24, 1, 32, R3_BLACK, P_ACC0); grad_h(s, 1, y, 24, P_ACC0, 0, RAMP - 1); }
        r3_put(s, 1, y, i == cm ? 16 : 32, i == a->msel ? pick_fg(s->bg[y * R3_COLS + 1]) : R3_LIGHT_GREEN, -1);
        r3_put(s, 2, y, 221, i == a->msel ? pick_fg(s->bg[y * R3_COLS + 2]) : R3_LIGHT_GRAY, P_HUE0 + hue);
        r3_put(s, 2, y, 32, pick_fg(P_HUE0 + hue), P_HUE0 + hue);
        int tw2 = r3_width(tm);
        (i == a->msel ? gscroll : gtext)(s, 4, y, a->cues.m[i].label, 24 - 4 - tw2 - 1, i == a->msel ? -1 : (i == cm ? R3_WHITE : R3_LIGHT_GRAY));
        gtext(s, 24 - tw2, y, tm, tw2, i == a->msel ? -1 : R3_LIGHT_CYAN);
    }

    /* rows 9-20, right: the cues, the one playing on a gradient bar, those before it fading away above, those to come fading below */
    char meta[40] = "";
    int mn = 0;
    if (a->cues.meta.key[0]) mn += snprintf(meta + mn, sizeof meta - (size_t)mn, "Key %s", a->cues.meta.key);
    if (a->cues.meta.tempo[0]) mn += snprintf(meta + mn, sizeof meta - (size_t)mn, "%s%s bpm", mn ? "  " : "", a->cues.meta.tempo);
    if (a->cues.meta.sig[0]) mn += snprintf(meta + mn, sizeof meta - (size_t)mn, "%s%s", mn ? "  " : "", a->cues.meta.sig);
    R3Box cb = {1, "Cues", meta[0] ? meta : NULL, NULL, 0};
    r3_box(s, 26, 9, 38, 12, &cb);
    int ci = cur_caption(a, vt);
    if (!a->cues.nc) {
        r3_text_fit(s, 28, 11, "No cues for this song.", 34, R3_ST(R3_ST_BODY));
        r3_text_fit(s, 28, 12, "Import a Cue bundle, or fetch the cue", 34, R3_ST(R3_ST_HINT));
        r3_text_fit(s, 28, 13, "file from Start > Sync.", 34, R3_ST(R3_ST_HINT));
    } else {
        int rows = 10, before = 3;
        int base = ci - before;
        if (ci < 0) base = -before + 0;
        for (int r = 0; r < rows; r++) {
            int idx = base + r, y = 10 + r;
            if (idx < 0 || idx >= a->cues.nc) continue;
            int rel = idx - ci;                              /* negative: before the one playing */
            int fade = rel < 0 ? -rel : rel;
            if (ci >= 0 && rel == 0) {
                r3_fill(s, 27, y, 36, 1, 32, R3_BLACK, P_ACC0);
                grad_h(s, 27, y, 36, P_ACC0, 0, RAMP - 1);
                gscroll(s, 28, y, a->cues.c[idx].text, 34, -1);
            } else {
                int step = (fade * 5 > RAMP - 1) ? RAMP - 1 : fade * 5;
                if (ci < 0) step = idx * 4 < RAMP - 1 ? idx * 4 : RAMP - 1;
                gtext(s, 28, y, a->cues.c[idx].text, 34, P_NEU0 + step);
            }
        }
    }

    /* rows 21-23: the command line: what just happened, the keys, and where the next marker is */
    char status[64] = "";
    if (cm >= 0 && cm + 1 < a->cues.nm) snprintf(status, sizeof status, "%s, then %s in %.0fs", a->cues.m[cm].label, a->cues.m[cm + 1].label, a->cues.m[cm + 1].time - vt);
    else if (cm >= 0) snprintf(status, sizeof status, "%s", a->cues.m[cm].label);
    R3Cmd cmd = {a->reply, a->reply_style, a->eng.playing ? '>' : '|', "A play  X jump  Y loop  L1 R1 step  Sel mark  Start menu", 0, status,
                 a->eng.noaudio && !a->demo ? "no sound device" : "", R3_ST_WARN};
    r3_command_line(s, &cmd);
}

static void compose_job(App *a, R3Screen *s) {
    r3_clear(s);
    compose_topbar(a, s, "MusiCue", "Sync");
    R3Box b = {1, "Getting songs and cues", NULL, a->job_done ? "B close" : "B stop", 0};
    r3_box(s, 2, 4, 60, 14, &b);
    int full = (int)(56 * a->job_frac + 0.5);
    for (int i = 0; i < 56; i++) {
        if (i < full) { int bg = P_ACC0 + i * (RAMP - 1) / 55; r3_put(s, 4 + i, 6, 32, pick_fg(bg), bg); }
        else r3_put(s, 4 + i, 6, 176, R3_ST(R3_ST_HINT));
    }
    for (int i = 0; i < a->job_nlines && i < 6; i++) r3_text_scroll(s, 4, 8 + i, a->job_lines[i], 56, R3_ST(i == a->job_nlines - 1 ? R3_ST_STRONG : R3_ST_BODY));
    R3Cmd cmd = {a->reply, a->reply_style, '>', "", 0, "Start your own list in MusiCue/inbox/urls.txt", "", R3_ST_HINT};
    r3_command_line(s, &cmd);
}

static const char ID_ALPHA[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static void compose_id(App *a, R3Screen *s) {
    r3_clear(s);
    compose_topbar(a, s, "MusiCue", "Add");
    R3Box b = {1, "Add: YouTube ID", NULL, "A next/fetch   B cancel", 0};
    r3_box(s, 8, 5, 48, 13, &b);
    r3_text_fit(s, 10, 7, "The 11 characters after v= in a YouTube link.", 44, R3_ST(R3_ST_BODY));
    r3_text_fit(s, 10, 8, "Capitals and small letters are different.", 44, R3_ST(R3_ST_HINT));
    for (int i = 0; i < 11; i++) {
        int col = 15 + i * 3, ch = a->idbuf[i] ? a->idbuf[i] : '_';
        if (i == a->idpos) { int bg = P_ACC0 + RAMP / 2; r3_put(s, col, 11, ch, pick_fg(bg), bg); }
        else r3_put(s, col, 11, ch, R3_ST(a->idbuf[i] ? R3_ST_STRONG : R3_ST_HINT));
    }
    r3_text_fit(s, 10, 14, "Up/Down letter   L1/R1 jump 8   Left/Right move", 44, R3_ST(R3_ST_HINT));
    r3_text_fit(s, 10, 15, "Y erase   A next, and fetch on the last one", 44, R3_ST(R3_ST_HINT));
    r3_text_fit(s, 10, 16, "A keyboard types it.", 44, R3_ST(R3_ST_HINT));
    R3Cmd cmd = {a->reply, a->reply_style, '>', "", 0, "Fetches the cues and the audio, then opens it", "", R3_ST_HINT};
    r3_command_line(s, &cmd);
}

static void compose_note(App *a, R3Screen *s) {
    r3_clear(s);
    compose_topbar(a, s, "MusiCue", VERSION);
    R3Box b = {1, "MusiCue", NULL, "A or B: close", 0};
    r3_box(s, 6, 8, 52, 8, &b);
    for (int i = 0; i < 3; i++) r3_text_scroll(s, 8, 10 + i * 1, a->note[i], 48, R3_ST(R3_ST_BODY));
    R3Cmd cmd = {a->reply, a->reply_style, '>', "", 0, "", "", R3_ST_HINT};
    r3_command_line(s, &cmd);
}

static void compose(App *a, R3Screen *s) {
    switch (a->mode) {
    case M_PLAYER: compose_player(a, s); break;
    case M_MENU:
        if (a->back_mode == M_PLAYER) compose_player(a, s); else compose_library(a, s);
        compose_menu_box(&a->menu, s, a->back_mode == M_PLAYER ? 8 : 5);      /* below the waveform when there is one */
        break;
    case M_JOB: compose_job(a, s); break;
    case M_NOTE: compose_note(a, s); break;
    case M_ID: compose_id(a, s); break;
    default: compose_library(a, s);
    }
}

/* ---- presenting: the cells, with the waveform's pixels laid over rows 1-6 ----------------------------------------------------------------- */

static unsigned wave_signature(App *a) {
    if (a->mode != M_PLAYER && !(a->mode == M_MENU && a->back_mode == M_PLAYER)) return 0;
    double dur = dur_of(a);
    unsigned h = 2166136261u;
    h = (h ^ (unsigned)wave_x(engine_pos_sec(&a->eng), dur)) * 16777619u;
    h = (h ^ (unsigned)a->loop_state) * 16777619u;
    h = (h ^ (unsigned)(a->loop_a * 100) ^ (unsigned)(a->loop_b * 100) << 3) * 16777619u;
    h = (h ^ (unsigned)a->cues.nm) * 16777619u;
    h = (h ^ (unsigned)(a->offset * 1000)) * 16777619u;
    for (int i = 0; i < a->cues.nm; i++) h = (h ^ (unsigned)(a->cues.m[i].time * 100)) * 16777619u;
    return h | 1u;
}

static void paint(App *a, uint32_t *px, const R3Screen *s) {
    r3_render(s, px);
    if (a->mode == M_PLAYER || (a->mode == M_MENU && a->back_mode == M_PLAYER)) {
        WaveStyle st;
        WaveMark marks[WAVE_MAX_MARKS];
        wave_style(&st);
        int nm = wave_marks(a, marks, WAVE_MAX_MARKS);
        wave_render(px + (size_t)R3_CELL_H * R3_W, R3_W, &a->peaks, &st, dur_of(a), engine_pos_sec(&a->eng), marks, nm,
                    a->loop_state == 2, a->loop_a, a->loop_b);
    }
}

static void present(App *a, R3Video *v, SDL_Renderer *r, const R3Screen *s, int force) {
    unsigned sig = wave_signature(a);
    int changed = force || !v->have_prev || v->pal_gen != r3_pal_gen || memcmp(&v->prev, s, sizeof *s) != 0 || sig != a->wave_sig;
    if (changed) {
        r3_mirror_tap(s, v->have_prev && v->pal_gen == r3_pal_gen ? &v->prev : NULL);
        paint(a, v->px, s);
        SDL_UpdateTexture(v->tex, NULL, v->px, R3_W * (int)sizeof(uint32_t));
        v->prev = *s;
        v->have_prev = 1;
        v->pal_gen = r3_pal_gen;
        a->wave_sig = sig;
    }
    if (r3_video_due(v, changed)) {
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        SDL_RenderCopy(r, v->tex, NULL, NULL);
        SDL_RenderPresent(r);
    }
}

/* ---- actions ------------------------------------------------------------------------------------------------------------------------------- */

enum { A_MARK = 1, A_LABEL, A_DELETE, A_SAVE, A_EXPORT, A_OFFSET, A_THEME, A_SYNC, A_IMPORT, A_DEMO, A_LICENSE, A_ADD, A_QUIT, A_CLOSE, A_LABELPICK };

static const char *const PRESET_LABELS[] = {"Intro", "Verse", "Pre-Chorus", "Chorus", "Bridge", "Solo", "Instrumental", "Outro", "End", "Tag"};
#define NPRESET ((int)(sizeof PRESET_LABELS / sizeof *PRESET_LABELS))

static void jump_marker(App *a, int i) {
    if (i < 0 || i >= a->cues.nm) return;
    engine_seek(&a->eng, a->cues.m[i].time + a->offset);
    engine_play(&a->eng, 1);
    a->msel = i;
    say(a, R3_ST_HINT, "%s", a->cues.m[i].label);
}

static void mark_here(App *a) {
    double vt = engine_pos_sec(&a->eng) - a->offset;
    if (vt < 0) vt = 0;
    char label[48];
    int n = a->cues.nm + 1;
    snprintf(label, sizeof label, "Mark %d", n);
    int i = cue_add_marker(&a->cues, vt, label, 0);
    if (i < 0) { say(a, R3_ST_ERROR, "No room for another marker."); return; }
    cue_sort(&a->cues);
    for (int k = 0; k < a->cues.nm; k++) if (!strcmp(a->cues.m[k].label, label) && fabs(a->cues.m[k].time - vt) < 0.0005) a->msel = k;
    say(a, R3_ST_OK, "Marked %s at %.1fs. Start > Name it.", label, vt);
}

static void loop_cycle(App *a) {
    double pos = engine_pos_sec(&a->eng);
    if (a->loop_state == 0) { a->loop_a = pos; a->loop_state = 1; say(a, R3_ST_HINT, "Loop A set. Y again sets B."); }
    else if (a->loop_state == 1) {
        if (pos - a->loop_a < 0.1) { say(a, R3_ST_WARN, "B must be after A."); return; }
        a->loop_b = pos; a->loop_state = 2; set_loop_engine(a);
        say(a, R3_ST_OK, "Looping A to B. Y clears it.");
    } else { a->loop_state = 0; set_loop_engine(a); say(a, R3_ST_HINT, "Loop cleared."); }
}

static void seek_rel(App *a, double d) { engine_seek(&a->eng, engine_pos_sec(&a->eng) + d); }

static void save_offset(const App *a) {
    if (a->open < 0) return;
    char path[PATH_MAX], buf[32];
    cue_path(a, a->songs[a->open].id, "offset", path, sizeof path);
    snprintf(buf, sizeof buf, "%.3f\n", a->offset);
    cue_write_file(path, buf, strlen(buf));
}

static void build_main_menu(App *a) {
    char v[40];
    menu_clear(&a->menu, a->back_mode == M_PLAYER ? "Song" : "MusiCue");
    if (a->back_mode == M_PLAYER) {
        menu_add(&a->menu, A_MARK, "Mark here", "");
        menu_add(&a->menu, A_LABEL, "Name the selected marker", a->cues.nm ? a->cues.m[a->msel].label : "");
        menu_add(&a->menu, A_DELETE, "Delete the selected marker", "");
        snprintf(v, sizeof v, "%+.2f s", a->offset);
        menu_add(&a->menu, A_OFFSET, "Offset (audio against cues)", v);
        menu_add(&a->menu, A_SAVE, "Save cues", a->cues.dirty ? "unsaved" : "saved");
        menu_add(&a->menu, A_EXPORT, "Export for Cue (json, srt, bundle)", "");
    } else {
        menu_add(&a->menu, A_ADD, "Add: YouTube ID (11 chars)", "");
        menu_add(&a->menu, A_SYNC, "Sync: fetch cues and songs", "");
        menu_add(&a->menu, A_IMPORT, "Import Cue bundles from inbox/", "");
        menu_add(&a->menu, A_DEMO, "Demo song", "");
        menu_add(&a->menu, A_LICENSE, "Fetch from YouTube", a->any_license ? "any video" : "Creative Commons only");
    }
    menu_add(&a->menu, A_THEME, "Theme", r3_themes[a->theme].name);
    menu_add(&a->menu, A_QUIT, a->back_mode == M_PLAYER ? "Back to the songs" : "Quit", "");
}

static void open_menu(App *a) {
    a->back_mode = a->mode;
    int keep = a->mode == M_MENU ? a->menu.sel : 0;
    build_main_menu(a);
    a->menu.sel = keep;
    a->mode = M_MENU;
}

static void open_label_menu(App *a) {
    if (!a->cues.nm) return;
    menu_clear(&a->menu, "Name the marker");
    for (int i = 0; i < NPRESET; i++) menu_add(&a->menu, A_LABELPICK + i, PRESET_LABELS[i], "");
    a->menu.sel = 0;
    a->mode = M_MENU;
}

/* Cue bundles waiting in inbox/, and server json/srt pairs: each video's cues go to cues/<id>.json. A file that disagrees with one already there is kept
 * as a separate copy in cues/.merge-conflicts/, never written over (Cue's own rule, and rule 16's). Returns the number of videos imported. */
static int import_inbox(App *a, int *conflicts) {
    char dir[PATH_MAX], cdir[PATH_MAX], kdir[PATH_MAX];
    sub_path(a, "inbox", NULL, dir, sizeof dir);
    sub_path(a, "cues", NULL, cdir, sizeof cdir);
    snprintf(kdir, sizeof kdir, "%s/.merge-conflicts", cdir);
    mkdirs(cdir);
    int imported = 0;
    *conflicts = 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        const char *dot = strrchr(e->d_name, '.');
        if (e->d_name[0] == '.' || !dot || (strcasecmp(dot, ".cuebundle") && strcasecmp(dot, ".json"))) continue;
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        size_t n;
        char *t = cue_read_file(path, &n);
        if (!t) continue;
        CueBundle b;
        char err[96];
        if (cue_load_bundle(&b, t, n, err) > 0) {
            for (int i = 0; i < b.n; i++) {
                CueBuf out = {0};
                cue_write_version(&b.it[i].set, &out, time(NULL));
                char dst[PATH_MAX];
                snprintf(dst, sizeof dst, "%s/%s.json", cdir, b.it[i].videoid);
                size_t on;
                char *old = cue_read_file(dst, &on);
                if (!old) { if (cue_write_file(dst, out.p, out.n) == 0) imported++; }
                else {
                    CueSet o;
                    cue_init(&o);
                    int same = cue_load_json(&o, old, on, 1, err) && o.nm == b.it[i].set.nm && o.nc == b.it[i].set.nc;
                    for (int k = 0; same && k < o.nm; k++) same = fabs(o.m[k].time - b.it[i].set.m[k].time) < 0.001 && !strcmp(o.m[k].label, b.it[i].set.m[k].label);
                    cue_free(&o);
                    if (!same) {
                        mkdirs(kdir);
                        snprintf(dst, sizeof dst, "%s/%s-%ld.json", kdir, b.it[i].videoid, (long)time(NULL));
                        if (cue_write_file(dst, out.p, out.n) == 0) (*conflicts)++;
                    }
                    free(old);
                }
                free(out.p);
            }
            cue_bundle_free(&b);
        }
        free(t);
    }
    closedir(d);
    return imported;
}

static void export_cues(App *a) {
    if (a->open < 0) { say(a, R3_ST_WARN, "Nothing to export for the demo song."); return; }
    save_cues(a);
    char dir[PATH_MAX], p[PATH_MAX];
    sub_path(a, "export", NULL, dir, sizeof dir);
    mkdirs(dir);
    CueSet copy = a->cues;                  /* a shallow copy: only the metadata is changed, and only in the copy */
    cue_meta_stamp(&copy, time(NULL));      /* an export is what sets `created` (Cue's rule) */
    const char *id = a->songs[a->open].id;
    CueBuf j = {0}, s = {0}, b = {0};
    cue_write_server_json(&copy, &j);
    cue_write_srt(&copy, &s);
    const CueSet *sets[1] = {&copy};
    const char *titles[1] = {a->songs[a->open].name};
    snprintf(copy.videoid, sizeof copy.videoid, "%s", id);
    cue_write_bundle(sets, titles, 1, &b, time(NULL));
    int bad = 0;
    snprintf(p, sizeof p, "%s/%s.json", dir, id); bad |= cue_write_file(p, j.p, j.n);
    snprintf(p, sizeof p, "%s/%s.srt", dir, id); bad |= cue_write_file(p, s.p, s.n);
    snprintf(p, sizeof p, "%s/%s.cuebundle", dir, id); bad |= cue_write_file(p, b.p, b.n);
    free(j.p); free(s.p); free(b.p);
    if (bad) say(a, R3_ST_ERROR, "Could not write to export/.");
    else say(a, R3_ST_OK, "Exported %s: .json, .srt and .cuebundle in export/.", id);
}

/* ---- the sync job: musicue-sync, a helper that fetches Cue's server files and, with yt-dlp, audio ------------------------------------------- */

static void job_line(App *a, const char *text) {
    if (a->job_nlines == 6) { memmove(a->job_lines[0], a->job_lines[1], 5 * sizeof a->job_lines[0]); a->job_nlines = 5; }
    snprintf(a->job_lines[a->job_nlines++], sizeof a->job_lines[0], "%s", text);
}

static void job_stop(App *a) {
    if (a->job_fd > 0) close(a->job_fd);
    a->job_fd = -1;
    if (a->job_pid > 0) { kill(a->job_pid, SIGTERM); waitpid(a->job_pid, NULL, 0); }
    a->job_pid = 0;
}

/* musicue-sync: $R3I6NOS_MUSICUE_SYNC, else the copy packed beside this program (usr/lib/musicue/ next to usr/games/), else the PATH's. */
static const char *helper_path(char *buf, size_t cap) {
    const char *o = getenv("R3I6NOS_MUSICUE_SYNC");
    if (o && *o) return o;
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = 0;
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = 0;
            snprintf(buf, cap, "%s/../lib/musicue/musicue-sync", exe);
            if (access(buf, X_OK) == 0) return buf;
        }
    }
    return "musicue-sync";
}

static void job_start(App *a, const char *one_id) {
    char hbuf[PATH_MAX + 40];
    const char *helper = helper_path(hbuf, sizeof hbuf);
    int fd[2];
    memset(a->job_lines, 0, sizeof a->job_lines);
    a->job_nlines = 0; a->job_frac = 0; a->job_done = 0; a->job_len = 0;
    snprintf(a->job_want, sizeof a->job_want, "%s", one_id ? one_id : "");
    if (pipe(fd) != 0) { say(a, R3_ST_ERROR, "Cannot start the sync."); return; }
    pid_t pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); say(a, R3_ST_ERROR, "Cannot start the sync."); return; }
    if (pid == 0) {
        dup2(fd[1], 1);
        dup2(fd[1], 2);
        close(fd[0]); close(fd[1]);
        setenv("R3I6NOS_APP_DATA", a->data, 1);
        if (one_id) execlp(helper, helper, "--data", a->data, a->any_license ? "--any-license" : "--cc-only", "--id", one_id, (char *)NULL);
        else execlp(helper, helper, "--data", a->data, a->any_license ? "--any-license" : "--cc-only", (char *)NULL);
        _exit(127);
    }
    close(fd[1]);
    fcntl(fd[0], F_SETFL, fcntl(fd[0], F_GETFL) | O_NONBLOCK);
    a->job_fd = fd[0];
    a->job_pid = pid;
    a->mode = M_JOB;
    job_line(a, "Starting...");
    say(a, R3_ST_HINT, one_id ? "Fetching %s." : "Reading MusiCue/inbox/urls.txt.", one_id ? one_id : "");
}

static void job_poll(App *a) {
    if (a->job_fd <= 0) return;
    char buf[256];
    ssize_t got;
    while ((got = read(a->job_fd, buf, sizeof buf)) > 0)
        for (ssize_t i = 0; i < got; i++) {
            char c = buf[i];
            if (c == '\n' || a->job_len >= (int)sizeof a->job_buf - 1) {
                a->job_buf[a->job_len] = 0;
                a->job_len = 0;
                if (!strncmp(a->job_buf, "PROGRESS ", 9)) {
                    int x = 0, y = 1;
                    if (sscanf(a->job_buf + 9, "%d/%d", &x, &y) == 2 && y > 0) a->job_frac = (double)x / y;
                    const char *msg = strchr(a->job_buf + 9, ' ');
                    if (msg) job_line(a, msg + 1);
                } else if (!strncmp(a->job_buf, "BAR ", 4)) {                 /* the bar only: no new line on the screen */
                    int x = 0, y = 1;
                    if (sscanf(a->job_buf + 4, "%d/%d", &x, &y) == 2 && y > 0) a->job_frac = (double)x / y;
                } else if (!strncmp(a->job_buf, "DONE", 4)) { a->job_frac = 1; a->job_done = 1; job_line(a, a->job_buf); }
                else if (a->job_buf[0]) job_line(a, a->job_buf);
            } else a->job_buf[a->job_len++] = c;
        }
    if (got == 0 || (got < 0 && errno != EAGAIN && errno != EINTR)) {
        int status = 0;
        waitpid(a->job_pid, &status, 0);
        a->job_pid = 0;
        close(a->job_fd);
        a->job_fd = -1;
        if (WIFEXITED(status) && WEXITSTATUS(status) == 127) { job_line(a, "musicue-sync is not installed."); say(a, R3_ST_ERROR, "The sync helper is missing."); }
        else if (!a->job_done) say(a, R3_ST_WARN, "The sync ended before it said it was done.");
        else say(a, R3_ST_OK, "Sync finished.");
        a->job_done = 1;
        scan_songs(a);
        if (a->job_want[0]) {                           /* Add: the song just fetched opens at once; if only its cues came, the last line says why not */
            for (int i = 0; i < a->nsongs; i++)
                if (!strcmp(a->songs[i].id, a->job_want)) {
                    a->lsel = i;
                    job_stop(a);
                    if (!open_song(a, i)) a->mode = M_LIBRARY;
                    break;
                }
            if (a->mode == M_JOB) say(a, R3_ST_WARN, "No audio came for %s: see the last line. A closes.", a->job_want);
            a->job_want[0] = 0;
        }
    }
}

/* ---- Add: a YouTube id typed in, fetched and opened ---------------------------------------------------------------------------------------- */

/* Online: the kernel has a default route (a wired or Wi-Fi link that is up). Nothing is sent to find out. $R3I6NOS_MUSICUE_ONLINE=0/1 overrides it for tests. */
static int device_online(void) {
    const char *o = getenv("R3I6NOS_MUSICUE_ONLINE");
    if (o && *o) return *o != '0';
    FILE *f = fopen("/proc/net/route", "r");
    if (!f) return 0;
    char line[256], ifc[32], dest[16], gw[16];
    unsigned flags;
    int up = 0;
    if (!fgets(line, sizeof line, f)) { fclose(f); return 0; }
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "%31s %15s %15s %x", ifc, dest, gw, &flags) == 4 && !strcmp(dest, "00000000") && (flags & 1) && strcmp(ifc, "lo")) up = 1;
    fclose(f);
    return up;
}

static void id_open(App *a) {
    memset(a->idbuf, 0, sizeof a->idbuf);
    a->idpos = 0;
    a->mode = M_ID;
    say(a, R3_ST_HINT, "Type or pick the 11 characters of a YouTube id.");
}

static void id_set(App *a, int ch) {
    a->idbuf[a->idpos] = (char)ch;
    if (a->idpos < 10) a->idpos++;
}

static void id_cycle(App *a, int step) {
    char *c = &a->idbuf[a->idpos];
    const char *at = *c ? strchr(ID_ALPHA, *c) : NULL;
    int n = (int)sizeof ID_ALPHA - 1, i = at ? (int)(at - ID_ALPHA) : (step > 0 ? -1 : 0);
    *c = ID_ALPHA[((i + step) % n + n) % n];
}

static int id_full(const App *a) { for (int i = 0; i < 11; i++) if (!a->idbuf[i]) return 0; return 1; }

/* Online: fetch its cues and audio now and open the song. Offline: keep the id in inbox/urls.txt, so the next Sync (online) gets it. */
static void id_go(App *a) {
    char id[12];
    snprintf(id, sizeof id, "%.11s", a->idbuf);
    if (!id_full(a) || !cue_videoid_ok(id)) { say(a, R3_ST_WARN, "An id is 11 characters: letters, digits, - and _."); return; }
    if (device_online()) { job_start(a, id); return; }
    char path[PATH_MAX];
    sub_path(a, "inbox", "urls.txt", path, sizeof path);
    FILE *f = fopen(path, "a");
    if (f) { fprintf(f, "%s\n", id); fclose(f); say(a, R3_ST_WARN, "Offline. %s is kept for the next Sync.", id); }
    else say(a, R3_ST_ERROR, "Offline, and inbox/urls.txt cannot be written.");
    a->mode = M_LIBRARY;
}

static void id_erase(App *a) {
    if (a->idbuf[a->idpos] == 0 && a->idpos > 0) a->idpos--;
    a->idbuf[a->idpos] = 0;
}

/* A key on a keyboard while the id is being entered: true when it was used. */
static int id_key(App *a, SDL_Keycode k, Uint16 mod) {
    if ((k >= SDLK_a && k <= SDLK_z) || (k >= SDLK_0 && k <= SDLK_9) || k == SDLK_MINUS || k == SDLK_UNDERSCORE) {
        int ch = (int)k;
        if (k >= SDLK_a && k <= SDLK_z && (mod & KMOD_SHIFT)) ch = toupper(ch);
        if (k == SDLK_MINUS && (mod & KMOD_SHIFT)) ch = '_';
        id_set(a, ch);
        return 1;
    }
    if (k == SDLK_BACKSPACE) { id_erase(a); return 1; }
    return 0;
}

/* ---- the menu's choices -------------------------------------------------------------------------------------------------------------------- */

static void menu_adjust(App *a, int dir) {
    MItem *it = &a->menu.it[a->menu.sel];
    if (it->id == A_THEME) {
        a->theme = (a->theme + dir + R3_THEME_COUNT) % R3_THEME_COUNT;
        apply_theme(a);
        save_settings(a);
        snprintf(it->value, sizeof it->value, "%s", r3_themes[a->theme].name);
    } else if (it->id == A_OFFSET && a->open >= 0) {
        a->offset += dir * 0.05;
        if (fabs(a->offset) < 0.0001) a->offset = 0;
        if (a->offset > 30) a->offset = 30;
        if (a->offset < -30) a->offset = -30;
        save_offset(a);
        snprintf(it->value, sizeof it->value, "%+.2f s", a->offset);
    } else if (it->id == A_LICENSE) {
        a->any_license = !a->any_license;
        save_settings(a);
        snprintf(it->value, sizeof it->value, "%s", a->any_license ? "any video" : "Creative Commons only");
    }
}

static void menu_choose(App *a) {
    int id = a->menu.it[a->menu.sel].id;
    if (id >= A_LABELPICK) {
        snprintf(a->cues.m[a->msel].label, sizeof a->cues.m[a->msel].label, "%s", PRESET_LABELS[id - A_LABELPICK]);
        a->cues.dirty = 1;
        a->mode = M_PLAYER;
        return;
    }
    switch (id) {
    case A_MARK: a->mode = M_PLAYER; mark_here(a); break;
    case A_LABEL: open_label_menu(a); break;
    case A_DELETE:
        if (a->cues.nm) { cue_del_marker(&a->cues, a->msel); if (a->msel >= a->cues.nm) a->msel = a->cues.nm ? a->cues.nm - 1 : 0; say(a, R3_ST_HINT, "Marker deleted."); }
        a->mode = M_PLAYER;
        break;
    case A_SAVE: say(a, save_cues(a) == 0 ? R3_ST_OK : R3_ST_ERROR, "%s", a->cues.dirty ? "Could not save the cues." : "Cues saved."); a->mode = M_PLAYER; break;
    case A_EXPORT: export_cues(a); a->mode = M_PLAYER; break;
    case A_OFFSET: case A_THEME: case A_LICENSE: menu_adjust(a, 1); break;
    case A_SYNC: job_start(a, NULL); break;
    case A_ADD: id_open(a); break;
    case A_IMPORT: {
        int c = 0, n = import_inbox(a, &c);
        scan_songs(a);
        if (n == 0 && c == 0) say(a, R3_ST_WARN, "Nothing new in inbox/.");
        else say(a, R3_ST_OK, "Imported %d song%s%s.", n, n == 1 ? "" : "s", c ? ", kept others as conflicts" : "");
        a->mode = M_LIBRARY;
        break;
    }
    case A_DEMO: open_demo(a); say(a, R3_ST_HINT, "The demo song: A plays, X jumps, Y loops."); break;
    case A_QUIT:
        if (a->back_mode == M_PLAYER) { save_cues(a); close_song(a); a->mode = M_LIBRARY; }
        else a->quit = 1;
        break;
    default: a->mode = a->back_mode;
    }
}

static void app_action(App *a, int act) {
    switch (a->mode) {
    case M_LIBRARY:
        if (act == R3_ACCEPT) { if (a->nsongs && !open_song(a, a->lsel)) a->mode = M_LIBRARY; }
        else if (act == R3_BACK) a->quit = 1;
        else if (act == R3_MORE) open_menu(a);
        else a->lsel = r3_list_key(a->lsel, a->nsongs, 11, act);
        break;
    case M_PLAYER:
        switch (act) {
        case R3_ACCEPT: engine_play(&a->eng, !a->eng.playing); break;
        case R3_BACK: save_cues(a); close_song(a); a->mode = M_LIBRARY; break;
        case R3_MORE: open_menu(a); break;
        case R3_X: if (a->cues.nm) jump_marker(a, a->msel); break;
        case R3_Y: loop_cycle(a); break;
        case R3_UP: case R3_DOWN: a->msel = r3_list_key(a->msel, a->cues.nm, MARK_ROWS, act); break;
        case R3_PAGEUP: case R3_PAGEDOWN: {
            int cm = cur_marker(a, engine_pos_sec(&a->eng) - a->offset);
            int target = act == R3_PAGEUP ? (cm > 0 && engine_pos_sec(&a->eng) - a->offset - a->cues.m[cm].time < 1.5 ? cm - 1 : cm) : cm + 1;
            if (target >= 0 && target < a->cues.nm) jump_marker(a, target);
            else if (act == R3_PAGEUP) engine_seek(&a->eng, 0);
            break;
        }
        case R3_LEFT: seek_rel(a, -1); break;
        case R3_RIGHT: seek_rel(a, 1); break;
        case R3_HOME: seek_rel(a, -10); break;
        case R3_END: seek_rel(a, 10); break;
        case R3_SELECT: break;
        default: break;
        }
        break;
    case M_MENU:
        if (act == R3_BACK || act == R3_MORE) { a->mode = a->back_mode == M_PLAYER || a->menu.it[0].id >= A_LABELPICK ? (a->open >= 0 || a->demo ? M_PLAYER : M_LIBRARY) : a->back_mode; }
        else if (act == R3_ACCEPT) menu_choose(a);
        else if (act == R3_LEFT) menu_adjust(a, -1);
        else if (act == R3_RIGHT) menu_adjust(a, 1);
        else a->menu.sel = r3_list_key(a->menu.sel, a->menu.n, 12, act);
        break;
    case M_JOB:
        if (act == R3_BACK || act == R3_ACCEPT) { job_stop(a); a->mode = M_LIBRARY; scan_songs(a); }
        break;
    case M_NOTE:
        if (act == R3_BACK || act == R3_ACCEPT) a->mode = M_LIBRARY;
        break;
    case M_ID:
        switch (act) {
        case R3_BACK: a->mode = M_LIBRARY; say(a, R3_ST_HINT, "Nothing added."); break;
        case R3_UP: id_cycle(a, 1); break;
        case R3_DOWN: id_cycle(a, -1); break;
        case R3_PAGEUP: id_cycle(a, 8); break;
        case R3_PAGEDOWN: id_cycle(a, -8); break;
        case R3_LEFT: if (a->idpos > 0) a->idpos--; break;
        case R3_RIGHT: if (a->idpos < 10) a->idpos++; break;
        case R3_Y: id_erase(a); break;
        case R3_ACCEPT:
            if (!a->idbuf[a->idpos]) { id_cycle(a, 1); if (a->idpos < 10) a->idpos++; }     /* an empty slot becomes A and the cursor moves on */
            else if (a->idpos < 10) a->idpos++;
            else id_go(a);                                                                  /* A on the last, filled slot fetches */
            break;
        case R3_MORE: id_go(a); break;
        default: break;
        }
        break;
    }
}

static void app_tick(App *a, double ms) {
    engine_tick(&a->eng, ms);
    r3_marquee_clock(ms);
    if (a->mode == M_PLAYER && a->eng.ended) { a->eng.ended = 0; say(a, R3_ST_HINT, "The end. A plays it again."); }
    job_poll(a);
}

static void app_init(App *a, const char *data) {
    memset(a, 0, sizeof *a);
    a->open = -1;
    a->theme = r3_theme_find("Nord");
    snprintf(a->data, sizeof a->data, "%s", data);
    a->job_fd = -1;
}

static void app_free(App *a) {
    job_stop(a);
    save_cues(a);
    close_song(a);
    free(a->songs);
}

/* ---- the tests ----------------------------------------------------------------------------------------------------------------------------- */

static int g_bad;
#define CHECK(cond, ...) do { if (!(cond)) { g_bad++; fprintf(stderr, "selftest FAIL (%s:%d): ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static void write_wav(const char *path, int rate, int chans, int bits, int frames, int shape) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int bps = chans * bits / 8;
    uint32_t data = (uint32_t)(frames * bps);
    unsigned char h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, (unsigned char)chans, 0};
    uint32_t v = 36 + data;
    memcpy(h + 4, &v, 4);
    v = (uint32_t)rate; memcpy(h + 24, &v, 4);
    v = (uint32_t)(rate * bps); memcpy(h + 28, &v, 4);
    h[32] = (unsigned char)bps; h[34] = (unsigned char)bits;
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data, 4);
    fwrite(h, 1, 44, f);
    for (int i = 0; i < frames; i++)
        for (int c = 0; c < chans; c++) {
            double x = shape ? ((i / 100) % 2 ? 0.8 : -0.8) : 0.5 * sin(i * 0.1);
            if (bits == 16) { int16_t s = (int16_t)(x * 30000); fwrite(&s, 2, 1, f); }
            else { unsigned char s = (unsigned char)(128 + x * 100); fwrite(&s, 1, 1, f); }
        }
    fclose(f);
}

static void test_json_and_cues(void) {
    JNode j;
    char err[96];
    CHECK(j_parse("{\"a\":[1,2.5,-3e2,true,null,\"x\\u00e9\\n\"],\"b\":{}}", 47, &j, err), "valid json: %s", err);
    CHECK(j.t == J_OBJ && j_get(&j, "a") && j_get(&j, "a")->n == 6, "array of six");
    CHECK(j_num(&j_get(&j, "a")->kid[2], 0) == -300, "exponent");
    CHECK(!strcmp(j_str(&j_get(&j, "a")->kid[5]), "x\xc3\xa9\n"), "\\u escape and \\n become UTF-8");
    j_free(&j);
    CHECK(!j_parse("{\"a\":", 5, &j, err), "a cut object is refused");
    CHECK(!j_parse("[1,]", 4, &j, err), "a trailing comma is refused");
    CHECK(!j_parse("{} x", 4, &j, err), "text after the value is refused");
    char deep[200];
    memset(deep, '[', 100); deep[100] = 0;
    CHECK(!j_parse(deep, 100, &j, err), "100 levels deep is refused (%s)", err);
    CHECK(j_parse("\"\\ud83d\\ude00\"", 14, &j, err) && !strcmp(j.s, "\xf0\x9f\x98\x80"), "a surrogate pair is one character");
    j_free(&j);

    const char *server = "{\"markers\":[{\"id\":2,\"time\":30.5,\"key\":\"2\",\"label\":\"Chorus\",\"noKey\":false},{\"id\":1,\"time\":5,\"label\":\"Verse 1\",\"noKey\":false},"
                         "{\"time\":-1,\"label\":\"bad\"},{\"time\":9,\"label\":\"Hidden\",\"noKey\":true}]}";
    CueSet c;
    cue_init(&c);
    CHECK(cue_load_json(&c, server, strlen(server), 1, err), "server markers: %s", err);
    CHECK(c.nm == 3, "three good markers (the negative time is dropped): %d", c.nm);
    CHECK(!strcmp(c.m[0].label, "Verse 1") && c.m[0].key[0] == '1' && !strcmp(c.m[2].label, "Chorus") && c.m[2].key[0] == '2', "sorted by time, keys in time order");
    CHECK(c.m[1].nokey && c.m[1].key[0] == 0, "a noKey marker has no key");
    const char *srt = "1\n00:00:00,000 --> 00:00:00,001\n[chordica.com/cue]\n\n2\n00:00:00,001 --> 00:00:00,002\nkey=Am\n\n3\n00:00:00,002 --> 00:00:00,003\nevil=1\n\n"
                      "4\n00:00:00,003 --> 00:00:00,004\ncreated=yesterday\n\n5\n00:00:00,004 --> 00:00:00,005\nscripter=Joash\n\n6\n00:00:00,005 --> 00:00:05,005\n[chordica.com/cue]\n\n"
                      "7\n00:00:12,500 --> 00:00:20,000\nAm  F\nsecond line\n\n8\n00:00:20,000 --> 00:00:21,000\nC\n";
    CHECK(cue_load_srt(&c, srt, strlen(srt), err) == 2, "two real captions (the metadata block is not one)");
    CHECK(!strcmp(c.meta.key, "Am") && !strcmp(c.meta.scripter, "Joash"), "metadata read");
    CHECK(c.meta.created[0] == 0, "a created that is not a time is dropped");
    CHECK(cue_meta_slot(&c.meta, "evil") == NULL, "no field can be added");
    CHECK(c.nc == 2 && c.c[0].start == 12.5 && c.c[0].has_end && !strcmp(c.c[0].text, "Am  F second line"), "a caption's lines join");
    CHECK(cue_meta_set(&c, "key", "a very long key name that will be cut at forty characters, certainly") && strlen(c.meta.key) == 40, "a value is cut to its limit");
    cue_meta_set(&c, "key", "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9");
    CHECK(strlen(c.meta.key) % 2 == 0, "a cut never splits a UTF-8 character");
    cue_meta_stamp(&c, 1000000000);
    CHECK(!strcmp(c.meta.created, "2001-09-09T01:46:40.000Z"), "an export sets created: %s", c.meta.created);
    CHECK(cue_created_ok("2026-09-21T08:07:26.634Z") && cue_created_ok("2026-09-21T08:07:26Z") && !cue_created_ok("2026-09-21 08:07:26Z") && !cue_created_ok("2026-09-21T08:07:26.6345Z"),
          "the created shape");

    /* the round trips: version data, srt, bundle */
    CueBuf b = {0};
    cue_write_version(&c, &b, 1000000000);
    CueSet r;
    cue_init(&r);
    CHECK(cue_load_json(&r, b.p, b.n, 1, err), "version data reloads: %s", err);
    CHECK(r.nm == c.nm && r.nc == c.nc && !strcmp(r.meta.key, c.meta.key) && !strcmp(r.meta.created, c.meta.created), "version data keeps everything");
    free(b.p); b.p = NULL; b.n = b.cap = 0;
    cue_write_srt(&c, &b);
    CueSet r2;
    cue_init(&r2);
    CHECK(cue_load_srt(&r2, b.p, b.n, err) == 2 && !strcmp(r2.meta.scripter, "Joash") && !strcmp(r2.meta.created, c.meta.created), "the srt reloads with its metadata");
    CHECK(strstr(b.p, "00:00:00,000 --> 00:00:00,001\n[chordica.com/cue]") != NULL, "the hidden block opens at 0 for 1 ms");
    free(b.p); b.p = NULL; b.n = b.cap = 0;
    snprintf(c.videoid, sizeof c.videoid, "abcDEF12345");
    const CueSet *sets[1] = {&c};
    const char *titles[1] = {"My \"song\""};
    cue_write_bundle(sets, titles, 1, &b, 1000000000);
    CueBundle bun;
    CHECK(cue_load_bundle(&bun, b.p, b.n, err) == 1 && !strcmp(bun.it[0].videoid, "abcDEF12345") && bun.it[0].set.nm == c.nm && !strcmp(bun.it[0].title, "My \"song\""),
          "the bundle reloads: %s", err);
    cue_bundle_free(&bun);
    free(b.p);
    const char *evil = "{\"type\":\"cue-bundle\",\"data\":{\"../../etc/passwd\":{\"versions\":{\"v\":{\"markers\":[]}}},\"ok_1\":{\"versions\":{\"a\":{\"markers\":[{\"time\":1,\"label\":\"x\"}]},"
                       "\"b\":{\"markers\":[]}},\"favorite\":\"b\"}}}";
    CHECK(cue_load_bundle(&bun, evil, strlen(evil), err) == 1 && !strcmp(bun.it[0].videoid, "ok_1") && bun.it[0].set.nm == 0, "an unsafe video id is skipped and the favorite version is chosen");
    cue_bundle_free(&bun);
    CHECK(cue_load_bundle(&bun, "{\"markers\":[]}", 14, err) < 0, "a plain file is not a bundle");
    cue_free(&c); cue_free(&r); cue_free(&r2);

    CueSet k;
    cue_init(&k);
    for (int i = 0; i < 40; i++) cue_add_marker(&k, i + 1, "m", 0);
    cue_sort(&k);
    CHECK(k.m[0].key[0] == '1' && k.m[9].key[0] == '0' && k.m[10].key[0] == 'd' && k.m[32].key[0] == 'z' && k.m[33].key[0] == 0, "33 key slots: 1-9, 0, d-z");
    cue_free(&k);
}

static const char *g_tmp;

static void test_audio(void) {
    char p[PATH_MAX], err[160];
    Track t;
    snprintf(p, sizeof p, "%s/a.wav", g_tmp);
    write_wav(p, 44100, 2, 16, 44100, 0);
    CHECK(track_load(&t, p, 600, NULL, NULL, err, sizeof err) && t.frames == 44100 && !strcmp(t.via, "wav"), "a 44.1 kHz stereo WAV loads: %s", err);
    track_free(&t);
    snprintf(p, sizeof p, "%s/b.wav", g_tmp);
    write_wav(p, 22050, 1, 8, 22050, 0);
    CHECK(track_load(&t, p, 600, NULL, NULL, err, sizeof err) && labs((long)t.frames - 44100) <= 1, "22.05 kHz mono 8-bit is resampled to 44.1 kHz stereo: %ld frames", (long)t.frames);
    CHECK(t.pcm[1000 * 2] == t.pcm[1000 * 2 + 1], "mono is on both channels");
    track_free(&t);
    snprintf(p, sizeof p, "%s/c.wav", g_tmp);
    write_wav(p, 44100, 2, 16, 44100 * 3, 0);
    CHECK(track_load(&t, p, 1.0, NULL, NULL, err, sizeof err) && t.frames == 44100 && t.truncated, "a long track is cut at the cap and says so");
    track_free(&t);
    CHECK(!track_load(&t, "/nonexistent/none.wav", 600, NULL, NULL, err, sizeof err) && err[0], "a missing file says why: %s", err);

    /* ffmpeg through a fake: a script that prints 1.5 s of raw PCM (an odd chunk size, so the 4-byte frames are split across reads) */
    char raw[PATH_MAX], fake[PATH_MAX];
    snprintf(raw, sizeof raw, "%s/raw.pcm", g_tmp);
    snprintf(fake, sizeof fake, "%s/fakeffmpeg", g_tmp);
    FILE *f = fopen(raw, "wb");
    for (int i = 0; i < 66150; i++) { int16_t s[2] = {(int16_t)(i % 2000), (int16_t)-(i % 2000)}; fwrite(s, 2, 2, f); }
    fclose(f);
    f = fopen(fake, "w");
    fprintf(f, "#!/bin/sh\nfor a; do last=$a; done\ncase \"$*\" in *-ar*44100*-ac*2*pipe:1*) dd if=%s bs=4093 2>/dev/null ;; *) exit 3 ;; esac\n", raw);
    fclose(f);
    chmod(fake, 0755);
    setenv("R3I6NOS_FFMPEG", fake, 1);
    snprintf(p, sizeof p, "%s/song.m4a", g_tmp);
    f = fopen(p, "wb"); fputs("not a wav", f); fclose(f);
    CHECK(track_load(&t, p, 600, NULL, NULL, err, sizeof err) && t.frames == 66150 && !strcmp(t.via, "ffmpeg"), "ffmpeg's pipe is read whole: %ld frames (%s)", (long)t.frames, err);
    CHECK(t.pcm[(int64_t)5000 * 2] == 5000 % 2000 && t.pcm[(int64_t)5000 * 2 + 1] == -(5000 % 2000), "and its frames are in order across split reads");
    track_free(&t);
    CHECK(track_load(&t, p, 0.5, NULL, NULL, err, sizeof err) && t.frames == 22050 && t.truncated, "the cap applies to the pipe");
    track_free(&t);
    setenv("R3I6NOS_FFMPEG", "/nonexistent/ffmpeg", 1);
    CHECK(!track_load(&t, p, 600, NULL, NULL, err, sizeof err) && strstr(err, "not installed"), "a missing ffmpeg is said: %s", err);
    unsetenv("R3I6NOS_FFMPEG");

    /* the engine on the caller's clock: play, seek, loop with no gap, end */
    setenv("R3I6NOS_MUSICUE_NOAUDIO", "1", 1);
    snprintf(p, sizeof p, "%s/a.wav", g_tmp);
    track_load(&t, p, 600, NULL, NULL, err, sizeof err);
    Engine e;
    engine_open(&e, &t);
    CHECK(e.noaudio, "no audio device when asked");
    engine_play(&e, 1);
    for (int i = 0; i < 10; i++) engine_tick(&e, 25);
    CHECK(fabs(engine_pos_sec(&e) - 0.25) < 0.001, "250 ms of ticks is 0.25 s: %.4f", engine_pos_sec(&e));
    engine_seek(&e, 0.5);
    CHECK(engine_loop_set(&e, 0.2, 0.3) && e.loop, "a loop sets");
    CHECK(!engine_loop_set(&e, 0.2, 0.21), "a loop shorter than 50 ms is refused");
    int64_t worst_lo = 1 << 30, worst_hi = 0;
    engine_seek(&e, 0.25);
    for (int i = 0; i < 400; i++) { engine_tick(&e, 7.3); if (e.pos < worst_lo) worst_lo = e.pos; if (e.pos > worst_hi) worst_hi = e.pos; }
    CHECK(worst_lo >= (int64_t)(0.2 * 44100) && worst_hi < (int64_t)(0.3 * 44100) + 1, "the position stays inside the loop: %.4f..%.4f", (double)worst_lo / 44100, (double)worst_hi / 44100);
    /* the callback path with a real buffer: continuous samples across the wrap */
    int16_t out[4096 * 2];
    engine_seek(&e, 0.2999);
    e.noaudio = 0;
    engine_callback(&e, (Uint8 *)out, sizeof out);
    e.noaudio = 1;
    int64_t la = (int64_t)(0.2 * 44100), lb = (int64_t)(0.3 * 44100), a0 = (int64_t)(0.2999 * 44100);
    int ok = 1;
    for (int i = 0; i < 4096 && ok; i++) {
        int64_t src = a0 + i;
        while (src >= lb) src = la + (src - lb);
        if (out[i * 2] != t.pcm[src * 2]) ok = 0;
    }
    CHECK(ok, "the callback's output wraps sample-exactly at B");
    engine_loop_clear(&e);
    engine_seek(&e, t.frames / 44100.0 - 0.1);
    for (int i = 0; i < 20; i++) engine_tick(&e, 10);
    CHECK(!e.playing && e.ended, "the end stops playback and says so");
    engine_play(&e, 1);
    CHECK(e.pos == 0 || engine_pos_sec(&e) < 1.0, "play at the end starts again");
    engine_close(&e);
    track_free(&t);
    unsetenv("R3I6NOS_MUSICUE_NOAUDIO");
}

static void test_wave(void) {
    int64_t n = 44100 * 4;
    int16_t *pcm = (int16_t *)malloc((size_t)n * 4);
    for (int64_t i = 0; i < n; i++) { int16_t v = (int16_t)(i < n / 2 ? 3000 * sin(i * 0.05) : 20000 * sin(i * 0.05)); pcm[i * 2] = pcm[i * 2 + 1] = v; }
    WavePeaks w;
    wave_peaks(pcm, n, &w);
    CHECK(w.cols == WAVE_W && w.peak > 19900 && w.peak <= 20000, "the loudest sample is found: %d", w.peak);
    CHECK(w.mx[10] < 3100 && w.mx[10] > 2900 && w.mx[600] > 19900, "quiet and loud halves differ (%d, %d)", w.mx[10], w.mx[600]);
    CHECK(w.rms[600] > 13500 && w.rms[600] < 14500, "rms of a sine is peak over root two: %d", w.rms[600]);
    WavePeaks none;
    wave_peaks(NULL, 0, &none);
    CHECK(none.cols == 0, "no sound, no columns");
    static uint32_t px[WAVE_W * WAVE_H];
    WaveStyle st = {0xFF101018, 0xFF7FE0FF, 0xFF2A7FB0, 0xFF35556A, 0xFF1E3446, 0xFF506070, 0xFFFFFFFF, 0xFFFFD060, 0xFFFFD060};
    WaveMark m[1] = {{2.0, 0xFFFF6090}};
    wave_render(px, WAVE_W, &w, &st, 4.0, 1.0, m, 1, 1, 0.5, 1.5);
    int phx = wave_x(1.0, 4.0), mxx = wave_x(2.0, 4.0);
    CHECK(px[60 * WAVE_W + phx] == st.playhead, "the playhead column is the playhead colour");
    CHECK(px[2 * WAVE_W + phx] == st.playhead, "with a mark at its top");
    CHECK(px[3 * WAVE_W + mxx] == m[0].color, "the marker's flag is at its time");
    int opaque = 1;
    for (int i = 0; i < WAVE_W * WAVE_H; i++) if ((px[i] >> 24) != 0xFF) opaque = 0;
    CHECK(opaque, "every pixel is opaque");
    CHECK(px[60 * WAVE_W + 100] != px[60 * WAVE_W + 500], "played and unplayed sides differ");
    static uint32_t ref[WAVE_W * WAVE_H];                  /* the same picture with no sound: only the shape differs from it */
    wave_render(ref, WAVE_W, &none, &st, 4.0, 1.0, m, 1, 1, 0.5, 1.5);
    int quiet_rows = 0, loud_rows = 0;
    for (int y = 0; y < WAVE_H; y++) {
        if (px[y * WAVE_W + 300] != ref[y * WAVE_W + 300]) quiet_rows++;
        if (px[y * WAVE_W + 500] != ref[y * WAVE_W + 500]) loud_rows++;
    }
    CHECK(loud_rows > quiet_rows + 20 && quiet_rows > 4, "the loud half is taller than the quiet half (%d against %d rows)", loud_rows, quiet_rows);
    wave_render(px, WAVE_W, &none, &st, 4.0, 0.0, NULL, 0, 0, 0, 0);
    CHECK((px[10 * WAVE_W + 300] >> 24) == 0xFF, "a song with no sound still draws");
    free(pcm);
}

static void check_contrast(App *a, const char *what) {
    R3Screen s;
    compose(a, &s);
    int worst_i = -1;
    double worst = 99;
    for (int i = 0; i < R3_COLS * R3_ROWS; i++) {
        int row = i / R3_COLS;
        if (s.ch[i] == 32 || s.ch[i] == 0) continue;
        if (a->mode == M_PLAYER && row >= 1 && row <= 6) continue;           /* the waveform's rows are pixels */
        double c = r3_contrast_pal(s.fg[i], s.bg[i]);
        if (c < worst) { worst = c; worst_i = i; }
    }
    CHECK(worst >= 4.5, "%s, theme %s: a cell at row %d col %d (char %d) has contrast %.2f", what, r3_themes[a->theme].name, worst_i / R3_COLS, worst_i % R3_COLS, worst_i >= 0 ? s.ch[worst_i] : 0, worst);
}

/* Start > Add: typed or picked, 11 characters; offline it is kept in urls.txt, online the helper runs for that one id and the song opens. */
static void test_add_id(void) {
    App a;
    app_init(&a, g_tmp);
    char p[PATH_MAX], fake[PATH_MAX], d[PATH_MAX];
    snprintf(d, sizeof d, "%s/add", g_tmp);
    snprintf(a.data, sizeof a.data, "%s", d);
    const char *sub[] = {"media", "cues", "inbox"};
    for (int i = 0; i < 3; i++) { sub_path(&a, sub[i], NULL, p, sizeof p); mkdirs(p); }
    scan_songs(&a);
    id_open(&a);
    CHECK(a.mode == M_ID && !id_full(&a), "Add opens an empty id");
    app_action(&a, R3_UP);
    CHECK(a.idbuf[0] == 'A', "Up on an empty slot gives A");
    app_action(&a, R3_DOWN);
    CHECK(a.idbuf[0] == '_', "Down from A wraps to the last character: %c", a.idbuf[0]);
    id_key(&a, SDLK_d, 0);
    CHECK(a.idbuf[0] == 'd' && a.idpos == 1, "a keyboard types a letter and moves on");
    id_key(&a, SDLK_q, KMOD_LSHIFT);
    CHECK(a.idbuf[1] == 'Q', "shift makes a capital");
    id_erase(&a);
    CHECK(a.idbuf[1] == 0 && a.idpos == 1, "Y erases");
    for (const char *c = "w4w9WgXcQ"; *c; c++) id_key(&a, (SDL_Keycode)tolower((unsigned char)*c), isupper((unsigned char)*c) ? KMOD_LSHIFT : 0);
    CHECK(a.idpos == 10 && !strncmp(a.idbuf, "dw4w9WgXcQ", 10) && !id_full(&a), "ten typed, one to go");
    app_action(&a, R3_MORE);
    CHECK(a.mode == M_ID, "a short id does not go");
    id_key(&a, SDLK_s, 0);
    CHECK(id_full(&a) && !strcmp(a.idbuf, "dw4w9WgXcQs"), "eleven");
    setenv("R3I6NOS_MUSICUE_ONLINE", "0", 1);
    app_action(&a, R3_ACCEPT);
    sub_path(&a, "inbox", "urls.txt", p, sizeof p);
    char *t = cue_read_file(p, NULL);
    CHECK(a.mode == M_LIBRARY && t && !strcmp(t, "dw4w9WgXcQs\n"), "offline: kept in urls.txt for the next Sync");
    free(t);
    /* online: a fake helper makes the audio the way yt-dlp would name it, and the song opens */
    sub_path(&a, "media", NULL, p, sizeof p);
    snprintf(fake, sizeof fake, "%s/fakesync", g_tmp);
    FILE *f = fopen(fake, "w");
    fprintf(f, "#!/bin/sh\ncase \"$*\" in *--id*dw4w9WgXcQs*) ;; *) echo wrong args; exit 1 ;; esac\ncp %s/a.wav \"%s/Fake [dw4w9WgXcQs].wav\"\necho \"PROGRESS 1/1 dw4w9WgXcQs: audio saved\"\necho \"DONE 1 video\"\n", g_tmp, p);
    fclose(f);
    chmod(fake, 0755);
    setenv("R3I6NOS_MUSICUE_SYNC", fake, 1);
    setenv("R3I6NOS_MUSICUE_NOAUDIO", "1", 1);
    setenv("R3I6NOS_MUSICUE_ONLINE", "1", 1);
    id_open(&a);
    memcpy(a.idbuf, "dw4w9WgXcQs", 11);
    a.idpos = 10;
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_JOB, "online: the fetch starts at once");
    for (int i = 0; i < 300 && a.mode == M_JOB; i++) { app_tick(&a, 10); usleep(10000); }
    CHECK(a.mode == M_PLAYER && a.open >= 0 && !strcmp(a.songs[a.open].id, "dw4w9WgXcQs"), "the song it fetched is open (mode %d)", a.mode);
    app_free(&a);
    unsetenv("R3I6NOS_MUSICUE_SYNC");
    unsetenv("R3I6NOS_MUSICUE_ONLINE");
    unsetenv("R3I6NOS_MUSICUE_NOAUDIO");
}

/* A line too long for its cells scrolls in a closed loop after a hold, the way KlamANSI's does; one that fits, or with the clock off, does not. */
static void test_marquee(void) {
    unsigned char o[40], m[40];
    const char *t = "A long title that cannot fit";           /* 28 cells */
    r3_marq_on = 0;
    int n = r3_marquee_cells(2, 3, t, 10, o, m);
    CHECK(n == 10 && o[9] == 175 && o[0] == 'A', "clock off: cut with the guillemet");
    r3_marquee_clock(0);
    n = r3_marquee_cells(2, 3, t, 10, o, m);
    CHECK(n == 10 && o[0] == 'A' && o[1] == ' ', "clock on, just shown: holds at its start");
    for (int i = 0; i < R3_MARQUEE_HOLD; i++) r3_marquee_clock(R3_MARQUEE_TICK_MS);
    r3_marquee_cells(2, 3, t, 10, o, m);
    CHECK(o[0] == 'A', "still held after the hold's ticks");
    r3_marquee_clock(R3_MARQUEE_TICK_MS);
    r3_marquee_cells(2, 3, t, 10, o, m);
    CHECK(o[0] == ' ' && o[1] == 'l', "then it moves one cell a tick");
    for (int i = 0; i < 28 + 5 - 1 + 0; i++) r3_marquee_clock(R3_MARQUEE_TICK_MS);
    r3_marquee_cells(2, 3, t, 10, o, m);
    CHECK(o[0] == 'A' || o[0] == ' ' || o[0] == 4, "it loops");
    int diamonds = 0;
    for (int tick = 0; tick < 40; tick++) {
        r3_marquee_clock(R3_MARQUEE_TICK_MS);
        r3_marquee_cells(2, 3, t, 10, o, m);
        for (int i = 0; i < 10; i++) if (m[i]) { diamonds++; CHECK(o[i] == 4, "the marked cell is the diamond"); }
    }
    CHECK(diamonds > 0, "the loop's diamond comes round");
    CHECK(r3_marquee_cells(2, 3, "short", 10, o, m) == 5 && !m[0], "a line that fits is not scrolled");
    r3_marquee_cells(2, 3, "A different title that is also long", 10, o, m);
    CHECK(o[0] == 'A' && o[2] == 'd', "a new text starts at its beginning");
}

static void test_screens(void) {
    App a;
    app_init(&a, g_tmp);
    setenv("R3I6NOS_MUSICUE_NOAUDIO", "1", 1);
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s/media", g_tmp);
    mkdirs(dir);
    snprintf(dir, sizeof dir, "%s/media/Some Song [abcDEF12345].wav", g_tmp);
    write_wav(dir, 44100, 2, 16, 44100 * 2, 1);
    snprintf(dir, sizeof dir, "%s/media/Plain.WAV", g_tmp);
    write_wav(dir, 44100, 2, 16, 44100, 0);
    snprintf(dir, sizeof dir, "%s/media/notes.txt", g_tmp);
    FILE *f = fopen(dir, "w"); fputs("x", f); fclose(f);
    scan_songs(&a);
    CHECK(a.nsongs == 2, "two songs are found (the text file is not one): %d", a.nsongs);
    CHECK(!strcmp(a.songs[0].name, "Plain") && !strcmp(a.songs[1].id, "abcDEF12345") && !strncmp(a.songs[0].id, "local-", 6), "ids: from [id] in the name, else local-<hash>");

    for (int th = 0; th < R3_THEME_COUNT; th++) {
        a.theme = th;
        apply_theme(&a);
        a.mode = M_LIBRARY;
        check_contrast(&a, "library");
        open_demo(&a);
        engine_seek(&a.eng, 50);
        check_contrast(&a, "player");
        engine_seek(&a.eng, 5);
        check_contrast(&a, "player at the start");
        a.loop_state = 2; a.loop_a = 40; a.loop_b = 50;
        a.msel = 3;
        open_menu(&a);
        check_contrast(&a, "menu");
        a.mode = M_PLAYER;
        close_song(&a);
        a.mode = M_JOB; job_line(&a, "Fetching 3 of 5"); a.job_frac = 0.6;
        check_contrast(&a, "job");
        a.job_nlines = 0;
        id_open(&a);
        memcpy(a.idbuf, "dQw4w9WgXc", 10);
        a.idpos = 4;
        check_contrast(&a, "add id");
        a.mode = M_LIBRARY;
    }
    a.theme = r3_theme_find("Nord");
    apply_theme(&a);
    CHECK(pick_fg(R3_BLACK) == P_WHITE || r3_contrast_pal(P_BLACK, R3_BLACK) < r3_contrast_pal(P_WHITE, R3_BLACK), "text on a dark cell is white");
    for (int i = 0; i < RAMP; i++) CHECK(r3_contrast_pal(P_NEU0 + i, R3_BLACK) >= 4.5, "the fade step %d keeps 4.5:1 against the page", i);

    /* the real use: open a song, play, step markers, mark, loop, save, reopen */
    a.lsel = 1;
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_PLAYER && a.open == 1 && a.track.frames == 88200, "A opens the song");
    CHECK(a.cues.nm == 0, "no cue file yet");
    app_action(&a, R3_ACCEPT);
    CHECK(a.eng.playing, "A plays");
    for (int i = 0; i < 20; i++) app_tick(&a, 25);
    mark_here(&a);
    CHECK(a.cues.nm == 1 && fabs(a.cues.m[0].time - 0.5) < 0.05, "Select marks the playhead: %.3f", a.cues.nm ? a.cues.m[0].time : -1);
    save_cues(&a);
    char cj[PATH_MAX];
    cue_path(&a, "abcDEF12345", "json", cj, sizeof cj);
    CHECK(access(cj, R_OK) == 0, "the cues were saved to cues/<id>.json");
    a.offset = 0.1;
    save_offset(&a);
    app_action(&a, R3_BACK);
    CHECK(a.mode == M_LIBRARY && a.open < 0, "B goes back to the songs");
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_PLAYER && a.cues.nm == 1 && fabs(a.offset - 0.1) < 0.001, "the cues and the offset come back: %d markers, offset %.2f", a.cues.nm, a.offset);
    a.eng.pos = 0;
    app_action(&a, R3_PAGEDOWN);
    CHECK(fabs(engine_pos_sec(&a.eng) - (a.cues.m[0].time + 0.1)) < 0.01, "R1 jumps to the next marker, in the audio's time (offset applied)");
    app_action(&a, R3_Y);
    CHECK(a.loop_state == 1, "Y sets loop A");
    for (int i = 0; i < 12; i++) app_tick(&a, 25);
    app_action(&a, R3_Y);
    CHECK(a.loop_state == 2 && a.eng.loop, "Y again sets B and loops");
    app_action(&a, R3_Y);
    CHECK(a.loop_state == 0 && !a.eng.loop, "and once more clears it");
    app_action(&a, R3_MORE);
    CHECK(a.mode == M_MENU && a.menu.n > 4, "Start opens the menu");
    app_action(&a, R3_BACK);
    CHECK(a.mode == M_PLAYER, "B closes it");
    app_free(&a);
    /* the inbox: a bundle is imported, an unchanged one is not imported twice, a different one is kept as a conflict */
    App b;
    app_init(&b, g_tmp);
    char ib[PATH_MAX];
    snprintf(ib, sizeof ib, "%s/inbox", g_tmp);
    mkdirs(ib);
    CueSet cs;
    cue_init(&cs);
    snprintf(cs.videoid, sizeof cs.videoid, "zzzTEST0001");
    cue_add_marker(&cs, 12.0, "Chorus", 0);
    const CueSet *sets[1] = {&cs};
    CueBuf bb = {0};
    cue_write_bundle(sets, NULL, 1, &bb, 1000000000);
    snprintf(ib, sizeof ib, "%s/inbox/one.cuebundle", g_tmp);
    cue_write_file(ib, bb.p, bb.n);
    int conflicts = 0;
    CHECK(import_inbox(&b, &conflicts) == 1 && conflicts == 0, "a bundle in inbox/ is imported");
    CHECK(import_inbox(&b, &conflicts) == 0 && conflicts == 0, "the same bundle again changes nothing");
    cue_add_marker(&cs, 20.0, "Bridge", 0);
    cue_sort(&cs);
    free(bb.p); bb.p = NULL; bb.n = bb.cap = 0;
    cue_write_bundle(sets, NULL, 1, &bb, 1000000000);
    cue_write_file(ib, bb.p, bb.n);
    CHECK(import_inbox(&b, &conflicts) == 0 && conflicts == 1, "a different one is kept as a conflict, not written over");
    free(bb.p);
    cue_free(&cs);
    free(b.songs);
    unsetenv("R3I6NOS_MUSICUE_NOAUDIO");
}

static int selftest(void) {
    char tmpl[PATH_MAX];
    snprintf(tmpl, sizeof tmpl, "%s/musicue-selftest-XXXXXX", getenv("TMPDIR") && *getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    char *d = mkdtemp(tmpl);
    if (!d) { fprintf(stderr, "musicue: no temporary folder\n"); return 1; }
    g_tmp = d;
    g_bad = 0;
    int trace = getenv("R3I6NOS_MUSICUE_TRACE") != NULL;
    if (trace) fprintf(stderr, "[cues]\n");
    test_json_and_cues();
    if (trace) fprintf(stderr, "[audio]\n");
    test_audio();
    if (trace) fprintf(stderr, "[wave]\n");
    test_wave();
    if (trace) fprintf(stderr, "[screens]\n");
    test_screens();
    test_add_id();
    test_marquee();
    char cmd[PATH_MAX + 16];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", d);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", d);
    return g_bad;
}

/* ---- screenshots and the demo files ------------------------------------------------------------------------------------------------------- */

static int save_screenshot(const char *file, const char *screen, int theme) {
    App a;
    app_init(&a, "/nonexistent-musicue-data");
    if (theme >= 0) a.theme = theme;
    setenv("R3I6NOS_MUSICUE_NOAUDIO", "1", 1);
    apply_theme(&a);
    open_demo(&a);
    engine_seek(&a.eng, 41.3);
    a.loop_state = 2; a.loop_a = 33.0; a.loop_b = 46.5;
    a.msel = 3;
    if (!strcmp(screen, "library")) {
        close_song(&a);
        a.songs = (Song *)calloc(6, sizeof *a.songs);
        static const char *names[] = {"Blue Hour [aB3dE5fG7hI]", "Slow Dance in C", "Night Bus Jam [kL9mN2oP4qR]", "Practice take 4", "Waltz for Mina", "Old Riff"};
        for (int i = 0; i < 6; i++) { snprintf(a.songs[i].name, sizeof a.songs[i].name, "%s", names[i]); a.songs[i].has_cues = i % 3 != 1; }
        a.nsongs = 6; a.lsel = 2;
        snprintf(a.reply, sizeof a.reply, "6 songs. A opens one.");
        a.reply_style = R3_ST_HINT;
        a.mode = M_LIBRARY;
    } else if (!strcmp(screen, "menu")) {
        open_menu(&a);
    }
    R3Screen s;
    compose(&a, &s);
    uint32_t *px = (uint32_t *)malloc((size_t)R3_W * R3_H * 4);
    paint(&a, px, &s);
    SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(px, R3_W, R3_H, 32, R3_W * 4, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    int rc = surf && SDL_SaveBMP(surf, file) == 0 ? 0 : 1;
    if (rc) fprintf(stderr, "musicue: cannot save %s\n", file);
    if (surf) SDL_FreeSurface(surf);
    free(px);
    close_song(&a);
    return rc;
}

static int make_demo(const char *dir) {
    mkdirs(dir);
    Track t;
    if (!demo_track(&t)) return 1;
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/media/MusiCue demo [demoSong001].wav", dir);
    char md[PATH_MAX];
    snprintf(md, sizeof md, "%s/media", dir);
    mkdirs(md);
    FILE *f = fopen(p, "wb");
    if (!f) { track_free(&t); return 1; }
    uint32_t data = (uint32_t)(t.frames * 4), v;
    unsigned char h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0};
    v = 36 + data; memcpy(h + 4, &v, 4);
    v = TRACK_RATE; memcpy(h + 24, &v, 4);
    v = TRACK_RATE * 4; memcpy(h + 28, &v, 4);
    h[32] = 4; h[34] = 16;
    memcpy(h + 36, "data", 4); memcpy(h + 40, &data, 4);
    fwrite(h, 1, 44, f);
    fwrite(t.pcm, 4, (size_t)t.frames, f);
    fclose(f);
    track_free(&t);
    CueSet c;
    demo_cues(&c);
    CueBuf b = {0};
    cue_write_version(&c, &b, time(NULL));
    char cd[PATH_MAX];
    snprintf(cd, sizeof cd, "%s/cues", dir);
    mkdirs(cd);
    snprintf(p, sizeof p, "%s/cues/demoSong001.json", dir);
    int rc = cue_write_file(p, b.p, b.n);
    free(b.p);
    cue_free(&c);
    return rc;
}

/* ---- main ---------------------------------------------------------------------------------------------------------------------------------- */

static void open_pads(void) { for (int i = 0; i < SDL_NumJoysticks(); i++) SDL_JoystickOpen(i); }

static int wait_event(SDL_Event *e, int ms, int step) {
    Uint32 end = SDL_GetTicks() + (Uint32)ms;
    for (;;) {
        if (SDL_PollEvent(e)) return 1;
        int left = (int)(end - SDL_GetTicks());
        if (left <= 0) return 0;
        SDL_Delay((Uint32)(left < step ? left : step));
    }
}

int main(int argc, char **argv) {
    int selftest_run = 0, theme = -1;
    const char *shot = NULL, *screen = "player", *demo_dir = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) selftest_run = 1;
        else if (!strcmp(argv[i], "--version")) { puts("musicue " VERSION); return 0; }
        else if (!strcmp(argv[i], "--sync-path")) { char hb[PATH_MAX + 40]; puts(helper_path(hb, sizeof hb)); return 0; }
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--screen") && i + 1 < argc) screen = argv[++i];
        else if (!strcmp(argv[i], "--make-demo") && i + 1 < argc) demo_dir = argv[++i];
        else if (!strcmp(argv[i], "--theme") && i + 1 < argc) {
            theme = r3_theme_find(argv[++i]);
            if (theme < 0) {
                fprintf(stderr, "musicue: no theme called %s; the themes:", argv[i]);
                for (int t = 0; t < R3_THEME_COUNT; t++) fprintf(stderr, "%s \"%s\"", t ? "," : "", r3_themes[t].name);
                fputc('\n', stderr);
                return 2;
            }
        } else {
            fprintf(stderr, "usage: %s [--selftest | --version | --sync-path | --make-demo DIR | [--theme NAME] [--screen library|player|menu] --screenshot out.bmp]\n", argv[0]);
            return 2;
        }
    }
    if (demo_dir) return make_demo(demo_dir);
    if (shot) return save_screenshot(shot, screen, theme);
    if (selftest_run) {
        setenv("SDL_AUDIODRIVER", "dummy", 0);
        int bad = selftest();
        if (bad) { fprintf(stderr, "musicue: %d selftest check(s) failed\n", bad); return 1; }
        SDL_version v;
        SDL_GetVersion(&v);
        printf("R3I6NOS_SMOKE ok musicue=%s sdl=%d.%d.%d themes=%d\n", VERSION, v.major, v.minor, v.patch, R3_THEME_COUNT);
        return 0;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) { fprintf(stderr, "musicue: SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_ShowCursor(SDL_DISABLE);
    SDL_Window *win = SDL_CreateWindow("MusiCue", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, R3_W, R3_H, SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!win) { fprintf(stderr, "musicue: window: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    SDL_Renderer *r = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!r) r = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!r) { fprintf(stderr, "musicue: renderer: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    R3Video video;
    R3Toast toast;
    r3_toast_init(&toast);
    if (!r3_video_init(&video, r)) { fprintf(stderr, "musicue: texture: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

    App app;
    app_init(&app, "/roms/MusiCue");
    if (theme >= 0) app.theme = theme;
    apply_theme(&app);
    R3Screen splash;
    r3_splash(&splash, "MusiCue", VERSION, NULL, "Markers and cues on your songs", "Looking for your data", 50);
    for (int i = 0; i < R3_VIDEO_SETTLE; i++) r3_video_draw(&video, r, &splash, i == 0);
    open_pads();
    R3AppData data;
    memset(&data, 0, sizeof data);
    R3AppDataUI adui = {r, &video, &toast, NULL, NULL, NULL};
    r3_appdata_init(&data, "MusiCue", &adui);
    if (data.quit) { r3_video_free(&video); SDL_DestroyRenderer(r); SDL_DestroyWindow(win); SDL_Quit(); return 0; }
    snprintf(app.data, sizeof app.data, "%s", data.use);
    char sub[PATH_MAX];
    const char *dirs[] = {"media", "cues", "inbox", "export"};
    for (int i = 0; i < 4; i++) { sub_path(&app, dirs[i], NULL, sub, sizeof sub); mkdirs(sub); }
    load_settings(&app);
    if (theme >= 0) app.theme = theme;
    apply_theme(&app);
    scan_songs(&app);
    if (data.notice[0]) say(&app, R3_ST_WARN, "%s", data.notice);
    else say(&app, R3_ST_HINT, "%d song%s. A opens one, Start is the menu.", app.nsongs, app.nsongs == 1 ? "" : "s");

    R3Screen screen_cells;
    int force = 1;
    Uint32 last = SDL_GetTicks();
    while (!app.quit) {
        SDL_Event e;
        int busy = force || video.settle > 0 || app.eng.playing || r3_toast_active(&toast) || app.mode == M_JOB;
        int have = wait_event(&e, busy ? 16 : 250, busy ? 8 : 40);
        while (have) {
            switch (e.type) {
            case SDL_QUIT: app.quit = 1; break;
            case SDL_WINDOWEVENT: force = 1; break;
            case SDL_JOYDEVICEADDED: SDL_JoystickOpen(e.jdevice.which); break;
            case SDL_JOYBUTTONUP:
                if (r3_pad_action(e.jbutton.button) == R3_SELECT) {
                    if (!app.select_used && app.mode == M_PLAYER) mark_here(&app);
                    app.select_held = 0;
                }
                break;
            case SDL_JOYBUTTONDOWN: {
                if (r3_toast_key(&toast)) break;
                int act = r3_pad_action(e.jbutton.button);
                if (act == R3_SELECT) { app.select_held = 1; app.select_used = 0; break; }
                if (act == R3_MORE && app.select_held) { app.select_used = 1; app.quit = 1; break; }
                if (app.select_held) app.select_used = 1;
                if (act != R3_NONE) app_action(&app, act);
                break;
            }
            case SDL_KEYDOWN: {
                if (r3_toast_key(&toast)) break;
                SDL_Keycode k = e.key.keysym.sym;
                if (app.mode == M_ID && id_key(&app, k, e.key.keysym.mod)) break;      /* a keyboard types the id */
                int act = k == SDLK_x ? R3_X : k == SDLK_y ? R3_Y : k == SDLK_m && app.mode == M_PLAYER ? R3_NONE : r3_key_action(k);
                if (k == SDLK_m && app.mode == M_PLAYER) mark_here(&app);
                if (k == SDLK_SPACE) act = R3_ACCEPT;
                if (k == SDLK_q) app.quit = 1;
                if (act != R3_NONE) app_action(&app, act);
                break;
            }
            default: break;
            }
            have = SDL_PollEvent(&e);
        }
        Uint32 now = SDL_GetTicks();
        app_tick(&app, (double)(now - last));
        last = now;
        r3_toast_poll(&toast, now);
        compose(&app, &screen_cells);
        r3_toast_draw(&toast, &screen_cells);
        present(&app, &video, r, &screen_cells, force);
        force = 0;
    }
    app_free(&app);
    r3_video_free(&video);
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

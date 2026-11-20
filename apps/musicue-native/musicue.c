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
 * `--selftest` runs the checks and prints one R3I6NOS_SMOKE line; `--screenshot out.bmp [--screen library|player|player:N|menu] [--theme NAME]` draws a screen
 * with a synthetic demo song and no files; `--make-demo DIR` writes that song and its cue files; `--version`.
 * The R36S's rules still open for this app (none of the rules are missing; the unit has run none of them): ROADMAP section 36.
 * Nothing here has run on a real R36S. */

#define _GNU_SOURCE
#define VERSION "0.2"
#ifndef R3_BUILD_COMMIT
#define R3_BUILD_COMMIT "dev"           /* scripts/build_native_app.sh passes the git commit */
#endif
#define BUILD_LINE R3_BUILD_COMMIT " " __DATE__

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
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "r3i6nos_ansiapps.h"
#include "r3i6nos_themes.h"
#include "r3i6nos_input.h"
#include "r3i6nos_mainmenu.h"
#include "r3i6nos_keymap.h"
#include "r3i6nos_toast.h"
#include "r3i6nos_report.h"
#include "r3i6nos_demo.h"
#include "r3i6nos_appdata.h"
#include "cuedata.h"
#include "audio.h"
#include "wave.h"
#include "kt.h"

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

enum { M_LIBRARY, M_PLAYER, M_MENU, M_JOB, M_NOTE, M_ID, M_TEXT, M_KT };
enum { JOB_SYNC, JOB_RENDER, JOB_COMPRESS };                                                 /* what the job screen is running: the sync, or a Key and Tempo render */
enum { OV_NONE, OV_MM, OV_ABOUT, OV_THEME, OV_KEYMAP, OV_REPORT, OV_DEMO };                                      /* what shows over a screen: the main menu (L1+R1), About, the colour theme picker */
enum { MK_LIBRARY, MK_SONG, MK_CONTEXT };                                         /* the app's own menus: the songs' (Add, Sync ...), the song's (offset, save ...), the context menu (L2+R2) */
enum { TF_KEY, TF_TEMPO, TF_SIG, TF_SCRIPTER, TF_SUFFIX = 10 };                  /* what M_TEXT edits: a metadata field, or the number or letter after a marker's name */
#define JUMP_SLOTS 8                                                              /* the Jump panel's one-touch buttons: D-pad up, down, left, right, then A, B, X, Y */
#define REC_UNDO 64
#define JUMP_ROWS 9                                                               /* markers a page on the Jump panel (the tenth inner row is the A-B bar) */

/* The panels of rows 9-20: two show at a time, and Fn turns the carousel one place. */
enum { P_MARKERS, P_CUES, P_META, P_SETLIST, P_LAST, P_TOP, P_COUNT };
static const char *const PANEL_NAMES[P_COUNT] = {"Markers", "Cues", "Metadata", "Set List", "Last 10 cued", "Top 10 cued"};
typedef struct { char id[16]; char title[96]; } SetItem;                       /* a Set List entry: the song's id and its name */
typedef struct { char id[16]; char title[96]; long last; int count; } HistItem;   /* a song opened here: when last (a counter that only rises) and how many times */
#define SETLIST_MAX 200
#define HIST_MAX 200

typedef struct { char label[48]; char value[40]; int id; } MItem;
typedef struct { char title[40]; MItem it[16]; int n, sel; } Menu;

typedef struct {
    char data[PATH_MAX];                    /* <app data>: media/, cues/, inbox/, export/, settings.conf */
    int theme, any_license, save_video;
    int kt_aac;                             /* the copy's format: 1 AAC 192 kbps in an M4A (the default), 0 a WAV; settings.conf copy_format */
    Song *songs; int nsongs, lsel;
    int mode, back_mode;
    /* the open song */
    int open;                               /* index into songs, or -1 */
    Track track; Engine eng; WavePeaks peaks; CueSet cues;
    double offset;                          /* seconds the audio runs after the cue data's time */
    int msel;                               /* the marker the cursor is on */
    int csel;                               /* the cue the cursor is on (follows the cue playing unless the stick moved lately) */
    int pan_first, pan_focus, psel[P_COUNT];   /* the carousel: panel pan_first on the left, the next on the right; which of the two the stick is in; each panel's selection */
    double stick_idle_ms;                   /* since the right stick last moved a selection */
    R3Keymap km;                            /* the main menu's Keymap (help and test) */
    R3Input in;                             /* the mapping's reader (theme/r3i6nos_input.h): held modifiers, chords, the right stick */
    R3MainMenu mm; int overlay, theme_sel;  /* L1+R1's menu and what shows over a screen */
    int menu_kind;                          /* MK_*: which of the app's own menus M_MENU shows */
    int jump_view, prev_view;               /* the carousel's seventh place: the Jump panel on the whole width; and the view before this one, for R1+Fn (0-5 the pair starting there, 6 Jump) */
    int jsel; double jump_t[JUMP_SLOTS]; int jump_on[JUMP_SLOTS];   /* the Jump panel's marker selection; the marker (by time) each button jumps to */
    int rec, nrec; double rec_t[REC_UNDO];  /* Rec mode (L1+Start): D-pad Down adds a cue, Up takes the last one back; the cues added in this take */
    int auto_jump, auto_prev;               /* Fn while Play mode plays went to the Jump panel by itself; the view to give back when the music stops */
    int loop_off;                           /* an A-B loop is set (loop_state 2) but switched off (R1+Start) */
    int piano, piano_oct, piano_now[12], piano_vis;   /* the Piano panel over the Key field: octave, the notes held, and which keys show pressed */
    char suffix_base[48];                   /* TF_SUFFIX: the marker name the number or letter goes after */
    SetItem *sl; int nsl;                   /* the Set List (setlist.tsv) */
    HistItem *hist; int nhist; long hist_clock;   /* songs opened here (history.tsv): Last 10 and Top 10 cued */
    char tbuf[72]; int tlen, tpos, tmax, tfield; const char *talpha;   /* M_TEXT: a metadata field being typed */
    int loop_state; double loop_a, loop_b;  /* 0 none, 1 A set, 2 A and B (looping) */
    int next_label;
    int demo;                               /* the synthetic demo song (no files) */
    Menu menu;
    char reply[160]; int reply_style;
    int quit, force;
    /* the sync job */
    pid_t job_pid; int job_fd; char job_buf[512]; int job_len; char job_lines[6][96]; int job_nlines; double job_frac; int job_done;
    char idbuf[12]; int idpos, idgx, idgy;            /* Start > Add: the 11 characters of a YouTube id, 0 for an empty slot */
    char job_want[16];                      /* a sync for one id: open that song when it is done */
    KtPlan kt; int kt_workers, kt_cheaper;  /* M_KT: the Key and Tempo being asked for, and its two choices (2 or 4 workers; best or faster) */
    int job_kind;                           /* JOB_SYNC or JOB_RENDER */
    char kt_base_id[16], kt_copy_id[16], kt_out[PATH_MAX], kt_title[112], kt_key_txt[16], kt_tempo_txt[16]; double kt_ratio; int kt_semis;   /* the render in progress */
    char note[3][72];                       /* a message box (M_NOTE) */
    unsigned wave_sig;
    int pending_report;                     /* the Device report was asked for (and confirmed): the main loop runs it */
    int pending_tour; unsigned tour_mask;   /* Rec demo was asked for (the checklist's Start): the main loop runs it with these hero features */
    R3Checklist tour_ck;                    /* Rec demo's checklist of hero features (OV_DEMO) */
    double audio_idle_ms;                   /* how long nothing has sounded: the sound device is closed after 2 s (engine_sleep) */
} App;
static void demo_checklist_open(App *a);          /* Rec demo's checklist (musicue_demo.h) */
static int make_demo(const char *dir);            /* the demo song and its cue file on disk */

/* The Jump panel's button tags, and the Piano panel's keys: which button plays which note of the octave (C C# D D# E F F# G G# A A# B). */
static const char *const JUMP_TAGS[JUMP_SLOTS] = {"[^]", "[v]", "[<]", "[>]", "[A]", "[B]", "[X]", "[Y]"};

static const struct { int btn; const char *tag; } PIANO_KEYS[12] = {
    {R3_BTN_UP, "[^]"}, {R3_BTN_L1, "L1"}, {R3_BTN_LEFT, "[<]"}, {R3_BTN_L2, "L2"}, {R3_BTN_DOWN, "[v]"}, {R3_BTN_RIGHT, "[>]"},
    {R3_BTN_R2, "R2"}, {R3_BTN_Y, "[Y]"}, {R3_BTN_R1, "R1"}, {R3_BTN_B, "[B]"}, {R3_BTN_X, "[X]"}, {R3_BTN_A, "[A]"}};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static int jump_marker_of(const App *a, int slot);
static int loop_running(const App *a);

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

/* How many of the songs are Key and Tempo copies still stored as WAV (their id is kt-..., their file ends .wav): what Compress copies to AAC would turn into M4A. */
static int wav_copies(const App *a) {
    int n = 0;
    for (int i = 0; i < a->nsongs; i++) {
        const char *dot = strrchr(a->songs[i].path, '.');
        if (kt_is_copy_id(a->songs[i].id) && dot && !strcasecmp(dot, ".wav")) n++;
    }
    return n;
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
        else if (!strncmp(line, "save_video=", 11)) a->save_video = atoi(line + 11) ? 1 : 0;
        else if (!strncmp(line, "copy_format=", 12)) a->kt_aac = strncmp(line + 12, "wav", 3) != 0;
    }
    free(t);
}

static void save_settings(const App *a) {
    char p[PATH_MAX], buf[256];
    settings_path(a, p, sizeof p);
    snprintf(buf, sizeof buf, "theme=%s\nany_license=%d\nsave_video=%d\ncopy_format=%s\n", r3_themes[a->theme].name, a->any_license, a->save_video, a->kt_aac ? "aac" : "wav");
    cue_write_file(p, buf, strlen(buf));
}


/* ---- the Set List and the songs opened here (both in the app's data folder, so they follow rule 16's card choice) ---------------------------------- */

static void clean_title(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    for (; *src && n + 1 < cap; src++) dst[n++] = (*src == '\t' || *src == '\n' || *src == '\r') ? ' ' : *src;
    dst[n] = 0;
}

static void data_file(const App *a, const char *name, char *out, size_t cap) { snprintf(out, cap, "%s/%s", a->data, name); }

static void setlist_save(const App *a) {
    char p[PATH_MAX];
    data_file(a, "setlist.tsv", p, sizeof p);
    CueBuf b = {0};
    char line[160];
    for (int i = 0; i < a->nsl; i++) { snprintf(line, sizeof line, "%s\t%s\n", a->sl[i].id, a->sl[i].title); cb_str(&b, line); }
    if (!b.bad) cue_write_file(p, b.p ? b.p : "", b.n);
    free(b.p);
}

static void setlist_load(App *a) {
    free(a->sl);
    a->sl = (SetItem *)calloc(SETLIST_MAX, sizeof *a->sl);
    a->nsl = 0;
    char p[PATH_MAX];
    data_file(a, "setlist.tsv", p, sizeof p);
    char *t = cue_read_file(p, NULL);
    if (!t || !a->sl) { free(t); return; }
    for (char *line = strtok(t, "\n"); line && a->nsl < SETLIST_MAX; line = strtok(NULL, "\n")) {
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        if (!cue_videoid_ok(line) && strncmp(line, "local-", 6)) continue;
        SetItem *it = &a->sl[a->nsl++];
        snprintf(it->id, sizeof it->id, "%s", line);
        clean_title(it->title, sizeof it->title, tab + 1);
    }
    free(t);
}

static int setlist_find(const App *a, const char *id) {
    for (int i = 0; i < a->nsl; i++) if (!strcmp(a->sl[i].id, id)) return i;
    return -1;
}

/* Cue's "Add to Set List": the song goes on the end, or its entry is updated if it is there. Returns 1 added, 0 updated, -1 no room. */
static int setlist_add(App *a, const char *id, const char *title) {
    int i = setlist_find(a, id);
    if (i < 0) {
        if (!a->sl || a->nsl >= SETLIST_MAX) return -1;
        i = a->nsl++;
        snprintf(a->sl[i].id, sizeof a->sl[i].id, "%s", id);
        clean_title(a->sl[i].title, sizeof a->sl[i].title, title);
        setlist_save(a);
        return 1;
    }
    clean_title(a->sl[i].title, sizeof a->sl[i].title, title);
    setlist_save(a);
    return 0;
}

static void setlist_remove(App *a, int i) {
    if (i < 0 || i >= a->nsl) return;
    memmove(&a->sl[i], &a->sl[i + 1], (size_t)(a->nsl - i - 1) * sizeof *a->sl);
    a->nsl--;
    setlist_save(a);
}

static int setlist_move(App *a, int i, int dir) {                 /* the selected entry up or down one place; returns its new index */
    int j = i + dir;
    if (i < 0 || i >= a->nsl || j < 0 || j >= a->nsl) return i;
    SetItem t = a->sl[i];
    a->sl[i] = a->sl[j];
    a->sl[j] = t;
    setlist_save(a);
    return j;
}

static void hist_save(const App *a) {
    char p[PATH_MAX];
    data_file(a, "history.tsv", p, sizeof p);
    CueBuf b = {0};
    char line[200];
    for (int i = 0; i < a->nhist; i++) {
        snprintf(line, sizeof line, "%s\t%ld\t%d\t%s\n", a->hist[i].id, a->hist[i].last, a->hist[i].count, a->hist[i].title);
        cb_str(&b, line);
    }
    if (!b.bad) cue_write_file(p, b.p ? b.p : "", b.n);
    free(b.p);
}

static void hist_load(App *a) {
    free(a->hist);
    a->hist = (HistItem *)calloc(HIST_MAX, sizeof *a->hist);
    a->nhist = 0;
    a->hist_clock = 0;
    char p[PATH_MAX];
    data_file(a, "history.tsv", p, sizeof p);
    char *t = cue_read_file(p, NULL);
    if (!t || !a->hist) { free(t); return; }
    for (char *line = strtok(t, "\n"); line && a->nhist < HIST_MAX; line = strtok(NULL, "\n")) {
        char *f2 = strchr(line, '\t'), *f3 = f2 ? strchr(f2 + 1, '\t') : NULL, *f4 = f3 ? strchr(f3 + 1, '\t') : NULL;
        if (!f4) continue;
        *f2 = *f3 = *f4 = 0;
        if (!cue_videoid_ok(line) && strncmp(line, "local-", 6)) continue;
        HistItem *h = &a->hist[a->nhist++];
        snprintf(h->id, sizeof h->id, "%s", line);
        h->last = atol(f2 + 1);
        h->count = atoi(f3 + 1);
        if (h->count < 1) h->count = 1;
        clean_title(h->title, sizeof h->title, f4 + 1);
        if (h->last > a->hist_clock) a->hist_clock = h->last;
    }
    free(t);
}

/* A song was opened: it is the newest, and counted once more. The clock only rises, so two openings in one second still have an order. */
static void hist_touch(App *a, const char *id, const char *title) {
    if (!a->hist) return;
    int i = -1;
    for (int k = 0; k < a->nhist; k++) if (!strcmp(a->hist[k].id, id)) { i = k; break; }
    if (i < 0) {
        if (a->nhist == HIST_MAX) {                                    /* full: the one opened longest ago goes */
            int old = 0;
            for (int k = 1; k < a->nhist; k++) if (a->hist[k].last < a->hist[old].last) old = k;
            memmove(&a->hist[old], &a->hist[old + 1], (size_t)(a->nhist - old - 1) * sizeof *a->hist);
            a->nhist--;
        }
        i = a->nhist++;
        memset(&a->hist[i], 0, sizeof a->hist[i]);
        snprintf(a->hist[i].id, sizeof a->hist[i].id, "%s", id);
    }
    clean_title(a->hist[i].title, sizeof a->hist[i].title, title);
    a->hist[i].count++;
    long now = (long)time(NULL);
    a->hist_clock = now > a->hist_clock ? now : a->hist_clock + 1;
    a->hist[i].last = a->hist_clock;
    hist_save(a);
}

/* The indices of the songs opened here, newest first (Last 10) or most opened first (Top 10, the newer first among equals); returns how many (at most 10). */
static int hist_rank(const App *a, int top, int *out) {
    int n = 0, idx[HIST_MAX];
    for (int i = 0; i < a->nhist; i++) idx[n++] = i;
    for (int i = 1; i < n; i++) {                                      /* insertion sort: n is small */
        int v = idx[i], j = i - 1;
        for (; j >= 0; j--) {
            const HistItem *x = &a->hist[idx[j]], *y = &a->hist[v];
            int before = top ? (x->count > y->count || (x->count == y->count && x->last >= y->last)) : x->last >= y->last;
            if (before) break;
            idx[j + 1] = idx[j];
        }
        idx[j + 1] = v;
    }
    if (n > 10) n = 10;
    memcpy(out, idx, (size_t)n * sizeof *out);
    return n;
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
    a->loop_off = 0;
    a->rec = 0;
    a->nrec = 0;
    a->piano = 0;
    a->jump_view = 0;
    a->auto_jump = 0;
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
    if (a->loop_state == 2 && !a->loop_off) engine_loop_set(&a->eng, a->loop_a, a->loop_b); else engine_loop_clear(&a->eng);
}

static double dur_of(const App *a) { return a->track.pcm ? track_seconds(&a->track) : 0; }

/* ---- Tempo, as Cue does it (its computeGapTempo, classifyTempoDot, computeSuggestedTempoCluster, maybeSuggestTempo) ------------------------------ */

static int sig_valid(const char *sig) { return !strcmp(sig, "4/4") || !strcmp(sig, "6/8"); }
static int sig_beats(const App *a) { return !strcmp(a->cues.meta.sig, "6/8") ? 8 : 16; }       /* beats in a cue: 16 in 4/4, 8 in 6/8; anything else counts as 4/4 */

/* "" and "?" are a Tempo nobody has set: the suggestion fills it. Otherwise the number, or 0 when what is there is not a positive number. */
static int tempo_unset(const App *a) { return !a->cues.meta.tempo[0] || !strcmp(a->cues.meta.tempo, "?"); }
static double set_tempo(const App *a) {
    char *end;
    double t = strtod(a->cues.meta.tempo, &end);
    return !tempo_unset(a) && end != a->cues.meta.tempo && isfinite(t) && t > 0 ? t : 0;
}

/* A cue's own tempo from its length (its start to the next cue's start): the length is 16 beats (4/4) or 8 (6/8), so BPM = 60 / (gap / beats). 0: none. */
static double gap_tempo(const App *a, double gap) {
    if (!(gap > 0)) return 0;
    double bpm = 60.0 / (gap / sig_beats(a));
    return isfinite(bpm) && bpm > 0 ? bpm : 0;
}

enum { DOT_NONE, DOT_GREEN, DOT_YELLOW, DOT_RED };
static int classify_dot(double cue_tempo, double tempo) {          /* within 2 BPM green, within 5 yellow, else red */
    if (cue_tempo <= 0 || !(tempo > 0)) return DOT_NONE;
    double d = fabs(cue_tempo - tempo);
    return d > 5 ? DOT_RED : d > 2 ? DOT_YELLOW : DOT_GREEN;
}

/* The suggestion: the most common tempo among the cues' own (the largest group within 2 BPM of one of them, the first found on a tie), averaged to at most 2 decimals;
 * in[i] is set for the cues of that group (cue i measured against cue i+1). Returns 0 when there is nothing to go on (fewer than two cues). */
static int suggest_cluster(const App *a, double *tempo, unsigned char *in) {
    int n = a->cues.nc;
    if (n < 2) return 0;
    double *t = (double *)malloc((size_t)n * sizeof *t);
    if (!t) return 0;
    int m = 0, best = 0, best_at = -1;
    for (int i = 0; i + 1 < n; i++) { t[i] = gap_tempo(a, a->cues.c[i + 1].start - a->cues.c[i].start); if (t[i] > 0) m++; }
    if (!m) { free(t); return 0; }
    for (int i = 0; i + 1 < n; i++) {
        if (t[i] <= 0) continue;
        int k = 0;
        for (int j = 0; j + 1 < n; j++) if (t[j] > 0 && fabs(t[j] - t[i]) <= 2) k++;
        if (k > best) { best = k; best_at = i; }
    }
    double sum = 0;
    if (in) memset(in, 0, (size_t)n);
    for (int j = 0; j + 1 < n; j++) if (t[j] > 0 && fabs(t[j] - t[best_at]) <= 2) { sum += t[j]; if (in) in[j] = 1; }
    *tempo = floor(sum / best * 100 + 0.5) / 100;
    free(t);
    return 1;
}

static void fmt_tempo(double t, char *out, size_t cap) {          /* the shortest text of a number with at most 2 decimals, as JavaScript's String(number) */
    snprintf(out, cap, "%.2f", t);
    char *dot = strchr(out, '.');
    if (dot) { char *e = out + strlen(out); while (e > dot && e[-1] == '0') *--e = 0; if (e == dot + 1) *dot = 0; }
}

/* Cue's maybeSuggestTempo: a Tempo that is blank or "?" is filled from the cues (never one a person typed). Returns 1 when it changed. */
static int maybe_suggest_tempo(App *a) {
    double t;
    char v[16];
    if (!tempo_unset(a) || !suggest_cluster(a, &t, NULL)) return 0;
    fmt_tempo(t, v, sizeof v);
    cue_meta_set(&a->cues, "tempo", v);
    return 1;
}

/* The dot of cue i (the last has none): measured against the Tempo a person set, or, while it is unset, the suggestion (the cluster's cues green). */
static int cue_dot(const App *a, int i, double tempo, const unsigned char *cluster, double suggested) {
    if (i < 0 || i + 1 >= a->cues.nc) return DOT_NONE;
    double ct = gap_tempo(a, a->cues.c[i + 1].start - a->cues.c[i].start);
    if (ct <= 0) return DOT_NONE;
    if (cluster) return cluster[i] ? DOT_GREEN : (fabs(ct - suggested) > 5 ? DOT_RED : DOT_YELLOW);
    return classify_dot(ct, tempo);
}

/* The Assisted Cue entry's model: the average length of the green cues and the last cue's start; 0 when it is not available (Cue's computeAssistModel). */
static int assist_model(const App *a, double *avg, double *anchor) {
    double tempo = set_tempo(a), sum = 0;
    int n = 0;
    if (tempo <= 0 || a->cues.nc < 2) return 0;
    for (int i = 1; i < a->cues.nc; i++) {
        double gap = a->cues.c[i].start - a->cues.c[i - 1].start;
        if (classify_dot(gap_tempo(a, gap), tempo) == DOT_GREEN) { sum += gap; n++; }
    }
    if (!n) return 0;
    *avg = sum / n;
    *anchor = a->cues.c[a->cues.nc - 1].start;
    return 1;
}


/* Open song i: decode it, read its cue files. Returns 1, or 0 with the reason in the reply row. */
/* The Jump panel's bindings are one line each in cues/<id>.jump ("<slot> <seconds>"): the marker is found by its time, so renaming or moving others does not matter. */
static void jump_load(App *a) {
    memset(a->jump_on, 0, sizeof a->jump_on);
    memset(a->jump_t, 0, sizeof a->jump_t);
    if (a->open < 0) return;
    char path[PATH_MAX];
    cue_path(a, a->songs[a->open].id, "jump", path, sizeof path);
    char *t = cue_read_file(path, NULL);
    if (!t) return;
    for (char *line = strtok(t, "\n"); line; line = strtok(NULL, "\n")) {
        int slot;
        double tm;
        if (sscanf(line, "%d %lf", &slot, &tm) == 2 && slot >= 0 && slot < JUMP_SLOTS && tm >= 0) { a->jump_on[slot] = 1; a->jump_t[slot] = tm; }
    }
    free(t);
}

static void jump_save(const App *a) {
    if (a->open < 0) return;
    char path[PATH_MAX], buf[JUMP_SLOTS * 40];
    size_t n = 0;
    for (int i = 0; i < JUMP_SLOTS; i++) if (a->jump_on[i]) n += (size_t)snprintf(buf + n, sizeof buf - n, "%d %.3f\n", i, a->jump_t[i]);
    cue_path(a, a->songs[a->open].id, "jump", path, sizeof path);
    cue_write_file(path, buf, n);
}

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
    a->cues.dirty = maybe_suggest_tempo(a);                  /* Cue fills an unset Tempo when cues load; saved with the cues when the song closes */
    hist_touch(a, s->id, s->name);
    a->offset = 0;
    cue_path(a, s->id, "offset", path, sizeof path);
    if ((t = cue_read_file(path, NULL))) { a->offset = atof(t); free(t); if (fabs(a->offset) > 30) a->offset = 0; }
    s->nmarkers = a->cues.nm;
    a->msel = 0;
    a->loop_state = 0;
    a->loop_off = 0;
    a->rec = 0;
    a->nrec = 0;
    a->piano = 0;
    a->jump_view = 0;
    a->jsel = 0;
    jump_load(a);
    a->next_label = 0;
    engine_open(&a->eng, &a->track);
    a->csel = 0;
    if (a->track.truncated) say(a, R3_ST_WARN, "Only the first %d minutes are loaded.", TRACK_MAX_SECONDS / 60);
    else if (a->cues.meta.sig[0] && !sig_valid(a->cues.meta.sig)) say(a, R3_ST_WARN, "Sig %s is not 4/4 or 6/8: counted as 4/4 (Metadata > Sig).", a->cues.meta.sig);
    else if (!a->cues.nm && !a->cues.nc) say(a, R3_ST_HINT, "No cues for this song yet: L1+Start records them.");
    else say(a, R3_ST_HINT, "%d markers, %d cues. Start plays, L1+Start records.", a->cues.nm, a->cues.nc);
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
        if (b % 4) continue;                                          /* a cue every four bars (16 beats at 100 bpm: every cue is a green one) */
        snprintf(txt, sizeof txt, "%s", (b / 4) % 3 == 2 ? "(hold the chord)" : prog);
        cue_add_caption(c, st, 0, 0, txt);
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
    a->jsel = 0;
    memset(a->jump_on, 0, sizeof a->jump_on);
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
        R3Box b = {1, "Songs", right[0] ? right : NULL, "A open   L1+R1 menu   B quit", 0};
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
        R3Box b = {1, "Songs", NULL, "L1+R1 menu   B quit", 0};
        r3_box(s, 0, 4, R3_COLS, 13, &b);
        r3_text_fit(s, 2, 6, "No songs yet.", 58, R3_ST(R3_ST_STRONG));
        r3_text_fit(s, 2, 8, "Put audio files in MusiCue/media/ on the card (m4a, mp3,", 58, R3_ST(R3_ST_BODY));
        r3_text_fit(s, 2, 9, "ogg, opus, flac, wav), and Cue's .cuebundle files in", 58, R3_ST(R3_ST_BODY));
        r3_text_fit(s, 2, 10, "MusiCue/inbox/. L1+R1 > Import reads them.", 58, R3_ST(R3_ST_BODY));
        r3_text_fit(s, 2, 12, "L1+R1 > Demo song plays a built-in one.", 58, R3_ST(R3_ST_HINT));
    }
    r3_fill(s, 0, 17, R3_COLS, 4, 32, R3_LIGHT_GRAY, R3_BLACK);
    r3_text_fit(s, 1, 18, a->nsongs ? "A song is decoded into memory when opened: jumps and loops are instant." : "", 62, R3_ST(R3_ST_BODY));
    R3Cmd cmd = {a->reply, a->reply_style, '>', "", 0, a->nsongs ? "Songs: media/ on the data card" : "Nothing in media/", "L1+R1: menu", R3_ST_HINT};
    r3_command_line(s, &cmd);
}

static void menu_clear(Menu *m, const char *title) { memset(m, 0, sizeof *m); snprintf(m->title, sizeof m->title, "%s", title); }
static void menu_add(Menu *m, int id, const char *label, const char *value) {
    if (m->n >= 16) return;
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

/* ---- the cue panel (rows 7-8): Cue's own rules (its web app's captions, beat dot and Assisted Cue entry) ---------------------------------------- */

static double cap_end(const App *a, int i) {
    const CueCaption *c = &a->cues.c[i];
    if (c->has_end) return c->end;
    return i + 1 < a->cues.nc ? a->cues.c[i + 1].start : c->start + 6;           /* until the next cue, or 6 s for the last */
}

/* The cue playing at vt: its start has passed and its end has not. */
static int active_caption(const App *a, double vt) {
    for (int i = 0; i < a->cues.nc; i++) if (vt >= a->cues.c[i].start && vt < cap_end(a, i)) return i;
    return -1;
}

static void compose_cue_panel(App *a, R3Screen *s, double vt) {
    r3_fill(s, 0, 7, R3_COLS, 2, 32, R3_ST(R3_ST_BODY));
    int idx = active_caption(a, vt);
    if (idx >= 0) r3_text_scroll(s, 1, 7, a->cues.c[idx].text, 59, R3_ST(R3_ST_STRONG));
    else if (!a->cues.nc) r3_text_fit(s, 1, 7, "No cues yet. D-pad Down stamps one.", 59, R3_ST(R3_ST_HINT));
    /* the beat dot at the right of the first row: pulses once a beat from the start of the cue playing, while the song plays and the Tempo is known */
    double tempo = set_tempo(a);
    if (a->eng.playing && a->cues.nc && tempo > 0) {
        double beat = 60.0 / tempo, anchor = a->cues.c[0].start;
        for (int i = 0; i < a->cues.nc; i++) if (a->cues.c[i].start <= vt) anchor = a->cues.c[i].start; else break;
        double off = fmod(vt - anchor, beat);
        if (off < 0) off += beat;
        double ph = off / beat, d = (ph < 0.5 ? ph : 1 - ph) * 2;                /* 0 on the beat, 1 half way between: Cue's pulse fades out and back */
        if (d < 0.5) r3_put(s, 62, 7, 10, R3_LIGHT_GREEN, R3_BLACK);
        else r3_put(s, 62, 7, 7, R3_GREEN, R3_BLACK);
    }
    /* the second row: the Assisted Cue entry when it applies, else the next cue (muted, only while a cue is playing) */
    double avg = 0, anchor = 0;
    int assist = a->eng.playing && assist_model(a, &avg, &anchor) && vt >= anchor - 0.25 && dur_of(a) - anchor >= avg;
    if (assist) {
        double p = 1 - (vt - anchor) / avg;
        if (p < 0) p = 0;
        if (p > 1) p = 1;
        int k = (int)(p * 30 + 0.5);                                              /* each half is 30 cells; the fill hugs the middle and shrinks toward it */
        for (int i = 0; i < 30; i++) {
            int lc = 1 + i, rc = 62 - i, fl = lc >= 31 - k, fr = rc <= 32 + k;   /* i counts out from the edges: the middle ones are filled last */
            int bg = P_ACC0 + i * (RAMP - 1) / 29;
            if (fl) r3_put(s, lc, 8, 32, pick_fg(bg), bg); else r3_put(s, lc, 8, 176, R3_ST(R3_ST_HINT));
            if (fr) r3_put(s, rc, 8, 32, pick_fg(bg), bg); else r3_put(s, rc, 8, 176, R3_ST(R3_ST_HINT));
        }
    } else if (idx >= 0 && idx + 1 < a->cues.nc) {
        r3_text_scroll(s, 1, 8, a->cues.c[idx + 1].text, 62, R3_ST(R3_ST_HINT));
    }
}

/* ---- rows 9-20: the panels. Two show at a time (the carousel), the stick moves in the one with the double frame --------------------------------------- */

static int marker_at(const App *a, double t) {                     /* the marker on a cue's time (Cue matches by time, within 10 ms) */
    for (int i = 0; i < a->cues.nm; i++) if (fabs(a->cues.m[i].time - t) < 0.01) return i;
    return -1;
}

static int panel_at(const App *a, int slot) { return (a->pan_first + slot) % P_COUNT; }

static int panel_count(const App *a, int p) {
    int idx[10];
    switch (p) {
    case P_MARKERS: return a->cues.nm;
    case P_CUES: return a->cues.nc;
    case P_META: return 5;
    case P_SETLIST: return a->nsl;
    case P_LAST: return hist_rank(a, 0, idx);
    case P_TOP: return hist_rank(a, 1, idx);
    }
    return 0;
}

static int *panel_sel(App *a, int p) { return p == P_MARKERS ? &a->msel : p == P_CUES ? &a->csel : &a->psel[p]; }

static void sel_bar(R3Screen *s, int col, int y, int w) {
    r3_fill(s, col, y, w, 1, 32, R3_BLACK, P_ACC0);
    grad_h(s, col, y, w, P_ACC0, 0, RAMP - 1);
}

/* The song in songs[] with this id, or -1 (a song in a list can be one that is not on this card). */
static int song_by_id(const App *a, const char *id) {
    for (int i = 0; i < a->nsongs; i++) if (!strcmp(a->songs[i].id, id)) return i;
    return -1;
}

static const char *const META_NAMES[5] = {"Key", "Tempo", "Sig", "Scripter", "Created"};

static const char *meta_value(const App *a, int i) {
    static char shown[64];
    const char *v = i == 0 ? a->cues.meta.key : i == 1 ? a->cues.meta.tempo : i == 2 ? a->cues.meta.sig : i == 3 ? a->cues.meta.scripter : a->cues.meta.created;
    if (i == 2 && !v[0]) return "4/4";
    if (i == 4) { snprintf(shown, sizeof shown, "%.10s", v[0] ? v : ""); return v[0] ? shown : "on export"; }
    return v[0] ? v : "?";
}

static void compose_panel(App *a, R3Screen *s, int slot, double vt, int cm) {
    const int p = panel_at(a, slot), col = slot * 32, focus = a->pan_focus == slot, n = panel_count(a, p), per = 10;
    int *sel = panel_sel(a, p);
    if (*sel >= n) *sel = n ? n - 1 : 0;
    int page, pages;
    r3_list_window(n, *sel, per, &page, &pages);
    char right[32] = "", foot[40] = "";
    if (pages > 1) snprintf(right, sizeof right, "Page %d/%d", page, pages);
    if (p == P_CUES && !right[0] && set_tempo(a) > 0) { char tv[16]; fmt_tempo(set_tempo(a), tv, sizeof tv); snprintf(right, sizeof right, "%s bpm", tv); }
    if (focus) snprintf(foot, sizeof foot, "%s", p == P_META ? "A edit" : p == P_SETLIST || p == P_LAST || p == P_TOP ? "A open" : "A jump");
    R3Box b = {focus, PANEL_NAMES[p], right[0] ? right : NULL, foot[0] ? foot : NULL, 0};
    r3_box(s, col, 9, 32, 12, &b);
    const int x0 = col + 1, wi = 30, first = (page - 1) * per;
    int rank[10];
    if (p == P_LAST || p == P_TOP) hist_rank(a, p == P_TOP, rank);
    if (!n && p != P_META) {
        const char *l1 = p == P_MARKERS ? "No markers yet." : p == P_CUES ? "No cues yet." : p == P_SETLIST ? "The Set List is empty." : "Nothing opened yet.";
        const char *l2 = p == P_MARKERS ? "Bookmark a cue in the Cues" : p == P_CUES ? "D-pad Down stamps one" : p == P_SETLIST ? "L1+R1 > Add this song" : "Songs you open show here.";
        const char *l3 = p == P_MARKERS ? "panel (L1+R1 menu)." : p == P_CUES ? "at the playhead." : "";
        r3_text_fit(s, x0 + 1, 11, l1, wi - 2, R3_ST(R3_ST_BODY));
        r3_text_fit(s, x0 + 1, 12, l2, wi - 2, R3_ST(R3_ST_HINT));
        r3_text_fit(s, x0 + 1, 13, l3, wi - 2, R3_ST(R3_ST_HINT));
        return;
    }
    double tempo = set_tempo(a), sug = 0;
    unsigned char *cluster = NULL;
    if (p == P_CUES && tempo <= 0 && a->cues.nc >= 2 && a->cues.nc <= 400) {        /* no Tempo set: the cues are judged against their own most common tempo */
        cluster = (unsigned char *)calloc((size_t)a->cues.nc, 1);
        if (cluster && !suggest_cluster(a, &sug, cluster)) { free(cluster); cluster = NULL; }
    }
    for (int k = 0; k < per; k++) {
        int i = first + k, y = 10 + k;
        if (i >= n) break;
        int on = i == *sel && focus;
        if (i == *sel) {
            if (focus) sel_bar(s, x0, y, wi);
            else r3_put(s, x0, y, 16, R3_LIGHT_GRAY, R3_BLACK);                   /* the selection of the panel the stick is not in: a pointer only */
        }
        int fgs = on ? -1 : R3_LIGHT_GRAY;
        (void)fgs;
        if (p == P_MARKERS) {
            int hue = hue_of(a->cues.m[i].label);
            char tm[16];
            fmt_time_s(a->cues.m[i].time, tm, sizeof tm);
            int tw = r3_width(tm);
            if (i == cm) r3_put(s, x0, y, 16, on ? pick_fg(s->bg[y * R3_COLS + x0]) : R3_LIGHT_GREEN, -1);
            r3_put(s, x0 + 1, y, 32, pick_fg(P_HUE0 + hue), P_HUE0 + hue);
            (on ? gscroll : gtext)(s, x0 + 3, y, a->cues.m[i].label, wi - 3 - tw - 2, on ? -1 : (i == cm ? R3_WHITE : R3_LIGHT_GRAY));
            gtext(s, x0 + wi - 1 - tw, y, tm, tw, on ? -1 : R3_LIGHT_CYAN);
        } else if (p == P_CUES) {
            char tm[16];
            fmt_time_s(a->cues.c[i].start, tm, sizeof tm);
            int dot = cue_dot(a, i, tempo, cluster, sug), ci = cur_caption(a, vt);
            if (i == ci) r3_put(s, x0, y, 16, on ? pick_fg(s->bg[y * R3_COLS + x0]) : R3_LIGHT_GREEN, -1);
            if (dot) { int bg = dot == DOT_GREEN ? R3_LIGHT_GREEN : dot == DOT_YELLOW ? R3_YELLOW : R3_LIGHT_RED; r3_put(s, x0 + 1, y, 32, R3_BLACK, bg); }
            gtext(s, x0 + 3, y, tm, 5, on ? -1 : R3_LIGHT_CYAN);
            if (marker_at(a, a->cues.c[i].start) >= 0) r3_put(s, x0 + 9, y, 4, on ? pick_fg(s->bg[y * R3_COLS + x0 + 9]) : R3_LIGHT_CYAN, -1);
            (on ? gscroll : gtext)(s, x0 + 11, y, a->cues.c[i].text, wi - 11 - 1, on ? -1 : (i == ci ? R3_WHITE : R3_LIGHT_GRAY));
        } else if (p == P_META) {
            gtext(s, x0 + 1, y, META_NAMES[i], 9, on ? -1 : R3_LIGHT_GRAY);
            (on ? gscroll : gtext)(s, x0 + 11, y, meta_value(a, i), wi - 12, on ? -1 : (i == 4 ? R3_LIGHT_GRAY : R3_WHITE));
        } else if (p == P_SETLIST) {
            char num[8];
            snprintf(num, sizeof num, "%d", i + 1);
            gtext(s, x0 + 1, y, num, 3, on ? -1 : R3_LIGHT_CYAN);
            int here = a->open >= 0 && !strcmp(a->songs[a->open].id, a->sl[i].id);
            if (here) r3_put(s, x0 + 4, y, 16, on ? pick_fg(s->bg[y * R3_COLS + x0 + 4]) : R3_LIGHT_GREEN, -1);
            (on ? gscroll : gtext)(s, x0 + 5, y, a->sl[i].title, wi - 6, on ? -1 : R3_LIGHT_GRAY);
        } else {
            const HistItem *h = &a->hist[rank[i]];
            int si = song_by_id(a, h->id), top = p == P_TOP;
            char cnt[12];
            snprintf(cnt, sizeof cnt, "x%d", h->count);
            int cw = top ? r3_width(cnt) : 0;
            if (si >= 0 && a->songs[si].has_cues) r3_put(s, x0, y, 13, on ? pick_fg(s->bg[y * R3_COLS + x0]) : R3_LIGHT_CYAN, -1);   /* a note: its cues are on this card */
            (on ? gscroll : gtext)(s, x0 + 2, y, h->title, wi - 3 - (cw ? cw + 1 : 0), on ? -1 : R3_LIGHT_GRAY);
            if (cw) gtext(s, x0 + wi - 1 - cw, y, cnt, cw, on ? -1 : R3_LIGHT_CYAN);
        }
    }
    if (p == P_META) {                                                              /* under the five fields: what Cue's Tempo logic makes of the cues */
        char l[64];
        double t2;
        if (tempo_unset(a) && suggest_cluster(a, &t2, NULL)) { char tv[16]; fmt_tempo(t2, tv, sizeof tv); snprintf(l, sizeof l, "Suggested tempo %s bpm", tv); }
        else if (a->cues.nc < 2) snprintf(l, sizeof l, "Two cues give a tempo.");
        else snprintf(l, sizeof l, "Tempo set: erase to suggest.");
        r3_text_fit(s, x0 + 1, 16, l, wi - 2, R3_ST(R3_ST_HINT));
        snprintf(l, sizeof l, "%d cues, %d markers", a->cues.nc, a->cues.nm);
        r3_text_fit(s, x0 + 1, 17, l, wi - 2, R3_ST(R3_ST_HINT));
        if (!sig_valid(a->cues.meta.sig) && a->cues.meta.sig[0]) r3_text_fit(s, x0 + 1, 18, "Sig counts as 4/4: A on it.", wi - 2, R3_ST(R3_ST_WARN));
    }
    free(cluster);
}

/* The Jump panel: every marker on the whole width, with Cue's playback-progress fill (its updateMarkerProgressFills): the marker's own section, from its time to the next
 * marker's, fills left to right while the playhead is in it, and the last marker has no section. The tags are the buttons bound to a marker; the bottom row is Cue's
 * A-B progress (its updateTempLoopCProgress), filling only while the loop is on. */
static void compose_jump(App *a, R3Screen *s, double vt, int cm) {
    const int n = a->cues.nm, per = JUMP_ROWS, x0 = 1, wi = R3_COLS - 2;
    if (a->jsel >= n) a->jsel = n ? n - 1 : 0;
    int page, pages;
    r3_list_window(n, a->jsel, per, &page, &pages);
    char right[32] = "";
    if (pages > 1) snprintf(right, sizeof right, "Page %d/%d", page, pages);
    R3Box b = {1, "Jump", right[0] ? right : NULL, a->eng.playing ? "a button jumps" : "a button binds", 0};
    r3_box(s, 0, 9, R3_COLS, 12, &b);
    int cur = -1;
    double frac = 0;
    for (int i = 0; i + 1 < n; i++)
        if (vt >= a->cues.m[i].time && vt < a->cues.m[i + 1].time) { cur = i; frac = (vt - a->cues.m[i].time) / (a->cues.m[i + 1].time - a->cues.m[i].time); break; }
    if (!n) {
        r3_text_fit(s, x0 + 1, 11, "No markers yet.", wi - 2, R3_ST(R3_ST_BODY));
        r3_text_fit(s, x0 + 1, 12, "Bookmark a cue in the Cues panel, or Mark here", wi - 2, R3_ST(R3_ST_HINT));
        r3_text_fit(s, x0 + 1, 13, "(L2+R2 menu). Then bind buttons to the markers.", wi - 2, R3_ST(R3_ST_HINT));
    }
    const double lo = a->loop_a, hi = a->loop_b;
    const double dur = dur_of(a);
    for (int k = 0; k < per; k++) {
        int i = (page - 1) * per + k, y = 10 + k;
        if (i >= n) break;
        int on = i == a->jsel, hue = hue_of(a->cues.m[i].label);
        if (on) sel_bar(s, x0, y, wi);
        if (i == cur) {
            int fillw = (int)(frac * wi + 0.5);
            for (int c = 0; c < fillw && c < wi; c++) r3_put(s, x0 + c, y, 32, pick_fg(P_HUE0 + hue), P_HUE0 + hue);
        }
        int bold = on || i == cur;
        int fg = bold ? -1 : R3_LIGHT_GRAY;
        char tm[16], tags[JUMP_SLOTS * 3 + 1] = "";
        fmt_time_s(a->cues.m[i].time, tm, sizeof tm);
        for (int sl = 0; sl < JUMP_SLOTS; sl++) if (jump_marker_of(a, sl) == i) strncat(tags, JUMP_TAGS[sl], sizeof tags - strlen(tags) - 1);
        int tw = (int)strlen(tags);
        if (i == cm) r3_put(s, x0, y, 16, bold ? pick_fg(s->bg[y * R3_COLS + x0]) : R3_LIGHT_GREEN, -1);
        gtext(s, x0 + 2, y, tm, 5, bold ? -1 : R3_LIGHT_CYAN);
        double end = i + 1 < n ? a->cues.m[i + 1].time : dur;
        int looping = loop_running(a) && a->cues.m[i].time + a->offset < hi && end + a->offset > lo;
        if (looping) r3_put(s, x0 + wi - tw - 2, y, 'L', bold ? pick_fg(s->bg[y * R3_COLS + x0 + wi - tw - 2]) : R3_LIGHT_CYAN, -1);
        (on ? gscroll : gtext)(s, x0 + 8, y, a->cues.m[i].label, wi - 8 - tw - 3, fg);
        if (tw) gtext(s, x0 + wi - tw, y, tags, tw, bold ? -1 : R3_LIGHT_CYAN);
    }
    /* the A-B row */
    const int y = 10 + per;
    if (a->loop_state >= 1) {
        char ta[16], tb[16], l[40];
        fmt_time(a->loop_a, ta, sizeof ta);
        if (a->loop_state == 2) fmt_time(a->loop_b, tb, sizeof tb); else snprintf(tb, sizeof tb, "...");
        snprintf(l, sizeof l, "A %s", ta);
        r3_text_fit(s, x0 + 1, y, l, 9, R3_ST(R3_ST_BODY));
        const int bx = x0 + 11, bw = 36;
        double pos = engine_pos_sec(&a->eng), f = loop_running(a) && hi > lo ? (pos - lo) / (hi - lo) : 0;
        if (f < 0) f = 0;
        if (f > 1) f = 1;
        int full = (int)(f * bw + 0.5);
        for (int c = 0; c < bw; c++) {
            if (c < full) { int bg = P_ACC0 + c * (RAMP - 1) / (bw - 1); r3_put(s, bx + c, y, 32, pick_fg(bg), bg); }
            else r3_put(s, bx + c, y, 176, R3_ST(R3_ST_HINT));
        }
        snprintf(l, sizeof l, "B %s%s", tb, a->loop_state == 2 && a->loop_off ? " off" : "");
        r3_text_fit(s, bx + bw + 2, y, l, wi - (bx + bw + 2) + 1, R3_ST(R3_ST_BODY));
    } else r3_text_fit(s, x0 + 1, y, "R1+Up sets A  R1+Down sets B  R1+Start loops  R1+Select to A", wi - 2, R3_ST(R3_ST_HINT));
}

static void compose_panels(App *a, R3Screen *s, double vt, int cm) {
    if (a->jump_view) { compose_jump(a, s, vt, cm); return; }
    compose_panel(a, s, 0, vt, cm);
    compose_panel(a, s, 1, vt, cm);
}

static void compose_player(App *a, R3Screen *s) {
    r3_clear(s);
    const double dur = dur_of(a), pos = engine_pos_sec(&a->eng), vt = pos - a->offset;
    const char *title = a->demo ? "Demo song" : (a->open >= 0 ? a->songs[a->open].name : "");
    char t1[24], t2[24], right[48];
    fmt_time_s(pos, t1, sizeof t1);
    fmt_time_s(dur, t2, sizeof t2);
    snprintf(right, sizeof right, "%s%s / %s", a->rec ? "REC " : "", t1, t2);
    compose_topbar(a, s, "MusiCue", right);
    int tw = r3_width(right);
    gscroll(s, 11, 0, title, R3_COLS - 14 - tw, -1);

    /* rows 1-6: the waveform (pixels, drawn by the presenter); the cells under it are its colour, for anything that mirrors the text */
    int wbg = P_NEU0;
    (void)wbg;
    r3_fill(s, 0, 1, R3_COLS, 6, 32, R3_LIGHT_GRAY, R3_BLACK);

    /* rows 7-8: the cue panel, as Cue's: the line now, the next one muted, the tempo dot, and the Assisted Cue entry's two bars draining to the middle */
    int cm = cur_marker(a, vt);
    compose_cue_panel(a, s, vt);

    compose_panels(a, s, vt, cm);

    /* rows 21-23: the command line: what just happened, the keys, and where the next marker is */
    char status[128] = "";
    if (cm >= 0 && cm + 1 < a->cues.nm) snprintf(status, sizeof status, "%s, then %s in %.0fs", a->cues.m[cm].label, a->cues.m[cm + 1].label, a->cues.m[cm + 1].time - vt);
    else if (cm >= 0) snprintf(status, sizeof status, "%s", a->cues.m[cm].label);
    if (a->loop_state || a->offset != 0) {                          /* what the transport row used to say */
        char extra[64] = "", la[16], lb[16];
        if (a->loop_state) {
            fmt_time(a->loop_a, la, sizeof la);
            if (a->loop_state == 2) fmt_time(a->loop_b, lb, sizeof lb); else snprintf(lb, sizeof lb, "...");
            snprintf(extra, sizeof extra, "Loop%s A %s B %s", a->loop_state == 2 && a->loop_off ? " off" : "", la, lb);
        }
        if (a->offset != 0) { size_t n = strlen(extra); snprintf(extra + n, sizeof extra - n, "%soffset %+.2fs", n ? "  " : "", a->offset); }
        char all[128];
        snprintf(all, sizeof all, "%s%s%s", extra, status[0] ? "  " : "", status);
        snprintf(status, sizeof status, "%s", all);
    }
    const char *keys = a->jump_view ? "Stick picks  buttons bind/jump  Fn panels  L1+R1 menu"
                     : a->rec && a->eng.playing ? "Down cue  Up undo  Left/Right cue  Start stop  Fn panels"
                     : "Start play  L1+Start rec  Left/Right cue  Fn panels  L1+R1 menu";
    R3Cmd cmd = {a->reply, a->reply_style, a->rec && a->eng.playing ? '*' : a->eng.playing ? '>' : '|', keys, 0, status,
                 a->eng.noaudio && !a->demo && !a->eng.stepped ? "no sound device" : "", R3_ST_WARN};
    r3_command_line(s, &cmd);
}

static void compose_job(App *a, R3Screen *s) {
    r3_clear(s);
    compose_topbar(a, s, "MusiCue", a->job_kind == JOB_RENDER ? "Copy" : a->job_kind == JOB_COMPRESS ? "Compress" : "Sync");
    R3Box b = {1, a->job_kind == JOB_RENDER ? "Making a copy at the new Key and Tempo" : a->job_kind == JOB_COMPRESS ? "Compressing copies to AAC" : "Getting songs and cues", NULL, a->job_done ? "B close" : "B stop", 0};
    r3_box(s, 2, 4, 60, 14, &b);
    int full = (int)(56 * a->job_frac + 0.5);
    for (int i = 0; i < 56; i++) {
        if (i < full) { int bg = P_ACC0 + i * (RAMP - 1) / 55; r3_put(s, 4 + i, 6, 32, pick_fg(bg), bg); }
        else r3_put(s, 4 + i, 6, 176, R3_ST(R3_ST_HINT));
    }
    for (int i = 0; i < a->job_nlines && i < 6; i++) r3_text_scroll(s, 4, 8 + i, a->job_lines[i], 56, R3_ST(i == a->job_nlines - 1 ? R3_ST_STRONG : R3_ST_BODY));
    R3Cmd cmd = {a->reply, a->reply_style, '>', "", 0, a->job_done ? "A or B closes this list" : a->job_kind == JOB_RENDER ? "B stops it and takes the half copy away" : a->job_kind == JOB_COMPRESS ? "B stops it; copies already done stay done" : "Your own list: MusiCue/inbox/urls.txt", "", R3_ST_HINT};
    r3_command_line(s, &cmd);
}

static void compose_kt(App *a, R3Screen *s) {
    r3_clear(s);
    compose_topbar(a, s, "MusiCue", "Key and Tempo");
    R3Box b = {1, "A copy at another Key and Tempo", NULL, "A make it   B back", 0};
    r3_box(s, 3, 3, 58, 17, &b);
    const Song *sg = &a->songs[a->open];
    char line[96], bk[16], tk[16], base_t[16], tgt_t[16];
    r3_text_fit(s, 5, 5, sg->name, 54, R3_ST(R3_ST_STRONG));
    kt_key_name(&a->kt.base, bk, sizeof bk);
    kt_key_name(&a->kt.target, tk, sizeof tk);
    fmt_tempo(a->kt.base_tempo, base_t, sizeof base_t);
    fmt_tempo(a->kt.target_tempo, tgt_t, sizeof tgt_t);
    r3_text_fit(s, 5, 7, "              Key          Tempo", 54, R3_ST(R3_ST_HINT));
    snprintf(line, sizeof line, "The song      %-12s %s bpm", bk, base_t);
    r3_text_fit(s, 5, 8, line, 54, R3_ST(R3_ST_BODY));
    snprintf(line, sizeof line, "The copy    < %-3s >      ^ %s bpm v", tk, tgt_t);
    int bg = P_ACC0 + RAMP / 2;
    r3_text_fit(s, 5, 9, line, 54, R3_ST(R3_ST_STRONG));
    for (int i = 0; i < 7; i++) r3_put(s, 17 + i, 9, (unsigned char)line[12 + i], pick_fg(bg), bg);   /* the Key being set */
    const char *why = kt_update(&a->kt);
    snprintf(line, sizeof line, "%+d semitone%s, tempo x%.3f (%.1f%%)", a->kt.semis, a->kt.semis == 1 || a->kt.semis == -1 ? "" : "s", a->kt.ratio, a->kt.ratio * 100);
    r3_text_fit(s, 5, 11, line, 54, R3_ST(R3_ST_BODY));
    if (why) r3_text_fit(s, 5, 12, why, 54, R3_ST(R3_ST_WARN));
    else if (a->kt.ratio < 0.75 || a->kt.ratio > 1.5) r3_text_fit(s, 5, 12, "It sounds best between 75% and 150% of the tempo.", 54, R3_ST(R3_ST_WARN));
    snprintf(line, sizeof line, "About %.0f s: %d workers, %s quality%s.", kt_estimate_s(dur_of(a), a->kt_workers, a->kt_cheaper) + (a->kt_aac ? kt_encode_s(dur_of(a) / a->kt.ratio) : 0), a->kt_workers, a->kt_cheaper ? "faster" : "best", a->kt_aac ? ", then AAC" : "");
    r3_text_fit(s, 5, 14, line, 54, R3_ST(R3_ST_BODY));
    snprintf(line, sizeof line, "The copy: %s, about %.1f MiB.", a->kt_aac ? "AAC 192 kbps" : "WAV (lossless)", kt_copy_bytes(dur_of(a), a->kt.ratio, a->kt_aac) / 1048576.0);
    r3_text_fit(s, 5, 15, line, 54, R3_ST(R3_ST_BODY));
    r3_text_fit(s, 5, 17, "Left/Right Key  Up/Down Tempo  R1+Up/Down 5 bpm", 54, R3_ST(R3_ST_HINT));
    r3_text_fit(s, 5, 18, "X workers  Y quality  Select format  R1+Left own", 54, R3_ST(R3_ST_HINT));
    R3Cmd cmd = {a->reply, a->reply_style, '>', "", 0, "All four cores for a minute; the song stays as it is", "", R3_ST_HINT};
    r3_command_line(s, &cmd);
}

static const char ID_ALPHA[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
#define ID_COLS 16

static void compose_id(App *a, R3Screen *s) {
    r3_clear(s);
    compose_topbar(a, s, "MusiCue", "Add");
    R3Box b = {1, "Add: YouTube ID", NULL, "A next/fetch   B cancel", 0};
    r3_box(s, 8, 5, 48, 14, &b);
    r3_text_fit(s, 10, 7, "The 11 characters after v= in a YouTube link.", 44, R3_ST(R3_ST_BODY));
    r3_text_fit(s, 10, 8, "Capitals and small letters are different.", 44, R3_ST(R3_ST_HINT));
    for (int i = 0; i < 11; i++) {
        int col = 15 + i * 3, ch = a->idbuf[i] ? a->idbuf[i] : '_';
        if (i == a->idpos) { int bg = P_ACC0 + RAMP / 2; r3_put(s, col, 11, ch, pick_fg(bg), bg); }
        else r3_put(s, col, 11, ch, R3_ST(a->idbuf[i] ? R3_ST_STRONG : R3_ST_HINT));
    }
    for (int i = 0; i < 64; i++) {                                                   /* the on-screen keyboard: 16 x 4 keys, the D-pad moves, A types */
        int gx = i % ID_COLS, gy = i / ID_COLS, col = 10 + gx * 2 + (gx >= 8), row = 13 + gy;
        if (gx == a->idgx && gy == a->idgy) { int bg = P_ACC0 + RAMP / 2; r3_put(s, col, row, ID_ALPHA[i], pick_fg(bg), bg); }
        else r3_put(s, col, row, ID_ALPHA[i], R3_ST(R3_ST_BODY));
    }
    r3_text_fit(s, 10, 17, "D-pad pick   A type   X/Y erase   Start fetch", 44, R3_ST(R3_ST_HINT));
    R3Cmd cmd = {a->reply, a->reply_style, '>', "", 0, "Fetches the cues and the audio, then opens it", "", R3_ST_HINT};
    r3_command_line(s, &cmd);
}

/* The Piano panel over the Key field: one octave C3 to B3 (the octave moves with Select and Start); each key shows the button that plays it. */
static void compose_piano(App *a, R3Screen *s) {
    const int bx = 12, by = 10, bw = 40, bh = 11, kx = bx + 2, ky = by + 2;
    char title[32], line[48];
    snprintf(title, sizeof title, "Piano  C%d-B%d", a->piano_oct, a->piano_oct);
    R3Box b = {1, title, NULL, "Fn closes", 0};
    r3_box(s, bx, by, bw, bh, &b);
    static const int white[7] = {0, 2, 4, 5, 7, 9, 11};
    static const int black_after[7] = {1, 3, -1, 6, 8, 10, -1};
    const int press_bg = P_ACC0 + RAMP / 2;
    for (int i = 0; i < 7; i++) {                                                    /* white keys: 4 cells and a dark divider, 5 rows */
        int key = white[i], x = kx + i * 5, bg = a->piano_now[key] ? press_bg : R3_WHITE;
        for (int r = 0; r < 5; r++) {
            r3_fill(s, x, ky + r, 4, 1, 32, pick_fg(bg), bg);
            r3_put(s, x + 4, ky + r, 32, R3_WHITE, R3_DARK_GRAY);
        }
        snprintf(line, sizeof line, "%s%d", NOTE_NAME[key], a->piano_oct);
        for (int c = 0; line[c] && c < 4; c++) r3_put(s, x + c, ky + 3, (unsigned char)line[c], pick_fg(bg), bg);
        for (int c = 0; PIANO_KEYS[key].tag[c] && c < 4; c++) r3_put(s, x + c, ky + 4, (unsigned char)PIANO_KEYS[key].tag[c], pick_fg(bg), bg);
    }
    for (int i = 0; i < 7; i++) {                                                    /* black keys: 3 cells over the divider, 3 rows */
        int key = black_after[i];
        if (key < 0) continue;
        int x = kx + i * 5 + 3, bg = a->piano_now[key] ? press_bg : R3_BLACK;
        for (int r = 0; r < 3; r++) r3_fill(s, x, ky + r, 3, 1, 32, pick_fg(bg), bg);
        for (int c = 0; NOTE_NAME[key][c]; c++) r3_put(s, x + c, ky + 1, (unsigned char)NOTE_NAME[key][c], pick_fg(bg), bg);
        for (int c = 0; PIANO_KEYS[key].tag[c] && c < 3; c++) r3_put(s, x + c, ky + 2, (unsigned char)PIANO_KEYS[key].tag[c], pick_fg(bg), bg);
    }
    snprintf(line, sizeof line, "Select Oct-   Start Oct+   Key: %s", a->tbuf[0] ? a->tbuf : "?");
    r3_text_fit(s, bx + 2, by + bh - 2, line, bw - 4, R3_ST(R3_ST_HINT));
}

static void compose_text(App *a, R3Screen *s) {
    r3_clear(s);
    const int suffix = a->tfield == TF_SUFFIX;
    compose_topbar(a, s, "MusiCue", suffix ? "Name" : "Metadata");
    char title[64], label[96];
    if (suffix) snprintf(title, sizeof title, "Name: %s", a->suffix_base);
    else snprintf(title, sizeof title, "Edit: %s", META_NAMES[a->tfield == TF_SCRIPTER ? TF_SCRIPTER : a->tfield]);
    const int top = a->piano ? 2 : 5, h = a->piano ? 8 : 12;                    /* the Piano takes the lower half, so the field's box shrinks to the upper */
    R3Box b = {1, title, NULL, "A keep   B cancel", 0};
    r3_box(s, 2, top, 60, h, &b);
    const int ly = top + 4;
    int from = a->tpos > 40 ? a->tpos - 40 : 0;                               /* the line scrolls with the cursor: a field can be longer than the box */
    for (int i = 0; i < 56; i++) {
        int k = from + i;
        int ch = k < a->tlen ? (unsigned char)a->tbuf[k] : (k == a->tpos ? 32 : 0);
        if (k == a->tpos) { int bg = P_ACC0 + RAMP / 2; r3_put(s, 4 + i, ly, ch ? ch : 32, pick_fg(bg), bg); }
        else if (ch) r3_put(s, 4 + i, ly, ch, R3_ST(R3_ST_STRONG));
        else if (k < a->tmax) r3_put(s, 4 + i, ly, 250, R3_ST(R3_ST_HINT));
    }
    r3_text_fit(s, 4, top + 2, suffix ? "The number or letter after the name: 1, 2, A, 1a ..." : a->tfield == TF_TEMPO ? "Beats per minute. Erase it and keep for a suggestion." : a->tfield == TF_KEY ? "The musical key, e.g. C or Am. Fn shows the piano." : "Who made these cues.", 56, R3_ST(R3_ST_BODY));
    if (a->piano) r3_text_fit(s, 4, top + 6, "Play the note. Fn closes the piano, then A keeps.", 56, R3_ST(R3_ST_HINT));
    else if (suffix) {
        snprintf(label, sizeof label, "Marker: %s%s%s", a->suffix_base, a->tlen ? " " : "", a->tbuf);
        r3_text_fit(s, 4, top + 6, label, 56, R3_ST(R3_ST_STRONG));
        r3_text_fit(s, 4, top + 7, "Up/Down number or letter   Right next place", 56, R3_ST(R3_ST_HINT));
        r3_text_fit(s, 4, top + 8, "Y erase   A keep   B cancel", 56, R3_ST(R3_ST_HINT));
    } else {
        r3_text_fit(s, 4, top + 7, "Up/Down letter   R1+Up/Down jump 8   Left/Right move", 56, R3_ST(R3_ST_HINT));
        r3_text_fit(s, 4, top + 8, "Y erase   A keep   a keyboard types it", 56, R3_ST(R3_ST_HINT));
    }
    if (a->piano) compose_piano(a, s);
    R3Cmd cmd = {a->reply, a->reply_style, '>', "", 0, "", "", R3_ST_HINT};
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
        compose_menu_box(&a->menu, s, a->back_mode == M_PLAYER ? 7 : 5);      /* below the waveform when there is one */
        break;
    case M_JOB: compose_job(a, s); break;
    case M_KT: compose_kt(a, s); break;
    case M_NOTE: compose_note(a, s); break;
    case M_ID: compose_id(a, s); break;
    case M_TEXT: compose_text(a, s); break;
    default: compose_library(a, s);
    }
    if (a->overlay == OV_MM) r3_mm_draw(s, &a->mm, r3_themes[a->theme].name);
    else if (a->overlay == OV_ABOUT) {
        static const char *const lines[] = {"The R36S port of Cue (chordica.com/cue):", "markers and cues on a song, A-B loops, Set Lists.", "r3i6nos-ansiapps theme, MIT; the font is CC BY-SA 4.0.", "Songs and cues stay on the card; nothing is sent.", NULL};
        r3_about_draw(s, "MusiCue", VERSION, BUILD_LINE, lines);
    } else if (a->overlay == OV_THEME) r3_theme_pick_draw(s, a->theme_sel, a->theme);
    else if (a->overlay == OV_KEYMAP) r3_km_draw(s, &a->km);
    else if (a->overlay == OV_DEMO) r3_checklist_draw(s, &a->tour_ck, 5);
    else if (a->overlay == OV_REPORT) {
        r3_box(s, 6, 6, 52, 12, &(R3Box){1, "Device report", NULL, "A run  B cancel", 0});
        r3_text_fit(s, 8, 8, "Writes r36s-report.txt in MusiCue's data folder.", 48, R3_ST(R3_ST_BODY));
        r3_text_fit(s, 8, 9, "It opens songs, plays the demo, draws the screen and", 48, R3_ST(R3_ST_BODY));
        r3_text_fit(s, 8, 10, "makes a Key and Tempo copy: about two minutes, no", 48, R3_ST(R3_ST_BODY));
        r3_text_fit(s, 8, 11, "input, all four cores for part of it.", 48, R3_ST(R3_ST_BODY));
        r3_text_fit(s, 8, 13, "For the battery's draw, unplug the charger first.", 48, R3_ST(R3_ST_STRONG));
        r3_text_fit(s, 8, 14, "Test files are made in the folder and removed.", 48, R3_ST(R3_ST_HINT));
    }
}

/* ---- presenting: the cells, with the waveform's pixels laid over rows 1-6 ----------------------------------------------------------------- */

static unsigned wave_signature(App *a) {
    if (a->mode != M_PLAYER && !(a->mode == M_MENU && a->back_mode == M_PLAYER)) return 0;
    double dur = dur_of(a);
    unsigned h = 2166136261u;
    h = (h ^ (unsigned)wave_x(engine_pos_sec(&a->eng), dur)) * 16777619u;
    h = (h ^ (unsigned)a->loop_state) * 16777619u;
    h = (h ^ (unsigned)a->overlay) * 16777619u;
    h = (h ^ (unsigned)(a->loop_a * 100) ^ (unsigned)(a->loop_b * 100) << 3) * 16777619u;
    h = (h ^ (unsigned)a->cues.nm) * 16777619u;
    h = (h ^ (unsigned)(a->offset * 1000)) * 16777619u;
    for (int i = 0; i < a->cues.nm; i++) h = (h ^ (unsigned)(a->cues.m[i].time * 100)) * 16777619u;
    return h | 1u;
}

/* The waveform's pixels are laid over the cells on the player and on a menu over it, but not under the two boxes that reach into its rows. */
static int wave_visible(const App *a) {
    return (a->mode == M_PLAYER || (a->mode == M_MENU && a->back_mode == M_PLAYER)) && a->overlay != OV_THEME && a->overlay != OV_ABOUT;
}

static void paint(App *a, uint32_t *px, const R3Screen *s) {
    r3_render(s, px);
    if (wave_visible(a)) {
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

enum { A_MARK = 1, A_LABEL, A_DELETE, A_SAVE, A_EXPORT, A_OFFSET, A_SYNC, A_IMPORT, A_DEMO, A_LICENSE, A_VIDEO, A_ADD, A_CUE, A_BOOKMARK, A_CUEDEL, A_RECALC, A_SLADD, A_SLDEL, A_SLUP, A_SLDOWN, A_QUIT, A_CLOSE, A_JUMPCLEAR, A_KT, A_COMPRESS, A_LABELPICK };

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
    say(a, R3_ST_OK, "Marked %s at %.1fs. L1+R1 > Name it.", label, vt);
}

/* Cue's "Cue" button (here D-pad Down in Rec mode, and the context menu's Cue here): a cue at the playhead, reading "- - - -" until its words are typed. The Tempo is suggested again from the cues (only if it is unset). */
static void cue_here(App *a) {
    double vt = engine_pos_sec(&a->eng) - a->offset;
    if (vt < 0) vt = 0;
    if (cue_add_caption(&a->cues, vt, 0, 0, "- - - -") < 0) { say(a, R3_ST_ERROR, "No room for another cue."); return; }
    if (a->rec && a->nrec < REC_UNDO) a->rec_t[a->nrec++] = vt;                 /* in Rec mode, so D-pad Up can take it back */
    cue_sort(&a->cues);
    for (int i = 0; i < a->cues.nc; i++) if (fabs(a->cues.c[i].start - vt) < 0.0005) a->csel = i;
    a->stick_idle_ms = 1e9;                                     /* the cursor goes to the new cue and follows the playing one */
    int changed = maybe_suggest_tempo(a);
    say(a, R3_ST_OK, "Cue at %.1fs (%d cues).%s", vt, a->cues.nc, changed ? " Tempo suggested." : "");
}

/* Delete cue i, and the marker on its time (a marker cannot outlive the cue it is on, as in Cue); the Tempo is suggested again if it is unset. */
static void cue_delete(App *a, int i) {
    if (i < 0 || i >= a->cues.nc) return;
    int m = marker_at(a, a->cues.c[i].start);
    if (m >= 0) cue_del_marker(&a->cues, m);
    memmove(&a->cues.c[i], &a->cues.c[i + 1], (size_t)(a->cues.nc - i - 1) * sizeof *a->cues.c);
    a->cues.nc--;
    a->cues.dirty = 1;
    if (a->csel >= a->cues.nc) a->csel = a->cues.nc ? a->cues.nc - 1 : 0;
    if (a->msel >= a->cues.nm) a->msel = a->cues.nm ? a->cues.nm - 1 : 0;
    maybe_suggest_tempo(a);
    say(a, R3_ST_HINT, "Cue deleted.");
}

/* Cue's marker box on a cue row: a marker on the cue's time, named by its words (or "Mark n" while they are still "- - - -"); again, and it is gone. */
static void cue_bookmark(App *a, int i) {
    if (i < 0 || i >= a->cues.nc) return;
    double t = a->cues.c[i].start;
    int m = marker_at(a, t);
    if (m >= 0) {
        cue_del_marker(&a->cues, m);
        if (a->msel >= a->cues.nm) a->msel = a->cues.nm ? a->cues.nm - 1 : 0;
        say(a, R3_ST_HINT, "Marker removed.");
        return;
    }
    char label[64];
    if (strcmp(a->cues.c[i].text, "- - - -") && a->cues.c[i].text[0]) snprintf(label, sizeof label, "%.40s", a->cues.c[i].text);
    else snprintf(label, sizeof label, "Mark %d", a->cues.nm + 1);
    if (cue_add_marker(&a->cues, t, label, 0) < 0) { say(a, R3_ST_ERROR, "No room for another marker."); return; }
    cue_sort(&a->cues);
    say(a, R3_ST_OK, "Marker: %s.", label);
}

/* A-B looping (R1+D-pad Up sets A, R1+D-pad Down sets B, R1+Start switches the loop on and off, R1+Select goes to A). */
static void loop_set_a(App *a) {
    double pos = engine_pos_sec(&a->eng);
    char t[16];
    fmt_time(pos, t, sizeof t);
    a->loop_a = pos;
    if (a->loop_state == 2 && a->loop_b - pos < 0.1) { a->loop_state = 1; a->loop_off = 0; set_loop_engine(a); say(a, R3_ST_WARN, "A is past B: set B again (R1+Down)."); return; }
    int had = a->loop_state == 2;
    if (a->loop_state == 0) a->loop_state = 1;
    set_loop_engine(a);
    say(a, R3_ST_OK, had ? "A moved to %s." : "A set at %s. R1+Down sets B.", t);
}

static void loop_set_b(App *a) {
    double pos = engine_pos_sec(&a->eng);
    char t[16];
    if (a->loop_state == 0) { say(a, R3_ST_WARN, "Set A first (R1+Up)."); return; }
    if (pos - a->loop_a < 0.1) { say(a, R3_ST_WARN, "B must be after A."); return; }
    a->loop_b = pos;
    a->loop_state = 2;
    a->loop_off = 0;
    set_loop_engine(a);
    fmt_time(pos, t, sizeof t);
    say(a, R3_ST_OK, "B set at %s: looping A to B. R1+Start stops it.", t);
}

static void loop_toggle(App *a) {
    if (a->loop_state != 2) { say(a, R3_ST_WARN, "No A-B loop yet: R1+Up sets A, R1+Down sets B."); return; }
    a->loop_off = !a->loop_off;
    set_loop_engine(a);
    if (a->loop_off) { say(a, R3_ST_HINT, "A-B loop off. R1+Start starts it again."); return; }
    double pos = engine_pos_sec(&a->eng);
    if (pos < a->loop_a || pos >= a->loop_b) engine_seek(&a->eng, a->loop_a);
    say(a, R3_ST_OK, "Looping A to B.");
}

static void loop_goto_a(App *a) {
    if (a->loop_state == 0) { say(a, R3_ST_WARN, "No A yet: R1+Up sets it."); return; }
    engine_seek(&a->eng, a->loop_a);
    say(a, R3_ST_HINT, "At A.");
}

static int loop_running(const App *a) { return a->loop_state == 2 && !a->loop_off; }

/* Cue navigation (D-pad Left and Right in both modes, Up and Down in Play mode): the cue's own time, in the audio's clock. */
static void cue_goto(App *a, int i) {
    engine_seek(&a->eng, a->cues.c[i].start + a->offset);
    a->csel = i;
    a->stick_idle_ms = 1e9;
    say(a, R3_ST_HINT, "Cue %d of %d: %s", i + 1, a->cues.nc, a->cues.c[i].text);
}

static void cue_prev(App *a) {
    if (!a->cues.nc) { say(a, R3_ST_WARN, "No cues yet."); return; }
    double vt = engine_pos_sec(&a->eng) - a->offset;
    int ci = -1;
    for (int i = 0; i < a->cues.nc; i++) if (a->cues.c[i].start <= vt + 0.0005) ci = i;
    int target = ci >= 0 && vt - a->cues.c[ci].start >= 1.5 ? ci : ci - 1;          /* inside a cue's first 1.5 s: the one before; later: its own start (as a player's Previous) */
    if (target < 0) { engine_seek(&a->eng, 0); say(a, R3_ST_HINT, "The start."); }
    else cue_goto(a, target);
}

static void cue_next(App *a) {
    double vt = engine_pos_sec(&a->eng) - a->offset;
    for (int i = 0; i < a->cues.nc; i++) if (a->cues.c[i].start > vt + 0.05) { cue_goto(a, i); return; }
    say(a, R3_ST_HINT, a->cues.nc ? "No later cue." : "No cues yet.");
}

static void cue_edge(App *a, int last) {
    if (!a->cues.nc) { say(a, R3_ST_WARN, "No cues yet."); return; }
    cue_goto(a, last ? a->cues.nc - 1 : 0);
}

/* Start: play and stop. L1+Start: play in Rec mode. Playing, either one stops (and ends Rec mode). */
static void transport(App *a, int rec) {
    if (a->eng.playing) {
        engine_play(&a->eng, 0);
        if (a->rec) say(a, R3_ST_HINT, "Rec stopped: %d cue%s added this take.", a->nrec, a->nrec == 1 ? "" : "s");
        a->rec = 0;
        return;
    }
    a->rec = rec;
    a->nrec = 0;
    if (rec) say(a, R3_ST_OK, "Rec: D-pad Down cues, Up takes the last back, Start stops.");
    engine_play(&a->eng, 1);
}

static void rec_undo(App *a) {
    while (a->nrec > 0) {
        double t = a->rec_t[--a->nrec];
        for (int i = 0; i < a->cues.nc; i++)
            if (fabs(a->cues.c[i].start - t) < 0.0005) {
                char tm[16];
                fmt_time(t, tm, sizeof tm);
                cue_delete(a, i);
                say(a, R3_ST_HINT, "Took back the cue at %s.", tm);
                return;
            }
    }
    say(a, R3_ST_WARN, "Nothing to take back: only the cues added in this take.");
}

/* The Jump panel's buttons: D-pad up, down, left, right, then A, B, X, Y. Stopped, a press binds the marker the right stick has picked (again on the same one unbinds);
 * playing, it moves the playhead to the marker bound. */
static int jump_marker_of(const App *a, int slot) { return a->jump_on[slot] ? marker_at(a, a->jump_t[slot]) : -1; }

static void jump_button(App *a, int slot) {
    int m = jump_marker_of(a, slot);
    if (a->eng.playing) {
        if (m < 0) { say(a, R3_ST_WARN, "%s has no marker: stop, pick one with the right stick, press it.", JUMP_TAGS[slot]); return; }
        engine_seek(&a->eng, a->cues.m[m].time + a->offset);
        a->jsel = m;
        say(a, R3_ST_HINT, "%s %s", JUMP_TAGS[slot], a->cues.m[m].label);
        return;
    }
    if (!a->cues.nm) { say(a, R3_ST_WARN, "No markers to bind: bookmark a cue first."); return; }
    if (a->jsel >= a->cues.nm) a->jsel = a->cues.nm - 1;
    if (m == a->jsel) { a->jump_on[slot] = 0; say(a, R3_ST_HINT, "%s is free again.", JUMP_TAGS[slot]); }
    else { a->jump_on[slot] = 1; a->jump_t[slot] = a->cues.m[a->jsel].time; say(a, R3_ST_OK, "%s jumps to %s.", JUMP_TAGS[slot], a->cues.m[a->jsel].label); }
    jump_save(a);
}

static void seek_rel(App *a, double d) { engine_seek(&a->eng, engine_pos_sec(&a->eng) + d); }

/* ---- the panels' actions ------------------------------------------------------------------------------------------------------------------------- */

static void open_by_id(App *a, const char *id, const char *title) {
    int i = song_by_id(a, id);
    if (i < 0) { say(a, R3_ST_WARN, "%s is not on this card.", title); return; }
    save_cues(a);
    if (!open_song(a, i)) a->mode = a->open >= 0 || a->demo ? M_PLAYER : M_LIBRARY;
}

static void meta_text_start(App *a, int field) {
    static const char *const KEY_ALPHA = "ABCDEFG#bm7 ?";
    static const char *const NUM_ALPHA = "0123456789.";
    static const char *const ANY_ALPHA = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .-_'";
    const char *cur = field == 0 ? a->cues.meta.key : field == 1 ? a->cues.meta.tempo : a->cues.meta.scripter;
    a->tfield = field;
    a->tmax = field == 0 ? 40 : field == 1 ? 12 : 60;
    a->talpha = field == 0 ? KEY_ALPHA : field == 1 ? NUM_ALPHA : ANY_ALPHA;
    snprintf(a->tbuf, sizeof a->tbuf, "%s", strcmp(cur, "?") ? cur : "");
    a->tlen = (int)strlen(a->tbuf);
    a->tpos = a->tlen;
    a->mode = M_TEXT;
    a->piano = field == TF_KEY;                                  /* the Key: a piano to find it by ear; Fn closes it */
    a->piano_oct = 3;
    memset(a->piano_now, 0, sizeof a->piano_now);
    if (a->piano) say(a, R3_ST_HINT, "Play the key's note. Fn closes the piano, then A keeps.");
    else say(a, R3_ST_HINT, "Up/Down letter, Left/Right move, Y erase, A keeps, B cancels.");
}

/* The Piano panel (floating over the Key field): one octave, C3 to B3, on the pad. The buttons are the keys (shoulders are one-touch here, not modifiers); Select and Start
 * move the octave, Fn closes it. A note names the key's root; what follows it (m, 7 ...) stays. */
static double piano_freq(int key, int oct) { return 130.8128 * pow(2.0, (key + 12 * (oct - 3)) / 12.0); }      /* C3 is 130.81 Hz */

static void piano_apply(App *a, int key) {
    int skip = 0;
    if (a->tbuf[0] >= 'A' && a->tbuf[0] <= 'G') skip = a->tbuf[1] == '#' || a->tbuf[1] == 'b' ? 2 : 1;
    char rest[72];
    snprintf(rest, sizeof rest, "%s", a->tbuf + skip);
    snprintf(a->tbuf, sizeof a->tbuf, "%s%s", NOTE_NAME[key], rest);
    a->tbuf[a->tmax < (int)sizeof a->tbuf - 1 ? a->tmax : (int)sizeof a->tbuf - 1] = 0;
    a->tlen = (int)strlen(a->tbuf);
    a->tpos = a->tlen;
}

static void piano_close(App *a) {
    a->piano = 0;
    memset(a->piano_now, 0, sizeof a->piano_now);
    engine_notes_off(&a->eng);
}

/* A raw button while the Piano is up (down or up). */
static void piano_button(App *a, int bt, int down) {
    if (bt == R3_BTN_FN) { if (down) { if (a->piano) { piano_close(a); say(a, R3_ST_HINT, "Piano closed. A keeps the key, B cancels."); } else if (a->tfield == TF_KEY) { a->piano = 1; say(a, R3_ST_HINT, "Fn closes the piano."); } } return; }
    if (!a->piano) return;
    if (bt == R3_BTN_SELECT || bt == R3_BTN_START) {
        if (down) { a->piano_oct += bt == R3_BTN_START ? 1 : -1; if (a->piano_oct < 1) a->piano_oct = 1; if (a->piano_oct > 5) a->piano_oct = 5; }
        return;
    }
    for (int k = 0; k < 12; k++) {
        if (PIANO_KEYS[k].btn != bt) continue;
        if (down) { engine_note_on(&a->eng, k, piano_freq(k, a->piano_oct)); a->piano_now[k] = 1; piano_apply(a, k); say(a, R3_ST_HINT, "%s%d", NOTE_NAME[k], a->piano_oct); }
        else { engine_note_off(&a->eng, k); a->piano_now[k] = 0; }
        return;
    }
}

/* Naming a marker: Right on a preset (Verse, Chorus ...) asks for the number or letter after it, with Up and Down, and A keeps "Verse 1", "Chorus 1a". */
static const char SUFFIX_ALPHA[] = "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0";

static void suffix_start(App *a, int preset) {
    if (preset < 0 || preset >= NPRESET || !a->cues.nm) return;
    snprintf(a->suffix_base, sizeof a->suffix_base, "%s", PRESET_LABELS[preset]);
    a->tfield = TF_SUFFIX;
    a->tmax = 8;
    a->talpha = SUFFIX_ALPHA;
    a->tbuf[0] = 0;
    a->tlen = a->tpos = 0;
    a->mode = M_TEXT;
    say(a, R3_ST_HINT, "Up/Down number or letter, Right next place, A keeps, B cancels.");
}

/* Keep what was typed. A Tempo must be a positive number, or empty (or ?) to be suggested from the cues; changing the Sig re-suggests too (Cue's input handler). */
static void meta_text_done(App *a) {
    static const char *const F[] = {"key", "tempo", "sig", "scripter"};
    char v[72];
    if (a->tfield == TF_SUFFIX) {                               /* the marker's name with its number or letter: "Verse 1a" */
        char label[sizeof a->cues.m[0].label];
        snprintf(label, sizeof label, "%s%s%s", a->suffix_base, a->tlen ? " " : "", a->tbuf);
        if (a->cues.nm) { snprintf(a->cues.m[a->msel].label, sizeof a->cues.m[a->msel].label, "%s", label); a->cues.dirty = 1; }
        a->mode = M_PLAYER;
        say(a, R3_ST_OK, "Named %s.", label);
        return;
    }
    snprintf(v, sizeof v, "%s", a->tbuf);
    while (a->tlen > 0 && v[a->tlen - 1] == ' ') v[--a->tlen] = 0;
    if (a->tfield == 1 && v[0]) {
        char *end;
        double t = strtod(v, &end);
        if (*end || !(t > 0)) { say(a, R3_ST_WARN, "Tempo is beats per minute: a number above 0, or erase it."); return; }
    }
    cue_meta_set(&a->cues, F[a->tfield], v[0] ? v : "?");
    if (a->tfield == 1) { int ch = maybe_suggest_tempo(a); say(a, R3_ST_OK, ch ? "Tempo suggested from the cues." : "Tempo kept."); }
    else say(a, R3_ST_OK, "%s kept.", META_NAMES[a->tfield == 3 ? 3 : a->tfield]);
    a->cues.dirty = 1;
    piano_close(a);
    a->mode = M_PLAYER;
}

static void meta_accept(App *a, int i) {
    if (i == 2) {                                               /* Sig: the two Cue knows */
        cue_meta_set(&a->cues, "sig", !strcmp(a->cues.meta.sig, "4/4") || !a->cues.meta.sig[0] ? "6/8" : "4/4");
        a->cues.dirty = 1;
        int ch = maybe_suggest_tempo(a);
        say(a, R3_ST_OK, "Sig %s.%s", a->cues.meta.sig, ch ? " Tempo suggested." : "");
    } else if (i == 4) say(a, R3_ST_HINT, "Created is set when the cues are exported.");
    else meta_text_start(a, i);
}

/* Cue's Recalculate: an unset Tempo is suggested from the cues; one a person set stands (the dots are re-judged against it anyway). */
static void recalc_tempo(App *a) {
    if (!tempo_unset(a)) { say(a, R3_ST_HINT, "Tempo is set to %s. Erase it (Metadata > Tempo) to suggest one.", a->cues.meta.tempo); return; }
    if (a->cues.nc < 2) { say(a, R3_ST_WARN, "At least two cues are needed to calculate a tempo."); return; }
    if (maybe_suggest_tempo(a)) { a->cues.dirty = 1; say(a, R3_ST_OK, "Tempo %s bpm.", a->cues.meta.tempo); }
}

/* A on the focused panel's selection. */
static void panel_accept(App *a) {
    int p = panel_at(a, a->pan_focus), sel = *panel_sel(a, p);
    switch (p) {
    case P_MARKERS: jump_marker(a, sel); break;
    case P_CUES:
        if (sel >= 0 && sel < a->cues.nc) { engine_seek(&a->eng, a->cues.c[sel].start + a->offset); engine_play(&a->eng, 1); say(a, R3_ST_HINT, "%s", a->cues.c[sel].text); }
        break;
    case P_META: meta_accept(a, sel); break;
    case P_SETLIST: if (sel >= 0 && sel < a->nsl) open_by_id(a, a->sl[sel].id, a->sl[sel].title); break;
    case P_LAST: case P_TOP: {
        int rank[10], n = hist_rank(a, p == P_TOP, rank);
        if (sel >= 0 && sel < n) open_by_id(a, a->hist[rank[sel]].id, a->hist[rank[sel]].title);
        break;
    }
    }
}

/* The right stick (and R1 with it): up and down move the selection in the panel it is in (Page up and down, Home and End with R1 held), left and right hop to the other
 * panel on display. On the Jump panel it picks the marker a button will bind to. */
static void stick_action(App *a, int act) {
    a->stick_idle_ms = 0;
    if (act == R3_LEFT || act == R3_RIGHT) { if (!a->jump_view) a->pan_focus = act == R3_LEFT ? 0 : 1; return; }
    if (a->jump_view) { a->jsel = r3_list_step(a->jsel, a->cues.nm, JUMP_ROWS, act); return; }
    int p = panel_at(a, a->pan_focus), *sel = panel_sel(a, p), n = panel_count(a, p);
    *sel = r3_list_step(*sel, n, 10, act);
}

/* The carousel: the six panels two at a time (Fn turns it one place), then the Jump panel on the whole width, then round again. */
static int view_code(const App *a) { return a->jump_view ? P_COUNT : a->pan_first; }

static void view_set(App *a, int code) {
    int cur = view_code(a);
    if (code == cur) return;
    a->prev_view = cur;
    a->jump_view = code == P_COUNT;
    if (!a->jump_view) a->pan_first = code;
    if (a->jump_view) say(a, R3_ST_HINT, "Jump: stopped, a button binds the marker; playing, it jumps.");
    else say(a, R3_ST_HINT, "%s | %s", PANEL_NAMES[panel_at(a, 0)], PANEL_NAMES[panel_at(a, 1)]);
}

static void panels_turn(App *a, int action) {
    if (action != R3_PANEL_NEXT) a->auto_jump = 0;
    if (action == R3_PANEL_FIRST) view_set(a, 0);
    else if (action == R3_PANEL_PREV) { if (a->prev_view == view_code(a)) say(a, R3_ST_HINT, "No earlier panel yet."); else view_set(a, a->prev_view); }
    else if (a->eng.playing && !a->rec && !a->jump_view) {      /* playing in Play mode: Fn goes straight to Jump, wherever the carousel is; stopping brings the panels back */
        a->auto_jump = 1;
        a->auto_prev = view_code(a);
        view_set(a, P_COUNT);
    } else view_set(a, (view_code(a) + 1) % (P_COUNT + 1));
}

/* A typed key while a metadata field is open (a keyboard); true when used. */
static int text_key(App *a, SDL_Keycode k, Uint16 mod) {
    int ch = 0;
    if (k >= SDLK_a && k <= SDLK_z) ch = (mod & KMOD_SHIFT) ? toupper((int)k) : (int)k;
    else if (k >= SDLK_0 && k <= SDLK_9) ch = (int)k;
    else if (k == SDLK_SPACE) ch = ' ';
    else if (k == SDLK_PERIOD) ch = '.';
    else if (k == SDLK_MINUS) ch = (mod & KMOD_SHIFT) ? '_' : '-';
    else if (k == SDLK_QUOTE) ch = '\'';
    else if (k == SDLK_BACKSPACE) { if (a->tpos > 0) { memmove(a->tbuf + a->tpos - 1, a->tbuf + a->tpos, (size_t)(a->tlen - a->tpos + 1)); a->tpos--; a->tlen--; } return 1; }
    if (!ch) return 0;
    if (a->tfield == 1 && !strchr("0123456789.", ch)) return 1;
    if (a->tlen < a->tmax && a->tlen + 1 < (int)sizeof a->tbuf) {
        memmove(a->tbuf + a->tpos + 1, a->tbuf + a->tpos, (size_t)(a->tlen - a->tpos + 1));
        a->tbuf[a->tpos++] = (char)ch;
        a->tlen++;
    }
    return 1;
}

static void text_action(App *a, int act) {
    const int alen = (int)strlen(a->talpha);
    switch (act) {
    case R3_BACK: piano_close(a); a->mode = M_PLAYER; say(a, R3_ST_HINT, "Unchanged."); break;
    case R3_ACCEPT: meta_text_done(a); break;
    case R3_LEFT: if (a->tpos > 0) a->tpos--; break;
    case R3_RIGHT: if (a->tpos < a->tlen) a->tpos++; break;
    case R3_Y:
        if (a->tpos > 0) { memmove(a->tbuf + a->tpos - 1, a->tbuf + a->tpos, (size_t)(a->tlen - a->tpos + 1)); a->tpos--; a->tlen--; }
        break;
    case R3_UP: case R3_DOWN: case R3_PAGEUP: case R3_PAGEDOWN: {
        int step = act == R3_UP ? 1 : act == R3_DOWN ? -1 : act == R3_PAGEUP ? 8 : -8;
        if (a->tpos == a->tlen) {                                    /* past the end: a new character, starting from the first of the alphabet */
            if (a->tlen >= a->tmax) break;
            a->tbuf[a->tlen++] = a->talpha[0];
            a->tbuf[a->tlen] = 0;
            if (step == 1 || step == -1) break;
        }
        const char *at = strchr(a->talpha, a->tbuf[a->tpos]);
        int i = at ? (int)(at - a->talpha) : 0;
        a->tbuf[a->tpos] = a->talpha[((i + step) % alen + alen) % alen];
        break;
    }
    default: break;
    }
}


static void save_offset(const App *a) {
    if (a->open < 0) return;
    char path[PATH_MAX], buf[32];
    cue_path(a, a->songs[a->open].id, "offset", path, sizeof path);
    snprintf(buf, sizeof buf, "%.3f\n", a->offset);
    cue_write_file(path, buf, strlen(buf));
}

/* The app's own menus (the main menu, L1+R1, has the six fixed items first, then one of these): the songs' (Add, Sync ...), the song's (offset, save, export ...), and the
 * context menu (L2+R2, or Y) for what the cursor is on. */
static void build_menu(App *a, int kind) {
    char v[40];
    a->menu_kind = kind;
    if (kind == MK_LIBRARY) {
        menu_clear(&a->menu, "Songs and sync");
        menu_add(&a->menu, A_ADD, "Add: YouTube ID (11 chars)", "");
        menu_add(&a->menu, A_SYNC, "Sync: fetch cues and songs", "");
        menu_add(&a->menu, A_IMPORT, "Import Cue bundles from inbox/", "");
        menu_add(&a->menu, A_DEMO, "Demo song", "");
        menu_add(&a->menu, A_LICENSE, "Fetch from YouTube", a->any_license ? "any video" : "Creative Commons only");
        menu_add(&a->menu, A_VIDEO, "Also save the video (480p)", a->save_video ? "[x]" : "[ ]");
        { int nwav = wav_copies(a); char cv[24]; snprintf(cv, sizeof cv, "%d WAV cop%s", nwav, nwav == 1 ? "y" : "ies"); menu_add(&a->menu, A_COMPRESS, "Compress copies to AAC (M4A)", nwav ? cv : "none to do"); }
    } else if (kind == MK_SONG) {
        menu_clear(&a->menu, "Song options");
        snprintf(v, sizeof v, "%+.2f s", a->offset);
        menu_add(&a->menu, A_OFFSET, "Offset (audio against cues)", v);
        menu_add(&a->menu, A_SAVE, "Save cues", a->cues.dirty ? "unsaved" : "saved");
        menu_add(&a->menu, A_EXPORT, "Export for Cue (json, srt, bundle)", "");
        menu_add(&a->menu, A_SLADD, "Add this song to the Set List", a->open >= 0 && setlist_find(a, a->songs[a->open].id) >= 0 ? "in list" : "");
        menu_add(&a->menu, A_RECALC, "Recalculate tempo", "");
        menu_add(&a->menu, A_KT, "Key and Tempo: make a copy", "");
        menu_add(&a->menu, A_QUIT, "Back to the songs", "");
    } else {
        menu_clear(&a->menu, a->jump_view ? "Jump" : PANEL_NAMES[panel_at(a, a->pan_focus)]);
        int fp = a->jump_view ? -1 : panel_at(a, a->pan_focus);
        menu_add(&a->menu, A_CUE, "Cue here", "");
        menu_add(&a->menu, A_MARK, "Mark here", "");
        if (fp == P_MARKERS || a->jump_view) {
            menu_add(&a->menu, A_LABEL, "Name the selected marker", a->cues.nm ? a->cues.m[a->jump_view ? a->jsel : a->msel].label : "");
            menu_add(&a->menu, A_DELETE, "Delete the selected marker", "");
            if (a->jump_view) menu_add(&a->menu, A_JUMPCLEAR, "Clear every button", "");
        } else if (fp == P_CUES) {
            menu_add(&a->menu, A_BOOKMARK, "Bookmark the selected cue", a->cues.nc && marker_at(a, a->cues.c[a->csel].start) >= 0 ? "marked" : "");
            menu_add(&a->menu, A_CUEDEL, "Delete the selected cue", "");
        } else if (fp == P_META) {
            menu_add(&a->menu, A_RECALC, "Recalculate tempo", "");
        } else if (fp == P_SETLIST) {
            menu_add(&a->menu, A_SLDEL, "Remove from the Set List", "");
            menu_add(&a->menu, A_SLUP, "Move up", "");
            menu_add(&a->menu, A_SLDOWN, "Move down", "");
        }
    }
}

static void open_menu(App *a, int kind) {
    a->back_mode = a->mode == M_MENU ? a->back_mode : a->mode;
    build_menu(a, kind);
    a->mode = M_MENU;
}

static void open_label_menu(App *a) {
    if (!a->cues.nm) return;
    menu_clear(&a->menu, "Name the marker");
    for (int i = 0; i < NPRESET; i++) menu_add(&a->menu, A_LABELPICK + i, PRESET_LABELS[i], "> 1 2 a b");
    a->menu.sel = 0;
    say(a, R3_ST_HINT, "A keeps the name; Right adds a number or letter (Verse 1, Chorus 1a).");
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

/* Starts argv as the job: its output, lines of PROGRESS, BAR, DONE or text, is read by job_poll and shown on the job screen. */
static int job_spawn(App *a, const char *const *argv, int kind) {
    int fd[2];
    memset(a->job_lines, 0, sizeof a->job_lines);
    a->job_nlines = 0; a->job_frac = 0; a->job_done = 0; a->job_len = 0;
    if (pipe(fd) != 0) return 0;
    pid_t pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); return 0; }
    if (pid == 0) {
        dup2(fd[1], 1);
        dup2(fd[1], 2);
        close(fd[0]); close(fd[1]);
        setenv("R3I6NOS_APP_DATA", a->data, 1);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    close(fd[1]);
    fcntl(fd[0], F_SETFL, fcntl(fd[0], F_GETFL) | O_NONBLOCK);
    a->job_fd = fd[0];
    a->job_pid = pid;
    a->job_kind = kind;
    a->mode = M_JOB;
    job_line(a, "Starting...");
    return 1;
}

static void job_start(App *a, const char *one_id) {
    char hbuf[PATH_MAX + 40];
    const char *helper = helper_path(hbuf, sizeof hbuf);
    snprintf(a->job_want, sizeof a->job_want, "%s", one_id ? one_id : "");
    const char *lic = a->any_license ? "--any-license" : "--cc-only", *vid = a->save_video ? "--video" : "--cc-only";   /* a repeated --cc-only is harmless */
    const char *argv_id[] = {helper, "--data", a->data, lic, vid, "--id", one_id, NULL};
    const char *argv_all[] = {helper, "--data", a->data, lic, vid, NULL};
    if (!job_spawn(a, one_id ? argv_id : argv_all, JOB_SYNC)) { say(a, R3_ST_ERROR, "Cannot start the sync."); return; }
    say(a, R3_ST_HINT, one_id ? "Fetching %s." : "Reading MusiCue/inbox/urls.txt.", one_id ? one_id : "");
}

/* ---- Key and Tempo: a copy of the song at another Key and Tempo, rendered by musicue-render (docs/MUSICUE.md, ROADMAP section 40) ------------------- */

/* musicue-render: $R3I6NOS_MUSICUE_RENDER, else the copy packed beside this program (usr/lib/musicue/), else the PATH's. */
static const char *render_helper_path(char *buf, size_t cap) {
    const char *o = getenv("R3I6NOS_MUSICUE_RENDER");
    if (o && *o) return o;
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = 0;
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = 0;
            snprintf(buf, cap, "%s/../lib/musicue/musicue-render", exe);
            if (access(buf, X_OK) == 0) return buf;
        }
    }
    return "musicue-render";
}

/* The battery: charge in percent and whether it is charging, from $R3I6NOS_MUSICUE_BATTERY (a folder, for tests) or /sys/class/power_supply/battery.
 * -1 when there is none to read (a laptop, a test): then nothing is refused for it. */
static int battery_state(int *charging) {
    const char *d = getenv("R3I6NOS_MUSICUE_BATTERY");
    char p[PATH_MAX], v[32] = "";
    snprintf(p, sizeof p, "%s/capacity", d && *d ? d : "/sys/class/power_supply/battery");
    FILE *f = fopen(p, "r");
    if (!f) return -1;
    int pct = fgets(v, sizeof v, f) ? atoi(v) : -1;
    fclose(f);
    snprintf(p, sizeof p, "%s/status", d && *d ? d : "/sys/class/power_supply/battery");
    *charging = 0;
    if ((f = fopen(p, "r"))) { if (fgets(v, sizeof v, f)) *charging = !strncmp(v, "Charging", 8) || !strncmp(v, "Full", 4); fclose(f); }
    return pct;
}

static double free_bytes(const char *dir) {
    const char *o = getenv("R3I6NOS_MUSICUE_FREE_MB");
    if (o && *o) return atof(o) * 1048576.0;
    struct statvfs sv;
    return statvfs(dir, &sv) == 0 ? (double)sv.f_bavail * (double)sv.f_frsize : 1e18;
}

/* Song options > Key and Tempo: the song's own Key and Tempo are the starting point. */
static void kt_open(App *a) {
    a->mode = M_PLAYER;
    if (a->open < 0) { say(a, R3_ST_WARN, "Open a song from the card first (the demo is not a file)."); return; }
    if (kt_is_copy_id(a->songs[a->open].id)) { say(a, R3_ST_WARN, "This is already a copy: make the new one from the original song."); return; }
    const char *why = kt_plan(&a->kt, a->cues.meta.key, a->cues.meta.tempo);
    if (why) { say(a, R3_ST_WARN, "%s", why); return; }
    a->kt_workers = 4; a->kt_cheaper = 0;
    a->mode = M_KT;
    say(a, R3_ST_HINT, "Left/Right the Key, Up/Down the Tempo, A makes the copy.");
}

static void kt_move(App *a, int dkey, double dtempo) {
    a->kt.target.root = ((a->kt.target.root + dkey) % 12 + 12) % 12;
    double t = a->kt.target_tempo + dtempo;
    if (t < a->kt.base_tempo * 0.5) t = a->kt.base_tempo * 0.5;
    if (t > a->kt.base_tempo * 2) t = a->kt.base_tempo * 2;
    a->kt.target_tempo = floor(t * 100 + 0.5) / 100;
    kt_update(&a->kt);
}

/* The copy's paths are made from the plan: media/<title>.wav, and the job's part and journal beside it. */
static void kt_paths(App *a, const Song *base) {
    fmt_tempo(a->kt.target_tempo, a->kt_tempo_txt, sizeof a->kt_tempo_txt);
    kt_key_name(&a->kt.target, a->kt_key_txt, sizeof a->kt_key_txt);
    kt_copy_id(base->id, a->kt.semis, a->kt.ratio, a->kt_copy_id, sizeof a->kt_copy_id);
    kt_copy_name(base->name, &a->kt.target, a->kt_tempo_txt, a->kt_copy_id, a->kt_title, sizeof a->kt_title);
    char file[PATH_MAX];
    snprintf(file, sizeof file, "%s.%s", a->kt_title, a->kt_aac ? "m4a" : "wav");
    sub_path(a, "media", file, a->kt_out, sizeof a->kt_out);
    snprintf(a->kt_base_id, sizeof a->kt_base_id, "%s", base->id);
    a->kt_ratio = a->kt.ratio; a->kt_semis = a->kt.semis;
}

static void kt_cleanup(App *a) {            /* a render stopped on purpose: the half-written copy and its journal go (a power cut keeps them, to continue) */
    char p[PATH_MAX + 16];
    snprintf(p, sizeof p, "%s.part", a->kt_out); unlink(p);
    snprintf(p, sizeof p, "%s.journal", a->kt_out); unlink(p);
    snprintf(p, sizeof p, "%s.in.wav", a->kt_out); unlink(p);
    snprintf(p, sizeof p, "%s.enc", a->kt_out); unlink(p);
}

/* Checks, then starts the render (the confirmation was the screen: A on it). */
static void kt_start(App *a) {
    const char *why = kt_update(&a->kt);
    if (why) { say(a, R3_ST_WARN, "%s", why); return; }
    const Song base = a->songs[a->open];
    const double song_s = dur_of(a);
    int charging = 0, pct = battery_state(&charging);
    if (pct >= 0 && pct < 20 && !charging) { say(a, R3_ST_WARN, "The battery is at %d%%: plug in the charger first, or wait. A render uses all four cores.", pct); return; }
    /* the room: the WAV it is rendered into (which is deleted once compressed) and, for AAC, the M4A beside it */
    const double need = kt_copy_bytes(song_s, a->kt.ratio, 0) * 1.05 + 16.0 * 1048576.0 + (a->kt_aac ? kt_copy_bytes(song_s, a->kt.ratio, 1) : 0);
    if (free_bytes(a->data) < need) {
        say(a, R3_ST_WARN, "Not enough room for the copy (%.0f MiB while it is made).", need / 1048576.0);
        return;
    }
    kt_paths(a, &base);
    scan_songs(a);
    if (song_by_id(a, a->kt_copy_id) >= 0) {                 /* the same ask again: the copy is there (as a WAV or an M4A) */
        a->lsel = song_by_id(a, a->kt_copy_id);
        save_cues(a); close_song(a);
        a->mode = M_LIBRARY;
        say(a, R3_ST_OK, "That copy is already made: it is selected in the songs.");
        return;
    }
    save_cues(a);                                              /* the copy's cues are made from the saved ones */
    close_song(a);                                             /* frees the song's memory for the render */
    char hbuf[PATH_MAX + 40], ratio[32], semis[16], workers[16], enc[24];
    snprintf(ratio, sizeof ratio, "%.10g", a->kt_ratio);
    snprintf(semis, sizeof semis, "%d", a->kt_semis);
    snprintf(workers, sizeof workers, "%d", a->kt_workers);
    const char *helper = render_helper_path(hbuf, sizeof hbuf);
    const char *argv[20];
    int n = 0;
    argv[n++] = helper; argv[n++] = "--in"; argv[n++] = base.path; argv[n++] = "--out"; argv[n++] = a->kt_out;
    argv[n++] = "--ratio"; argv[n++] = ratio; argv[n++] = "--semitones"; argv[n++] = semis; argv[n++] = "--workers"; argv[n++] = workers;
    if (a->kt_cheaper) argv[n++] = "--cheaper";
    if (a->kt_aac) { argv[n++] = "--encode"; snprintf(enc, sizeof enc, "aac:%d:fast", KT_AAC_KBPS); argv[n++] = enc; }
    argv[n] = NULL;
    a->job_want[0] = 0;
    if (!job_spawn(a, argv, JOB_RENDER)) { say(a, R3_ST_ERROR, "Cannot start the render."); a->mode = M_LIBRARY; return; }
    say(a, R3_ST_HINT, "A copy of %.40s at %s and %s bpm. B stops it.", base.name, a->kt_key_txt, a->kt_tempo_txt);
}

/* The render is done: the copy's cue file is the base's with every time divided by the ratio and the Key and Tempo set, its offset is scaled the same,
 * and a sidecar names what it is a copy of. 1 when all of it was written. */
static int kt_finish(App *a) {
    char path[PATH_MAX], dir[PATH_MAX];
    sub_path(a, "cues", NULL, dir, sizeof dir);
    mkdirs(dir);
    CueSet cs;
    cue_init(&cs);
    size_t n;
    char e2[96];
    cue_path(a, a->kt_base_id, "json", path, sizeof path);
    char *t = cue_read_file(path, &n);
    if (t) { cue_load_json(&cs, t, n, 1, e2); free(t); }
    if (!cs.nc) {
        cue_path(a, a->kt_base_id, "srt", path, sizeof path);
        t = cue_read_file(path, &n);
        if (t) { cue_load_srt(&cs, t, n, e2); free(t); }
    }
    kt_scale_cues(&cs, a->kt_ratio);
    snprintf(cs.videoid, sizeof cs.videoid, "%s", a->kt_copy_id);
    cue_meta_set(&cs, "key", a->kt_key_txt);
    cue_meta_set(&cs, "tempo", a->kt_tempo_txt);
    CueBuf b = {0};
    cue_write_version(&cs, &b, time(NULL));
    cue_path(a, a->kt_copy_id, "json", path, sizeof path);
    int ok = !b.bad && cue_write_file(path, b.p, b.n) == 0;
    free(b.p);
    cue_free(&cs);
    cue_path(a, a->kt_base_id, "offset", path, sizeof path);
    if ((t = cue_read_file(path, NULL))) {
        double off = atof(t) / a->kt_ratio;
        free(t);
        char buf[32];
        snprintf(buf, sizeof buf, "%.3f\n", off);
        cue_path(a, a->kt_copy_id, "offset", path, sizeof path);
        cue_write_file(path, buf, strlen(buf));
    }
    char side[256];
    snprintf(side, sizeof side, "base=%s\nratio=%.10g\nsemitones=%d\nkey=%s\ntempo=%s\n", a->kt_base_id, a->kt_ratio, a->kt_semis, a->kt_key_txt, a->kt_tempo_txt);
    cue_path(a, a->kt_copy_id, "kt", path, sizeof path);
    ok = cue_write_file(path, side, strlen(side)) == 0 && ok;
    return ok;
}

/* Library menu > Compress copies to AAC (M4A): every copy still stored as a WAV becomes an M4A (the same id, so its cues stay) and the WAV is deleted. */
static void compress_start(App *a) {
    a->mode = M_LIBRARY;
    if (!wav_copies(a)) { say(a, R3_ST_WARN, "No WAV copies to compress."); return; }
    int charging = 0, pct = battery_state(&charging);
    if (pct >= 0 && pct < 20 && !charging) { say(a, R3_ST_WARN, "The battery is at %d%%: plug in the charger first, or wait.", pct); return; }
    char hbuf[PATH_MAX + 40], media[PATH_MAX], enc[24];
    const char *helper = render_helper_path(hbuf, sizeof hbuf);
    sub_path(a, "media", NULL, media, sizeof media);
    snprintf(enc, sizeof enc, "aac:%d:fast", KT_AAC_KBPS);
    const char *argv[] = {helper, "--compress-dir", media, "--encode", enc, NULL};
    a->job_want[0] = 0;
    if (!job_spawn(a, argv, JOB_COMPRESS)) { say(a, R3_ST_ERROR, "Cannot start the compressing."); return; }
    say(a, R3_ST_HINT, "Compressing %d cop%s to AAC. B stops it.", wav_copies(a), wav_copies(a) == 1 ? "y" : "ies");
}

static void compress_cleanup(App *a) {       /* a stopped compress leaves the half-written M4A of the copy it was on */
    char dir[PATH_MAX], p[PATH_MAX + 300];
    sub_path(a, "media", NULL, dir, sizeof dir);
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n > 4 && !strcmp(e->d_name + n - 4, ".enc")) { snprintf(p, sizeof p, "%s/%s", dir, e->d_name); unlink(p); }
    }
    closedir(d);
}

static void compress_job_ended(App *a, int status) {
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) { job_line(a, "musicue-render is not installed."); say(a, R3_ST_ERROR, "The render helper is missing."); }
    else if (a->job_done) say(a, R3_ST_OK, "The copies are compressed. A closes.");
    else say(a, R3_ST_WARN, "The compressing stopped before it was done. Run it again: copies already done stay done.");
    a->job_done = 1;
    scan_songs(a);
}

static void render_job_ended(App *a, int status) {
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) { job_line(a, "musicue-render is not installed."); say(a, R3_ST_ERROR, "The render helper is missing."); }
    else if (a->job_done) {
        if (kt_finish(a)) say(a, R3_ST_OK, "The copy is ready: %s. A closes.", a->kt_title);
        else say(a, R3_ST_ERROR, "The copy was made but its cue file could not be saved.");
    } else say(a, R3_ST_WARN, "The render stopped before it was done. Make the same copy again to continue it.");
    a->job_done = 1;
    scan_songs(a);
    if (song_by_id(a, a->kt_copy_id) >= 0) a->lsel = song_by_id(a, a->kt_copy_id);
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
        if (a->job_kind == JOB_RENDER) { render_job_ended(a, status); return; }
        if (a->job_kind == JOB_COMPRESS) { compress_job_ended(a, status); return; }
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
    a->idpos = 0; a->idgx = 0; a->idgy = 0;
    a->mode = M_ID;
    say(a, R3_ST_HINT, "Pick the 11 characters of a YouTube id on the keyboard.");
}

static void id_set(App *a, int ch) {
    a->idbuf[a->idpos] = (char)ch;
    if (a->idpos < 10) a->idpos++;
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
    if (it->id >= A_LABELPICK) {
        if (dir > 0) suffix_start(a, it->id - A_LABELPICK);
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
    } else if (it->id == A_VIDEO) {
        a->save_video = !a->save_video;
        save_settings(a);
        snprintf(it->value, sizeof it->value, "%s", a->save_video ? "[x]" : "[ ]");
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
    case A_CUE: a->mode = M_PLAYER; cue_here(a); break;
    case A_LABEL: if (a->jump_view) a->msel = a->jsel; open_label_menu(a); break;
    case A_DELETE:
        if (a->jump_view) a->msel = a->jsel;
        if (a->cues.nm) { cue_del_marker(&a->cues, a->msel); if (a->msel >= a->cues.nm) a->msel = a->cues.nm ? a->cues.nm - 1 : 0; say(a, R3_ST_HINT, "Marker deleted."); }
        a->mode = M_PLAYER;
        break;
    case A_SAVE: say(a, save_cues(a) == 0 ? R3_ST_OK : R3_ST_ERROR, "%s", a->cues.dirty ? "Could not save the cues." : "Cues saved."); a->mode = M_PLAYER; break;
    case A_EXPORT: export_cues(a); a->mode = M_PLAYER; break;
    case A_OFFSET: case A_LICENSE: case A_VIDEO: menu_adjust(a, 1); break;
    case A_JUMPCLEAR: memset(a->jump_on, 0, sizeof a->jump_on); jump_save(a); say(a, R3_ST_HINT, "Every Jump button is free."); a->mode = M_PLAYER; break;
    case A_SYNC: job_start(a, NULL); break;
    case A_ADD: id_open(a); break;
    case A_BOOKMARK: cue_bookmark(a, a->csel); a->mode = M_PLAYER; break;
    case A_CUEDEL: cue_delete(a, a->csel); a->mode = M_PLAYER; break;
    case A_RECALC: recalc_tempo(a); a->mode = M_PLAYER; break;
    case A_KT: kt_open(a); break;
    case A_COMPRESS: compress_start(a); break;
    case A_SLADD:
        if (a->open < 0) say(a, R3_ST_WARN, "Open a song from the card to add it (the demo is not a file).");
        else { int r = setlist_add(a, a->songs[a->open].id, a->songs[a->open].name); say(a, r < 0 ? R3_ST_ERROR : R3_ST_OK, r < 0 ? "The Set List is full." : r ? "Added to the Set List." : "Set List entry updated."); }
        a->mode = M_PLAYER;
        break;
    case A_SLDEL: setlist_remove(a, a->psel[P_SETLIST]); if (a->psel[P_SETLIST] >= a->nsl) a->psel[P_SETLIST] = a->nsl ? a->nsl - 1 : 0; say(a, R3_ST_HINT, "Removed from the Set List."); a->mode = M_PLAYER; break;
    case A_SLUP: a->psel[P_SETLIST] = setlist_move(a, a->psel[P_SETLIST], -1); a->mode = M_PLAYER; break;
    case A_SLDOWN: a->psel[P_SETLIST] = setlist_move(a, a->psel[P_SETLIST], 1); a->mode = M_PLAYER; break;
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
        save_cues(a);
        close_song(a);
        a->mode = M_LIBRARY;
        break;
    default: a->mode = a->back_mode;
    }
}

/* ---- the pad: the mapping's reader, the main menu, and MusiCue's own map for the player ---------------------------------------------------------- */

static void mm_open(App *a) {
    a->mm.n_own = 0;
    a->mm.own[a->mm.n_own++] = a->mode == M_PLAYER ? "Song options" : "Songs and sync";
    r3_mm_open(&a->mm);
    a->overlay = OV_MM;
    a->force = 1;
}

/* One action while L1+R1's menu, About or the colour theme picker shows. */
static void overlay_key(App *a, int act) {
    a->force = 1;
    if (a->overlay == OV_ABOUT) { a->overlay = OV_NONE; return; }
    if (a->overlay == OV_KEYMAP) { if (r3_km_key(&a->km, act)) a->overlay = OV_NONE; return; }
    if (a->overlay == OV_DEMO) {                           /* Rec demo's checklist: A ticks, Start records, B goes back; one stays ticked */
        if (act == R3_UP || act == R3_DOWN) r3_checklist_move(&a->tour_ck, act == R3_UP ? -1 : 1);
        else if (act == R3_ACCEPT) { r3_checklist_toggle(&a->tour_ck); if (!r3_checklist_mask(&a->tour_ck)) a->tour_ck.on[a->tour_ck.sel] = 1; }
        else if (act == R3_START) { a->tour_mask = r3_checklist_mask(&a->tour_ck); a->pending_tour = 1; a->overlay = OV_NONE; say(a, R3_ST_WARN, "Recording the demo (about two minutes; no input meanwhile)..."); }
        else if (act == R3_BACK || act == R3_MENU) { a->overlay = OV_NONE; say(a, R3_ST_HINT, "Rec demo cancelled."); }
        return;
    }
    if (a->overlay == OV_REPORT) {                         /* it asks first: A runs it, B (or the menu key) does not */
        if (act == R3_ACCEPT) { a->pending_report = 1; a->overlay = OV_NONE; say(a, R3_ST_WARN, "Writing the device report (about two minutes; no input meanwhile)..."); }
        else if (act == R3_BACK || act == R3_MENU) { a->overlay = OV_NONE; say(a, R3_ST_HINT, "Device report cancelled."); }
        return;
    }
    if (a->overlay == OV_THEME) {
        if (act == R3_ACCEPT) {
            a->theme = a->theme_sel;
            apply_theme(a);
            save_settings(a);
            say(a, R3_ST_OK, "Colour theme: %s", r3_themes[a->theme].name);
            a->overlay = OV_NONE;
        } else if (act == R3_BACK || act == R3_MENU) {
            apply_theme(a);
            a->overlay = OV_NONE;
        } else {
            a->theme_sel = r3_list_step(a->theme_sel, R3_THEME_COUNT, r3_list_rows(R3_DISPLAY_ROWS - 2), act);
            r3_theme_apply(a->theme_sel);
            app_palette(r3_themes[a->theme_sel].look);
        }
        return;
    }
    int id = r3_mm_key(&a->mm, act);
    if (id == R3MM_NOTHING) return;
    a->overlay = OV_NONE;
    switch (id) {
    case R3MM_ABOUT: a->overlay = OV_ABOUT; break;
    case R3MM_THEME: a->overlay = OV_THEME; a->theme_sel = a->theme; break;
    case R3MM_KEYMAP: r3_km_open(&a->km, NULL, 0, 0); a->overlay = OV_KEYMAP; break;
    case R3MM_REPORT: a->overlay = OV_REPORT; break;
    case R3MM_DEMO: demo_checklist_open(a); break;
    case R3MM_QUIT: a->quit = 1; break;
    case R3MM_OWN0: open_menu(a, a->mode == M_PLAYER ? MK_SONG : MK_LIBRARY); break;
    default: break;
    }
}

static void app_action(App *a, int act) {
    switch (a->mode) {
    case M_LIBRARY:
        if (act == R3_ACCEPT) { if (a->nsongs && !open_song(a, a->lsel)) a->mode = M_LIBRARY; }
        else if (act == R3_BACK) say(a, R3_ST_HINT, "L1+R1 is the menu; Quit is in it.");
        else if (act == R3_MORE || act == R3_Y) open_menu(a, MK_LIBRARY);
        else a->lsel = r3_list_key(a->lsel, a->nsongs, 11, act);
        break;
    case M_PLAYER: break;                                                   /* the player reads raw buttons (player_press) and the stick (stick_action) */
    case M_TEXT: text_action(a, act); break;
    case M_MENU:
        if (act == R3_BACK || act == R3_MORE || act == R3_MENU || act == R3_CONTEXT) { a->mode = a->back_mode == M_PLAYER || a->menu.it[0].id >= A_LABELPICK ? (a->open >= 0 || a->demo ? M_PLAYER : M_LIBRARY) : a->back_mode; }
        else if (act == R3_ACCEPT) menu_choose(a);
        else if (act == R3_LEFT) menu_adjust(a, -1);
        else if (act == R3_RIGHT) menu_adjust(a, 1);
        else a->menu.sel = r3_list_step(a->menu.sel, a->menu.n, 12, act);
        break;
    case M_JOB:
        if (act == R3_BACK || act == R3_ACCEPT) {
            int running = a->job_pid > 0;
            job_stop(a);
            if (running && a->job_kind == JOB_RENDER) { kt_cleanup(a); say(a, R3_ST_HINT, "The render was stopped; nothing was added."); }
            if (running && a->job_kind == JOB_COMPRESS) { compress_cleanup(a); say(a, R3_ST_HINT, "The compressing was stopped; copies already done stay done."); }
            a->mode = M_LIBRARY; scan_songs(a);
        }
        break;
    case M_KT:
        switch (act) {
        case R3_BACK: a->mode = M_PLAYER; say(a, R3_ST_HINT, "No copy made."); break;
        case R3_LEFT: kt_move(a, -1, 0); break;
        case R3_RIGHT: kt_move(a, 1, 0); break;
        case R3_UP: kt_move(a, 0, 1); break;
        case R3_DOWN: kt_move(a, 0, -1); break;
        case R3_PAGEUP: kt_move(a, 0, 5); break;
        case R3_PAGEDOWN: kt_move(a, 0, -5); break;
        case R3_HOME: a->kt.target = a->kt.base; a->kt.target_tempo = a->kt.base_tempo; kt_update(&a->kt); break;
        case R3_X: a->kt_workers = a->kt_workers == 4 ? 2 : 4; break;
        case R3_Y: a->kt_cheaper = !a->kt_cheaper; break;
        case R3_SELECT: a->kt_aac = !a->kt_aac; save_settings(a); break;
        case R3_ACCEPT: case R3_START: kt_start(a); break;
        default: break;
        }
        break;
    case M_NOTE:
        if (act == R3_BACK || act == R3_ACCEPT) a->mode = M_LIBRARY;
        break;
    case M_ID:
        switch (act) {
        case R3_BACK: a->mode = M_LIBRARY; say(a, R3_ST_HINT, "Nothing added."); break;
        case R3_UP: a->idgy = (a->idgy + 3) % 4; break;
        case R3_DOWN: a->idgy = (a->idgy + 1) % 4; break;
        case R3_LEFT: a->idgx = (a->idgx + ID_COLS - 1) % ID_COLS; break;
        case R3_RIGHT: a->idgx = (a->idgx + 1) % ID_COLS; break;
        case R3_PAGEUP: a->idgy = 0; break;
        case R3_PAGEDOWN: a->idgy = 3; break;
        case R3_X: case R3_Y: id_erase(a); break;
        case R3_ACCEPT: id_set(a, ID_ALPHA[a->idgy * ID_COLS + a->idgx]); break;
        case R3_MORE: case R3_START: id_go(a); break;
        default: break;
        }
        break;
    }
}

/* The system chords and the overlays, ahead of every screen's own actions. */
static void dispatch(App *a, int act) {
    if (act == R3_NONE) return;
    if (a->overlay) { overlay_key(a, act); return; }
    if (act == R3_MENU && (a->mode == M_LIBRARY || a->mode == M_PLAYER)) { mm_open(a); return; }
    if (act == R3_CONTEXT && a->mode == M_PLAYER) { open_menu(a, MK_CONTEXT); return; }
    if (act == R3_CONTEXT && a->mode == M_LIBRARY) { say(a, R3_ST_HINT, "No contextual menu here: L1+R1 is the menu."); return; }
    if (act == R3_L3 || act == R3_R3) { if (a->mode == M_PLAYER || a->mode == M_LIBRARY) say(a, R3_ST_HINT, "MusiCue has no virtual %s.", act == R3_L3 ? "mouse" : "keyboard"); return; }
    app_action(a, act);
}

/* A step of the right stick (the reader makes them at the stick's rate, Page up and down and Home and End with R1 held). */
static void stick_dispatch(App *a, int act) {
    if (a->overlay) { overlay_key(a, act); return; }
    if (a->mode == M_PLAYER) stick_action(a, act);
    else app_action(a, act);
}

/* The player's map, from the raw button and what is held (the maintainer's, 2026-11-20). Returns 1 when it used the press.
 *   Start play/stop;  L1+Start play in Rec mode;  playing, Start stops (and ends Rec mode).
 *   Rec mode:  D-pad Down adds a cue, Up takes the last one back, Left and Right the cue before and after.
 *   Play mode: D-pad Up the first cue, Down the last, Left and Right the cue before and after.
 *   R1+Up sets A, R1+Down sets B, R1+Start switches the A-B loop, R1+Select goes to A;  R1+Left/Right: 10 seconds back or on.
 *   On the Jump panel the D-pad and A, B, X, Y are its eight buttons (Rec mode's D-pad and B's Back give way to them).
 *   Fn turns the panels, L1+Fn the first, R1+Fn the one before;  A acts on the selection;  B back to the songs;  Y the context menu;  Select 0:00. */
static int player_press(App *a, int bt, int act) {
    if (act == R3_MENU || act == R3_CONTEXT) return 0;
    const int r1 = a->in.held[R3_BTN_R1], l1 = a->in.held[R3_BTN_L1];
    switch (bt) {
    case R3_BTN_FN: panels_turn(a, act); return 1;
    case R3_BTN_START:
        if (r1) loop_toggle(a);
        else transport(a, l1);
        return 1;
    case R3_BTN_SELECT:
        if (r1) loop_goto_a(a);
        else { engine_seek(&a->eng, 0); say(a, R3_ST_HINT, "Back to 0:00."); }
        return 1;
    case R3_BTN_UP: case R3_BTN_DOWN: case R3_BTN_LEFT: case R3_BTN_RIGHT: {
        const int up = bt == R3_BTN_UP, down = bt == R3_BTN_DOWN, left = bt == R3_BTN_LEFT;
        if (r1) {
            if (up) loop_set_a(a);
            else if (down) loop_set_b(a);
            else seek_rel(a, left ? -10 : 10);
        } else if (a->jump_view) jump_button(a, up ? 0 : down ? 1 : left ? 2 : 3);
        else if (a->rec && a->eng.playing) {
            if (down) cue_here(a);
            else if (up) rec_undo(a);
            else if (left) cue_prev(a);
            else cue_next(a);
        } else {
            if (up) cue_edge(a, 0);
            else if (down) cue_edge(a, 1);
            else if (left) cue_prev(a);
            else cue_next(a);
        }
        return 1;
    }
    case R3_BTN_A: case R3_BTN_B: case R3_BTN_X: case R3_BTN_Y:
        if (a->jump_view && !r1) { jump_button(a, bt == R3_BTN_A ? 4 : bt == R3_BTN_B ? 5 : bt == R3_BTN_X ? 6 : 7); return 1; }
        if (bt == R3_BTN_A) panel_accept(a);
        else if (bt == R3_BTN_B) { save_cues(a); close_song(a); a->mode = M_LIBRARY; }
        else if (bt == R3_BTN_Y) open_menu(a, MK_CONTEXT);
        else say(a, R3_ST_HINT, "Delete is in the context menu (L2+R2).");
        return 1;
    case R3_BTN_L3: case R3_BTN_R3:
        say(a, R3_ST_HINT, "MusiCue has no virtual %s.", bt == R3_BTN_L3 ? "mouse" : "keyboard");
        return 1;
    default: return 1;                                                      /* L1, R1, L2, R2 alone do nothing */
    }
}

/* A raw button, down or up, from the pad (or the keyboard standing in for it). The reader keeps what is held; the Piano over the Key field takes every button but
 * the system chords' meaning (a shoulder is a note there). */
static void pad_button(App *a, int bt, int down) {
    int act = r3_input_button(&a->in, bt, down);
    if (a->overlay == OV_KEYMAP) { r3_km_button(&a->km, bt, down, NULL); if (down) overlay_key(a, act); a->force = 1; return; }
    if (a->mode == M_TEXT && a->tfield == TF_KEY && !a->overlay && (a->piano || bt == R3_BTN_FN)) { piano_button(a, bt, down); return; }
    if (!down) return;
    if (!a->overlay && a->mode == M_PLAYER && player_press(a, bt, act)) return;
    dispatch(a, act);
}

/* The right stick, once a pass of the main loop: the steps it made since the last (the reader counts the rate). */
static void stick_tick(App *a, uint32_t now_ms) {
    int acts[8], n = r3_input_tick(&a->in, now_ms, acts, 8);
    for (int i = 0; i < n; i++) stick_dispatch(a, acts[i]);
}

static void app_tick(App *a, double ms) {
    engine_tick(&a->eng, ms);
    r3_marquee_clock(ms);
    a->stick_idle_ms += ms;
    if (a->auto_jump && !a->eng.playing) {                                          /* the music stopped: the panels Fn left come back */
        if (a->jump_view) view_set(a, a->auto_prev);
        a->auto_jump = 0;
    }
    if (a->mode == M_PLAYER && a->eng.playing && a->stick_idle_ms > 4000) {        /* the cue cursor follows the cue playing, unless the stick moved lately */
        int ci = cur_caption(a, engine_pos_sec(&a->eng) - a->offset);
        if (ci >= 0) a->csel = ci;
    }
    if (a->mode == M_PLAYER && a->eng.ended) {
        a->eng.ended = 0;
        if (a->rec) { say(a, R3_ST_HINT, "The end: Rec stopped, %d cue%s added.", a->nrec, a->nrec == 1 ? "" : "s"); a->rec = 0; }
        else say(a, R3_ST_HINT, "The end. Start plays it again.");
    }
    if (a->eng.dev && !a->piano && !engine_busy(&a->eng)) {          /* nothing sounds: after 2 s the device is closed (it wakes on the next play or note) */
        a->audio_idle_ms += ms;
        if (a->audio_idle_ms > 2000) engine_sleep(&a->eng);
    } else a->audio_idle_ms = 0;
    job_poll(a);
}

static void app_init(App *a, const char *data) {
    memset(a, 0, sizeof *a);
    a->open = -1;
    a->theme = r3_theme_find("Nord");
    a->kt_aac = 1;
    snprintf(a->data, sizeof a->data, "%s", data);
    a->job_fd = -1;
    a->sl = (SetItem *)calloc(SETLIST_MAX, sizeof *a->sl);
    a->hist = (HistItem *)calloc(HIST_MAX, sizeof *a->hist);
    a->stick_idle_ms = 1e9;
    r3_input_init(&a->in);
    r3_mm_init(&a->mm, 0);                  /* Benchmark is hidden: MusiCue has nothing to measure yet */
}

static void app_free(App *a) {
    job_stop(a);
    save_cues(a);
    close_song(a);
    free(a->songs);
    free(a->sl);
    free(a->hist);
}

/* ---- the tests ----------------------------------------------------------------------------------------------------------------------------- */

static int g_bad;
static char g_prog_file[4200];
/* ---- the device report (musicue_report.h) uses the main loop's wait, so it is defined here, before the tests that run the report ---- */
/* How long the main loop waits for an event and how often it looks while it waits (docs/POWER.md). Input in flight or something moving gets a frame every 16 ms; a song
 * playing on its own is 30 frames a second (the playhead moves a few pixels a second); a screen at rest looks every 100 ms, and an event wakes it at once. */
static void loop_pacing(App *a, int force, int settle, int toast, int *ms, int *step) {
    int busy = force || a->force || settle > 0 || toast || a->mode == M_JOB || r3_input_stick_active(&a->in);
    if (busy) { *ms = 16; *step = 8; }
    else if (a->eng.playing) { *ms = 33; *step = 20; }
    else { *ms = 250; *step = 100; }
}

static int wait_event(SDL_Event *e, int ms, int step) {
    Uint32 end = SDL_GetTicks() + (Uint32)ms;
    for (;;) {
        if (SDL_PollEvent(e)) return 1;
        int left = (int)(end - SDL_GetTicks());
        if (left <= 0) return 0;
        SDL_Delay((Uint32)(left < step ? left : step));
    }
}

#include "musicue_report.h"
#include "musicue_demo.h"

#define CHECK(cond, ...) do { if (!(cond)) { g_bad++; fprintf(stderr, "selftest FAIL (%s:%d): ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* A tap of a pad button through the same path the main loop uses, and a tap with a modifier held. */
static void press(App *a, int bt) { pad_button(a, bt, 1); pad_button(a, bt, 0); }
static void with(App *a, int mod, int bt) { pad_button(a, mod, 1); pad_button(a, bt, 1); pad_button(a, bt, 0); pad_button(a, mod, 0); }

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
    app_action(&a, R3_ACCEPT);
    CHECK(a.idbuf[0] == 'A' && a.idpos == 1, "A on the keyboard types the key under the cursor");
    app_action(&a, R3_UP);
    app_action(&a, R3_LEFT);
    CHECK(a.idgy == 3 && a.idgx == 15, "the D-pad wraps over the grid");
    app_action(&a, R3_ACCEPT);
    CHECK(a.idbuf[1] == '_' && a.idpos == 2, "the last key is _");
    app_action(&a, R3_X);
    app_action(&a, R3_X);
    a.idpos = 0; memset(a.idbuf, 0, sizeof a.idbuf);
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
    app_action(&a, R3_START);
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
    app_action(&a, R3_START);
    CHECK(a.mode == M_JOB, "online: the fetch starts at once");
    {   /* rule 13: the toast shows over the sync, a failure waits for a key and that key is used up (the main loop's order: key first, poll, compose, draw) */
        char hd[PATH_MAX], ev[PATH_MAX];
        snprintf(hd, sizeof hd, "%s/hp-sync", g_tmp);
        mkdir(hd, 0755);
        snprintf(ev, sizeof ev, "%s/events", hd);
        setenv("R3I6NOS_HOTPLUG_DIR", hd, 1);
        R3Toast tt;
        r3_toast_init(&tt);
        FILE *ef = fopen(ev, "w");
        if (ef) { fprintf(ef, "5\tinfo\tHeadphones in\n6\terror\tUSB device failed\n"); fclose(ef); }
        r3_toast_poll(&tt, 1000);                                 /* a failure goes first and stays until a key */
        R3Screen sc;
        compose(&a, &sc);
        r3_toast_draw(&tt, &sc);
        CHECK(a.mode == M_JOB && tt.showing && tt.error && sc.ch[R3_TOP_ROW * R3_COLS + 1] == 'U', "the failure toast is drawn over the sync's top bar");
        r3_toast_poll(&tt, 60000);
        CHECK(tt.showing && tt.error, "it does not time out");
        CHECK(r3_toast_key(&tt) && !tt.showing, "a key clears it and is used up");
        r3_toast_poll(&tt, 61000);
        CHECK(tt.showing && !tt.error && !r3_toast_key(&tt), "the change shows next, and a key does not touch it");
        unsetenv("R3I6NOS_HOTPLUG_DIR");
    }
    for (int i = 0; i < 300 && a.mode == M_JOB; i++) { app_tick(&a, 10); usleep(10000); }
    CHECK(a.mode == M_PLAYER && a.open >= 0 && !strcmp(a.songs[a.open].id, "dw4w9WgXcQs"), "the song it fetched is open (mode %d)", a.mode);
    app_free(&a);
    unsetenv("R3I6NOS_MUSICUE_SYNC");
    unsetenv("R3I6NOS_MUSICUE_ONLINE");
    unsetenv("R3I6NOS_MUSICUE_NOAUDIO");
}

/* A line too long for its cells scrolls in a closed loop after a hold, the way KlamANSI's does; one that fits, or with the clock off, does not. */
/* ---- Key and Tempo: the arithmetic, the screen, and the job against a fake musicue-render --------------------------------------------------------- */

static int song_with_id(const App *a, const char *id) { for (int i = 0; i < a->nsongs; i++) if (!strcmp(a->songs[i].id, id)) return i; return -1; }

static void write_script(const char *path, const char *body) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(body, f);
    fclose(f);
    chmod(path, 0755);
}

static void test_kt(void) {
    /* the arithmetic */
    KtKey k;
    CHECK(kt_parse_key("G", &k) && k.root == 7 && !k.minor, "G is root 7, major");
    CHECK(kt_parse_key("F#m", &k) && k.root == 6 && k.minor, "F#m is root 6, minor");
    CHECK(kt_parse_key("Bb", &k) && k.root == 10 && k.flat && !k.minor, "Bb is root 10, major, written flat");
    CHECK(kt_parse_key("Gm7", &k) && k.root == 7 && k.minor, "Gm7 is G minor");
    CHECK(kt_parse_key("Cmaj7", &k) && k.root == 0 && !k.minor, "Cmaj7 is C major (maj is not m)");
    CHECK(kt_parse_key("e minor", &k) && k.root == 4 && k.minor, "e minor is E minor");
    CHECK(kt_parse_key("Abm", &k) && k.root == 8 && k.minor && k.flat, "Abm is G# minor written flat");
    CHECK(!kt_parse_key("H", &k) && !kt_parse_key("", &k) && !kt_parse_key("?", &k), "H, nothing and ? are not keys");
    char nm[16];
    kt_key_name(&k, nm, sizeof nm);
    CHECK(kt_semitones(7, 9) == 2 && kt_semitones(9, 7) == -2 && kt_semitones(0, 6) == 6 && kt_semitones(0, 7) == -5 && kt_semitones(11, 0) == 1 && kt_semitones(3, 3) == 0, "the shift goes the nearer way: G to A +2, A to G -2, C to F# +6, C to G -5, B to C +1");
    CHECK(kt_tempo("96") == 96 && kt_tempo("104.5") == 104.5 && kt_tempo("") == 0 && kt_tempo("?") == 0 && kt_tempo("-3") == 0 && kt_tempo("fast") == 0, "a Tempo is a positive number or nothing");
    KtPlan p;
    CHECK(kt_plan(&p, "", "96") && kt_plan(&p, "G", "") && kt_plan(&p, "G", "?") && kt_plan(&p, "H", "96"), "no Key, no Tempo or a Key that is not a note is refused, saying what to fix");
    CHECK(kt_plan(&p, "Gm", "96") == NULL && p.base.minor && p.base_tempo == 96 && p.ratio == 1, "a plan starts as the song itself");
    CHECK(kt_update(&p) != NULL, "nothing to change is refused");
    p.target.root = 9; p.target_tempo = 104;
    CHECK(kt_update(&p) == NULL && p.semis == 2 && fabs(p.ratio - 104.0 / 96) < 1e-9, "G minor to A minor and 96 to 104: +2 and x1.0833");
    p.target_tempo = 200;
    CHECK(kt_update(&p) != NULL, "more than double is refused");
    p.target_tempo = 96; p.target.root = 7; p.target_tempo = 48;
    CHECK(kt_update(&p) == NULL && fabs(p.ratio - 0.5) < 1e-9, "half the tempo is the limit and is allowed");
    char i1[16], i2[16], i3[16];
    kt_copy_id("abc123", 2, 1.0833, i1, sizeof i1);
    kt_copy_id("abc123", 2, 1.0833, i2, sizeof i2);
    kt_copy_id("abc123", 3, 1.0833, i3, sizeof i3);
    CHECK(!strcmp(i1, i2) && strcmp(i1, i3) && strlen(i1) == 11 && kt_is_copy_id(i1) && cue_videoid_ok(i1), "a copy's id is the same for the same ask, different for another, 11 characters and a valid id");
    KtKey tk = {9, 0, 0};
    char name[128];
    kt_copy_name("Some Song [dQw4w9WgXcQ]", &tk, "104", i1, name, sizeof name);
    CHECK(!strncmp(name, "Some Song (A 104bpm) [kt-", 25), "the copy's name drops the base's id and says what it is: %s", name);
    CHECK(kt_estimate_s(240, 4, 0) > kt_estimate_s(240, 4, 1) && kt_estimate_s(240, 2, 0) > kt_estimate_s(240, 4, 0), "the estimate: faster with 4 workers and the cheaper preset");
    CHECK(kt_estimate_s(240, 4, 0) > 25 && kt_estimate_s(240, 4, 0) < 40, "a 4-minute song renders in about 25-40 s with 4 workers (the unit's 10.6x plus the margin): %.0f", kt_estimate_s(240, 4, 0));
    CHECK(fabs(kt_copy_bytes(60, 1.0, 0) - 60 * 176400.0) < 100, "a minute is 10 MB as a WAV");
    CHECK(kt_copy_bytes(60, 1.0, 1) < kt_copy_bytes(60, 1.0, 0) / 6 && kt_copy_bytes(60, 1.0, 1) > 1.2e6 && kt_copy_bytes(60, 1.0, 1) < 1.7e6, "and about 1.4 MB as AAC 192 kbps: %.0f", kt_copy_bytes(60, 1.0, 1));
    CHECK(kt_encode_s(240) > 45 && kt_encode_s(240) < 65, "compressing a 4-minute copy is estimated at about 50 s (the unit's 4.7x): %.0f s", kt_encode_s(240));

    /* the app: a song with a Key and a Tempo and cues, a fake render helper */
    App a;
    app_init(&a, g_tmp);
    char d[PATH_MAX], p2[PATH_MAX], fake[PATH_MAX], args[PATH_MAX], bat[PATH_MAX];
    snprintf(d, sizeof d, "%s/kt", g_tmp);
    snprintf(a.data, sizeof a.data, "%s", d);
    const char *sub[] = {"media", "cues", "inbox"};
    for (int i = 0; i < 3; i++) { sub_path(&a, sub[i], NULL, p2, sizeof p2); mkdirs(p2); }
    sub_path(&a, "media", "Song One.wav", p2, sizeof p2);
    write_wav(p2, 44100, 2, 16, 44100 * 3, 0);
    sub_path(&a, "media", "Song Two.wav", p2, sizeof p2);
    write_wav(p2, 44100, 2, 16, 44100 * 3, 0);
    scan_songs(&a);
    CHECK(a.nsongs == 2, "two songs");
    char one_id[16], two_id[16];
    snprintf(one_id, sizeof one_id, "%s", a.songs[0].id);
    snprintf(two_id, sizeof two_id, "%s", a.songs[1].id);
    {   /* Song One: Key G, Tempo 96, two markers, a caption, an offset of 0.5 s */
        CueSet cs; cue_init(&cs);
        snprintf(cs.videoid, sizeof cs.videoid, "%s", one_id);
        cue_add_marker(&cs, 1.0, "Verse", 0);
        cue_add_marker(&cs, 2.0, "Chorus", 0);
        cue_add_caption(&cs, 0.5, 1, 1.5, "hello");
        cue_meta_set(&cs, "key", "G"); cue_meta_set(&cs, "tempo", "96");
        CueBuf b = {0}; cue_write_version(&cs, &b, 1000000000);
        cue_path(&a, one_id, "json", p2, sizeof p2); cue_write_file(p2, b.p, b.n); free(b.p); cue_free(&cs);
        cue_path(&a, one_id, "offset", p2, sizeof p2); cue_write_file(p2, "0.500\n", 6);
    }
    snprintf(args, sizeof args, "%s/kt-args.txt", g_tmp);
    snprintf(fake, sizeof fake, "%s/fakerender", g_tmp);
    char script[2048];
    snprintf(script, sizeof script, "#!/bin/sh\necho \"$@\" > %s\nout=; in=\nwhile [ $# -gt 0 ]; do case \"$1\" in --in) in=\"$2\";; --out) out=\"$2\";; esac; shift; done\n"
             "echo \"PROGRESS 0/2 Rendering...\"\ncp \"$in\" \"$out.part\"\necho \"PROGRESS 1/2 Segment 1 of 2 joined\"\necho \"PROGRESS 2/2 Segment 2 of 2 joined\"\nmv \"$out.part\" \"$out\"\necho \"DONE 132300 frames in 0.1 s\"\n", args);
    write_script(fake, script);
    setenv("R3I6NOS_MUSICUE_RENDER", fake, 1);
    unsetenv("R3I6NOS_MUSICUE_BATTERY"); unsetenv("R3I6NOS_MUSICUE_FREE_MB");

    int i1x = song_with_id(&a, one_id), i2x = song_with_id(&a, two_id);
    CHECK(open_song(&a, i2x) && a.cues.meta.key[0] == 0, "Song Two opens with no Key");
    open_menu(&a, MK_SONG);
    int has = 0; for (int i = 0; i < a.menu.n; i++) if (a.menu.it[i].id == A_KT) has = 1;
    CHECK(has, "the song's options have Key and Tempo");
    kt_open(&a);
    CHECK(a.mode == M_PLAYER && strstr(a.reply, "no Key"), "a song with no Key says so and stays: %s", a.reply);
    close_song(&a);

    CHECK(open_song(&a, i1x) && !strcmp(a.cues.meta.key, "G") && !strcmp(a.cues.meta.tempo, "96"), "Song One opens with Key G and Tempo 96");
    kt_open(&a);
    CHECK(a.mode == M_KT && a.kt.base.root == 7 && a.kt.base_tempo == 96 && a.kt_workers == 4 && !a.kt_cheaper, "Key and Tempo opens on the song's own values, 4 workers, best quality");
    for (int th = 0; th < R3_THEME_COUNT; th++) { a.theme = th; apply_theme(&a); check_contrast(&a, "key and tempo"); }
    a.theme = r3_theme_find("Nord"); apply_theme(&a);
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_KT && a.job_pid == 0 && strstr(a.reply, "Nothing to change"), "A with nothing changed does not start: %s", a.reply);
    app_action(&a, R3_RIGHT); app_action(&a, R3_RIGHT);
    for (int i = 0; i < 8; i++) app_action(&a, R3_UP);
    CHECK(a.kt.target.root == 9 && a.kt.semis == 2 && a.kt.target_tempo == 104, "Right twice is A (+2) and Up eight times is 104");
    app_action(&a, R3_PAGEDOWN);
    CHECK(a.kt.target_tempo == 99, "R1+Down is 5 bpm");
    app_action(&a, R3_HOME);
    CHECK(a.kt.target.root == 7 && a.kt.target_tempo == 96, "R1+Left puts back the song's own");
    app_action(&a, R3_LEFT); app_action(&a, R3_LEFT); app_action(&a, R3_LEFT); app_action(&a, R3_RIGHT); app_action(&a, R3_RIGHT); app_action(&a, R3_RIGHT);
    for (int i = 0; i < 8; i++) app_action(&a, R3_DOWN);
    CHECK(a.kt.target_tempo == 88, "tempo can go down: 88");
    for (int i = 0; i < 100; i++) app_action(&a, R3_PAGEUP);
    CHECK(a.kt.target_tempo == 192, "and stops at double the song's: %g", a.kt.target_tempo);
    app_action(&a, R3_HOME);
    app_action(&a, R3_RIGHT); app_action(&a, R3_RIGHT);
    for (int i = 0; i < 8; i++) app_action(&a, R3_UP);
    app_action(&a, R3_X);
    CHECK(a.kt_workers == 2, "X makes it 2 workers");
    app_action(&a, R3_Y);
    CHECK(a.kt_cheaper == 1, "Y makes it the cheaper preset");

    /* refused before anything starts: a low battery, no room */
    snprintf(bat, sizeof bat, "%s/battery", g_tmp);
    mkdirs(bat);
    snprintf(p2, sizeof p2, "%s/capacity", bat); cue_write_file(p2, "12\n", 3);
    snprintf(p2, sizeof p2, "%s/status", bat); cue_write_file(p2, "Discharging\n", 12);
    setenv("R3I6NOS_MUSICUE_BATTERY", bat, 1);
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_KT && a.job_pid == 0 && strstr(a.reply, "battery"), "12%% and not charging: no render, and it says why: %s", a.reply);
    unsetenv("R3I6NOS_MUSICUE_BATTERY");
    setenv("R3I6NOS_MUSICUE_FREE_MB", "1", 1);
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_KT && a.job_pid == 0 && strstr(a.reply, "room"), "no room: no render, and it says so: %s", a.reply);
    unsetenv("R3I6NOS_MUSICUE_FREE_MB");

    /* the render */
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_JOB && a.job_kind == JOB_RENDER && a.job_pid > 0 && a.open < 0, "A starts the render on the job screen and frees the song");
    for (int i = 0; i < 400 && !(a.job_pid == 0 && a.job_fd < 0); i++) { app_tick(&a, 10); usleep(10000); }
    CHECK(a.job_done && a.job_frac == 1, "the job finishes with the bar full");
    char *argt = cue_read_file(args, NULL);
    CHECK(argt && strstr(argt, "--ratio 1.0833333") && strstr(argt, "--semitones 2") && strstr(argt, "--workers 2") && strstr(argt, "--cheaper") && strstr(argt, "--in") && strstr(argt, "Song One.wav") && strstr(argt, "--encode aac:192:fast") && strstr(argt, ".m4a"),
          "the helper got the ratio, the shift, the workers, the preset, the song and AAC into an M4A: %s", argt ? argt : "(none)");
    free(argt);
    int ci = song_with_id(&a, a.kt_copy_id);
    CHECK(ci >= 0 && strstr(a.songs[ci].name, "Song One (A 104bpm) [kt-") && a.lsel == ci, "the copy is in the songs, named for its Key and Tempo, and selected: %s", ci >= 0 ? a.songs[ci].name : "(missing)");
    {
        char *t = NULL; size_t n; CueSet c2; cue_init(&c2); char e2[96];
        cue_path(&a, a.kt_copy_id, "json", p2, sizeof p2);
        t = cue_read_file(p2, &n);
        CHECK(t && cue_load_json(&c2, t, n, 1, e2), "the copy has its own cue file");
        free(t);
        CHECK(c2.nm == 2 && fabs(c2.m[0].time - 1.0 / (104.0 / 96)) < 1e-3 && fabs(c2.m[1].time - 2.0 / (104.0 / 96)) < 1e-3, "its markers are the song's divided by the ratio: %.4f %.4f", c2.nm > 1 ? c2.m[0].time : -1, c2.nm > 1 ? c2.m[1].time : -1);
        CHECK(!strcmp(c2.meta.key, "A") && !strcmp(c2.meta.tempo, "104"), "its Key is A and its Tempo 104: %s %s", c2.meta.key, c2.meta.tempo);
        cue_free(&c2);
        cue_path(&a, a.kt_copy_id, "offset", p2, sizeof p2);
        t = cue_read_file(p2, NULL);
        CHECK(t && fabs(atof(t) - 0.5 / (104.0 / 96)) < 0.002, "its offset is scaled too: %s", t ? t : "(none)");
        free(t);
        cue_path(&a, a.kt_copy_id, "kt", p2, sizeof p2);
        t = cue_read_file(p2, NULL);
        CHECK(t && strstr(t, one_id) && strstr(t, "semitones=2") && strstr(t, "tempo=104"), "its sidecar names what it is a copy of");
        free(t);
        cue_path(&a, one_id, "json", p2, sizeof p2);
        t = cue_read_file(p2, &n);
        cue_init(&c2);
        CHECK(t && cue_load_json(&c2, t, n, 1, e2) && c2.nm == 2 && fabs(c2.m[0].time - 1.0) < 1e-9 && !strcmp(c2.meta.key, "G"), "the song's own cues are untouched");
        free(t); cue_free(&c2);
    }
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_LIBRARY, "A closes the job screen");

    /* the same ask again finds the copy; a copy cannot be copied */
    CHECK(open_song(&a, song_with_id(&a, one_id)), "Song One again");
    kt_open(&a);
    app_action(&a, R3_RIGHT); app_action(&a, R3_RIGHT);
    for (int i = 0; i < 8; i++) app_action(&a, R3_UP);
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_LIBRARY && a.job_pid == 0 && strstr(a.reply, "already made"), "the same Key and Tempo again starts nothing: %s", a.reply);
    CHECK(open_song(&a, song_with_id(&a, a.kt_copy_id)), "the copy opens");
    kt_open(&a);
    CHECK(a.mode == M_PLAYER && strstr(a.reply, "already a copy"), "a copy is not copied again: %s", a.reply);
    close_song(&a);

    /* the format: AAC is the default, Select makes it a WAV, and the choice is kept in settings.conf */
    CHECK(strstr(a.kt_out, ".m4a") != NULL, "the first copy was an M4A: %s", a.kt_out);
    char conf[PATH_MAX];
    snprintf(conf, sizeof conf, "%s/kt-settings.conf", g_tmp);
    setenv("R3I6NOS_MUSICUE_CONF", conf, 1);
    CHECK(open_song(&a, song_with_id(&a, one_id)), "Song One for the WAV choice");
    kt_open(&a);
    CHECK(a.kt_aac == 1, "AAC is the default format");
    app_action(&a, R3_SELECT);
    CHECK(a.kt_aac == 0, "Select makes the copy a WAV");
    {
        App b;
        app_init(&b, g_tmp);
        CHECK(b.kt_aac == 1, "a fresh app starts on AAC");
        load_settings(&b);
        CHECK(b.kt_aac == 0, "and the choice is kept in settings.conf");
        app_free(&b);
    }
    for (int th = 0; th < R3_THEME_COUNT; th++) { a.theme = th; apply_theme(&a); check_contrast(&a, "key and tempo (WAV)"); }
    a.theme = r3_theme_find("Nord"); apply_theme(&a);
    for (int i = 0; i < 3; i++) app_action(&a, R3_RIGHT);
    unlink(args);
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_JOB && a.job_pid > 0, "a WAV copy starts");
    for (int i = 0; i < 400 && !(a.job_pid == 0 && a.job_fd < 0); i++) { app_tick(&a, 10); usleep(10000); }
    argt = cue_read_file(args, NULL);
    CHECK(argt && !strstr(argt, "--encode") && strstr(argt, ".wav") && strstr(argt, "--semitones 3"), "a WAV copy asks the helper for no encoding and a .wav: %s", argt ? argt : "(none)");
    free(argt);
    CHECK(wav_copies(&a) == 1, "it is the one WAV copy");
    app_action(&a, R3_ACCEPT);

    /* Compress copies to AAC: the library menu counts the WAV copies, the helper turns them into M4A files with the same id, so their cues stay */
    build_menu(&a, MK_LIBRARY);
    int ci2 = -1; for (int i = 0; i < a.menu.n; i++) if (a.menu.it[i].id == A_COMPRESS) ci2 = i;
    CHECK(ci2 >= 0 && strstr(a.menu.it[ci2].value, "1 WAV copy"), "the library menu offers it and counts 1 WAV copy: %s", ci2 >= 0 ? a.menu.it[ci2].value : "(no item)");
    char cargs[PATH_MAX];
    snprintf(cargs, sizeof cargs, "%s/kt-cargs.txt", g_tmp);
    snprintf(script, sizeof script, "#!/bin/sh\necho \"$@\" > %s\ndir=\nwhile [ $# -gt 0 ]; do case \"$1\" in --compress-dir) dir=\"$2\";; esac; shift; done\n"
             "for f in \"$dir\"/*\\ \\[kt-*.wav; do [ -e \"$f\" ] || continue; echo \"PROGRESS 0/1 $f\"; cp \"$f\" \"${f%%.wav}.m4a\"; rm \"$f\"; done\necho \"DONE 1 copies compressed, 0.0 MiB saved\"\n", cargs);
    write_script(fake, script);
    char copy_id[16];
    snprintf(copy_id, sizeof copy_id, "%s", a.songs[0].id);
    for (int i = 0; i < a.nsongs; i++) if (kt_is_copy_id(a.songs[i].id) && strstr(a.songs[i].path, ".wav")) snprintf(copy_id, sizeof copy_id, "%s", a.songs[i].id);
    compress_start(&a);
    CHECK(a.mode == M_JOB && a.job_kind == JOB_COMPRESS && a.job_pid > 0, "Compress starts a job");
    for (int i = 0; i < 400 && !(a.job_pid == 0 && a.job_fd < 0); i++) { app_tick(&a, 10); usleep(10000); }
    argt = cue_read_file(cargs, NULL);
    CHECK(argt && strstr(argt, "--compress-dir") && strstr(argt, "/media") && strstr(argt, "--encode aac:192:fast"), "the helper was asked to compress the media folder as AAC 192: %s", argt ? argt : "(none)");
    free(argt);
    CHECK(a.job_done && wav_copies(&a) == 0, "afterwards no copy is a WAV");
    {
        int ix = song_with_id(&a, copy_id);
        CHECK(ix >= 0 && strstr(a.songs[ix].path, ".m4a"), "the same copy, same id, is an M4A now: %s", ix >= 0 ? a.songs[ix].path : "(gone)");
        cue_path(&a, copy_id, "json", p2, sizeof p2);
        CHECK(access(p2, R_OK) == 0, "and its cue file is still there");
    }
    app_action(&a, R3_ACCEPT);
    compress_start(&a);
    CHECK(a.mode == M_LIBRARY && a.job_pid == 0 && strstr(a.reply, "No WAV copies"), "with none left it says so: %s", a.reply);
    /* B stops a compress and removes the half-written M4A */
    sub_path(&a, "media", "Dummy copy [kt-deadbeef].wav", p2, sizeof p2);
    write_wav(p2, 44100, 2, 16, 44100 * 2, 0);
    scan_songs(&a);
    snprintf(script, sizeof script, "#!/bin/sh\ndir=\nwhile [ $# -gt 0 ]; do case \"$1\" in --compress-dir) dir=\"$2\";; esac; shift; done\necho half > \"$dir/Dummy copy [kt-deadbeef].m4a.enc\"\necho \"PROGRESS 0/1 working\"\nsleep 20\n");
    write_script(fake, script);
    compress_start(&a);
    char encp[PATH_MAX];
    sub_path(&a, "media", "Dummy copy [kt-deadbeef].m4a.enc", encp, sizeof encp);
    for (int i = 0; i < 100 && access(encp, F_OK) != 0; i++) { app_tick(&a, 10); usleep(10000); }
    CHECK(a.mode == M_JOB && access(encp, F_OK) == 0, "a compress is running with its half-written file");
    app_action(&a, R3_BACK);
    CHECK(a.mode == M_LIBRARY && a.job_pid == 0 && access(encp, F_OK) != 0, "B stops it and removes the half-written file");
    unlink(p2);
    scan_songs(&a);
    snprintf(conf, sizeof conf, "%s/kt-settings.conf", g_tmp);
    unlink(conf);
    unsetenv("R3I6NOS_MUSICUE_CONF");
    /* put the AAC default and the first fake helper back for what follows */
    a.kt_aac = 1;
    snprintf(script, sizeof script, "#!/bin/sh\necho \"$@\" > %s\nout=; in=\nwhile [ $# -gt 0 ]; do case \"$1\" in --in) in=\"$2\";; --out) out=\"$2\";; esac; shift; done\n"
             "echo \"PROGRESS 0/2 Rendering...\"\ncp \"$in\" \"$out.part\"\necho \"PROGRESS 2/2 Segment 2 of 2 joined\"\nmv \"$out.part\" \"$out\"\necho \"DONE 132300 frames in 0.1 s\"\n", args);
    write_script(fake, script);

    /* B stops a render and takes the half copy away */
    snprintf(script, sizeof script, "#!/bin/sh\nout=\nwhile [ $# -gt 0 ]; do case \"$1\" in --out) out=\"$2\";; esac; shift; done\necho \"PROGRESS 0/4 Rendering...\"\necho half > \"$out.part\"\necho half > \"$out.journal\"\nsleep 20\n");
    write_script(fake, script);
    CHECK(open_song(&a, song_with_id(&a, one_id)), "Song One a third time");
    kt_open(&a);
    app_action(&a, R3_LEFT);
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_JOB && a.job_pid > 0, "another ask starts a render");
    for (int i = 0; i < 100; i++) { app_tick(&a, 10); usleep(10000); char pp[PATH_MAX + 8]; snprintf(pp, sizeof pp, "%s.part", a.kt_out); if (access(pp, F_OK) == 0) break; }
    char pp[PATH_MAX + 16];
    snprintf(pp, sizeof pp, "%s.part", a.kt_out);
    CHECK(access(pp, F_OK) == 0, "the half copy is there while it runs");
    app_action(&a, R3_BACK);
    snprintf(pp, sizeof pp, "%s.part", a.kt_out);
    char pj[PATH_MAX + 16];
    snprintf(pj, sizeof pj, "%s.journal", a.kt_out);
    CHECK(a.mode == M_LIBRARY && a.job_pid == 0 && access(pp, F_OK) != 0 && access(pj, F_OK) != 0, "B stops it and removes the part and the journal");
    CHECK(access(a.kt_out, F_OK) != 0, "and there is no copy");

    /* the helper is missing */
    setenv("R3I6NOS_MUSICUE_RENDER", "/nonexistent/musicue-render", 1);
    CHECK(open_song(&a, song_with_id(&a, one_id)), "Song One a fourth time");
    kt_open(&a);
    app_action(&a, R3_RIGHT);
    app_action(&a, R3_ACCEPT);
    for (int i = 0; i < 300 && !(a.job_pid == 0 && a.job_fd < 0); i++) { app_tick(&a, 10); usleep(10000); }
    CHECK(a.job_done && strstr(a.reply, "missing") && a.job_nlines > 0 && strstr(a.job_lines[a.job_nlines - 1], "not installed"), "no helper: it says so on the job screen and the line: %s", a.reply);
    unsetenv("R3I6NOS_MUSICUE_RENDER");
    app_free(&a);
}

/* ---- the device report: the menu asks first, and the report has every section and ends with `end` (docs/DEVICE-REPORTS.md) ----------------------------------- */

/* ---- the sound device sleeps while nothing plays and wakes for the next play or note (an open, paused device wakes ~40 times a second: docs/POWER.md) ---- */

static void test_sound_sleep(void) {
    unsetenv("R3I6NOS_MUSICUE_NOAUDIO");
    App a;
    app_init(&a, g_tmp);
    char d[PATH_MAX], p[PATH_MAX];
    snprintf(d, sizeof d, "%s/sleep", g_tmp);
    snprintf(a.data, sizeof a.data, "%s", d);
    sub_path(&a, "media", NULL, p, sizeof p);
    mkdirs(p);
    sub_path(&a, "media", "Sleep Song.wav", p, sizeof p);
    write_wav(p, 44100, 2, 16, 44100 * 5, 0);
    scan_songs(&a);
    CHECK(open_song(&a, 0) && a.eng.dev != 0 && !a.eng.noaudio && !a.eng.asleep, "a song opens with the sound device open (SDL's dummy driver)");
    app_tick(&a, 1500);
    CHECK(a.eng.dev != 0, "1.5 s of quiet: the device is still open");
    app_tick(&a, 600);
    CHECK(a.eng.dev == 0 && a.eng.asleep, "after 2 s of quiet the device is closed");
    double p0 = engine_pos_sec(&a.eng);
    engine_play(&a.eng, 1);
    CHECK(a.eng.dev != 0 && !a.eng.asleep, "play wakes it");
    SDL_Delay(300);
    CHECK(engine_pos_sec(&a.eng) - p0 > 0.15, "and the song plays: %.2f s in 0.3 s", engine_pos_sec(&a.eng) - p0);
    app_tick(&a, 5000);
    CHECK(a.eng.dev != 0, "while it plays the device stays open however long that is");
    engine_play(&a.eng, 0);
    a.piano = 1;
    app_tick(&a, 5000);
    CHECK(a.eng.dev != 0, "while the Piano is open it stays open");
    a.piano = 0;
    app_tick(&a, 2100);
    CHECK(a.eng.dev == 0, "and closes again when the Piano is shut");
    engine_note_on(&a.eng, 3, 440.0);
    CHECK(a.eng.dev != 0 && engine_busy(&a.eng), "a piano note wakes it");
    app_tick(&a, 3000);
    CHECK(a.eng.dev != 0, "a ringing voice keeps it open");
    engine_notes_off(&a.eng);
    app_free(&a);
    /* the loop's pacing: at rest it looks every 100 ms, a song alone is 30 frames a second, anything moving is 16 ms */
    App b;
    app_init(&b, g_tmp);
    int ms, step;
    loop_pacing(&b, 0, 0, 0, &ms, &step);
    CHECK(ms == 250 && step == 100, "at rest the loop looks every 100 ms (%d, %d)", ms, step);
    b.eng.playing = 1;
    loop_pacing(&b, 0, 0, 0, &ms, &step);
    CHECK(ms == 33 && step == 20, "a song playing alone is 30 frames a second (%d, %d)", ms, step);
    loop_pacing(&b, 1, 0, 0, &ms, &step);
    CHECK(ms == 16 && step == 8, "input in flight is 16 ms (%d, %d)", ms, step);
    b.eng.playing = 0;
    app_free(&b);
}

static int screen_has(const R3Screen *s, const char *text) {        /* does any row of the screen contain this text? */
    char row[R3_COLS + 1];
    for (int y = 0; y < R3_ROWS; y++) {
        for (int x = 0; x < R3_COLS; x++) { int c = s->ch[y * R3_COLS + x]; row[x] = c >= 32 && c < 127 ? (char)c : ' '; }
        row[R3_COLS] = 0;
        if (strstr(row, text)) return 1;
    }
    return 0;
}

static void test_report(void) {
    App a;
    app_init(&a, g_tmp);
    /* the main menu's Device report opens the question; B does not run it, A does */
    a.mode = M_LIBRARY;
    mm_open(&a);
    for (int i = 0; i < 3; i++) dispatch(&a, R3_DOWN);
    dispatch(&a, R3_ACCEPT);
    CHECK(a.overlay == OV_REPORT && !a.pending_report, "Device report in the main menu asks first");
    R3Screen s;
    compose(&a, &s);
    CHECK(screen_has(&s, "unplug the charger"), "it says what the report needs");
    check_contrast(&a, "device report question");
    dispatch(&a, R3_BACK);
    CHECK(a.overlay == OV_NONE && !a.pending_report, "B cancels it");
    a.overlay = OV_REPORT;
    dispatch(&a, R3_ACCEPT);
    CHECK(a.overlay == OV_NONE && a.pending_report, "A runs it");
    a.pending_report = 0;
    app_free(&a);

    /* the report itself, on SDL's dummy drivers, with short windows and a short song, in a throwaway data folder */
    setenv("SDL_VIDEODRIVER", "dummy", 0);
    setenv("SDL_AUDIODRIVER", "dummy", 0);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { CHECK(0, "SDL video would not start: %s", SDL_GetError()); return; }
    SDL_Window *win = SDL_CreateWindow("report", 0, 0, R3_W, R3_H, 0);
    SDL_Renderer *r = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : NULL;
    R3Video video;
    R3Toast toast;
    r3_toast_init(&toast);
    int up = r && r3_video_init(&video, r);
    CHECK(up, "a window and a renderer on the dummy driver");
    if (up) {
        App b;
        app_init(&b, g_tmp);
        char d[PATH_MAX], p[PATH_MAX + 64], fake[PATH_MAX];
        snprintf(d, sizeof d, "%s/report", g_tmp);
        snprintf(b.data, sizeof b.data, "%s", d);
        const char *sub[] = {"media", "cues"};
        for (int i = 0; i < 2; i++) { sub_path(&b, sub[i], NULL, p, sizeof p); mkdirs(p); }
        sub_path(&b, "media", "Own Song.wav", p, sizeof p);
        write_wav(p, 44100, 2, 16, 44100 * 3, 0);
        snprintf(fake, sizeof fake, "%s/fakereportrender", g_tmp);
        write_script(fake, "#!/bin/sh\nin=; out=\nwhile [ $# -gt 0 ]; do case \"$1\" in --in) in=\"$2\";; --out) out=\"$2\";; --ratio) case \"$2\" in 9) exit 2;; esac;; esac; shift; done\n"
                           "[ -n \"$in\" ] || exit 2\ncp \"$in\" \"$out\"\necho \"DONE AAC 192k, 0.1 MiB, 1 frames in 0.1 s\"\n");
        setenv("R3I6NOS_MUSICUE_RENDER", fake, 1);
        setenv("R3I6NOS_REPORT_POWER_S", "1", 1);
        setenv("R3I6NOS_REPORT_SONG_S", "10", 1);
        sub_path(&b, "media", NULL, p, sizeof p);
        snprintf(g_prog_file, sizeof g_prog_file, "%s/progress", g_tmp);
        setenv("R3I6NOS_REPORT_PROGRESS", g_prog_file, 1);       /* Settings > Device reports sets this: the bar's file */
        b.force = 1;                                    /* as the menu item leaves it: the report must not keep the loop busy (first unit report) */
        int rc = musicue_report(&b, r, &video, &toast, 1);
        CHECK(rc == 0, "the report is written (rc %d)", rc);
        CHECK(b.force == 0, "the report clears the main loop's force flag, or idle polls every 8 ms");
        CHECK(g_rpt_prog.max_step == RPT_STEPS && !g_rpt_prog.on, "the report's progress reaches step %d of %d and ends (rule 18; reached %d)", RPT_STEPS, RPT_STEPS, g_rpt_prog.max_step);
        CHECK(g_rpt_prog.drawn >= RPT_STEPS, "the bar was drawn at every step (%d draws)", g_rpt_prog.drawn);
        {
            char *pg = cue_read_file(g_prog_file, NULL);
            CHECK(pg && strstr(pg, "step=13\n") && strstr(pg, "steps=13\n") && strstr(pg, "r36s-report.txt"), "the progress file ends at step=13 steps=13 and names the report");
            free(pg);
        }
        unsetenv("R3I6NOS_REPORT_PROGRESS");
        snprintf(p, sizeof p, "%s/r36s-report.txt", d);
        char *t = cue_read_file(p, NULL);
        CHECK(t != NULL, "r36s-report.txt is in the data folder");
        if (t) {
            const char *need[] = {"== system ==", "== card ==", "== helpers ==", "== songs ==", "== sound ==", "== screen ==", "== power ==", "== key and tempo ==", "== summary ==", NULL};
            for (int i = 0; need[i]; i++) CHECK(strstr(t, need[i]) != NULL, "the report has %s", need[i]);
            CHECK(strstr(t, "open a WAV") && strstr(t, "yours (.wav) Own Song"), "it times the app's own open of a WAV and of one of the person's songs");
            CHECK(strstr(t, "idle") && strstr(t, "playing") && strstr(t, "rendering"), "the power section has idle, playing and rendering");
            CHECK(strstr(t, "musicue-render") && strstr(t, "ffmpeg aac encoder") && strstr(t, "ffmpeg ipod muxer") && strstr(t, "ffmpeg s16le muxer"), "the helpers section checks the renderer and the muxers MusiCue needs");
            size_t n = strlen(t);
            CHECK(n > 4 && !strcmp(t + n - 4, "end\n"), "and it ends with `end`");
            CHECK(strstr(t, "no personal") == NULL, "no stray text");
            free(t);
        }
        snprintf(p, sizeof p, "%s/.report-work", d);
        CHECK(access(p, F_OK) != 0, "the test files are gone");
        CHECK(b.mode == M_LIBRARY && b.open < 0, "the app is back on the songs, nothing open");
        unsetenv("R3I6NOS_MUSICUE_RENDER"); unsetenv("R3I6NOS_REPORT_POWER_S"); unsetenv("R3I6NOS_REPORT_SONG_S");
        app_free(&b);
        r3_video_free(&video);
    }
    if (r) SDL_DestroyRenderer(r);
    if (win) SDL_DestroyWindow(win);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

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

static void row_text(const R3Screen *s, int row, char *out) {
    for (int i = 0; i < R3_COLS; i++) out[i] = s->ch[row * R3_COLS + i] >= 32 && s->ch[row * R3_COLS + i] < 127 ? (char)s->ch[row * R3_COLS + i] : ' ';
    out[R3_COLS] = 0;
}

/* The cue panel on rows 7-8, by Cue's rules: the line now, the next one, the beat dot, and the two draining bars at the last cue. */
static void test_cue_panel(void) {
    App a;
    app_init(&a, g_tmp);
    open_demo(&a);
    char r7[R3_COLS + 1], r8[R3_COLS + 1];
    R3Screen s;
    engine_seek(&a.eng, 12);                                        /* the second cue (9.6 s) is playing */
    engine_play(&a.eng, 1);
    compose(&a, &s);
    row_text(&s, 7, r7);
    row_text(&s, 8, r8);
    CHECK(strstr(r7, "C   G   Am  F") && !strchr(r7, '\x1f'), "row 7 is the cue playing now: %s", r7);
    CHECK(strstr(r8, "(hold the chord)"), "row 8 is the next cue (the third is the hold line): %s", r8);
    engine_seek(&a.eng, 20);                                        /* the third (19.2 s) */
    compose(&a, &s);
    row_text(&s, 8, r8);
    CHECK(strstr(r8, "C   G   Am  F"), "the next line follows: %s", r8);
    int dots = 0;
    for (double t = 12; t < 12.7; t += 0.05) {                      /* 0.6 s a beat: the dot changes shape through a beat */
        engine_seek(&a.eng, t);
        compose(&a, &s);
        dots += s.ch[7 * R3_COLS + 62] == 10 || s.ch[7 * R3_COLS + 62] == 7;
    }
    CHECK(dots == 14, "the beat dot is there while playing (%d)", dots);
    engine_play(&a.eng, 0);
    compose(&a, &s);
    CHECK(s.ch[7 * R3_COLS + 62] == 32, "no beat dot when paused");
    /* the bars: at the last cue, full at its start, draining to the middle one cue's length later */
    cue_free(&a.cues);
    cue_init(&a.cues);
    for (int i = 0; i < 4; i++) cue_add_caption(&a.cues, i * 10.0, 0, 0, "line");
    cue_meta_set(&a.cues, "tempo", "96");
    cue_meta_set(&a.cues, "sig", "4/4");
    engine_play(&a.eng, 1);
    engine_seek(&a.eng, 30.0);
    compose(&a, &s);
    int filled = 0;
    for (int c = 0; c < R3_COLS; c++) filled += s.ch[8 * R3_COLS + c] == 32 && s.bg[8 * R3_COLS + c] != R3_BLACK;
    CHECK(filled == 60, "full bars at the last cue: %d of 60 cells", filled);
    engine_seek(&a.eng, 35.0);
    compose(&a, &s);
    filled = 0;
    for (int c = 0; c < R3_COLS; c++) filled += s.ch[8 * R3_COLS + c] == 32 && s.bg[8 * R3_COLS + c] != R3_BLACK;
    CHECK(filled >= 28 && filled <= 32, "half drained half way: %d", filled);
    CHECK(s.ch[8 * R3_COLS + 30] == 32 && s.bg[8 * R3_COLS + 30] != R3_BLACK && s.bg[8 * R3_COLS + 33] != R3_BLACK && s.ch[8 * R3_COLS + 5] == 176, "the fill hugs the middle, the track shows at the edges");
    engine_seek(&a.eng, 20.0);
    compose(&a, &s);
    filled = 0;
    for (int c = 0; c < R3_COLS; c++) filled += s.ch[8 * R3_COLS + c] == 32 && s.bg[8 * R3_COLS + c] != R3_BLACK;
    CHECK(filled == 0, "no bars before the last cue (%d)", filled);
    engine_seek(&a.eng, 25.0);
    cue_here(&a);
    CHECK(a.cues.nc == 5 && !strcmp(a.cues.c[3].text, "- - - -") && fabs(a.cues.c[3].start - 25.0) < 0.1, "Cue here stamps a cue at the playhead");
    app_free(&a);
}

/* The panels, the pad's new map, the Tempo logic of Cue, the Set List and the songs opened here. */
static void test_panels_and_tempo(void) {
    App a;
    app_init(&a, g_tmp);
    char d[PATH_MAX];
    snprintf(d, sizeof d, "%s/panels", g_tmp);
    snprintf(a.data, sizeof a.data, "%s", d);
    mkdirs(d);
    setenv("R3I6NOS_MUSICUE_NOAUDIO", "1", 1);
    open_demo(&a);
    R3Screen s;

    /* the carousel: Fn turns it one place (six pairs, then the Jump panel on the whole width); two panels show, the stick hops between them and moves in the one it is in */
    CHECK(panel_at(&a, 0) == P_MARKERS && panel_at(&a, 1) == P_CUES, "Markers and Cues first");
    press(&a, R3_BTN_FN);
    CHECK(panel_at(&a, 0) == P_CUES && panel_at(&a, 1) == P_META, "Fn: Cues and Metadata");
    for (int i = 0; i < 5; i++) press(&a, R3_BTN_FN);
    CHECK(a.jump_view && view_code(&a) == P_COUNT, "six more turns reach the Jump panel");
    press(&a, R3_BTN_FN);
    CHECK(!a.jump_view && panel_at(&a, 0) == P_MARKERS && panel_at(&a, 1) == P_CUES, "and one more comes round to Markers and Cues");
    with(&a, R3_BTN_R1, R3_BTN_FN);
    CHECK(a.jump_view, "R1+Fn goes back to the panel shown before (the Jump panel)");
    with(&a, R3_BTN_R1, R3_BTN_FN);
    CHECK(!a.jump_view && a.pan_first == 0, "R1+Fn again comes back, like Alt+Tab");
    press(&a, R3_BTN_FN);
    press(&a, R3_BTN_FN);
    with(&a, R3_BTN_L1, R3_BTN_FN);
    CHECK(!a.jump_view && a.pan_first == 0, "L1+Fn is the first panel");
    CHECK(a.pan_focus == 0, "the left panel has the stick");
    stick_dispatch(&a, R3_DOWN);
    CHECK(a.msel == 1, "the stick moves the selection in the left panel");
    stick_dispatch(&a, R3_RIGHT);
    CHECK(a.pan_focus == 1, "right hops to the right panel");
    stick_dispatch(&a, R3_DOWN);
    stick_dispatch(&a, R3_DOWN);
    CHECK(a.csel == 2 && a.msel == 1, "and moves there");
    stick_dispatch(&a, R3_PAGEDOWN);
    CHECK(a.csel == a.cues.nc - 1, "R1 held pages (the end of a short list)");
    stick_dispatch(&a, R3_HOME);
    CHECK(a.csel == 0, "and Home goes to the first");
    stick_dispatch(&a, R3_LEFT);
    CHECK(a.pan_focus == 0, "left hops back");
    compose(&a, &s);
    CHECK(s.ch[9 * R3_COLS + 0] == 201 && s.ch[9 * R3_COLS + 32] == 218, "the panel with the stick has the double frame, the other the single");

    /* Select is 0:00; Start plays and stops */
    engine_seek(&a.eng, 33.0);
    press(&a, R3_BTN_SELECT);
    CHECK(engine_pos_sec(&a.eng) == 0, "Select goes to 0:00");
    press(&a, R3_BTN_START);
    CHECK(a.eng.playing && !a.rec, "Start plays (Play mode)");
    press(&a, R3_BTN_START);
    CHECK(!a.eng.playing, "Start stops");
    a.mode = M_LIBRARY;
    dispatch(&a, R3_MENU);
    CHECK(a.overlay == OV_MM && a.mode == M_LIBRARY, "L1+R1 opens the main menu");
    dispatch(&a, R3_MENU);
    CHECK(a.overlay == OV_NONE, "and closes it");
    a.mode = M_PLAYER;

    /* Tempo, as Cue does it: 16 beats a cue in 4/4 (8 in 6/8), cues 9.6 s apart are 100 bpm */
    CueSet *c = &a.cues;
    cue_free(c); cue_init(c);
    for (int i = 0; i < 5; i++) cue_add_caption(c, i * 9.6, 0, 0, "x");
    CHECK(fabs(gap_tempo(&a, 9.6) - 100) < 1e-9, "9.6 s of 4/4 is 100 bpm");
    cue_meta_set(c, "sig", "6/8");
    CHECK(fabs(gap_tempo(&a, 4.8) - 100) < 1e-9, "4.8 s of 6/8 is 100 bpm");
    cue_meta_set(c, "sig", "4/4");
    CHECK(classify_dot(100, 100) == DOT_GREEN && classify_dot(102, 100) == DOT_GREEN && classify_dot(102.1, 100) == DOT_YELLOW && classify_dot(105, 100) == DOT_YELLOW && classify_dot(105.1, 100) == DOT_RED && classify_dot(94.9, 100) == DOT_RED, "green within 2, yellow within 5, else red");
    CHECK(tempo_unset(&a) && set_tempo(&a) == 0, "a new song has no Tempo");
    CHECK(maybe_suggest_tempo(&a) && !strcmp(c->meta.tempo, "100"), "four cues of 9.6 s suggest 100: %s", c->meta.tempo);
    cue_meta_set(c, "tempo", "?");
    cue_add_caption(c, 4 * 9.6 + 10.0, 0, 0, "odd");             /* a gap of 10 s: 96 bpm, within 4 of 100: yellow, not in the cluster of four */
    cue_sort(c);
    double sug;
    unsigned char in[16];
    CHECK(suggest_cluster(&a, &sug, in) && fabs(sug - 100) < 1e-9 && in[0] && in[3] && !in[4], "the largest group within 2 bpm is the four 100s: %.2f", sug);
    CHECK(cue_dot(&a, 4, 0, in, sug) == DOT_YELLOW && cue_dot(&a, 0, 0, in, sug) == DOT_GREEN && cue_dot(&a, 5, 0, in, sug) == DOT_NONE, "outside the group is yellow; the last cue has no dot");
    cue_meta_set(c, "tempo", "120");
    CHECK(!maybe_suggest_tempo(&a) && !strcmp(c->meta.tempo, "120"), "a Tempo that was typed is never overwritten");
    CHECK(cue_dot(&a, 0, 120, NULL, 0) == DOT_RED, "100 against 120 is red");
    cue_meta_set(c, "tempo", "");
    CHECK(tempo_unset(&a) && maybe_suggest_tempo(&a), "blank is unset too");
    cue_meta_set(c, "tempo", "98.5");
    CHECK(fabs(set_tempo(&a) - 98.5) < 1e-9, "decimals are fine");
    char tv[16];
    fmt_tempo(96.0, tv, sizeof tv); CHECK(!strcmp(tv, "96"), "96 not 96.00: %s", tv);
    fmt_tempo(96.5, tv, sizeof tv); CHECK(!strcmp(tv, "96.5"), "96.5: %s", tv);
    fmt_tempo(96.125, tv, sizeof tv); CHECK(!strcmp(tv, "96.13") || !strcmp(tv, "96.12"), "at most two decimals: %s", tv);
    cue_meta_set(c, "sig", "7/4");
    CHECK(!sig_valid(c->meta.sig) && sig_beats(&a) == 16, "an unknown Sig counts as 4/4");

    /* the Metadata panel: A on a row edits it (Sig toggles), Tempo checks its number and an empty one is suggested */
    a.csel = 0;
    cue_meta_set(c, "sig", "4/4");
    a.pan_first = P_CUES;                                          /* Cues left, Metadata right */
    a.pan_focus = 1;
    a.psel[P_META] = 2;
    press(&a, R3_BTN_A);
    CHECK(!strcmp(c->meta.sig, "6/8"), "A on Sig toggles it");
    a.psel[P_META] = 1;
    press(&a, R3_BTN_A);
    CHECK(a.mode == M_TEXT && !strcmp(a.tbuf, "98.5"), "A on Tempo opens it");
    snprintf(a.tbuf, sizeof a.tbuf, "fast"); a.tlen = 4;
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_TEXT && !strcmp(c->meta.tempo, "98.5"), "a Tempo that is not a number is refused");
    a.tbuf[0] = 0; a.tlen = a.tpos = 0;
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_PLAYER && !tempo_unset(&a), "an empty Tempo is suggested from the cues: %s", c->meta.tempo);
    a.psel[P_META] = 0;
    press(&a, R3_BTN_A);
    piano_close(&a);                                               /* the piano is tested in test_pad_map; here the field is typed with Up and Down */
    a.tbuf[0] = 0; a.tlen = a.tpos = 0;
    app_action(&a, R3_UP);
    app_action(&a, R3_UP);
    app_action(&a, R3_RIGHT);
    app_action(&a, R3_UP);
    CHECK(!strcmp(a.tbuf, "BA"), "the pad types a key (Up starts A, Up again B, Right moves on, Up starts the next A): %s", a.tbuf);
    app_action(&a, R3_ACCEPT);
    CHECK(!strcmp(c->meta.key, "BA") && c->dirty, "and keeps it");
    compose(&a, &s);                                               /* Metadata on screen */

    /* delete a cue and bookmark a cue (a marker on its time) */
    a.pan_first = P_MARKERS;
    a.pan_focus = 1;
    int n0 = c->nc;
    a.csel = 1;
    cue_bookmark(&a, 1);
    CHECK(c->nm == 1 && marker_at(&a, c->c[1].start) == 0, "bookmarking a cue puts a marker on its time");
    cue_bookmark(&a, 1);
    CHECK(c->nm == 0, "and again takes it off");
    cue_bookmark(&a, 1);
    cue_delete(&a, 1);
    CHECK(c->nc == n0 - 1 && c->nm == 0, "deleting a cue takes its marker with it");

    /* the Set List: add (once), move, remove, and it is on the card in setlist.tsv */
    setlist_load(&a);
    CHECK(a.nsl == 0, "empty at first");
    CHECK(setlist_add(&a, "abcDEF12345", "One") == 1 && setlist_add(&a, "bbbDEF12345", "Two") == 1 && setlist_add(&a, "abcDEF12345", "One again") == 0 && a.nsl == 2, "two songs; adding one again updates it");
    CHECK(setlist_move(&a, 1, -1) == 0 && !strcmp(a.sl[0].id, "bbbDEF12345"), "move up");
    CHECK(setlist_move(&a, 0, -1) == 0, "the first cannot go up");
    setlist_load(&a);
    CHECK(a.nsl == 2 && !strcmp(a.sl[0].title, "Two") && !strcmp(a.sl[1].title, "One again"), "the Set List comes back from the card in order");
    setlist_remove(&a, 0);
    CHECK(a.nsl == 1 && !strcmp(a.sl[0].id, "abcDEF12345"), "remove");

    /* the songs opened here: Last 10 is newest first, Top 10 is most opened first */
    hist_load(&a);
    CHECK(a.nhist == 0, "nothing opened yet");
    hist_touch(&a, "aaaaaaaaaaa", "A"); hist_touch(&a, "bbbbbbbbbbb", "B"); hist_touch(&a, "ccccccccccc", "C"); hist_touch(&a, "aaaaaaaaaaa", "A"); hist_touch(&a, "aaaaaaaaaaa", "A"); hist_touch(&a, "bbbbbbbbbbb", "B");
    int r[10], n = hist_rank(&a, 0, r);
    CHECK(n == 3 && !strcmp(a.hist[r[0]].id, "bbbbbbbbbbb") && !strcmp(a.hist[r[1]].id, "aaaaaaaaaaa") && !strcmp(a.hist[r[2]].id, "ccccccccccc"), "Last: B, A, C");
    n = hist_rank(&a, 1, r);
    CHECK(n == 3 && !strcmp(a.hist[r[0]].id, "aaaaaaaaaaa") && a.hist[r[0]].count == 3 && !strcmp(a.hist[r[1]].id, "bbbbbbbbbbb") && !strcmp(a.hist[r[2]].id, "ccccccccccc"), "Top: A (3), B (2), C (1)");
    hist_load(&a);
    CHECK(a.nhist == 3 && hist_rank(&a, 1, r) == 3 && a.hist[r[0]].count == 3, "kept on the card");
    for (int i = 0; i < 14; i++) { char id[16]; snprintf(id, sizeof id, "x%010d", i); hist_touch(&a, id, "song"); }
    CHECK(hist_rank(&a, 0, r) == 10 && hist_rank(&a, 1, r) == 10, "at most ten show");
    a.pan_first = P_SETLIST;
    compose(&a, &s);
    a.pan_first = P_LAST;
    compose(&a, &s);
    a.pan_first = P_TOP;                                           /* Top, then Markers */
    compose(&a, &s);
    app_free(&a);
    unsetenv("R3I6NOS_MUSICUE_NOAUDIO");
}

/* The 2026-11-20 map: Start and L1+Start (Play and Rec mode), the D-pad in each, A-B looping on R1, the Jump panel, the Piano over the Key, the number after a marker's name,
 * the main menu and the context menu. Every press goes through pad_button, the path the main loop uses. */
static void test_pad_map(void) {
    setenv("R3I6NOS_MUSICUE_NOAUDIO", "1", 1);
    App a;
    app_init(&a, g_tmp);
    char d[PATH_MAX];
    snprintf(d, sizeof d, "%s/padmap", g_tmp);
    snprintf(a.data, sizeof a.data, "%s", d);
    mkdirs(d);
    open_demo(&a);
    R3Screen s;
    const int ncues = a.cues.nc;

    /* Play mode: Start plays, the D-pad goes to cues */
    press(&a, R3_BTN_START);
    CHECK(a.eng.playing && !a.rec, "Start plays in Play mode");
    press(&a, R3_BTN_DOWN);
    CHECK(a.cues.nc == ncues && fabs(engine_pos_sec(&a.eng) - a.cues.c[ncues - 1].start) < 0.001, "Play mode: Down goes to the last cue and adds none");
    press(&a, R3_BTN_UP);
    CHECK(engine_pos_sec(&a.eng) == a.cues.c[0].start, "Play mode: Up goes to the first cue");
    engine_seek(&a.eng, 21.0);
    press(&a, R3_BTN_LEFT);
    CHECK(fabs(engine_pos_sec(&a.eng) - 19.2) < 0.001, "Left goes to the start of the cue playing (more than 1.5 s in)");
    press(&a, R3_BTN_LEFT);
    CHECK(fabs(engine_pos_sec(&a.eng) - 9.6) < 0.001, "and again to the one before");
    press(&a, R3_BTN_RIGHT);
    CHECK(fabs(engine_pos_sec(&a.eng) - 19.2) < 0.001, "Right goes to the next cue");
    press(&a, R3_BTN_START);
    CHECK(!a.eng.playing, "Start stops");

    /* Rec mode: L1+Start plays in Rec mode; Down adds a cue, Up takes the last back; Start stops and ends it */
    engine_seek(&a.eng, 40.0);
    with(&a, R3_BTN_L1, R3_BTN_START);
    CHECK(a.eng.playing && a.rec, "L1+Start plays in Rec mode");
    compose(&a, &s);
    char top[R3_COLS + 1];
    row_text(&s, 0, top);
    CHECK(strstr(top, "REC ") != NULL, "the top bar says REC: %s", top);
    for (int i = 0; i < 10; i++) engine_tick(&a.eng, 50);                       /* half a second on the caller's clock */
    press(&a, R3_BTN_DOWN);
    CHECK(a.cues.nc == ncues + 1 && a.nrec == 1, "Rec mode: Down adds a cue");
    double t1 = a.rec_t[0];
    for (int i = 0; i < 10; i++) engine_tick(&a.eng, 50);
    press(&a, R3_BTN_DOWN);
    CHECK(a.cues.nc == ncues + 2 && a.nrec == 2 && a.rec_t[1] > t1, "a second cue");
    press(&a, R3_BTN_UP);
    CHECK(a.cues.nc == ncues + 1 && a.nrec == 1, "Rec mode: Up takes the last cue back");
    press(&a, R3_BTN_LEFT);
    CHECK(fabs(engine_pos_sec(&a.eng) - t1) < 0.001 || engine_pos_sec(&a.eng) < t1, "Rec mode: Left goes to a cue before");
    press(&a, R3_BTN_START);
    CHECK(!a.eng.playing && !a.rec, "Start stops and ends Rec mode");
    press(&a, R3_BTN_UP);
    CHECK(a.cues.nc == ncues + 1 && engine_pos_sec(&a.eng) == a.cues.c[0].start, "back in Play mode Up is the first cue and takes nothing back");
    with(&a, R3_BTN_L1, R3_BTN_START);
    with(&a, R3_BTN_L1, R3_BTN_START);
    CHECK(!a.eng.playing && !a.rec, "L1+Start while playing stops it too");

    /* A-B looping on R1 */
    engine_seek(&a.eng, 10.0);
    with(&a, R3_BTN_R1, R3_BTN_UP);
    CHECK(a.loop_state == 1 && fabs(a.loop_a - 10.0) < 0.001, "R1+Up sets A");
    with(&a, R3_BTN_R1, R3_BTN_DOWN);
    CHECK(a.loop_state == 1, "B at the same place is refused");
    engine_seek(&a.eng, 20.0);
    with(&a, R3_BTN_R1, R3_BTN_DOWN);
    CHECK(a.loop_state == 2 && fabs(a.loop_b - 20.0) < 0.001 && a.eng.loop && !a.loop_off, "R1+Down sets B and the loop runs");
    with(&a, R3_BTN_R1, R3_BTN_START);
    CHECK(a.loop_state == 2 && a.loop_off && !a.eng.loop, "R1+Start stops the loop and keeps A and B");
    with(&a, R3_BTN_R1, R3_BTN_START);
    CHECK(!a.loop_off && a.eng.loop, "and starts it again");
    CHECK(fabs(engine_pos_sec(&a.eng) - 10.0) < 0.001, "starting it from outside the span goes to A");
    engine_seek(&a.eng, 50.0);
    with(&a, R3_BTN_R1, R3_BTN_SELECT);
    CHECK(fabs(engine_pos_sec(&a.eng) - 10.0) < 0.001, "R1+Select goes to the start of the loop");
    press(&a, R3_BTN_SELECT);
    CHECK(engine_pos_sec(&a.eng) == 0, "Select alone is still 0:00");
    engine_seek(&a.eng, 25.0);
    with(&a, R3_BTN_R1, R3_BTN_UP);
    CHECK(a.loop_state == 1 && !a.eng.loop, "an A past B puts the loop back to A alone");
    with(&a, R3_BTN_R1, R3_BTN_RIGHT);
    CHECK(fabs(engine_pos_sec(&a.eng) - 35.0) < 0.001, "R1+Right is 10 s on");

    /* playing in Play mode, Fn goes to Jump from anywhere on the carousel; stopping restores the panels */
    a.jump_view = 0; a.pan_first = P_META;
    press(&a, R3_BTN_START);
    press(&a, R3_BTN_FN);
    CHECK(a.jump_view && a.auto_jump, "playing, Fn goes straight to Jump");
    press(&a, R3_BTN_START);
    app_tick(&a, 10);
    CHECK(!a.jump_view && a.pan_first == P_META && !a.auto_jump, "stopping brings the earlier panels back");
    a.pan_first = 0;

    /* the Jump panel: the stick picks a marker, stopped a button binds it, playing it jumps; the tags and the fill show */
    a.loop_state = 0;
    set_loop_engine(&a);
    for (int i = 0; i < 6; i++) press(&a, R3_BTN_FN);
    CHECK(a.jump_view, "Fn reaches the Jump panel");
    stick_dispatch(&a, R3_DOWN);
    stick_dispatch(&a, R3_DOWN);
    CHECK(a.jsel == 2, "the stick picks a marker (Pre-Chorus)");
    press(&a, R3_BTN_A);
    CHECK(a.jump_on[4] && jump_marker_of(&a, 4) == 2 && a.mode == M_PLAYER, "stopped, A binds the marker (and does not accept a panel)");
    stick_dispatch(&a, R3_END);
    press(&a, R3_BTN_UP);
    press(&a, R3_BTN_B);
    CHECK(jump_marker_of(&a, 0) == 7 && jump_marker_of(&a, 5) == 7 && a.mode == M_PLAYER, "D-pad Up and B bind the last marker too; B does not leave the song here");
    press(&a, R3_BTN_B);
    CHECK(!a.jump_on[5] && jump_marker_of(&a, 0) == 7, "the same button on the same marker frees it");
    stick_dispatch(&a, R3_HOME);
    press(&a, R3_BTN_LEFT);
    CHECK(jump_marker_of(&a, 2) == 0, "Left binds the first marker");
    compose(&a, &s);
    char row[R3_COLS + 1];
    row_text(&s, 10, row);
    CHECK(strstr(row, "[<]") != NULL, "the first marker's row carries its tag: %s", row);
    row_text(&s, 12, row);
    CHECK(strstr(row, "[A]") != NULL, "the third row carries [A]: %s", row);
    engine_seek(&a.eng, 28.0);
    engine_play(&a.eng, 1);
    compose(&a, &s);
    int fill = 0;
    for (int c = 1; c < R3_COLS - 1; c++) fill += s.bg[12 * R3_COLS + c] >= P_HUE0 && s.bg[12 * R3_COLS + c] < P_HUE0 + 32;
    CHECK(fill > 25 && fill < 36, "Pre-Chorus (24 s to 32 s) is half filled at 28 s: %d cells", fill);
    int others = 0;
    for (int r = 10; r < 19; r++) if (r != 12) for (int c = 1; c < R3_COLS - 1; c++) others += s.bg[r * R3_COLS + c] >= P_HUE0 && s.bg[r * R3_COLS + c] < P_HUE0 + 32;
    CHECK(others == 0, "only the section the playhead is in fills (Cue's rule): %d", others);
    press(&a, R3_BTN_A);
    CHECK(fabs(engine_pos_sec(&a.eng) - 24.0) < 0.01, "playing, A jumps to the bound marker");
    press(&a, R3_BTN_UP);
    CHECK(fabs(engine_pos_sec(&a.eng) - 88.0) < 0.01, "and D-pad Up to the one bound to it");
    press(&a, R3_BTN_X);
    CHECK(a.jump_on[6] == 0 && fabs(engine_pos_sec(&a.eng) - 88.0) < 0.01, "an unbound button only says so");
    engine_seek(&a.eng, 40.0);
    with(&a, R3_BTN_R1, R3_BTN_UP);
    with(&a, R3_BTN_R1, R3_BTN_DOWN);
    engine_seek(&a.eng, 50.0);
    with(&a, R3_BTN_R1, R3_BTN_DOWN);
    CHECK(a.loop_state == 2, "R1+Up and R1+Down keep setting A and B on the Jump panel");
    engine_seek(&a.eng, 45.0);
    compose(&a, &s);
    int abfill = 0;
    for (int c = 0; c < R3_COLS; c++) abfill += s.bg[19 * R3_COLS + c] >= P_ACC0 && s.bg[19 * R3_COLS + c] < P_ACC0 + RAMP;
    CHECK(abfill > 10 && abfill < 26, "the A-B bar is half full at 45 s of 40 to 50 s: %d", abfill);
    with(&a, R3_BTN_R1, R3_BTN_START);
    compose(&a, &s);
    abfill = 0;
    for (int c = 0; c < R3_COLS; c++) abfill += s.bg[19 * R3_COLS + c] >= P_ACC0 && s.bg[19 * R3_COLS + c] < P_ACC0 + RAMP;
    CHECK(abfill == 0, "it empties while the loop is off: %d", abfill);
    engine_play(&a.eng, 0);
    a.open = 0;
    a.nsongs = 1;
    a.songs = (Song *)calloc(1, sizeof *a.songs);
    snprintf(a.songs[0].id, sizeof a.songs[0].id, "jumpTEST001");
    char jp[PATH_MAX];
    sub_path(&a, "cues", NULL, jp, sizeof jp);
    mkdirs(jp);
    jump_save(&a);
    memset(a.jump_on, 0, sizeof a.jump_on);
    jump_load(&a);
    CHECK(a.jump_on[0] && a.jump_on[2] && a.jump_on[4] && jump_marker_of(&a, 4) == 2, "the bindings are kept in cues/<id>.jump and come back");
    a.open = -1;
    free(a.songs);
    a.songs = NULL;
    a.nsongs = 0;
    a.jump_view = 0;
    a.pan_first = 0;

    /* the main menu (L1+R1) and the context menu (L2+R2 or Y) */
    pad_button(&a, R3_BTN_L1, 1);
    pad_button(&a, R3_BTN_R1, 1);
    CHECK(a.overlay == OV_MM && a.mode == M_PLAYER, "L1+R1 opens the main menu over the player");
    pad_button(&a, R3_BTN_R1, 0);
    pad_button(&a, R3_BTN_L1, 0);
    char msg[160];
    CHECK(r3_mm_selftest(&a.mm, msg, sizeof msg) == 0, "%s", msg);
    int ids[R3MM_FIXED + R3MM_MAX_OWN], n = r3_mm_ids(&a.mm, ids);
    CHECK(n == 7 && ids[0] == R3MM_ABOUT && ids[1] == R3MM_THEME && ids[2] == R3MM_KEYMAP && ids[3] == R3MM_REPORT && ids[4] == R3MM_DEMO && ids[5] == R3MM_QUIT && ids[6] == R3MM_OWN0, "About, Colour theme, Keymap, Device report, Rec demo, Quit (Benchmark hidden), then the app's own");
    press(&a, R3_BTN_A);
    CHECK(a.overlay == OV_ABOUT, "About opens");
    compose(&a, &s);
    press(&a, R3_BTN_B);
    CHECK(a.overlay == OV_NONE, "any key closes it");
    dispatch(&a, R3_MENU);
    press(&a, R3_BTN_DOWN);
    press(&a, R3_BTN_A);
    CHECK(a.overlay == OV_THEME && a.theme_sel == a.theme, "Colour theme opens the picker");
    press(&a, R3_BTN_DOWN);
    int shown = a.theme_sel;
    CHECK(shown != a.theme, "moving shows another theme");
    press(&a, R3_BTN_A);
    CHECK(a.overlay == OV_NONE && a.theme == shown, "A keeps it");
    a.theme = r3_theme_find("Nord");
    apply_theme(&a);
    dispatch(&a, R3_MENU);
    for (int i = 0; i < 6; i++) press(&a, R3_BTN_DOWN);
    CHECK(a.mm.sel == 6, "the app's own item is last");
    press(&a, R3_BTN_A);
    CHECK(a.mode == M_MENU && a.menu_kind == MK_SONG && a.menu.n >= 5, "the app's own item opens the song's options");
    press(&a, R3_BTN_B);
    CHECK(a.mode == M_PLAYER, "B closes it");
    dispatch(&a, R3_MENU);
    for (int i = 0; i < 5; i++) press(&a, R3_BTN_DOWN);
    press(&a, R3_BTN_A);
    CHECK(a.quit, "Quit leaves the app");
    a.quit = 0;
    pad_button(&a, R3_BTN_L2, 1);
    pad_button(&a, R3_BTN_R2, 1);
    CHECK(a.mode == M_MENU && a.menu_kind == MK_CONTEXT, "L2+R2 opens the context menu");
    pad_button(&a, R3_BTN_R2, 0);
    pad_button(&a, R3_BTN_L2, 0);
    press(&a, R3_BTN_B);
    CHECK(a.mode == M_PLAYER, "B closes it");
    press(&a, R3_BTN_Y);
    CHECK(a.mode == M_MENU && a.menu_kind == MK_CONTEXT, "Y opens it as well");
    press(&a, R3_BTN_B);

    /* naming a marker with a number or a letter: Right on a preset */
    a.msel = 1;                                                              /* "Verse 1" in the demo */
    open_menu(&a, MK_CONTEXT);
    for (int i = 0; i < a.menu.n; i++) if (a.menu.it[i].id == A_LABEL) a.menu.sel = i;
    press(&a, R3_BTN_A);
    CHECK(a.mode == M_MENU && a.menu.it[0].id >= A_LABELPICK, "Name the selected marker lists the presets (mode %d, first item %d, %d items)", a.mode, a.menu.it[0].id, a.menu.n);
    press(&a, R3_BTN_DOWN);                                                  /* Verse */
    press(&a, R3_BTN_RIGHT);
    CHECK(a.mode == M_TEXT && a.tfield == TF_SUFFIX && !strcmp(a.suffix_base, "Verse"), "Right on Verse asks for a number or letter");
    press(&a, R3_BTN_UP);
    press(&a, R3_BTN_UP);
    CHECK(!strcmp(a.tbuf, "2"), "Up gives 1, then 2: %s", a.tbuf);
    press(&a, R3_BTN_RIGHT);
    press(&a, R3_BTN_UP);
    press(&a, R3_BTN_DOWN);
    CHECK(!strcmp(a.tbuf, "20") || !strcmp(a.tbuf, "21") || a.tlen == 2, "Right makes a second place: %s", a.tbuf);
    a.tbuf[1] = 0; a.tlen = 1; a.tpos = 1;                                   /* "2": make it "2" then a letter */
    press(&a, R3_BTN_UP);
    for (int i = 0; i < 9; i++) press(&a, R3_BTN_UP);                         /* 1 2 ... 9 A */
    CHECK(a.tlen == 2, "two places");
    a.tbuf[0] = '1'; a.tbuf[1] = 'a'; a.tbuf[2] = 0;
    press(&a, R3_BTN_A);
    CHECK(a.mode == M_PLAYER && !strcmp(a.cues.m[1].label, "Verse 1a") && a.cues.dirty, "A keeps \"Verse 1a\"");
    open_label_menu(&a);
    press(&a, R3_BTN_A);
    CHECK(!strcmp(a.cues.m[1].label, "Intro"), "A on a preset alone keeps the plain name");

    /* the Piano over the Key: C3 to B3 on the pad, the shoulders are notes, Select and Start move the octave, Fn closes */
    a.pan_first = P_META;
    a.pan_focus = 0;
    a.psel[P_META] = 0;
    press(&a, R3_BTN_A);
    CHECK(a.mode == M_TEXT && a.tfield == TF_KEY && a.piano && a.piano_oct == 3, "editing the Key shows the piano at octave 3");
    static const struct { int bt; const char *name; } want[] = {
        {R3_BTN_UP, "C"}, {R3_BTN_L1, "C#"}, {R3_BTN_LEFT, "D"}, {R3_BTN_L2, "D#"}, {R3_BTN_DOWN, "E"}, {R3_BTN_RIGHT, "F"},
        {R3_BTN_R2, "F#"}, {R3_BTN_Y, "G"}, {R3_BTN_R1, "G#"}, {R3_BTN_B, "A"}, {R3_BTN_X, "A#"}, {R3_BTN_A, "B"}};
    for (int k = 0; k < 12; k++) {
        pad_button(&a, want[k].bt, 1);
        CHECK(!strcmp(a.tbuf, want[k].name) && a.piano_now[k] && a.mode == M_TEXT && a.piano, "%s plays %s3 (key shows %s)", r3_button_name(want[k].bt), NOTE_NAME[k], a.tbuf);
        int voices = 0;
        for (int v = 0; v < VOICES; v++) voices += a.eng.voice[v].active && a.eng.voice[v].key == k;
        CHECK(voices == 1, "a voice sounds for %s", NOTE_NAME[k]);
        pad_button(&a, want[k].bt, 0);
        CHECK(!a.piano_now[k], "and lets go");
    }
    CHECK(fabs(piano_freq(9, 3) - 220.0) < 0.2 && fabs(piano_freq(0, 4) - 261.63) < 0.1, "A3 is 220 Hz and C4 is 261.63 Hz (C3 is 130.81)");
    int16_t buf[2 * 512];
    memset(buf, 0, sizeof buf);
    memset(a.eng.voice, 0, sizeof a.eng.voice);                              /* no audio callback runs here to retire the twelve notes just played */
    pad_button(&a, R3_BTN_L1, 1);
    pad_button(&a, R3_BTN_R1, 1);
    CHECK(a.overlay == OV_NONE && a.piano, "a shoulder pair plays two notes; it is not the menu chord");
    engine_mix_voices(&a.eng, buf, 512);
    int peak = 0;
    for (int i = 0; i < 1024; i++) peak = abs(buf[i]) > peak ? abs(buf[i]) : peak;
    CHECK(peak > 1000 && peak < 32000, "the voices make sound without clipping: peak %d", peak);
    pad_button(&a, R3_BTN_R1, 0);
    pad_button(&a, R3_BTN_L1, 0);
    press(&a, R3_BTN_START);
    CHECK(a.piano_oct == 4, "Start is Oct +");
    press(&a, R3_BTN_SELECT);
    press(&a, R3_BTN_SELECT);
    press(&a, R3_BTN_SELECT);
    press(&a, R3_BTN_SELECT);
    CHECK(a.piano_oct == 1, "Select is Oct -, down to 1");
    check_contrast(&a, "piano");
    snprintf(a.tbuf, sizeof a.tbuf, "Am7");
    a.tlen = a.tpos = 3;
    press(&a, R3_BTN_Y);                                                     /* G */
    CHECK(!strcmp(a.tbuf, "Gm7"), "a note names the root and keeps what follows: %s", a.tbuf);
    press(&a, R3_BTN_FN);
    CHECK(!a.piano && a.mode == M_TEXT, "Fn closes the piano");
    press(&a, R3_BTN_A);
    CHECK(a.mode == M_PLAYER && !strcmp(a.cues.meta.key, "Gm7"), "A keeps the key: %s", a.cues.meta.key);
    app_free(&a);
    unsetenv("R3I6NOS_MUSICUE_NOAUDIO");
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
        for (int pf = 0; pf < P_COUNT; pf++) {                       /* every pair of panels the carousel shows, with a Set List and songs to list */
            a.pan_first = pf;
            a.pan_focus = pf & 1;
            a.nsl = 2; a.nhist = 2;
            snprintf(a.sl[0].id, sizeof a.sl[0].id, "demoSong001"); snprintf(a.sl[0].title, sizeof a.sl[0].title, "Demo");
            snprintf(a.sl[1].id, sizeof a.sl[1].id, "zzzzzzzzzzz"); snprintf(a.sl[1].title, sizeof a.sl[1].title, "Not here");
            snprintf(a.hist[0].id, sizeof a.hist[0].id, "demoSong001"); snprintf(a.hist[0].title, sizeof a.hist[0].title, "Demo"); a.hist[0].count = 3; a.hist[0].last = 2;
            snprintf(a.hist[1].id, sizeof a.hist[1].id, "zzzzzzzzzzz"); snprintf(a.hist[1].title, sizeof a.hist[1].title, "Not here"); a.hist[1].count = 1; a.hist[1].last = 1;
            check_contrast(&a, "panels");
            a.nsl = 0; a.nhist = 0;
            check_contrast(&a, "empty panels");
        }
        a.pan_first = 0; a.pan_focus = 0;
        a.loop_state = 2; a.loop_a = 40; a.loop_b = 50;
        a.msel = 3;
        for (int mk = MK_SONG; mk <= MK_CONTEXT; mk++) { a.mode = M_PLAYER; open_menu(&a, mk); check_contrast(&a, "menu"); }
        a.mode = M_PLAYER;
        a.jump_view = 1;                                                      /* the Jump panel: a section filling, tags, the A-B bar, then empty */
        a.jump_on[0] = 1; a.jump_t[0] = a.cues.m[1].time; a.jump_on[4] = 1; a.jump_t[4] = a.cues.m[3].time; a.jump_on[7] = 1; a.jump_t[7] = a.cues.m[3].time;
        a.jsel = 3;
        engine_seek(&a.eng, 45.0);
        check_contrast(&a, "jump");
        a.loop_off = 1;
        check_contrast(&a, "jump, loop off");
        a.loop_off = 0; a.loop_state = 1;
        check_contrast(&a, "jump, A only");
        a.loop_state = 0;
        check_contrast(&a, "jump, no loop");
        a.cues.nm = 0;
        check_contrast(&a, "jump, no markers");
        demo_cues(&a.cues);
        a.jump_view = 0;
        a.rec = 1;
        engine_play(&a.eng, 1);
        check_contrast(&a, "rec");
        a.rec = 0;
        engine_play(&a.eng, 0);
        a.mode = M_TEXT; a.tfield = TF_KEY; a.talpha = "ABCDEFG#bm7 ?"; a.tmax = 40; snprintf(a.tbuf, sizeof a.tbuf, "Am"); a.tlen = a.tpos = 2;
        a.piano = 1; a.piano_oct = 3; a.piano_now[1] = a.piano_now[4] = 1;
        check_contrast(&a, "piano");
        a.piano = 0; a.tfield = TF_SUFFIX; snprintf(a.suffix_base, sizeof a.suffix_base, "Chorus");
        check_contrast(&a, "marker number");
        a.mode = M_PLAYER;
        for (int ov = OV_MM; ov <= OV_THEME; ov++) { a.overlay = ov; if (ov == OV_MM) mm_open(&a); a.theme_sel = a.theme; check_contrast(&a, "overlay"); }
        a.overlay = OV_NONE;
        a.mm.open = 0;
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
    press(&a, R3_BTN_START);
    CHECK(a.eng.playing, "Start plays");
    press(&a, R3_BTN_START);
    CHECK(!a.eng.playing, "and stops");
    press(&a, R3_BTN_START);
    for (int i = 0; i < 20; i++) app_tick(&a, 25);
    mark_here(&a);
    CHECK(a.cues.nm == 1 && fabs(a.cues.m[0].time - 0.5) < 0.05, "Mark here marks the playhead: %.3f", a.cues.nm ? a.cues.m[0].time : -1);
    save_cues(&a);
    char cj[PATH_MAX];
    cue_path(&a, "abcDEF12345", "json", cj, sizeof cj);
    CHECK(access(cj, R_OK) == 0, "the cues were saved to cues/<id>.json");
    a.offset = 0.1;
    save_offset(&a);
    press(&a, R3_BTN_B);
    CHECK(a.mode == M_LIBRARY && a.open < 0, "B goes back to the songs");
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_PLAYER && a.cues.nm == 1 && fabs(a.offset - 0.1) < 0.001, "the cues and the offset come back: %d markers, offset %.2f", a.cues.nm, a.offset);
    a.eng.pos = 0;
    a.jump_view = 1;                                                       /* a button on the Jump panel jumps to a marker, in the audio's time (offset applied) */
    a.jump_on[0] = 1;
    a.jump_t[0] = a.cues.m[0].time;
    engine_play(&a.eng, 1);
    press(&a, R3_BTN_UP);
    CHECK(fabs(engine_pos_sec(&a.eng) - (a.cues.m[0].time + 0.1)) < 0.01, "a Jump button goes to the marker, offset applied");
    engine_play(&a.eng, 0);
    a.jump_view = 0;
    press(&a, R3_BTN_Y);
    CHECK(a.mode == M_MENU && a.menu.n >= 3, "Y opens the context menu");
    press(&a, R3_BTN_B);
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


/* ---- Rec demo (musicue_demo.h): the tour on a stepped clock, with the real mixer and a fake render helper ------------------------------------------------ */

static int same_file(const char *x, const char *y) {
    FILE *fx = fopen(x, "rb"), *fy = fopen(y, "rb");
    int same = fx && fy;
    while (same) {
        unsigned char bx[4096], by[4096];
        size_t nx = fread(bx, 1, sizeof bx, fx), ny = fread(by, 1, sizeof by, fy);
        if (nx != ny || memcmp(bx, by, nx)) same = 0;
        if (nx < sizeof bx) break;
    }
    if (fx) fclose(fx);
    if (fy) fclose(fy);
    return same;
}

static int count_ext(const char *dir, const char *ext) {
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0;
    size_t le = strlen(ext);
    while (d && (e = readdir(d))) { size_t l = strlen(e->d_name); if (l > le && !strcmp(e->d_name + l - le, ext)) n++; }
    if (d) closedir(d);
    return n;
}

static void test_rec_demo(void) {
    char run[PATH_MAX], d1[PATH_MAX], d2[PATH_MAX], d3[PATH_MAX], fake[PATH_MAX], p1[PATH_MAX], p2[PATH_MAX], fail[160];
    snprintf(run, sizeof run, "%s/rdrun", g_tmp);
    snprintf(d1, sizeof d1, "%s/rd1/Demo", g_tmp);
    snprintf(d2, sizeof d2, "%s/rd2/Demo", g_tmp);
    snprintf(d3, sizeof d3, "%s/rd3/Demo", g_tmp);
    snprintf(fake, sizeof fake, "%s/rdfake", g_tmp);
    setenv("R3I6NOS_RUN", run, 1);
    setenv("R3I6NOS_DEMO_KT_WAV", "1", 1);
    setenv("R3I6NOS_DEMO_STAMP", "20261120-120000", 1);
    unsetenv("R3I6NOS_MUSICUE_BATTERY"); unsetenv("R3I6NOS_MUSICUE_FREE_MB");
    write_script(fake, "#!/bin/sh\nout=; in=\nwhile [ $# -gt 0 ]; do case \"$1\" in --in) in=\"$2\";; --out) out=\"$2\";; esac; shift; done\n"
                       "echo \"PROGRESS 0/2 Rendering...\"\ncp \"$in\" \"$out.part\"\necho \"PROGRESS 1/2 Segment 1 of 2 joined\"\necho \"PROGRESS 2/2 Segment 2 of 2 joined\"\nmv \"$out.part\" \"$out\"\necho \"DONE WAV, 16.2 MiB, 4233600 frames in 0.1 s, peak memory 9 MiB\"\n");
    setenv("R3I6NOS_MUSICUE_RENDER", fake, 1);
    const unsigned all = (1u << DF_COUNT) - 1;

    int rc1 = musicue_demo(NULL, NULL, d1, all, 1, 1, fail, sizeof fail);
    CHECK(rc1 == 0, "the tour records (rc %d)", rc1);
    snprintf(p1, sizeof p1, "%s/rec-demo.txt", d1);
    size_t n = 0;
    char *log = cue_read_file(p1, &n);
    CHECK(log != NULL, "rec-demo.txt is written");
    if (log) {
        CHECK(!strstr(log, "\nFAIL "), "the log has no FAIL line:%s", strstr(log, "\nFAIL ") ? strstr(log, "\nFAIL ") : "");
        char *last = log + n;
        while (last > log && (last[-1] == '\n' || last[-1] == ' ')) last--;
        CHECK(last - log >= 3 && !strncmp(last - 3, "end", 3), "the log ends with `end`");
        for (int f = 0; f < DF_COUNT; f++) {
            char tag[32];
            snprintf(tag, sizeof tag, "[%s]", DF_NAME[f]);
            CHECK(strstr(log, tag) != NULL, "the tour shows the hero feature %s", DF_NAME[f]);
        }
        CHECK(strstr(log, "build: v" VERSION " ") != NULL, "the log has the build line");
        CHECK(strstr(log, "PASS length") != NULL, "the video's length is between 30 and 90 s:%s", strstr(log, "length") ? strstr(log, "length") : " no length line");
        free(log);
    }
    CHECK(count_ext(d1, ".png") >= 16, "screenshots: %d", count_ext(d1, ".png"));
    {   /* the first frame is the splash, with the version line */
        snprintf(p1, sizeof p1, "%s/demo-20261120-120000-frames.r3f", d1);
        FILE *f = fopen(p1, "rb");
        R3Screen *first = (R3Screen *)malloc(sizeof *first);
        uint32_t runlen = 0;
        CHECK(f && first && fread(&runlen, sizeof runlen, 1, f) == 1 && fread(first, sizeof *first, 1, f) == 1, "the frames file has a first screen");
        if (f && first) {
            CHECK(screen_has(first, "v" VERSION " "), "the video opens on the splash with its version line");
            CHECK(screen_has(first, "Markers and cues on your songs"), "the splash has the tagline");
        }
        if (f) fclose(f);
        free(first);
    }
    snprintf(p1, sizeof p1, "%s/demo-20261120-120000-aux.bin", d1);
    struct stat sb;
    CHECK(stat(p1, &sb) == 0 && sb.st_size > 5000, "the pixel layer is kept beside the frames");
    snprintf(p1, sizeof p1, "%s/demo-file.json", d1);
    CHECK(stat(p1, &sb) == 0 && sb.st_size > 100, "the demo song's cue file is copied");

    /* the same tour again: every byte of the frames, the pixel layer, the sound and every picture is the same */
    int rc2 = musicue_demo(NULL, NULL, d2, all, 1, 1, fail, sizeof fail);
    CHECK(rc2 == 0, "the second recording");
    const char *same[] = {"demo-20261120-120000-frames.r3f", "demo-20261120-120000-aux.bin", "demo-20261120-120000.wav"};
    for (size_t i = 0; i < sizeof same / sizeof *same; i++) {
        snprintf(p1, sizeof p1, "%s/%s", d1, same[i]);
        snprintf(p2, sizeof p2, "%s/%s", d2, same[i]);
        CHECK(same_file(p1, p2), "two recordings make the same %s", same[i]);
    }
    {
        DIR *d = opendir(d1);
        struct dirent *e;
        int pngs = 0;
        while (d && (e = readdir(d))) {
            size_t l = strlen(e->d_name);
            if (l < 5 || strcmp(e->d_name + l - 4, ".png")) continue;
            snprintf(p1, sizeof p1, "%s/%s", d1, e->d_name);
            snprintf(p2, sizeof p2, "%s/%s", d2, e->d_name);
            pngs++;
            CHECK(same_file(p1, p2), "two recordings make the same %s", e->d_name);
        }
        if (d) closedir(d);
        CHECK(pngs >= 16, "pictures compared: %d", pngs);
    }

    /* one hero feature alone: the others are said to be left out, and its steps work from their own start */
    int rc3 = musicue_demo(NULL, NULL, d3, 1u << DF_LOOPS, 1, 1, fail, sizeof fail);
    CHECK(rc3 == 0, "a feature recorded alone");
    snprintf(p1, sizeof p1, "%s/rec-demo.txt", d3);
    log = cue_read_file(p1, &n);
    if (log) {
        CHECK(strstr(log, "SKIP markers: not chosen") != NULL && strstr(log, "[loops]") != NULL && !strstr(log, "[markers]") && !strstr(log, "[keytempo]"), "only the ticked feature is shown");
        free(log);
    }

    /* the lock: a live holder refuses a second recording */
    {
        char lk[PATH_MAX];
        snprintf(lk, sizeof lk, "%s/demo", run);
        mkdirs(lk);
        snprintf(lk, sizeof lk, "%s/demo/lock", run);
        FILE *f = fopen(lk, "w");
        if (f) { fprintf(f, "1\n"); fclose(f); }
        CHECK(musicue_demo(NULL, NULL, d3, all, 1, 1, fail, sizeof fail) == 75, "a second recording is refused while another holds the lock");
        unlink(lk);
    }

    /* the pixel layer paints what the app's own presenter paints */
    {
        setenv("R3I6NOS_MUSICUE_NOAUDIO", "1", 1);
        App a;
        app_init(&a, g_tmp);
        open_demo(&a);
        engine_seek(&a.eng, 31.3);
        loop_set_a(&a);
        engine_seek(&a.eng, 40.1);
        loop_set_b(&a);
        engine_seek(&a.eng, 44.7);
        R3Screen s;
        compose(&a, &s);
        uint32_t *px1 = (uint32_t *)malloc((size_t)R3_W * R3_H * 4), *px2 = (uint32_t *)malloc((size_t)R3_W * R3_H * 4);
        DemoHead *h = (DemoHead *)calloc(1, sizeof *h);
        DemoAux x;
        if (px1 && px2 && h) {
            paint(&a, px1, &s);
            h->pk[0] = a.peaks; h->dur[0] = dur_of(&a);
            demo_aux_of(&a, &x, 0);
            r3_render(&s, px2);
            demo_paint(NULL, px2, h, &x);
            CHECK(!memcmp(px1, px2, (size_t)R3_W * R3_H * 4), "the recorder's pixel layer is the pixels the presenter paints");
        }
        free(px1); free(px2); free(h);
        close_song(&a);
        free(a.songs); free(a.sl); free(a.hist);
        unsetenv("R3I6NOS_MUSICUE_NOAUDIO");
    }
    unsetenv("R3I6NOS_RUN"); unsetenv("R3I6NOS_DEMO_KT_WAV"); unsetenv("R3I6NOS_DEMO_STAMP"); unsetenv("R3I6NOS_MUSICUE_RENDER");
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
    test_kt();
    if (trace) fprintf(stderr, "[rec demo]\n");
    test_rec_demo();
    test_sound_sleep();
    test_report();
    test_marquee();
    test_cue_panel();
    test_panels_and_tempo();
    test_pad_map();
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
    static const char *const sample[] = {"Blue Hour", "Slow Dance in C", "Night Bus Jam", "Practice take 4", "Waltz for Mina", "Old Riff"};
    for (int i = 0; i < 6; i++) {                                    /* made-up songs for the Set List, Last 10 and Top 10 panels of a screenshot */
        snprintf(a.sl[i].id, sizeof a.sl[i].id, "song%07d", i);
        snprintf(a.sl[i].title, sizeof a.sl[i].title, "%s", sample[i]);
        snprintf(a.hist[i].id, sizeof a.hist[i].id, "song%07d", i);
        snprintf(a.hist[i].title, sizeof a.hist[i].title, "%s", sample[i]);
        a.hist[i].count = 9 - i * 2 + (i % 2);
        a.hist[i].last = 100 - i * 3;
    }
    a.nsl = 4; a.nhist = 6;
    if (!strncmp(screen, "player:", 7)) { a.pan_first = atoi(screen + 7) % P_COUNT; a.pan_focus = 1; a.csel = 2; screen = "player"; }
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
        open_menu(&a, MK_SONG);
    } else if (!strcmp(screen, "context")) {
        open_menu(&a, MK_CONTEXT);
    } else if (!strcmp(screen, "mainmenu")) {
        mm_open(&a);
    } else if (!strcmp(screen, "about")) {
        a.overlay = OV_ABOUT;
    } else if (!strcmp(screen, "jump")) {
        a.jump_view = 1;
        a.jsel = 3;
        a.jump_on[0] = 1; a.jump_t[0] = 8.0; a.jump_on[4] = 1; a.jump_t[4] = 32.0; a.jump_on[7] = 1; a.jump_t[7] = 32.0; a.jump_on[2] = 1; a.jump_t[2] = 88.0;
        engine_seek(&a.eng, 38.0);
        a.eng.playing = 1;
    } else if (!strcmp(screen, "rec")) {
        a.rec = 1;
        a.eng.playing = 1;
        a.loop_state = 0;
    } else if (!strcmp(screen, "piano")) {
        a.mode = M_TEXT; a.tfield = TF_KEY; a.talpha = "ABCDEFG#bm7 ?"; a.tmax = 40; snprintf(a.tbuf, sizeof a.tbuf, "Am"); a.tlen = a.tpos = 2;
        a.piano = 1; a.piano_oct = 3; a.piano_now[4] = 1; a.piano_now[1] = 1;
    } else if (!strcmp(screen, "kt")) {                       /* Key and Tempo, on the demo song with a pretend entry in the library */
        a.songs = (Song *)calloc(1, sizeof *a.songs); a.nsongs = 1; a.open = 0;
        snprintf(a.songs[0].name, sizeof a.songs[0].name, "Slow Burn (live) [dQw4w9WgXcQ]"); snprintf(a.songs[0].id, sizeof a.songs[0].id, "dQw4w9WgXcQ");
        kt_plan(&a.kt, "G", "96");
        a.kt.target.root = 9; a.kt.target_tempo = 104; kt_update(&a.kt);
        a.kt_workers = 4; a.mode = M_KT;
        say(&a, R3_ST_HINT, "Left/Right the Key, Up/Down the Tempo, A makes the copy.");
    } else if (!strcmp(screen, "suffix")) {
        a.mode = M_TEXT; a.tfield = TF_SUFFIX; snprintf(a.suffix_base, sizeof a.suffix_base, "Chorus"); a.talpha = SUFFIX_ALPHA; a.tmax = 8; snprintf(a.tbuf, sizeof a.tbuf, "1a"); a.tlen = 2; a.tpos = 2;
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

/* A keyboard standing in for the pad: arrows are the D-pad, Enter or Z is A, Esc is B, X, Y, Space is Start, Tab is Fn, Left Shift is R1 and Left Ctrl is L1 (held),
 * so Ctrl+Space is Rec mode and Shift+Up sets A. -1 for a key with no button. */
static int key_button(SDL_Keycode k) {
    switch (k) {
    case SDLK_UP: return R3_BTN_UP;
    case SDLK_DOWN: return R3_BTN_DOWN;
    case SDLK_LEFT: return R3_BTN_LEFT;
    case SDLK_RIGHT: return R3_BTN_RIGHT;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_z: return R3_BTN_A;
    case SDLK_ESCAPE: return R3_BTN_B;
    case SDLK_x: return R3_BTN_X;
    case SDLK_y: return R3_BTN_Y;
    case SDLK_SPACE: return R3_BTN_START;
    case SDLK_BACKSPACE: return R3_BTN_SELECT;
    case SDLK_TAB: return R3_BTN_FN;
    case SDLK_LSHIFT: return R3_BTN_R1;
    case SDLK_LCTRL: return R3_BTN_L1;
    default: return -1;
    }
}

static void open_pads(void) { for (int i = 0; i < SDL_NumJoysticks(); i++) SDL_JoystickOpen(i); }


int main(int argc, char **argv) {
    int selftest_run = 0, theme = -1, report = 0, rec_demo = 0, no_video = 0;
    const char *features = "all", *encode_dir = NULL;
    const char *shot = NULL, *screen = "player", *demo_dir = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) selftest_run = 1;
        else if (!strcmp(argv[i], "--report")) report = 1;
        else if (!strcmp(argv[i], "--rec-demo")) rec_demo = 1;
        else if (!strcmp(argv[i], "--no-video")) no_video = 1;
        else if (!strcmp(argv[i], "--features") && i + 1 < argc) features = argv[++i];
        else if (!strcmp(argv[i], "--encode-demo") && i + 1 < argc) encode_dir = argv[++i];
        else if (!strcmp(argv[i], "--version")) { puts("musicue " VERSION); return 0; }
        else if (!strcmp(argv[i], "--sync-path")) { char hb[PATH_MAX + 40]; puts(helper_path(hb, sizeof hb)); return 0; }
        else if (!strcmp(argv[i], "--render-path")) { char hb[PATH_MAX + 40]; puts(render_helper_path(hb, sizeof hb)); return 0; }
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
            fprintf(stderr, "usage: %s [--selftest | --report | --rec-demo [--features LIST] [--no-video] | --encode-demo DIR | --version | --sync-path | --render-path | --make-demo DIR | [--theme NAME] [--screen library|player|menu|context|mainmenu|about|jump|rec|piano|suffix|kt] --screenshot out.bmp]\n", argv[0]);
            return 2;
        }
    }
    if (demo_dir) return make_demo(demo_dir);
    unsigned demo_mask = (1u << DF_COUNT) - 1;
    if (rec_demo) {
        R3Checklist ck;
        r3_checklist_init(&ck, DF_COUNT, DF_NAME, DF_TITLE, DF_BLURB);
        demo_mask = r3_checklist_parse(&ck, features);
        if (!demo_mask) {
            fprintf(stderr, "musicue: --features takes all, or some of:");
            for (int f = 0; f < DF_COUNT; f++) fprintf(stderr, "%s %s", f ? "," : "", DF_NAME[f]);
            fputc('\n', stderr);
            return 2;
        }
    }
    if (encode_dir) {                                           /* the video from the frames and the sound a run kept, on a computer that has ffmpeg */
        char msg[400];
        int rc = r3_demo_encode_px(encode_dir, msg, sizeof msg, NULL, NULL, demo_paint, NULL);
        printf("%s: %s\n", rc == 0 ? "OK" : "FAIL", msg);
        return rc == 0 ? 0 : 1;
    }
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
    r3_splash(&splash, "MusiCue", VERSION, BUILD_LINE, "Markers and cues on your songs", "Looking for your data", 50);
    for (int i = 0; i < R3_VIDEO_SETTLE; i++) r3_video_draw(&video, r, &splash, i == 0);
    open_pads();
    R3AppData data;
    memset(&data, 0, sizeof data);
    R3AppDataUI adui = {r, &video, &toast, NULL, NULL, NULL};
    if (report || rec_demo) r3_appdata_quiet(&data, "MusiCue");           /* an unattended run never asks and never moves anything (rule 16) */
    else {
        r3_appdata_init(&data, "MusiCue", &adui);
        if (data.quit) { r3_video_free(&video); SDL_DestroyRenderer(r); SDL_DestroyWindow(win); SDL_Quit(); return 0; }
    }
    snprintf(app.data, sizeof app.data, "%s", data.use);
    char sub[PATH_MAX];
    const char *dirs[] = {"media", "cues", "inbox", "export"};
    for (int i = 0; i < 4; i++) { sub_path(&app, dirs[i], NULL, sub, sizeof sub); mkdirs(sub); }
    load_settings(&app);
    setlist_load(&app);
    hist_load(&app);
    if (theme >= 0) app.theme = theme;
    apply_theme(&app);
    scan_songs(&app);
    if (data.notice[0]) say(&app, R3_ST_WARN, "%s", data.notice);
    else say(&app, R3_ST_HINT, "%d song%s. A opens one, L1+R1 is the menu.", app.nsongs, app.nsongs == 1 ? "" : "s");

    if (report) {                                              /* musicue --report: the real window, the report, and out */
        int rc = musicue_report(&app, r, &video, &toast, 0);
        app_free(&app);
        r3_video_free(&video);
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return rc;
    }
    if (rec_demo) {                                            /* musicue --rec-demo: the real window, the tour, and out */
        char dd[PATH_MAX + 8], fail[160];
        snprintf(dd, sizeof dd, "%s/Demo", app.data);
        int rc = musicue_demo(r, &video, dd, demo_mask, no_video, 0, fail, sizeof fail);
        if (fail[0]) fprintf(stderr, "musicue: the recording was stopped: %s\n", fail);
        app_free(&app);
        r3_video_free(&video);
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return rc;
    }
    R3Screen screen_cells;
    int force = 1;
    Uint32 last = SDL_GetTicks();
    while (!app.quit) {
        SDL_Event e;
        int wait_ms, wait_step;
        loop_pacing(&app, force, video.settle, r3_toast_active(&toast), &wait_ms, &wait_step);
        int have = wait_event(&e, wait_ms, wait_step);
        while (have) {
            switch (e.type) {
            case SDL_QUIT: app.quit = 1; break;
            case SDL_WINDOWEVENT: force = 1; break;
            case SDL_JOYDEVICEADDED: SDL_JoystickOpen(e.jdevice.which); break;
            case SDL_JOYAXISMOTION: r3_input_axis(&app.in, e.jaxis.axis, e.jaxis.value / 32768.0f); break;      /* axes 2 and 3 are the right stick (docs/hardware-findings.md) */
            case SDL_JOYBUTTONUP: pad_button(&app, e.jbutton.button, 0); break;
            case SDL_JOYBUTTONDOWN:
                if (r3_toast_key(&toast)) break;
                pad_button(&app, e.jbutton.button, 1);
                break;
            case SDL_KEYUP: {
                int bt = key_button(e.key.keysym.sym);
                if (bt == R3_BTN_L1 || bt == R3_BTN_R1 || (app.piano && bt >= 0)) pad_button(&app, bt, 0);
                break;
            }
            case SDL_KEYDOWN: {
                if (e.key.repeat) break;
                if (r3_toast_key(&toast)) break;
                SDL_Keycode k = e.key.keysym.sym;
                if (app.mode == M_ID && id_key(&app, k, e.key.keysym.mod)) break;      /* a keyboard types the id */
                if (app.mode == M_TEXT && !app.piano && text_key(&app, k, e.key.keysym.mod)) break;  /* ... and a metadata field */
                if (k == SDLK_q && app.mode != M_TEXT && app.mode != M_ID) { app.quit = 1; break; }
                if (k == SDLK_m && app.mode == M_PLAYER && !app.overlay) { mark_here(&app); break; }
                if (app.mode == M_PLAYER && !app.overlay && (k == SDLK_i || k == SDLK_k || k == SDLK_j || k == SDLK_l)) {       /* a keyboard: I K J L are the right stick */
                    stick_action(&app, k == SDLK_i ? R3_UP : k == SDLK_k ? R3_DOWN : k == SDLK_j ? R3_LEFT : R3_RIGHT);
                    break;
                }
                int bt = key_button(k);
                if (k == SDLK_F10 || k == SDLK_BACKQUOTE) dispatch(&app, R3_MENU);
                else if (k == SDLK_F9 || k == SDLK_BACKSLASH) dispatch(&app, R3_CONTEXT);
                else if (bt >= 0) pad_button(&app, bt, 1);
                else dispatch(&app, r3_input_key(k));
                break;
            }
            default: break;
            }
            have = SDL_PollEvent(&e);
        }
        Uint32 now = SDL_GetTicks();
        app_tick(&app, (double)(now - last));
        stick_tick(&app, now);
        last = now;
        if (app.pending_report) {                              /* the Device report: show that it is working, then run it (no input is read meanwhile) */
            app.pending_report = 0;
            compose(&app, &screen_cells);
            present(&app, &video, r, &screen_cells, 1);
            int rc = musicue_report(&app, r, &video, &toast, 1);
            if (rc == 0) say(&app, R3_ST_OK, "Report written: %s/r36s-report.txt", app.data);
            else say(&app, R3_ST_ERROR, "The report could not be written in %s", app.data);
            last = SDL_GetTicks();
            force = 1;
        }
        if (app.pending_tour) {                                /* Rec demo: the unit is the tour's until it ends (rule 11) */
            app.pending_tour = 0;
            engine_play(&app.eng, 0);
            engine_sleep(&app.eng);
            char dd[PATH_MAX + 8], fail[160];
            snprintf(dd, sizeof dd, "%s/Demo", app.data);
            int rc = musicue_demo(r, &video, dd, app.tour_mask, 0, 1, fail, sizeof fail);
            apply_theme(&app);
            if (fail[0]) say(&app, R3_ST_ERROR, "Rec demo stopped: %s. The files so far are in %s.", fail, dd);
            else if (rc == 75) say(&app, R3_ST_WARN, "Another Rec demo is recording.");
            else if (rc == 0) say(&app, R3_ST_OK, "Rec demo done: %s/", dd);
            else say(&app, R3_ST_ERROR, "Rec demo did not finish: see %s/rec-demo.txt", dd);
            last = SDL_GetTicks();
            force = 1;
        }
        if (app.force) { force = 1; app.force = 0; }
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

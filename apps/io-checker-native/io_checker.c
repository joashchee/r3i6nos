/* IO Checker, native ARM64 build (C and SDL2), on the r3i6nos-ansiapps theme (theme/r3i6nos_ansiapps.h). docs/IO-CHECKER.md is the spec.
 *
 * The same app as the LÖVE build (apps/io-checker-love/), with the same pages, words and report, so the two can be compared on a unit:
 * what goes in (every raw button, both sticks, the D-pad, the power and volume keys: presses, chatter, axis ranges, an event log) and
 * what comes out (the screen with full-screen test patterns and a measured frame rate, sound with test tones you mark heard or silent,
 * files on both cards), the system, and a device report (docs/DEVICE-REPORTS.md) at EASYROMS/IO_Checker/r36s-report-native.txt.
 * Button numbers on screen count from 1, as LÖVE does (SDL's own index is one less).
 *
 *   io-checker-native                         run it
 *   io-checker-native --report                write the device report without a window (over SSH; nothing is played or pressed)
 *   io-checker-native --selftest              check the logic and the screens, draw one frame, print one R3I6NOS_SMOKE line (CI, qemu)
 *   io-checker-native --screenshot out.bmp [page]   draw a page (home, input, video, sound, files, system) to a file
 *
 * Overrides, for tests: R3I6NOS_ROMS (/roms), R3I6NOS_ROMS2 (/roms2), R3I6NOS_SOUND (r3i6nos-sound), R3I6NOS_IO_MB (8).
 * Build: scripts/build_native_apps.sh (cross-compiles for arm64 and packs the .deb). */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "r3i6nos_ansiapps.h"
#include "r3i6nos_report.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define VERSION "2.0"
#define EXIT_HOLD_MS 1000
#define IDLE_LIMIT_MS 15000
#define IDLE_WARN_MS 10000
#define CHATTER_MS 30
#define ZONE 0.5
#define MAX_B 33        /* raw buttons, LÖVE numbering (1-based) */
#define MAX_A 8
#define LOG_LINES 6
#define MAX_KEYS 16
#define MAX_DTS 600
#define WARMUP 10
#define RATE 44100
#define FADE 0.02

/* The R36S's raw buttons in LÖVE's numbering (1-based), measured on a real unit (docs/hardware-findings.md). */
enum { L_B = 1, L_A, L_X, L_Y, L_L1, L_R1, L_L2, L_R2, L_UP, L_DOWN, L_LEFT, L_RIGHT, L_SELECT, L_START, L_L3, L_R3, L_FN };
static const char *NAMES[18] = {"", "B", "A", "X", "Y", "L1", "R1", "L2", "R2", "Up", "Down", "Left", "Right", "Select", "Start", "L3", "R3", "Fn"};

enum { P_HOME, P_INPUT, P_VIDEO, P_PATTERN, P_SOUND, P_FILES, P_SYSTEM, P_COUNT };
enum { H_INPUT, H_VIDEO, H_SOUND, H_FILES, H_SYSTEM, H_REPORT, H_QUIT, H_COUNT };
static const char *HOME_LABEL[H_COUNT] = {"Buttons and sticks", "Video", "Sound", "Files", "System", "Device report", "Quit"};
static const char *HOME_ABOUT[H_COUNT][4] = {
    {"Every button, both sticks, the D-pad, and the power and", "volume keys, as the pad reports them. Press each one:",
     "the list of buttons never pressed shrinks to nothing.", "Hold SELECT+START for a second to come back."},
    {"Full-screen test patterns: colour bars, solid colours for", "stuck pixels, ramps for colour depth, a pixel grid for",
     "scaling and edges, and a moving bar that measures the", "frame rate."},
    {"Test tones on the left, the right and both channels, a", "sweep and a quiet tone. After each, press X if you heard",
     "it or Y if not: the answers go in the device report.", "Shows the speaker path the system set."},
    {"On each card: makes its IO_Checker folder, writes,", "renames over, reads back and deletes a file, then times",
     "writing and reading 8 MB and shows the free space.", "Nothing else on the card is touched."},
    {"The r3i6nos build, kernel, board, CPU, memory, battery,", "backlight, the pad's name and GUID, and the video and", "sound drivers.", NULL},
    {"Runs the checks again and writes everything, with your", "heard / silent answers and the buttons you pressed, to",
     "EASYROMS/IO_Checker/ on the games card, for Claude.", NULL},
    {"Back to the r3i6nos menu.", NULL, NULL, NULL},
};

typedef struct { const char *id, *label, *look; } Pattern;
static const Pattern PATTERNS[] = {
    {"bars", "Colour bars", "8 bars, then the 16 VGA colours: none missing, none the same"},
    {"white", "White", "stuck dark pixels, an even backlight"},
    {"red", "Red", "stuck pixels; no green or blue tint"},
    {"green", "Green", "stuck pixels; no red or blue tint"},
    {"blue", "Blue", "stuck pixels; no red or green tint"},
    {"black", "Black", "stuck bright pixels, backlight bleed"},
    {"ramps", "Grey and colour ramps", "smooth ramps; steps mean fewer colour bits"},
    {"grid", "Pixel grid and edges", "an even grey checker (1:1 pixels), all four edge lines"},
    {"motion", "Motion and frame rate", "a smooth bar; the rate is measured"},
};
#define NPAT ((int)(sizeof PATTERNS / sizeof PATTERNS[0]))
#define PAT_MOTION (NPAT - 1)

typedef struct { const char *id, *label; double freq, from, to, left, right, amp, secs; } Tone;
static const Tone TONES[] = {
    {"left", "Left channel, 440 Hz", 440, 0, 0, 1, 0, 0.5, 1.5},
    {"right", "Right channel, 440 Hz", 440, 0, 0, 0, 1, 0.5, 1.5},
    {"both", "Both channels, 440 Hz", 440, 0, 0, 1, 1, 0.5, 1.5},
    {"sweep", "Sweep, 100 Hz to 8 kHz", 0, 100, 8000, 1, 1, 0.5, 4},
    {"quiet", "Quiet, 440 Hz at 10%", 440, 0, 0, 1, 1, 0.1, 1.5},
};
#define NTONE ((int)(sizeof TONES / sizeof TONES[0]))
enum { A_NONE, A_PLAYED, A_HEARD, A_SILENT };

typedef struct { char status[5], card[16], item[48], value[96]; } Row;

typedef struct {
    /* the pad */
    SDL_Joystick *js;
    SDL_GameController *gc;
    char pad_name[64], guid[40];
    int nbuttons, naxes, nhats;
    Uint8 down[MAX_B];
    float axis[MAX_A];
    Uint8 hat;
    /* the input record */
    int presses[MAX_B], chatter[MAX_B], has_up[MAX_B];
    Uint32 last_up[MAX_B];
    float amin[MAX_A], amax[MAX_A];
    int amoved[MAX_A], zone[MAX_A];
    unsigned hats_seen; /* bit v for hat value v (1..15) */
    char key_name[MAX_KEYS][24];
    int key_count[MAX_KEYS], nkeys;
    int k_power, k_vdown, k_vup; /* held now */
    char log[LOG_LINES][48];
    unsigned seq;
    /* pages */
    int page, sel[P_COUNT];
    char reply[200];
    int reply_style;
    Uint32 chord_since, idle_since;
    int latch, quit, pending, force;
    /* video */
    double dts[MAX_DTS];
    int ndts, skipped, motion_x, motion_n;
    int pattern_seen[NPAT];
    char mode[64], renderer[96], backlight[64], vdriver[32];
    int win_w, win_h;
    SDL_Texture *checker;
    /* sound */
    SDL_AudioDeviceID dev;
    SDL_AudioSpec have;
    char audio_dev[128];
    int audio_error, answers[NTONE];
    char snd_card[96], snd_path[16], snd_jack[64], snd_hp[16];
    /* files, system, report */
    Row files[24];
    int nfiles, files_run;
    R3Item sys[28];
    int nsys;
    char report_file[96];
    int report_written;
} App;

static const char *env_or(const char *name, const char *dflt) {
    const char *v = getenv(name);
    return v && *v ? v : dflt;
}

static void say(App *a, int style, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->reply, sizeof a->reply, fmt, ap);
    va_end(ap);
    a->reply_style = style;
}

static const char *fmt2(char *buf, double v) {
    snprintf(buf, 8, "%+.2f", v);
    return buf;
}

/* ---- the input record (the same rules as apps/io-checker-love/model.lua) ------------------------------------------------ */

static void add_log(App *a, const char *line) {
    memmove(a->log[1], a->log[0], sizeof(a->log[0]) * (LOG_LINES - 1));
    snprintf(a->log[0], sizeof a->log[0], "%s", line);
}

/* One numbered event line; is_held < 0 for keys, which have no state to compare with. A DOWN that says "free" means the event
 * stream and the state disagree. */
static void event(App *a, int is_held, const char *fmt, ...) {
    char what[32], line[48];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(what, sizeof what, fmt, ap);
    va_end(ap);
    if (is_held < 0) snprintf(line, sizeof line, "#%u %s", ++a->seq, what);
    else snprintf(line, sizeof line, "#%u %s (%s)", ++a->seq, what, is_held ? "held" : "free");
    add_log(a, line);
}

static void rec_press(App *a, int b, Uint32 t) {
    if (b <= 0 || b >= MAX_B) return;
    a->presses[b]++;
    if (a->has_up[b] && t - a->last_up[b] < CHATTER_MS) a->chatter[b]++;
}
static void rec_release(App *a, int b, Uint32 t) {
    if (b <= 0 || b >= MAX_B) return;
    a->last_up[b] = t;
    a->has_up[b] = 1;
}
static void rec_axis(App *a, int i, float v) {
    if (i < 1 || i >= MAX_A) return;
    if (!a->amoved[i]) { a->amin[i] = a->amax[i] = v; a->amoved[i] = 1; }
    if (v < a->amin[i]) a->amin[i] = v;
    if (v > a->amax[i]) a->amax[i] = v;
}
static void rec_key(App *a, const char *name) {
    for (int i = 0; i < a->nkeys; i++)
        if (!strcmp(a->key_name[i], name)) { a->key_count[i]++; return; }
    if (a->nkeys < MAX_KEYS) {
        snprintf(a->key_name[a->nkeys], sizeof a->key_name[0], "%s", name);
        a->key_count[a->nkeys++] = 1;
    }
}
static int key_seen(const App *a, const char *n1, const char *n2) {
    for (int i = 0; i < a->nkeys; i++)
        if (!strcmp(a->key_name[i], n1) || !strcmp(a->key_name[i], n2)) return 1;
    return 0;
}
static int missing(const App *a, int *out) {
    int n = 0;
    for (int b = 1; b <= a->nbuttons && b < MAX_B; b++)
        if (!a->presses[b]) out[n++] = b;
    return n;
}
static int down(const App *a, int b) { return b > 0 && b < MAX_B && a->down[b]; }
static float axis(const App *a, int i) { return i >= 1 && i <= a->naxes && i < MAX_A ? a->axis[i] : 0; }
static int gcb(const App *a, SDL_GameControllerButton b) { return a->gc && SDL_GameControllerGetButton(a->gc, b); }
static int exit_held(const App *a) {
    return (down(a, L_SELECT) && down(a, L_START)) || (gcb(a, SDL_CONTROLLER_BUTTON_BACK) && gcb(a, SDL_CONTROLLER_BUTTON_START));
}
static void hat_text(Uint8 h, char *out) {
    out[0] = 0;
    if (h & SDL_HAT_LEFT) strcat(out, "l");
    if (h & SDL_HAT_RIGHT) strcat(out, "r");
    if (h & SDL_HAT_UP) strcat(out, "u");
    if (h & SDL_HAT_DOWN) strcat(out, "d");
    if (!out[0]) strcat(out, "c");
}

/* ---- frame times of the motion pattern -------------------------------------------------------------------------------- */

typedef struct { int frames, late; double fps, mean, worst; } Frames;

static void frames_reset(App *a) { a->ndts = a->skipped = 0; }
static void frames_add(App *a, double dt) {
    if (a->skipped < WARMUP) { a->skipped++; return; }
    if (a->ndts == MAX_DTS) { memmove(a->dts, a->dts + 1, sizeof(double) * (MAX_DTS - 1)); a->ndts--; }
    a->dts[a->ndts++] = dt;
}
/* 0 until a second of frames. */
static int frames_stats(const App *a, Frames *f) {
    double sum = 0, worst = 0;
    for (int i = 0; i < a->ndts; i++) { sum += a->dts[i]; if (a->dts[i] > worst) worst = a->dts[i]; }
    if (a->ndts < 30 || sum < 1) return 0;
    double mean = sum / a->ndts;
    int late = 0;
    for (int i = 0; i < a->ndts; i++) late += a->dts[i] > mean * 1.5;
    *f = (Frames){a->ndts, late, a->ndts / sum, mean * 1000, worst * 1000};
    return 1;
}
static const char *frames_line(const App *a, char *buf, size_t cap) {
    Frames f;
    if (!frames_stats(a, &f)) snprintf(buf, cap, "not measured");
    else snprintf(buf, cap, "%.1f fps, %.1f ms mean, %.1f ms worst, %d late of %d", f.fps, f.mean, f.worst, f.late, f.frames);
    return buf;
}

/* ---- tones ------------------------------------------------------------------------------------------------------------ */

/* Sample i of a tone: left and right in -1..1, faded in and out; the sweep rises exponentially. */
static void tone_sample(const Tone *t, int i, double *l, double *r) {
    double tt = (double)i / RATE, len = t->secs, phase;
    if (t->from > 0) {
        double k = log(t->to / t->from);
        phase = 2 * M_PI * t->from * len / k * (exp(k * tt / len) - 1);
    } else {
        phase = 2 * M_PI * t->freq * tt;
    }
    double env = tt / FADE;
    if ((len - tt) / FADE < env) env = (len - tt) / FADE;
    if (env > 1) env = 1;
    if (env < 0) env = 0;
    double v = sin(phase) * t->amp * env;
    *l = v * t->left;
    *r = v * t->right;
}
static int tone_frames(const Tone *t) { return (int)(t->secs * RATE); }

static int audio_open(App *a) {
    if (a->dev) return 1;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        snprintf(a->audio_dev, sizeof a->audio_dev, "SDL audio did not start: %.80s", SDL_GetError());
        a->audio_error = 1;
        return 0;
    }
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    a->dev = SDL_OpenAudioDevice(NULL, 0, &want, &a->have, 0);
    if (!a->dev) {
        snprintf(a->audio_dev, sizeof a->audio_dev, "cannot open: %.90s", SDL_GetError());
        a->audio_error = 1;
        return 0;
    }
    const char *name = SDL_GetNumAudioDevices(0) > 0 ? SDL_GetAudioDeviceName(0, 0) : NULL;
    snprintf(a->audio_dev, sizeof a->audio_dev, "SDL %s, %s, %d Hz", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?",
             name ? name : "default", a->have.freq);
    a->audio_error = 0;
    return 1;
}

static void play(App *a, int i) {
    const Tone *t = &TONES[i];
    if (!audio_open(a)) { say(a, R3_ST_ERROR, "No sound: %s", a->audio_dev); return; }
    int n = tone_frames(t);
    Sint16 *buf = malloc((size_t)n * 2 * sizeof(Sint16));
    if (!buf) { say(a, R3_ST_ERROR, "Out of memory for the tone."); return; }
    for (int k = 0; k < n; k++) {
        double l, r;
        tone_sample(t, k, &l, &r);
        buf[2 * k] = (Sint16)lround(l * 32767);
        buf[2 * k + 1] = (Sint16)lround(r * 32767);
    }
    SDL_ClearQueuedAudio(a->dev);
    int rc = SDL_QueueAudio(a->dev, buf, (Uint32)((size_t)n * 2 * sizeof(Sint16)));
    free(buf);
    if (rc != 0) { say(a, R3_ST_ERROR, "Could not play: %s", SDL_GetError()); a->audio_error = 1; return; }
    SDL_PauseAudioDevice(a->dev, 0);
    a->answers[i] = A_PLAYED;
    say(a, R3_ST_HINT, "Playing %s. Heard it? X yes, Y no.", t->label);
}

static void answer(App *a, int i, int ans) {
    a->answers[i] = ans;
    say(a, ans == A_HEARD ? R3_ST_OK : R3_ST_WARN, "%s: %s.", TONES[i].label, ans == A_HEARD ? "heard" : "silent");
}

static const char *answer_text(int ans) {
    switch (ans) {
    case A_HEARD: return "heard";
    case A_SILENT: return "SILENT";
    case A_PLAYED: return "played";
    default: return "";
    }
}

/* ---- what the system says ---------------------------------------------------------------------------------------------- */

static int read_first(const char *path, char *out, size_t cap) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = 0;
    for (size_t i = 0; i < n; i++) if (out[i] == 0) { out[i] = 0; n = i; break; }
    out[strcspn(out, "\r\n")] = 0;
    return 0;
}

static void sound_status(App *a) {
    char cards[512] = "";
    r3_report_slurp_line("/proc/asound/cards", cards, sizeof cards);
    const char *p = strstr(cards, "]:");
    snprintf(a->snd_card, sizeof a->snd_card, "%s", p ? p + 2 + strspn(p + 2, " ") : "none");
    char *bar = strstr(a->snd_card, " | ");
    if (bar) *bar = 0;
    a->snd_path[0] = a->snd_jack[0] = a->snd_hp[0] = 0;
    char cmd[PATH_MAX + 32], line[256];
    snprintf(cmd, sizeof cmd, "'%s' status 2>/dev/null", env_or("R3I6NOS_SOUND", "/usr/local/bin/r3i6nos-sound"));
    FILE *f = popen(cmd, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *v;
        if (strstr(line, "playback path: ") && (v = strstr(line, ": ")) && !a->snd_path[0]) sscanf(v + 2, "%15s", a->snd_path);
        else if (!strncmp(line, "jack: ", 6)) snprintf(a->snd_jack, sizeof a->snd_jack, "%.63s", line + 6);
        else if (!strncmp(line, "headphones: ", 12)) snprintf(a->snd_hp, sizeof a->snd_hp, "%.15s", line + 12);
    }
    pclose(f);
}

static int find_class(const char *base, const char *want_type, char *out, size_t cap) {
    DIR *d = opendir(base);
    if (!d) return -1;
    struct dirent *e;
    int found = -1;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char p[PATH_MAX], t[64];
        snprintf(p, sizeof p, "%s/%s/type", base, e->d_name);
        if (!want_type || (read_first(p, t, sizeof t) == 0 && !strcmp(t, want_type))) {
            snprintf(out, cap, "%s/%s", base, e->d_name);
            found = 0;
            break;
        }
    }
    closedir(d);
    return found;
}

static void video_info(App *a, SDL_Window *win, SDL_Renderer *r) {
    SDL_DisplayMode m;
    int disp = win ? SDL_GetWindowDisplayIndex(win) : 0;
    if (win) SDL_GetWindowSize(win, &a->win_w, &a->win_h);
    if (!win) snprintf(a->mode, sizeof a->mode, "no window (--report)");
    else if (SDL_GetCurrentDisplayMode(disp < 0 ? 0 : disp, &m) == 0)
        snprintf(a->mode, sizeof a->mode, "%dx%d, %d Hz", m.w, m.h, m.refresh_rate);
    else snprintf(a->mode, sizeof a->mode, "%dx%d, ? Hz", a->win_w, a->win_h);
    SDL_RendererInfo ri;
    if (r && SDL_GetRendererInfo(r, &ri) == 0)
        snprintf(a->renderer, sizeof a->renderer, "SDL %s%s", ri.name, (ri.flags & SDL_RENDERER_PRESENTVSYNC) ? ", vsync" : "");
    else snprintf(a->renderer, sizeof a->renderer, "none");
    snprintf(a->vdriver, sizeof a->vdriver, "%s", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "none");
    char dir[PATH_MAX], p[PATH_MAX + 32], now[32], max[32];
    if (find_class("/sys/class/backlight", NULL, dir, sizeof dir) == 0) {
        snprintf(p, sizeof p, "%s/brightness", dir);
        int ok = read_first(p, now, sizeof now) == 0;
        snprintf(p, sizeof p, "%s/max_brightness", dir);
        ok = ok && read_first(p, max, sizeof max) == 0;
        snprintf(a->backlight, sizeof a->backlight, ok ? "%s of %s (%s)" : "unreadable%.0s%.0s (%s)", now, max, strrchr(dir, '/') + 1);
    } else {
        snprintf(a->backlight, sizeof a->backlight, "none found");
    }
}

static void add_sys(App *a, const char *label, const char *fmt, ...) {
    if (a->nsys >= (int)(sizeof a->sys / sizeof a->sys[0])) return;
    R3Item *it = &a->sys[a->nsys++];
    snprintf(it->label, sizeof it->label, "%s", label);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(it->value, sizeof it->value, fmt, ap);
    va_end(ap);
}

static void system_rows(App *a) {
    a->nsys = 0;
    char v[256], c[64];
    char ver[64] = "", commit[64] = "";
    r3_report_field("/etc/r3i6nos-release", "VERSION=", ver, sizeof ver);
    r3_report_field("/etc/r3i6nos-release", "COMMIT=", commit, sizeof commit);
    if (ver[0]) add_sys(a, "r3i6nos", "%s, %.7s", ver, commit[0] ? commit : "?");
    else add_sys(a, "r3i6nos", "not r3i6nos");
    struct utsname u;
    add_sys(a, "Kernel", "%s", uname(&u) == 0 ? u.release : "?");
    add_sys(a, "Board", "%s", read_first("/proc/device-tree/model", v, sizeof v) == 0 ? v : "?");
    long khz = read_first("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", c, sizeof c) == 0 ? atol(c) : 0;
    if (khz > 0) add_sys(a, "CPU", "%ld cores, %ld MHz max", sysconf(_SC_NPROCESSORS_CONF), khz / 1000);
    else add_sys(a, "CPU", "%ld cores", sysconf(_SC_NPROCESSORS_CONF));
    char total[64] = "", avail[64] = "";
    if (r3_report_field("/proc/meminfo", "MemTotal", total, sizeof total) == 0) {
        r3_report_field("/proc/meminfo", "MemAvailable", avail, sizeof avail);
        add_sys(a, "Memory", "%ld MB, %ld MB free", atol(total) / 1024, atol(avail) / 1024);
    } else {
        add_sys(a, "Memory", "?");
    }
    char dir[PATH_MAX], p[PATH_MAX + 32], cap[32] = "?", st[32] = "?";
    if (find_class("/sys/class/power_supply", "Battery", dir, sizeof dir) == 0) {
        snprintf(p, sizeof p, "%s/capacity", dir);
        read_first(p, cap, sizeof cap);
        snprintf(p, sizeof p, "%s/status", dir);
        read_first(p, st, sizeof st);
        add_sys(a, "Battery", "%s%%, %s", cap, st);
    } else {
        add_sys(a, "Battery", "none found");
    }
    add_sys(a, "Backlight", "%s", a->backlight);
    SDL_version sv;
    SDL_GetVersion(&sv);
    add_sys(a, "Runs on", "native, SDL %d.%d.%d", sv.major, sv.minor, sv.patch);
    add_sys(a, "Screen", "%s", a->mode);
    add_sys(a, "Renderer", "%s, video driver %s", a->renderer, a->vdriver);
    add_sys(a, "Pad", "%s", a->js ? a->pad_name : "none");
    add_sys(a, "GUID", "%s", a->js ? a->guid : "-");
    if (a->js) add_sys(a, "Pad shape", "%d buttons, %d axes, %d hats", a->nbuttons, a->naxes, a->nhats);
    else add_sys(a, "Pad shape", "-");
    add_sys(a, "SDL mapping", "%s", a->js ? (a->gc ? "yes" : "no (raw buttons are right)") : "-");
    add_sys(a, "Sound card", "%s", a->snd_card);
    add_sys(a, "Speaker path", "%s", a->snd_path[0] ? a->snd_path : "unknown");
}

/* ---- the cards ----------------------------------------------------------------------------------------------------------- */

/* The mount that holds path (the longest mount point): its filesystem and device. 1 when found; *exact says it is the mount point. */
static int mount_of(const char *path, char *fs, size_t fcap, char *dev, size_t dcap, int *exact) {
    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return 0;
    char d[256], m[4096], t[64];
    size_t best = 0;
    int found = 0;
    while (fscanf(f, "%255s %4095s %63s %*[^\n]", d, m, t) == 3) {
        size_t n = strlen(m);
        int under = !strncmp(path, m, n) && (path[n] == '/' || path[n] == 0 || (n == 1 && m[0] == '/'));
        if (under && n >= best) {
            best = n;
            snprintf(fs, fcap, "%s", t);
            snprintf(dev, dcap, "%s", d);
            *exact = path[n] == 0;
            found = 1;
        }
    }
    fclose(f);
    return found;
}

static void add_row(App *a, const char *status, const char *card, const char *item, const char *fmt, ...) {
    if (a->nfiles >= (int)(sizeof a->files / sizeof a->files[0])) return;
    Row *r = &a->files[a->nfiles++];
    snprintf(r->status, sizeof r->status, "%s", status);
    snprintf(r->card, sizeof r->card, "%s", card);
    snprintf(r->item, sizeof r->item, "%s", item);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->value, sizeof r->value, fmt, ap);
    va_end(ap);
}

static int write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    int ok = fputs(text, f) >= 0;
    return (fclose(f) == 0) && ok;
}

/* The checks on one card, in its IO_Checker folder (the same as apps/io-checker-love/probe.lua). */
static void card_check(App *a, const char *label, const char *path) {
    char fs[64] = "?", dev[256] = "?";
    int exact = 0;
    mount_of(path, fs, sizeof fs, dev, sizeof dev, &exact);
    add_row(a, "INFO", label, "where", "%s, %s, %s", path, fs, dev);
    char dir[PATH_MAX], fa[PATH_MAX + 32], fb[PATH_MAX + 32], buf[64] = "";
    snprintf(dir, sizeof dir, "%s/IO_Checker", path);
    snprintf(fa, sizeof fa, "%s/.io-check-a.tmp", dir);
    snprintf(fb, sizeof fb, "%s/.io-check-b.tmp", dir);
    const char *step = NULL;
    char why[160];
    if (r3_report_mkdirs(dir) != 0 || !write_text(fa, "first\n")) { snprintf(why, sizeof why, "cannot make or write in %.100s", dir); step = why; }
    else if (!write_text(fb, "second\n")) step = "cannot write a second file";
    else if (rename(fb, fa) != 0) step = "cannot rename over a file";
    else {
        FILE *f = fopen(fa, "r");
        int same = f && fgets(buf, sizeof buf, f) && !strcmp(buf, "second\n");
        if (f) fclose(f);
        if (!same) step = "read back the wrong text";
        else if (unlink(fa) != 0) step = "cannot delete";
        else if (access(fa, F_OK) == 0) step = "deleted, but still there";
    }
    unlink(fa);
    unlink(fb);
    add_row(a, step ? "FAIL" : "PASS", label, "write, rename over, read, delete", "%s", step ? step : "ok");
    if (step) return;

    int mb = atoi(env_or("R3I6NOS_IO_MB", "8"));
    if (mb < 1) mb = 1;
    size_t chunk = 1 << 20;
    unsigned char *data = malloc(chunk), *back = malloc(chunk);
    if (!data || !back) { free(data); free(back); add_row(a, "FAIL", label, "write speed", "out of memory"); return; }
    for (size_t i = 0; i < chunk; i++) data[i] = (unsigned char)i;
    char sp[PATH_MAX + 32];
    snprintf(sp, sizeof sp, "%s/.io-check-speed.tmp", dir);
    double t0 = r3_report_ms();
    int fd = open(sp, O_WRONLY | O_CREAT | O_TRUNC, 0644), ok = fd >= 0;
    for (int i = 0; ok && i < mb; i++) ok = write(fd, data, chunk) == (ssize_t)chunk;
    if (fd >= 0) { ok = ok && fsync(fd) == 0; ok = (close(fd) == 0) && ok; }
    double tw = r3_report_ms() - t0;
    if (!ok) {
        unlink(sp);
        free(data); free(back);
        add_row(a, "FAIL", label, "write speed", "could not write %d MB (card full?)", mb);
        return;
    }
    add_row(a, "INFO", label, "write speed", "%.1f MB/s (%d MB + sync)", mb * 1.048576 / (tw > 0.001 ? tw / 1000 : 1e-6), mb);
    t0 = r3_report_ms();
    fd = open(sp, O_RDONLY);
    long long n = 0;
    int same = fd >= 0;
    ssize_t got;
    while (fd >= 0 && (got = read(fd, back, chunk)) > 0) {
        if (memcmp(back, data, (size_t)got) != 0) same = 0;
        n += got;
    }
    if (fd >= 0) close(fd);
    double tr = r3_report_ms() - t0;
    unlink(sp);
    free(data); free(back);
    long long want = (long long)chunk * mb;
    if (same && n == want) add_row(a, "PASS", label, "read back", "every byte the same");
    else add_row(a, "FAIL", label, "read back", "%lld of %lld bytes, %s", n, want, same ? "same" : "DIFFERENT");
    add_row(a, "INFO", label, "read speed", "%.1f MB/s (may be cached)", mb * 1.048576 / (tr > 0.001 ? tr / 1000 : 1e-6));
    struct statvfs st;
    if (statvfs(path, &st) == 0) {
        double free_b = (double)st.f_bavail * (double)st.f_frsize;
        if (free_b >= 1e9) add_row(a, "INFO", label, "free", "%.1f GB", free_b / 1e9);
        else add_row(a, "INFO", label, "free", "%.0f MB", free_b / 1e6);
    } else {
        add_row(a, "INFO", label, "free", "?");
    }
}

static void run_files(App *a) {
    a->nfiles = 0;
    const char *roms = env_or("R3I6NOS_ROMS", "/roms"), *roms2 = env_or("R3I6NOS_ROMS2", "/roms2");
    card_check(a, "Games card", roms);
    char fs[64], dev[256];
    int exact = 0;
    if ((mount_of(roms2, fs, sizeof fs, dev, sizeof dev, &exact) && exact) || getenv("R3I6NOS_ROMS2")) card_check(a, "Data card", roms2);
    a->files_run = 1;
    int bad = 0;
    for (int i = 0; i < a->nfiles; i++) bad += !strcmp(a->files[i].status, "FAIL");
    if (bad) say(a, R3_ST_ERROR, "%d check(s) failed: see the list.", bad);
    else say(a, R3_ST_OK, "Every card passed.");
}

/* ---- the device report ------------------------------------------------------------------------------------------------- */

static void report_input(App *a, R3Report *r) {
    r3_report_section(r, "input");
    if (!a->js) {
        r3_report_line(r, R3_FAIL, "pad", "none: the pad did not reach SDL");
    } else {
        r3_report_line(r, R3_INFO, "pad", "%s", a->pad_name);
        r3_report_line(r, R3_INFO, "guid", "%s", a->guid);
        r3_report_line(r, a->nbuttons == 17 && a->naxes == 4 ? R3_PASS : R3_FAIL, "shape", "%d buttons, %d axes, %d hats (R36S: 17, 4, 0)",
                       a->nbuttons, a->naxes, a->nhats);
        r3_report_line(r, R3_INFO, "sdl gamepad mapping", "%s", a->gc ? "yes" : "no");
        int miss[MAX_B], nm = missing(a, miss);
        char list[600] = "";
        for (int i = 0; i < nm; i++) {
            size_t u = strlen(list);
            snprintf(list + u, sizeof list - u, "%s%s(%d)", i ? " " : "", miss[i] < 18 ? NAMES[miss[i]] : "", miss[i]);
        }
        if (a->seq == 0) r3_report_line(r, R3_SKIP, "buttons pressed", "none: open Buttons and sticks and press every button first");
        else if (nm == 0) r3_report_line(r, R3_PASS, "buttons pressed", "all %d", a->nbuttons);
        else r3_report_line(r, R3_FAIL, "buttons pressed", "%d of %d; never: %s", a->nbuttons - nm, a->nbuttons, list);
        char pr[600] = "", ch[400] = "";
        for (int b = 1; b <= a->nbuttons && b < MAX_B; b++) {
            const char *nmb = b < 18 ? NAMES[b] : "";
            if (a->presses[b]) { size_t u = strlen(pr); snprintf(pr + u, sizeof pr - u, "%s%s(%d)=%d", u ? " " : "", nmb, b, a->presses[b]); }
            if (a->chatter[b]) { size_t u = strlen(ch); snprintf(ch + u, sizeof ch - u, "%s%s(%d)=%d", u ? " " : "", nmb, b, a->chatter[b]); }
        }
        if (pr[0]) r3_report_line(r, R3_INFO, "presses", "%s", pr);
        if (a->seq > 0) {
            if (ch[0]) r3_report_line(r, R3_FAIL, "chatter", "%s", ch);
            else r3_report_line(r, R3_PASS, "chatter", "none (no press within %d ms of a release)", CHATTER_MS);
        }
        char k[32], b1[8], b2[8];
        for (int i = 1; i <= a->naxes && i < MAX_A; i++) {
            snprintf(k, sizeof k, "axis %d range", i);
            if (a->amoved[i])
                r3_report_line(r, a->amin[i] <= -0.9 && a->amax[i] >= 0.9 ? R3_PASS : R3_FAIL, k, "%s to %s (goal: past -0.90 and +0.90)",
                               fmt2(b1, a->amin[i]), fmt2(b2, a->amax[i]));
            else r3_report_line(r, R3_SKIP, k, "never moved");
            snprintf(k, sizeof k, "axis %d at rest", i);
            r3_report_line(r, fabsf(axis(a, i)) < 0.1f ? R3_PASS : R3_FAIL, k,
                           "%s when the report ran (goal: within 0.10 of centre; hands off the sticks)", fmt2(b1, axis(a, i)));
        }
        char dirs[64] = "";
        for (int v = 1; v < 16; v++)
            if (a->hats_seen & (1u << v)) { char h[8]; hat_text((Uint8)v, h); size_t u = strlen(dirs); snprintf(dirs + u, sizeof dirs - u, "%s%s", u ? " " : "", h); }
        r3_report_line(r, R3_INFO, "hat directions", "%s", dirs[0] ? dirs : "none");
    }
    const char *groups[3][3] = {{"power", "sleep", "power key"}, {"volumedown", "audiolower", "volume down key"}, {"volumeup", "audioraise", "volume up key"}};
    for (int g = 0; g < 3; g++) {
        if (key_seen(a, groups[g][0], groups[g][1])) r3_report_line(r, R3_PASS, groups[g][2], "arrived as a key");
        else r3_report_line(r, R3_SKIP, groups[g][2], "not pressed on Buttons and sticks (or it never reaches apps)");
    }
    char keys[400] = "";
    for (int i = 0; i < a->nkeys; i++) { size_t u = strlen(keys); snprintf(keys + u, sizeof keys - u, "%s%s=%d", u ? " " : "", a->key_name[i], a->key_count[i]); }
    if (keys[0]) r3_report_line(r, R3_INFO, "keys", "%s", keys);
}

static void write_report(App *a, const char *how) {
    const char *roms = env_or("R3I6NOS_ROMS", "/roms");
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s/IO_Checker", roms);
    R3Report r;
    memset(&r, 0, sizeof r);
    r.quiet = 1;
    if (r3_report_open_named(&r, dir, "r36s-report-native.txt", "IO Checker", VERSION, "r36s-io-checker-native", how, "r3i6nos") != 0) {
        say(a, R3_ST_ERROR, "Could not write the report in %s", dir);
        return;
    }
    sound_status(a);
    system_rows(a);
    r3_report_system(&r);
    for (int i = 0; i < a->nsys; i++) r3_report_line(&r, R3_INFO, a->sys[i].label, "%s", a->sys[i].value);
    report_input(a, &r);

    r3_report_section(&r, "video");
    r3_report_line(&r, R3_INFO, "mode", "%s", a->mode);
    r3_report_line(&r, R3_INFO, "renderer", "%s, video driver %s", a->renderer, a->vdriver);
    if (a->win_w) r3_report_line(&r, a->win_w == 640 && a->win_h == 480 ? R3_PASS : R3_FAIL, "window", "%dx%d (goal 640x480)", a->win_w, a->win_h);
    else r3_report_line(&r, R3_SKIP, "window", "no window (--report)");
    Frames f;
    char fl[128];
    if (frames_stats(a, &f))
        r3_report_line(&r, f.fps >= 55 && f.late <= f.frames / 100 ? R3_PASS : R3_FAIL, "motion", "%s (goal 55+ fps, under 1%% late)", frames_line(a, fl, sizeof fl));
    else r3_report_line(&r, R3_SKIP, "motion", "not measured: show Video > Motion and frame rate for a few seconds first");
    char shown[160] = "";
    for (int i = 0; i < NPAT; i++)
        if (a->pattern_seen[i]) { size_t u = strlen(shown); snprintf(shown + u, sizeof shown - u, "%s%s", u ? " " : "", PATTERNS[i].id); }
    r3_report_line(&r, R3_INFO, "patterns shown", "%s", shown[0] ? shown : "none");
    r3_report_line(&r, R3_INFO, "how they looked", "not judged by a program: say in words what was wrong, if anything");
    r3_report_line(&r, R3_INFO, "backlight", "%s", a->backlight);

    r3_report_section(&r, "sound");
    r3_report_line(&r, R3_INFO, "card", "%s", a->snd_card);
    if (a->snd_path[0]) r3_report_line(&r, strcmp(a->snd_path, "OFF") ? R3_PASS : R3_FAIL, "speaker path", "%s%s", a->snd_path,
                                       strcmp(a->snd_path, "OFF") ? "" : " (the codec is silent)");
    else r3_report_line(&r, R3_SKIP, "speaker path", "unknown (no r3i6nos-sound, or another codec)");
    r3_report_line(&r, R3_INFO, "headphones", "%s, jack %s", a->snd_hp[0] ? a->snd_hp : "?", a->snd_jack[0] ? a->snd_jack : "none");
    audio_open(a);
    r3_report_check(&r, !a->audio_error, "output opened", a->audio_dev);
    char key[64];
    for (int i = 0; i < NTONE; i++) {
        snprintf(key, sizeof key, "heard %s", TONES[i].label);
        switch (a->answers[i]) {
        case A_HEARD: r3_report_line(&r, R3_PASS, key, "yes"); break;
        case A_SILENT: r3_report_line(&r, R3_FAIL, key, "no (silent)"); break;
        case A_PLAYED: r3_report_line(&r, R3_SKIP, key, "played, not answered (X heard, Y silent)"); break;
        default: r3_report_line(&r, R3_SKIP, key, "not played");
        }
    }

    r3_report_section(&r, "files");
    run_files(a);
    for (int i = 0; i < a->nfiles; i++) {
        snprintf(key, sizeof key, "%s %s", a->files[i].card, a->files[i].item);
        r3_report_line(&r, a->files[i].status, key, "%s", a->files[i].value);
    }
    r3_report_memory(&r);
    int pass = r.pass, fail = r.fail, skip = r.skip;
    if (r3_report_close(&r) == 0) {
        snprintf(a->report_file, sizeof a->report_file, "EASYROMS/IO_Checker/r36s-report-native.txt");
        a->report_written = 1;
        say(a, fail ? R3_ST_WARN : R3_ST_OK, "Written: %s (PASS %d FAIL %d SKIP %d)", a->report_file, pass, fail, skip);
    } else {
        say(a, R3_ST_ERROR, "Could not write the report in %s", dir);
    }
}

/* ---- the screen ---------------------------------------------------------------------------------------------------------- */

#define LIT_FG R3_BLACK
#define LIT_BG R3_LIGHT_GREEN

static const char *runtime(void) {
    static char rt[40];
    SDL_version v;
    SDL_GetVersion(&v);
    snprintf(rt, sizeof rt, "native SDL %d.%d.%d", v.major, v.minor, v.patch);
    return rt;
}

static void cmdline(R3Screen *s, const App *a, const char *line, const char *left) {
    R3Cmd c = {a->reply, a->reply_style ? a->reply_style : R3_ST_HINT, '>', line, 0, left, runtime(), 0};
    r3_command_line(s, &c);
}

static void text_at(R3Screen *s, int col, int row, int max, int style, const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    r3_text_fit(s, col, row, buf, max, R3_ST(style));
}

static void home_value(const App *a, int id, char *out, size_t cap) {
    out[0] = 0;
    if (id == H_INPUT && a->js) { int m[MAX_B]; snprintf(out, cap, "%d/%d buttons", a->nbuttons - missing(a, m), a->nbuttons); }
    Frames f;
    if (id == H_VIDEO && frames_stats(a, &f)) snprintf(out, cap, "%.1f fps", f.fps);
    if (id == H_SOUND) {
        int heard = 0, asked = 0;
        for (int i = 0; i < NTONE; i++) { asked += a->answers[i] >= A_HEARD; heard += a->answers[i] == A_HEARD; }
        if (asked) snprintf(out, cap, "%d/%d heard", heard, NTONE);
    }
    if (id == H_FILES && a->files_run) {
        int bad = 0;
        for (int i = 0; i < a->nfiles; i++) bad += !strcmp(a->files[i].status, "FAIL");
        if (bad) snprintf(out, cap, "%d FAIL", bad);
        else snprintf(out, cap, "ok");
    }
    if (id == H_REPORT && a->report_written) snprintf(out, cap, "written");
}

static void page_home(R3Screen *s, const App *a) {
    R3Item items[H_COUNT];
    char title[32];
    for (int i = 0; i < H_COUNT; i++) { snprintf(items[i].label, sizeof items[i].label, "%s", HOME_LABEL[i]); home_value(a, i, items[i].value, sizeof items[i].value); }
    snprintf(title, sizeof title, "IO Checker %s", VERSION);
    R3List l = {0, 1, R3_COLS, H_COUNT + 2, title, "A open  B quit", items, H_COUNT, a->sel[P_HOME], {NULL}};
    r3_list_dialog(s, &l);
    int top = H_COUNT + 3, sel = a->sel[P_HOME];
    R3Box b = {0, HOME_LABEL[sel], NULL, NULL, 0};
    r3_box(s, 0, top, R3_COLS, R3_DISPLAY_TOP + R3_DISPLAY_ROWS - top, &b);
    for (int i = 0; i < 4 && HOME_ABOUT[sel][i]; i++) r3_text_fit(s, 2, top + 1 + i, HOME_ABOUT[sel][i], R3_COLS - 4, R3_ST(R3_ST_BODY));
    char line[64];
    if (sel == H_QUIT) snprintf(line, sizeof line, "quit");
    else snprintf(line, sizeof line, "open %s", HOME_LABEL[sel]);
    cmdline(s, a, line, a->js ? a->pad_name : "no pad found");
}

static void key_cell(R3Screen *s, int col, int row, const char *label, int on) {
    char buf[16];
    snprintf(buf, sizeof buf, "[%s]", label);
    if (on) r3_text(s, col, row, buf, LIT_FG, LIT_BG);
    else r3_text(s, col, row, buf, R3_ST(R3_ST_BODY));
}
static void arrow_cell(R3Screen *s, int col, int row, int glyph, int on) {
    key_cell(s, col, row, " ", on);
    r3_put(s, col + 1, row, glyph, on ? LIT_FG : r3_styles[R3_ST_BODY][0], -1);
}
static void stick_box(R3Screen *s, int col, int row, float ax, float ay, int pressed, const char *label) {
    R3Box b = {0, NULL, NULL, NULL, 0};
    r3_box(s, col, row, 9, 7, &b);
    if (pressed)
        for (int i = 0; i < 9 * 7; i++) {
            int c = col + i % 9, r = row + i / 9;
            if (c == col || c == col + 8 || r == row || r == row + 6) s->fg[r * R3_COLS + c] = R3_LIGHT_GREEN;
        }
    r3_put(s, col + 4, row + 3, 250, R3_ST(R3_ST_BODY));
    int dx = (int)floor(ax * 3 + 0.5), dy = (int)floor(ay * 2 + 0.5);
    dx = dx < -3 ? -3 : (dx > 3 ? 3 : dx);
    dy = dy < -2 ? -2 : (dy > 2 ? 2 : dy);
    r3_put(s, col + 4 + dx, row + 3 + dy, 254, pressed ? R3_LIGHT_GREEN : R3_LIGHT_CYAN, -1);
    char buf[24];
    snprintf(buf, sizeof buf, pressed ? "%s IN" : "%s", label);
    if (pressed) r3_text(s, col, row + 7, buf, LIT_FG, LIT_BG);
    else r3_text(s, col, row + 7, buf, R3_ST(R3_ST_BODY));
}

static void page_input(R3Screen *s, const App *a, Uint32 now) {
    if (!a->js) {
        R3Box b = {1, "Buttons and sticks", NULL, NULL, 0};
        r3_box(s, 0, 1, R3_COLS, R3_DISPLAY_ROWS, &b);
        r3_text(s, 2, 3, "No gamepad has reached SDL.", R3_ST(R3_ST_ERROR));
        r3_text(s, 2, 5, "Press a button: this page changes when a pad appears.", R3_ST(R3_ST_BODY));
        r3_text(s, 2, 6, "The power and volume keys are still shown below.", R3_ST(R3_ST_BODY));
        key_cell(s, 2, 9, "PWR", a->k_power);
        key_cell(s, 9, 9, "VOL-", a->k_vdown);
        key_cell(s, 16, 9, "VOL+", a->k_vup);
    } else {
        key_cell(s, 1, 2, "L2", down(a, L_L2)); key_cell(s, 6, 2, "L1", down(a, L_L1));
        key_cell(s, 23, 2, "R1", down(a, L_R1)); key_cell(s, 28, 2, "R2", down(a, L_R2));
        Uint8 h = a->hat;
        arrow_cell(s, 5, 4, 24, down(a, L_UP) || (h & SDL_HAT_UP));
        arrow_cell(s, 2, 5, 27, down(a, L_LEFT) || (h & SDL_HAT_LEFT));
        arrow_cell(s, 8, 5, 26, down(a, L_RIGHT) || (h & SDL_HAT_RIGHT));
        arrow_cell(s, 5, 6, 25, down(a, L_DOWN) || (h & SDL_HAT_DOWN));
        key_cell(s, 26, 4, "X", down(a, L_X)); key_cell(s, 23, 5, "Y", down(a, L_Y));
        key_cell(s, 29, 5, "A", down(a, L_A)); key_cell(s, 26, 6, "B", down(a, L_B));
        stick_box(s, 2, 8, axis(a, 1), axis(a, 2), down(a, L_L3), "L stick");
        stick_box(s, 23, 8, axis(a, 3), axis(a, 4), down(a, L_R3), "R stick");
        key_cell(s, 2, 17, "SELECT", down(a, L_SELECT)); key_cell(s, 12, 17, "FN", down(a, L_FN)); key_cell(s, 18, 17, "START", down(a, L_START));
        key_cell(s, 2, 19, "PWR", a->k_power); key_cell(s, 9, 19, "VOL-", a->k_vdown); key_cell(s, 16, 19, "VOL+", a->k_vup);

        R3Box b = {1, "Raw input", NULL, NULL, 0};
        int bx = 36, w = 28, x = bx + 2, y = 2, room = w - 4;
        r3_box(s, bx, 1, w, R3_DISPLAY_ROWS, &b);
        char held[64] = "", v1[8], v2[8], v3[8];
        for (int i = 1; i <= a->nbuttons && i < MAX_B; i++)
            if (a->down[i]) { size_t u = strlen(held); snprintf(held + u, sizeof held - u, "%s%d", u ? " " : "", i); }
        text_at(s, x, y++, room, held[0] ? R3_ST_OK : R3_ST_BODY, "%-6s%s", "Down", held[0] ? held : "none");
        char hs[8];
        hat_text(h, hs);
        text_at(s, x, y++, room, R3_ST_BODY, "%-6s%s", "Hat", a->nhats > 0 ? hs : "none");
        for (int i = 1; i <= a->naxes && i <= 4; i++) {
            if (a->amoved[i]) text_at(s, x, y++, room, R3_ST_BODY, "A%-5d%s  %s %s", i, fmt2(v1, axis(a, i)), fmt2(v2, a->amin[i]), fmt2(v3, a->amax[i]));
            else text_at(s, x, y++, room, R3_ST_BODY, "A%-5d%s", i, fmt2(v1, axis(a, i)));
        }
        int miss[MAX_B], nm = missing(a, miss);
        char ms[128] = "";
        for (int i = 0; i < nm; i++) { size_t u = strlen(ms); snprintf(ms + u, sizeof ms - u, "%s%d", i ? " " : "", miss[i]); }
        text_at(s, x, y++, room, nm ? R3_ST_BODY : R3_ST_OK, "%-6s%d/%d buttons", "Seen", a->nbuttons - nm, a->nbuttons);
        text_at(s, x, y++, room, nm ? R3_ST_WARN : R3_ST_BODY, "%-6s%s", "Never", nm ? ms : "-");
        char ks[128] = "";
        for (int i = 0; i < a->nkeys; i++) { size_t u = strlen(ks); snprintf(ks + u, sizeof ks - u, "%s%s", i ? " " : "", a->key_name[i]); }
        text_at(s, x, y++, room, R3_ST_BODY, "%-6s%s", "Keys", ks[0] ? ks : "none yet");
        int chat = 0;
        for (int i = 0; i < MAX_B; i++) chat += a->chatter[i];
        if (chat) text_at(s, x, y++, room, R3_ST_WARN, "%-6s%d (bouncing)", "Chat", chat);
        else text_at(s, x, y++, room, R3_ST_BODY, "%-6s%s", "Chat", "none");
        text_at(s, x, y++, room, R3_ST_BODY, "%-6s%s", "Map", a->gc ? "SDL has one" : "none");
        y++;
        r3_text(s, x, y++, "Last events", R3_ST(R3_ST_STRONG));
        for (int i = 0; i < LOG_LINES && y <= 19; i++, y++)
            if (a->log[i][0]) r3_text_fit(s, x, y, a->log[i], room, R3_ST(R3_ST_HINT));
    }
    char reply[96], left[48];
    int style = R3_ST_HINT;
    snprintf(reply, sizeof reply, "Press every button. Hold SELECT+START to go back.");
    Uint32 idle = now - a->idle_since;
    if (a->chord_since) {
        Uint32 held = now - a->chord_since;
        snprintf(reply, sizeof reply, "Going back... %u%%", held >= EXIT_HOLD_MS ? 100u : held * 100u / EXIT_HOLD_MS);
        style = R3_ST_WARN;
    } else if (idle + IDLE_WARN_MS >= IDLE_LIMIT_MS) {
        snprintf(reply, sizeof reply, "No input: back to the list in %u s", (IDLE_LIMIT_MS - (idle > IDLE_LIMIT_MS ? IDLE_LIMIT_MS : idle) + 999) / 1000);
        style = R3_ST_WARN;
    }
    int m[MAX_B];
    snprintf(left, sizeof left, "Seen %d/%d buttons", a->nbuttons - missing(a, m), a->nbuttons);
    R3Cmd c = {reply, style, '>', a->js ? a->pad_name : "no pad", 0, a->js ? left : NULL, runtime(), 0};
    r3_command_line(s, &c);
}

static void page_video(R3Screen *s, const App *a) {
    R3Item items[NPAT];
    Frames f;
    for (int i = 0; i < NPAT; i++) { snprintf(items[i].label, sizeof items[i].label, "%s", PATTERNS[i].label); items[i].value[0] = 0; }
    if (frames_stats(a, &f)) snprintf(items[PAT_MOTION].value, sizeof items[0].value, "%.1f fps", f.fps);
    R3List l = {0, 1, R3_COLS, NPAT + 2, "Video", "A show  L/R next  B back", items, NPAT, a->sel[P_VIDEO], {NULL}};
    r3_list_dialog(s, &l);
    int top = NPAT + 3;
    R3Box b = {0, "This screen", NULL, NULL, 0};
    r3_box(s, 0, top, R3_COLS, R3_DISPLAY_TOP + R3_DISPLAY_ROWS - top, &b);
    char fl[128];
    const char *rows[5][2] = {{"Mode", a->mode}, {"Renderer", a->renderer}, {"Backlight", a->backlight},
                              {"Frame rate", frames_line(a, fl, sizeof fl)}, {"Look for", PATTERNS[a->sel[P_VIDEO]].look}};
    for (int i = 0; i < 5 && top + 1 + i < R3_DISPLAY_TOP + R3_DISPLAY_ROWS - 1; i++)
        text_at(s, 2, top + 1 + i, R3_COLS - 4, R3_ST_BODY, "%-11s%s", rows[i][0], rows[i][1]);
    char line[64], left[32];
    snprintf(line, sizeof line, "show %s", PATTERNS[a->sel[P_VIDEO]].label);
    snprintf(left, sizeof left, "%d of %d", a->sel[P_VIDEO] + 1, NPAT);
    cmdline(s, a, line, left);
}

static void page_sound(R3Screen *s, const App *a) {
    R3Item items[NTONE];
    for (int i = 0; i < NTONE; i++) {
        snprintf(items[i].label, sizeof items[i].label, "%s", TONES[i].label);
        snprintf(items[i].value, sizeof items[i].value, "%s", answer_text(a->answers[i]));
    }
    R3List l = {0, 1, R3_COLS, NTONE + 2, "Sound", "A play  X heard  Y silent  B back", items, NTONE, a->sel[P_SOUND], {NULL}};
    r3_list_dialog(s, &l);
    int top = NTONE + 3, y = top + 1, room = R3_COLS - 4;
    R3Box b = {0, "The sound card", NULL, NULL, 0};
    r3_box(s, 0, top, R3_COLS, R3_DISPLAY_TOP + R3_DISPLAY_ROWS - top, &b);
    text_at(s, 2, y++, room, R3_ST_BODY, "%-14s%s", "Card", a->snd_card);
    if (!strcmp(a->snd_path, "OFF")) text_at(s, 2, y++, room, R3_ST_ERROR, "%-14s%s", "Speaker path", "OFF: the codec is silent (r3i6nos-sound did not run)");
    else if (a->snd_path[0])
        text_at(s, 2, y++, room, R3_ST_OK, "%-14s%s%s", "Speaker path", a->snd_path,
                !strcmp(a->snd_path, "SPK") ? " (speaker)" : (strstr(a->snd_path, "HP") ? " (headphones)" : ""));
    else text_at(s, 2, y++, room, R3_ST_BODY, "%-14s%s", "Speaker path", "unknown (no r3i6nos-sound, or another codec)");
    if (a->snd_hp[0]) text_at(s, 2, y++, room, R3_ST_BODY, "%-14s%s%s%s", "Headphones", a->snd_hp, a->snd_jack[0] ? ", " : "", a->snd_jack);
    else text_at(s, 2, y++, room, R3_ST_BODY, "%-14s%s", "Headphones", a->snd_jack[0] ? a->snd_jack : "no jack switch found");
    text_at(s, 2, y++, room, a->audio_error ? R3_ST_ERROR : R3_ST_BODY, "%-14s%s", "Output", a->audio_dev[0] ? a->audio_dev : "opens on the first tone");
    y++;
    r3_text(s, 2, y, "Play a tone, then press X if you heard it, Y if you did not.", R3_ST(R3_ST_HINT));
    r3_text(s, 2, y + 1, "Try the speaker first, then again with headphones in.", R3_ST(R3_ST_HINT));
    char line[64], left[32];
    snprintf(line, sizeof line, "play %s", TONES[a->sel[P_SOUND]].label);
    snprintf(left, sizeof left, "%d of %d", a->sel[P_SOUND] + 1, NTONE);
    cmdline(s, a, line, left);
}

static void page_files(R3Screen *s, const App *a) {
    static R3Item items[24];
    for (int i = 0; i < a->nfiles; i++) {
        const Row *r = &a->files[i];
        char v[160];
        unsigned char g[64];
        snprintf(items[i].label, sizeof items[i].label, "%s: %s", r->card, r->item);
        snprintf(v, sizeof v, "%s%s", strcmp(r->status, "FAIL") ? "" : "FAIL ", r->value);
        int n = r3_fit(v, 34, g); /* the label always shows */
        /* back to UTF-8 for the list (the fit's » is code page 437 175) */
        size_t u = 0;
        for (int k = 0; k < n && u + 3 < sizeof items[i].value; k++) {
            if (g[k] == 175) { items[i].value[u++] = (char)0xC2; items[i].value[u++] = (char)0xBB; }
            else items[i].value[u++] = (char)g[k];
        }
        items[i].value[u] = 0;
    }
    R3List l = {0, 1, R3_COLS, R3_DISPLAY_ROWS, "Files", "A test again  B back", items, a->nfiles, a->sel[P_FILES],
                {"Press A to test every card: it writes, renames, reads and", "deletes small files in an IO_Checker folder, then times 8 MB.", NULL}};
    r3_list_dialog(s, &l);
    char left[32];
    snprintf(left, sizeof left, "%d of %d", a->sel[P_FILES] + 1, a->nfiles);
    cmdline(s, a, "test every card", a->nfiles ? left : NULL);
}

static void page_system(R3Screen *s, const App *a) {
    R3List l = {0, 1, R3_COLS, R3_DISPLAY_ROWS, "System", "B back", a->sys, a->nsys, a->sel[P_SYSTEM], {NULL}};
    r3_list_dialog(s, &l);
    char left[32];
    snprintf(left, sizeof left, "%d of %d", a->sel[P_SYSTEM] + 1, a->nsys);
    cmdline(s, a, "", left);
}

static void compose(App *a, R3Screen *s, Uint32 now) {
    r3_clear(s);
    r3_top_bar(s, "IO Checker", runtime());
    switch (a->page) {
    case P_INPUT: page_input(s, a, now); break;
    case P_VIDEO: page_video(s, a); break;
    case P_SOUND: page_sound(s, a); break;
    case P_FILES: page_files(s, a); break;
    case P_SYSTEM: page_system(s, a); break;
    default: page_home(s, a);
    }
}

/* ---- the video patterns (pixels, not cells) --------------------------------------------------------------------------------- */

static void fill(SDL_Renderer *r, int cr, int cg, int cb, int x, int y, int w, int h) {
    SDL_Rect q = {x, y, w, h};
    SDL_SetRenderDrawColor(r, (Uint8)cr, (Uint8)cg, (Uint8)cb, 255);
    SDL_RenderFillRect(r, &q);
}

static void draw_pattern(App *a, SDL_Renderer *r) {
    const char *id = PATTERNS[a->sel[P_VIDEO]].id;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    if (!strcmp(id, "white")) fill(r, 255, 255, 255, 0, 0, R3_W, R3_H);
    else if (!strcmp(id, "red")) fill(r, 255, 0, 0, 0, 0, R3_W, R3_H);
    else if (!strcmp(id, "green")) fill(r, 0, 255, 0, 0, 0, R3_W, R3_H);
    else if (!strcmp(id, "blue")) fill(r, 0, 0, 255, 0, 0, R3_W, R3_H);
    else if (!strcmp(id, "bars")) {
        static const int bars[8][3] = {{255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0}, {255, 0, 255}, {255, 0, 0}, {0, 0, 255}, {0, 0, 0}};
        for (int i = 0; i < 8; i++) fill(r, bars[i][0], bars[i][1], bars[i][2], i * 80, 0, 80, 320);
        for (int i = 0; i < 16; i++) fill(r, r3_rgb[i][0], r3_rgb[i][1], r3_rgb[i][2], i * 40, 320, 40, 160);
    } else if (!strcmp(id, "ramps")) {
        for (int x = 0; x < R3_W; x++) {
            int v = x * 255 / (R3_W - 1);
            fill(r, v, v, v, x, 0, 1, 120); fill(r, v, 0, 0, x, 120, 1, 120); fill(r, 0, v, 0, x, 240, 1, 120); fill(r, 0, 0, v, x, 360, 1, 120);
        }
    } else if (!strcmp(id, "grid")) {
        if (!a->checker) {
            a->checker = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, R3_W / 2, R3_H);
            Uint32 *px = malloc((size_t)(R3_W / 2) * R3_H * sizeof(Uint32));
            if (a->checker && px) {
                for (int y = 0; y < R3_H; y++)
                    for (int x = 0; x < R3_W / 2; x++) px[y * (R3_W / 2) + x] = ((x + y) % 2) ? 0xFF000000u : 0xFFFFFFFFu;
                SDL_UpdateTexture(a->checker, NULL, px, (R3_W / 2) * (int)sizeof(Uint32));
            }
            free(px);
        }
        SDL_Rect left = {0, 0, R3_W / 2, R3_H};
        if (a->checker) SDL_RenderCopy(r, a->checker, NULL, &left);
        for (int x = R3_W / 2; x < R3_W; x += 10) fill(r, 128, 128, 128, x, 0, 1, R3_H);
        for (int y = 0; y < R3_H; y += 10) fill(r, 128, 128, 128, R3_W / 2, y, R3_W / 2, 1);
        fill(r, 255, 255, 255, 0, 0, R3_W, 1); fill(r, 255, 255, 255, 0, R3_H - 1, R3_W, 1);
        fill(r, 255, 255, 255, 0, 0, 1, R3_H); fill(r, 255, 255, 255, R3_W - 1, 0, 1, R3_H);
        fill(r, 255, 0, 0, 0, 0, 4, 4); fill(r, 255, 0, 0, R3_W - 4, 0, 4, 4); fill(r, 255, 0, 0, 0, R3_H - 4, 4, 4); fill(r, 255, 0, 0, R3_W - 4, R3_H - 4, 4, 4);
    } else if (!strcmp(id, "motion")) {
        a->motion_n++;
        a->motion_x = (a->motion_x + 8) % R3_W;
        fill(r, 255, 255, 255, a->motion_x, 0, 16, R3_H - 40);
        for (int i = 0; i < 60; i++) fill(r, 85, 85, 85, i * 10 + 20, R3_H - 28, 8, 16);
        fill(r, 255, 255, 0, (a->motion_n % 60) * 10 + 20, R3_H - 28, 8, 16); /* one box a frame: a gap in a photo is a dropped frame */
    }
    SDL_RenderPresent(r);
}

/* ---- pages and actions ------------------------------------------------------------------------------------------------------ */

static void go(App *a, int page, Uint32 now) {
    a->page = page;
    a->chord_since = 0;
    a->idle_since = now;
    a->force = 1;
    if (page == P_INPUT) say(a, R3_ST_HINT, "Press every button. Hold SELECT+START to go back.");
    if (page == P_SYSTEM) system_rows(a);
    if (page == P_SOUND) sound_status(a);
}

static void show_pattern(App *a, int i) {
    a->sel[P_VIDEO] = (i % NPAT + NPAT) % NPAT;
    a->pattern_seen[a->sel[P_VIDEO]] = 1;
    if (a->sel[P_VIDEO] == PAT_MOTION) { frames_reset(a); a->motion_x = a->motion_n = 0; }
    a->page = P_PATTERN;
}

static int count_of(const App *a, int page) {
    switch (page) {
    case P_HOME: return H_COUNT;
    case P_VIDEO: return NPAT;
    case P_SOUND: return NTONE;
    case P_FILES: return a->nfiles;
    case P_SYSTEM: return a->nsys;
    default: return 0;
    }
}
static int per_of(int page) { return page == P_HOME ? H_COUNT : page == P_VIDEO ? NPAT : page == P_SOUND ? NTONE : r3_list_rows(R3_DISPLAY_ROWS); }

static void act(App *a, int act_, Uint32 now) {
    int page = a->page;
    if (page == P_PATTERN) {
        if (act_ == R3_LEFT || act_ == R3_PAGEUP || act_ == R3_UP) show_pattern(a, a->sel[P_VIDEO] - 1);
        else if (act_ == R3_RIGHT || act_ == R3_PAGEDOWN || act_ == R3_DOWN) show_pattern(a, a->sel[P_VIDEO] + 1);
        else if (act_ == R3_BACK || act_ == R3_ACCEPT) {
            a->page = P_VIDEO;
            a->force = 1;
            if (a->sel[P_VIDEO] == PAT_MOTION) {
                char fl[128];
                Frames f;
                say(a, frames_stats(a, &f) ? R3_ST_OK : R3_ST_WARN, "Measured: %s", frames_line(a, fl, sizeof fl));
            }
        }
        return;
    }
    if (page == P_INPUT || page < 0 || page >= P_COUNT) return;
    a->sel[page] = r3_list_key(a->sel[page], count_of(a, page), per_of(page), act_);
    int sel = a->sel[page];
    switch (page) {
    case P_HOME:
        if (act_ == R3_BACK) a->quit = 1;
        else if (act_ == R3_ACCEPT) {
            if (sel == H_QUIT) a->quit = 1;
            else if (sel == H_REPORT) { a->pending = H_REPORT; say(a, R3_ST_WARN, "Writing the device report (the card test takes a few seconds)..."); }
            else if (sel == H_FILES) { go(a, P_FILES, now); if (!a->files_run) { a->pending = H_FILES; say(a, R3_ST_WARN, "Testing the cards..."); } }
            else go(a, sel == H_INPUT ? P_INPUT : sel == H_VIDEO ? P_VIDEO : sel == H_SOUND ? P_SOUND : P_SYSTEM, now);
        }
        break;
    case P_VIDEO:
        if (act_ == R3_BACK) go(a, P_HOME, now);
        else if (act_ == R3_ACCEPT) show_pattern(a, sel);
        break;
    case P_SOUND:
        if (act_ == R3_BACK) go(a, P_HOME, now);
        else if (act_ == R3_ACCEPT) play(a, sel);
        else if (act_ == R3_X) answer(a, sel, A_HEARD);
        else if (act_ == R3_Y) answer(a, sel, A_SILENT);
        break;
    case P_FILES:
        if (act_ == R3_BACK) go(a, P_HOME, now);
        else if (act_ == R3_ACCEPT) { a->pending = H_FILES; say(a, R3_ST_WARN, "Testing the cards..."); }
        break;
    default:
        if (act_ == R3_BACK) go(a, P_HOME, now);
    }
}

/* ---- the pad ------------------------------------------------------------------------------------------------------------- */

static void close_pad(App *a) {
    if (a->gc) SDL_GameControllerClose(a->gc);
    else if (a->js) SDL_JoystickClose(a->js);
    a->gc = NULL;
    a->js = NULL;
    memset(a->down, 0, sizeof a->down);
}

static int open_pad(App *a, int index) {
    close_pad(a);
    if (SDL_IsGameController(index)) a->gc = SDL_GameControllerOpen(index);
    a->js = a->gc ? SDL_GameControllerGetJoystick(a->gc) : SDL_JoystickOpen(index);
    if (!a->js) return 0;
    snprintf(a->pad_name, sizeof a->pad_name, "%.60s", SDL_JoystickName(a->js) ? SDL_JoystickName(a->js) : "?");
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(a->js), a->guid, sizeof a->guid);
    a->nbuttons = SDL_JoystickNumButtons(a->js);
    a->naxes = SDL_JoystickNumAxes(a->js);
    a->nhats = SDL_JoystickNumHats(a->js);
    for (int i = 0; i < a->naxes && i + 1 < MAX_A; i++) a->axis[i + 1] = SDL_JoystickGetAxis(a->js, i) / 32768.0f;
    return 1;
}

static void app_init(App *a) {
    memset(a, 0, sizeof *a);
    say(a, R3_ST_HINT, "Pick a check with the D-pad and A.");
    a->force = 1;
    snprintf(a->mode, sizeof a->mode, "?");
    snprintf(a->renderer, sizeof a->renderer, "?");
    snprintf(a->backlight, sizeof a->backlight, "?");
    snprintf(a->snd_card, sizeof a->snd_card, "?");
}

/* One SDL event. Every button, axis, hat and key is recorded on every page; the list pages also act on them. */
static void handle(App *a, const SDL_Event *e, Uint32 now) {
    switch (e->type) {
    case SDL_QUIT: a->quit = 1; break;
    case SDL_WINDOWEVENT: a->force = 1; break;
    case SDL_JOYDEVICEADDED:
        if (!a->js && open_pad(a, e->jdevice.which)) { char l[48]; snprintf(l, sizeof l, "pad added: %.36s", a->pad_name); add_log(a, l); }
        a->idle_since = now;
        break;
    case SDL_JOYDEVICEREMOVED:
        if (a->js && e->jdevice.which == SDL_JoystickInstanceID(a->js)) {
            close_pad(a);
            add_log(a, "pad removed");
            if (SDL_NumJoysticks() > 0) open_pad(a, 0);
        }
        break;
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP: {
        int isdown = e->type == SDL_JOYBUTTONDOWN, b = e->jbutton.button + 1;
        if (b < MAX_B) a->down[b] = (Uint8)isdown;
        if (isdown) rec_press(a, b, now);
        else rec_release(a, b, now);
        event(a, a->js && SDL_JoystickGetButton(a->js, e->jbutton.button), "btn %d %s", b, isdown ? "DOWN" : "UP");
        a->idle_since = now;
        if (isdown) {
            int ac = r3_pad_action(e->jbutton.button);
            if (ac != R3_NONE) act(a, ac, now);
        }
        break;
    }
    case SDL_JOYHATMOTION: {
        char h[8];
        a->hat = e->jhat.value;
        if (a->hat && a->hat < 16) a->hats_seen |= 1u << a->hat;
        hat_text(a->hat, h);
        event(a, a->hat != 0, "hat %d %s", e->jhat.hat + 1, h);
        a->idle_since = now;
        break;
    }
    case SDL_JOYAXISMOTION: {
        int i = e->jaxis.axis + 1;
        if (i >= MAX_A) break;
        float v = e->jaxis.value / 32768.0f;
        a->axis[i] = v;
        rec_axis(a, i, v);
        int z = v <= -ZONE ? -1 : (v >= ZONE ? 1 : 0);
        if (z != a->zone[i]) {
            char b[8];
            a->zone[i] = z;
            if (z) a->idle_since = now;
            event(a, z != 0, "axis %d %s", i, z ? fmt2(b, v) : "centre");
        }
        break;
    }
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        if (e->key.repeat) break;
        int isdown = e->type == SDL_KEYDOWN;
        SDL_Keycode k = e->key.keysym.sym;
        char name[24];
        snprintf(name, sizeof name, "%s", SDL_GetKeyName(k));
        for (char *p = name; *p; p++) *p = (*p == ' ') ? '_' : (char)tolower((unsigned char)*p);
        if (k == SDLK_VOLUMEDOWN || k == SDLK_AUDIOMUTE) a->k_vdown = isdown;
        if (k == SDLK_VOLUMEUP) a->k_vup = isdown;
        if (k == SDLK_POWER || k == SDLK_SLEEP) a->k_power = isdown;
        /* the names LÖVE uses, so both builds' reports agree */
        const char *lname = k == SDLK_VOLUMEDOWN ? "volumedown" : k == SDLK_VOLUMEUP ? "volumeup" : k == SDLK_POWER ? "power" : k == SDLK_SLEEP ? "sleep" : name;
        event(a, -1, "key %.14s %s", lname, isdown ? "DOWN" : "UP");
        a->idle_since = now;
        if (!isdown) break;
        rec_key(a, lname);
        if (a->page == P_INPUT) { if (k == SDLK_ESCAPE) go(a, P_HOME, now); break; }
        if (k == SDLK_h) act(a, R3_X, now);
        else if (k == SDLK_n) act(a, R3_Y, now);
        else { int ac = r3_key_action(k); if (ac != R3_NONE) act(a, ac, now); }
        break;
    }
    }
}

/* Once a frame: the exit chord (back to the list, or out from the list), the input page's idle timer. */
static void tick(App *a, Uint32 now) {
    int held = a->js && exit_held(a);
    if (!held) { a->latch = 0; a->chord_since = 0; }
    else if (!a->latch) {
        if (!a->chord_since) a->chord_since = now;
        if (now - a->chord_since >= EXIT_HOLD_MS) {
            a->latch = 1;
            a->chord_since = 0;
            if (a->page == P_HOME) a->quit = 1;
            else { go(a, P_HOME, now); say(a, R3_ST_HINT, "Back."); }
        }
    }
    if (a->page == P_INPUT && now - a->idle_since >= IDLE_LIMIT_MS) {
        go(a, P_HOME, now);
        say(a, R3_ST_WARN, "No input for 15 s: back to the list.");
    }
}

/* ---- self-test, screenshot, headless report ----------------------------------------------------------------------------- */

static int bad;
#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "io-checker selftest: %s\n", what); bad++; } } while (0)

static int screen_has(const R3Screen *s, const char *want) {
    char rows[R3_ROWS][R3_COLS + 1];
    r3_dump(s, rows);
    for (int i = 0; i < R3_ROWS; i++) if (strstr(rows[i], want)) return 1;
    return 0;
}

static SDL_Event joy(int type, int button) {
    SDL_Event e;
    SDL_zero(e);
    e.type = (Uint32)type;
    e.jbutton.button = (Uint8)(button - 1);
    return e;
}

static int selftest_logic(void) {
    App *a = calloc(1, sizeof(App));
    R3Screen s;
    if (!a) return 1;
    app_init(a);
    /* a pretend pad: the R36S's shape */
    a->nbuttons = 17; a->naxes = 4; a->nhats = 0;
    a->js = (SDL_Joystick *)1; /* only tested for non-NULL in this test; never passed to SDL */
    snprintf(a->pad_name, sizeof a->pad_name, "GO-Super Gamepad");
    snprintf(a->guid, sizeof a->guid, "1900bb3e4b4800000011000000010000");

    /* chatter: a press 20 ms after a release counts, one 400 ms after does not */
    rec_press(a, 2, 1000); rec_release(a, 2, 1100); rec_press(a, 2, 1120);
    rec_press(a, 3, 2000); rec_release(a, 3, 2100); rec_press(a, 3, 2500);
    CHECK(a->presses[2] == 2 && a->chatter[2] == 1 && a->chatter[3] == 0, "chatter");
    rec_axis(a, 1, 0.2f); rec_axis(a, 1, -0.95f); rec_axis(a, 1, 0.97f);
    CHECK(a->amin[1] < -0.94f && a->amax[1] > 0.96f, "axis range");
    int m[MAX_B];
    CHECK(missing(a, m) == 15 && m[0] == 1 && m[1] == 4, "missing buttons");
    /* frame times */
    frames_reset(a);
    for (int i = 0; i < WARMUP; i++) frames_add(a, 1.0);
    Frames f;
    CHECK(!frames_stats(a, &f), "no stats before a second of frames");
    for (int i = 0; i < 119; i++) frames_add(a, 1.0 / 60);
    frames_add(a, 3.0 / 60);
    CHECK(frames_stats(a, &f) && f.frames == 120 && f.late == 1 && f.fps > 58.9 && f.fps < 59.1, "frame stats");
    /* tones: left is silent on the right, fades from and to zero, the sweep stays in range */
    double l, r;
    tone_sample(&TONES[0], 1000, &l, &r);
    CHECK(l != 0 && r == 0, "left tone is left only");
    tone_sample(&TONES[0], 0, &l, &r);
    CHECK(l == 0, "fades in");
    tone_sample(&TONES[0], tone_frames(&TONES[0]), &l, &r);
    CHECK(fabs(l) < 1e-9, "fades out");
    for (int i = 0; i < tone_frames(&TONES[3]); i += 997) { tone_sample(&TONES[3], i, &l, &r); CHECK(fabs(l) <= 0.5 && l == r, "sweep in range"); }
    CHECK(tone_frames(&TONES[0]) == 66150, "tone length");

    /* the home page, the input page with buttons held, every other page */
    compose(a, &s, 0);
    CHECK(screen_has(&s, "IO Checker") && screen_has(&s, "Buttons and sticks") && screen_has(&s, "2/17 buttons"), "home page");
    CHECK(screen_has(&s, "59.0 fps") && screen_has(&s, "open Buttons and sticks"), "home values");
    a->page = P_INPUT; a->down[2] = a->down[9] = a->down[15] = 1; a->axis[1] = 0.9f; a->axis[2] = -0.6f;
    compose(a, &s, 0);
    CHECK(screen_has(&s, "[L2]") && screen_has(&s, "[SELECT]") && screen_has(&s, "L stick IN") && screen_has(&s, "Down  2 9 15"), "input page");
    CHECK(screen_has(&s, "Never 1 4 5") && screen_has(&s, "A1    +0.90  -0.95 +0.97"), "input numbers");
    CHECK(s.bg[5 * R3_COLS + 29] == LIT_BG && s.bg[4 * R3_COLS + 26] == R3_BLACK, "A lit, X not");
    CHECK(s.ch[10 * R3_COLS + 9] == 254, "stick dot");
    CHECK(r3_contrast(LIT_FG, LIT_BG) >= 4.5, "lit colours pass 4.5:1");
    compose(a, &s, 9000);
    CHECK(screen_has(&s, "back to the list in 6 s"), "idle countdown");
    a->page = P_VIDEO; compose(a, &s, 0);
    CHECK(screen_has(&s, "Motion and frame rate") && screen_has(&s, "Look for   8 bars"), "video page");
    a->page = P_SOUND; a->answers[0] = A_HEARD; a->answers[1] = A_SILENT; snprintf(a->snd_path, sizeof a->snd_path, "OFF");
    compose(a, &s, 0);
    CHECK(screen_has(&s, "SILENT") && screen_has(&s, "OFF: the codec is silent") && screen_has(&s, "A play  X heard  Y silent  B back"), "sound page");

    /* files and the report, in a temporary folder */
    char tmpl[] = "/tmp/io-checker-XXXXXX";
    const char *tmpdir = getenv("TMPDIR");
    char base[PATH_MAX];
    snprintf(base, sizeof base, "%s/io-checker-XXXXXX", tmpdir && *tmpdir ? tmpdir : "/tmp");
    char *root = mkdtemp(base);
    if (!root) root = mkdtemp(tmpl);
    CHECK(root != NULL, "temporary folder");
    if (root) {
        setenv("R3I6NOS_ROMS", root, 1);
        setenv("R3I6NOS_IO_MB", "1", 1);
        unsetenv("R3I6NOS_ROMS2");
        a->page = P_FILES;
        run_files(a);
        CHECK(a->nfiles >= 5 && !strcmp(a->files[1].status, "PASS") && !strcmp(a->files[3].status, "PASS"), "files check");
        compose(a, &s, 0);
        CHECK(screen_has(&s, "Games card: where") && screen_has(&s, "Games card: read back"), "files page");
        a->page = P_SYSTEM; system_rows(a); compose(a, &s, 0);
        CHECK(screen_has(&s, "Kernel") && screen_has(&s, "GUID"), "system page");
        SDL_Joystick *fake = a->js;
        a->js = NULL; /* the report asks SDL about real pads; this pretend one must not reach it */
        write_report(a, "selftest");
        a->js = fake;
        char path[PATH_MAX + 64], line[512], last[512] = "";
        snprintf(path, sizeof path, "%s/IO_Checker/r36s-report-native.txt", root);
        FILE *fp = fopen(path, "r");
        CHECK(fp != NULL, "report written");
        int sections = 0;
        while (fp && fgets(line, sizeof line, fp)) {
            if (!strncmp(line, "== ", 3)) sections++;
            snprintf(last, sizeof last, "%s", line);
        }
        if (fp) fclose(fp);
        CHECK(!strcmp(last, "end\n") && sections >= 6, "report has its sections and ends with end");
        char cmd[PATH_MAX + 32];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
        if (system(cmd) != 0) fprintf(stderr, "io-checker selftest: could not remove %s\n", root);
    }
    /* navigation through the real event handler: pages, patterns, B back, the chord and the idle return */
    app_init(a);
    a->nbuttons = 17; a->naxes = 4;
    SDL_Event e;
#define PRESS(btn) do { e = joy(SDL_JOYBUTTONDOWN, btn); handle(a, &e, t); e = joy(SDL_JOYBUTTONUP, btn); handle(a, &e, t); t += 100; } while (0)
    Uint32 t = 1000;
    PRESS(L_DOWN); PRESS(L_A);
    CHECK(a->page == P_VIDEO, "D-pad down and A open Video");
    PRESS(L_A); CHECK(a->page == P_PATTERN && a->sel[P_VIDEO] == 0, "A shows the first pattern");
    PRESS(L_RIGHT); PRESS(L_RIGHT); CHECK(a->sel[P_VIDEO] == 2 && a->pattern_seen[2], "right turns the pattern");
    PRESS(L_LEFT); PRESS(L_LEFT); PRESS(L_LEFT); CHECK(a->sel[P_VIDEO] == NPAT - 1, "left wraps to the last pattern");
    PRESS(L_B); CHECK(a->page == P_VIDEO, "B leaves the pattern");
    PRESS(L_B); CHECK(a->page == P_HOME, "B goes home");
    PRESS(L_UP); PRESS(L_A); CHECK(a->page == P_INPUT, "Buttons and sticks");
    for (int b = 1; b <= 17; b++) PRESS(b);
    CHECK(a->page == P_INPUT && missing(a, m) == 0 && !a->quit, "every button recorded; none of them navigates here");
    a->js = (SDL_Joystick *)1; /* tick() only asks down(); this pointer never reaches SDL */
    a->down[L_SELECT] = a->down[L_START] = 1;
    tick(a, t); tick(a, t + 1001);
    CHECK(a->page == P_HOME && !a->quit, "Select+Start held a second: back to the list");
    tick(a, t + 2500); tick(a, t + 4000);
    CHECK(!a->quit, "a held chord fires once");
    a->down[L_SELECT] = a->down[L_START] = 0;
    tick(a, t + 4100);
    a->down[L_SELECT] = a->down[L_START] = 1;
    tick(a, t + 4200); tick(a, t + 5300);
    CHECK(a->quit, "Select+Start on the list quits");
    a->down[L_SELECT] = a->down[L_START] = 0;
    a->quit = 0;
    go(a, P_INPUT, t);
    tick(a, t + IDLE_LIMIT_MS + 1);
    CHECK(a->page == P_HOME, "15 s idle on the input page goes back to the list");
    a->js = NULL;
    PRESS(L_DOWN); PRESS(L_DOWN); PRESS(L_A);
    CHECK(a->page == P_SOUND, "Sound");
    PRESS(L_X); CHECK(a->answers[0] == A_HEARD, "X marks heard");
    PRESS(L_DOWN); PRESS(L_Y); CHECK(a->answers[1] == A_SILENT, "Y marks silent");
    PRESS(L_B); PRESS(L_B); CHECK(a->quit, "B on the list quits");
#undef PRESS
    free(a);
    return bad;
}

static int save_screenshot(const char *path, const char *page) {
    App *a = calloc(1, sizeof(App));
    if (!a) return 1;
    app_init(a);
    a->nbuttons = 17; a->naxes = 4;
    a->js = (SDL_Joystick *)1; /* drawing only */
    snprintf(a->pad_name, sizeof a->pad_name, "Demo pad (not real)");
    snprintf(a->guid, sizeof a->guid, "1900bb3e4b4800000011000000010000");
    a->down[L_A] = a->down[L_UP] = a->down[L_L3] = 1;
    a->axis[1] = 0.67f; a->axis[2] = -0.27f; a->axis[4] = 0.52f;
    rec_press(a, L_A, 100); rec_press(a, L_UP, 200); rec_press(a, L_B, 300); rec_axis(a, 1, -0.98f); rec_axis(a, 1, 1.0f);
    rec_key(a, "volumeup");
    event(a, 1, "btn 9 DOWN"); event(a, 0, "btn 9 UP"); event(a, 1, "btn 15 DOWN"); event(a, -1, "key volumeup DOWN");
    snprintf(a->snd_card, sizeof a->snd_card, "rockchip,rk817-codec");
    snprintf(a->snd_path, sizeof a->snd_path, "SPK");
    snprintf(a->snd_hp, sizeof a->snd_hp, "out");
    snprintf(a->snd_jack, sizeof a->snd_jack, "rk817-sound Headphone Jack");
    a->answers[0] = A_HEARD; a->answers[1] = A_PLAYED;
    const char *names[] = {"home", "input", "video", "pattern", "sound", "files", "system"};
    for (int i = 0; i < P_COUNT; i++) if (page && !strcmp(page, names[i])) a->page = i;
    if (a->page == P_SYSTEM) system_rows(a);
    if (a->page == P_FILES) run_files(a);
    R3Screen s;
    compose(a, &s, 0);
    Uint32 *px = malloc((size_t)R3_W * R3_H * sizeof(Uint32));
    SDL_Surface *surf = px ? SDL_CreateRGBSurfaceWithFormatFrom(px, R3_W, R3_H, 32, R3_W * 4, SDL_PIXELFORMAT_ARGB8888) : NULL;
    int rc = 1;
    if (surf) {
        r3_render(&s, px);
        rc = SDL_SaveBMP(surf, path);
        SDL_FreeSurface(surf);
    }
    free(px);
    free(a);
    return rc;
}

static int headless_report(void) {
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "io-checker: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    App *a = calloc(1, sizeof(App));
    if (!a) return 1;
    app_init(a);
    if (SDL_NumJoysticks() > 0) open_pad(a, 0);
    video_info(a, NULL, NULL);
    write_report(a, "io-checker-native --report (no window: nothing was pressed, shown or played)");
    printf("%s\n", a->reply);
    int ok = a->report_written;
    close_pad(a);
    if (a->dev) SDL_CloseAudioDevice(a->dev);
    free(a);
    SDL_Quit();
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    int selftest = 0;
    const char *shot = NULL, *shot_page = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) selftest = 1;
        else if (!strcmp(argv[i], "--report")) return headless_report();
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) { shot = argv[++i]; if (i + 1 < argc && argv[i + 1][0] != '-') shot_page = argv[++i]; }
        else if (!strcmp(argv[i], "--version")) { puts("io-checker-native " VERSION); return 0; }
        else { fprintf(stderr, "usage: %s [--report | --selftest | --screenshot out.bmp [page] | --version]\n", argv[0]); return 2; }
    }
    if (shot) return save_screenshot(shot, shot_page);
    if (selftest && selftest_logic() != 0) { fprintf(stderr, "io-checker: %d selftest check(s) failed\n", bad); return 1; }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "io-checker: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_ShowCursor(SDL_DISABLE);
    SDL_Window *win = SDL_CreateWindow("IO Checker", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, R3_W, R3_H,
                                       selftest ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!win) { fprintf(stderr, "io-checker: window: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    SDL_Renderer *r = SDL_CreateRenderer(win, -1, selftest ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!r) r = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE); /* no GPU: the software path still draws */
    if (!r) { fprintf(stderr, "io-checker: renderer: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    R3Video video;
    if (!r3_video_init(&video, r)) { fprintf(stderr, "io-checker: texture: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

    App *a = calloc(1, sizeof(App));
    if (!a) return 1;
    app_init(a);
    a->idle_since = SDL_GetTicks();
    if (SDL_NumJoysticks() > 0) open_pad(a, 0);
    video_info(a, win, r);
    sound_status(a);
    R3Screen screen;

    if (selftest) {
        compose(a, &screen, SDL_GetTicks());
        r3_video_draw(&video, r, &screen, 1);
        SDL_version v;
        SDL_GetVersion(&v);
        printf("R3I6NOS_SMOKE ok io-checker-native=%s sdl=%d.%d.%d joysticks=%d renderer=%s\n", VERSION, v.major, v.minor, v.patch,
               SDL_NumJoysticks(), SDL_GetCurrentVideoDriver());
    }

    Uint64 freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    while (!selftest && !a->quit) {
        Uint32 now = SDL_GetTicks();
        SDL_Event e;
        while (SDL_PollEvent(&e)) handle(a, &e, now);
        tick(a, now);
        if (a->page == P_PATTERN) {
            draw_pattern(a, r); /* vsync paces this loop */
            Uint64 t = SDL_GetPerformanceCounter();
            if (a->sel[P_VIDEO] == PAT_MOTION) frames_add(a, (double)(t - last) / (double)freq);
            last = t;
            continue;
        }
        compose(a, &screen, now);
        r3_video_draw(&video, r, &screen, a->force);
        a->force = 0;
        if (a->pending) {
            int job = a->pending;
            a->pending = 0;
            if (job == H_FILES) run_files(a);
            else write_report(a, "Device report (native)");
            continue;
        }
        SDL_Delay(16);
        last = SDL_GetPerformanceCounter();
    }
    close_pad(a);
    if (a->dev) SDL_CloseAudioDevice(a->dev);
    if (a->checker) SDL_DestroyTexture(a->checker);
    free(a);
    r3_video_free(&video);
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

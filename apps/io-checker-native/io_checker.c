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
#include "r3i6nos_toast.h"
#include "usb_io.h"

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

enum { P_HOME, P_INPUT, P_VIDEO, P_PATTERN, P_SOUND, P_FILES, P_SYSTEM, P_USB, P_USBSUB, P_COUNT };
enum { H_INPUT, H_VIDEO, H_SOUND, H_FILES, H_USB, H_SYSTEM, H_REPORT, H_QUIT, H_COUNT };
static const char *HOME_LABEL[H_COUNT] = {"Buttons and sticks", "Video", "Sound", "Files", "USB and OTG", "System", "Device report", "Quit"};
/* The checks behind "USB and OTG": what the OTG port can bring (usb_io.h). */
enum { U_DEVICES, U_VIDEO, U_MIDI, U_STORAGE, U_NETWORK, U_KEYBOARD, U_MOUSE, U_PADS, U_COUNT };
static const char *HOME_ABOUT[H_COUNT][4] = {
    {"Every button, both sticks, the D-pad, and the power and", "volume keys, as the pad reports them. Press each one:",
     "the list of buttons never pressed shrinks to nothing.", "Hold SELECT+START for a second to come back."},
    {"Full-screen test patterns: colour bars, solid colours for", "stuck pixels, ramps for colour depth, a pixel grid for",
     "scaling and edges, and a moving bar that measures the", "frame rate."},
    {"Test tones on the left, the right and both channels, a", "low tone, a sweep, a quiet one. After each, press X if heard",
     "it or Y if not: the answers go in the device report.", "Shows the speaker path the system set."},
    {"On each card: makes its IO_Checker folder, writes,", "renames over, reads back and deletes a file, then times",
     "writing and reading 8 MB and shows the free space.", "Nothing else on the card is touched."},
    {"What the OTG port brings: every USB device and what it is,", "video out (HDMI or DisplayLink docks), MIDI in and out,",
     "USB storage, Ethernet, a keyboard, a mouse and game", "controllers. Plug one in and watch it appear."},
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
    /* 1 kHz, not 440 Hz: on the unit every 440 Hz tone was marked silent from the speaker and the sweep heard (2026-11-09,
     * docs/hardware-findings.md); a small speaker gives little below about 500 Hz. "low" keeps 440 Hz to measure that. */
    {"left", "Left channel, 1 kHz", 1000, 0, 0, 1, 0, 0.5, 1.5},
    {"right", "Right channel, 1 kHz", 1000, 0, 0, 0, 1, 0.5, 1.5},
    {"both", "Both channels, 1 kHz", 1000, 0, 0, 1, 1, 0.5, 1.5},
    {"low", "Low, 440 Hz", 440, 0, 0, 1, 1, 0.5, 1.5},
    {"sweep", "Sweep, 100 Hz to 8 kHz", 0, 100, 8000, 1, 1, 0.5, 4},
    {"quiet", "Quiet, 1 kHz at 10%", 1000, 0, 0, 1, 1, 0.1, 1.5},
};
#define NTONE ((int)(sizeof TONES / sizeof TONES[0]))
enum { A_NONE, A_PLAYED, A_HEARD, A_SILENT };

typedef struct { char status[5], card[16], item[48], value[96]; } Row;

/* A game controller on USB (not the built-in pad): opened while the Game controllers page is up, its state kept after. */
typedef struct {
    SDL_Joystick *js;
    SDL_JoystickID id;
    char name[64];
    int nb, na, nh;
    Uint8 down[MAX_B], seen[MAX_B], moved[MAX_A];
    float axis[MAX_A];
    Uint8 hat;
} ExtPad;

typedef struct {
    /* the pad */
    SDL_Joystick *js;
    SDL_JoystickID js_id;           /* the built-in pad's instance id: events from any other pad belong to the USB page */
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
    Uint32 audio_close_at;          /* when to close the device after a tone (an open one keeps a thread waking ~40 times a second: docs/POWER.md) */
    SDL_AudioSpec have;
    char audio_dev[128];
    int audio_error, answers[NTONE];
    char snd_card[96], snd_path[16], snd_jack[64], snd_hp[16];
    /* files, system, report */
    Row files[56];
    int nfiles, files_run;
    R3Item sys[28];
    int nsys;
    char report_file[96];
    int report_written;
    R3Toast toast;                  /* the hot-plug toast over the top bar (docs/HOTPLUG.md) */
    /* USB and OTG (usb_io.h): the last scan, and the live checks */
    int usb_sub, usb_scanned;
    Uint32 usb_scan_at;
    UioDev usb_dev[UIO_MAX];    int n_dev;
    UioConn usb_conn[UIO_MAX];  int n_conn;
    UioDisk usb_disk[8];        int n_disk;
    UioNet usb_net[8];          int n_net;
    UioMidi usb_midi[8];        int n_midi;
    UioInput usb_in[16];        int n_in;
    int displays;               /* SDL's display count, -1 while SDL's video is not up (--report) */
    R3Item usb_rows[48];
    char usb_detail[48][120];
    int n_rows;
    int midi_fd, midi_idx;      /* the port open for the live check, -1 when none */
    unsigned midi_in_total;
    int midi_in_msgs, midi_out_msgs;
    char midi_log[4][56], midi_err[64];
    Uint32 midi_off_at;
    Uint8 kb_seen[SDL_NUM_SCANCODES];
    int kb_n;
    char kb_log[6][40];
    int m_x, m_y, m_moves, m_wheel;
    unsigned m_buttons;         /* bit n: button n was pressed */
    ExtPad ext[4];
    int n_ext;
    struct Loop *loop;              /* what a pass of the main loop draws with; NULL with no window (--report) */
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

#define AUDIO_TAIL_MS 500   /* after the tone's last sample: the device's own buffer plays out */

/* Close the output. SDL's audio thread feeds an open device silence and wakes about 40 times a second until it is closed, so it is only open while a tone plays. */
static void audio_close(App *a) {
    if (a->dev) SDL_CloseAudioDevice(a->dev);
    a->dev = 0;
    a->audio_close_at = 0;
    /* Closing the device is not enough: with the subsystem up SDL's ALSA hotplug thread keeps waking 10 times a second (the unit's first power
     * report, 2026-11-12: SDLHotplugALSA 10.1/s of the idle 25.3). audio_open starts it again for the next tone. */
    if (SDL_WasInit(SDL_INIT_AUDIO)) SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

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
    a->audio_close_at = SDL_GetTicks() + (Uint32)(t->secs * 1000) + AUDIO_TAIL_MS;
    if (!a->audio_close_at) a->audio_close_at = 1;
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
    UioDisk disks[8];
    int nd = uio_usb_disks(disks, 8);
    for (int i = 0; i < nd; i++) {
        if (!disks[i].mount[0]) continue;
        char label[24];
        snprintf(label, sizeof label, "USB %.15s", disks[i].dev);
        card_check(a, label, disks[i].mount);
    }
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

static void power_windows(App *a, R3Report *r);
static void report_usb(App *a, R3Report *r);

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
    audio_close(a);     /* the check only needed it to open */
    char key[64];
    for (int i = 0; i < NTONE; i++) {
        snprintf(key, sizeof key, "heard %s", TONES[i].label);
        switch (a->answers[i]) {
        case A_HEARD: r3_report_line(&r, R3_PASS, key, "yes"); break;
        case A_SILENT:
            /* the right channel alone, on the speaker path with the left and both heard: the unit has one speaker, so this is not a fault until
             * headphones say so (the unit's reports of 2026-11-09 and 2026-11-12: silent with the headphones out) */
            if (TONES[i].left == 0 && TONES[i].right > 0 && !strcmp(a->snd_hp, "out") && a->answers[0] == A_HEARD)
                r3_report_line(&r, R3_INFO, key, "no (silent) with the headphones out: the speaker may play one channel; repeat with headphones in to tell a dead channel");
            else r3_report_line(&r, R3_FAIL, key, "no (silent)");
            break;
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
    report_usb(a, &r);
    r3_report_memory(&r);
    power_windows(a, &r);
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
    if (id == H_USB && a->usb_scanned) snprintf(out, cap, "%d device%s", a->n_dev, a->n_dev == 1 ? "" : "s");
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
        key_cell(s, 1, 2, "L1", down(a, L_L1)); key_cell(s, 6, 2, "L2", down(a, L_L2));
        key_cell(s, 23, 2, "R2", down(a, L_R2)); key_cell(s, 28, 2, "R1", down(a, L_R1));
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
    static R3Item items[56];
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

/* ---- USB and OTG (usb_io.h; docs/IO-CHECKER.md) ---------------------------------------------------------------------------------- */

/* The reply line when a USB page opens. */
static const char *const USB_HINT[U_COUNT] = {
    "Every device on the USB bus. A scans again.", "Video outputs: plug in a dock or an adapter.", "Up/Down picks a port. Play a key, or press A to send C4.",
    "USB drives. A tests a mounted one.", "Network adapters: plug one in, then a cable.", "Type on the USB keyboard. Esc goes back.",
    "Move the mouse, click, turn the wheel.", "Press the buttons of a USB controller."};

static const char *USB_LABEL[U_COUNT] = {"Devices", "Video out", "MIDI", "Storage", "Network", "Keyboard", "Mouse", "Game controllers"};
static const char *USB_ABOUT[U_COUNT][3] = {
    {"Every device on the USB bus, what it is (from its", "interfaces) and the driver the kernel gave it. A device", "with no driver is a FAIL in the report."},
    {"The video outputs the kernel knows: this unit's own screen,", "and an HDMI or DisplayLink dock when one is plugged in:", "connected or not, and its first mode."},
    {"USB MIDI ports. Pick one: what comes in is shown, and A", "sends middle C (note on, then off). Play a key, or loop", "a cable from the out to the in."},
    {"USB drives and sticks: model, size, and whether the system", "has mounted them. A tests a mounted one like a card:", "write, rename, read, delete and the speed."},
    {"Ethernet and Wi-Fi adapters: link, speed and address.", "Plug in an adapter and a cable and watch the link come up.", NULL},
    {"Type on a USB keyboard: each different key is counted and", "the last six are shown. Esc goes back.", NULL},
    {"Move a USB mouse, press its buttons, turn the wheel.", "B goes back.", NULL},
    {"Other game controllers on USB: shape, and live buttons and", "axes. The built-in pad is on Buttons and sticks.", NULL},
};

/* -- MIDI: the port selected in the list is open (never blocking) while the page is up -- */

static void midi_close(App *a) {
    if (a->midi_fd >= 0) close(a->midi_fd);
    a->midi_fd = a->midi_idx = -1;
    a->midi_off_at = 0;
}

static void midi_open_sel(App *a) {
    int want = a->sel[P_USBSUB];
    if (want < 0 || want >= a->n_midi) return;      /* a log row is selected: keep the port that is open */
    if (a->midi_idx == want && a->midi_fd >= 0) return;
    midi_close(a);
    a->midi_err[0] = 0;
    const char *node = a->usb_midi[want].node;
    int fd = open(node, O_RDWR | O_NONBLOCK);
    if (fd < 0) fd = open(node, O_RDONLY | O_NONBLOCK);
    if (fd < 0) fd = open(node, O_WRONLY | O_NONBLOCK);
    a->midi_idx = want;
    if (fd < 0) { snprintf(a->midi_err, sizeof a->midi_err, "%s", strerror(errno)); return; }
    a->midi_fd = fd;
}

static void midi_log_add(App *a, const char *dir, const unsigned char *b, int n) {
    char t[48];
    uio_midi_text(b, n, t, sizeof t);
    memmove(a->midi_log[1], a->midi_log[0], sizeof a->midi_log[0] * 3);
    snprintf(a->midi_log[0], sizeof a->midi_log[0], "%s %s", dir, t);
}

/* What came in on the open port: counted, and each channel message put in words. */
static void midi_poll(App *a) {
    if (a->midi_fd < 0) return;
    unsigned char buf[128];
    ssize_t n = read(a->midi_fd, buf, sizeof buf);
    if (n <= 0) return;
    a->midi_in_total += (unsigned)n;
    for (ssize_t i = 0; i < n;) {
        if (buf[i] < 0x80 || buf[i] >= 0xF0) { i++; continue; }     /* data without a status, clock, system messages: counted, not shown */
        int len = ((buf[i] & 0xF0) == 0xC0 || (buf[i] & 0xF0) == 0xD0) ? 2 : 3;
        if (i + len > n) break;
        midi_log_add(a, "in ", buf + i, len);
        a->midi_in_msgs++;
        i += len;
    }
}

/* A sends middle C: note on now, note off 300 ms later (usb_tick). */
static void midi_send(App *a, Uint32 now) {
    if (a->midi_fd < 0) {
        say(a, R3_ST_WARN, "No MIDI port is open%s%s", a->midi_err[0] ? ": " : ".", a->midi_err);
        return;
    }
    static const unsigned char on[3] = {0x90, 60, 100};
    if (write(a->midi_fd, on, 3) != 3) { say(a, R3_ST_ERROR, "Could not send: %s", strerror(errno)); return; }
    midi_log_add(a, "out", on, 3);
    a->midi_out_msgs++;
    a->midi_off_at = now + 300;
    if (!a->midi_off_at) a->midi_off_at = 1;
    say(a, R3_ST_OK, "Sent middle C (note on, then note off).");
}

/* -- game controllers on USB: opened while their page is up; events carry the pad's instance id -- */

static void ext_close_all(App *a) {
    for (int i = 0; i < a->n_ext; i++)
        if (a->ext[i].js) { SDL_JoystickClose(a->ext[i].js); a->ext[i].js = NULL; }
}

static void ext_open_all(App *a) {
    ExtPad old[4];
    int nold = a->n_ext;
    memcpy(old, a->ext, sizeof old);
    ext_close_all(a);
    memset(a->ext, 0, sizeof a->ext);
    a->n_ext = 0;
    if (!SDL_WasInit(SDL_INIT_JOYSTICK)) return;
    for (int i = 0; i < SDL_NumJoysticks() && a->n_ext < 4; i++) {
        if (a->js && SDL_JoystickGetDeviceInstanceID(i) == a->js_id) continue;     /* the built-in pad is the Buttons and sticks page's */
        SDL_Joystick *j = SDL_JoystickOpen(i);
        if (!j) continue;
        ExtPad *p = &a->ext[a->n_ext++];
        p->js = j;
        p->id = SDL_JoystickInstanceID(j);
        snprintf(p->name, sizeof p->name, "%.60s", SDL_JoystickName(j) ? SDL_JoystickName(j) : "?");
        p->nb = SDL_JoystickNumButtons(j);
        p->na = SDL_JoystickNumAxes(j);
        p->nh = SDL_JoystickNumHats(j);
        for (int k = 0; k < p->na && k + 1 < MAX_A; k++) p->axis[k + 1] = SDL_JoystickGetAxis(j, k) / 32768.0f;
        for (int k = 0; k < nold; k++)      /* seen before on an earlier visit: keep what it pressed */
            if (!strcmp(old[k].name, p->name)) { memcpy(p->seen, old[k].seen, sizeof p->seen); memcpy(p->moved, old[k].moved, sizeof p->moved); break; }
    }
}

static void ext_event(App *a, const SDL_Event *e) {
    SDL_JoystickID id = e->type == SDL_JOYAXISMOTION ? e->jaxis.which : e->type == SDL_JOYHATMOTION ? e->jhat.which : e->jbutton.which;
    ExtPad *p = NULL;
    for (int i = 0; i < a->n_ext; i++) if (a->ext[i].js && a->ext[i].id == id) p = &a->ext[i];
    if (!p) return;
    if (e->type == SDL_JOYBUTTONDOWN || e->type == SDL_JOYBUTTONUP) {
        int b = e->jbutton.button + 1;
        if (b < MAX_B) { p->down[b] = e->type == SDL_JOYBUTTONDOWN; if (p->down[b]) p->seen[b] = 1; }
    } else if (e->type == SDL_JOYHATMOTION) {
        p->hat = e->jhat.value;
    } else {
        int i = e->jaxis.axis + 1;
        if (i < MAX_A) { p->axis[i] = e->jaxis.value / 32768.0f; if (fabsf(p->axis[i]) > 0.5f) p->moved[i] = 1; }
    }
}

/* -- a keyboard: each different key counted, the last six shown -- */

static void kb_press(App *a, int scancode, const char *name) {
    if (scancode > 0 && scancode < SDL_NUM_SCANCODES && !a->kb_seen[scancode]) { a->kb_seen[scancode] = 1; a->kb_n++; }
    memmove(a->kb_log[1], a->kb_log[0], sizeof a->kb_log[0] * 5);
    snprintf(a->kb_log[0], sizeof a->kb_log[0], "%s", name);
}

/* -- the scan: every list, again (on entering a page, every 2 s while one is up, and on A) -- */

static void usb_scan(App *a, Uint32 now) {
    a->n_dev = uio_usb_devices(a->usb_dev, UIO_MAX);
    a->n_conn = uio_connectors(a->usb_conn, UIO_MAX);
    a->n_disk = uio_usb_disks(a->usb_disk, 8);
    a->n_net = uio_nets(a->usb_net, 8);
    a->n_midi = uio_midi_ports(a->usb_midi, 8);
    a->n_in = uio_inputs(a->usb_in, 16);
    a->displays = SDL_WasInit(SDL_INIT_VIDEO) ? SDL_GetNumVideoDisplays() : -1;
    a->usb_scanned = 1;
    a->usb_scan_at = now;
}

/* Once a pass: the 2 s rescan on a USB page, the MIDI port read, a note ended. */
static void usb_tick(App *a, Uint32 now) {
    if (a->page != P_USB && a->page != P_USBSUB) return;
    if (now - a->usb_scan_at >= 2000) {
        usb_scan(a, now);
        if (a->page == P_USBSUB && a->usb_sub == U_PADS && a->n_ext < SDL_NumJoysticks() - (a->js ? 1 : 0)) ext_open_all(a);   /* one was plugged in */
        if (a->page == P_USBSUB && a->usb_sub == U_MIDI) midi_open_sel(a);
    }
    if (a->page == P_USBSUB && a->usb_sub == U_MIDI) {
        midi_poll(a);
        if (a->midi_off_at && now >= a->midi_off_at && a->midi_fd >= 0) {
            static const unsigned char off[3] = {0x80, 60, 0};
            if (write(a->midi_fd, off, 3) == 3) midi_log_add(a, "out", off, 3);
            a->midi_off_at = 0;
        }
    }
}

/* -- the rows of a page -- */

static void urow(App *a, const char *label, const char *value, const char *detail) {
    if (a->n_rows >= (int)(sizeof a->usb_rows / sizeof a->usb_rows[0])) return;
    R3Item *it = &a->usb_rows[a->n_rows];
    snprintf(it->label, sizeof it->label, "%s", label);
    snprintf(it->value, sizeof it->value, "%s", value ? value : "");
    snprintf(a->usb_detail[a->n_rows], sizeof a->usb_detail[0], "%s", detail ? detail : "");
    a->n_rows++;
}

/* The plain-words reason a Billboard device gives: a USB-C dock asked for video (DisplayPort alternate mode) and the OTG port, USB 2.0 data only, could not give it. */
#define BILLBOARD_WHY "a USB-C dock is asking for video (DisplayPort) and this port cannot carry it: it is USB 2.0 data only, so a monitor on the dock's HDMI stays dark"

static int usb_has_billboard(const App *a) {
    for (int i = 0; i < a->n_dev; i++) if (strstr(a->usb_dev[i].kinds, "billboard")) return 1;
    return 0;
}

static void usb_build(App *a) {
    a->n_rows = 0;
    char l[240], v[240], d[300];
    switch (a->usb_sub) {
    case U_DEVICES:
        for (int i = 0; i < a->n_dev; i++) {
            const UioDev *u = &a->usb_dev[i];
            snprintf(l, sizeof l, "%s %s", u->name, u->product);
            snprintf(v, sizeof v, "%s%s", u->kinds, u->unbound && !u->hub ? " NO DRIVER" : "");
            snprintf(d, sizeof d, "%s  %s Mb/s  %s", u->vidpid, u->speed[0] ? u->speed : "?", u->drivers[0] ? u->drivers : "no driver bound");
            if (strstr(u->kinds, "billboard")) snprintf(d, sizeof d, "Billboard: %s", BILLBOARD_WHY);
            urow(a, l, v, d);
        }
        break;
    case U_VIDEO:
        if (a->displays >= 0) {
            snprintf(v, sizeof v, "%d", a->displays);
            urow(a, "Displays SDL can draw to now", v, "SDL's own count; this app draws to one");
        }
        for (int i = 0; i < a->n_conn; i++) {
            const UioConn *c = &a->usb_conn[i];
            snprintf(l, sizeof l, "%s%s", c->name, c->internal ? " (this unit's screen)" : c->usb ? " (USB adapter)" : "");
            snprintf(v, sizeof v, "%s%s%s", c->status, c->mode[0] ? " " : "", c->mode);
            snprintf(d, sizeof d, "%s: %s%s", c->name, c->status, !c->internal && !strcmp(c->status, "connected") ? ", an external display is attached" : "");
            urow(a, l, v, d);
        }
        if (usb_has_billboard(a)) urow(a, "A dock asked for video", "Billboard", "Billboard: " BILLBOARD_WHY);
        break;
    case U_MIDI:
        for (int i = 0; i < a->n_midi; i++) {
            const UioMidi *m = &a->usb_midi[i];
            snprintf(l, sizeof l, "%s  card %d device %d%s", m->name, m->card, m->device, m->usb ? " (USB)" : "");
            if (i == a->midi_idx && a->midi_fd >= 0) snprintf(v, sizeof v, "open, %u bytes in", a->midi_in_total);
            else if (i == a->midi_idx) snprintf(v, sizeof v, "cannot open: %s", a->midi_err);
            else v[0] = 0;
            urow(a, l, v, m->node);
        }
        if (a->n_midi) {
            for (int i = 0; i < 4; i++)
                if (a->midi_log[i][0]) { snprintf(l, sizeof l, "  %s", a->midi_log[i]); urow(a, l, "", "The last messages, newest first"); }
            snprintf(v, sizeof v, "%d out, %d in", a->midi_out_msgs, a->midi_in_msgs);
            urow(a, "Messages", v, "A sends middle C; what comes in is listed above");
        }
        break;
    case U_STORAGE:
        for (int i = 0; i < a->n_disk; i++) {
            const UioDisk *k = &a->usb_disk[i];
            snprintf(l, sizeof l, "%s %s", k->dev, k->model);
            snprintf(v, sizeof v, "%.1f GB", (double)k->bytes / 1e9);
            urow(a, l, v, "A tests a mounted one: write, rename, read, speed");
            if (k->mount[0]) {
                snprintf(l, sizeof l, "  mounted at %s", k->mount);
                urow(a, l, k->fstype, "Mounted: A tests it like a card");
            } else {
                urow(a, "  not mounted", "", "Nothing mounts USB storage yet (a mount needs root)");
            }
        }
        break;
    case U_NETWORK:
        for (int i = 0; i < a->n_net; i++) {
            const UioNet *n = &a->usb_net[i];
            snprintf(l, sizeof l, "%s %s%s", n->name, n->wifi ? "Wi-Fi" : "Ethernet", n->usb ? " (USB)" : "");
            int up = n->carrier == 1 || !strcmp(n->state, "up");
            if (up && n->speed > 0) snprintf(v, sizeof v, "up %d Mb/s%s%s", n->speed, n->ip[0] ? " " : "", n->ip);
            else if (up) snprintf(v, sizeof v, "up%s%s", n->ip[0] ? " " : "", n->ip);
            else snprintf(v, sizeof v, "%s", n->state);
            snprintf(d, sizeof d, "%s  %llu bytes in, %llu out", n->mac[0] ? n->mac : "no address", n->rx, n->tx);
            urow(a, l, v, d);
        }
        break;
    case U_KEYBOARD: {
        int found = 0;
        for (int i = 0; i < a->n_in; i++)
            if (a->usb_in[i].keyboard) { found++; urow(a, a->usb_in[i].name, a->usb_in[i].usb ? "USB" : "built in", "A keyboard the kernel made an input device for"); }
        if (!found) urow(a, "No keyboard found", "plug one in", "A USB keyboard appears here; its keys are counted below");
        snprintf(v, sizeof v, "%d", a->kb_n);
        urow(a, "Different keys pressed", v, "Each key counts once, however often it is pressed");
        SDL_Keymod m = SDL_GetModState();
        snprintf(v, sizeof v, "%s%s%s%s%s", m & KMOD_SHIFT ? "Shift " : "", m & KMOD_CTRL ? "Ctrl " : "", m & KMOD_ALT ? "Alt " : "", m & KMOD_GUI ? "Gui " : "",
                 m & (KMOD_SHIFT | KMOD_CTRL | KMOD_ALT | KMOD_GUI) ? "" : "none");
        urow(a, "Modifiers held", v, "Hold Shift, Ctrl or Alt to see them");
        for (int i = 0; i < 6; i++)
            if (a->kb_log[i][0]) { snprintf(l, sizeof l, "  %s", a->kb_log[i]); urow(a, l, "", "The last keys, newest first"); }
        break;
    }
    case U_MOUSE: {
        int found = 0;
        for (int i = 0; i < a->n_in; i++)
            if (a->usb_in[i].mouse) { found++; urow(a, a->usb_in[i].name, a->usb_in[i].usb ? "USB" : "built in", "A mouse the kernel made an input device for"); }
        if (!found) urow(a, "No mouse found", "plug one in", "A USB mouse appears here; its movement is counted below");
        snprintf(v, sizeof v, "x %d  y %d  (%d moves)", a->m_x, a->m_y, a->m_moves);
        urow(a, "Pointer moved", v, "Relative movement since this page opened");
        static const char *const bn[6] = {"", "left", "middle", "right", "x1", "x2"};
        v[0] = 0;
        for (int b = 1; b <= 5; b++) if (a->m_buttons & (1u << b)) { size_t u = strlen(v); snprintf(v + u, sizeof v - u, "%s%s", u ? " " : "", bn[b]); }
        urow(a, "Buttons pressed", v[0] ? v : "none yet", "Press each button once");
        snprintf(v, sizeof v, "%d", a->m_wheel);
        urow(a, "Wheel", v, "Turn it up and down; up counts positive");
        break;
    }
    case U_PADS:
        for (int i = 0; i < a->n_ext; i++) {
            const ExtPad *p = &a->ext[i];
            snprintf(v, sizeof v, "%d btn %d axes %d hats", p->nb, p->na, p->nh);
            urow(a, p->name, v, "A game controller on USB: press its buttons");
            char held[96] = "";
            int seen = 0;
            for (int b = 1; b <= p->nb && b < MAX_B; b++) {
                seen += p->seen[b];
                if (p->down[b]) { size_t u = strlen(held); snprintf(held + u, sizeof held - u, "%s%d", u ? " " : "", b); }
            }
            urow(a, "  held now", held[0] ? held : "-", "Buttons down this moment, numbered from 1");
            snprintf(v, sizeof v, "%d of %d", seen, p->nb);
            urow(a, "  buttons pressed", v, "Press every button once");
            char ax[96] = "";
            for (int k = 1; k <= p->na && k <= 4; k++) { size_t u = strlen(ax); snprintf(ax + u, sizeof ax - u, "%sA%d %.2f", u ? "  " : "", k, p->axis[k]); }
            urow(a, "  axes", ax[0] ? ax : "-", "Move the sticks: each axis from -1 to 1");
        }
        break;
    default: break;
    }
}

static void usb_summary(const App *a, int i, char *out, size_t cap) {
    out[0] = 0;
    int n = 0;
    switch (i) {
    case U_DEVICES: if (a->n_dev) snprintf(out, cap, "%d found", a->n_dev); break;
    case U_VIDEO:
        for (int k = 0; k < a->n_conn; k++) n += !a->usb_conn[k].internal && !strcmp(a->usb_conn[k].status, "connected");
        if (n) snprintf(out, cap, "%d connected", n);
        break;
    case U_MIDI: if (a->n_midi) snprintf(out, cap, "%d port%s", a->n_midi, a->n_midi == 1 ? "" : "s"); break;
    case U_STORAGE: if (a->n_disk) snprintf(out, cap, "%d found", a->n_disk); break;
    case U_NETWORK: if (a->n_net) snprintf(out, cap, "%d adapter%s", a->n_net, a->n_net == 1 ? "" : "s"); break;
    case U_KEYBOARD: for (int k = 0; k < a->n_in; k++) n += a->usb_in[k].keyboard && a->usb_in[k].usb; if (n) snprintf(out, cap, "%d on USB", n); break;
    case U_MOUSE: for (int k = 0; k < a->n_in; k++) n += a->usb_in[k].mouse && a->usb_in[k].usb; if (n) snprintf(out, cap, "%d on USB", n); break;
    case U_PADS:
        n = SDL_WasInit(SDL_INIT_JOYSTICK) ? SDL_NumJoysticks() - (a->js ? 1 : 0) : 0;
        if (n > 0) snprintf(out, cap, "%d found", n);
        break;
    default: break;
    }
}

static void usb_empty_lines(int sub, const char *out[6]) {
    for (int i = 0; i < 6; i++) out[i] = NULL;
    switch (sub) {
    case U_DEVICES: out[0] = "Nothing is plugged into the OTG port."; out[1] = "Plug in a device or a hub; a dock shows as a hub"; out[2] = "with the things behind it."; break;
    case U_VIDEO: out[0] = "No video connector found in /sys/class/drm."; break;
    case U_MIDI: out[0] = "No MIDI port."; out[1] = "Plug in a USB MIDI keyboard or interface. If it is"; out[2] = "plugged in already, the kernel has no USB MIDI driver."; break;
    case U_STORAGE: out[0] = "No USB storage."; out[1] = "Plug a stick or a drive into the OTG port."; break;
    case U_NETWORK: out[0] = "No network adapter."; out[1] = "Plug a USB Ethernet or Wi-Fi adapter into the OTG port."; break;
    case U_PADS: out[0] = "No other game controller."; out[1] = "Plug one into the OTG port; the built-in pad is on"; out[2] = "Buttons and sticks."; break;
    default: break;
    }
}

static void page_usb(R3Screen *s, const App *a) {
    R3Item items[U_COUNT];
    for (int i = 0; i < U_COUNT; i++) { snprintf(items[i].label, sizeof items[i].label, "%s", USB_LABEL[i]); usb_summary(a, i, items[i].value, sizeof items[i].value); }
    R3List l = {0, 1, R3_COLS, U_COUNT + 2, "USB and OTG", "A open  B back", items, U_COUNT, a->sel[P_USB], {NULL}};
    r3_list_dialog(s, &l);
    int top = U_COUNT + 3, sel = a->sel[P_USB];
    R3Box b = {0, USB_LABEL[sel], NULL, NULL, 0};
    r3_box(s, 0, top, R3_COLS, R3_DISPLAY_TOP + R3_DISPLAY_ROWS - top, &b);
    for (int i = 0; i < 3 && USB_ABOUT[sel][i]; i++) r3_text_fit(s, 2, top + 1 + i, USB_ABOUT[sel][i], R3_COLS - 4, R3_ST(R3_ST_BODY));
    char line[64], left[32];
    snprintf(line, sizeof line, "open %s", USB_LABEL[sel]);
    snprintf(left, sizeof left, "%d USB device%s", a->n_dev, a->n_dev == 1 ? "" : "s");
    cmdline(s, a, line, left);
}

static void page_usbsub(R3Screen *s, const App *a) {
    static const char *const foot[U_COUNT] = {"A scan again  B back", "A scan again  B back", "A send C4  B back", "A test  B back", "A scan again  B back",
                                              "type keys  Esc back", "move, click  B back", "press buttons  B back"};
    R3List l = {0, 1, R3_COLS, R3_DISPLAY_ROWS, USB_LABEL[a->usb_sub], foot[a->usb_sub], a->usb_rows, a->n_rows, a->sel[P_USBSUB], {NULL}};
    const char *em[6];
    usb_empty_lines(a->usb_sub, em);
    for (int i = 0; i < 6; i++) l.empty[i] = em[i];
    r3_list_dialog(s, &l);
    char left[32];
    snprintf(left, sizeof left, "%d of %d", a->sel[P_USBSUB] + 1, a->n_rows);
    int sel = a->sel[P_USBSUB];
    cmdline(s, a, a->n_rows && sel < a->n_rows ? a->usb_detail[sel] : "", a->n_rows ? left : NULL);
}

/* ---- the device report's USB sections (docs/DEVICE-REPORTS.md; usb_io.h) --------------------------------------------------------------- */

/* A scan, then one section a kind of thing the OTG port can bring. Nothing plugged in is SKIP, never FAIL; a device that is there and does not
 * work (no driver, will not open, not mounted) is FAIL. What a person has to do (type, move, play) is SKIP until they have, from the USB pages. */
static void report_usb(App *a, R3Report *r) {
    usb_scan(a, SDL_GetTicks());
    char key[128];

    r3_report_section(r, "usb devices");
    if (!a->n_dev) r3_report_line(r, R3_SKIP, "devices", "nothing is plugged into the OTG port");
    else r3_report_line(r, R3_INFO, "devices", "%d on USB", a->n_dev);
    for (int i = 0; i < a->n_dev; i++) {
        const UioDev *u = &a->usb_dev[i];
        snprintf(key, sizeof key, "device %s", u->name);
        r3_report_line(r, R3_INFO, key, "%s (%s), %s Mb/s: %s; drivers: %s", u->product, u->vidpid, u->speed[0] ? u->speed : "?", u->kinds, u->drivers[0] ? u->drivers : "none");
        if (strstr(u->kinds, "billboard")) {
            snprintf(key, sizeof key, "billboard %s", u->name);
            r3_report_line(r, R3_INFO, key, "%s: %s; nothing is wrong with the unit, and no driver is meant for it", u->product, BILLBOARD_WHY);
        }
        if (u->unbound && !u->hub) {
            snprintf(key, sizeof key, "driver %s", u->name);
            r3_report_line(r, R3_FAIL, key, "%d of %d interface(s) of %s have no driver: the kernel cannot use this device", u->unbound, u->interfaces, u->product);
        }
    }

    r3_report_section(r, "usb video out");
    int external = 0;
    for (int i = 0; i < a->n_conn; i++) {
        const UioConn *c = &a->usb_conn[i];
        snprintf(key, sizeof key, "connector %s", c->name);
        r3_report_line(r, R3_INFO, key, "%s%s%s%s", c->status, c->mode[0] ? ", " : "", c->mode, c->internal ? " (this unit's screen)" : c->usb ? " (USB adapter)" : "");
        if (!c->internal && !strcmp(c->status, "connected")) {
            external++;
            r3_report_line(r, R3_PASS, "external display", "connected on %s%s%s", c->name, c->mode[0] ? ", first mode " : "", c->mode);
        }
    }
    if (!a->n_conn) r3_report_line(r, R3_SKIP, "connectors", "no video connectors in /sys/class/drm here");
    else if (!external) r3_report_line(r, R3_SKIP, "external display", "none connected (an HDMI or DisplayLink dock would show here)");
    if (usb_has_billboard(a) && !external) r3_report_line(r, R3_INFO, "billboard", "%s; a USB 2.0 DisplayLink adapter is the kind that could work", BILLBOARD_WHY);
    if (a->displays >= 0) r3_report_line(r, R3_INFO, "SDL displays", "%d", a->displays);

    /* DisplayLink (USB video): the four things it needs, asked in order (docs/DISPLAYLINK-ADAPTERS.md). A fact that is missing and that nothing can change
     * without a rebuilt kernel is a FAIL once an adapter is plugged in (it cannot work) and INFO before (nothing plugged in is never a FAIL); one that
     * depends on an adapter being plugged in is SKIP until it is. */
    r3_report_section(r, "usb displaylink");
    UioUdl ud;
    uio_udl_probe(&ud, a->usb_dev, a->n_dev);
    if (ud.adapter) r3_report_line(r, R3_INFO, "adapter", "%s %s (DisplayLink, vendor 17e9)", ud.adapter_name, ud.adapter_product);
    else r3_report_line(r, R3_SKIP, "adapter", "no DisplayLink adapter plugged in (USB vendor 17e9): the checks below read the kernel only");
    if (ud.loaded) r3_report_line(r, R3_PASS, "udl loaded", "%s", ud.loaded_how);
    else if (ud.adapter) r3_report_line(r, R3_FAIL, "udl loaded", "an adapter is plugged in and the udl driver is not in the running kernel (kernel %s)", ud.release);
    else r3_report_line(r, R3_SKIP, "udl loaded", "not loaded now: a module loads when an adapter is plugged in, so this says nothing until one is (kernel %s)", ud.release);
    int builtin_cfg = !strcmp(ud.cfg_drm, "y");
    const char *miss = ud.adapter ? R3_FAIL : R3_INFO;
    if (ud.file) r3_report_line(r, R3_PASS, "udl module file", "%s", ud.file_how);
    else if (builtin_cfg || (ud.loaded && !strcmp(ud.loaded_how, "built in"))) r3_report_line(r, R3_PASS, "udl module file", "none needed: udl is built into the kernel");
    else r3_report_line(r, miss, "udl module file", "no udl.ko under /lib/modules/%s and the kernel does not list it as built in: the kernel cannot load it (build it out of tree against the vendor kernel)", ud.release);
    if (!ud.cfg_read) r3_report_line(r, R3_SKIP, "kernel config DRM_UDL", "no kernel config could be read (/proc/config.gz, /boot/config-%s, the build tree's .config)", ud.release);
    else if (!strcmp(ud.cfg_drm, "y") || !strcmp(ud.cfg_drm, "m"))
        r3_report_line(r, R3_PASS, "kernel config DRM_UDL", "CONFIG_DRM_UDL=%s (%s)", ud.cfg_drm, ud.cfg_src);
    else if (!ud.cfg_drm[0]) r3_report_line(r, miss, "kernel config DRM_UDL", "CONFIG_DRM_UDL is not in the config (%s): the kernel's DRM has no udl driver", ud.cfg_src);
    else r3_report_line(r, miss, "kernel config DRM_UDL", "CONFIG_DRM_UDL is %s (%s): the kernel was built without the udl driver", ud.cfg_drm, ud.cfg_src);
    if (ud.cfg_read) r3_report_line(r, R3_INFO, "kernel config FB_UDL", "%s (the older framebuffer driver for the same chips)", ud.cfg_fb[0] ? ud.cfg_fb : "not in the config");
    if (ud.drm_udl) r3_report_line(r, R3_PASS, "drm device", "%s is behind USB and bound to udl: plugging the adapter in made a video card", ud.drm_name);
    else if (ud.drm_usb) r3_report_line(r, R3_INFO, "drm device", "%s is behind USB but not bound to udl (another adapter's driver)", ud.drm_name);
    else if (ud.adapter) r3_report_line(r, R3_FAIL, "drm device", "an adapter is plugged in and no DRM card appeared behind USB (/sys/class/drm): the driver did not bind to it");
    else r3_report_line(r, R3_SKIP, "drm device", "no DRM card behind USB: plug a DisplayLink adapter in and run again");
    {
        const char *why = !ud.cfg_read ? "read the kernel config first" :
                          (strcmp(ud.cfg_drm, "y") && strcmp(ud.cfg_drm, "m")) ? "the kernel has no udl: it needs a kernel built with CONFIG_DRM_UDL, or the module built out of tree" :
                          !ud.file && strcmp(ud.cfg_drm, "y") ? "the config has udl as a module but no udl.ko is on this card" :
                          !ud.adapter ? "everything the kernel needs is there: plug a DisplayLink adapter in and run again" :
                          ud.drm_udl ? "working: a DisplayLink adapter made a video card" : "the adapter is plugged in and no card appeared: read the kernel log";
        r3_report_line(r, R3_INFO, "udl verdict", "%s", why);
    }

    r3_report_section(r, "usb midi");
    if (!a->n_midi) r3_report_line(r, R3_SKIP, "ports", "no MIDI port: nothing USB MIDI is plugged in, or the kernel has no USB MIDI driver");
    for (int i = 0; i < a->n_midi; i++) {
        const UioMidi *m = &a->usb_midi[i];
        snprintf(key, sizeof key, "%s opens", m->name);
        int fd = open(m->node, O_RDWR | O_NONBLOCK);
        if (fd < 0) fd = open(m->node, O_RDONLY | O_NONBLOCK);
        if (fd < 0) fd = open(m->node, O_WRONLY | O_NONBLOCK);
        if (fd >= 0) { close(fd); r3_report_line(r, R3_PASS, key, "%s%s", m->node, m->usb ? " (USB)" : ""); }
        else r3_report_line(r, R3_FAIL, key, "%s: %s", m->node, strerror(errno));
    }
    if (a->n_midi) {
        if (a->midi_in_msgs || a->midi_out_msgs) r3_report_line(r, R3_PASS, "messages", "%d in, %d out", a->midi_in_msgs, a->midi_out_msgs);
        else r3_report_line(r, R3_SKIP, "messages", "not tried: open USB and OTG > MIDI, play a key, and press A to send middle C");
    }

    r3_report_section(r, "usb storage");
    if (!a->n_disk) r3_report_line(r, R3_SKIP, "disks", "no USB storage");
    for (int i = 0; i < a->n_disk; i++) {
        const UioDisk *k = &a->usb_disk[i];
        snprintf(key, sizeof key, "disk %s", k->dev);
        r3_report_line(r, R3_INFO, key, "%s, %.1f GB", k->model, (double)k->bytes / 1e9);
        snprintf(key, sizeof key, "%s mounted", k->dev);
        if (!k->bytes) r3_report_line(r, R3_SKIP, key, "no media in it (an empty slot of a card reader): put a card in");
        else if (k->mount[0]) r3_report_line(r, R3_PASS, key, "%s on %s (%s); its checks are in the files section as USB %s", k->part, k->mount, k->fstype, k->dev);
        else r3_report_line(r, R3_FAIL, key, "not mounted: nothing mounts USB storage for apps yet, so it cannot be read or written (a mount needs root)");
    }

    r3_report_section(r, "usb network");
    if (!a->n_net) r3_report_line(r, R3_SKIP, "adapters", "no network adapter");
    for (int i = 0; i < a->n_net; i++) {
        const UioNet *n = &a->usb_net[i];
        int up = n->carrier == 1 || !strcmp(n->state, "up");
        snprintf(key, sizeof key, "%s", n->name);
        if (n->wifi) r3_report_line(r, R3_INFO, key, "Wi-Fi%s, %s%s%s", n->usb ? " (USB)" : "", n->state, n->ip[0] ? ", " : "", n->ip);
        else if (up) {
            char sp[24] = "";
            if (n->speed > 0) snprintf(sp, sizeof sp, ", %d Mb/s", n->speed);
            r3_report_line(r, R3_PASS, key, "Ethernet%s: link up%s%s%s", n->usb ? " (USB)" : "", sp, n->ip[0] ? ", " : ", no IPv4 address yet", n->ip);
        }
        else r3_report_line(r, R3_INFO, key, "Ethernet%s: no link (%s): no cable, or the other end is off", n->usb ? " (USB)" : "", n->state);
        snprintf(key, sizeof key, "%s traffic", n->name);
        r3_report_line(r, R3_INFO, key, "%llu bytes in, %llu out, address %s", n->rx, n->tx, n->mac[0] ? n->mac : "none");
    }

    r3_report_section(r, "usb keyboard");
    int found = 0;
    for (int i = 0; i < a->n_in; i++)
        if (a->usb_in[i].keyboard) { found++; r3_report_line(r, R3_INFO, "keyboard", "%s%s", a->usb_in[i].name, a->usb_in[i].usb ? " (USB)" : " (built in)"); }
    if (!found) r3_report_line(r, R3_SKIP, "keyboard", "none found: plug a USB keyboard in");
    if (a->kb_n) r3_report_line(r, R3_PASS, "keys pressed", "%d different keys reached SDL", a->kb_n);
    else r3_report_line(r, R3_SKIP, "keys pressed", "not tried: open USB and OTG > Keyboard and type");

    r3_report_section(r, "usb mouse");
    found = 0;
    for (int i = 0; i < a->n_in; i++)
        if (a->usb_in[i].mouse) { found++; r3_report_line(r, R3_INFO, "mouse", "%s%s", a->usb_in[i].name, a->usb_in[i].usb ? " (USB)" : " (built in)"); }
    if (!found) r3_report_line(r, R3_SKIP, "mouse", "none found: plug a USB mouse in");
    if (a->m_moves || a->m_buttons || a->m_wheel) r3_report_line(r, R3_PASS, "mouse events", "%d moves, buttons %s%s%s%s%s, wheel %d", a->m_moves,
                                                                    a->m_buttons & 2 ? "left " : "", a->m_buttons & 4 ? "middle " : "", a->m_buttons & 8 ? "right " : "",
                                                                    a->m_buttons & 0x30 ? "side " : "", a->m_buttons ? "" : "none", a->m_wheel);
    else r3_report_line(r, R3_SKIP, "mouse events", "not tried: open USB and OTG > Mouse, move it and click");

    r3_report_section(r, "usb pads");
    int others = 0;
    if (SDL_WasInit(SDL_INIT_JOYSTICK)) {
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            if (a->js && SDL_JoystickGetDeviceInstanceID(i) == a->js_id) continue;
            others++;
            char guid[40];
            SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i), guid, sizeof guid);
            snprintf(key, sizeof key, "pad %d", others);
            r3_report_line(r, R3_INFO, key, "%s, guid %s", SDL_JoystickNameForIndex(i) ? SDL_JoystickNameForIndex(i) : "?", guid);
        }
    }
    if (!others) r3_report_line(r, R3_SKIP, "pads", "no other game controller (the built-in pad is in the input section)");
    for (int i = 0; i < a->n_ext; i++) {
        const ExtPad *p = &a->ext[i];
        int seen = 0;
        for (int b = 1; b <= p->nb && b < MAX_B; b++) seen += p->seen[b];
        snprintf(key, sizeof key, "%s buttons", p->name);
        if (p->nb && seen == p->nb) r3_report_line(r, R3_PASS, key, "all %d pressed", p->nb);
        else if (seen) r3_report_line(r, R3_INFO, key, "%d of %d pressed (open USB and OTG > Game controllers and press the rest)", seen, p->nb);
        else r3_report_line(r, R3_SKIP, key, "not tried: open USB and OTG > Game controllers and press its buttons");
    }
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
    case P_USB: page_usb(s, a); break;
    case P_USBSUB:
        usb_build(a);
        if (a->sel[P_USBSUB] >= a->n_rows) a->sel[P_USBSUB] = a->n_rows ? a->n_rows - 1 : 0;
        page_usbsub(s, a);
        break;
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
    if (a->page == P_USBSUB && page != P_USBSUB) { midi_close(a); ext_close_all(a); }     /* the live checks end with their page */
    a->page = page;
    a->chord_since = 0;
    a->idle_since = now;
    a->force = 1;
    if (page == P_INPUT) say(a, R3_ST_HINT, "Press every button. Hold SELECT+START to go back.");
    if (page == P_SYSTEM) system_rows(a);
    if (page == P_SOUND) sound_status(a);
    if (page == P_USB || page == P_USBSUB) usb_scan(a, now);
    if (page == P_USB) say(a, R3_ST_HINT, "What the OTG port can bring. A opens a check.");
    if (page == P_USBSUB) {
        say(a, R3_ST_HINT, "%s", USB_HINT[a->usb_sub]);
        if (a->usb_sub == U_MIDI) midi_open_sel(a);
        if (a->usb_sub == U_PADS) ext_open_all(a);
        if (a->usb_sub == U_KEYBOARD) { memset(a->kb_seen, 0, sizeof a->kb_seen); a->kb_n = 0; memset(a->kb_log, 0, sizeof a->kb_log); }
        if (a->usb_sub == U_MOUSE) { a->m_x = a->m_y = a->m_moves = a->m_wheel = 0; a->m_buttons = 0; }
    }
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
    case P_USB: return U_COUNT;
    case P_USBSUB: return a->n_rows;
    default: return 0;
    }
}
static int per_of(int page) { return page == P_HOME ? H_COUNT : page == P_VIDEO ? NPAT : page == P_SOUND ? NTONE : page == P_USB ? U_COUNT : r3_list_rows(R3_DISPLAY_ROWS); }

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
            else if (sel == H_USB) go(a, P_USB, now);
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
    case P_USB:
        if (act_ == R3_BACK) go(a, P_HOME, now);
        else if (act_ == R3_ACCEPT) { a->usb_sub = sel; a->sel[P_USBSUB] = 0; go(a, P_USBSUB, now); }
        break;
    case P_USBSUB:
        if (act_ == R3_BACK) go(a, P_USB, now);
        else if (act_ == R3_ACCEPT) {
            if (a->usb_sub == U_MIDI) midi_send(a, now);
            else if (a->usb_sub == U_STORAGE) {
                int mounted = 0;
                for (int i = 0; i < a->n_disk; i++) mounted += a->usb_disk[i].mount[0] != 0;
                if (!mounted) say(a, R3_ST_WARN, "Nothing to test: no USB storage is mounted.");
                else { go(a, P_FILES, now); a->pending = H_FILES; say(a, R3_ST_WARN, "Testing the cards and the USB storage..."); }
            } else { usb_scan(a, now); say(a, R3_ST_OK, "Scanned again."); }
        } else if (a->usb_sub == U_MIDI) midi_open_sel(a);   /* the selection moved: that port is the open one */
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
    a->js_id = SDL_JoystickInstanceID(a->js);
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
    a->midi_fd = a->midi_idx = -1;
    a->displays = -1;
    snprintf(a->mode, sizeof a->mode, "?");
    snprintf(a->renderer, sizeof a->renderer, "?");
    snprintf(a->backlight, sizeof a->backlight, "?");
    snprintf(a->snd_card, sizeof a->snd_card, "?");
}

/* Whether a pad event is from a pad that is not the built-in one: it belongs to the Game controllers page, never to the built-in pad's record. */
static int foreign_pad(const App *a, SDL_JoystickID which) {
    return a->js && which != a->js_id;
}

/* One SDL event. Every button, axis, hat and key is recorded on every page; the list pages also act on them. */
static void handle(App *a, const SDL_Event *e, Uint32 now) {
    if (a->js && (e->type == SDL_JOYBUTTONDOWN || e->type == SDL_JOYBUTTONUP || e->type == SDL_JOYAXISMOTION || e->type == SDL_JOYHATMOTION)) {
        SDL_JoystickID which = e->type == SDL_JOYAXISMOTION ? e->jaxis.which : e->type == SDL_JOYHATMOTION ? e->jhat.which : e->jbutton.which;
        if (foreign_pad(a, which)) { ext_event(a, e); return; }      /* another pad (USB): the Game controllers page's, not the built-in pad's record */
    }
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
        if (isdown && r3_toast_key(&a->toast)) break; /* the key that clears a failure toast is used up (not counted as a press) */
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
    case SDL_MOUSEMOTION:
        if (a->page == P_USBSUB && a->usb_sub == U_MOUSE) { a->m_x += e->motion.xrel; a->m_y += e->motion.yrel; a->m_moves++; }
        break;
    case SDL_MOUSEBUTTONDOWN:
        if (a->page == P_USBSUB && a->usb_sub == U_MOUSE && e->button.button < 32) a->m_buttons |= 1u << e->button.button;
        break;
    case SDL_MOUSEWHEEL:
        if (a->page == P_USBSUB && a->usb_sub == U_MOUSE) a->m_wheel += e->wheel.y;
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        if (e->key.repeat) break;
        int isdown = e->type == SDL_KEYDOWN;
        if (isdown && r3_toast_key(&a->toast)) break;
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
        if (a->page == P_USBSUB && a->usb_sub == U_KEYBOARD) {    /* every key is a test here; Esc, or B on the pad, goes back */
            kb_press(a, (int)e->key.keysym.scancode, SDL_GetKeyName(k));
            if (k == SDLK_ESCAPE) act(a, R3_BACK, now);
            break;
        }
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
    usb_tick(a, now);
    if (a->dev && a->audio_close_at && now >= a->audio_close_at) audio_close(a);      /* the tone is over */
    if (a->page == P_INPUT && now - a->idle_since >= IDLE_LIMIT_MS) {
        go(a, P_HOME, now);
        say(a, R3_ST_WARN, "No input for 15 s: back to the list.");
    }
}

#define IDLE_NAP_MS 100 /* how long an idle pass sleeps on a quiet page: about 10 wake-ups a second, where 16 ms was 60 (CLAUDE.md rule 14; docs/POWER.md) */

/* How long the main loop sleeps after a pass: a frame (16 ms) while something is changing or about to (a screen still being presented, a toast,
 * the exit chord counting up, a job about to run) or on a live page, else IDLE_NAP_MS. A press waits at most that long. Not
 * SDL_WaitEventTimeout: on a video driver without a native wait SDL turns it into a 1 ms polling loop (docs/POWER.md). */
static unsigned nap_ms(App *a, const R3Video *v) {
    /* the pages where a person is pressing things and wants to see them at once keep the frame: the button test, the live USB checks */
    if (a->page == P_INPUT || (a->page == P_USBSUB && (a->usb_sub == U_KEYBOARD || a->usb_sub == U_MOUSE || a->usb_sub == U_PADS || a->usb_sub == U_MIDI))) return 16;
    if (a->force || v->settle > 0 || r3_toast_active(&a->toast) || a->chord_since || a->pending || a->midi_off_at) return 16;
    return IDLE_NAP_MS;
}

/* ---- the power section of the device report (CLAUDE.md rule 14; docs/POWER.md) ------------------------------------------------ */

/* What a pass of the main loop draws with. Set by main; NULL for --report, which has no window and so nothing to measure. */
struct Loop { SDL_Renderer *r; R3Video *video; R3Screen screen; };

/* The part of a pass that draws, shared by the main loop and the idle window below, so the report measures the real thing. */
static void draw_frame(App *a, Uint32 now) {
    compose(a, &a->loop->screen, now);
    r3_toast_draw(&a->toast, &a->loop->screen);
    r3_video_draw(a->loop->video, a->loop->r, &a->loop->screen, a->force);
    a->force = 0;
}

static void drain_events(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) { /* a report in progress takes no input */ }
}

/* One pass of the idle loop as main runs it (the home page), minus input. */
static void idle_turn(void *arg) {
    App *a = arg;
    Uint32 now = SDL_GetTicks();
    drain_events();
    tick(a, now);
    r3_toast_poll(&a->toast, now);
    draw_frame(a, now);
    SDL_Delay(nap_ms(a, a->loop->video));
}

#define POWER_WORK_CPU 40.0    /* % of a core on the Motion pattern: a first guess, set before any measurement (docs/POWER.md) */
#define POWER_WORK_WAKE 600.0  /* wake-ups a second there. Measured on the unit (2026-11-12): 544/s = this thread 273 + the Mali driver's 241 (vsync at 60 a second, about 9 each); the first guess, 180, was set before any measurement */

/* idle: the app open on its list, nothing pressed. working: the Motion pattern, its heaviest page (a frame every vsync). */
static void power_windows(App *a, R3Report *r) {
    r3_report_power_section(r);
    if (!a->loop) {
        r3_report_line(r, R3_SKIP, "idle", "no window (--report): open Device report in the app to measure the app itself");
        return;
    }
    R3PowerResult idle, work;
    r3_report_power_idle(r, r3_report_power_seconds(10), idle_turn, a, &idle);
    int was = a->sel[P_VIDEO];
    a->sel[P_VIDEO] = PAT_MOTION;
    R3Power p;
    r3_power_begin(&p);
    double t0 = r3_report_ms();
    while (r3_report_ms() - t0 < r3_report_power_seconds(5) * 1000.0) {
        drain_events();
        draw_pattern(a, a->loop->r);
        r3_power_sample(&p);
    }
    r3_report_power_end(r, &p, "working", POWER_WORK_CPU, POWER_WORK_WAKE, &work);
    a->sel[P_VIDEO] = was;
    a->force = 1;           /* the list is drawn again */
    r3_report_power_advice(r, &idle, &work);
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

/* One screen to a BMP (R3I6NOS_IO_SHOTS=dir io-checker-native --selftest saves the USB pages, as Commander's R3I6NOS_FM_SHOTS saves its screens). */
static void save_bmp(const R3Screen *s, const char *path) {
    Uint32 *px = malloc((size_t)R3_W * R3_H * sizeof(Uint32));
    SDL_Surface *surf = px ? SDL_CreateRGBSurfaceWithFormatFrom(px, R3_W, R3_H, 32, R3_W * 4, SDL_PIXELFORMAT_ARGB8888) : NULL;
    if (surf) { r3_render(s, px); SDL_SaveBMP(surf, path); SDL_FreeSurface(surf); }
    free(px);
}

/* ---- the USB and OTG checks against a fake /sys, /proc and /dev (usb_io.h) --------------------------------------------------------- */

static void ut_file(const char *root, const char *rel, const char *text) {
    char p[PATH_MAX + 200], d[PATH_MAX + 200];
    snprintf(p, sizeof p, "%s/%s", root, rel);
    snprintf(d, sizeof d, "%s", p);
    char *sl = strrchr(d, '/');
    if (sl) { *sl = 0; r3_report_mkdirs(d); }
    FILE *f = fopen(p, "w");
    if (f) { fputs(text, f); fclose(f); }
}
static void ut_dir(const char *root, const char *rel) {
    char p[PATH_MAX + 200];
    snprintf(p, sizeof p, "%s/%s", root, rel);
    r3_report_mkdirs(p);
}
/* link (relative to root) -> target (relative to root, or an absolute text when it starts with "text:") */
static void ut_link(const char *root, const char *link, const char *target) {
    char l[PATH_MAX + 200], t[PATH_MAX + 200], d[PATH_MAX + 200];
    snprintf(l, sizeof l, "%s/%s", root, link);
    if (!strncmp(target, "text:", 5)) snprintf(t, sizeof t, "%s", target + 5);
    else snprintf(t, sizeof t, "%s/%s", root, target);
    snprintf(d, sizeof d, "%s", l);
    char *sl = strrchr(d, '/');
    if (sl) { *sl = 0; r3_report_mkdirs(d); }
    if (symlink(t, l) != 0) fprintf(stderr, "io-checker selftest: symlink %s: %s\n", l, strerror(errno));
}
static void ut_usb_iface(const char *root, const char *dev, const char *iface, const char *cls, const char *sub, const char *proto, const char *driver) {
    char rel[200];
    snprintf(rel, sizeof rel, "sys/bus/usb/devices/%s/%s/bInterfaceClass", dev, iface); ut_file(root, rel, cls);
    snprintf(rel, sizeof rel, "sys/bus/usb/devices/%s/%s/bInterfaceSubClass", dev, iface); ut_file(root, rel, sub);
    snprintf(rel, sizeof rel, "sys/bus/usb/devices/%s/%s/bInterfaceProtocol", dev, iface); ut_file(root, rel, proto);
    if (driver) {
        char link[200], tgt[200];
        snprintf(link, sizeof link, "sys/bus/usb/devices/%s/%s/driver", dev, iface);
        snprintf(tgt, sizeof tgt, "text:../../../../../bus/usb/drivers/%s", driver);
        ut_link(root, link, tgt);
    }
}
static void ut_usb_dev(const char *root, const char *name, const char *vid, const char *pid, const char *product, const char *speed) {
    char rel[200];
    snprintf(rel, sizeof rel, "sys/bus/usb/devices/%s/idVendor", name); ut_file(root, rel, vid);
    snprintf(rel, sizeof rel, "sys/bus/usb/devices/%s/idProduct", name); ut_file(root, rel, pid);
    snprintf(rel, sizeof rel, "sys/bus/usb/devices/%s/product", name); ut_file(root, rel, product);
    snprintf(rel, sizeof rel, "sys/bus/usb/devices/%s/speed", name); ut_file(root, rel, speed);
}
static const UioDev *ut_find(const App *u, const char *name) {
    for (int i = 0; i < u->n_dev; i++) if (!strcmp(u->usb_dev[i].name, name)) return &u->usb_dev[i];
    return NULL;
}
static int ut_rows_have(const App *u, const char *text) {
    for (int i = 0; i < u->n_rows; i++) if (strstr(u->usb_rows[i].label, text) || strstr(u->usb_rows[i].value, text)) return 1;
    return 0;
}

static void selftest_usb(void) {
    char tmpl[PATH_MAX];
    snprintf(tmpl, sizeof tmpl, "%s/io-checker-usb-XXXXXX", env_or("TMPDIR", "/tmp"));
    char *root = mkdtemp(tmpl);
    if (!root) { CHECK(0, "usb: a temporary folder"); return; }
    char sysd[PATH_MAX + 8], procd[PATH_MAX + 8], devd[PATH_MAX + 8];
    snprintf(sysd, sizeof sysd, "%s/sys", root);
    snprintf(procd, sizeof procd, "%s/proc", root);
    snprintf(devd, sizeof devd, "%s/dev", root);
    setenv("R3I6NOS_SYS", sysd, 1);
    setenv("R3I6NOS_PROC", procd, 1);
    setenv("R3I6NOS_DEV", devd, 1);
    char modsd[PATH_MAX + 16], bootd[PATH_MAX + 16];
    snprintf(modsd, sizeof modsd, "%s/lib/modules", root);
    snprintf(bootd, sizeof bootd, "%s/boot", root);
    setenv("R3I6NOS_MODULES", modsd, 1);
    setenv("R3I6NOS_BOOT", bootd, 1);
    setenv("R3I6NOS_KERNEL_RELEASE", "4.4.189", 1);

    /* USB devices: a keyboard, a MIDI keyboard (audio + MIDI), a hub, a device the kernel has no driver for, Ethernet, a pad, a stick */
    ut_dir(root, "sys/bus/usb/devices/usb1");
    ut_usb_dev(root, "1-1", "046d", "c31c", "USB Keyboard", "12");
    ut_usb_iface(root, "1-1", "1-1:1.0", "03", "01", "01", "usbhid");
    ut_usb_dev(root, "1-2", "1235", "0036", "Launchkey Mini", "480");
    ut_usb_iface(root, "1-2", "1-2:1.0", "01", "01", "00", "snd-usb-audio");
    ut_usb_iface(root, "1-2", "1-2:1.1", "01", "03", "00", "snd-usb-audio");
    ut_usb_dev(root, "1-3", "05e3", "0610", "USB2.0 Hub", "480");
    ut_usb_iface(root, "1-3", "1-3:1.0", "09", "00", "01", "hub");
    ut_usb_dev(root, "1-10", "17e9", "0141", "USB Graphics Adapter", "480");   /* a DisplayLink adapter: vendor 17e9, its interface bound to udl */
    ut_usb_iface(root, "1-10", "1-10:1.0", "ff", "00", "00", "udl");
    ut_usb_dev(root, "1-4", "dead", "beef", "Mystery Gadget", "480");
    ut_usb_iface(root, "1-4", "1-4:1.0", "ff", "00", "00", NULL);
    ut_usb_dev(root, "1-5", "0b95", "1790", "USB Ethernet", "480");
    ut_usb_iface(root, "1-5", "1-5:1.0", "ff", "ff", "00", "asix");
    ut_usb_dev(root, "1-6", "045e", "028e", "Gamepad", "12");
    ut_usb_iface(root, "1-6", "1-6:1.0", "03", "00", "00", "usbhid");
    ut_dir(root, "sys/bus/usb/devices/1-6/1-6:1.0/input/input7/js0");
    ut_usb_dev(root, "1-7", "0781", "5581", "Ultra", "480");
    ut_usb_iface(root, "1-7", "1-7:1.0", "08", "06", "50", "usb-storage");
    ut_usb_dev(root, "1-9", "2109", "0100", "USB 2.0 BILLBOARD", "480");     /* a dock's notice: class 0x11, no driver is meant to bind */
    ut_usb_iface(root, "1-9", "1-9:1.0", "11", "00", "00", NULL);
    /* video: this unit's panel, an HDMI port with nothing in it, a USB display adapter with a monitor */
    ut_file(root, "sys/class/drm/card0-DSI-1/status", "connected\n");
    ut_file(root, "sys/class/drm/card0-DSI-1/modes", "640x480\n480x320\n");
    ut_file(root, "sys/class/drm/card0-HDMI-A-1/status", "disconnected\n");
    ut_file(root, "sys/class/drm/card1-DVI-I-1/status", "connected\n");
    ut_file(root, "sys/class/drm/card1-DVI-I-1/modes", "1920x1080\n1280x720\n");
    ut_dir(root, "sys/devices/platform/ff400000.gpu");
    ut_dir(root, "sys/devices/usb1/1-6/drm");
    ut_link(root, "sys/class/drm/card0/device", "sys/devices/platform/ff400000.gpu");
    ut_link(root, "sys/class/drm/card1/device", "sys/devices/usb1/1-6/drm");
    ut_dir(root, "sys/bus/usb/drivers/udl");
    ut_link(root, "sys/devices/usb1/1-6/drm/driver", "sys/bus/usb/drivers/udl");              /* the card behind USB is bound to udl */
    ut_file(root, "proc/modules", "snd_usb_audio 90112 1 - Live 0x0\nudl 20480 0 - Live 0x0\n");
    ut_file(root, "lib/modules/4.4.189/kernel/drivers/gpu/drm/udl/udl.ko", "ELF\n");
    ut_file(root, "proc/config.txt", "CONFIG_DRM=y\nCONFIG_DRM_UDL=m\n# CONFIG_FB_UDL is not set\n");
    {
        char gz[PATH_MAX * 2 + 64];
        snprintf(gz, sizeof gz, "gzip -c '%s/proc/config.txt' > '%s/proc/config.gz'", root, root);
        CHECK(system(gz) == 0, "usb: a gzipped kernel config for the fixture");
    }
    /* storage: a stick on USB (mounted, with a space in the path) and an SD card that is not USB */
    ut_file(root, "sys/devices/usb1/1-7/host6/target6:0:0/6:0:0:0/vendor", "SanDisk \n");
    ut_file(root, "sys/devices/usb1/1-7/host6/target6:0:0/6:0:0:0/model", "Ultra\n");
    ut_link(root, "sys/block/sda/device", "sys/devices/usb1/1-7/host6/target6:0:0/6:0:0:0");
    ut_dir(root, "sys/devices/usb1/1-7/host6/target6:0:0/6:0:0:1");           /* the card reader's second slot, empty */
    ut_link(root, "sys/block/sdb/device", "sys/devices/usb1/1-7/host6/target6:0:0/6:0:0:1");
    ut_file(root, "sys/block/sdb/size", "0\n");
    ut_file(root, "sys/block/sda/size", "15564800\n");
    ut_dir(root, "sys/devices/platform/ff0f0000.mmc");
    ut_link(root, "sys/block/mmcblk0/device", "sys/devices/platform/ff0f0000.mmc");
    ut_file(root, "proc/mounts", "/dev/mmcblk0p3 /roms exfat rw 0 0\n/dev/sda1 /media/My\\040Stick vfat rw 0 0\n");
    /* network: a USB Ethernet with a link, a USB Wi-Fi adapter that is down */
    ut_dir(root, "sys/devices/usb1/1-5/net");
    ut_dir(root, "sys/devices/usb1/1-8/net");
    ut_file(root, "sys/class/net/eth1/operstate", "up\n");
    ut_file(root, "sys/class/net/eth1/carrier", "1\n");
    ut_file(root, "sys/class/net/eth1/speed", "1000\n");
    ut_file(root, "sys/class/net/eth1/address", "aa:bb:cc:dd:ee:ff\n");
    ut_file(root, "sys/class/net/eth1/statistics/rx_bytes", "1000\n");
    ut_file(root, "sys/class/net/eth1/statistics/tx_bytes", "2000\n");
    ut_link(root, "sys/class/net/eth1/device", "sys/devices/usb1/1-5/net");
    ut_file(root, "sys/class/net/wlan0/operstate", "down\n");
    ut_dir(root, "sys/class/net/wlan0/wireless");
    ut_link(root, "sys/class/net/wlan0/device", "sys/devices/usb1/1-8/net");
    ut_file(root, "sys/class/net/lo/operstate", "unknown\n");
    /* MIDI: one port, a FIFO standing in for the device (what is written to it is read back: a loopback) */
    ut_dir(root, "dev/snd");
    char fifo[PATH_MAX + 64];
    snprintf(fifo, sizeof fifo, "%s/snd/midiC1D0", devd);
    CHECK(mkfifo(fifo, 0600) == 0, "usb: a fake MIDI node");
    ut_file(root, "proc/asound/card1/id", "Launchkey\n");
    ut_file(root, "proc/asound/card1/usbbus", "001/005\n");
    /* input devices: a USB keyboard, a USB mouse, a pad, and the unit's power key (not a keyboard: no LEDs) */
    ut_file(root, "proc/bus/input/devices",
            "I: Bus=0003 Vendor=046d Product=c31c Version=0110\nN: Name=\"Logitech USB Keyboard\"\nS: Sysfs=/devices/platform/ff580000.usb/usb1/1-1/1-1:1.0/input/input3\nH: Handlers=sysrq kbd leds event3 \n\n"
            "I: Bus=0003 Vendor=046d Product=c077 Version=0111\nN: Name=\"Optical Mouse\"\nS: Sysfs=/devices/platform/ff580000.usb/usb1/1-2/1-2:1.0/input/input4\nH: Handlers=mouse0 event4 \n\n"
            "I: Bus=0003 Vendor=045e Product=028e Version=0110\nN: Name=\"Gamepad\"\nS: Sysfs=/devices/platform/ff580000.usb/usb1/1-6/1-6:1.0/input/input7\nH: Handlers=js0 event7 \n\n"
            "I: Bus=0003 Vendor=413c Product=301a Version=0111\nN: Name=\"Event Mouse\"\nS: Sysfs=/devices/platform/ff580000.usb/usb1/1-9/1-9:1.0/input/input8\nB: EV=17\nB: KEY=1f0000 0 0 0 0\nB: REL=903\nH: Handlers=event8 \n\n"
            "I: Bus=0019 Vendor=0001 Product=0001 Version=0100\nN: Name=\"rk8xx_pwrkey\"\nS: Sysfs=/devices/platform/rk8xx/input/input0\nH: Handlers=kbd event0 \n\n");

    App *u = calloc(1, sizeof(App));
    if (!u) { CHECK(0, "usb: memory"); return; }
    app_init(u);
    usb_scan(u, 0);
    /* every parser */
    CHECK(u->n_dev == 9, "usb: nine devices (the root hub and the interfaces are not devices)");
    const UioDev *d = ut_find(u, "1-1");
    CHECK(d && !strcmp(d->kinds, "keyboard") && !strcmp(d->drivers, "usbhid") && !strcmp(d->vidpid, "046d:c31c") && !strcmp(d->speed, "12") && !strcmp(d->product, "USB Keyboard"), "usb: the keyboard");
    d = ut_find(u, "1-2");
    CHECK(d && strstr(d->kinds, "audio") && strstr(d->kinds, "midi") && !strcmp(d->drivers, "snd-usb-audio") && d->interfaces == 2 && d->unbound == 0, "usb: the MIDI keyboard is audio and MIDI (in either order: a directory is not sorted)");
    d = ut_find(u, "1-3");
    CHECK(d && d->hub && !strcmp(d->kinds, "hub"), "usb: the hub");
    d = ut_find(u, "1-4");
    CHECK(d && d->unbound == 1 && !strcmp(d->kinds, "other") && !d->drivers[0], "usb: a device with no driver");
    d = ut_find(u, "1-5");
    CHECK(d && !strcmp(d->kinds, "network") && !strcmp(d->drivers, "asix"), "usb: Ethernet from its driver");
    d = ut_find(u, "1-6");
    CHECK(d && !strcmp(d->kinds, "pad"), "usb: a HID with a joystick node is a pad");
    d = ut_find(u, "1-7");
    CHECK(d && !strcmp(d->kinds, "storage"), "usb: storage");
    CHECK(u->n_conn == 3, "usb: three connectors");
    int dsi = 0, hdmi = 0, dvi = 0;
    for (int i = 0; i < u->n_conn; i++) {
        const UioConn *c = &u->usb_conn[i];
        if (!strcmp(c->name, "DSI-1")) dsi = c->internal && !strcmp(c->status, "connected") && !strcmp(c->mode, "640x480") && !c->usb;
        if (!strcmp(c->name, "HDMI-A-1")) hdmi = !c->internal && !strcmp(c->status, "disconnected") && !c->usb;
        if (!strcmp(c->name, "DVI-I-1")) dvi = !c->internal && !strcmp(c->status, "connected") && !strcmp(c->mode, "1920x1080") && c->usb;
    }
    CHECK(dsi && hdmi && dvi, "usb: the panel is internal, HDMI is empty, the DVI adapter on USB is connected at 1920x1080");
    d = ut_find(u, "1-9");
    CHECK(d && !strcmp(d->kinds, "billboard") && d->unbound == 0, "usb: a Billboard device is a notice, not a missing driver");
    CHECK(u->n_disk == 2 && u->usb_disk[1].bytes == 0 && !strcmp(u->usb_disk[1].dev, "sdb"), "usb: an empty card reader slot has no size");
    CHECK(u->n_disk == 2 && !strcmp(u->usb_disk[0].dev, "sda") && !strcmp(u->usb_disk[0].model, "SanDisk Ultra") && u->usb_disk[0].bytes == 15564800ULL * 512 &&
          !strcmp(u->usb_disk[0].mount, "/media/My Stick") && !strcmp(u->usb_disk[0].fstype, "vfat") && !strcmp(u->usb_disk[0].part, "sda1"),
          "usb: the stick, its model, size and mount (the SD card is not USB)");
    CHECK(u->n_net == 2, "usb: two adapters (not lo)");
    int eth = 0, wifi = 0;
    for (int i = 0; i < u->n_net; i++) {
        const UioNet *n = &u->usb_net[i];
        if (!strcmp(n->name, "eth1")) eth = !n->wifi && n->usb && n->carrier == 1 && n->speed == 1000 && !strcmp(n->state, "up") && !strcmp(n->mac, "aa:bb:cc:dd:ee:ff") && n->rx == 1000 && n->tx == 2000;
        if (!strcmp(n->name, "wlan0")) wifi = n->wifi && n->usb && !strcmp(n->state, "down") && n->carrier == -1;
    }
    CHECK(eth && wifi, "usb: Ethernet with a link at 1000 Mb/s, Wi-Fi down, both on USB");
    CHECK(u->n_midi == 1 && u->usb_midi[0].card == 1 && u->usb_midi[0].device == 0 && !strcmp(u->usb_midi[0].name, "Launchkey") && u->usb_midi[0].usb, "usb: the MIDI port");
    CHECK(u->n_in == 4 && u->usb_in[0].keyboard && u->usb_in[0].usb && u->usb_in[1].mouse && u->usb_in[2].pad && u->usb_in[3].mouse && !u->usb_in[3].keyboard,
          "usb: a keyboard, a mouse, a pad and a mouse with no mousedev handler, told by its X and Y axes (not the power key)");
    unsigned char on[3] = {0x90, 60, 100}, cc[3] = {0xB0, 7, 90}, off[3] = {0x80, 60, 0}, pc[2] = {0xC1, 5};
    char mt[64];
    uio_midi_text(on, 3, mt, sizeof mt); CHECK(!strcmp(mt, "ch1 note on C4 vel 100"), "usb: a note on in words");
    uio_midi_text(off, 3, mt, sizeof mt); CHECK(!strcmp(mt, "ch1 note off C4"), "usb: a note off in words");
    uio_midi_text(cc, 3, mt, sizeof mt); CHECK(!strcmp(mt, "ch1 control 7 = 90"), "usb: a controller in words");
    uio_midi_text(pc, 2, mt, sizeof mt); CHECK(!strcmp(mt, "ch2 program 5"), "usb: a program change in words");

    /* the pages: the list, then each sub-page's rows */
    u->page = P_USB;
    R3Screen sc;
    compose(u, &sc, 0);
    CHECK(screen_has(&sc, "USB and OTG") && screen_has(&sc, "Game controllers") && screen_has(&sc, "9 found") && screen_has(&sc, "1 connected"), "usb: the list shows what was found");
    u->page = P_USBSUB;
    u->usb_sub = U_DEVICES; compose(u, &sc, 0);
    CHECK(u->n_rows == 9 && ut_rows_have(u, "NO DRIVER") && ut_rows_have(u, "Mystery Gadget") && screen_has(&sc, "USB Keyboard"), "usb: nine device rows, the one with no driver says so");
    u->usb_sub = U_VIDEO; compose(u, &sc, 0);
    CHECK(u->n_rows == 4 && ut_rows_have(u, "A dock asked for video") && ut_rows_have(u, "connected 1920x1080") && ut_rows_have(u, "(USB adapter)"), "usb: video rows");
    u->usb_sub = U_STORAGE; compose(u, &sc, 0);
    CHECK(u->n_rows == 4 && ut_rows_have(u, "SanDisk Ultra") && ut_rows_have(u, "mounted at /media/My Stick") && ut_rows_have(u, "7.968") == 0 && ut_rows_have(u, "8.0 GB"), "usb: storage rows");
    u->usb_sub = U_NETWORK; compose(u, &sc, 0);
    CHECK(u->n_rows == 2 && ut_rows_have(u, "up 1000 Mb/s") && ut_rows_have(u, "Ethernet (USB)") && ut_rows_have(u, "Wi-Fi (USB)"), "usb: network rows");
    u->usb_sub = U_KEYBOARD; compose(u, &sc, 0);
    CHECK(ut_rows_have(u, "Logitech USB Keyboard") && ut_rows_have(u, "Different keys pressed") && !ut_rows_have(u, "rk8xx"), "usb: keyboard rows");
    u->usb_sub = U_PADS; compose(u, &sc, 0);
    CHECK(u->n_rows == 0 && screen_has(&sc, "No other game controller"), "usb: no controller opened yet: the page says what to do");

    const char *shots = getenv("R3I6NOS_IO_SHOTS");
    if (shots && *shots) {
        char f[PATH_MAX + 64];
        r3_report_mkdirs(shots);
        u->page = P_HOME; u->sel[P_HOME] = H_USB; compose(u, &sc, 0); snprintf(f, sizeof f, "%s/home.bmp", shots); save_bmp(&sc, f);
        u->page = P_USB; compose(u, &sc, 0); snprintf(f, sizeof f, "%s/usb.bmp", shots); save_bmp(&sc, f);
        u->page = P_USBSUB;
        for (int sub = 0; sub < U_COUNT; sub++) {
            u->usb_sub = sub; u->sel[P_USBSUB] = 0;
            if (sub == U_PADS) { u->n_ext = 1; snprintf(u->ext[0].name, sizeof u->ext[0].name, "Xbox 360 Controller"); u->ext[0].nb = 11; u->ext[0].na = 6; u->ext[0].nh = 1;
                                 u->ext[0].seen[1] = u->ext[0].seen[2] = u->ext[0].seen[5] = 1; u->ext[0].down[2] = 1; u->ext[0].axis[1] = -0.5f; u->ext[0].axis[2] = 0.99f; }
            if (sub == U_KEYBOARD) { u->kb_n = 3; snprintf(u->kb_log[0], sizeof u->kb_log[0], "Space"); snprintf(u->kb_log[1], sizeof u->kb_log[1], "B"); snprintf(u->kb_log[2], sizeof u->kb_log[2], "A"); }
            if (sub == U_MOUSE) { u->m_x = 120; u->m_y = -40; u->m_moves = 31; u->m_buttons = 2 | 8; u->m_wheel = 3; }
            if (sub == U_MIDI) { u->midi_in_msgs = 2; u->midi_out_msgs = 1; snprintf(u->midi_log[0], sizeof u->midi_log[0], "in  ch1 note off C4"); snprintf(u->midi_log[1], sizeof u->midi_log[1], "in  ch1 note on C4 vel 100"); snprintf(u->midi_log[2], sizeof u->midi_log[2], "out ch1 note on C4 vel 100"); }
            compose(u, &sc, 0);
            snprintf(f, sizeof f, "%s/usb-%d.bmp", shots, sub);
            save_bmp(&sc, f);
            if (sub == U_PADS) { u->n_ext = 0; memset(u->ext, 0, sizeof u->ext); }
        }
        u->midi_in_msgs = u->midi_out_msgs = 0; u->kb_n = 0; memset(u->kb_log, 0, sizeof u->kb_log); memset(u->midi_log, 0, sizeof u->midi_log);
        u->m_x = u->m_y = u->m_moves = u->m_wheel = 0; u->m_buttons = 0;
    }

    /* MIDI: open the port, send middle C, see it come back, then the note off */
    u->usb_sub = U_MIDI;
    u->sel[P_USBSUB] = 0;
    midi_open_sel(u);
    CHECK(u->midi_fd >= 0 && u->midi_idx == 0, "usb: the MIDI port opens");
    midi_send(u, 1000);
    CHECK(u->midi_out_msgs == 1 && u->midi_off_at == 1300, "usb: A sends middle C and schedules the note off");
    midi_poll(u);
    CHECK(u->midi_in_msgs == 1 && strstr(u->midi_log[0], "ch1 note on C4 vel 100"), "usb: the note came back in, in words");
    usb_tick(u, 1400);
    midi_poll(u);
    CHECK(u->midi_off_at == 0 && u->midi_in_msgs == 2 && strstr(u->midi_log[0], "note off C4"), "usb: the note off was sent and read");
    compose(u, &sc, 0);
    CHECK(ut_rows_have(u, "Launchkey") && ut_rows_have(u, "open, 6 bytes in") && ut_rows_have(u, "note on C4") && ut_rows_have(u, "1 out, 2 in"), "usb: the MIDI page shows the port and the messages");
    midi_close(u);
    CHECK(u->midi_fd == -1, "usb: closed");

    /* a keyboard: each different key counts once; Esc goes back */
    SDL_Event ev;
    u->page = P_USBSUB; u->usb_sub = U_KEYBOARD; u->kb_n = 0;
    SDL_zero(ev);
    ev.type = SDL_KEYDOWN; ev.key.keysym.sym = SDLK_a; ev.key.keysym.scancode = SDL_SCANCODE_A;
    handle(u, &ev, 100);
    handle(u, &ev, 120);
    ev.key.keysym.sym = SDLK_b; ev.key.keysym.scancode = SDL_SCANCODE_B;
    handle(u, &ev, 140);
    CHECK(u->kb_n == 2 && u->page == P_USBSUB && !strcmp(u->kb_log[0], "B") && !strcmp(u->kb_log[1], "A"), "usb: two different keys counted, the last in the log, no key acted on the page");
    ev.key.keysym.sym = SDLK_h; ev.key.keysym.scancode = SDL_SCANCODE_H;
    handle(u, &ev, 160);
    CHECK(u->kb_n == 3 && u->answers[0] == 0, "usb: H on this page is a key under test, not 'heard'");
    ev.key.keysym.sym = SDLK_ESCAPE; ev.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
    handle(u, &ev, 180);
    CHECK(u->page == P_USB, "usb: Esc goes back to the list");

    /* a mouse */
    u->page = P_USBSUB; u->usb_sub = U_MOUSE;
    SDL_zero(ev);
    ev.type = SDL_MOUSEMOTION; ev.motion.xrel = 5; ev.motion.yrel = -3;
    handle(u, &ev, 200); handle(u, &ev, 210);
    SDL_zero(ev); ev.type = SDL_MOUSEBUTTONDOWN; ev.button.button = SDL_BUTTON_LEFT; handle(u, &ev, 220);
    SDL_zero(ev); ev.type = SDL_MOUSEWHEEL; ev.wheel.y = 2; handle(u, &ev, 230);
    CHECK(u->m_x == 10 && u->m_y == -6 && u->m_moves == 2 && (u->m_buttons & 2) && u->m_wheel == 2, "usb: a mouse's motion, left button and wheel");
    compose(u, &sc, 0);
    CHECK(ut_rows_have(u, "x 10  y -6") && ut_rows_have(u, "left") && ut_rows_have(u, "Optical Mouse"), "usb: mouse rows");
    u->usb_sub = U_MOUSE; u->page = P_USBSUB;

    /* the home list reaches it: Down x4 and A (USB and OTG), A (Devices), B, B */
    u->page = P_HOME;
    u->sel[P_HOME] = 0;
    Uint32 t = 1000;
    SDL_Event e2;
#define UPRESS(btn) do { e2 = joy(SDL_JOYBUTTONDOWN, btn); handle(u, &e2, t); e2 = joy(SDL_JOYBUTTONUP, btn); handle(u, &e2, t); t += 100; } while (0)
    UPRESS(L_DOWN); UPRESS(L_DOWN); UPRESS(L_DOWN); UPRESS(L_DOWN);
    CHECK(u->sel[P_HOME] == H_USB && !strcmp(HOME_LABEL[u->sel[P_HOME]], "USB and OTG"), "usb: USB and OTG is the fifth entry");
    UPRESS(L_A);
    CHECK(u->page == P_USB, "usb: A opens the USB list");
    UPRESS(L_DOWN); UPRESS(L_DOWN); UPRESS(L_A);
    CHECK(u->page == P_USBSUB && u->usb_sub == U_MIDI && u->midi_fd >= 0, "usb: A opens MIDI and the port is open");
    UPRESS(L_B);
    CHECK(u->page == P_USB && u->midi_fd == -1, "usb: B goes back and closes the port");
    UPRESS(L_B);
    CHECK(u->page == P_HOME, "usb: B goes home");
#undef UPRESS

    /* a pad that is not the built-in one: its events go to the USB page, never to the built-in pad's record */
    App *x = calloc(1, sizeof(App));
    if (x) {
        app_init(x);
        x->js = (SDL_Joystick *)1;      /* the built-in pad, pretend; instance id 0 */
        x->js_id = 0;
        x->n_ext = 1;
        x->ext[0].js = (SDL_Joystick *)1;
        x->ext[0].id = 9;
        x->ext[0].nb = 17;
        snprintf(x->ext[0].name, sizeof x->ext[0].name, "USB Pad");
        SDL_zero(ev);
        ev.type = SDL_JOYBUTTONDOWN; ev.jbutton.which = 9; ev.jbutton.button = 1;
        handle(x, &ev, 100);
        CHECK(x->ext[0].seen[2] && x->ext[0].down[2] && !x->down[2] && x->presses[2] == 0, "usb: a USB pad's button is its own, not the built-in pad's");
        CHECK(foreign_pad(x, 9) && !foreign_pad(x, 0), "usb: only another pad's events are foreign (the built-in pad is instance 0)");
        x->js = NULL;
        CHECK(!foreign_pad(x, 9), "usb: with no built-in pad, nothing is foreign (a first pad is the built-in one)");
        x->js = (SDL_Joystick *)1;
        x->ext[0].js = NULL;            /* never passed to SDL */
        free(x);
    }

    /* the report's sections */
    R3Report rep;
    memset(&rep, 0, sizeof rep);
    rep.quiet = 1;
    char rdir[PATH_MAX + 16];
    snprintf(rdir, sizeof rdir, "%s/report", root);
    CHECK(r3_report_open(&rep, rdir, "USB test", "1", "usb-test", "selftest") == 0, "usb: the report opens");
    u->midi_in_msgs = 2; u->midi_out_msgs = 1; u->kb_n = 3; u->m_moves = 2; u->m_buttons = 2; u->m_wheel = 2;
    report_usb(u, &rep);
    CHECK(r3_report_close(&rep) == 0, "usb: the report closes");
    char rpath[PATH_MAX + 64], text[16384];
    snprintf(rpath, sizeof rpath, "%s/r36s-report.txt", rdir);
    FILE *rf = fopen(rpath, "r");
    size_t got = rf ? fread(text, 1, sizeof text - 1, rf) : 0;
    text[got] = 0;
    if (rf) fclose(rf);
    const char *want[] = {
        "== usb devices ==", "INFO devices: 9 on USB", "INFO device 1-1: USB Keyboard (046d:c31c), 12 Mb/s: keyboard; drivers: usbhid",
        "FAIL driver 1-4: 1 of 1 interface(s) of Mystery Gadget have no driver",
        "== usb video out ==", "INFO connector DSI-1: connected, 640x480 (this unit's screen)", "PASS external display: connected on DVI-I-1, first mode 1920x1080",
        "== usb displaylink ==", "INFO adapter: 1-10 USB Graphics Adapter (DisplayLink, vendor 17e9)", "PASS udl loaded: module loaded",
        "PASS udl module file: ", "PASS kernel config DRM_UDL: CONFIG_DRM_UDL=m", "INFO kernel config FB_UDL: not set", "PASS drm device: card1 is behind USB and bound to udl",
        "INFO udl verdict: working: a DisplayLink adapter made a video card",
        "== usb midi ==", "PASS Launchkey opens: ", "PASS messages: 2 in, 1 out",
        "== usb storage ==", "INFO billboard 1-9: USB 2.0 BILLBOARD: a USB-C dock is asking for video", "SKIP sdb mounted: no media in it", "INFO disk sda: SanDisk Ultra, 8.0 GB", "PASS sda mounted: sda1 on /media/My Stick (vfat)",
        "== usb network ==", "PASS eth1: Ethernet (USB): link up, 1000 Mb/s", "INFO wlan0: Wi-Fi (USB), down",
        "== usb keyboard ==", "INFO keyboard: Logitech USB Keyboard (USB)", "PASS keys pressed: 3 different keys reached SDL",
        "== usb mouse ==", "INFO mouse: Optical Mouse (USB)", "PASS mouse events: 2 moves, buttons left",
        "== usb pads ==", "SKIP pads: no other game controller",
    };
    for (size_t i = 0; i < sizeof want / sizeof *want; i++) {
        char m[200];
        snprintf(m, sizeof m, "usb report says: %s", want[i]);
        CHECK(strstr(text, want[i]) != NULL, m);
    }
    /* DisplayLink probes against other kernels: built in (modules.builtin and a plain /boot config), udl off, and nothing readable at all */
    {
        char none[PATH_MAX + 16], mods2[PATH_MAX + 32], boot2[PATH_MAX + 16];
        snprintf(none, sizeof none, "%s/none", root);
        snprintf(mods2, sizeof mods2, "%s/mods2", root);
        snprintf(boot2, sizeof boot2, "%s/boot2", root);
        ut_file(root, "mods2/4.4.189/modules.builtin", "kernel/drivers/gpu/drm/drm.ko\nkernel/drivers/gpu/drm/udl/udl.ko\n");
        ut_file(root, "boot2/config-4.4.189", "CONFIG_DRM_UDL=y\nCONFIG_FB_UDL=m\n");
        setenv("R3I6NOS_PROC", none, 1); setenv("R3I6NOS_SYS", none, 1); setenv("R3I6NOS_MODULES", mods2, 1); setenv("R3I6NOS_BOOT", boot2, 1);
        UioUdl bu;
        uio_udl_probe(&bu, NULL, 0);
        CHECK(!bu.loaded && bu.file && strstr(bu.file_how, "built in") && !strcmp(bu.cfg_drm, "y") && !strcmp(bu.cfg_fb, "m") && bu.cfg_read && strstr(bu.cfg_src, "boot2/config-4.4.189"),
              "udl: built in (modules.builtin, CONFIG_DRM_UDL=y from /boot), not loaded here, the framebuffer driver a module");
        ut_file(root, "boot2/config-4.4.189", "CONFIG_DRM=y\n# CONFIG_DRM_UDL is not set\n");
        uio_udl_probe(&bu, NULL, 0);
        CHECK(!strcmp(bu.cfg_drm, "not set") && !bu.cfg_fb[0] && bu.cfg_read, "udl: a config with udl switched off says so");
        ut_file(root, "boot2/config-4.4.189", "CONFIG_DRM=y\n");
        uio_udl_probe(&bu, NULL, 0);
        CHECK(bu.cfg_read && !bu.cfg_drm[0], "udl: a config that never mentions udl reads as not there");
        setenv("R3I6NOS_MODULES", none, 1); setenv("R3I6NOS_BOOT", none, 1);
        uio_udl_probe(&bu, NULL, 0);
        CHECK(!bu.loaded && !bu.file && !bu.cfg_read && !bu.adapter && bu.drm_usb == 0, "udl: nothing readable: not loaded, no file, no config, no adapter, no card");
        UioDev dl;
        memset(&dl, 0, sizeof dl);
        snprintf(dl.name, sizeof dl.name, "1-2");
        snprintf(dl.vidpid, sizeof dl.vidpid, "17e9:4301");
        snprintf(dl.product, sizeof dl.product, "DL-165");
        uio_udl_probe(&bu, &dl, 1);
        CHECK(bu.adapter && !strcmp(bu.adapter_product, "DL-165") && !bu.drm_udl, "udl: an adapter plugged in and no card behind it is told apart");
    }
    /* nothing plugged in: every section SKIPs, none FAILs */
    unsetenv("R3I6NOS_SYS"); unsetenv("R3I6NOS_PROC"); unsetenv("R3I6NOS_DEV");
    char empty[PATH_MAX + 16];
    snprintf(empty, sizeof empty, "%s/empty", root);
    setenv("R3I6NOS_SYS", empty, 1); setenv("R3I6NOS_PROC", empty, 1); setenv("R3I6NOS_DEV", empty, 1); setenv("R3I6NOS_MODULES", empty, 1); setenv("R3I6NOS_BOOT", empty, 1);
    App *z = calloc(1, sizeof(App));
    if (z) {
        app_init(z);
        R3Report r2;
        memset(&r2, 0, sizeof r2);
        r2.quiet = 1;
        snprintf(rdir, sizeof rdir, "%s/report2", root);
        if (r3_report_open(&r2, rdir, "USB test", "1", "usb-test", "selftest") == 0) {
            report_usb(z, &r2);
            CHECK(r2.fail == 0 && r2.skip >= 8, "usb: with nothing plugged in the sections SKIP and nothing FAILs");
            r3_report_close(&r2);
        }
        z->page = P_USBSUB;
        for (int sub = 0; sub < U_COUNT; sub++) { z->usb_sub = sub; compose(z, &sc, 0); }   /* every empty page draws */
        z->usb_sub = U_MIDI; compose(z, &sc, 0);
        CHECK(screen_has(&sc, "No MIDI port."), "usb: an empty MIDI page says what to plug in");
        free(z);
    }
    unsetenv("R3I6NOS_SYS"); unsetenv("R3I6NOS_PROC"); unsetenv("R3I6NOS_DEV");
    free(u);
    char cmd[PATH_MAX + 16];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (system(cmd) != 0) fprintf(stderr, "io-checker selftest: could not remove %s\n", root);
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
    for (int i = 0; i < tone_frames(&TONES[4]); i += 997) { tone_sample(&TONES[4], i, &l, &r); CHECK(fabs(l) <= 0.5 && l == r, "sweep in range"); }
    CHECK(!strcmp(TONES[4].id, "sweep") && TONES[3].freq == 440 && TONES[0].freq == 1000, "tones: 1 kHz, then low and sweep");
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
        /* a whole GUID survives into the item (the report copies items; on the unit it lost its last digit), and a value too long
         * for the row is cut inside the frame, its label kept */
        add_sys(a, "Long GUID", "%s", "1900bb3e4b4800000011000000010000");
        CHECK(!strcmp(a->sys[a->nsys - 1].value, "1900bb3e4b4800000011000000010000"), "a GUID is not cut");
        add_sys(a, "Very long", "%s", "0123456789012345678901234567890123456789012345678901234567890");
        a->sel[P_SYSTEM] = a->nsys - 1; compose(a, &s, 0);
        {
            char rows[R3_ROWS][R3_COLS + 1];
            r3_dump(&s, rows);
            int found = 0;
            for (int i = 0; i < R3_ROWS; i++) if (strstr(rows[i], "Very long")) found = 1;
            CHECK(found, "a long value leaves its label");
        }
        a->sel[P_SYSTEM] = 0;
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
    selftest_usb();
    /* the loop's pacing (CLAUDE.md rule 14): a frame while something changes, a long nap when nothing does */
    {
        R3Video v;
        memset(&v, 0, sizeof v);
        a->force = 0; a->pending = 0; a->chord_since = 0; a->page = P_HOME;
        r3_toast_init(&a->toast);
        CHECK(nap_ms(a, &v) == IDLE_NAP_MS, "idle: the long nap");
        a->force = 1;
        CHECK(nap_ms(a, &v) == 16, "a redraw owed: a frame");
        a->force = 0; v.settle = 2;
        CHECK(nap_ms(a, &v) == 16, "a change still being presented: a frame");
        v.settle = 0; a->chord_since = 5;
        CHECK(nap_ms(a, &v) == 16, "the exit chord counting up: a frame");
        a->chord_since = 0; a->pending = H_REPORT;
        CHECK(nap_ms(a, &v) == 16, "a job about to run: a frame");
        a->pending = 0;
        CHECK(nap_ms(a, &v) == IDLE_NAP_MS && IDLE_NAP_MS >= 40, "back to idle; at most ~25 wake-ups a second");
        a->page = P_INPUT;
        CHECK(nap_ms(a, &v) == 16, "the button test is a live page: a frame");
        a->page = P_USBSUB; a->usb_sub = U_KEYBOARD;
        CHECK(nap_ms(a, &v) == 16, "the keyboard test is a live page: a frame");
        a->usb_sub = U_DEVICES;
        CHECK(nap_ms(a, &v) == IDLE_NAP_MS, "the device list is quiet: the long nap");
        a->page = P_SYSTEM;
        CHECK(nap_ms(a, &v) == IDLE_NAP_MS, "the system page is quiet: the long nap");
        a->page = P_HOME;
    }
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
    const char *names[] = {"home", "input", "video", "pattern", "sound", "files", "system", "usb", "usbsub"};
    for (int i = 0; i < P_COUNT; i++) if (page && !strcmp(page, names[i])) a->page = i;
    if (a->page == P_SYSTEM) system_rows(a);
    if (a->page == P_FILES) run_files(a);
    if (a->page == P_USB || a->page == P_USBSUB) usb_scan(a, 0);
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
    r3_toast_init(&a->toast);
    a->idle_since = SDL_GetTicks();
    if (SDL_NumJoysticks() > 0) open_pad(a, 0);
    video_info(a, win, r);
    sound_status(a);
    R3Screen screen;
    struct Loop loop;
    loop.r = r;
    loop.video = &video;
    a->loop = &loop;

    if (selftest) {
        compose(a, &screen, SDL_GetTicks());
        r3_video_draw(&video, r, &screen, 1);
        SDL_version v;
        SDL_GetVersion(&v);
        printf("R3I6NOS_SMOKE ok io-checker-native=%s sdl=%d.%d.%d joysticks=%d renderer=%s\n", VERSION, v.major, v.minor, v.patch,
               SDL_NumJoysticks(), SDL_GetCurrentVideoDriver());
        /* the windowed report, with short power windows, into a throwaway folder: the power section must be in it */
        /* the output is open only while a tone plays: an open device keeps a thread waking ~40 times a second */
        play(a, 0);
        if (!a->audio_error) {
            CHECK(a->dev != 0 && a->audio_close_at != 0, "a tone opens the device and schedules its close");
            tick(a, a->audio_close_at - 1);
            CHECK(a->dev != 0, "the device stays open until the tone has played out");
            tick(a, a->audio_close_at + 1);
            CHECK(a->dev == 0 && a->audio_close_at == 0, "the device is closed once the tone is over");
        }
        char tmpl[PATH_MAX];
        snprintf(tmpl, sizeof tmpl, "%s/io-checker-selftest-XXXXXX", env_or("TMPDIR", "/tmp"));
        char *dir = mkdtemp(tmpl);
        if (dir) {
            setenv("R3I6NOS_ROMS", dir, 1);
            setenv("R3I6NOS_REPORT_POWER_S", "0.2", 1);
            write_report(a, "selftest");
            char path[PATH_MAX + 64], text[24576] = "";
            snprintf(path, sizeof path, "%s/IO_Checker/r36s-report-native.txt", dir);
            FILE *f = fopen(path, "r");
            size_t n = f ? fread(text, 1, sizeof text - 1, f) : 0;
            text[n] = 0;
            if (f) fclose(f);
            CHECK(strstr(text, "== power ==") && strstr(text, "idle cpu: ") && strstr(text, "working cpu: ") && strstr(text, "cut first: "), "the windowed report has its power section");
            printf("R3I6NOS_SMOKE power-report %s\n", strstr(text, "working cpu: ") ? "ok" : "missing");
            char cmd[PATH_MAX + 16];
            snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
            if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", dir);
        }
        if (bad) { fprintf(stderr, "io-checker: %d selftest check(s) failed\n", bad); return 1; }
    }

    Uint64 freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    while (!selftest && !a->quit) {
        Uint32 now = SDL_GetTicks();
        SDL_Event e;
        while (SDL_PollEvent(&e)) handle(a, &e, now);
        tick(a, now);
        r3_toast_poll(&a->toast, now);
        if (a->page == P_PATTERN) {
            draw_pattern(a, r); /* vsync paces this loop */
            Uint64 t = SDL_GetPerformanceCounter();
            if (a->sel[P_VIDEO] == PAT_MOTION) frames_add(a, (double)(t - last) / (double)freq);
            last = t;
            continue;
        }
        draw_frame(a, now);
        if (a->pending && video.settle == 0) { /* after "Testing..." has been presented in full: the job blocks for seconds */
            int job = a->pending;
            a->pending = 0;
            if (job == H_FILES) run_files(a);
            else write_report(a, "Device report (native)");
            continue;
        }
        SDL_Delay(nap_ms(a, &video));
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

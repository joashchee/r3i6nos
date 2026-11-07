/* R36S Input Checker, native ARM64 build (SDL2 only; its own 5x7 font, no font files).
 *
 * Same job and layout as the LÖVE versions (docs/R36S_Input_Checker_Specification.md): every raw
 * button, axis and the hat as the pad reports them, plus SDL's gamepad mapping when it has one.
 * Exit: hold Select+Start for 1 s, Esc, or 15 s without input.
 * Button numbers on screen start at 1, as LÖVE counts them (SDL's own index is one less).
 *
 *   input-checker-native               run it
 *   input-checker-native --selftest    open SDL, draw one frame, print one line, exit 0 (CI, under qemu)
 *   input-checker-native --screenshot out.bmp   draw a demo frame to a file (development)
 *
 * Build: scripts/build_native_apps.sh (cross-compiles for arm64 and packs the .deb).
 */
#include <SDL.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846 /* strict -std=c11 on glibc leaves it out */
#endif

#define W 640
#define H 480
#define VERSION "1.0"
#define EXIT_HOLD_MS 1000
#define IDLE_LIMIT_MS 15000
#define IDLE_WARN_MS 10000
#define ZONE 16384 /* an axis counts as pushed past half travel */
#define MAX_AXES 8
#define MAX_BUTTONS 32
#define LOG_LINES 6

/* The R36S's raw button indices (SDL, from 0), measured on a real unit on 2026-11-07: the D-pad is four
 * buttons, not a hat, and the pad has no SDL gamepad mapping. Shoulders and triggers (4-7) are a guess. */
enum { B_A = 0, B_B = 1, B_X = 2, B_Y = 3, B_L1 = 4, B_R1 = 5, B_L2 = 6, B_R2 = 7,
       B_LEFT = 8, B_DOWN = 9, B_UP = 10, B_RIGHT = 11, B_FN = 12, B_SELECT = 13, B_L3 = 14, B_R3 = 15, B_START = 16 };

typedef struct { Uint8 r, g, b; } Col;
static const Col BG = {24, 24, 27}, PANEL = {36, 36, 46}, DIM = {76, 76, 84}, ACTIVE = {34, 197, 94},
                 ACCENT = {56, 189, 248}, TEXT = {217, 217, 217}, MUTE = {140, 140, 153},
                 BAD = {255, 89, 89}, WARM = {250, 158, 77}, HEAD = {38, 38, 46}, FOOT = {64, 64, 77};

/* 5x7 font, ASCII 32..126, one byte per column, bit 0 is the top row (the public-domain "glcd" font). */
static const unsigned char FONT[95][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x56,0x20,0x50},{0x00,0x08,0x07,0x03,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x2A,0x1C,0x7F,0x1C,0x2A},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x80,0x70,0x30,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x00,0x60,0x60,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x72,0x49,0x49,0x49,0x46},{0x21,0x41,0x49,0x4D,0x33},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x31},{0x41,0x21,0x11,0x09,0x07},
    {0x36,0x49,0x49,0x49,0x36},{0x46,0x49,0x49,0x29,0x1E},{0x00,0x00,0x14,0x00,0x00},{0x00,0x40,0x34,0x00,0x00},
    {0x00,0x08,0x14,0x22,0x41},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x59,0x09,0x06},
    {0x3E,0x41,0x5D,0x59,0x4E},{0x7C,0x12,0x11,0x12,0x7C},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x41,0x3E},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x41,0x51,0x73},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x1C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x26,0x49,0x49,0x49,0x32},
    {0x03,0x01,0x7F,0x01,0x03},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x03,0x04,0x78,0x04,0x03},{0x61,0x59,0x49,0x4D,0x43},{0x00,0x7F,0x41,0x41,0x41},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x41,0x7F},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x03,0x07,0x08,0x00},{0x20,0x54,0x54,0x78,0x40},{0x7F,0x28,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x28},
    {0x38,0x44,0x44,0x28,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x00,0x08,0x7E,0x09,0x02},{0x18,0xA4,0xA4,0x9C,0x78},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x40,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x78,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0xFC,0x18,0x24,0x24,0x18},{0x18,0x24,0x24,0x18,0xFC},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x24},
    {0x04,0x04,0x3F,0x44,0x24},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},{0x4C,0x90,0x90,0x90,0x7C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x77,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x02,0x01,0x02,0x04,0x02}};

typedef struct {
    SDL_Joystick *js;
    SDL_GameController *gc;
    char name[64], guid[40];
    int naxes, nbuttons, nhats;
    Sint16 axis[MAX_AXES];
    Uint8 button[MAX_BUTTONS], seen_button[MAX_BUTTONS];
    Uint8 hat;
    int seen_axes, seen_hat_dirs;
    int key_vol_down, key_vol_up, key_power; /* power and the volume keys arrive as keyboard keys */
    unsigned seq;
    char log[LOG_LINES][48];
    char last_key[24];
    Uint32 idle_since, chord_since;
} State;

static void logf_(State *s, const char *fmt, ...) {
    memmove(s->log[1], s->log[0], sizeof(s->log[0]) * (LOG_LINES - 1));
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s->log[0], sizeof(s->log[0]), fmt, ap);
    va_end(ap);
}

/* One event line: a sequence number, what happened, and what the pad says right now. A DOWN that says
 * "free" (or an UP that says "held") would mean the event stream and the state disagree. */
static void event(State *s, int is_held, const char *fmt, ...) {
    char what[32];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(what, sizeof what, fmt, ap);
    va_end(ap);
    if (is_held < 0) logf_(s, "#%u %s", ++s->seq, what); /* keys have no state to compare with */
    else logf_(s, "#%u %s (%s)", ++s->seq, what, is_held ? "held" : "free");
}

/* ---- drawing ---- */
static void color(SDL_Renderer *r, Col c) { SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255); }
static void rect(SDL_Renderer *r, Col c, int x, int y, int w, int h) {
    SDL_Rect q = {x, y, w, h};
    color(r, c);
    SDL_RenderFillRect(r, &q);
}
static void disc(SDL_Renderer *r, Col c, int cx, int cy, int rad) {
    color(r, c);
    for (int dy = -rad; dy <= rad; dy++) {
        int dx = (int)sqrt((double)(rad * rad - dy * dy));
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}
static void ring(SDL_Renderer *r, Col c, int cx, int cy, int rad) {
    color(r, c);
    for (int a = 0; a < 360; a += 2)
        SDL_RenderDrawPoint(r, cx + (int)lround(rad * cos(a * M_PI / 180)), cy + (int)lround(rad * sin(a * M_PI / 180)));
}
static int text_w(const char *t, int sc) { return (int)strlen(t) * 6 * sc; }
static void text(SDL_Renderer *r, Col c, int x, int y, int sc, const char *t) {
    color(r, c);
    for (; *t; t++, x += 6 * sc) {
        unsigned ch = (unsigned char)*t;
        if (ch < 32 || ch > 126) ch = '?';
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 8; row++)
                if (FONT[ch - 32][col] & (1 << row)) {
                    SDL_Rect q = {x + col * sc, y + row * sc, sc, sc};
                    SDL_RenderFillRect(r, &q);
                }
    }
}
static void textf(SDL_Renderer *r, Col c, int x, int y, int sc, const char *fmt, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    text(r, c, x, y, sc, buf);
}
static void textc(SDL_Renderer *r, Col c, int cx, int y, int sc, const char *t) {
    text(r, c, cx - text_w(t, sc) / 2, y, sc, t);
}

static int held(const State *s, int i) { return i < MAX_BUTTONS && s->button[i]; }
static int gcb(const State *s, SDL_GameControllerButton b) {
    return s->gc && SDL_GameControllerGetButton(s->gc, b);
}
/* Select+Start: the raw buttons (the pad has no mapping), or SDL's mapping if one exists. */
static int exit_held(const State *s) {
    return (held(s, B_SELECT) && held(s, B_START)) || (gcb(s, SDL_CONTROLLER_BUTTON_BACK) && gcb(s, SDL_CONTROLLER_BUTTON_START));
}

static void round_btn(SDL_Renderer *r, int x, int y, const char *label, int on, int rad) {
    disc(r, on ? ACTIVE : DIM, x, y, rad);
    textc(r, on ? BG : TEXT, x, y - 7, 2, label);
}
/* A stick: the whole circle lights up while it is pressed in (L3 / R3). */
static void stick(SDL_Renderer *r, int cx, int cy, Sint16 ax, Sint16 ay, int pressed, Col tint, const char *label) {
    disc(r, pressed ? ACTIVE : PANEL, cx, cy, 38);
    ring(r, pressed ? ACTIVE : DIM, cx, cy, 38);
    disc(r, pressed ? BG : tint, cx + (int)(ax / 32768.0 * 26), cy + (int)(ay / 32768.0 * 26), 10);
    textc(r, pressed ? ACTIVE : MUTE, cx, cy + 42, 2, label);
}
static void pill(SDL_Renderer *r, int x, int y, int w, const char *label, int on) {
    rect(r, on ? ACTIVE : DIM, x, y, w, 24);
    textc(r, on ? BG : TEXT, x + w / 2, y + 4, 2, label);
}

static void draw(SDL_Renderer *r, State *s, Uint32 now, int have_pad) {
    color(r, BG);
    SDL_RenderClear(r);
    rect(r, HEAD, 0, 0, W, 45);
    text(r, ACTIVE, 16, 12, 3, "R36S INPUT CHECKER");
    char rt[40], nm[32];
    SDL_version v;
    SDL_GetVersion(&v);
    snprintf(rt, sizeof rt, "ARM64 SDL %d.%d.%d", v.major, v.minor, v.patch);
    text(r, MUTE, W - 12 - text_w(rt, 2), 3, 2, rt);
    snprintf(nm, sizeof nm, "%.24s", have_pad ? s->name : "no gamepad");
    text(r, MUTE, W - 12 - text_w(nm, 2), 24, 2, nm);

    if (!have_pad) {
        textc(r, BAD, W / 2, 190, 3, "NO GAMEPAD DETECTED");
        textc(r, MUTE, W / 2, 232, 2, "Press a button: this page");
        textc(r, MUTE, W / 2, 252, 2, "updates when a pad appears.");
    } else {
        int h = s->hat;
        /* shoulders and triggers: buttons 5-8 are an assumption; the telemetry shows what really arrives */
        round_btn(r, 36, 76, "L1", held(s, B_L1), 18);
        round_btn(r, 84, 76, "L2", held(s, B_L2), 18);
        round_btn(r, 246, 76, "R2", held(s, B_R2), 18);
        round_btn(r, 294, 76, "R1", held(s, B_R1), 18);
        /* D-pad: the four buttons, or a hat if the pad has one */
        int up = held(s, B_UP) || (h & SDL_HAT_UP), dn = held(s, B_DOWN) || (h & SDL_HAT_DOWN);
        int lf = held(s, B_LEFT) || (h & SDL_HAT_LEFT), rt_ = held(s, B_RIGHT) || (h & SDL_HAT_RIGHT);
        rect(r, PANEL, 47, 127, 96, 96);
        rect(r, up ? ACTIVE : DIM, 82, 131, 26, 32);
        rect(r, dn ? ACTIVE : DIM, 82, 187, 26, 32);
        rect(r, lf ? ACTIVE : DIM, 51, 162, 32, 26);
        rect(r, rt_ ? ACTIVE : DIM, 107, 162, 32, 26);
        disc(r, PANEL, 250, 175, 52);
        round_btn(r, 250, 143, "X", held(s, B_X), 17);
        round_btn(r, 250, 207, "A", held(s, B_A), 17);
        round_btn(r, 218, 175, "Y", held(s, B_Y), 17);
        round_btn(r, 282, 175, "B", held(s, B_B), 17);
        stick(r, 95, 300, s->axis[0], s->axis[1], held(s, B_L3), ACCENT, "L STICK");
        stick(r, 250, 300, s->axis[2], s->axis[3], held(s, B_R3), WARM, "R STICK");
        /* left to right: Select, Fn, Start */
        round_btn(r, 95, 380, "SEL", held(s, B_SELECT), 22);
        round_btn(r, 165, 380, "FN", held(s, B_FN), 22);
        round_btn(r, 235, 380, "STA", held(s, B_START), 22);

        /* telemetry */
        rect(r, PANEL, 340, 56, 284, 376);
        int y = 60;
        text(r, TEXT, 350, y, 2, "TELEMETRY");
        y += 26;
        char down[40] = "";
        int nseen = 0;
        for (int i = 0; i < s->nbuttons && i < MAX_BUTTONS; i++) {
            if (s->button[i] && strlen(down) < 14) {
                char one[8];
                snprintf(one, sizeof one, "%d ", i + 1);
                strncat(down, one, sizeof down - strlen(down) - 1);
            }
            nseen += s->seen_button[i];
        }
        textf(r, ACTIVE, 350, y, 2, "Down: %s", *down ? down : "none");
        y += 22;
        char hs[8] = ""; /* up to "udlr" */
        if (h & SDL_HAT_UP) strcat(hs, "u");
        if (h & SDL_HAT_DOWN) strcat(hs, "d");
        if (h & SDL_HAT_LEFT) strcat(hs, "l");
        if (h & SDL_HAT_RIGHT) strcat(hs, "r");
        textf(r, ACCENT, 350, y, 2, "Hat: %s", *hs ? hs : "c");
        y += 22;
        for (int i = 0; i < s->naxes && i < 4; i += 2, y += 22) {
            if (i + 1 < s->naxes && i + 1 < 4)
                textf(r, ACCENT, 350, y, 2, "A%d %+.2f A%d %+.2f", i + 1, s->axis[i] / 32768.0, i + 2, s->axis[i + 1] / 32768.0);
            else
                textf(r, ACCENT, 350, y, 2, "A%d %+.2f", i + 1, s->axis[i] / 32768.0);
        }
        textf(r, MUTE, 350, y, 2, "Seen %d/%d buttons", nseen, s->nbuttons);
        y += 22;
        textf(r, s->gc ? MUTE : WARM, 350, y, 2, "Gamepad map: %s", s->gc ? "yes" : "NO");
        y += 22;
        textf(r, MUTE, 350, y, 1, "GUID %s", s->guid);
        y += 16;
        text(r, TEXT, 350, y, 2, "LAST EVENTS");
        y += 20;
        for (int i = 0; i < LOG_LINES; i++, y += 18) {
            char clip[23];
            snprintf(clip, sizeof clip, "%s", s->log[i]);
            text(r, MUTE, 350, y, 2, clip);
        }
        if (s->chord_since) {
            Uint32 hold = now - s->chord_since;
            rect(r, PANEL, 16, 472, 608, 6);
            rect(r, WARM, 16, 472, (int)(608 * (hold > EXIT_HOLD_MS ? 1.0 : (double)hold / EXIT_HOLD_MS)), 6);
        }
    }
    /* power and the volume keys (they come as keys, not joystick buttons) */
    pill(r, 59, 408, 64, "VOL-", s->key_vol_down);
    pill(r, 133, 408, 64, "VOL+", s->key_vol_up);
    pill(r, 207, 408, 64, "PWR", s->key_power);

    rect(r, FOOT, 0, 440, W, 40);
    Uint32 idle = now - s->idle_since;
    if (idle + IDLE_WARN_MS >= IDLE_LIMIT_MS)
        textf(r, WARM, 16, 448, 2, "No input: leaving in %u s", (IDLE_LIMIT_MS - (idle > IDLE_LIMIT_MS ? IDLE_LIMIT_MS : idle) + 999) / 1000);
    else
        text(r, TEXT, 16, 448, 2, "Hold SEL+STA 1 s to quit. 15 s idle quits.");
}

/* ---- joystick ---- */
static void close_pad(State *s) {
    if (s->gc) SDL_GameControllerClose(s->gc);
    else if (s->js) SDL_JoystickClose(s->js);
    s->gc = NULL;
    s->js = NULL;
}

static int open_pad(State *s, int index) {
    close_pad(s);
    if (SDL_IsGameController(index)) s->gc = SDL_GameControllerOpen(index);
    s->js = s->gc ? SDL_GameControllerGetJoystick(s->gc) : SDL_JoystickOpen(index);
    if (!s->js) return 0;
    snprintf(s->name, sizeof s->name, "%.40s", SDL_JoystickName(s->js) ? SDL_JoystickName(s->js) : "?");
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(s->js), s->guid, sizeof s->guid);
    s->naxes = SDL_JoystickNumAxes(s->js);
    s->nbuttons = SDL_JoystickNumButtons(s->js);
    s->nhats = SDL_JoystickNumHats(s->js);
    return 1;
}

static void demo_state(State *s) {
    snprintf(s->name, sizeof s->name, "Demo pad (not real)");
    snprintf(s->guid, sizeof s->guid, "03000000deadbeef0000000000000000");
    s->naxes = 4; s->nbuttons = 17; s->nhats = 0;
    s->axis[0] = 22000; s->axis[1] = -9000; s->axis[2] = -3000; s->axis[3] = 17000;
    s->button[B_UP] = s->button[B_RIGHT] = s->button[B_L3] = s->button[B_FN] = s->button[B_A] = s->button[B_L1] = 1;
    s->seen_button[B_A] = s->seen_button[B_UP] = s->seen_button[B_L3] = 1;
    s->key_vol_up = 1;
    s->seen_axes = 3;
    event(s, 1, "btn 11 DOWN"); event(s, 0, "btn 11 UP"); event(s, 1, "btn 15 DOWN");
    event(s, 1, "ax 1 pushed"); event(s, 1, "btn 11 DOWN"); event(s, -1, "key VolumeUp DOWN");
    snprintf(s->last_key, sizeof s->last_key, "-");
}

static int save_screenshot(const char *path) {
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) return 1;
    SDL_Renderer *r = SDL_CreateSoftwareRenderer(surf);
    if (!r) return 1;
    State s;
    memset(&s, 0, sizeof s);
    demo_state(&s);
    draw(r, &s, 5000, 1);
    SDL_RenderPresent(r);
    int rc = SDL_SaveBMP(surf, path);
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(surf);
    return rc;
}

int main(int argc, char **argv) {
    int selftest = 0;
    const char *shot = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) selftest = 1;
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--version")) { puts("input-checker-native " VERSION); return 0; }
        else { fprintf(stderr, "usage: %s [--selftest | --screenshot out.bmp | --version]\n", argv[0]); return 2; }
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "input-checker: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (shot) {
        int rc = save_screenshot(shot);
        SDL_Quit();
        return rc;
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_ShowCursor(SDL_DISABLE);

    SDL_Window *win = SDL_CreateWindow("R36S Input Checker", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, W, H,
                                       selftest ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!win) {
        fprintf(stderr, "input-checker: window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer *r = SDL_CreateRenderer(win, -1, selftest ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!r) r = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE); /* no GPU: the software path still draws */
    if (!r) {
        fprintf(stderr, "input-checker: renderer: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_RenderSetLogicalSize(r, W, H);

    State s;
    memset(&s, 0, sizeof s);
    snprintf(s.last_key, sizeof s.last_key, "-");
    s.idle_since = SDL_GetTicks();
    if (SDL_NumJoysticks() > 0) open_pad(&s, 0);

    if (selftest) {
        draw(r, &s, SDL_GetTicks(), s.js != NULL);
        SDL_RenderPresent(r);
        SDL_version v;
        SDL_GetVersion(&v);
        printf("R3I6NOS_SMOKE ok input-checker-native=%s sdl=%d.%d.%d joysticks=%d renderer=%s\n", VERSION, v.major,
               v.minor, v.patch, SDL_NumJoysticks(), SDL_GetCurrentVideoDriver());
        close_pad(&s);
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

    int running = 1;
    while (running) {
        Uint32 now = SDL_GetTicks();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running = 0; break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                int down = e.type == SDL_KEYDOWN;
                SDL_Keycode k = e.key.keysym.sym;
                if (e.key.repeat) break;
                if (k == SDLK_VOLUMEDOWN) s.key_vol_down = down;
                else if (k == SDLK_VOLUMEUP) s.key_vol_up = down;
                else if (k == SDLK_POWER) s.key_power = down;
                snprintf(s.last_key, sizeof s.last_key, "%.20s", SDL_GetKeyName(k));
                event(&s, -1, "key %.12s %s", s.last_key, down ? "DOWN" : "UP");
                s.idle_since = now;
                if (down && k == SDLK_ESCAPE) running = 0;
                break;
            }
            case SDL_JOYDEVICEADDED:
                if (!s.js && open_pad(&s, e.jdevice.which)) logf_(&s, "pad added");
                s.idle_since = now;
                break;
            case SDL_JOYDEVICEREMOVED:
                if (s.js && e.jdevice.which == SDL_JoystickInstanceID(s.js)) {
                    close_pad(&s);
                    memset(s.button, 0, sizeof s.button);
                    logf_(&s, "pad removed");
                    if (SDL_NumJoysticks() > 0) open_pad(&s, 0);
                }
                break;
            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP: {
                int down = e.type == SDL_JOYBUTTONDOWN, b = e.jbutton.button;
                if (b < MAX_BUTTONS) {
                    s.button[b] = down;
                    if (down) s.seen_button[b] = 1;
                }
                /* the state as SDL reports it right now, to compare with the event */
                event(&s, s.js && SDL_JoystickGetButton(s.js, b), "btn %d %s", b + 1, down ? "DOWN" : "UP");
                s.idle_since = now;
                break;
            }
            case SDL_JOYHATMOTION:
                s.hat = e.jhat.value;
                if (s.hat) { s.seen_hat_dirs++; s.idle_since = now; }
                event(&s, s.hat != 0, "hat %d %s%s%s%s%s", e.jhat.hat, (s.hat & SDL_HAT_UP) ? "u" : "", (s.hat & SDL_HAT_DOWN) ? "d" : "",
                      (s.hat & SDL_HAT_LEFT) ? "l" : "", (s.hat & SDL_HAT_RIGHT) ? "r" : "", s.hat ? "" : "c");
                break;
            case SDL_JOYAXISMOTION:
                if (e.jaxis.axis < MAX_AXES) {
                    int was = abs(s.axis[e.jaxis.axis]) >= ZONE, is = abs(e.jaxis.value) >= ZONE;
                    s.axis[e.jaxis.axis] = e.jaxis.value;
                    if (was != is) {
                        event(&s, is, "ax %d %s", e.jaxis.axis + 1, is ? "pushed" : "centre");
                        if (is) { s.seen_axes |= 1 << e.jaxis.axis; s.idle_since = now; }
                    }
                }
                break;
            }
        }
        if (exit_held(&s)) {
            if (!s.chord_since) s.chord_since = now;
            if (now - s.chord_since >= EXIT_HOLD_MS) running = 0;
        } else {
            s.chord_since = 0;
        }
        if (now - s.idle_since >= IDLE_LIMIT_MS) running = 0;
        draw(r, &s, now, s.js != NULL);
        SDL_RenderPresent(r);
        SDL_Delay(16);
    }
    close_pad(&s);
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

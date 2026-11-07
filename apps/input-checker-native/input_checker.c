/* R36S Input Checker, native ARM64 build (SDL2 only; its own 5x7 font, no font files).
 *
 * Same job and layout as the LÖVE versions (docs/R36S_Input_Checker_Specification.md): every raw
 * button, axis and the hat as the pad reports them, plus SDL's gamepad mapping when it has one.
 * Exit: hold Select+Start for 1 s (gamepad mapping), Esc, or 120 s without input.
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
#define IDLE_LIMIT_MS 120000
#define IDLE_WARN_MS 15000
#define ZONE 16384 /* an axis counts as pushed past half travel */
#define MAX_AXES 8
#define MAX_BUTTONS 32
#define LOG_LINES 6

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
    for (int a = 0; a < 360; a += 3)
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

static int gcb(const State *s, SDL_GameControllerButton b) {
    return s->gc && SDL_GameControllerGetButton(s->gc, b);
}
static void round_btn(SDL_Renderer *r, int x, int y, const char *label, int on, int rad) {
    disc(r, on ? ACTIVE : DIM, x, y, rad);
    textc(r, on ? BG : TEXT, x, y - 3, 1, label);
}
static void stick(SDL_Renderer *r, int cx, int cy, Sint16 ax, Sint16 ay, int pressed, Col tint, const char *label) {
    disc(r, PANEL, cx, cy, 36);
    ring(r, DIM, cx, cy, 36);
    disc(r, pressed ? ACTIVE : tint, cx + (int)(ax / 32768.0 * 26), cy + (int)(ay / 32768.0 * 26), 10);
    textc(r, MUTE, cx, cy + 44, 1, label);
}

static void draw(SDL_Renderer *r, State *s, Uint32 now, int have_pad) {
    color(r, BG);
    SDL_RenderClear(r);
    rect(r, HEAD, 0, 0, W, 45);
    text(r, ACTIVE, 20, 14, 2, "R36S INPUT DIAGNOSTIC TOOL");
    char rt[48];
    SDL_version v;
    SDL_GetVersion(&v);
    snprintf(rt, sizeof rt, "Native ARM64 build, SDL %d.%d.%d", v.major, v.minor, v.patch);
    text(r, MUTE, W - 14 - text_w(rt, 1), 8, 1, rt);
    text(r, MUTE, W - 14 - text_w(have_pad ? s->name : "no gamepad", 1), 26, 1, have_pad ? s->name : "no gamepad");

    if (!have_pad) {
        textc(r, BAD, W / 2, 200, 2, "NO GAMEPAD DETECTED");
        textc(r, MUTE, W / 2, 236, 1, "The pad is not reaching SDL. Press a button; this page updates when one appears.");
    } else {
        int h = s->hat;
        /* shoulders and triggers: SDL's gamepad mapping, when the pad has one */
        round_btn(r, 40, 70, "L1", gcb(s, SDL_CONTROLLER_BUTTON_LEFTSHOULDER), 15);
        round_btn(r, 85, 70, "L2", s->gc && SDL_GameControllerGetAxis(s->gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > ZONE, 15);
        round_btn(r, 235, 70, "R2", s->gc && SDL_GameControllerGetAxis(s->gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > ZONE, 15);
        round_btn(r, 280, 70, "R1", gcb(s, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER), 15);
        /* D-pad */
        rect(r, PANEL, 57, 112, 96, 96);
        rect(r, (h & SDL_HAT_UP) ? ACTIVE : DIM, 92, 116, 26, 32);
        rect(r, (h & SDL_HAT_DOWN) ? ACTIVE : DIM, 92, 172, 26, 32);
        rect(r, (h & SDL_HAT_LEFT) ? ACTIVE : DIM, 61, 147, 32, 26);
        rect(r, (h & SDL_HAT_RIGHT) ? ACTIVE : DIM, 117, 147, 32, 26);
        /* face buttons: raw 0..3 are A, B, X, Y in the spec (it counts from 1) */
        disc(r, PANEL, 250, 160, 48);
        round_btn(r, 250, 130, "X", s->button[2], 14);
        round_btn(r, 250, 190, "A", s->button[0], 14);
        round_btn(r, 220, 160, "Y", s->button[3], 14);
        round_btn(r, 280, 160, "B", s->button[1], 14);
        stick(r, 105, 290, s->axis[0], s->axis[1], gcb(s, SDL_CONTROLLER_BUTTON_LEFTSTICK), ACCENT, "Left stick (L3)");
        stick(r, 250, 290, s->axis[2], s->axis[3], gcb(s, SDL_CONTROLLER_BUTTON_RIGHTSTICK), WARM, "Right stick (R3)");
        round_btn(r, 120, 372, "SEL", gcb(s, SDL_CONTROLLER_BUTTON_BACK), 15);
        round_btn(r, 170, 372, "STA", gcb(s, SDL_CONTROLLER_BUTTON_START), 15);
        round_btn(r, 220, 372, "FN", gcb(s, SDL_CONTROLLER_BUTTON_GUIDE), 15);
        if (s->chord_since) {
            Uint32 held = now - s->chord_since;
            rect(r, PANEL, 40, 404, 240, 10);
            rect(r, WARM, 40, 404, (int)(240 * (held > EXIT_HOLD_MS ? 1.0 : (double)held / EXIT_HOLD_MS)), 10);
        }

        /* telemetry */
        rect(r, PANEL, 340, 56, 284, 376);
        int y = 66;
        text(r, TEXT, 352, y, 2, "TELEMETRY");
        y += 22;
        char hs[8] = ""; /* up to "udlr" */
        if (h & SDL_HAT_UP) strcat(hs, "u");
        if (h & SDL_HAT_DOWN) strcat(hs, "d");
        if (h & SDL_HAT_LEFT) strcat(hs, "l");
        if (h & SDL_HAT_RIGHT) strcat(hs, "r");
        textf(r, ACCENT, 352, y, 1, "Hat 0 (D-pad): %s", *hs ? hs : "c");
        y += 14;
        for (int i = 0; i < s->naxes && i < 6; i++, y += 14)
            textf(r, ACCENT, 352, y, 1, "Axis %d: %+.2f%s", i, s->axis[i] / 32768.0, (s->seen_axes >> i) & 1 ? "  seen" : "");
        y += 4;
        char down[96] = "";
        int nseen = 0;
        for (int i = 0; i < s->nbuttons && i < MAX_BUTTONS; i++) {
            if (s->button[i]) {
                char one[8];
                snprintf(one, sizeof one, "%d ", i);
                strncat(down, one, sizeof down - strlen(down) - 1);
            }
            nseen += s->seen_button[i];
        }
        textf(r, ACTIVE, 352, y, 1, "Buttons down: %s", *down ? down : "none");
        y += 14;
        textf(r, MUTE, 352, y, 1, "Seen: %d of %d buttons, last key %s", nseen, s->nbuttons, s->last_key);
        y += 12;
        textf(r, s->gc ? MUTE : WARM, 352, y, 1, "Axes %d Buttons %d Hats %d Gamepad map: %s", s->naxes, s->nbuttons, s->nhats, s->gc ? "yes" : "NO");
        y += 12;
        textf(r, MUTE, 352, y, 1, "GUID %s", s->guid);
        y += 20;
        text(r, TEXT, 352, y, 1, "LAST EVENTS");
        y += 14;
        for (int i = 0; i < LOG_LINES; i++, y += 12)
            text(r, MUTE, 352, y, 1, s->log[i]);
    }

    rect(r, FOOT, 0, 440, W, 40);
    Uint32 idle = now - s->idle_since;
    if (idle + IDLE_WARN_MS >= IDLE_LIMIT_MS)
        textf(r, WARM, 20, 454, 1, "No input: leaving in %u s. Press anything to stay.", (IDLE_LIMIT_MS - (idle > IDLE_LIMIT_MS ? IDLE_LIMIT_MS : idle) + 999) / 1000);
    else
        text(r, TEXT, 20, 454, 1, "Exit: hold Select+Start for 1 s, or Esc. It also leaves after 2 minutes without input.");
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
    snprintf(s->name, sizeof s->name, "Demo pad (not a real device)");
    snprintf(s->guid, sizeof s->guid, "03000000deadbeef0000000000000000");
    s->naxes = 4; s->nbuttons = 17; s->nhats = 1;
    s->axis[0] = 22000; s->axis[1] = -9000; s->axis[2] = -3000; s->axis[3] = 17000;
    s->button[0] = s->button[2] = 1;
    s->seen_button[0] = s->seen_button[1] = s->seen_button[2] = 1;
    s->hat = SDL_HAT_UP | SDL_HAT_LEFT;
    s->seen_axes = 3;
    logf_(s, "button 0 down"); logf_(s, "hat 0 ul"); logf_(s, "axis 0 +0.67"); logf_(s, "button 2 down");
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
                snprintf(s.last_key, sizeof s.last_key, "%.20s", SDL_GetKeyName(e.key.keysym.sym));
                logf_(&s, "key %.30s", s.last_key);
                s.idle_since = now;
                if (e.key.keysym.sym == SDLK_ESCAPE) running = 0;
                break;
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
                if (e.jbutton.button < MAX_BUTTONS) { s.button[e.jbutton.button] = 1; s.seen_button[e.jbutton.button] = 1; }
                logf_(&s, "button %d down", e.jbutton.button);
                s.idle_since = now;
                break;
            case SDL_JOYBUTTONUP:
                if (e.jbutton.button < MAX_BUTTONS) s.button[e.jbutton.button] = 0;
                logf_(&s, "button %d up", e.jbutton.button);
                break;
            case SDL_JOYHATMOTION:
                s.hat = e.jhat.value;
                if (s.hat) { s.seen_hat_dirs++; s.idle_since = now; }
                logf_(&s, "hat %d %s%s%s%s", e.jhat.hat, (s.hat & SDL_HAT_UP) ? "u" : "", (s.hat & SDL_HAT_DOWN) ? "d" : "",
                      (s.hat & SDL_HAT_LEFT) ? "l" : "", (s.hat & SDL_HAT_RIGHT) ? "r" : "");
                if (!s.hat) logf_(&s, "hat %d c", e.jhat.hat);
                break;
            case SDL_JOYAXISMOTION:
                if (e.jaxis.axis < MAX_AXES) {
                    int was = abs(s.axis[e.jaxis.axis]) >= ZONE, is = abs(e.jaxis.value) >= ZONE;
                    s.axis[e.jaxis.axis] = e.jaxis.value;
                    if (was != is) {
                        logf_(&s, "axis %d %s", e.jaxis.axis, is ? "pushed" : "centre");
                        if (is) { s.seen_axes |= 1 << e.jaxis.axis; s.idle_since = now; }
                    }
                }
                break;
            }
        }
        if (gcb(&s, SDL_CONTROLLER_BUTTON_BACK) && gcb(&s, SDL_CONTROLLER_BUTTON_START)) {
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

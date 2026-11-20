/* r3i6nos input mapping for native apps (C): the reader of docs/r3i6nos-mapping.md. MIT, like the rest of theme/.
 *
 * One reader, so every app turns the R36S's raw buttons and sticks into the same actions:
 *   - the plain inputs (A accept, B back, X, Y, Select, Start, L3, R3) and the D-pad;
 *   - **R1 held is "increase"**: up/down become Page up/down, left/right Home/End (and, for the pointer, 3 times as fast);
 *   - the system chords: L1+R1 the main menu, L2+R2 the contextual menu, Fn the next panel, L1+Fn the first, R1+Fn the previous;
 *   - the right stick as the D-pad, stepping at a rate that follows how far it is pushed (dead zone 0.35, at most 6 steps a second);
 *   - the left stick's pointer speed (120 px/s at full, 3 times that with R1 held).
 * The modifiers alone return R3_NONE: a chord fires when its second input goes down. Fn is only ever the panel key.
 * Raw buttons, 0-based as SDL counts them (the pad's mapping is not SDL's gamepad mapping, docs/hardware-findings.md):
 * B 0, A 1, X 2, Y 3, L1 4, R1 5, L2 6, R2 7, D-pad 8-11, Select 12, Start 13, L3 14, R3 15, Fn 16; axes 0-1 the left stick, 2-3 the right.
 * The numbers (0.35, 6, 120, 3) are the four constants below: change them here and nowhere else. Untested on a unit.
 * Needs r3i6nos_ansiapps.h first. */
#ifndef R3I6NOS_INPUT_H
#define R3I6NOS_INPUT_H

#include <math.h>
#include <stdint.h>
#include <string.h>

#define R3_STICK_DEAD 0.35f     /* of the axis's travel: FM1-NTT's STICK_DEAD */
#define R3_STICK_RATE 6.0f      /* steps a second at full deflection (FM1-NTT turns a knob at 12; a list is easier to overshoot) */
#define R3_POINTER_PPS 120.0f   /* pixels a second at full deflection */
#define R3_R1_POINTER_X 3.0f    /* R1 held: the pointer this many times as fast */

enum { R3_BTN_B, R3_BTN_A, R3_BTN_X, R3_BTN_Y, R3_BTN_L1, R3_BTN_R1, R3_BTN_L2, R3_BTN_R2,
       R3_BTN_UP, R3_BTN_DOWN, R3_BTN_LEFT, R3_BTN_RIGHT, R3_BTN_SELECT, R3_BTN_START, R3_BTN_L3, R3_BTN_R3, R3_BTN_FN, R3_BTN_COUNT };
enum { R3_AXIS_LX, R3_AXIS_LY, R3_AXIS_RX, R3_AXIS_RY, R3_AXIS_COUNT };

/* The button's name, for a screen or a report ("L1"). */
static inline const char *r3_button_name(int b) {
    static const char *const n[R3_BTN_COUNT] = {"B", "A", "X", "Y", "L1", "R1", "L2", "R2", "Up", "Down", "Left", "Right", "Select", "Start", "L3", "R3", "Fn"};
    return b >= 0 && b < R3_BTN_COUNT ? n[b] : "?";
}

/* The action's name ("Page up"), for a screen or a report. */
static inline const char *r3_action_name(int a) {
    switch (a) {
    case R3_UP: return "Up";
    case R3_DOWN: return "Down";
    case R3_LEFT: return "Left";
    case R3_RIGHT: return "Right";
    case R3_PAGEUP: return "Page up";
    case R3_PAGEDOWN: return "Page down";
    case R3_HOME: return "Home";
    case R3_END: return "End";
    case R3_ACCEPT: return "Accept";
    case R3_BACK: return "Back";
    case R3_MORE: case R3_Y: return "Y (more)";
    case R3_X: return "X (delete)";
    case R3_SELECT: return "Select";
    case R3_START: return "Start";
    case R3_MENU: return "Main menu";
    case R3_CONTEXT: return "Context menu";
    case R3_PANEL_NEXT: return "Next panel";
    case R3_PANEL_FIRST: return "First panel";
    case R3_PANEL_PREV: return "Previous panel";
    case R3_L3: return "L3 (mouse)";
    case R3_R3: return "R3 (keyboard)";
    default: return "";
    }
}

typedef struct {
    uint8_t held[R3_BTN_COUNT];
    float axis[R3_AXIS_COUNT];
    int zone[2];                /* the right stick's x and y: -1, 0 or +1 once past the dead zone (a push is counted once, for Home and End) */
    float frac[2];              /* the part of a step carried between frames, so a slow stick is not lost */
    uint32_t last_ms;
    int have_last;
} R3Input;

static inline void r3_input_init(R3Input *in) { memset(in, 0, sizeof *in); }

/* Whether the modifier is down. */
static inline int r3_input_held(const R3Input *in, int b) { return b >= 0 && b < R3_BTN_COUNT && in->held[b]; }

/* A button went down (down = 1) or up. Returns the action it makes (R3_NONE for a release, a modifier on its own, or an unknown button). */
static inline int r3_input_button(R3Input *in, int b, int down) {
    if (b < 0 || b >= R3_BTN_COUNT) return R3_NONE;
    in->held[b] = down ? 1 : 0;
    if (!down) return R3_NONE;
    int r1 = in->held[R3_BTN_R1], l1 = in->held[R3_BTN_L1];
    switch (b) {
    case R3_BTN_L1: case R3_BTN_R1: return in->held[R3_BTN_L1] && in->held[R3_BTN_R1] ? R3_MENU : R3_NONE;
    case R3_BTN_L2: case R3_BTN_R2: return in->held[R3_BTN_L2] && in->held[R3_BTN_R2] ? R3_CONTEXT : R3_NONE;
    case R3_BTN_FN: return l1 ? R3_PANEL_FIRST : r1 ? R3_PANEL_PREV : R3_PANEL_NEXT;
    case R3_BTN_UP: return r1 ? R3_PAGEUP : R3_UP;
    case R3_BTN_DOWN: return r1 ? R3_PAGEDOWN : R3_DOWN;
    case R3_BTN_LEFT: return r1 ? R3_HOME : R3_LEFT;
    case R3_BTN_RIGHT: return r1 ? R3_END : R3_RIGHT;
    case R3_BTN_A: return R3_ACCEPT;
    case R3_BTN_B: return R3_BACK;
    case R3_BTN_X: return R3_X;
    case R3_BTN_Y: return R3_Y;
    case R3_BTN_SELECT: return R3_SELECT;
    case R3_BTN_START: return R3_START;
    case R3_BTN_L3: return R3_L3;
    case R3_BTN_R3: return R3_R3;
    default: return R3_NONE;
    }
}

/* A keyboard key as an action: r3_key_action's, the backquote key for the main menu and the backslash key for the contextual menu (settled 2026-11-20). */
static inline int r3_input_key(SDL_Keycode k) { return k == SDLK_BACKQUOTE ? R3_MENU : k == SDLK_BACKSLASH ? R3_CONTEXT : r3_key_action(k); }

/* An axis moved: v in -1..1. */
static inline void r3_input_axis(R3Input *in, int axis, float v) {
    if (axis >= 0 && axis < R3_AXIS_COUNT) in->axis[axis] = v > 1 ? 1 : v < -1 ? -1 : v;
}

/* Steps a second for a deflection v (0 inside the dead zone, R3_STICK_RATE at full). */
static inline float r3_stick_rate(float v) {
    float m = fabsf(v);
    if (m <= R3_STICK_DEAD) return 0;
    if (m > 1) m = 1;
    return (m - R3_STICK_DEAD) / (1 - R3_STICK_DEAD) * R3_STICK_RATE;
}

/* Pixels a second for a deflection v: R3_POINTER_PPS at full, R3_R1_POINTER_X times that with R1 held. */
static inline float r3_pointer_speed(float v, int r1) {
    float m = fabsf(v);
    if (m <= R3_STICK_DEAD) return 0;
    if (m > 1) m = 1;
    return (m - R3_STICK_DEAD) / (1 - R3_STICK_DEAD) * R3_POINTER_PPS * (r1 ? R3_R1_POINTER_X : 1.0f);
}

/* The left stick's pointer movement over dt seconds, in pixels (signed). */
static inline void r3_input_pointer(const R3Input *in, float dt, float *dx, float *dy) {
    int r1 = in->held[R3_BTN_R1];
    float sx = r3_pointer_speed(in->axis[R3_AXIS_LX], r1), sy = r3_pointer_speed(in->axis[R3_AXIS_LY], r1);
    *dx = (in->axis[R3_AXIS_LX] < 0 ? -sx : sx) * dt;
    *dy = (in->axis[R3_AXIS_LY] < 0 ? -sy : sy) * dt;
}

/* Whether a stick is past the dead zone now (the main loop keeps its short frame while one is, so steps are not late). */
static inline int r3_input_stick_active(const R3Input *in) {
    for (int i = 0; i < R3_AXIS_COUNT; i++) if (fabsf(in->axis[i]) > R3_STICK_DEAD) return 1;
    return 0;
}

/* Once a pass of the main loop: the right stick's steps since the last call, as actions (up to cap). Up and down step at the stick's rate
 * (Page up and down with R1 held); left and right step likewise, but with R1 held fire Home or End once for each push past the dead zone.
 * The first step of a push comes at once; then the rate runs. Returns how many were written to out. */
static inline int r3_input_tick(R3Input *in, uint32_t now_ms, int out[], int cap) {
    float dt = in->have_last ? (float)(now_ms - in->last_ms) / 1000.0f : 0;
    if (dt > 0.25f) dt = 0.25f;     /* a stalled frame (a report, a card test) must not turn into a burst of steps */
    in->last_ms = now_ms;
    in->have_last = 1;
    int n = 0, r1 = in->held[R3_BTN_R1];
    for (int k = 0; k < 2; k++) {
        float v = in->axis[k == 0 ? R3_AXIS_RX : R3_AXIS_RY];
        int z = v <= -R3_STICK_DEAD ? -1 : v >= R3_STICK_DEAD ? 1 : 0;
        if (z != in->zone[k]) {
            in->zone[k] = z;
            in->frac[k] = z ? 1.0f : 0;     /* a new push steps at once */
            if (z && k == 0 && r1) {        /* Home or End: once for the push, not repeated */
                if (n < cap) out[n++] = z < 0 ? R3_HOME : R3_END;
                in->frac[k] = 0;
                continue;
            }
        } else if (z && !(k == 0 && r1)) in->frac[k] += dt * r3_stick_rate(v);
        if (!z || (k == 0 && r1)) continue;
        int act = k == 0 ? (z < 0 ? R3_LEFT : R3_RIGHT) : r1 ? (z < 0 ? R3_PAGEUP : R3_PAGEDOWN) : (z < 0 ? R3_UP : R3_DOWN);
        while (in->frac[k] >= 1.0f && n < cap) { out[n++] = act; in->frac[k] -= 1.0f; }
        if (in->frac[k] >= 1.0f) in->frac[k] = 0.99f;   /* the caller's buffer was full: keep the rest for the next pass, never a burst */
    }
    return n;
}

/* The list rule of the mapping (docs/R3I6NOS-ANSIAPPS-THEME.md, "List dialogs") on a 0-based selection: up and down wrap, Page up and down move by a
 * page and stop at the ends, Home and End jump. Left and right are the app's, so they do nothing here. Accept, back and Y are the caller's. */
static inline int r3_list_step(int sel, int n, int per, int action) {
    if (n <= 0) return 0;
    if (per < 1) per = 1;
    switch (action) {
    case R3_UP: return (sel + n - 1) % n;
    case R3_DOWN: return (sel + 1) % n;
    case R3_PAGEUP: return sel - per < 0 ? 0 : sel - per;
    case R3_PAGEDOWN: return sel + per > n - 1 ? n - 1 : sel + per;
    case R3_HOME: return 0;
    case R3_END: return n - 1;
    default: return sel;
    }
}

#endif /* R3I6NOS_INPUT_H */

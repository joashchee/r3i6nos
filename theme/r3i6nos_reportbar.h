/* The Device report's progress bar for native apps (CLAUDE.md rule 18; docs/DEVICE-REPORTS.md "Progress"). MIT, like the rest of theme/.
 * Include after r3i6nos_ansiapps.h and r3i6nos_report.h.
 *
 *   R3ReportBar bar = { video, renderer, "Commander" };        // video NULL: no window (--report over SSH), the file is still written
 *   r3_reportbar_begin(&bar, &rep, 8);                          // after r3_report_open; 8 milestones
 *   r3_report_at(&rep, 1, "System, card and memory");           // each milestone: the bar jumps to it and the progress file is written
 *   ... r3_report_tick(&rep) in a long loop moves the time ...
 *   r3_reportbar_end(&rep);                                     // before r3_report_close
 *
 * Between milestones the bar is a screen of its own. While the app's own screen is being measured (the power windows) the app calls
 * r3_reportbar_overlay(&screen) after composing it, which lays only the three command rows over it, so what is measured is still that screen. */
#ifndef R3I6NOS_REPORTBAR_H
#define R3I6NOS_REPORTBAR_H

typedef struct { R3Video *video; SDL_Renderer *r; const char *app; } R3ReportBar;

static const R3Report *r3_reportbar_active;       /* the report whose bar the overlay draws; NULL when none runs */

static inline int r3_reportbar_percent(int step, int steps) {
    if (steps <= 0) return 0;
    int p = step >= steps ? 100 : (step - 1) * 100 / steps;
    return p < 0 ? 0 : p;
}

static inline void r3_reportbar_rows(R3Screen *s, int step, int steps, const char *doing, int secs) {
    int pct = r3_reportbar_percent(step, steps), full = pct / 10;
    char bar[40], stat[64];
    snprintf(bar, sizeof bar, "[%.*s%.*s] %d%%", full, "##########", 10 - full, "..........", pct);
    snprintf(stat, sizeof stat, "step %d of %d, %d s", step, steps, secs);
    R3Cmd cmd = {doing, R3_ST_STRONG, '>', bar, 0, stat, "", R3_ST_HINT};
    r3_command_line(s, &cmd);
}

/* The app's own screen with the three rows over it. No-op when no report runs. */
static inline void r3_reportbar_overlay(R3Screen *s) {
    const R3Report *r = r3_reportbar_active;
    if (!r || r->steps <= 0) return;
    int secs = (int)((r3_report_ms() - r->t0) / 1000.0);
    r3_reportbar_rows(s, r->step, r->steps, r->doing, secs);
}

/* The screen of its own: the report's name, what it is doing, the bar. */
static inline void r3_reportbar_draw(void *ctx, int step, int steps, const char *doing, int secs) {
    R3ReportBar *b = ctx;
    if (!b || !b->video || !b->r) return;
    R3Screen s;
    r3_clear(&s);
    r3_top_bar(&s, b->app ? b->app : "", "Device report");
    r3_box(&s, 6, 5, 52, 10, &(R3Box){1, "Device report", NULL, NULL, 0});
    r3_text_fit(&s, 8, 7, "The unit is measuring itself.", 48, R3_ST(R3_ST_BODY));
    r3_text_fit(&s, 8, 8, "No input is read until it is done.", 48, R3_ST(R3_ST_BODY));
    r3_progress(&s, 8, 11, 48, r3_reportbar_percent(step, steps));
    r3_reportbar_rows(&s, step, steps, doing, secs);
    r3_video_draw(b->video, b->r, &s, 1);
}

static inline void r3_reportbar_begin(R3ReportBar *b, R3Report *r, int steps) {
    r3_report_steps(r, steps, b->video && b->r ? r3_reportbar_draw : NULL, b);
    r3_reportbar_active = r;
}
static inline void r3_reportbar_end(R3Report *r) { (void)r; r3_reportbar_active = NULL; }

#endif

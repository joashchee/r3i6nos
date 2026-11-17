/* r3i6nos app data for native apps (C, SDL2): CLAUDE.md rule 16, docs/APP-DATA.md. MIT, like the rest of theme/.
 *
 * Where an app keeps its data is decided once, by the engine `r3i6nos-appdata` (written once, for every app, native or LÖVE). This header is
 * the frontend: it asks the engine (`state`), shows the question in the theme when there is one, runs the move or merge behind a progress bar
 * the person can stop, and gives the app the folder to use.
 *
 *   R3AppData data;
 *   R3AppDataUI ui = { .r = renderer, .video = &video, .toast = &toast };    // the toast is optional: it keeps showing and eats its key
 *   r3_appdata_init(&data, "Commander", &ui);    // on every launch, after the splash and before heavy work
 *   if (data.quit) ...leave;                     // the person chose Quit ("Needs the data card")
 *   ... write only under data.use (also $R3I6NOS_APP_DATA, set for the app and what it starts; $R3I6NOS_DATA_CARD is the data card's root or "")
 *   if (data.notice[0]) ...say it on the reply line (the data card is read-only, full, ...)
 *
 * An unattended run (--report, --rec-demo, a screenshot) calls r3_appdata_quiet(&data, "Commander") instead: no dialog, nothing moved, the
 * folder the engine resolves (the data card's when it is valid), made. If $R3I6NOS_APP_DATA is already set (a test, a caller that decided) it
 * is used as it is, by both. No engine (a laptop, an old image): the old place, $R3I6NOS_ROMS or /roms, plus "/<App>", and a notice says so.
 *
 * The engine is `r3i6nos-appdata` on the PATH; $R3I6NOS_APPDATA names another (tests). It is run, never linked, so what moves data is one
 * program. Define _GNU_SOURCE before the first system header (fork, kill, poll); this header includes the theme and the toast itself. */
#ifndef R3I6NOS_APPDATA_H
#define R3I6NOS_APPDATA_H

#include "r3i6nos_ansiapps.h"
#include "r3i6nos_toast.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

enum { R3_AD_NONE, R3_AD_OFFER_MOVE, R3_AD_OFFER_MERGE, R3_AD_NEEDS_CARD, R3_AD_RESUME };

typedef struct {
    char use[PATH_MAX];         /* the folder to use: always set, always made */
    char data_card[PATH_MAX];   /* the data card's root, or "" when no valid data card is in */
    char notice[512];           /* a line for the reply row, or "" */
    int quit;                   /* the person chose Quit */
    int engine;                 /* the engine answered (0: the old place was used) */
} R3AppData;

typedef struct {
    SDL_Renderer *r;            /* NULL: nothing is drawn (tests) */
    R3Video *video;
    R3Toast *toast;             /* optional */
    int (*next)(void *ctx, int wait_ms);            /* the next R3_* action or R3_NONE; NULL: from SDL's events */
    void (*seen)(void *ctx, const R3Screen *s);     /* every composed screen (tests) */
    void *ctx;
} R3AppDataUI;

typedef struct {
    char card[24], action[24], use[PATH_MAX], s_at[PATH_MAX], d_at[PATH_MAX], reason[200];
    long s_files, d_files, conflicts;
    double s_bytes, d_bytes;
} R3AdState;

/* ---- running the engine -------------------------------------------------------------------------------------------------------- */

static inline const char *r3_ad_engine(void) {
    const char *e = getenv("R3I6NOS_APPDATA");
    return e && *e ? e : "r3i6nos-appdata";
}

/* Start the engine with stdout piped; stderr goes nowhere (the screen is a text-mode UI). Returns the pid, and the read end in *fd. */
static inline pid_t r3_ad_spawn(const char *a, const char *b, const char *c, int *fd) {
    int p[2];
    if (pipe(p) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(p[0]); close(p[1]); return -1; }
    if (pid == 0) {
        dup2(p[1], 1);
        int nul = open("/dev/null", O_WRONLY);
        if (nul >= 0) { dup2(nul, 2); close(nul); }
        close(p[0]);
        close(p[1]);
        const char *argv[5];
        int n = 0;
        argv[n++] = r3_ad_engine();
        if (a) argv[n++] = a;
        if (b) argv[n++] = b;
        if (c) argv[n++] = c;
        argv[n] = NULL;
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    close(p[1]);
    *fd = p[0];
    return pid;
}

/* Run the engine to the end and keep what it printed (at most cap-1 bytes). Returns its exit status, or -1 when it could not run. */
static inline int r3_ad_capture(const char *a, const char *b, const char *c, char *out, size_t cap) {
    int fd;
    pid_t pid = r3_ad_spawn(a, b, c, &fd);
    if (pid < 0) return -1;
    size_t n = 0;
    ssize_t got;
    while (n + 1 < cap && (got = read(fd, out + n, cap - 1 - n)) > 0) n += (size_t)got;
    out[n] = 0;
    close(fd);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static inline void r3_ad_copy(char *dst, size_t cap, const char *src, size_t len) {
    if (len >= cap) len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

/* `state APP`: false when the engine did not answer with an action. */
static inline int r3_ad_state(const char *app, R3AdState *st) {
    char out[4096], line[1024];
    memset(st, 0, sizeof *st);
    if (r3_ad_capture("state", app, NULL, out, sizeof out) != 0) return 0;
    for (char *p = out; *p;) {
        char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        r3_ad_copy(line, sizeof line, p, len);
        p += len + (nl ? 1 : 0);
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;
        if (!strcmp(k, "card")) snprintf(st->card, sizeof st->card, "%s", v);
        else if (!strcmp(k, "action")) snprintf(st->action, sizeof st->action, "%s", v);
        else if (!strcmp(k, "use")) snprintf(st->use, sizeof st->use, "%s", v);
        else if (!strcmp(k, "s_at")) snprintf(st->s_at, sizeof st->s_at, "%s", v);
        else if (!strcmp(k, "d_at")) snprintf(st->d_at, sizeof st->d_at, "%s", v);
        else if (!strcmp(k, "reason")) snprintf(st->reason, sizeof st->reason, "%s", v);
        else if (!strcmp(k, "s_files")) st->s_files = atol(v);
        else if (!strcmp(k, "d_files")) st->d_files = atol(v);
        else if (!strcmp(k, "s_bytes")) st->s_bytes = atof(v);
        else if (!strcmp(k, "d_bytes")) st->d_bytes = atof(v);
        else if (!strcmp(k, "conflicts")) st->conflicts = atol(v);
    }
    return st->action[0] != 0;
}

static inline int r3_ad_mkdirs(const char *path) {
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s", path);
    for (char *s = p + 1; *s; s++)
        if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
    return mkdir(p, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

static inline void r3_ad_human(double n, char *out, size_t cap) {
    const char *unit[] = { "B", "KB", "MB", "GB", "TB" };
    int u = 0;
    while (n >= 1024 && u < 4) { n /= 1024; u++; }
    if (u == 0) snprintf(out, cap, "%.0f %s", n, unit[u]);
    else snprintf(out, cap, "%.1f %s", n, unit[u]);
}

/* ---- settling on a folder ---------------------------------------------------------------------------------------------------- */

/* Make the folder, tell the engine where the app is using its data (for "Needs the data card" later) and export it. */
static inline void r3_ad_settle(R3AppData *d, const char *app, const char *use, const char *data_app_dir) {
    snprintf(d->use, sizeof d->use, "%s", use);
    r3_ad_mkdirs(d->use);
    d->data_card[0] = 0;
    if (data_app_dir && *data_app_dir) {
        snprintf(d->data_card, sizeof d->data_card, "%s", data_app_dir);
        char *slash = strrchr(d->data_card, '/');
        if (slash && slash != d->data_card) *slash = 0;
    }
    if (d->engine) {
        char out[256];
        int on_data = data_app_dir && *data_app_dir && !strcmp(d->use, data_app_dir);
        r3_ad_capture("used", app, on_data ? "data" : "system", out, sizeof out);
    }
    setenv("R3I6NOS_APP_DATA", d->use, 1);
    setenv("R3I6NOS_DATA_CARD", d->data_card, 1);
}

/* The old place, when there is no engine: $R3I6NOS_ROMS or /roms, plus the app's folder. */
static inline void r3_ad_legacy(R3AppData *d, const char *app) {
    const char *roms = getenv("R3I6NOS_ROMS") && *getenv("R3I6NOS_ROMS") ? getenv("R3I6NOS_ROMS") : "/roms";
    char use[PATH_MAX];
    snprintf(use, sizeof use, "%s/%s", roms, app);
    d->engine = 0;
    snprintf(d->notice, sizeof d->notice, "r3i6nos-appdata is not available; keeping data in %.300s", use);
    r3_ad_settle(d, app, use, NULL);
}

/* No dialog, nothing moved: the folder the engine resolves (the data card's when it is valid), made and exported. */
static inline int r3_appdata_quiet(R3AppData *d, const char *app) {
    memset(d, 0, sizeof *d);
    const char *preset = getenv("R3I6NOS_APP_DATA");
    if (preset && *preset) {
        d->engine = 0;
        const char *dc = getenv("R3I6NOS_DATA_CARD");
        snprintf(d->data_card, sizeof d->data_card, "%s", dc ? dc : "");
        snprintf(d->use, sizeof d->use, "%s", preset);
        r3_ad_mkdirs(d->use);
        return 0;
    }
    R3AdState st;
    if (!r3_ad_state(app, &st) || !st.use[0]) { r3_ad_legacy(d, app); return 0; }
    d->engine = 1;
    if (strcmp(st.card, "valid") && strcmp(st.card, "none") && st.reason[0]) snprintf(d->notice, sizeof d->notice, "%s Using the system card.", st.reason);
    r3_ad_settle(d, app, st.use, st.d_at);
    return 0;
}

/* ---- the screens --------------------------------------------------------------------------------------------------------------- */

static inline int r3_ad_sdl_next(void *ctx, int wait_ms) {
    R3AppDataUI *ui = (R3AppDataUI *)ctx;
    SDL_Event e;
    /* SDL_WaitEventTimeout polls every millisecond under KMSDRM (about 920 wake-ups a second on the unit, docs/POWER.md): nap instead */
    int have = SDL_PollEvent(&e);
    for (Uint32 end = SDL_GetTicks() + (Uint32)(wait_ms > 0 ? wait_ms : 0); !have && SDL_GetTicks() < end; have = SDL_PollEvent(&e)) SDL_Delay(20);
    if (!have) return R3_NONE;
    switch (e.type) {
    case SDL_QUIT: return R3_BACK;
    case SDL_JOYDEVICEADDED: SDL_JoystickOpen(e.jdevice.which); return R3_NONE;
    case SDL_JOYBUTTONDOWN:
        if (ui->toast && r3_toast_key(ui->toast)) return R3_NONE;      /* the key that clears a failure toast is used up */
        return r3_pad_action(e.jbutton.button);
    case SDL_KEYDOWN:
        if (ui->toast && r3_toast_key(ui->toast)) return R3_NONE;
        return r3_key_action(e.key.keysym.sym);
    default: return R3_NONE;
    }
}

static inline int r3_ad_next(const R3AppDataUI *ui, int wait_ms) {
    if (ui->next) return ui->next(ui->ctx, wait_ms);
    return r3_ad_sdl_next((void *)ui, wait_ms);
}

static inline void r3_ad_show(const R3AppDataUI *ui, const R3Screen *s) {
    if (ui->seen) ui->seen(ui->ctx, s);
    if (ui->r && ui->video) {
        if (ui->toast) {
            r3_toast_poll(ui->toast, SDL_GetTicks());
            R3Screen withtoast = *s;
            r3_toast_draw(ui->toast, &withtoast);
            r3_video_draw(ui->video, ui->r, &withtoast, 0);
        } else r3_video_draw(ui->video, ui->r, s, 0);
    }
}

/* A question: lines of text over a list of choices. Returns the chosen index; the default is `sel`, and B (or closing the window) answers -1. */
static inline int r3_ad_ask(const R3AppDataUI *ui, const char *app, const char *title, const char *const *lines, int nl,
                            const char *const *options, int n, int sel) {
    R3Item items[6];
    memset(items, 0, sizeof items);
    if (n > 6) n = 6;
    for (int i = 0; i < n; i++) snprintf(items[i].label, sizeof items[i].label, "%s", options[i]);
    for (;;) {
        R3Screen s;
        r3_clear(&s);
        r3_top_bar(&s, app, "data");
        int h = nl + n + 6;
        r3_box(&s, 3, 3, 58, h, &(R3Box){1, title, NULL, NULL, 0});
        for (int i = 0; i < nl; i++) r3_text_fit(&s, 5, 5 + i, lines[i], 54, R3_ST(R3_ST_BODY));
        R3List l = { 5, 6 + nl, 54, n + 2, "Choose", "A choose   B not now", items, n, sel, { NULL } };
        r3_list_dialog(&s, &l);
        r3_ad_show(ui, &s);
        int act = r3_ad_next(ui, 50);
        if (act == R3_ACCEPT) return sel;
        if (act == R3_BACK) return -1;
        if (act == R3_UP || act == R3_DOWN || act == R3_PAGEUP || act == R3_PAGEDOWN || act == R3_HOME || act == R3_END)
            sel = r3_list_key(sel, n, n, act);
    }
}

/* Run the engine's move or merge behind a progress bar. B asks it to stop (it stops between files). Returns 1 when it finished
 * and 0 when it failed or was stopped; `why` gets the reason or the DONE counts. */
static inline int r3_ad_work(const R3AppDataUI *ui, const char *app, const char *verb, const char *arg, char *why, size_t why_cap) {
    int fd = -1;
    pid_t pid = r3_ad_spawn(verb, app, arg, &fd);
    char step[512] = "Starting", buf[2048], line[512];
    size_t have = 0;
    long done = 0, total = 0;
    int ok = 0, finished = 0, stopped = 0;
    Uint32 t0 = SDL_GetTicks();
    snprintf(why, why_cap, "r3i6nos-appdata could not run");
    if (pid < 0) return 0;
    while (!finished) {
        struct pollfd pf = { fd, POLLIN, 0 };
        int pr = poll(&pf, 1, 100);
        if (pr > 0) {
            ssize_t got = read(fd, buf + have, sizeof buf - 1 - have);
            if (got <= 0) finished = 1;
            else have += (size_t)got;
        }
        for (char *nl; (nl = memchr(buf, '\n', have)) != NULL;) {
            size_t len = (size_t)(nl - buf);
            r3_ad_copy(line, sizeof line, buf, len);
            memmove(buf, nl + 1, have - len - 1);
            have -= len + 1;
            if (!strncmp(line, "STEP ", 5)) snprintf(step, sizeof step, "%s", line + 5);
            else if (!strncmp(line, "PROGRESS ", 9)) sscanf(line + 9, "%ld/%ld", &done, &total);
            else if (!strncmp(line, "DONE", 4)) { ok = 1; snprintf(why, why_cap, "%s", line + 4 + (line[4] == ' ')); }
            else if (!strncmp(line, "FAIL ", 5)) snprintf(why, why_cap, "%s", line + 5);
        }
        if (have >= sizeof buf - 1) have = 0;                          /* a line this long is not ours: drop it rather than stall */
        R3Screen s;
        r3_clear(&s);
        r3_top_bar(&s, app, "data");
        char title[96], count[96], clock[32];
        snprintf(title, sizeof title, "%s %s's data", !strcmp(verb, "move") ? "Moving" : "Merging", app);
        r3_box(&s, 6, 6, 52, 11, &(R3Box){1, title, NULL, "B stops; what is done stays done", 0});
        r3_text_fit(&s, 8, 8, step, 48, R3_ST(R3_ST_BODY));
        r3_progress(&s, 8, 10, 48, total > 0 ? (int)(done * 100 / total) : 0);
        Uint32 secs = (SDL_GetTicks() - t0) / 1000;
        snprintf(count, sizeof count, "%ld of %ld files", done, total);
        snprintf(clock, sizeof clock, "%u:%02u", (unsigned)(secs / 60), (unsigned)(secs % 60));
        r3_text_fit(&s, 8, 12, count, 30, R3_ST(R3_ST_STRONG));
        r3_text(&s, 56 - (int)strlen(clock), 12, clock, R3_ST(R3_ST_HINT));
        if (stopped) r3_text_fit(&s, 8, 14, "Stopping after this file...", 48, R3_ST(R3_ST_HINT));
        r3_ad_show(ui, &s);
        if (r3_ad_next(ui, 0) == R3_BACK && !stopped) { stopped = 1; kill(pid, SIGTERM); }
    }
    close(fd);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (!ok && stopped) snprintf(why, why_cap, "Stopped. What was done stays done; the next launch offers to finish.");
    return ok && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* A message and one button. */
static inline void r3_ad_tell(const R3AppDataUI *ui, const char *app, const char *title, const char *text) {
    const char *lines[4] = { "", "", "", "" };
    char part[3][56];
    int nl = 0;
    size_t len = strlen(text), at = 0;
    while (at < len && nl < 3) {                      /* wrapped at 54 cells, at a space when there is one */
        size_t take = len - at > 54 ? 54 : len - at;
        if (at + take < len) { while (take > 20 && text[at + take] != ' ') take--; }
        r3_ad_copy(part[nl], sizeof part[nl], text + at, take);
        lines[nl] = part[nl];
        nl++;
        at += take + (text[at + take] == ' ');
    }
    const char *ok[1] = { "OK" };
    r3_ad_ask(ui, app, title, lines, nl, ok, 1, 0);
}

/* ---- the launch check ------------------------------------------------------------------------------------------------------- */

static inline int r3_appdata_init(R3AppData *d, const char *app, const R3AppDataUI *ui) {
    char text[4][160], line[200];
    const char *lines[4];
    memset(d, 0, sizeof *d);
    if (getenv("R3I6NOS_APP_DATA") && *getenv("R3I6NOS_APP_DATA")) return r3_appdata_quiet(d, app);
    R3AdState st;
    if (!r3_ad_state(app, &st) || !st.use[0]) { r3_ad_legacy(d, app); return 0; }
    d->engine = 1;
    char sb[32], db[32], title[96];
    r3_ad_human(st.s_bytes, sb, sizeof sb);
    r3_ad_human(st.d_bytes, db, sizeof db);
    snprintf(title, sizeof title, "%s's data", app);
    char work[512];

    if (!strcmp(st.action, "offer-move")) {
        snprintf(text[0], sizeof text[0], "Move %s's data to the data card?", app);
        snprintf(text[1], sizeof text[1], "%s in %ld files.", sb, st.s_files);
        snprintf(text[2], sizeof text[2], "The system card can then stay out.");
        lines[0] = text[0]; lines[1] = text[1]; lines[2] = text[2];
        const char *opts[3] = { "Move all", "Keep on the system card", "Not now" };
        int pick = r3_ad_ask(ui, app, title, lines, 3, opts, 3, 2);
        if (pick == 0) {
            if (!r3_ad_work(ui, app, "move", NULL, work, sizeof work)) r3_ad_tell(ui, app, "It did not finish", work);
        } else if (pick == 1) {
            r3_ad_capture("choice", app, "keep", line, sizeof line);
        }
    } else if (!strcmp(st.action, "offer-merge")) {
        snprintf(text[0], sizeof text[0], "%s has data on both cards:", app);
        snprintf(text[1], sizeof text[1], "%s on the system card, %s on the data card.", sb, db);
        if (st.conflicts > 0) snprintf(text[2], sizeof text[2], "%ld files are on both and differ (the newer is kept).", st.conflicts);
        else snprintf(text[2], sizeof text[2], "Nothing differs; the copies are joined.");
        lines[0] = text[0]; lines[1] = text[1]; lines[2] = text[2];
        const char *opts[4] = { "Merge into the data card (recommended)", "Merge into the system card", "Not now", "Don't ask again" };
        int pick = r3_ad_ask(ui, app, title, lines, 3, opts, 4, 2);
        if (pick == 0 || pick == 1) {
            if (!r3_ad_work(ui, app, "merge", pick == 0 ? "data" : "system", work, sizeof work)) r3_ad_tell(ui, app, "It did not finish", work);
        } else if (pick == 3) {
            r3_ad_capture("choice", app, "keep", line, sizeof line);
        }
    } else if (!strcmp(st.action, "resume")) {
        snprintf(text[0], sizeof text[0], "Finish moving %s's data?", app);
        snprintf(text[1], sizeof text[1], "It was stopped part way; nothing is lost.");
        lines[0] = text[0]; lines[1] = text[1];
        const char *opts[2] = { "Finish now", "Not now" };
        if (r3_ad_ask(ui, app, title, lines, 2, opts, 2, 1) == 0)
            if (!r3_ad_work(ui, app, "merge", "data", work, sizeof work)) r3_ad_tell(ui, app, "It did not finish", work);
    } else if (!strcmp(st.action, "needs-card")) {
        snprintf(text[0], sizeof text[0], "Needs the data card.");
        snprintf(text[1], sizeof text[1], "%s's data is on it, and it is not in.", app);
        lines[0] = text[0]; lines[1] = text[1];
        const char *opts[2] = { "Use the system card (new, separate data)", "Quit" };
        int pick = r3_ad_ask(ui, app, title, lines, 2, opts, 2, 1);
        if (pick == 0) {
            snprintf(d->use, sizeof d->use, "%s", st.use);
            r3_ad_mkdirs(d->use);
            r3_ad_capture("used", app, "system", line, sizeof line);
        } else {
            d->quit = 1;
            return 0;
        }
    }
    /* what is where now: the engine's answer after whatever was chosen */
    R3AdState after;
    if (!r3_ad_state(app, &after) || !after.use[0]) after = st;
    if (strcmp(after.card, "valid") && strcmp(after.card, "none") && after.reason[0])
        snprintf(d->notice, sizeof d->notice, "%s Using the system card.", after.reason);
    r3_ad_settle(d, app, after.use, after.d_at);
    return 0;
}

#endif /* R3I6NOS_APPDATA_H */

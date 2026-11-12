/* r3i6nos device reports for native apps (C, SDL2): docs/DEVICE-REPORTS.md. MIT, like the rest of theme/.
 *
 * A port's report checks and measures what only the R36S can answer and writes one text file for Claude to read:
 * /roms/<App>/r36s-report.txt, in the grammar of docs/DEVICE-REPORTS.md (STATUS key: value, sections, a summary, "end").
 *
 *   R3Report r;
 *   if (r3_report_open(&r, "/roms/Neumetik", "Neumetik", "0.1.0", "r36s-neumetik", "Start > Device report") == 0) {
 *       r3_report_system(&r);            // kernel, CPU, memory, SDL drivers, sound cards, every pad and its GUID
 *       r3_report_card(&r, "/roms/Neumetik");
 *       r3_report_section(&r, "speed");
 *       r3_report_line(&r, R3_PASS, "midi", "%.1fx real time (goal 2x)", x);
 *       r3_report_memory(&r);
 *       r3_report_power_section(&r);     // == power ==: the battery (CLAUDE.md rule 14; the measuring is below, "Power")
 *       R3PowerResult idle, work;
 *       r3_report_power_idle(&r, 30, NULL, NULL, &idle);       // the app open and waiting: must cost almost nothing
 *       R3Power p; r3_power_begin(&p);                          // ...the Rec demo tour runs, calling r3_power_sample(&p) about each second
 *       r3_report_power_end(&r, &p, "working", 40.0, 120.0, &work);
 *       r3_report_power_advice(&r, &idle, &work);               // the last line: what to cut first
 *       r3_report_close(&r);             // summary, "end", rename into place
 *   }
 *
 * Every line also goes to stdout. Define the POSIX/GNU feature macros (e.g. _GNU_SOURCE) before including system headers.
 */
#ifndef R3I6NOS_REPORT_H
#define R3I6NOS_REPORT_H

#include <SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define R3_PASS "PASS"
#define R3_FAIL "FAIL"
#define R3_INFO "INFO"
#define R3_SKIP "SKIP"

typedef struct {
    FILE *f;
    char path[PATH_MAX], tmp[PATH_MAX + 8];
    int pass, fail, info, skip;
    int quiet;              /* 1: not to stdout (tests) */
} R3Report;

/* Milliseconds on a steady clock, for timing a check. */
static inline double r3_report_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

static inline void r3_report_raw(R3Report *r, const char *text) {
    if (r->f) fputs(text, r->f);
    if (!r->quiet) { fputs(text, stdout); fflush(stdout); }
}

/* One fact: status is R3_PASS, R3_FAIL, R3_INFO or R3_SKIP. Line breaks in the value become spaces. */
static inline void r3_report_line(R3Report *r, const char *status, const char *key, const char *fmt, ...) {
    char value[512], line[700];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(value, sizeof value, fmt, ap);
    va_end(ap);
    char k[96];
    snprintf(k, sizeof k, "%s", key);
    for (char *p = value; *p; p++) if (*p == '\n' || *p == '\r' || *p == '\t') *p = ' ';
    for (char *p = k; *p; p++) if (*p == '\n' || *p == '\r' || *p == '\t' || *p == ':') *p = ' ';
    snprintf(line, sizeof line, "%s %s: %s\n", status, k, value);
    r3_report_raw(r, line);
    if (!strcmp(status, R3_PASS)) r->pass++;
    else if (!strcmp(status, R3_FAIL)) r->fail++;
    else if (!strcmp(status, R3_SKIP)) r->skip++;
    else r->info++;
}

/* A check: PASS when ok, else FAIL, with the same words. */
static inline void r3_report_check(R3Report *r, int ok, const char *key, const char *value) {
    r3_report_line(r, ok ? R3_PASS : R3_FAIL, key, "%s", value);
}

static inline void r3_report_section(R3Report *r, const char *name) {
    char line[160];
    snprintf(line, sizeof line, "\n== %s ==\n", name);
    r3_report_raw(r, line);
}

static inline int r3_report_mkdirs(const char *path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1; *p = '/'; }
    return (mkdir(tmp, 0755) == 0 || errno == EEXIST) ? 0 : -1;
}

/* Starts dir/file (as .tmp until closed) and writes the header. 0, or -1 when it can't (errno set). For an app with more than one
 * build on the same card (the IO Checker: LÖVE 11.5, LÖVE 12.0, native), one file a build; everything else uses r3_report_open. */
static inline int r3_report_open_named(R3Report *r, const char *dir, const char *file, const char *app, const char *version, const char *package,
                                       const char *how, const char *repo /* the repo Claude reads it in; NULL: the app's own */) {
    int quiet = r->quiet;
    memset(r, 0, sizeof *r);
    r->quiet = quiet;
    if (r3_report_mkdirs(dir) != 0) return -1;
    int pn = snprintf(r->path, sizeof r->path, "%s/%s", dir, file);
    if (pn < 0 || (size_t)pn >= sizeof r->path) { errno = ENAMETOOLONG; return -1; }
    snprintf(r->tmp, sizeof r->tmp, "%s.tmp", r->path);
    r->f = fopen(r->tmp, "w");
    if (!r->f) return -1;
    char when[32], line[1024];
    time_t now = time(NULL);
    strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", localtime(&now));
    const char *base = strrchr(dir, '/');
    int hn = snprintf(line, sizeof line,
             "# r3i6nos device report\napp: %s %s (%s)\nmade: %s\nhow: %s\nfor: Claude. Copy EASYROMS/%s/%s to a computer and give it to Claude in the %s repo.\n",
             app, version, package, when, how, base ? base + 1 : dir, file, repo ? repo : app);
    if (hn < 0 || (size_t)hn >= sizeof line) line[sizeof line - 2] = '\n'; /* cut, but the last header line still ends */
    r3_report_raw(r, line);
    return 0;
}

/* Starts dir/r36s-report.txt (as .tmp until closed) and writes the header. 0, or -1 when it can't (errno set). */
static inline int r3_report_open(R3Report *r, const char *dir, const char *app, const char *version, const char *package, const char *how) {
    return r3_report_open_named(r, dir, "r36s-report.txt", app, version, package, how, NULL);
}

/* The summary, "end", and the file renamed into place. 0 when the report is on the card. */
static inline int r3_report_close(R3Report *r) {
    char line[160];
    r3_report_section(r, "summary");
    snprintf(line, sizeof line, "PASS %d  FAIL %d  INFO %d  SKIP %d\nend\n", r->pass, r->fail, r->info, r->skip);
    r3_report_raw(r, line);
    if (!r->f) return -1;
    int bad = fflush(r->f) != 0;
    bad |= fsync(fileno(r->f)) != 0;
    bad |= fclose(r->f) != 0;
    r->f = NULL;
    if (bad || rename(r->tmp, r->path) != 0) { unlink(r->tmp); return -1; }
    return 0;
}

/* The first line of a file that starts with key (e.g. "MemTotal:"), after the key, trimmed; 0 when found. */
static inline int r3_report_field(const char *file, const char *key, char *out, size_t cap) {
    FILE *f = fopen(file, "r");
    if (!f) return -1;
    char line[512];
    int found = -1;
    size_t n = strlen(key);
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, key, n) != 0) continue;
        char *v = line + n;
        while (*v == ' ' || *v == '\t' || *v == ':') v++;
        v[strcspn(v, "\r\n")] = 0;
        size_t len = strlen(v);
        if (cap == 0) break;
        if (len >= cap) len = cap - 1; /* cut to fit, as snprintf would (copied by hand: GCC warns about the snprintf) */
        memcpy(out, v, len);
        out[len] = 0;
        found = 0;
        break;
    }
    fclose(f);
    return found;
}

/* A whole small file on one line (separated by " | "); 0 when read. */
static inline int r3_report_slurp_line(const char *file, char *out, size_t cap) {
    FILE *f = fopen(file, "r");
    if (!f) return -1;
    out[0] = 0;
    char line[256];
    size_t used = 0;
    while (fgets(line, sizeof line, f) && used + 4 < cap) {
        line[strcspn(line, "\r\n")] = 0;
        char *s = line;
        while (*s == ' ') s++;
        if (!*s) continue;
        used += (size_t)snprintf(out + used, cap - used, "%s%s", used ? " | " : "", s);
    }
    fclose(f);
    return 0;
}

/* == system ==: kernel, CPU, memory, SDL (version, video and audio drivers, sound devices) and every pad with its GUID.
 * Initialises SDL's joystick and audio subsystems for the look if the app has not, and leaves them as it found them. */
static inline void r3_report_system(R3Report *r) {
    r3_report_section(r, "system");
    struct utsname u;
    if (uname(&u) == 0) r3_report_line(r, R3_INFO, "kernel", "%s %s %s", u.sysname, u.release, u.machine);
    char v[512];
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    char part[64] = "", hz[64] = "";
    r3_report_field("/proc/cpuinfo", "CPU part", part, sizeof part);
    if (r3_report_field("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "", hz, sizeof hz) == 0 && atol(hz) > 0)
        snprintf(hz, sizeof hz, ", %ld MHz max", atol(hz) / 1000);
    else hz[0] = 0;
    r3_report_line(r, R3_INFO, "cpu", "%ld CPUs%s%s%s", cpus, part[0] ? ", part " : "", part, hz);
    if (r3_report_field("/proc/meminfo", "MemTotal", v, sizeof v) == 0) {
        char avail[64] = "?";
        r3_report_field("/proc/meminfo", "MemAvailable", avail, sizeof avail);
        r3_report_line(r, R3_INFO, "memory", "total %s, available %s", v, avail);
    } else {
        r3_report_line(r, R3_SKIP, "memory", "no /proc/meminfo here (not Linux)");
    }
    if (r3_report_slurp_line("/proc/asound/cards", v, sizeof v) == 0) r3_report_line(r, R3_INFO, "sound cards", "%s", v[0] ? v : "none");
    else r3_report_line(r, R3_SKIP, "sound cards", "no /proc/asound here");
    /* The output route, from the root helper's own status (docs/SOUND.md): the codec's Playback Path and the jack. R3I6NOS_SOUND names it. */
    {
        const char *helper = getenv("R3I6NOS_SOUND");
        char cmd[PATH_MAX + 32];
        snprintf(cmd, sizeof cmd, "%s status 2>/dev/null", helper && *helper ? helper : "r3i6nos-sound");
        FILE *p = popen(cmd, "r");
        int shown = 0;
        char ln[256];
        while (p && fgets(ln, sizeof ln, p)) {
            ln[strcspn(ln, "\n")] = 0;
            if (strstr(ln, " playback path: ") || !strncmp(ln, "output: ", 8) || !strncmp(ln, "headphones: ", 12) || !strncmp(ln, "jack: ", 6)) {
                char *c = strchr(ln, ':');
                *c = 0;
                r3_report_line(r, R3_INFO, ln, "%s", c + 2);
                shown++;
            }
        }
        if (p) pclose(p);
        if (!shown) r3_report_line(r, R3_SKIP, "sound route", "r3i6nos-sound status gave nothing (not the R36S image)");
    }

    SDL_version sv;
    SDL_GetVersion(&sv);
    r3_report_line(r, R3_INFO, "sdl", "%d.%d.%d, video driver %s", sv.major, sv.minor, sv.patch,
                   SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(video not started)");
    int had_audio = SDL_WasInit(SDL_INIT_AUDIO) != 0;
    if (had_audio || SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        const char *drv = SDL_GetCurrentAudioDriver();
        int n = SDL_GetNumAudioDevices(0);
        r3_report_check(r, drv != NULL, "sdl audio driver", drv ? drv : SDL_GetError());
        r3_report_line(r, R3_INFO, "sdl output devices", "%d", n);
        for (int i = 0; i < n && i < 8; i++) {
            char key[32];
            snprintf(key, sizeof key, "sdl output %d", i);
            r3_report_line(r, R3_INFO, key, "%s", SDL_GetAudioDeviceName(i, 0) ? SDL_GetAudioDeviceName(i, 0) : "?");
        }
        if (!had_audio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    } else {
        r3_report_line(r, R3_FAIL, "sdl audio", "cannot start SDL audio: %s", SDL_GetError());
    }
    int had_joy = SDL_WasInit(SDL_INIT_JOYSTICK) != 0;
    if (had_joy || SDL_InitSubSystem(SDL_INIT_JOYSTICK) == 0) {
        int n = SDL_NumJoysticks();
        r3_report_line(r, n > 0 ? R3_PASS : R3_FAIL, "pads", "%d (the R36S has 1)", n);
        for (int i = 0; i < n && i < 4; i++) {
            SDL_Joystick *j = SDL_JoystickOpen(i);
            char guid[64] = "?", key[24];
            SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i), guid, sizeof guid);
            snprintf(key, sizeof key, "pad %d", i);
            r3_report_line(r, R3_INFO, key, "\"%s\" guid %s, %d buttons, %d axes, %d hats", SDL_JoystickNameForIndex(i) ? SDL_JoystickNameForIndex(i) : "?",
                           guid, j ? SDL_JoystickNumButtons(j) : -1, j ? SDL_JoystickNumAxes(j) : -1, j ? SDL_JoystickNumHats(j) : -1);
            if (j && !had_joy) SDL_JoystickClose(j);
        }
        if (!had_joy) SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    } else {
        r3_report_line(r, R3_FAIL, "pads", "cannot start SDL joysticks: %s", SDL_GetError());
    }
}

/* The filesystem a path is on, from /proc/mounts (the longest mount point that holds it); 0 when found. */
static inline int r3_report_fstype(const char *path, char *out, size_t cap) {
    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return -1;
    char dev[256], mnt[4096], type[64];
    size_t best = 0;
    int found = -1;
    while (fscanf(f, "%255s %4095s %63s %*[^\n]", dev, mnt, type) == 3) {
        size_t n = strlen(mnt);
        int under = !strncmp(path, mnt, n) && (path[n] == '/' || path[n] == 0 || (n == 1 && mnt[0] == '/'));
        if (under && n >= best) { best = n; snprintf(out, cap, "%s (%s on %s)", type, dev, mnt); found = 0; }
    }
    fclose(f);
    return found;
}

/* == card ==: in dir (the app's own folder): make it, write, rename over a file, read back, delete; filesystem and free space. */
static inline void r3_report_card(R3Report *r, const char *dir) {
    r3_report_section(r, "card");
    char a[PATH_MAX + 32], b[PATH_MAX + 32], buf[64] = "";
    r3_report_check(r, r3_report_mkdirs(dir) == 0, "mkdir", dir);
    if (snprintf(a, sizeof a, "%s/.r36s-report-test", dir) >= (int)sizeof a || snprintf(b, sizeof b, "%s/.r36s-report-test.tmp", dir) >= (int)sizeof b) {
        r3_report_line(r, R3_FAIL, "card", "the folder's path is too long: %.80s...", dir);
        return;
    }
    FILE *f = fopen(a, "w");
    int ok = f && fputs("first\n", f) >= 0;
    if (f) ok &= fclose(f) == 0;
    r3_report_check(r, ok, "write a file", ok ? "ok" : strerror(errno));
    f = fopen(b, "w");
    ok = f && fputs("second\n", f) >= 0;
    if (f) ok &= fclose(f) == 0;
    ok = ok && rename(b, a) == 0;
    r3_report_check(r, ok, "rename over a file", ok ? "ok (saves are a temp file and a rename)" : strerror(errno));
    f = fopen(a, "r");
    ok = f && fgets(buf, sizeof buf, f) && !strcmp(buf, "second\n");
    if (f) fclose(f);
    r3_report_check(r, ok, "read back", ok ? "the renamed file's text" : "different text, or unreadable");
    ok = unlink(a) == 0;
    r3_report_check(r, ok, "delete", ok ? "ok" : strerror(errno));
    unlink(b);
    char fs[4096 + 400];
    if (r3_report_fstype(dir, fs, sizeof fs) == 0) r3_report_line(r, R3_INFO, "filesystem", "%s", fs);
    else r3_report_line(r, R3_SKIP, "filesystem", "no /proc/mounts here");
    struct statvfs st;
    if (statvfs(dir, &st) == 0)
        r3_report_line(r, R3_INFO, "free", "%.1f GB of %.1f GB", (double)st.f_bavail * (double)st.f_frsize / 1e9, (double)st.f_blocks * (double)st.f_frsize / 1e9);
}

/* INFO lines for this process's memory: peak (VmHWM) and now (VmRSS). */
static inline void r3_report_memory(R3Report *r) {
    char hwm[64], rss[64];
    if (r3_report_field("/proc/self/status", "VmHWM", hwm, sizeof hwm) == 0 && r3_report_field("/proc/self/status", "VmRSS", rss, sizeof rss) == 0)
        r3_report_line(r, R3_INFO, "memory of this app", "peak %s, now %s", hwm, rss);
    else r3_report_line(r, R3_SKIP, "memory of this app", "no /proc/self/status here (not Linux)");
}

/* ---- Power (CLAUDE.md rule 14; docs/POWER.md): what this app costs the battery, idle and working ---------------------------------------
 *
 * From /proc and /sys only (R3I6NOS_PROC and R3I6NOS_SYS name other roots, for tests). One window is one state: r3_power_begin takes the
 * snapshots (this process and its threads, every process, the system, the battery), r3_power_sample reads the battery's draw (call it about
 * once a second; it ignores a call that is sooner), and r3_report_power_end writes the lines and checks the goals. The draw (current_now x
 * voltage_now) is only read while the battery says Discharging, and the report says to unplug when it does not. Goals are first guesses,
 * set before any measurement (docs/POWER.md): change them from what a unit says. The same grammar as power_section in r3i6nos-report.
 */
#define R3_POWER_IDLE_CPU 2.0      /* % of a core an app waiting for input may use */
#define R3_POWER_IDLE_WAKE 25.0    /* context switches a second while waiting (a 30 fps loop is 30: block, or sleep longer) */
#define R3_POWER_MAXPROC 1024
#define R3_POWER_MAXTHR 128

typedef struct { int id; unsigned long long ticks, ctx; char name[24]; } R3PowerTask;

typedef struct {
    double (*clock_ms)(void);               /* r3_report_ms; a test sets a fake clock */
    double t0, last_sample;
    unsigned long long self_ticks0, sys_total0, sys_idle0, sys_intr0, sys_ctxt0;
    R3PowerTask *procs0, *thr0;
    int nprocs0, nthr0;
    double mw_sum, mw_min, mw_max;
    int mw_n;
} R3Power;

typedef struct {
    double seconds, cpu, wake, mw;          /* mw is mean over mw_n readings, 0 readings: not measured */
    int mw_n, cpu_fail, wake_fail, measured;    /* measured: the process's CPU could be read (Linux) */
    long rss_kb, peak_kb;
    char busiest_thread[24];
} R3PowerResult;

/* How long a power window runs: $R3I6NOS_REPORT_POWER_S when set (as r3i6nos-report does; tests use a fraction of a second), else `def`. */
static inline double r3_report_power_seconds(double def) {
    const char *v = getenv("R3I6NOS_REPORT_POWER_S");
    double x = v && *v ? atof(v) : 0.0;
    return x > 0 ? x : def;
}

/* "<state> <what>" in buf. */
static inline const char *r3_power_key(char *buf, size_t cap, const char *state, const char *what) {
    snprintf(buf, cap, "%s %s", state, what);
    return buf;
}

static inline const char *r3_power_root(const char *env, const char *def) {
    const char *v = getenv(env);
    return v && *v ? v : def;
}

static inline int r3_power_join(char *out, size_t cap, const char *root, const char *rel) {
    int n = snprintf(out, cap, "%s/%s", root, rel);
    return n < 0 || (size_t)n >= cap ? -1 : 0;
}

/* /proc/<x>/stat: the name between the first "(" and the last ")", and utime + stime in clock ticks. 0 when read. */
static inline int r3_power_stat(const char *path, R3PowerTask *t) {
    char buf[1024];
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    char *open = strchr(buf, '('), *close = strrchr(buf, ')');
    if (!open || !close || close < open) return -1;
    size_t len = (size_t)(close - open - 1);
    if (len >= sizeof t->name) len = sizeof t->name - 1;
    memcpy(t->name, open + 1, len);
    t->name[len] = 0;
    unsigned long long ut = 0, st = 0;
    if (sscanf(close + 1, " %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &ut, &st) != 2) return -1;
    t->ticks = ut + st;
    return 0;
}

/* The two context-switch counters of a /proc/<x>/status, added. */
static inline unsigned long long r3_power_ctx(const char *status) {
    char a[64], b[64];
    unsigned long long x = 0, y = 0;
    if (r3_report_field(status, "voluntary_ctxt_switches", a, sizeof a) == 0) x = strtoull(a, NULL, 10);
    if (r3_report_field(status, "nonvoluntary_ctxt_switches", b, sizeof b) == 0) y = strtoull(b, NULL, 10);
    return x + y;
}

/* The system's counters from /proc/stat: all CPU time and idle time (ticks), interrupts and context switches. 0 when read. */
static inline int r3_power_system(unsigned long long *total, unsigned long long *idle, unsigned long long *intr, unsigned long long *ctxt) {
    char path[PATH_MAX];
    if (r3_power_join(path, sizeof path, r3_power_root("R3I6NOS_PROC", "/proc"), "stat") != 0) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[1024];
    int got = 0;
    *total = *idle = *intr = *ctxt = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned long long v[8] = {0};
        if (!strncmp(line, "cpu ", 4)) {
            if (sscanf(line + 4, "%llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) >= 4) {
                for (int i = 0; i < 8; i++) *total += v[i];
                *idle = v[3] + v[4];
                got = 1;
            }
        } else if (!strncmp(line, "intr ", 5)) *intr = strtoull(line + 5, NULL, 10);
        else if (!strncmp(line, "ctxt ", 5)) *ctxt = strtoull(line + 5, NULL, 10);
    }
    fclose(f);
    return got ? 0 : -1;
}

/* Every thread of this process (ticks, context switches, name), up to R3_POWER_MAXTHR. Caller frees. */
static inline R3PowerTask *r3_power_threads(int *count) {
    char dirp[PATH_MAX], p[PATH_MAX + 300];
    *count = 0;
    if (r3_power_join(dirp, sizeof dirp, r3_power_root("R3I6NOS_PROC", "/proc"), "self/task") != 0) return NULL;
    DIR *d = opendir(dirp);
    if (!d) return NULL;
    R3PowerTask *out = (R3PowerTask *)calloc(R3_POWER_MAXTHR, sizeof *out);
    struct dirent *e;
    while (out && (e = readdir(d)) && *count < R3_POWER_MAXTHR) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        R3PowerTask *t = &out[*count];
        snprintf(p, sizeof p, "%s/%s/stat", dirp, e->d_name);
        if (r3_power_stat(p, t) != 0) continue;
        t->id = atoi(e->d_name);
        snprintf(p, sizeof p, "%s/%s/status", dirp, e->d_name);
        t->ctx = r3_power_ctx(p);
        (*count)++;
    }
    closedir(d);
    return out;
}

/* Every other process (ticks, name), up to R3_POWER_MAXPROC. Caller frees. */
static inline R3PowerTask *r3_power_procs(int *count) {
    const char *root = r3_power_root("R3I6NOS_PROC", "/proc");
    char p[PATH_MAX + 300];
    *count = 0;
    DIR *d = opendir(root);
    if (!d) return NULL;
    R3PowerTask *out = (R3PowerTask *)calloc(R3_POWER_MAXPROC, sizeof *out);
    struct dirent *e;
    while (out && (e = readdir(d)) && *count < R3_POWER_MAXPROC) {
        if (!isdigit((unsigned char)e->d_name[0]) || atoi(e->d_name) == (int)getpid()) continue;
        R3PowerTask *t = &out[*count];
        snprintf(p, sizeof p, "%s/%s/stat", root, e->d_name);
        if (r3_power_stat(p, t) != 0) continue;
        t->id = atoi(e->d_name);
        (*count)++;
    }
    closedir(d);
    return out;
}

/* The battery supply's folder (type Battery, else a name with "bat" or "bms"), or -1. */
static inline int r3_power_battery(char *dir, size_t cap) {
    char base[PATH_MAX], t[PATH_MAX + 300], v[64];
    if (r3_power_join(base, sizeof base, r3_power_root("R3I6NOS_SYS", "/sys"), "class/power_supply") != 0) return -1;
    DIR *d = opendir(base);
    if (!d) return -1;
    struct dirent *e;
    int found = -1;
    char byname[PATH_MAX + 300] = "";
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        snprintf(t, sizeof t, "%s/%s/type", base, e->d_name);
        if (r3_report_field(t, "", v, sizeof v) == 0 && !strcasecmp(v, "battery")) {
            if (snprintf(dir, cap, "%s/%s", base, e->d_name) < (int)cap) found = 0;
            break;
        }
        char low[300];
        snprintf(low, sizeof low, "%s", e->d_name);
        for (char *c = low; *c; c++) *c = (char)tolower((unsigned char)*c);
        if (!byname[0] && (strstr(low, "bat") || !strncmp(low, "bms", 3))) snprintf(byname, sizeof byname, "%s/%s", base, e->d_name);
    }
    closedir(d);
    if (found != 0 && byname[0] && snprintf(dir, cap, "%s", byname) < (int)cap) found = 0;
    return found;
}

static inline int r3_power_num(const char *dir, const char *file, long long *out) {
    char p[PATH_MAX + 64], v[64];
    snprintf(p, sizeof p, "%s/%s", dir, file);
    if (r3_report_field(p, "", v, sizeof v) != 0 || !v[0]) return -1;
    char *end;
    *out = strtoll(v, &end, 10);
    return end == v ? -1 : 0;
}

static inline int r3_power_discharging(const char *dir) {
    char p[PATH_MAX + 64], v[64];
    snprintf(p, sizeof p, "%s/status", dir);
    return r3_report_field(p, "", v, sizeof v) == 0 && !strcmp(v, "Discharging");
}

/* The battery's draw now in mW (|current| x voltage), or -1 when it does not report both. */
static inline double r3_power_draw_mw(const char *dir) {
    long long i, v;
    if (r3_power_num(dir, "current_now", &i) != 0 || r3_power_num(dir, "voltage_now", &v) != 0 || v <= 0) return -1;
    return (double)(i < 0 ? -i : i) * (double)v / 1e9;
}

/* == power ==: the section header and the battery's state. Say to unplug when it is not discharging. */
static inline void r3_report_power_section(R3Report *r) {
    char b[PATH_MAX], v[64];
    r3_report_section(r, "power");
    if (r3_power_battery(b, sizeof b) != 0) {
        r3_report_line(r, R3_SKIP, "battery", "no battery supply found, so no draw can be measured (CPU time and wake-ups still are)");
        return;
    }
    char p[PATH_MAX + 64], st[64] = "", cap[64] = "?";
    snprintf(p, sizeof p, "%s/status", b);
    if (r3_report_field(p, "", st, sizeof st) != 0) snprintf(st, sizeof st, "status not reported");
    snprintf(p, sizeof p, "%s/capacity", b);
    if (r3_report_field(p, "", v, sizeof v) == 0) snprintf(cap, sizeof cap, "%s", v);
    r3_report_line(r, R3_INFO, "battery supply", "%s%%, %s", cap, st);
    if (!r3_power_discharging(b)) r3_report_line(r, R3_SKIP, "draw", "the battery says %s, not Discharging: unplug the power and run again for the draw in mW", st);
    else if (r3_power_draw_mw(b) < 0) r3_report_line(r, R3_SKIP, "draw", "current_now is not reported: only CPU time and wake-ups are measured");
}

/* Start a window: snapshot this process, its threads, every process, the system and the battery. `clock` is a milliseconds clock (NULL:
 * the steady clock; a test passes a fake one). */
static inline void r3_power_begin_with(R3Power *p, double (*clock)(void)) {
    memset(p, 0, sizeof *p);
    p->clock_ms = clock ? clock : r3_report_ms;
    p->mw_min = 1e18;
    char path[PATH_MAX];
    R3PowerTask self = {0};
    if (r3_power_join(path, sizeof path, r3_power_root("R3I6NOS_PROC", "/proc"), "self/stat") == 0 && r3_power_stat(path, &self) == 0) p->self_ticks0 = self.ticks;
    r3_power_system(&p->sys_total0, &p->sys_idle0, &p->sys_intr0, &p->sys_ctxt0);
    p->thr0 = r3_power_threads(&p->nthr0);
    p->procs0 = r3_power_procs(&p->nprocs0);
    p->t0 = p->last_sample = p->clock_ms();
    p->last_sample -= 1e6;      /* the first sample is taken */
}

static inline void r3_power_begin(R3Power *p) { r3_power_begin_with(p, NULL); }

/* Read the battery's draw, at most once a second. Call it about once a second while the window runs. */
static inline void r3_power_sample(R3Power *p) {
    double now = p->clock_ms();
    if (now - p->last_sample < 900.0) return;
    p->last_sample = now;
    char b[PATH_MAX];
    if (r3_power_battery(b, sizeof b) != 0 || !r3_power_discharging(b)) return;
    double mw = r3_power_draw_mw(b);
    if (mw < 0) return;
    p->mw_sum += mw;
    if (mw < p->mw_min) p->mw_min = mw;
    if (mw > p->mw_max) p->mw_max = mw;
    p->mw_n++;
}

/* End a window and write its lines under `state` ("idle", "working"): the process's CPU and wake-ups against the goals (0 or less: no goal, an
 * INFO), memory, the busiest threads and other processes, the draw in mW and the time left at it. Fills *res (may be NULL). */
static inline void r3_report_power_end(R3Report *r, R3Power *p, const char *state, double goal_cpu, double goal_wake, R3PowerResult *res) {
    R3PowerResult local;
    if (!res) res = &local;
    memset(res, 0, sizeof *res);
    r3_power_sample(p);
    double wall = (p->clock_ms() - p->t0) / 1000.0;
    if (wall < 1e-3) wall = 1e-3;
    double clk = (double)sysconf(_SC_CLK_TCK);
    if (clk <= 0) clk = 100.0;
    res->seconds = wall;
    char key[96], path[PATH_MAX];
    R3PowerTask self = {0};
    int have_self = r3_power_join(path, sizeof path, r3_power_root("R3I6NOS_PROC", "/proc"), "self/stat") == 0 && r3_power_stat(path, &self) == 0;
    r3_report_line(r, R3_INFO, r3_power_key(key, sizeof key, state, "window"), "%.*f s, this app %s", wall >= 10 ? 0 : 1, wall, !strcmp(state, "idle") ? "open and waiting, nothing pressed" : "doing its work");
    if (have_self) {
        res->measured = 1;
        res->cpu = (double)(self.ticks > p->self_ticks0 ? self.ticks - p->self_ticks0 : 0) / clk / wall * 100.0;
        r3_power_key(key, sizeof key, state, "cpu");
        if (goal_cpu > 0) {
            res->cpu_fail = res->cpu > goal_cpu;
            r3_report_line(r, res->cpu_fail ? R3_FAIL : R3_PASS, key, "%.2f%% of a core (goal at most %.1f%%)", res->cpu, goal_cpu);
        } else r3_report_line(r, R3_INFO, key, "%.2f%% of a core", res->cpu);
    } else r3_report_line(r, R3_SKIP, r3_power_key(key, sizeof key, state, "cpu"), "no /proc/self/stat here (not Linux)");
    /* threads: context switches (wake-ups) summed over the threads in both snapshots, the busiest by CPU */
    int n1 = 0;
    R3PowerTask *t1 = r3_power_threads(&n1);
    double best = -1;
    unsigned long long wakes = 0;
    for (int i = 0; i < n1; i++)
        for (int j = 0; j < p->nthr0; j++) {
            if (t1[i].id != p->thr0[j].id) continue;
            wakes += t1[i].ctx > p->thr0[j].ctx ? t1[i].ctx - p->thr0[j].ctx : 0;
            double c = (double)(t1[i].ticks > p->thr0[j].ticks ? t1[i].ticks - p->thr0[j].ticks : 0) / clk / wall * 100.0;
            if (c > best) { best = c; snprintf(res->busiest_thread, sizeof res->busiest_thread, "%s", t1[i].name); }
        }
    if (n1 > 0 && p->nthr0 > 0) {
        res->wake = (double)wakes / wall;
        r3_power_key(key, sizeof key, state, "wakeups");
        if (goal_wake > 0) {
            res->wake_fail = res->wake > goal_wake;
            r3_report_line(r, res->wake_fail ? R3_FAIL : R3_PASS, key, "%.1f/s (goal at most %.1f)", res->wake, goal_wake);
        } else r3_report_line(r, R3_INFO, key, "%.1f/s", res->wake);
        if (best >= 0) r3_report_line(r, R3_INFO, r3_power_key(key, sizeof key, state, "busiest thread"), "%s, %.2f%% of a core (%d threads)", res->busiest_thread, best, n1);
    } else r3_report_line(r, R3_SKIP, r3_power_key(key, sizeof key, state, "wakeups"), "no /proc/self/task here (not Linux)");
    free(t1);
    char rss[64], hwm[64], sp[PATH_MAX];
    if (r3_power_join(sp, sizeof sp, r3_power_root("R3I6NOS_PROC", "/proc"), "self/status") == 0 && r3_report_field(sp, "VmRSS", rss, sizeof rss) == 0 &&
        r3_report_field(sp, "VmHWM", hwm, sizeof hwm) == 0) {
        res->rss_kb = atol(rss);
        res->peak_kb = atol(hwm);
        r3_report_line(r, R3_INFO, r3_power_key(key, sizeof key, state, "memory"), "%ld kB resident, peak %ld kB", res->rss_kb, res->peak_kb);
    }
    /* the system: busy %, interrupts and context switches a second, and the busiest other processes (a surprise shows) */
    unsigned long long total, idle, intr, ctxt;
    if (r3_power_system(&total, &idle, &intr, &ctxt) == 0 && total > p->sys_total0) {
        double dt = (double)(total - p->sys_total0);
        r3_report_line(r, R3_INFO, r3_power_key(key, sizeof key, state, "system busy"), "%.1f%% of all CPUs, %.0f interrupts/s, %.0f context switches/s",
                       100.0 * (dt - (double)((idle - p->sys_idle0))) / dt, (double)(intr - p->sys_intr0) / wall, (double)(ctxt - p->sys_ctxt0) / wall);
    }
    int n2 = 0;
    R3PowerTask *pr = r3_power_procs(&n2);
    char taken[R3_POWER_MAXPROC] = {0};
    for (int shown = 0; shown < 3 && pr; shown++) {
        int top = -1;
        double topc = 0;
        for (int i = 0; i < n2; i++) {
            if (taken[i]) continue;
            for (int j = 0; j < p->nprocs0; j++) {
                if (pr[i].id != p->procs0[j].id) continue;
                double c = (double)(pr[i].ticks > p->procs0[j].ticks ? pr[i].ticks - p->procs0[j].ticks : 0) / clk / wall * 100.0;
                if (c > topc) { topc = c; top = i; }
            }
        }
        if (top < 0) break;
        taken[top] = 1;
        snprintf(key, sizeof key, "%s busiest other %s", state, pr[top].name);
        r3_report_line(r, R3_INFO, key, "%.2f%% of a core (pid %d)", topc, pr[top].id);
    }
    free(pr);
    r3_power_key(key, sizeof key, state, "draw");
    if (p->mw_n > 0) {
        res->mw = p->mw_sum / p->mw_n;
        res->mw_n = p->mw_n;
        r3_report_line(r, R3_INFO, key, "%.0f mW mean (min %.0f, max %.0f, %d readings)", res->mw, p->mw_min, p->mw_max, p->mw_n);
        char b[PATH_MAX];
        long long full, volt, cap;
        if (r3_power_battery(b, sizeof b) == 0 && r3_power_num(b, "charge_full", &full) == 0 && r3_power_num(b, "voltage_now", &volt) == 0 &&
            r3_power_num(b, "capacity", &cap) == 0 && full > 0 && res->mw > 0)
            r3_report_line(r, R3_INFO, r3_power_key(key, sizeof key, state, "time left"), "%.1f h at this draw (%lld%% of %.0f mAh)",
                           (double)full * (double)volt / 1e12 * (double)cap / 100.0 / (res->mw / 1000.0), cap, (double)full / 1000.0);
    } else r3_report_line(r, R3_SKIP, key, "not measured: needs the battery Discharging with current_now (see the battery line above)");
    free(p->thr0);
    free(p->procs0);
    p->thr0 = p->procs0 = NULL;
    p->nthr0 = p->nprocs0 = 0;
}

/* A whole idle window: `seconds` with the app open and nothing pressed, against the default idle goals. `tick` is one turn of the app's own
 * idle loop and must block (SDL_WaitEventTimeout for up to a second), or NULL: the report then sleeps a second at a time. A tick that
 * returns at once is a busy loop, and the numbers will say so. */
static inline void r3_report_power_idle(R3Report *r, double seconds, void (*tick)(void *), void *arg, R3PowerResult *res) {
    R3Power p;
    r3_power_begin(&p);
    while (p.clock_ms() - p.t0 < seconds * 1000.0) {
        r3_power_sample(&p);
        if (tick) tick(arg);
        else {
            double left = seconds - (p.clock_ms() - p.t0) / 1000.0;
            struct timespec ts;
            if (left > 1.0) left = 1.0;
            if (left <= 0) break;
            ts.tv_sec = (time_t)left;
            ts.tv_nsec = (long)((left - (double)ts.tv_sec) * 1e9);
            nanosleep(&ts, NULL);
        }
    }
    r3_report_power_end(r, &p, "idle", R3_POWER_IDLE_CPU, R3_POWER_IDLE_WAKE, res);
}

/* The last line of the power section: what to cut first. idle or working may be NULL. */
static inline void r3_report_power_advice(R3Report *r, const R3PowerResult *idle, const R3PowerResult *working) {
    if (idle && (idle->cpu_fail || idle->wake_fail))
        r3_report_line(r, R3_INFO, "cut first", "idle: the app is not waiting (%.1f%% of a core, %.0f wake-ups/s, busiest thread %s, with nothing pressed): block on input or a timer "
                       "and redraw only when something changed (the menu went from 60%% to a few this way)", idle->cpu, idle->wake, idle->busiest_thread[0] ? idle->busiest_thread : "?");
    else if (working && (working->cpu_fail || working->wake_fail))
        r3_report_line(r, R3_INFO, "cut first", "working: %.1f%% of a core, %.0f wake-ups/s, busiest thread %s: lower the frame rate or the work of the feature the tour was in",
                       working->cpu, working->wake, working->busiest_thread[0] ? working->busiest_thread : "?");
    else if ((!idle || !idle->measured) && (!working || !working->measured))
        r3_report_line(r, R3_SKIP, "cut first", "nothing was measured here (not Linux, or no window): run the report on the unit");
    else r3_report_line(r, R3_INFO, "cut first", "nothing flagged in this window: repeat at 100%%, 50%% and 20%% charge and on a warm unit before trusting a small difference");
}

#endif /* R3I6NOS_REPORT_H */

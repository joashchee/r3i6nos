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
 *       r3_report_close(&r);             // summary, "end", rename into place
 *   }
 *
 * Every line also goes to stdout. Define the POSIX/GNU feature macros (e.g. _GNU_SOURCE) before including system headers.
 */
#ifndef R3I6NOS_REPORT_H
#define R3I6NOS_REPORT_H

#include <SDL.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#endif /* R3I6NOS_REPORT_H */

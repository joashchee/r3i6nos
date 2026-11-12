/* The device report helper (theme/r3i6nos_report.h, docs/DEVICE-REPORTS.md): a report written to a temporary folder must follow the
 * grammar line for line, count its statuses, end with "end", and leave no temp file. Built and run by scripts/check.sh. */
#define _GNU_SOURCE
#include "r3i6nos_report.h"

static int fails;
static double fake_now;
static double fake_clock(void) { return fake_now; }
static void wf(const char *path, const char *text) { /* a file, its folders made */
    char d[PATH_MAX];
    snprintf(d, sizeof d, "%s", path);
    char *sl = strrchr(d, '/');
    if (sl) { *sl = 0; r3_report_mkdirs(d); }
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}
static char *slurp(const char *path) {
    static char buf[16384];
    FILE *f = fopen(path, "r");
    size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
    if (f) fclose(f);
    buf[n] = 0;
    return buf;
}
#define CHECK(c, m) do { if (!(c)) { fails++; fprintf(stderr, "FAIL: %s\n", m); } } while (0)


/* ---- r3_report_power: a fake /proc and /sys, and a fake clock, so every figure is exact ---- */
static void proc_files(const char *proc, int ut, int st, int ctx1, int ctx2, int t1, int t2, int busy, int idle_ticks, int sysbase, int bigger, int smaller) {
    char p[PATH_MAX + 64], text[512];
    snprintf(p, sizeof p, "%s/stat", proc);
    snprintf(text, sizeof text, "cpu  %d 0 0 %d 0 0 0 0\nintr %d\nctxt %d\n", busy, idle_ticks, 1000 + sysbase * 10, 5000 + sysbase * 50);
    wf(p, text);
    snprintf(p, sizeof p, "%s/self/stat", proc);
    snprintf(text, sizeof text, "123 (test app) S 1 1 1 0 -1 4194304 0 0 0 0 %d %d 0 0 20 0 2 0 1 1000 100\n", ut, st);
    wf(p, text);
    snprintf(p, sizeof p, "%s/self/status", proc);
    wf(p, "Name:\ttest app\nVmHWM:\t    9000 kB\nVmRSS:\t    8000 kB\n");
    snprintf(p, sizeof p, "%s/self/task/123/stat", proc);
    snprintf(text, sizeof text, "123 (render) S 1 1 1 0 -1 4194304 0 0 0 0 %d 0 0 0 20 0 2 0 1 1000 100\n", t1);
    wf(p, text);
    snprintf(p, sizeof p, "%s/self/task/123/status", proc);
    snprintf(text, sizeof text, "voluntary_ctxt_switches:\t%d\nnonvoluntary_ctxt_switches:\t2\n", ctx1);
    wf(p, text);
    snprintf(p, sizeof p, "%s/self/task/124/stat", proc);
    snprintf(text, sizeof text, "124 (audio) S 1 1 1 0 -1 4194304 0 0 0 0 %d 0 0 0 20 0 2 0 1 1000 100\n", t2);
    wf(p, text);
    snprintf(p, sizeof p, "%s/self/task/124/status", proc);
    snprintf(text, sizeof text, "voluntary_ctxt_switches:\t%d\nnonvoluntary_ctxt_switches:\t0\n", ctx2);
    wf(p, text);
    snprintf(p, sizeof p, "%s/900/stat", proc);
    snprintf(text, sizeof text, "900 (busy d) R 1 1 1 0 -1 4194304 0 0 0 0 %d 0 0 0 20 0 1 0 1 1000 100\n", bigger);
    wf(p, text);
    snprintf(p, sizeof p, "%s/901/stat", proc);
    snprintf(text, sizeof text, "901 (idled) S 1 1 1 0 -1 4194304 0 0 0 0 %d 0 0 0 20 0 1 0 1 1000 100\n", smaller);
    wf(p, text);
}

static void battery_files(const char *sys, const char *status, int current_ua) {
    char d[PATH_MAX + 64], p[PATH_MAX + 128], num[32];
    snprintf(d, sizeof d, "%s/class/power_supply/BAT0", sys);
    const char *names[] = {"type", "status", "capacity", "current_now", "voltage_now", "charge_full"};
    snprintf(num, sizeof num, "%d", current_ua);
    const char *vals[] = {"Battery", status, "80", num, "3900000", "3000000"};
    for (int i = 0; i < 6; i++) { snprintf(p, sizeof p, "%s/%s", d, names[i]); wf(p, vals[i]); }
}

static void power_test(const char *base) {
    char proc[PATH_MAX + 16], sys[PATH_MAX + 16], dir[PATH_MAX + 16], path[PATH_MAX + 64];
    snprintf(proc, sizeof proc, "%s/fakeproc", base);
    snprintf(sys, sizeof sys, "%s/fakesys", base);
    snprintf(dir, sizeof dir, "%s/Power App", base);
    setenv("R3I6NOS_PROC", proc, 1);
    setenv("R3I6NOS_SYS", sys, 1);
    long clk = sysconf(_SC_CLK_TCK);
    CHECK(clk == 100, "this test's figures assume 100 clock ticks a second");
    R3Report r = {0};
    r.quiet = 1;
    CHECK(r3_report_open(&r, dir, "Power App", "1.0", "r36s-power", "--report") == 0, "open the power report");
    battery_files(sys, "Discharging", -300000);
    proc_files(proc, 100, 0, 10, 10, 50, 20, 1000, 800, 0, 100, 100);
    r3_report_power_section(&r);
    /* working: 10 s on the fake clock; CPU +40 ticks (4%), wake-ups +150 (15/s), the draw read three times */
    R3Power p;
    fake_now = 0;
    r3_power_begin_with(&p, fake_clock);
    r3_power_sample(&p);                                    /* 1170 mW */
    battery_files(sys, "Discharging", -400000);
    fake_now = 500;
    r3_power_sample(&p);                                    /* too soon: ignored */
    fake_now = 5000;
    r3_power_sample(&p);                                    /* 1560 mW */
    proc_files(proc, 130, 10, 110, 60, 80, 30, 1100, 1700, 1000, 300, 110);
    fake_now = 10000;
    R3PowerResult w;
    r3_report_power_end(&r, &p, "working", 3.0, 25.0, &w);  /* at the end: a third reading, 1560 mW */
    CHECK(w.cpu > 3.99 && w.cpu < 4.01, "working cpu is 4%");
    CHECK(w.cpu_fail && !w.wake_fail, "4% misses a 3% goal; 15 a second meets 25");
    CHECK(w.wake > 14.99 && w.wake < 15.01, "15 wake-ups a second over the two threads");
    CHECK(w.mw_n == 3 && w.mw > 1429 && w.mw < 1431, "mean of 1170, 1560 and 1560 mW");
    CHECK(w.rss_kb == 8000 && w.peak_kb == 9000, "memory");
    CHECK(!strcmp(w.busiest_thread, "render"), "the busiest thread");
    /* idle: nothing changes in the fake files, a real 50 ms window */
    battery_files(sys, "Charging", -300000);
    R3PowerResult idle;
    r3_report_power_idle(&r, 0.05, NULL, NULL, &idle);
    CHECK(idle.cpu == 0 && idle.wake == 0 && !idle.cpu_fail && !idle.wake_fail, "idle: nothing used, goals met");
    CHECK(idle.mw_n == 0, "no draw while the battery is Charging");
    r3_report_power_advice(&r, &idle, &w);
    r3_report_power_advice(&r, &idle, NULL);
    R3PowerResult none = {0};
    r3_report_power_advice(&r, &none, &none);
    R3PowerResult bad = {0};
    bad.cpu_fail = 1;
    bad.cpu = 60;
    r3_report_power_advice(&r, &bad, &w);
    CHECK(r3_report_close(&r) == 0, "close the power report");
    snprintf(path, sizeof path, "%s/r36s-report.txt", dir);
    char *t = slurp(path);
    const char *want[] = {
        "== power ==", "INFO battery supply: 80%, Discharging",
        "INFO working window: 10 s, this app doing its work",
        "FAIL working cpu: 4.00% of a core (goal at most 3.0%)",
        "PASS working wakeups: 15.0/s (goal at most 25.0)",
        "INFO working busiest thread: render, 3.00% of a core (2 threads)",
        "INFO working memory: 8000 kB resident, peak 9000 kB",
        "INFO working system busy: 10.0% of all CPUs, 1000 interrupts/s, 5000 context switches/s",
        "INFO working busiest other busy d: 20.00% of a core (pid 900)",
        "INFO working busiest other idled: 1.00% of a core (pid 901)",
        "INFO working draw: 1430 mW mean (min 1170, max 1560, 3 readings)",
        "INFO working time left: 6.5 h at this draw (80% of 3000 mAh)",
        "PASS idle cpu: 0.00% of a core (goal at most 2.0%)",
        "PASS idle wakeups: 0.0/s (goal at most 25.0)",
        "SKIP idle draw: not measured: needs the battery Discharging",
        "INFO cut first: working: 4.0% of a core, 15 wake-ups/s, busiest thread render",
        "INFO cut first: nothing flagged in this window",
        "SKIP cut first: nothing was measured here",
        "INFO cut first: idle: the app is not waiting (60.0% of a core",
    };
    for (size_t i = 0; i < sizeof want / sizeof *want; i++) {
        char m[200];
        snprintf(m, sizeof m, "the power report says: %s", want[i]);
        CHECK(strstr(t, want[i]) != NULL, m);
    }
    /* no battery at all, and no /proc: SKIP lines, never a crash */
    char nobat[PATH_MAX + 16];
    snprintf(nobat, sizeof nobat, "%s/nosys", base);
    setenv("R3I6NOS_SYS", nobat, 1);
    setenv("R3I6NOS_PROC", nobat, 1);
    R3Report r2 = {0};
    r2.quiet = 1;
    snprintf(dir, sizeof dir, "%s/Power App 2", base);
    CHECK(r3_report_open(&r2, dir, "P", "1", "p", "t") == 0, "open the second power report");
    r3_report_power_section(&r2);
    r3_report_power_idle(&r2, 0.01, NULL, NULL, NULL);
    r3_report_close(&r2);
    snprintf(path, sizeof path, "%s/r36s-report.txt", dir);
    t = slurp(path);
    CHECK(strstr(t, "SKIP battery: no battery supply found") != NULL, "no battery: said so");
    CHECK(strstr(t, "SKIP idle cpu: no /proc/self/stat") != NULL, "no /proc: said so");
    CHECK(r2.fail == 0, "nothing here fails for lack of a battery or /proc");
    unsetenv("R3I6NOS_PROC");
    unsetenv("R3I6NOS_SYS");
}

int main(void) {
    char tmpl[PATH_MAX];
    const char *t = getenv("TMPDIR");
    snprintf(tmpl, sizeof tmpl, "%s/r3-report-XXXXXX", t && *t ? t : "/tmp");
    char *base = mkdtemp(tmpl);
    if (!base) { perror("mkdtemp"); return 1; }
    char dir[PATH_MAX + 16];
    snprintf(dir, sizeof dir, "%s/Test App", base); /* a space, as a card folder may have */
    /* A stand-in for the root helper: the report must pass on its playback path, jack and output lines and nothing else. */
    char fake[PATH_MAX + 32];
    snprintf(fake, sizeof fake, "%s/fake-sound", base);
    FILE *fs = fopen(fake, "w");
    if (!fs) { perror("fake"); return 1; }
    fputs("#!/bin/sh\necho 'cards: 1'\necho 'output: card 0'\necho 'card 0 playback path: HP (of OFF SPK HP SPK_HP)'\n"
          "echo 'jack: rk817 Headphones'\necho 'headphones: in'\necho 'mixer Volume: 80%'\n", fs);
    fclose(fs);
    chmod(fake, 0755);
    setenv("R3I6NOS_SOUND", fake, 1);
    R3Report r = {0};
    r.quiet = 1;
    CHECK(r3_report_open(&r, dir, "Test App", "1.2.3", "r36s-test", "--report") == 0, "open");
    r3_report_system(&r);
    r3_report_card(&r, dir);
    r3_report_section(&r, "speed");
    r3_report_line(&r, R3_PASS, "engine", "%.1fx real time (goal 2x)", 3.5);
    r3_report_line(&r, R3_FAIL, "other\nengine", "1.0x\nreal time");
    r3_report_check(&r, 1, "a check", "ok");
    r3_report_memory(&r);
    int pass = r.pass, fail = r.fail, info = r.info, skip = r.skip;
    CHECK(r3_report_close(&r) == 0, "close");

    char path[PATH_MAX + 64];
    snprintf(path, sizeof path, "%s/r36s-report.txt", dir);
    FILE *f = fopen(path, "r");
    CHECK(f != NULL, "the report is on the card");
    char line[1024], last[1024] = "", summary[128];
    int n = 0, bad = 0, sections = 0, header = 0, route = 0;
    snprintf(summary, sizeof summary, "PASS %d  FAIL %d  INFO %d  SKIP %d", pass, fail, info, skip);
    int saw_summary = 0;
    while (f && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        n++;
        if (n == 1) { CHECK(!strcmp(line, "# r3i6nos device report"), "the first line"); continue; }
        if (!strncmp(line, "app: Test App 1.2.3 (r36s-test)", 31) || !strncmp(line, "made: ", 6) || !strncmp(line, "how: --report", 13) ||
            !strncmp(line, "for: Claude. Copy EASYROMS/Test App/r36s-report.txt", 51)) { header++; continue; }
        if (!line[0]) continue;
        if (!strncmp(line, "== ", 3)) { sections++; continue; }
        if (!strcmp(line, summary)) { saw_summary = 1; continue; }
        if (!strcmp(line, "INFO card 0 playback path: HP (of OFF SPK HP SPK_HP)")) route |= 1;
        if (!strcmp(line, "INFO headphones: in")) route |= 2;
        if (!strncmp(line, "INFO mixer ", 11) || !strncmp(line, "INFO cards: ", 12)) route |= 4;
        if (!strcmp(line, "end")) { snprintf(last, sizeof last, "%s", line); continue; }
        int ok = (!strncmp(line, "PASS ", 5) || !strncmp(line, "FAIL ", 5) || !strncmp(line, "INFO ", 5) || !strncmp(line, "SKIP ", 5)) && strstr(line, ": ");
        if (!ok) { bad++; fprintf(stderr, "not in the grammar: %s\n", line); }
        snprintf(last, sizeof last, "%s", line);
    }
    if (f) fclose(f);
    CHECK(route == 3, "the Playback Path and the headphones line are in the report, the mixer's lines are not");
    CHECK(header == 4, "the four header lines");
    CHECK(sections == 4, "system, card, speed, summary");
    CHECK(bad == 0, "every line is STATUS key: value");
    CHECK(saw_summary, "the summary counts every status");
    CHECK(!strcmp(last, "end"), "the last line is end");
    SDL_InitSubSystem(SDL_INIT_JOYSTICK);
    int pads = SDL_NumJoysticks(); /* no pad here is a FAIL of its own */
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    CHECK(fail == 1 + (pads == 0) && pass >= 6, "card checks pass here, one FAIL written");
    snprintf(path, sizeof path, "%s/r36s-report.txt.tmp", dir);
    CHECK(access(path, F_OK) != 0, "no temp file left");
    snprintf(path, sizeof path, "%s/.r36s-report-test", dir);
    CHECK(access(path, F_OK) != 0, "the card test cleans up");
    R3Report r2 = {0};
    r2.quiet = 1;
    CHECK(r3_report_open(&r2, "/dev/null/nowhere", "X", "1", "x", "test") != 0, "an unwritable folder is an error, not a crash");
    power_test(base);
    char cmd[PATH_MAX + 16];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", base);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", base);
    printf("report helper: %s (%d lines)\n", fails ? "FAILED" : "ok", n);
    return fails ? 1 : 0;
}

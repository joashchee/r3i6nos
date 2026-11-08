/* The device report helper (theme/r3i6nos_report.h, docs/DEVICE-REPORTS.md): a report written to a temporary folder must follow the
 * grammar line for line, count its statuses, end with "end", and leave no temp file. Built and run by scripts/check.sh. */
#define _GNU_SOURCE
#include "r3i6nos_report.h"

static int fails;
#define CHECK(c, m) do { if (!(c)) { fails++; fprintf(stderr, "FAIL: %s\n", m); } } while (0)

int main(void) {
    char tmpl[PATH_MAX];
    const char *t = getenv("TMPDIR");
    snprintf(tmpl, sizeof tmpl, "%s/r3-report-XXXXXX", t && *t ? t : "/tmp");
    char *base = mkdtemp(tmpl);
    if (!base) { perror("mkdtemp"); return 1; }
    char dir[PATH_MAX + 16];
    snprintf(dir, sizeof dir, "%s/Test App", base); /* a space, as a card folder may have */
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
    int n = 0, bad = 0, sections = 0, header = 0;
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
        if (!strcmp(line, "end")) { snprintf(last, sizeof last, "%s", line); continue; }
        int ok = (!strncmp(line, "PASS ", 5) || !strncmp(line, "FAIL ", 5) || !strncmp(line, "INFO ", 5) || !strncmp(line, "SKIP ", 5)) && strstr(line, ": ");
        if (!ok) { bad++; fprintf(stderr, "not in the grammar: %s\n", line); }
        snprintf(last, sizeof last, "%s", line);
    }
    if (f) fclose(f);
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
    char cmd[PATH_MAX + 16];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", base);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", base);
    printf("report helper: %s (%d lines)\n", fails ? "FAILED" : "ok", n);
    return fails ? 1 : 0;
}

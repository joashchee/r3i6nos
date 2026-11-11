/* The Rec demo recorder (theme/r3i6nos_demo.h, docs/DEMO-RECORDINGS.md): a tour of 32 seconds on a fake app must leave numbered PNGs that
 * decode to the screen, a WAV in step with the frames, the demo file's copy, a log in the report grammar ending with "end", and, where
 * ffmpeg is installed, a demo-<stamp>.mp4; without it (or with a broken one) the log must say FAIL and the frames and WAV must be kept.
 * Built and run by scripts/check.sh. */
#define _GNU_SOURCE
#include "r3i6nos_ansiapps.h"
#include "r3i6nos_report.h"
#include "r3i6nos_demo.h"
#include <math.h>
#include <sys/wait.h>

static int fails;
#define CHECK(c, m) do { if (!(c)) { fails++; fprintf(stderr, "FAIL: %s\n", m); } } while (0)

static long file_size(const char *p) { struct stat st; return stat(p, &st) == 0 ? (long)st.st_size : -1; }

/* Reads a PNG back: every chunk's CRC, the IHDR, the palette, the stored zlib blocks; the pixels must equal r3_render's. */
static int check_png(const char *path, const R3Screen *s) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char sig[8];
    static const unsigned char SIG[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    if (fread(sig, 1, 8, f) != 8 || memcmp(sig, SIG, 8)) { fclose(f); return 0; }
    unsigned char *idat = malloc(2000000), plte[768];
    size_t idn = 0;
    int w = 0, h = 0, depth = 0, ctype = -1, ok = 1, saw_end = 0, np = 0;
    unsigned char hd[8], c[4];
    while (ok && fread(hd, 1, 8, f) == 8) {
        uint32_t n = ((uint32_t)hd[0] << 24) | (hd[1] << 16) | (hd[2] << 8) | hd[3];
        unsigned char *d = malloc(n + 1);
        if (fread(d, 1, n, f) != n || fread(c, 1, 4, f) != 4) { ok = 0; free(d); break; }
        uint32_t crc = r3_demo_crc(r3_demo_crc(0, hd + 4, 4), d, n);
        if (crc != (((uint32_t)c[0] << 24) | (c[1] << 16) | (c[2] << 8) | c[3])) ok = 0;
        if (!memcmp(hd + 4, "IHDR", 4)) { w = (d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3]; h = (d[4] << 24) | (d[5] << 16) | (d[6] << 8) | d[7]; depth = d[8]; ctype = d[9]; }
        else if (!memcmp(hd + 4, "PLTE", 4)) { memcpy(plte, d, n > 768 ? 768 : n); np = (int)n / 3; }
        else if (!memcmp(hd + 4, "IDAT", 4)) { memcpy(idat + idn, d, n); idn += n; }
        else if (!memcmp(hd + 4, "IEND", 4)) saw_end = 1;
        free(d);
    }
    fclose(f);
    if (!ok || !saw_end || w != R3_W || h != R3_H || ctype != 3 || (depth != 4 && depth != 8)) { free(idat); return 0; }
    size_t row = depth == 4 ? (R3_W + 1) / 2 : R3_W;
    uint32_t *want = malloc((size_t)R3_W * R3_H * 4);
    r3_render(s, want);
    size_t at = 2, y = 0; /* skip the zlib header; blocks of stored data */
    unsigned char *raw = malloc((row + 1) * R3_H);
    size_t rn = 0;
    while (at + 5 <= idn && rn < (row + 1) * R3_H) {
        unsigned fin = idat[at], len = idat[at + 1] | (idat[at + 2] << 8), nlen = idat[at + 3] | (idat[at + 4] << 8);
        if ((len ^ nlen) != 0xFFFF) { ok = 0; break; }
        memcpy(raw + rn, idat + at + 5, len);
        rn += len;
        at += 5 + len;
        if (fin) break;
    }
    ok = ok && rn == (row + 1) * R3_H;
    for (y = 0; ok && y < (size_t)R3_H; y++) {
        const unsigned char *o = raw + y * (row + 1);
        if (o[0] != 0) { ok = 0; break; }
        o++;
        for (int x = 0; x < R3_W && ok; x++) {
            int i = depth == 4 ? ((x % 2) ? (o[x / 2] & 15) : (o[x / 2] >> 4)) : o[x];
            if (i >= np) { ok = 0; break; }
            uint32_t rgb = ((uint32_t)plte[i * 3] << 16) | (plte[i * 3 + 1] << 8) | plte[i * 3 + 2];
            if (rgb != (want[y * R3_W + x] & 0xFFFFFF)) ok = 0;
        }
    }
    free(idat); free(want); free(raw);
    return ok;
}

static void note_pct(void *ctx, int pct) { *(int *)ctx = pct; }

static int log_ok(const char *path, const char *must_have, int *fails_in_log) {
    FILE *f = fopen(path, "r");
    char line[1024], last[1024] = "";
    int found = 0, bad = 0;
    *fails_in_log = 0;
    while (f && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        if (line[0]) snprintf(last, sizeof last, "%s", line);
        if (!strncmp(line, "FAIL ", 5)) (*fails_in_log)++;
        if ((!strncmp(line, "PASS ", 5) || !strncmp(line, "FAIL ", 5) || !strncmp(line, "INFO ", 5) || !strncmp(line, "SKIP ", 5)) && !strstr(line, ": ") && !strstr(line, "  FAIL ")) bad++;
        if (must_have && strstr(line, must_have)) found = 1;
    }
    if (f) fclose(f);
    if (!(f && bad == 0 && !strcmp(last, "end") && (!must_have || found)) && f) {
        fprintf(stderr, "log %s: bad lines %d, last '%s', found %d\n", path, bad, last, found);
        char cmd[PATH_MAX + 16];
        snprintf(cmd, sizeof cmd, "cat '%s' >&2", path);
        if (system(cmd) != 0) {}
    }
    return f && bad == 0 && !strcmp(last, "end") && (!must_have || found);
}

static void tour(R3Demo *d, int seconds, R3Screen *shown) {
    float buf[2 * (44100 / R3_DEMO_FPS)];
    for (int i = 0; i < seconds * R3_DEMO_FPS; i++) {
        for (int k = 0; k < 44100 / R3_DEMO_FPS; k++) {
            float v = 0.5f * sinf(6.2831853f * 440.0f * (float)(d->n_audio + k) / 44100.0f);
            buf[2 * k] = buf[2 * k + 1] = v;
        }
        R3Screen s;
        r3_clear(&s);
        r3_text(&s, 2, 2, "Rec demo test", R3_ST(R3_ST_STRONG));
        r3_progress(&s, 2, 4, 40, i * 100 / (seconds * R3_DEMO_FPS));
        if (i == 5) { *shown = s; r3_demo_caption(d, "test-hero", "the first shot, taken at frame 5"); r3_demo_shot(d, &s, "first shot/odd name"); }
        r3_demo_audio(d, buf, 44100 / R3_DEMO_FPS);
        r3_demo_frame(d, &s);
    }
}

int main(void) {
    char tmpl[PATH_MAX];
    const char *t = getenv("TMPDIR");
    snprintf(tmpl, sizeof tmpl, "%s/r3-demo-XXXXXX", t && *t ? t : "/tmp");
    char *base = mkdtemp(tmpl);
    if (!base) { perror("mkdtemp"); return 1; }
    char dir[PATH_MAX + 16], p[PATH_MAX + 128], src[PATH_MAX + 32];
    snprintf(dir, sizeof dir, "%s/Test App/Demo", base);
    snprintf(src, sizeof src, "%s/song.xyz", base);
    FILE *sf = fopen(src, "w");
    fputs("a demo file\n", sf);
    fclose(sf);
    R3Demo d = {0};
    d.log.quiet = 1;
    R3Screen shown = {0};

    /* old files from an earlier run go; others stay */
    r3_report_mkdirs(dir);
    snprintf(p, sizeof p, "%s/99-old.png", dir); sf = fopen(p, "w"); fclose(sf);
    snprintf(p, sizeof p, "%s/keep-me.txt", dir); sf = fopen(p, "w"); fclose(sf);

    /* a rate that is not a multiple of the frame rate is refused */
    CHECK(r3_demo_open(&d, dir, "Test App", "1.2.3", "r36s-test", "--rec-demo", NULL, 44101) == -1, "an odd rate is refused");
    d.log.quiet = 1;

    /* with a broken ffmpeg: the log says FAIL, the frames and WAV stay, the PNGs and the copy are there */
    setenv("R3I6NOS_FFMPEG", "/nonexistent/ffmpeg", 1);
    /* an earlier run's video stays (its name carries its own time); only the kept frames and audio of a run are replaced */
    snprintf(p, sizeof p, "%s/demo-20261110-090000.mp4", dir); sf = fopen(p, "w"); fclose(sf);
    setenv("R3I6NOS_DEMO_STAMP", "20261111-100000", 1);
    CHECK(r3_demo_open(&d, dir, "Test App", "1.2.3", "r36s-test", "--rec-demo", NULL, 44100) == 0, "open");
    r3_demo_build(&d, "v1.2.3 a1b2c3d 2026-11-11");
    snprintf(p, sizeof p, "%s/demo-20261110-090000.mp4", dir);
    CHECK(access(p, F_OK) == 0, "an earlier run's video is kept");
    snprintf(p, sizeof p, "%s/99-old.png", dir);
    CHECK(access(p, F_OK) != 0, "an old screenshot is removed");
    snprintf(p, sizeof p, "%s/keep-me.txt", dir);
    CHECK(access(p, F_OK) == 0, "a file that is not the recorder's stays");
    tour(&d, 32, &shown);
    CHECK(r3_demo_finish(&d, src, 1) == -1, "no ffmpeg: finish says it failed");
    int bad_in_log;
    snprintf(p, sizeof p, "%s/rec-demo.txt", dir);
    CHECK(log_ok(p, "FAIL video", &bad_in_log) && bad_in_log == 1, "the log ends with end and says only the video failed");
    snprintf(p, sizeof p, "%s/demo-20261111-100000.wav", dir);
    CHECK(file_size(p) == 44 + 32L * 44100 * 4, "the WAV is the tour's length (header + 4 bytes a frame)");
    snprintf(p, sizeof p, "%s/demo-20261111-100000-frames.r3f", dir);
    long fs = file_size(p);
    CHECK(fs > 0 && fs < 32 * 30 * 4608L / 4, "the frames are kept, and runs of equal screens are one record");
    snprintf(p, sizeof p, "%s/01-first-shot-odd-name.png", dir);
    CHECK(check_png(p, &shown), "the screenshot is a valid PNG that decodes to the screen");
    CHECK(file_size(p) < 200000, "an indexed 4-bit screenshot is small");
    snprintf(p, sizeof p, "%s/demo-file.xyz", dir);
    CHECK(file_size(p) == 12, "the demo file is copied");
    snprintf(p, sizeof p, "%s/rec-demo.txt.tmp", dir);
    CHECK(access(p, F_OK) != 0, "no temporary log is left");
    snprintf(p, sizeof p, "%s/rec-demo.txt", dir);
    CHECK(log_ok(p, "INFO build: v1.2.3 a1b2c3d 2026-11-11", &bad_in_log), "the log names the build: version, commit, date");

    /* a cancelled tour: the files so far are kept, the log says FAIL cancelled, no encode is tried */
    setenv("R3I6NOS_DEMO_STAMP", "20261111-110000", 1);
    CHECK(r3_demo_open(&d, dir, "Test App", "1.2.3", "r36s-test", "--rec-demo", NULL, 44100) == 0, "open for a cancel");
    tour(&d, 3, &shown);
    d.cancelled = 1;
    CHECK(r3_demo_finish(&d, src, 1) == -1, "cancelled: finish says it failed");
    snprintf(p, sizeof p, "%s/rec-demo.txt", dir);
    CHECK(log_ok(p, "FAIL cancelled", &bad_in_log), "the log says cancelled");
    snprintf(p, sizeof p, "%s/demo-20261111-110000.wav", dir);
    CHECK(file_size(p) == 44 + 3L * 44100 * 4, "the audio so far is kept");

    /* a hot-plug failure during the tour: the recording is closed, each failure is a FAIL line, nothing is encoded; a plain change is ignored */
    {
        char hp[PATH_MAX], ev[PATH_MAX + 16];
        snprintf(hp, sizeof hp, "%s/hotplug", base);
        r3_report_mkdirs(hp);
        snprintf(ev, sizeof ev, "%s/events", hp);
        setenv("R3I6NOS_HOTPLUG_DIR", hp, 1);
        setenv("R3I6NOS_DEMO_STAMP", "20261111-130000", 1);
        CHECK(r3_demo_open(&d, dir, "Test App", "1.2.3", "r36s-test", "--rec-demo", NULL, 44100) == 0, "open for a failure");
        tour(&d, 2, &shown);
        FILE *ef = fopen(ev, "w");
        fprintf(ef, "5\tinfo\tHeadphones connected\n6\terror\tA USB device did not start\n");
        fclose(ef);
        CHECK(r3_demo_failed(&d), "an error event during the tour is seen");
        r3_demo_finish(&d, src, 1);
        snprintf(p, sizeof p, "%s/rec-demo.txt", dir);
        CHECK(log_ok(p, "FAIL hotplug failure: A USB device did not start", &bad_in_log), "the log has the failure and ends with end");
        snprintf(p, sizeof p, "%s/demo-20261111-130000.mp4", dir);
        CHECK(access(p, F_OK) != 0, "no video is made after a failure");
        snprintf(p, sizeof p, "%s/demo-20261111-130000.wav", dir);
        CHECK(file_size(p) == 44 + 2L * 44100 * 4, "what was recorded is kept");
        /* only a change: not a failure */
        ef = fopen(ev, "w");
        fprintf(ef, "5\tinfo\tHeadphones connected\n");
        fclose(ef);
        setenv("R3I6NOS_DEMO_STAMP", "20261111-130100", 1);
        CHECK(r3_demo_open(&d, dir, "Test App", "1.2.3", "r36s-test", "--rec-demo", NULL, 44100) == 0, "open again");
        ef = fopen(ev, "a");
        fprintf(ef, "7\tinfo\tUSB storage connected: Stick\n");
        fclose(ef);
        tour(&d, 1, &shown);
        CHECK(!r3_demo_failed(&d), "a plain change is not a failure");
        unsetenv("R3I6NOS_HOTPLUG_DIR");
    }

    /* the lock: taken once, refused to a second live holder, taken over from a dead one, removed at the end */
    {
        char run[PATH_MAX], lp[PATH_MAX];
        snprintf(run, sizeof run, "%s/run", base);
        setenv("R3I6NOS_RUN", run, 1);
        r3_demo_lock_path(lp, sizeof lp);
        CHECK(r3_demo_lock() == 1 && access(lp, F_OK) == 0, "the lock is taken");
        pid_t kid = fork();
        if (kid == 0) _exit(r3_demo_lock() == 0 ? 0 : 1); /* the child sees a live holder (its parent) */
        int st = 0;
        waitpid(kid, &st, 0);
        CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "a second recording is refused while the first runs");
        FILE *lf = fopen(lp, "w");
        fputs("999999\n", lf);
        fclose(lf);
        CHECK(r3_demo_lock() == 1, "a lock left by a dead process is taken over");
        r3_demo_status("Test App", 42, "Showing: the roll", "/roms/Test App/Demo/rec-demo.txt");
        char sdir[PATH_MAX], sp[PATH_MAX + 16], sbuf[400];
        snprintf(sdir, sizeof sdir, "%s", lp);
        *strrchr(sdir, '/') = 0;
        snprintf(sp, sizeof sp, "%s/progress", sdir);
        FILE *pf = fopen(sp, "r");
        size_t pn = pf ? fread(sbuf, 1, sizeof sbuf - 1, pf) : 0;
        sbuf[pn] = 0;
        if (pf) fclose(pf);
        CHECK(strstr(sbuf, "step=42\nsteps=100\ndoing=Showing: the roll\nfile=/roms/Test App/Demo/rec-demo.txt\n"), "the progress file has step, steps, doing and file (the report's keys)");
        snprintf(sp, sizeof sp, "%s/running", sdir);
        CHECK(access(sp, F_OK) == 0, "running names the app");
        r3_demo_status_done(0);
        snprintf(sp, sizeof sp, "%s/done", sdir);
        pf = fopen(sp, "r");
        pn = pf ? fread(sbuf, 1, sizeof sbuf - 1, pf) : 0;
        sbuf[pn] = 0;
        if (pf) fclose(pf);
        CHECK(!strcmp(sbuf, "exit=0\n"), "done says exit=0");
        snprintf(sp, sizeof sp, "%s/running", sdir);
        CHECK(access(sp, F_OK) != 0, "running is gone when it is done");
        CHECK(r3_demo_lock() == 1, "a new lock");
        snprintf(sp, sizeof sp, "%s/done", sdir);
        CHECK(access(sp, F_OK) != 0, "a new lock clears the last run's done");
        r3_demo_unlock();
        CHECK(access(lp, F_OK) != 0, "the lock is removed at the end");
        unsetenv("R3I6NOS_RUN");
    }
    setenv("R3I6NOS_DEMO_STAMP", "20261111-100000", 1);

    /* with a real ffmpeg, if there is one: the video is made; the frames and the sound are kept, the ffmpeg log goes */
    unsetenv("R3I6NOS_FFMPEG");
    char ff[PATH_MAX];
    if (r3_demo_find_ffmpeg(ff, sizeof ff)) {
        char msg[400];
        /* a run whose encode failed (the cancel run above cleared the first one's files), then the later encode of its kept files */
        setenv("R3I6NOS_FFMPEG", "/nonexistent/ffmpeg", 1);
        CHECK(r3_demo_open(&d, dir, "Test App", "1.2.3", "r36s-test", "--rec-demo", NULL, 44100) == 0, "open for the later encode");
        tour(&d, 32, &shown);
        r3_demo_finish(&d, src, 1);
        unsetenv("R3I6NOS_FFMPEG");
        int pct = 0;
        CHECK(r3_demo_encode_p(dir, msg, sizeof msg, note_pct, &pct) == 0 && pct == 100, "the encode reports its progress to 100");
        CHECK(strstr(msg, "demo-20261111-100000.mp4") != NULL, msg);
        snprintf(p, sizeof p, "%s/demo-20261111-100000.mp4", dir);
        CHECK(file_size(p) > 1000, "demo-<stamp>.mp4 is made from the kept frames and WAV");
        snprintf(p, sizeof p, "%s/demo-20261111-100000.wav", dir);
        CHECK(access(p, F_OK) == 0, "the WAV is kept after a good encode");
        snprintf(p, sizeof p, "%s/demo-20261111-100000-frames.r3f", dir);
        CHECK(access(p, F_OK) == 0, "the frames are kept after a good encode");
        snprintf(p, sizeof p, "%s/demo-ffmpeg.log", dir);
        CHECK(access(p, F_OK) != 0, "the ffmpeg log is removed after a good encode");
        snprintf(p, sizeof p, "%s/demo-20261110-090000.mp4", dir);
        CHECK(access(p, F_OK) == 0, "the earlier run's video is still there");
        setenv("R3I6NOS_DEMO_STAMP", "20261111-120000", 1);
        /* and a whole run, start to finish */
        CHECK(r3_demo_open(&d, dir, "Test App", "1.2.3", "r36s-test", "--rec-demo", NULL, 44100) == 0, "open again");
        tour(&d, 32, &shown);
        CHECK(r3_demo_finish(&d, src, 1) == 0, "finish with ffmpeg");
        snprintf(p, sizeof p, "%s/rec-demo.txt", dir);
        CHECK(log_ok(p, "PASS video", &bad_in_log) && bad_in_log == 0, "the log is all PASS and ends with end");
        snprintf(p, sizeof p, "%s/demo-20261111-120000.mp4", dir);
        char cmd[PATH_MAX * 2];
        snprintf(cmd, sizeof cmd, "ffprobe -v error -show_entries stream=codec_name,width,height -of csv=p=0 '%s' > '%s/probe.txt' 2>&1", p, base);
        if (system(cmd) == 0) {
            snprintf(p, sizeof p, "%s/probe.txt", base);
            FILE *pf = fopen(p, "r");
            char all[512] = "";
            size_t n = pf ? fread(all, 1, sizeof all - 1, pf) : 0;
            all[n] = 0;
            if (pf) fclose(pf);
            CHECK(strstr(all, "h264,640,480") && strstr(all, "aac"), "ffprobe: H.264 640x480 and AAC");
        }
    } else {
        fprintf(stderr, "test_demo: ffmpeg not installed: the encode itself was not run\n");
    }
    /* the checklist and the splash every port shares */
    {
        static const char *const NAME[3] = {"one", "two-b", "three"}, *const TITLE[3] = {"One", "Two", "Three"}, *const BLURB[3] = {"a", "b", "c"};
        R3Checklist c;
        r3_checklist_init(&c, 3, NAME, TITLE, BLURB);
        CHECK(r3_checklist_mask(&c) == 7, "all ticked at first");
        r3_checklist_move(&c, 1);
        r3_checklist_toggle(&c);
        r3_checklist_move(&c, -2);
        CHECK(r3_checklist_mask(&c) == 5 && c.sel == 2, "toggle and wrap");
        CHECK(r3_checklist_parse(&c, "all") == 7 && r3_checklist_parse(&c, "three,two-b") == 6 && r3_checklist_parse(&c, "x") == 0, "--features parsing");
        R3Screen scr = {0};
        char dump[R3_ROWS][R3_COLS + 1];
        r3_checklist_draw(&scr, &c, 5);
        r3_dump(&scr, dump);
        int found = 0;
        for (int r = 0; r < R3_ROWS; r++) if (strstr(dump[r], "[ ] Two")) found = 1;
        CHECK(found, "the checklist shows an unticked line");
        r3_splash(&scr, "Test App", "1.2.3", "a1b2c3d 2026-11-11", "A tagline", "Opening", 55);
        r3_dump(&scr, dump);
        int bar = 0, ver = 0, name = 0;
        for (int r = 0; r < R3_ROWS; r++) { if (strstr(dump[r], "v1.2.3 a1b2c3d 2026-11-11")) ver = 1; if (strstr(dump[r], "Opening")) bar = 1; if (strstr(dump[r], "A tagline")) name = 1; }
        CHECK(ver && bar && name, "the splash shows the tagline, what is loading and the version line");
    }
    snprintf(p, sizeof p, "rm -rf '%s'", base);
    if (system(p) != 0) fprintf(stderr, "test_demo: could not remove %s\n", base);
    if (fails) return 1;
    puts("test_demo: ok");
    return 0;
}

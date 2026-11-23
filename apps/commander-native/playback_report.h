/* playback_report.h: Commander's `== playback ==` section of the device report (rule 9, header-only, MIT; docs/PLAYBACK.md).
 *
 * It measures every way Commander plays something, to find what to optimise and where the CPU cores, the GPU and the VPU could be used:
 *   - the machine: cores online, governor and frequency, the hardware-decode nodes (/dev/mpp_service, vpu_service, rga, dri, video*), what ffmpeg lists
 *     (`-hwaccels`, an rkmpp or v4l2m2m decoder), whether its H.264 decoder threads;
 *   - **video through ffmpeg** (the Play tab's PL_VIDEO path), on a real MusiCue video when one is on a card (its `video/` folder, or $R3I6NOS_PLAYBACK_CLIP),
 *     else a synthetic H.264 clip at 480p made by ffmpeg's libx264: the player's own command line, then one change at a time (decoder threads 1, 2 and 4, the
 *     scaler's flags, the loop filter skipped, and **the picture left as YUV at its own size**, which is what an SDL YUV texture would be given), each as
 *     times real time, CPU, the share of all cores and **which cores were busy**, then the advice the numbers give;
 *   - **sound through ffmpeg** (PL_AUDIO, and the audio half of PL_VIDEO) and **through nttplay** (PL_NTT: a MIDI or tracker file from the corpus);
 *   - **the sound device** (PL_WAV, and what every other path queues into): driver, the format it gave, the latency, the time to open;
 *   - **the picture to the screen**: the renderer (accelerated or software, vsync), the cost of a frame of the app's own screen, and the cost of uploading a
 *     frame as BGRA against as YUV (IYUV) with the GPU asked to scale it;
 *   - **the whole path**: the real Player on the clip for a few seconds on the real clock, with the power window (rule 14) as the state `playing`, the frames
 *     it delivered against the frames due, and how far behind the sound the picture ended.
 * Everything here is read-only and takes only a throwaway folder. Include after player.h and r3i6nos_report.h. Never run on a unit. */
#ifndef COMMANDER_PLAYBACK_REPORT_H
#define COMMANDER_PLAYBACK_REPORT_H

#include <sys/resource.h>
#include <glob.h>

#define PB_GOAL_X 1.5          /* times real time the player's own command must reach at 480p: below it the picture lags the sound (docs/VIDEO-DECODE.md) */
#define PB_MAX_CORES 8

typedef struct { double wall, cpu; long peak_kb; int rc; unsigned long long bytes; double busy[PB_MAX_CORES]; int ncores; } PbRun;

static int pb_cpu_read(unsigned long long busy[], unsigned long long total[], int cap) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0;
    char line[256];
    int n = 0;
    while (fgets(line, sizeof line, f) && n < cap) {
        unsigned long long u, ni, sy, id, io, ir, so, st;
        int c;
        if (sscanf(line, "cpu%d %llu %llu %llu %llu %llu %llu %llu %llu", &c, &u, &ni, &sy, &id, &io, &ir, &so, &st) < 5) continue;
        if (c < 0 || c >= cap) continue;
        total[c] = u + ni + sy + id + io + ir + so + st;
        busy[c] = total[c] - id - io;
        if (c + 1 > n) n = c + 1;
    }
    fclose(f);
    return n;
}

/* Runs argv: stdin from `feed` (a function writing frames to the fd, or NULL: /dev/null), stdout drained and counted (or sent to /dev/null when `keep` is
 * 0 and nothing is read), stderr to /dev/null. Fills wall, the child's own CPU and peak memory (wait4), and each core's share of the wall time. */
static void pb_run(char *const argv[], void (*feed)(int fd, void *arg), void *arg, PbRun *out) {
    memset(out, 0, sizeof *out);
    int in[2] = {-1, -1}, op[2];
    if (pipe(op) != 0) { out->rc = -1; return; }
    if (feed && pipe(in) != 0) { close(op[0]); close(op[1]); out->rc = -1; return; }
    unsigned long long b0[PB_MAX_CORES] = {0}, t0[PB_MAX_CORES] = {0}, b1[PB_MAX_CORES] = {0}, t1[PB_MAX_CORES] = {0};
    int nc = pb_cpu_read(b0, t0, PB_MAX_CORES);
    double w0 = r3_report_ms();
    signal(SIGPIPE, SIG_IGN);
    pid_t c = fork();
    if (c < 0) { out->rc = -1; return; }
    if (c == 0) {
        int nul = open("/dev/null", O_RDWR);
        dup2(feed ? in[0] : nul, 0);
        dup2(op[1], 1);
        dup2(nul, 2);
        for (int i = 3; i < 64; i++) close(i);
        execv(argv[0], argv);
        _exit(127);
    }
    close(op[1]);
    if (feed) { close(in[0]); feed(in[1], arg); close(in[1]); }
    static unsigned char sink[1 << 16];
    ssize_t n;
    while ((n = read(op[0], sink, sizeof sink)) > 0 || (n < 0 && errno == EINTR)) if (n > 0) out->bytes += (unsigned long long)n;
    close(op[0]);
    int status = 0;
    struct rusage ru;
    wait4(c, &status, 0, &ru);
    out->wall = (r3_report_ms() - w0) / 1000.0;
    out->cpu = (double)ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 + (double)ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
    out->peak_kb = ru.ru_maxrss;
    out->rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    int n1 = pb_cpu_read(b1, t1, PB_MAX_CORES);
    out->ncores = nc < n1 ? nc : n1;
    for (int i = 0; i < out->ncores; i++) out->busy[i] = t1[i] > t0[i] ? 100.0 * (double)(b1[i] - b0[i]) / (double)(t1[i] - t0[i]) : 0;
}

static void pb_cores(const PbRun *r, char *out, size_t cap) {
    size_t u = 0;
    out[0] = 0;
    for (int i = 0; i < r->ncores && u + 12 < cap; i++) u += (size_t)snprintf(out + u, cap - u, "%scpu%d %.0f%%", i ? ", " : "", i, r->busy[i]);
}

/* ---- the synthetic clip: a ramp scrolling under a fixed noise screen (real residual data on every frame), flat chroma; 4:2:0 ----------------------- */

typedef struct { int w, h, n; } PbFeed;

static void pb_write_all(int fd, const unsigned char *p, size_t n) {
    while (n) { ssize_t k = write(fd, p, n); if (k <= 0) { if (k < 0 && errno == EINTR) continue; return; } p += k; n -= (size_t)k; }
}

static void pb_feed_yuv(int fd, void *arg) {
    PbFeed *f = arg;
    size_t luma = (size_t)f->w * f->h, chroma = luma / 2;
    unsigned char *noise = malloc(luma), *frame = malloc(luma + chroma);
    if (!noise || !frame) { free(noise); free(frame); return; }
    unsigned x = 0x9E3779B9u;
    for (size_t i = 0; i < luma; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; noise[i] = (unsigned char)((x >> 8) & 0x0F); }
    memset(frame + luma, 128, chroma);
    for (int k = 0; k < f->n; k++) {
        for (int y = 0; y < f->h; y++) {
            unsigned char *row = frame + (size_t)y * f->w;
            const unsigned char *nz = noise + (size_t)y * f->w;
            int o = (y * 3 + k * 3) % f->w;
            for (int xx = 0; xx < f->w; xx++) { int v = (xx + o) % f->w; row[xx] = (unsigned char)(((v * 255 / (f->w - 1)) & 0xF0) | nz[xx]); }
        }
        pb_write_all(fd, frame, luma + chroma);
    }
    free(noise);
    free(frame);
}

/* A sine, 16-bit stereo 44100 Hz, as a WAV file (ffmpeg-min has the wav demuxer, not a raw one): the clip's sound, AAC-encoded so the decode is a real AAC decode. */
static int pb_write_sine(const char *path, double secs) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    long n = (long)(secs * 44100);
    unsigned bytes = (unsigned)(n * 4), riff = 36 + bytes, rate = 44100, brate = 44100 * 4, fmt = 16;
    unsigned short pcm = 1, ch = 2, align = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (long i = 0; i < n; i++) {
        short v = (short)(8000.0 * sin(2.0 * 3.14159265358979 * 440.0 * (double)i / 44100.0));
        short st[2] = {v, v};
        fwrite(st, sizeof st, 1, f);
    }
    fclose(f);
    return 0;
}

/* ---- one measured variant --------------------------------------------------------------------------------------------------------------------- */

typedef struct { char name[40]; double x, cpu_pct, cores, mbps; int ok; } PbVar;

/* Decodes the clip with `extra` (ffmpeg options before -i), `vf` and `pix`, best of two by wall time. `secs` is how much video the run covers. */
static PbVar pb_variant(R3Report *r, const char *ff, const char *clip, double secs, const char *name, const char *pre1, const char *pre2, const char *vf, const char *pix, int keep_cores) {
    PbVar v;
    memset(&v, 0, sizeof v);
    snprintf(v.name, sizeof v.name, "%s", name);
    char *argv[24];
    int a = 0;
    argv[a++] = (char *)ff; argv[a++] = "-v"; argv[a++] = "error";
    if (pre1) argv[a++] = (char *)pre1;
    if (pre2) argv[a++] = (char *)pre2;
    argv[a++] = "-i"; argv[a++] = (char *)clip; argv[a++] = "-t"; char ts[16]; snprintf(ts, sizeof ts, "%.0f", secs); argv[a++] = ts;
    argv[a++] = "-an"; argv[a++] = "-vf"; argv[a++] = (char *)vf;
    argv[a++] = "-f"; argv[a++] = "rawvideo"; argv[a++] = "-pix_fmt"; argv[a++] = (char *)pix; argv[a++] = "pipe:1"; argv[a] = NULL;
    PbRun best, cur;
    memset(&best, 0, sizeof best);
    best.rc = -1;
    for (int i = 0; i < 2; i++) {
        pb_run(argv, NULL, NULL, &cur);
        if (cur.rc == 0 && cur.bytes > 0 && (best.rc != 0 || cur.wall < best.wall)) best = cur;
    }
    if (best.rc != 0) { r3_report_line(r, R3_SKIP, name, "ffmpeg did not run this variant (exit %d)", best.rc); return v; }
    v.ok = 1;
    v.x = best.wall > 0 ? secs / best.wall : 0;
    v.cpu_pct = best.wall > 0 ? 100.0 * best.cpu / best.wall : 0;
    int nc = best.ncores ? best.ncores : 1;
    v.cores = best.wall > 0 ? best.cpu / best.wall : 0;
    v.mbps = secs > 0 ? (double)best.bytes / secs / 1e6 : 0;
    char cores[160] = "";
    if (keep_cores) { pb_cores(&best, cores, sizeof cores); }
    r3_report_line(r, R3_INFO, name, "%.1fx real time (%.2f s for %.0f s of video), %.2f s of CPU = %.1f cores of %d busy, %.0f MiB peak, %llu bytes out%s%s", v.x, best.wall, secs,
                   best.cpu, v.cores, nc, (double)best.peak_kb / 1024.0, best.bytes, keep_cores ? "; per core: " : "", cores);
    return v;
}

/* ---- the section's ffmpeg half ------------------------------------------------------------------------------------------------------------------ */

static char *pb_popen_text(const char *cmd, char *buf, size_t cap) {
    buf[0] = 0;
    FILE *p = popen(cmd, "r");
    if (!p) return buf;
    size_t n = fread(buf, 1, cap - 1, p);
    buf[n] = 0;
    pclose(p);
    return buf;
}

/* A real video on a card: $R3I6NOS_PLAYBACK_CLIP, else the first *.mp4 in MusiCue's video/ folder on the data card or the system card. */
static int pb_find_real_clip(char *out, size_t cap) {
    const char *e = getenv("R3I6NOS_PLAYBACK_CLIP");
    if (e && *e && access(e, R_OK) == 0) { snprintf(out, cap, "%s", e); return 1; }
    const char *roots[] = {getenv("R3I6NOS_DATA_CARD"), "/roms2", "/roms"};
    for (int i = 0; i < 3; i++) {
        if (!roots[i] || !*roots[i]) continue;
        char pat[PATH_MAX + 32];
        snprintf(pat, sizeof pat, "%s/MusiCue/video/*.mp4", roots[i]);
        glob_t g;
        if (glob(pat, 0, NULL, &g) == 0 && g.gl_pathc > 0) { snprintf(out, cap, "%s", g.gl_pathv[0]); globfree(&g); return 1; }
        globfree(&g);
    }
    return 0;
}

/* The ffmpeg half: the clip, the variants, the advice. Writes the clip path to `clip_out` (in `work`) for the later steps; returns the seconds of video it covers (0: none). */
static double pb_report_ffmpeg(R3Report *r, const char *ff, const char *work, char *clip_out, size_t cap, int *has_audio) {
    double secs = 4;
    if (getenv("R3I6NOS_PLAYBACK_SECS") && atof(getenv("R3I6NOS_PLAYBACK_SECS")) >= 1) secs = atof(getenv("R3I6NOS_PLAYBACK_SECS"));
    char real[PATH_MAX], enc[8192];
    *has_audio = 0;
    int is_real = pb_find_real_clip(real, sizeof real);
    if (is_real) {
        snprintf(clip_out, cap, "%s", real);
        r3_report_line(r, R3_INFO, "clip", "a real video: %s (decoded for up to %.0f s)", real, secs);
        *has_audio = 1;     /* asked of ffmpeg below; a silent file just reports a skip */
    } else {
        char cmd[PATH_MAX + 64];
        snprintf(cmd, sizeof cmd, "'%s' -hide_banner -encoders 2>&1", ff);
        if (!strstr(pb_popen_text(cmd, enc, sizeof enc), "libx264")) {
            r3_report_line(r, R3_SKIP, "clip", "no real video on a card (MusiCue's video/ folder, or $R3I6NOS_PLAYBACK_CLIP) and this ffmpeg cannot encode H.264, so there is nothing to decode");
            return 0;
        }
        snprintf(clip_out, cap, "%s/clip480.mp4", work);
        char sine[PATH_MAX + 16], nf[16];
        snprintf(sine, sizeof sine, "%s/sine.wav", work);
        snprintf(nf, sizeof nf, "%d", (int)(secs * 30));
        PbFeed fd = {854, 480, (int)(secs * 30)};
        int sine_ok = pb_write_sine(sine, secs) == 0;
        char *with_a[] = {(char *)ff, "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "yuv420p", "-s", "854x480", "-r", "30", "-i", "pipe:0", "-i", sine,
                          "-c:v", "libx264", "-preset", "veryfast", "-b:v", "1000k", "-maxrate", "1000k", "-bufsize", "2000k", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "128k", "-shortest", clip_out, NULL};
        char *video_only[] = {(char *)ff, "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "yuv420p", "-s", "854x480", "-r", "30", "-i", "pipe:0",
                              "-c:v", "libx264", "-preset", "veryfast", "-b:v", "1000k", "-maxrate", "1000k", "-bufsize", "2000k", "-pix_fmt", "yuv420p", clip_out, NULL};
        PbRun mk;
        pb_run(sine_ok ? with_a : video_only, pb_feed_yuv, &fd, &mk);
        *has_audio = sine_ok && mk.rc == 0;
        if (mk.rc != 0) { pb_run(video_only, pb_feed_yuv, &fd, &mk); *has_audio = 0; }
        if (mk.rc != 0 || access(clip_out, R_OK) != 0) { r3_report_line(r, R3_SKIP, "clip", "could not make the synthetic 854x480 clip (exit %d)", mk.rc); return 0; }
        r3_report_line(r, R3_INFO, "clip", "no real video on a card, so a synthetic 854x480 H.264 clip, %.0f s at 30 fps, about 1000 kbit/s (a ramp under noise; %s); made in %.1f s with %.0f%% of a core: a real clip in MusiCue's video/ folder is the better test",
                       secs, *has_audio ? "with AAC sound" : "no sound: this ffmpeg cannot encode AAC or read WAV", mk.wall, mk.wall > 0 ? 100.0 * mk.cpu / mk.wall : 0);
    }
    /* the variants, each one change from the player's own command */
    const char *base = PL_VIDEO_VF("neighbor");        /* the old BGRA path (before 2026-11-23), kept as the comparison */
    char yvf[96] = "fps=30";                           /* the player's own command now: YUV at the picture's size (player.h, pl_yuv_size) */
    {   int cw = 0, ch = 0, yw, yh;
        if (pl_probe_size(ff, clip_out, &cw, &ch, NULL) == 0) pl_yuv_size(cw, ch, &yw, &yh, yvf, sizeof yvf);
    }
    PbVar app = pb_variant(r, ff, clip_out, secs, "app path", NULL, NULL, yvf, "yuv420p", 1);
    PbVar old = pb_variant(r, ff, clip_out, secs, "old BGRA path", NULL, NULL, base, "bgra", 1);
    PbVar t1 = pb_variant(r, ff, clip_out, secs, "decoder threads 1", "-threads", "1", yvf, "yuv420p", 1);
    PbVar t2 = pb_variant(r, ff, clip_out, secs, "decoder threads 2", "-threads", "2", yvf, "yuv420p", 1);
    PbVar t4 = pb_variant(r, ff, clip_out, secs, "decoder threads 4", "-threads", "4", yvf, "yuv420p", 1);
    PbVar skip = pb_variant(r, ff, clip_out, secs, "skip loop filter", "-skip_loop_filter", "all", yvf, "yuv420p", 0);
    if (app.ok) {
        r3_report_line(r, app.x >= PB_GOAL_X ? R3_PASS : R3_FAIL, "480p app path", "%.1fx real time with the player's own command (YUV at the picture's size, the GPU scales it; goal at least %.1fx); below 1x the picture falls behind the sound", app.x, PB_GOAL_X);
        if (t1.ok && t4.ok) {
            double g = t4.x / t1.x;
            if (g < 1.15) r3_report_line(r, R3_INFO, "advice threads", "decoding on 4 threads is only %.2fx the speed of 1: the decode does not spread over the cores (a stream with one slice per frame cannot), so the spare cores are free for the picture and the sound", g);
            else r3_report_line(r, R3_INFO, "advice threads", "decoding on 4 threads is %.2fx the speed of 1 (2 threads: %.2fx): ffmpeg's default (every core) is right; capping it would trade speed for the other cores", g, t2.ok ? t2.x / t1.x : 0.0);
        }
        if (old.ok)
            r3_report_line(r, R3_INFO, "advice pixel format", "the YUV path is %.2fx the speed of the old scale-and-BGRA path (%.1fx against %.1fx), and the pipe carries %.1f MB/s instead of %.1f", app.x / old.x, app.x, old.x, app.mbps, old.mbps);
        if (skip.ok && skip.x > app.x * 1.2) r3_report_line(r, R3_INFO, "advice loop filter", "skipping the H.264 loop filter is %.2fx faster (a softer picture): a fallback for a unit that does not keep up, not a default", skip.x / app.x);
    }
    return secs;
}

/* ---- sound through ffmpeg and nttplay -------------------------------------------------------------------------------------------------------- */

static void pb_report_sound(R3Report *r, const char *ff, const char *clip, double secs, int has_audio) {
    if (has_audio) {
        char *argv[] = {(char *)ff, "-v", "error", "-i", (char *)clip, "-vn", "-f", "s16le", "-ar", "44100", "-ac", "2", "pipe:1", NULL};
        PbRun best, cur;
        memset(&best, 0, sizeof best);
        best.rc = -1;
        for (int i = 0; i < 2; i++) { pb_run(argv, NULL, NULL, &cur); if (cur.rc == 0 && cur.bytes > 0 && (best.rc != 0 || cur.wall < best.wall)) best = cur; }
        if (best.rc != 0) r3_report_line(r, R3_SKIP, "ffmpeg audio decode", "the clip has no sound ffmpeg could decode");
        else {
            double got = (double)best.bytes / (44100.0 * 4.0);
            r3_report_line(r, R3_INFO, "ffmpeg audio decode", "%.0fx real time (%.2f s for %.1f s of sound, 16-bit stereo 44100), %.0f%% of a core: the Play tab starts a second ffmpeg for the sound beside the picture's, so the file is read and demuxed twice (one process with two outputs would read it once)",
                           got / (best.wall > 0 ? best.wall : 1e-3), best.wall, got, best.wall > 0 ? 100.0 * best.cpu / best.wall : 0);
        }
    } else r3_report_line(r, R3_SKIP, "ffmpeg audio decode", "no clip with sound");
    (void)secs;
}

/* nttplay on one file of the corpus a tracker or MIDI player would take. */
static void pb_report_ntt(R3Report *r, char **list, int n) {
    char nt[PATH_MAX];
    if (!pl_find_nttplay(nt, sizeof nt)) { r3_report_line(r, R3_SKIP, "nttplay decode", "nttplay is not installed here (the image has /usr/bin/nttplay)"); return; }
    static const char *const ext[] = {".mid", ".midi", ".mod", ".xm", ".it", ".s3m", ".ntt", NULL};
    int shown = 0;
    for (int i = 0; i < n && shown < 3; i++) {
        const char *dot = strrchr(list[i], '.');
        int hit = 0;
        for (int k = 0; dot && ext[k]; k++) if (!strcasecmp(dot, ext[k])) hit = 1;
        if (!hit) continue;
        char *argv[] = {nt, "decode", list[i], "-ar", "44100", NULL};
        PbRun run;
        pb_run(argv, NULL, NULL, &run);
        double got = (double)run.bytes / (44100.0 * 4.0);
        const char *base = strrchr(list[i], '/');
        if (run.rc != 0 || got <= 0) { r3_report_line(r, R3_SKIP, "nttplay decode", "%s: exit %d", base ? base + 1 : list[i], run.rc); continue; }
        r3_report_line(r, R3_INFO, "nttplay decode", "%s: %.0fx real time (%.2f s for %.1f s of sound), %.0f%% of a core, %.0f MiB peak", base ? base + 1 : list[i], got / (run.wall > 0 ? run.wall : 1e-3), run.wall, got,
                       run.wall > 0 ? 100.0 * run.cpu / run.wall : 0, (double)run.peak_kb / 1024.0);
        shown++;
    }
    if (!shown) r3_report_line(r, R3_SKIP, "nttplay decode", "no MIDI or tracker file in the test corpus");
}

/* ---- the machine ------------------------------------------------------------------------------------------------------------------------------ */

static void pb_report_machine(R3Report *r, const char *ff) {
    char b[256], cmd[PATH_MAX + 64], txt[8192];
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    const char *g = "/sys/devices/system/cpu/cpu0/cpufreq/";
    char gov[64] = "?", cur[64] = "?", mx[64] = "?";
    snprintf(b, sizeof b, "%sscaling_governor", g); r3_report_slurp_line(b, gov, sizeof gov);
    snprintf(b, sizeof b, "%sscaling_cur_freq", g); r3_report_slurp_line(b, cur, sizeof cur);
    snprintf(b, sizeof b, "%sscaling_max_freq", g); r3_report_slurp_line(b, mx, sizeof mx);
    r3_report_line(r, R3_INFO, "cpu", "%ld cores online, governor %s, cpu0 %.0f MHz now (max %.0f): a governor that holds the clock low makes every decode figure below slower than it needs to be", cores, gov, atof(cur) / 1000.0, atof(mx) / 1000.0);
    static const char *const nodes[] = {"/dev/mpp_service", "/dev/vpu_service", "/dev/rga", "/dev/dri/card0", "/dev/dri/renderD128", "/dev/video0", "/dev/video1", "/dev/ion", NULL};
    char have[512] = "", miss[512] = "";
    for (int i = 0; nodes[i]; i++) strncat(access(nodes[i], F_OK) == 0 ? have : miss, nodes[i], 80), strncat(access(nodes[i], F_OK) == 0 ? have : miss, " ", 2);
    r3_report_line(r, R3_INFO, "hardware nodes", "present: %s| absent: %s", *have ? have : "none ", *miss ? miss : "none ");
    snprintf(cmd, sizeof cmd, "'%s' -hide_banner -hwaccels 2>&1", ff);
    pb_popen_text(cmd, txt, sizeof txt);
    if (!*txt) { r3_report_line(r, R3_SKIP, "ffmpeg", "%s did not run: no ffmpeg here, so the Play tab cannot play video or most sound", ff); return; }
    char acc[256] = "";
    int in = 0;
    for (char *save = NULL, *ln = strtok_r(txt, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save)) {
        if (!strncmp(ln, "Hardware acceleration methods", 29)) { in = 1; continue; }
        if (in && *ln) { strncat(acc, ln, sizeof acc - strlen(acc) - 2); strcat(acc, " "); }
    }
    r3_report_line(r, R3_INFO, "ffmpeg hwaccels", "%s", *acc ? acc : "none (ffmpeg-min is built with everything off: H.264 decodes in software on the CPU)");
    snprintf(cmd, sizeof cmd, "'%s' -hide_banner -decoders 2>&1", ff);
    pb_popen_text(cmd, txt, sizeof txt);
    r3_report_line(r, R3_INFO, "ffmpeg hardware decoders", "%s", strstr(txt, "rkmpp") ? "rkmpp is listed" : strstr(txt, "v4l2m2m") ? "v4l2m2m is listed" : "no rkmpp or v4l2m2m decoder: the VPU is not reachable from this ffmpeg");
    snprintf(cmd, sizeof cmd, "'%s' -hide_banner -h decoder=h264 2>&1", ff);
    pb_popen_text(cmd, txt, sizeof txt);
    char *th = strstr(txt, "Threading capabilities:");
    char thr[96] = "unknown";
    if (th) { th += 23; while (*th == ' ') th++; snprintf(thr, sizeof thr, "%.*s", (int)strcspn(th, "\n"), th); }
    r3_report_line(r, R3_INFO, "h264 decoder threading", "%s", thr);
}

#endif /* COMMANDER_PLAYBACK_REPORT_H */

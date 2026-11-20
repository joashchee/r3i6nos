/* musicue_report.h: MusiCue's device report (CLAUDE.md rule 9, docs/DEVICE-REPORTS.md; the power section is rule 14, docs/POWER.md). MIT.
 *
 * `musicue --report`, or the main menu's Device report (it asks first), writes $R3I6NOS_APP_DATA/r36s-report.txt (the data card's MusiCue folder, else the system
 * card's; rule 16) for Claude to read. It is included by musicue.c after the program it measures, so it uses the app's own functions: the very code that opens a
 * song, plays it, draws the screen and starts the render.
 *
 *   == helpers ==       musicue-render and musicue-sync run; the image's ffmpeg has the aac encoder and the muxers MusiCue needs (the first unit try of the render failed
 *                       on a missing wav muxer, so the report checks them)
 *   == songs ==         how long the app takes to open a WAV and an M4A of the same sound (60 s, made here), against the goal, and a few of the person's own songs
 *   == sound ==         the sound device opens with the engine's own settings and the callback runs
 *   == screen ==        composing the screen and presenting it, with the waveform's pixels over the cells
 *   == power ==         the battery; idle (a song open, paused, nothing pressed: must cost almost nothing); playing (the song, the playhead redrawing); rendering (a Key and
 *                       Tempo copy on four workers, then compressed, measured while it runs: the helper shows as the busiest other process)
 *   == key and tempo == the copy itself: it finished, how long it took against the screen's own estimate, its length, and how long the M4A takes to open
 *
 * It makes test files (a 60 s song, its M4A, a copy) in <data>/.report-work and removes them. R3I6NOS_REPORT_POWER_S (default 10) is the idle and playing windows,
 * R3I6NOS_REPORT_SONG_S (default 60) the test song's length: the self-test sets both short. */
#ifndef MUSICUE_REPORT_H
#define MUSICUE_REPORT_H

typedef struct { App *app; SDL_Renderer *r; R3Video *video; R3Toast *toast; R3Screen cells; Uint32 last; } RptCtx;

/* One pass of the main loop as it runs while waiting or playing, minus input (a report in progress takes none). */
static void rpt_turn(void *arg) {
    RptCtx *c = (RptCtx *)arg;
    App *a = c->app;
    SDL_Event e;
    int wait_ms, wait_step;
    loop_pacing(a, 0, c->video->settle, r3_toast_active(c->toast), &wait_ms, &wait_step);
    int have = wait_event(&e, wait_ms, wait_step);
    while (have) have = SDL_PollEvent(&e);
    Uint32 now = SDL_GetTicks();
    app_tick(a, (double)(now - c->last));
    c->last = now;
    /* The main loop takes a->force (set by the menu item that started the report) into its next present and clears it. Left set here it kept loop_pacing busy for
     * the whole report: a look every 8 ms, 137 wake-ups a second while idle (the unit's first report, 2026-11-20; `--report` never sets it, so Docker missed it). */
    int force = a->force;
    a->force = 0;
    r3_toast_poll(c->toast, now);
    compose(a, &c->cells);
    r3_toast_draw(c->toast, &c->cells);
    present(a, c->video, c->r, &c->cells, force);
}

typedef struct { int code; double wall; char done[256], error[256], first[256]; } RptRun;

/* Runs argv, reading its output line by line (DONE and ERROR lines are kept), sampling the power window every pass. code is the exit status (127: not found). */
static void rpt_run(char *const argv[], R3Power *pw, RptRun *out) {
    memset(out, 0, sizeof *out);
    out->code = -1;
    int fd[2];
    if (pipe(fd) != 0) return;
    double t0 = r3_report_ms();
    pid_t pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); return; }
    if (pid == 0) {
        dup2(fd[1], 1); dup2(fd[1], 2);
        close(fd[0]); close(fd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fd[1]);
    fcntl(fd[0], F_SETFL, fcntl(fd[0], F_GETFL) | O_NONBLOCK);
    char line[512];
    int n = 0;
    for (;;) {
        char ch;
        ssize_t got = read(fd[0], &ch, 1);
        if (got == 1) {
            if (ch == '\n' || n >= (int)sizeof line - 1) {
                line[n] = 0; n = 0;
                if (!strncmp(line, "DONE", 4)) snprintf(out->done, sizeof out->done, "%s", line);
                else if (!strncmp(line, "ERROR", 5)) snprintf(out->error, sizeof out->error, "%s", line);
                else if (!out->first[0] && line[0] && strncmp(line, "PROGRESS", 8) && strncmp(line, "BAR", 3)) snprintf(out->first, sizeof out->first, "%s", line);
            } else line[n++] = ch;
            continue;
        }
        if (got == 0) break;
        if (errno == EINTR) continue;
        if (errno != EAGAIN) break;
        if (pw) r3_power_sample(pw);
        usleep(100000);
        if (r3_report_ms() - t0 > 900000.0) { kill(pid, SIGKILL); break; }       /* fifteen minutes: something is wrong */
    }
    close(fd[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    out->wall = (r3_report_ms() - t0) / 1000.0;
    out->code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* A test song: a slow chord with a click on every beat, 16-bit stereo 44.1 kHz. */
static int rpt_make_wav(const char *path, double secs) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    const int64_t frames = (int64_t)(secs * 44100);
    unsigned char h[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,0x44,0xAC,0,0,0x10,0xB1,2,0,4,0,16,0,'d','a','t','a',0,0,0,0};
    uint32_t bytes = (uint32_t)(frames * 4), riff = 36 + bytes;
    memcpy(h + 4, &riff, 4); memcpy(h + 40, &bytes, 4);
    fwrite(h, 1, 44, f);
    int16_t buf[8192];
    for (int64_t i = 0; i < frames;) {
        int n = (int)(frames - i < 4096 ? frames - i : 4096);
        for (int k = 0; k < n; k++, i++) {
            double t = (double)i / 44100.0, v = 0.12 * (sin(6.2831853 * 220.0 * t) + sin(6.2831853 * 277.18 * t) + sin(6.2831853 * 329.63 * t));
            double b = fmod(t, 0.5);
            if (b < 0.03) v += 0.3 * exp(-b * 150.0) * (((double)((uint32_t)((uint64_t)i * 2654435761u) >> 16) / 32768.0) - 1.0);
            int16_t s = (int16_t)(v * 32000);
            buf[2 * k] = s; buf[2 * k + 1] = s;
        }
        fwrite(buf, 4, (size_t)n, f);
    }
    return fclose(f) == 0;
}

/* The app's own open of a song, timed: decode (or read) into memory, then the waveform. Returns the seconds of audio, or 0. */
static double rpt_open(R3Report *rep, const char *label, const char *path, double goal_x, double *ms_out) {
    Track t;
    memset(&t, 0, sizeof t);
    char err[160] = "";
    double t0 = r3_report_ms();
    int ok = track_load(&t, path, TRACK_MAX_SECONDS, NULL, NULL, err, sizeof err);
    double ms = r3_report_ms() - t0;
    if (!ok) { r3_report_line(rep, R3_FAIL, label, "the app could not open it: %s", err[0] ? err : "unknown"); return 0; }
    double secs = track_seconds(&t), x = secs / (ms / 1000.0 + 1e-9);
    WavePeaks pk;
    memset(&pk, 0, sizeof pk);
    double w0 = r3_report_ms();
    wave_peaks(t.pcm, t.frames, &pk);
    double wms = r3_report_ms() - w0;
    char what[200];
    snprintf(what, sizeof what, "%.0f ms to open %.0f s of sound (%.1fx real time), %.0f MiB in memory, the waveform %.0f ms; a 4-minute song would take about %.1f s", ms, secs, x, (double)t.frames * 4 / 1048576.0, wms, 240.0 / (x + 1e-9));
    if (goal_x > 0) r3_report_line(rep, x >= goal_x ? R3_PASS : R3_FAIL, label, "%s (goal %.0fx)", what, goal_x);
    else r3_report_line(rep, R3_INFO, label, "%s", what);
    if (ms_out) *ms_out = ms;
    track_free(&t);
    return secs;
}

static int rpt_has(const char *cmd, const char *needle) {                 /* does the command's output contain needle? */
    FILE *p = popen(cmd, "r");
    if (!p) return 0;
    char buf[1024];
    int found = 0;
    while (fgets(buf, sizeof buf, p)) if (strstr(buf, needle)) found = 1;
    pclose(p);
    return found;
}

#define RPT_OPEN_WAV_X 30.0      /* a WAV is read, not decoded: 60 s in two seconds. A first guess, set before any measurement */
#define RPT_OPEN_M4A_X 15.0      /* an M4A is decoded by ffmpeg: 4 minutes in 16 s. A first guess, set before any measurement */
#define RPT_COPY_X 2.5           /* the whole copy (render and compress) against the song's length: the measured 3.2x less a margin */

static int musicue_report(App *a, SDL_Renderer *r, R3Video *video, R3Toast *toast, int quiet) {
    char dir[PATH_MAX], wdir[PATH_MAX], wav[PATH_MAX + 32], m4a[PATH_MAX + 32], copy[PATH_MAX + 32];
    snprintf(dir, sizeof dir, "%s", a->data);
    snprintf(wdir, sizeof wdir, "%s/.report-work", dir);
    snprintf(wav, sizeof wav, "%s/test.wav", wdir);
    snprintf(m4a, sizeof m4a, "%s/test.m4a", wdir);
    snprintf(copy, sizeof copy, "%s/copy.m4a", wdir);
    R3Report rep;
    memset(&rep, 0, sizeof rep);
    rep.quiet = quiet;
    if (r3_report_open_named(&rep, dir, "r36s-report.txt", "MusiCue", VERSION, "r36s-musicue", "musicue --report", "r3i6nos") != 0) {
        fprintf(stderr, "musicue: cannot write the report in %s: %s\n", dir, strerror(errno));
        return 1;
    }
    save_cues(a);                                              /* the report plays its own song: the person's is saved and closed */
    close_song(a);
    r3_report_system(&rep);
    r3_report_card(&rep, dir);
    r3_report_memory(&rep);
    const double song_s = getenv("R3I6NOS_REPORT_SONG_S") && atof(getenv("R3I6NOS_REPORT_SONG_S")) >= 8 ? atof(getenv("R3I6NOS_REPORT_SONG_S")) : 60.0;
    const double window_s = r3_report_power_seconds(10);
    mkdirs(wdir);
    char hbuf[PATH_MAX + 40], sbuf[PATH_MAX + 40];
    const char *render = render_helper_path(hbuf, sizeof hbuf), *sync = helper_path(sbuf, sizeof sbuf);
    const char *ffmpeg = getenv("R3I6NOS_FFMPEG") && *getenv("R3I6NOS_FFMPEG") ? getenv("R3I6NOS_FFMPEG") : "ffmpeg";

    /* ---- helpers ---- */
    r3_report_section(&rep, "helpers");
    {
        RptRun run;
        char *const bad[] = {(char *)render, (char *)"--ratio", (char *)"9", NULL};      /* a usage error: 2 means it is there and runs */
        rpt_run(bad, NULL, &run);
        r3_report_check(&rep, run.code == 2, "musicue-render", run.code == 2 ? render : run.code == 127 ? "not found (the Key and Tempo copy cannot be made)" : "found but did not run as expected");
        char *const hs[] = {(char *)sync, (char *)"--help", NULL};
        rpt_run(hs, NULL, &run);
        r3_report_check(&rep, run.code == 0, "musicue-sync", run.code == 0 ? sync : run.code == 127 ? "not found (the sync cannot run)" : "found but --help failed");
        char cmd[PATH_MAX + 160];
        snprintf(cmd, sizeof cmd, "'%s' -hide_banner -encoders 2>/dev/null", ffmpeg);
        int enc = rpt_has(cmd, " aac ");
        snprintf(cmd, sizeof cmd, "'%s' -hide_banner -muxers 2>/dev/null", ffmpeg);
        int mux_ipod = rpt_has(cmd, " ipod"), mux_s16 = rpt_has(cmd, " s16le");
        r3_report_check(&rep, enc, "ffmpeg aac encoder", enc ? "present" : "missing: a copy cannot be compressed to AAC");
        r3_report_check(&rep, mux_ipod, "ffmpeg ipod muxer (M4A)", mux_ipod ? "present" : "missing: a copy cannot be written as an M4A");
        r3_report_check(&rep, mux_s16, "ffmpeg s16le muxer", mux_s16 ? "present (songs other than WAV are decoded through it)" : "missing: MusiCue cannot open anything but a WAV");
        r3_report_line(&rep, R3_INFO, "yt-dlp", "%s", system("command -v yt-dlp >/dev/null 2>&1") == 0 ? "installed (Creative Commons audio can be fetched)" : "not installed (only Cue's cue files can be fetched)");
    }

    /* ---- songs ---- */
    r3_report_section(&rep, "songs");
    scan_songs(a);
    int ncopies = 0;
    for (int i = 0; i < a->nsongs; i++) ncopies += kt_is_copy_id(a->songs[i].id);
    r3_report_line(&rep, R3_INFO, "library", "%d songs, %d of them Key and Tempo copies, %d of those still WAV (Compress copies to AAC would shrink them)", a->nsongs, ncopies, wav_copies(a));
    int made = rpt_make_wav(wav, song_s);
    if (!made) r3_report_line(&rep, R3_FAIL, "test song", "cannot write %.100s: the data card may be full or read-only", wav);
    else {
        rpt_open(&rep, "open a WAV", wav, RPT_OPEN_WAV_X, NULL);
        RptRun er;
        char *const enc_argv[] = {(char *)ffmpeg, (char *)"-v", (char *)"error", (char *)"-nostdin", (char *)"-y", (char *)"-f", (char *)"wav", (char *)"-i", wav, (char *)"-vn",
                                  (char *)"-c:a", (char *)"aac", (char *)"-aac_coder", (char *)"fast", (char *)"-b:a", (char *)"192k", (char *)"-f", (char *)"ipod", m4a, NULL};
        rpt_run(enc_argv, NULL, &er);
        if (er.code == 0) {
            r3_report_line(&rep, R3_INFO, "compress the test song", "%.1f s for %.0f s of sound (%.1fx real time) into AAC 192 kbps", er.wall, song_s, song_s / (er.wall + 1e-9));
            rpt_open(&rep, "open an M4A", m4a, RPT_OPEN_M4A_X, NULL);
        } else r3_report_line(&rep, R3_SKIP, "open an M4A", "ffmpeg could not make one (%s)", er.first[0] ? er.first : "see helpers");
    }
    {   /* a few of the person's own songs, one of each kind of file */
        char seen[8][8];
        int nseen = 0;
        for (int i = 0; i < a->nsongs && nseen < 4; i++) {
            const char *dot = strrchr(a->songs[i].path, '.');
            if (!dot || strlen(dot) > 6) continue;
            int dup = 0;
            for (int k = 0; k < nseen; k++) if (!strcasecmp(seen[k], dot)) dup = 1;
            if (dup) continue;
            snprintf(seen[nseen++], sizeof seen[0], "%s", dot);
            char label[96];
            snprintf(label, sizeof label, "yours (%.4s) %.60s", dot, a->songs[i].name);
            rpt_open(&rep, label, a->songs[i].path, 0, NULL);
        }
        if (!nseen) r3_report_line(&rep, R3_SKIP, "your songs", "none in the media folder: copy some in and run again to time the real ones");
    }

    /* ---- sound ---- */
    r3_report_section(&rep, "sound");
    open_demo(a);                                              /* the built-in demo song: the screen, the sound and the power windows below all use it */
    if (a->eng.noaudio) r3_report_line(&rep, R3_FAIL, "sound device", "the app could not open one: %s", SDL_GetError());
    else {
        const char *drv = SDL_GetCurrentAudioDriver();
        double p0 = engine_pos_sec(&a->eng);
        engine_play(&a->eng, 1);
        Uint32 t0 = SDL_GetTicks();
        while (SDL_GetTicks() - t0 < 2000) SDL_Delay(20);
        double adv = engine_pos_sec(&a->eng) - p0;
        engine_play(&a->eng, 0);
        r3_report_check(&rep, adv > 1.6 && adv < 2.4, "sound callback", adv > 1.6 && adv < 2.4 ? "ran" : "did not advance in step with the clock");
        r3_report_line(&rep, R3_INFO, "sound driver", "%s: the playhead moved %.2f s in 2 s of the clock", drv ? drv : "?", adv);
        engine_seek(&a->eng, 0);
    }

    /* ---- screen ---- */
    r3_report_section(&rep, "screen");
    RptCtx c;
    memset(&c, 0, sizeof c);
    c.app = a; c.r = r; c.video = video; c.toast = toast;
    {
        enum { N = 20 };
        double tc = 0, td = 0;
        for (int i = 0; i < N; i++) {
            double t0 = r3_report_ms();
            compose(a, &c.cells);
            double t1 = r3_report_ms();
            present(a, video, r, &c.cells, 1);
            double t2 = r3_report_ms();
            tc += t1 - t0; td += t2 - t1;
        }
        r3_report_line(&rep, R3_INFO, "player screen", "composing the cells %.2f ms, drawing the waveform and presenting %.2f ms (%.0f frames a second at most)", tc / N, td / N, td > 0 ? 1000.0 * N / td : 0.0);
    }

    /* ---- power: idle, playing, rendering ---- */
    r3_report_power_section(&rep);
    R3PowerResult idle, work, rendering;
    memset(&idle, 0, sizeof idle); memset(&work, 0, sizeof work); memset(&rendering, 0, sizeof rendering);
    c.last = SDL_GetTicks();
    r3_report_power_idle(&rep, window_s, rpt_turn, &c, &idle);
    engine_play(&a->eng, 1);
    {
        R3Power p;
        r3_power_begin(&p);
        double t0 = r3_report_ms();
        c.last = SDL_GetTicks();
        while (r3_report_ms() - t0 < window_s * 1000.0) { r3_power_sample(&p); rpt_turn(&c); }
        r3_report_power_end(&rep, &p, "playing", 40.0, 120.0, &work);
    }
    engine_play(&a->eng, 0);
    RptRun kt;
    memset(&kt, 0, sizeof kt);
    int kt_ran = 0;
    if (made) {
        char *const argv[] = {(char *)render, (char *)"--in", wav, (char *)"--out", copy, (char *)"--ratio", (char *)"0.8", (char *)"--semitones", (char *)"3", (char *)"--workers", (char *)"4",
                              (char *)"--encode", (char *)"aac:192:fast", NULL};
        R3Power p;
        r3_power_begin(&p);
        rpt_run(argv, &p, &kt);
        r3_report_power_end(&rep, &p, "rendering", 0, 0, &rendering);
        kt_ran = 1;
    } else r3_report_line(&rep, R3_SKIP, "rendering", "no test song to render");
    r3_report_power_advice(&rep, &idle, &work);

    /* ---- key and tempo ---- */
    r3_report_section(&rep, "key and tempo");
    if (!kt_ran) r3_report_line(&rep, R3_SKIP, "copy", "not run");
    else {
        int ok = kt.code == 0 && kt.done[0];
        if (ok) r3_report_line(&rep, R3_PASS, "copy", "%s", kt.done);
        else r3_report_line(&rep, R3_FAIL, "copy", "exit %d: %s", kt.code, kt.error[0] ? kt.error : kt.first[0] ? kt.first : "no DONE line");
        if (ok) {
            double x = song_s / (kt.wall + 1e-9);
            r3_report_line(&rep, x >= RPT_COPY_X ? R3_PASS : R3_FAIL, "render and compress", "%.1f s for %.0f s of song (%.1fx real time: a 4-minute song in about %.0f s; goal %.1fx), four workers, ratio 0.8 and +3 semitones, AAC 192 kbps",
                           kt.wall, song_s, x, 240.0 / (x + 1e-9), RPT_COPY_X);
            double est = kt_estimate_s(song_s, 4, 0) + kt_encode_s(song_s / 0.8);
            r3_report_line(&rep, est >= kt.wall * 0.5 && est <= kt.wall * 2.0 ? R3_PASS : R3_FAIL, "the screen's estimate", "%.0f s against %.0f s taken (it should be within half and double)", est, kt.wall);
            double secs = rpt_open(&rep, "open the copy (M4A)", copy, RPT_OPEN_M4A_X, NULL), want = song_s / 0.8;
            r3_report_line(&rep, secs >= want - 0.05 && secs <= want + 0.3 ? R3_PASS : R3_FAIL, "copy length", "%.2f s, wanted %.2f s (the encoder may add a few hundredths at the end)", secs, want);
        }
    }

    /* tidy up: the test files, the demo song, the screen */
    const char *junk[] = {"test.wav", "test.m4a", "copy.m4a", "copy.m4a.part", "copy.m4a.journal", "copy.m4a.enc", "copy.m4a.in.wav", NULL};
    for (int i = 0; junk[i]; i++) { char p[PATH_MAX + 48]; snprintf(p, sizeof p, "%s/%s", wdir, junk[i]); unlink(p); }
    rmdir(wdir);
    close_song(a);
    a->mode = M_LIBRARY;
    scan_songs(a);
    return r3_report_close(&rep);
}

#endif

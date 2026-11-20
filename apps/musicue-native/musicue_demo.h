/* musicue_demo.h: MusiCue's Rec demo (CLAUDE.md rule 11, docs/DEMO-RECORDINGS.md). MIT. Included by musicue.c after the program it records, so the tour drives the real code.
 *
 * `musicue --rec-demo [--features a,b] [--no-video]`, or the main menu's Rec demo (a checklist of the hero features first), runs a second copy of the app on a stepped
 * clock (a frame is exactly 1/30 s, the sound is rendered by the engine's own mixer into the recording and never to the device), presses the pad's buttons through the app's
 * own input path, and records in <data>/Demo/: numbered PNGs of what the app drew, demo-<stamp>.mp4 with the app's sound, the demo song's cue file, and rec-demo.txt.
 *
 * What the recorder (theme/r3i6nos_demo.h) keeps is the 64 x 24 cells; MusiCue's waveform is pixels over rows 1-6, so each frame also carries a small entry of what the
 * waveform needs (the playhead's, the loop's and the markers' columns and the colours) and the song's peaks are in the file's head: the pixel layer of the recorder.
 *
 * The tour is one block for each hero feature of the README's list (DF_*), each starting from a known place, so a feature records alone. The songs it plays are the app's
 * own demo song (made in code by `--make-demo`, 96 s) and the Key and Tempo copy of it that the real musicue-render makes: a thing that runs on its own clock (the render,
 * the decode) is waited for with no frames, so two recordings are the same whatever the unit's speed. Not built: Sync (it needs a network) and the Offset: each is a SKIP line.
 *
 * Untested on a unit: all of it. Run in Debian 12 arm64 under Docker and on the Mac only. */
#ifndef MUSICUE_DEMO_H
#define MUSICUE_DEMO_H

enum { DF_WAVEFORM, DF_MARKERS, DF_JUMPS, DF_LOOPS, DF_CUES, DF_PANELS, DF_PIANO, DF_THEMES, DF_KEYTEMPO, DF_COUNT };
static const char *const DF_NAME[DF_COUNT] = {"waveform", "markers", "jumps", "loops", "cues", "panels", "piano", "themes", "keytempo"};
static const char *const DF_TITLE[DF_COUNT] = {"The waveform", "Markers", "Instant jumps", "Gapless A/B loops", "Cues and Rec mode", "Panels and Jump", "The Piano", "Colour themes", "Key and Tempo copy"};
static const char *const DF_BLURB[DF_COUNT] = {"the whole song, a playhead", "flags and the list", "D-pad to the next cue", "R1+Up, R1+Down", "L1+Start, Down adds", "Fn turns, Fn while playing", "find the key by ear", "13, true colour", "a copy at another key"};

static void demo_checklist_open(App *a) {
    r3_checklist_init(&a->tour_ck, DF_COUNT, DF_NAME, DF_TITLE, DF_BLURB);
    a->overlay = OV_DEMO;
    a->force = 1;
}

/* ---- the pixel layer: what the waveform needs, by frame ------------------------------------------------------------------------------------------------ */

#define DA_MARKS 32
typedef struct {
    uint8_t on, song, loop_on, nmarks;      /* the waveform shows; which of the two songs (0 the demo song, 1 its copy); the A-B span is drawn; markers drawn */
    int16_t playx, la, lb, pad;             /* the columns (0-639) of the playhead and the loop's ends */
    WaveStyle st;
    int16_t mx[DA_MARKS];
    uint32_t mc[DA_MARKS];
} DemoAux;
typedef struct { WavePeaks pk[2]; double dur[2]; } DemoHead;

/* The waveform over a rendered screen, from an entry. Positions are columns, so the playhead is the same pixel in any run (and a frame that only moves the playhead
 * within a column is the same record). */
static void demo_paint(void *ctx, uint32_t *px, const void *headp, const void *entryp) {
    (void)ctx;
    const DemoHead *h = (const DemoHead *)headp;
    const DemoAux *x = (const DemoAux *)entryp;
    if (!x->on || x->song > 1) return;
    const double dur = h->dur[x->song], per = dur / (WAVE_W - 1);
    WaveMark marks[DA_MARKS];
    for (int i = 0; i < x->nmarks && i < DA_MARKS; i++) { marks[i].t = x->mx[i] * per; marks[i].color = x->mc[i]; }
    wave_render(px + (size_t)R3_CELL_H * R3_W, R3_W, &h->pk[x->song], &x->st, dur, x->playx * per, marks, x->nmarks, x->loop_on, x->la * per, x->lb * per);
}

static void demo_aux_of(const App *a, DemoAux *x, int song) {
    memset(x, 0, sizeof *x);
    if (!wave_visible(a)) return;
    const double dur = dur_of(a);
    WaveMark marks[WAVE_MAX_MARKS];
    x->on = 1;
    x->song = (uint8_t)song;
    wave_style(&x->st);
    x->playx = (int16_t)wave_x(engine_pos_sec(&a->eng), dur);
    x->loop_on = a->loop_state == 2;
    x->la = (int16_t)wave_x(a->loop_a, dur);
    x->lb = (int16_t)wave_x(a->loop_b, dur);
    int nm = wave_marks(a, marks, WAVE_MAX_MARKS);
    if (nm > DA_MARKS) nm = DA_MARKS;
    x->nmarks = (uint8_t)nm;
    for (int i = 0; i < nm; i++) { x->mx[i] = (int16_t)wave_x(marks[i].t, dur); x->mc[i] = marks[i].color; }
}

/* ---- the recorder's state ------------------------------------------------------------------------------------------------------------------------------- */

typedef struct {
    App *a;                                 /* the second copy of the app */
    R3Demo d;
    DemoHead head;
    int song;                               /* 0 the demo song, 1 the copy */
    unsigned mask;
    SDL_Renderer *r; R3Video *video;        /* the unit's own screen, for the progress (not recorded); NULL in tests */
    R3Screen cells;
    uint32_t *px;
    int stop;                               /* 1: cancelled (Select+Start held, Esc), 2: a hot-plug failure */
    char doing[96];
    long plan;                              /* frames the tour expects, for the bar */
    int sel_held, start_held; Uint32 both_since;
    int last_pct;
    char dir[PATH_MAX];
} Rd;

#define RD_FRAMES_FIXED 200                 /* the splash, the songs, the ending */
static const int RD_FRAMES[DF_COUNT] = {210, 175, 215, 300, 215, 240, 215, 245, 470};   /* a close estimate of each block, for the progress bar only */

static int rd_on(const Rd *rd, int f) { return (rd->mask >> f) & 1; }

static void rd_show(Rd *rd) {
    if (!rd->video) return;
    int pct = rd->plan > 0 ? (int)(rd->d.n_frames * 100 / rd->plan) : 0;
    if (pct > 99) pct = 99;
    R3Screen s;
    r3_clear(&s);
    compose_topbar(rd->a, &s, "MusiCue", "Rec demo");
    r3_box(&s, 6, 5, 52, 10, &(R3Box){1, "Rec demo", NULL, "Select+Start held: cancel", 0});
    r3_text_fit(&s, 8, 7, "The unit is recording its own screen and sound.", 48, R3_ST(R3_ST_BODY));
    r3_text_fit(&s, 8, 8, "Nothing else runs until it is done.", 48, R3_ST(R3_ST_BODY));
    r3_progress(&s, 8, 11, 48, pct);
    char bar[40], stat[96];
    int full = pct / 10;
    snprintf(bar, sizeof bar, "[%.*s%.*s] %d%%", full, "##########", 10 - full, "..........", pct);
    snprintf(stat, sizeof stat, "%.1f s recorded", r3_demo_seconds(&rd->d));
    R3Cmd cmd = {rd->doing, R3_ST_STRONG, '>', bar, 0, stat, "", R3_ST_HINT};
    r3_command_line(&s, &cmd);
    r3_video_draw(rd->video, rd->r, &s, 1);
    if (pct != rd->last_pct) { rd->last_pct = pct; r3_demo_status("MusiCue", pct, rd->doing, rd->dir); }
}

/* Between frames: the cancel (Select and Start held for a second, or Esc), a hot-plug failure, the progress. */
static void rd_poll(Rd *rd) {
    if (rd->video) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) rd->stop = 1;
            else if (e.type == SDL_JOYBUTTONDOWN || e.type == SDL_JOYBUTTONUP) {
                int dn = e.type == SDL_JOYBUTTONDOWN;
                if (e.jbutton.button == R3_BTN_SELECT) rd->sel_held = dn;
                if (e.jbutton.button == R3_BTN_START) rd->start_held = dn;
                rd->both_since = rd->sel_held && rd->start_held ? (rd->both_since ? rd->both_since : SDL_GetTicks()) : 0;
            } else if (e.type == SDL_JOYDEVICEADDED) SDL_JoystickOpen(e.jdevice.which);
        }
        if (rd->both_since && SDL_GetTicks() - rd->both_since >= 1000) rd->stop = 1;
    }
    if (!rd->stop && r3_demo_failed(&rd->d)) rd->stop = 2;
    rd_show(rd);
}

/* One video frame: 1/30 s of the app's sound, the app's clock stepped by exactly that, the screen as the app composes it, and the waveform's entry. */
static void rd_frame(Rd *rd) {
    App *a = rd->a;
    const int n = TRACK_RATE / R3_DEMO_FPS;
    int16_t snd[(TRACK_RATE / R3_DEMO_FPS) * 2];
    float f[(TRACK_RATE / R3_DEMO_FPS) * 2];
    a->eng.noaudio = 1;                                         /* never the device: the engine's mixer renders into the recording */
    a->eng.stepped = 1;
    if (a->track.pcm && a->eng.t && (a->open >= 0 || a->demo)) engine_render(&a->eng, snd, n);
    else memset(snd, 0, sizeof snd);
    for (int i = 0; i < n * 2; i++) f[i] = (float)snd[i] / 32768.0f;
    app_tick(a, 1000.0 / R3_DEMO_FPS);
    compose(a, &rd->cells);
    DemoAux x;
    demo_aux_of(a, &x, rd->song);
    r3_demo_audio(&rd->d, f, (size_t)n);
    r3_demo_frame_aux(&rd->d, &rd->cells, &x);
    if (rd->d.n_frames % 8 == 0) rd_poll(rd);
}

static void rd_hold(Rd *rd, int frames) { for (int i = 0; i < frames && !rd->stop; i++) rd_frame(rd); }

static void rd_press(Rd *rd, int bt) { pad_button(rd->a, bt, 1); pad_button(rd->a, bt, 0); rd_hold(rd, 1); }
static void rd_with(Rd *rd, int mod, int bt) { pad_button(rd->a, mod, 1); pad_button(rd->a, bt, 1); pad_button(rd->a, bt, 0); pad_button(rd->a, mod, 0); rd_hold(rd, 1); }
static void rd_stick(Rd *rd, int act) { stick_dispatch(rd->a, act); rd_hold(rd, 1); }

static void rd_step(Rd *rd, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void rd_step(Rd *rd, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(rd->doing, sizeof rd->doing, fmt, ap);
    va_end(ap);
    rd_show(rd);
}

static void rd_cap(Rd *rd, int f, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void rd_cap(Rd *rd, int f, const char *fmt, ...) {
    char text[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    r3_demo_caption(&rd->d, DF_NAME[f], "%s", text);
}

/* A screenshot of what the app has drawn now: the cells and the waveform over them. */
static void rd_shot(Rd *rd, const char *name) {
    DemoAux x;
    compose(rd->a, &rd->cells);
    demo_aux_of(rd->a, &x, rd->song);
    r3_render(&rd->cells, rd->px);
    demo_paint(NULL, rd->px, &rd->head, &x);
    r3_demo_shot_px(&rd->d, &rd->cells, rd->px, name);
}

/* The song's peaks for the pixel layer, once it is open (the head has a place for the demo song and for its copy). */
static void rd_head_set(Rd *rd, int song) {
    rd->head.pk[song] = rd->a->peaks;
    rd->head.dur[song] = dur_of(rd->a);
    r3_demo_aux_patch(&rd->d, (size_t)((char *)&rd->head.pk[song] - (char *)&rd->head), &rd->head.pk[song], sizeof rd->head.pk[song]);
    r3_demo_aux_patch(&rd->d, (size_t)((char *)&rd->head.dur[song] - (char *)&rd->head), &rd->head.dur[song], sizeof rd->head.dur[song]);
}

/* A known place for a hero feature to start from: the open song at `pos`, stopped, no loop, no Rec mode, the first two panels, nothing over the screen. */
static void rd_home(Rd *rd, double pos) {
    App *a = rd->a;
    a->overlay = OV_NONE;
    a->mode = M_PLAYER;
    a->piano = 0;
    engine_notes_off(&a->eng);
    engine_play(&a->eng, 0);
    a->rec = 0; a->nrec = 0;
    a->loop_state = 0; a->loop_off = 0;
    engine_loop_clear(&a->eng);
    a->auto_jump = 0;
    a->jump_view = 0; a->pan_first = 0; a->pan_focus = 0; a->prev_view = 0;
    a->msel = 0; a->csel = 0;
    engine_seek(&a->eng, pos);
    say(a, R3_ST_HINT, "%d markers, %d cues. Start plays, L1+Start records.", a->cues.nm, a->cues.nc);
    rd_hold(rd, 6);
}

/* Cue edge presses to a cue: Right `n` times from the start (the cues are 9.6 s apart: 100 bpm, 4 bars). */
static void rd_cues_on(Rd *rd, int n) { for (int i = 0; i < n; i++) rd_press(rd, R3_BTN_RIGHT); }

static void rm_rf(const char *path) {
    struct stat sb;
    if (lstat(path, &sb) != 0) return;
    if (S_ISDIR(sb.st_mode)) {
        DIR *d = opendir(path);
        struct dirent *e;
        while (d && (e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char p[PATH_MAX];
            snprintf(p, sizeof p, "%s/%s", path, e->d_name);
            rm_rf(p);
        }
        if (d) closedir(d);
        rmdir(path);
    } else unlink(path);
}

/* ---- the tour ----------------------------------------------------------------------------------------------------------------------------------------- */

static void tour_waveform(Rd *rd) {
    rd_step(rd, "The waveform");
    rd_home(rd, 0);
    rd_cap(rd, DF_WAVEFORM, "the whole song is one waveform, the loud choruses fat and the intro thin");
    rd_hold(rd, 24);
    rd_cues_on(rd, 3);                                          /* to the pre-chorus, 28.8 s: the chorus comes in 3 s */
    rd_press(rd, R3_BTN_START);
    rd_cap(rd, DF_WAVEFORM, "Start plays: the played part is bright, the part to come dim, a playhead with a soft edge moves across");
    rd_hold(rd, 120);
    rd_shot(rd, "waveform-playing");
    rd_hold(rd, 45);
    rd_press(rd, R3_BTN_START);
    rd_hold(rd, 10);
}

static void tour_markers(Rd *rd) {
    rd_step(rd, "Markers");
    rd_home(rd, 0);
    rd_cap(rd, DF_MARKERS, "Cue's markers are flags on the waveform and a list: the one the playhead is in, and the next one in so many seconds");
    rd_hold(rd, 24);
    for (int i = 0; i < 3; i++) { rd_stick(rd, R3_DOWN); rd_hold(rd, 12); }
    rd_shot(rd, "markers-list");
    rd_press(rd, R3_BTN_A);
    rd_cap(rd, DF_MARKERS, "A on a marker jumps to it");
    rd_hold(rd, 15);
    rd_press(rd, R3_BTN_START);
    rd_hold(rd, 75);
    rd_press(rd, R3_BTN_START);
    rd_hold(rd, 10);
}

static void tour_jumps(Rd *rd) {
    rd_step(rd, "Instant jumps");
    rd_home(rd, 0);
    rd_cap(rd, DF_JUMPS, "the song is in memory: a jump is a pointer move, and the sound is there at once");
    rd_press(rd, R3_BTN_START);
    rd_hold(rd, 25);
    rd_press(rd, R3_BTN_RIGHT);
    rd_hold(rd, 40);
    rd_press(rd, R3_BTN_RIGHT);
    rd_hold(rd, 30);
    rd_shot(rd, "jump-to-next-cue");
    rd_press(rd, R3_BTN_DOWN);
    rd_cap(rd, DF_JUMPS, "Down is the last cue, Up the first");
    rd_hold(rd, 40);
    rd_press(rd, R3_BTN_UP);
    rd_hold(rd, 40);
    rd_press(rd, R3_BTN_START);
    rd_hold(rd, 10);
}

static void tour_loops(Rd *rd) {
    rd_step(rd, "Gapless A/B loops");
    rd_home(rd, 0);
    rd_cues_on(rd, 2);
    rd_press(rd, R3_BTN_START);
    rd_hold(rd, 15);
    rd_with(rd, R3_BTN_R1, R3_BTN_UP);
    rd_cap(rd, DF_LOOPS, "R1+Up sets A at the playhead");
    rd_hold(rd, 105);
    rd_with(rd, R3_BTN_R1, R3_BTN_DOWN);
    rd_cap(rd, DF_LOOPS, "R1+Down sets B: the span between them is tinted and loops with no gap");
    rd_hold(rd, 80);
    rd_shot(rd, "ab-loop");
    rd_hold(rd, 50);
    rd_with(rd, R3_BTN_R1, R3_BTN_START);
    rd_cap(rd, DF_LOOPS, "R1+Start switches the loop off: the song plays on past B");
    rd_hold(rd, 40);
    rd_press(rd, R3_BTN_START);
    rd_hold(rd, 10);
}

static void tour_cues(Rd *rd) {
    rd_step(rd, "Cues and Rec mode");
    rd_home(rd, 0);
    rd_cues_on(rd, 4);
    rd_cap(rd, DF_CUES, "the cue panel: the line now, the next one muted, and a beat dot that pulses at the Tempo");
    rd_hold(rd, 20);
    rd_with(rd, R3_BTN_L1, R3_BTN_START);
    rd_cap(rd, DF_CUES, "L1+Start plays in Rec mode (REC in the top bar): Down adds a cue at the playhead, exactly");
    rd_hold(rd, 25);
    rd_press(rd, R3_BTN_DOWN);
    rd_hold(rd, 35);
    rd_press(rd, R3_BTN_DOWN);
    rd_hold(rd, 35);
    rd_press(rd, R3_BTN_DOWN);
    rd_hold(rd, 15);
    rd_shot(rd, "rec-mode");
    rd_hold(rd, 15);
    rd_press(rd, R3_BTN_UP);
    rd_cap(rd, DF_CUES, "Up takes the last cue back");
    rd_hold(rd, 20);
    rd_press(rd, R3_BTN_UP);
    rd_press(rd, R3_BTN_UP);
    rd_hold(rd, 20);
    rd_press(rd, R3_BTN_START);
    rd_hold(rd, 10);
}

static void tour_panels(Rd *rd) {
    rd_step(rd, "Panels and the Jump panel");
    rd_home(rd, 0);
    rd_cues_on(rd, 4);
    rd_cap(rd, DF_PANELS, "six panels two at a time: Fn turns them (Markers, Cues, Metadata, Set List, Last 10, Top 10)");
    rd_hold(rd, 15);
    rd_press(rd, R3_BTN_FN);
    rd_hold(rd, 25);
    rd_press(rd, R3_BTN_FN);
    rd_hold(rd, 20);
    rd_shot(rd, "panels-metadata");
    rd_hold(rd, 10);
    rd_with(rd, R3_BTN_L1, R3_BTN_FN);
    rd_hold(rd, 12);
    rd_press(rd, R3_BTN_START);
    rd_hold(rd, 12);
    rd_press(rd, R3_BTN_FN);
    rd_cap(rd, DF_PANELS, "Fn while the song plays goes straight to the Jump panel: every marker with Cue's progress fill");
    rd_hold(rd, 80);
    rd_shot(rd, "jump-panel");
    rd_hold(rd, 25);
    rd_press(rd, R3_BTN_START);
    rd_cap(rd, DF_PANELS, "stopping brings the panels back");
    rd_hold(rd, 30);
}

static void tour_piano(Rd *rd) {
    App *a = rd->a;
    rd_step(rd, "The Piano");
    rd_home(rd, 0);
    rd_cap(rd, DF_PIANO, "Metadata > Key opens a piano on the pad: one octave, C3 to B3, sounding over the song, to find a key by ear");
    rd_press(rd, R3_BTN_FN);
    rd_press(rd, R3_BTN_FN);
    rd_hold(rd, 15);
    rd_press(rd, R3_BTN_A);
    rd_hold(rd, 20);
    static const int arp[3] = {R3_BTN_UP, R3_BTN_DOWN, R3_BTN_Y};        /* C, E, G */
    for (int i = 0; i < 3; i++) { pad_button(a, arp[i], 1); rd_hold(rd, 16); pad_button(a, arp[i], 0); rd_hold(rd, 6); }
    for (int i = 0; i < 3; i++) pad_button(a, arp[i], 1);
    rd_hold(rd, 45);
    rd_shot(rd, "piano-chord");
    for (int i = 0; i < 3; i++) pad_button(a, arp[i], 0);
    rd_hold(rd, 30);
    rd_press(rd, R3_BTN_FN);
    rd_hold(rd, 12);
    rd_press(rd, R3_BTN_B);                                              /* the Key stays as it was */
    rd_hold(rd, 12);
}

static void tour_themes(Rd *rd) {
    App *a = rd->a;
    rd_step(rd, "Colour themes");
    rd_home(rd, 28.8);
    rd_cap(rd, DF_THEMES, "L1+R1 opens the main menu; Colour theme previews each of the 13 as the cursor moves, waveform and all");
    rd_hold(rd, 15);
    pad_button(a, R3_BTN_L1, 1); pad_button(a, R3_BTN_R1, 1); pad_button(a, R3_BTN_R1, 0); pad_button(a, R3_BTN_L1, 0);
    rd_hold(rd, 30);
    rd_shot(rd, "main-menu");
    rd_press(rd, R3_BTN_DOWN);
    rd_hold(rd, 8);
    rd_press(rd, R3_BTN_A);
    rd_hold(rd, 20);
    for (int i = 0; i < 4; i++) { rd_press(rd, R3_BTN_DOWN); rd_hold(rd, 28); if (i == 1) rd_shot(rd, "theme-preview"); }
    rd_press(rd, R3_BTN_A);
    rd_hold(rd, 40);
    rd_shot(rd, "theme-chosen");
    a->theme = r3_theme_find("Nord");                                     /* the next block, and the end, are the default look again */
    apply_theme(a);
    rd_hold(rd, 10);
}

static int rd_wait_job(Rd *rd) {                                         /* the render runs on its own clock: waited for with no frames */
    App *a = rd->a;
    while (a->job_pid > 0 && !rd->stop) {
        job_poll(a);
        rd_poll(rd);
        usleep(20000);
    }
    job_poll(a);
    return a->job_done;
}

static void tour_keytempo(Rd *rd) {
    App *a = rd->a;
    rd_step(rd, "Key and Tempo");
    rd_home(rd, 0);
    rd_cap(rd, DF_KEYTEMPO, "L1+R1 > Song options > Key and Tempo: a copy of the song two semitones up and a little faster, made by musicue-render on four cores");
    pad_button(a, R3_BTN_L1, 1); pad_button(a, R3_BTN_R1, 1); pad_button(a, R3_BTN_R1, 0); pad_button(a, R3_BTN_L1, 0);
    rd_hold(rd, 10);
    for (int i = 0; i < 6; i++) rd_press(rd, R3_BTN_DOWN);                /* About, Colour theme, Keymap, Device report, Rec demo, Quit, then Song options */
    rd_hold(rd, 8);
    rd_press(rd, R3_BTN_A);
    rd_hold(rd, 20);
    rd_shot(rd, "song-options");
    for (int i = 0; i < 5; i++) rd_press(rd, R3_BTN_DOWN);
    rd_hold(rd, 10);
    rd_press(rd, R3_BTN_A);
    if (a->mode != M_KT) { r3_report_line(&rd->d.log, R3_FAIL, "keytempo", "the dialog did not open: %s", a->reply); return; }
    rd_hold(rd, 20);
    const char *wav = getenv("R3I6NOS_DEMO_KT_WAV");                    /* tests: a WAV copy, so no ffmpeg is needed to open it */
    if (wav && *wav == '1') rd_press(rd, R3_BTN_SELECT);
    for (int i = 0; i < 2; i++) { rd_press(rd, R3_BTN_RIGHT); rd_hold(rd, 18); }
    for (int i = 0; i < 8; i++) { rd_press(rd, R3_BTN_UP); rd_hold(rd, 4); }
    rd_hold(rd, 35);
    rd_shot(rd, "key-and-tempo");
    rd_press(rd, R3_BTN_A);
    if (a->mode != M_JOB) { r3_report_line(&rd->d.log, R3_FAIL, "keytempo", "the render did not start: %s", a->reply); rd_press(rd, R3_BTN_B); return; }
    rd_step(rd, "Rendering the copy (the real musicue-render, no frames)");
    int ok = rd_wait_job(rd);
    if (rd->stop) return;
    if (!ok) { r3_report_line(&rd->d.log, R3_FAIL, "keytempo", "the render did not finish: %s", a->reply); return; }
    for (int i = 0; i < a->job_nlines; i++) {                            /* the helper's last line says how long it took on this unit: the video shows what is the same on any */
        char *at = strstr(a->job_lines[i], " in ");
        if (!strncmp(a->job_lines[i], "DONE", 4) && at) *at = 0;
    }
    rd_step(rd, "The copy");
    rd_cap(rd, DF_KEYTEMPO, "the copy is made (the render took no frames: the video shows what it left)");
    rd_hold(rd, 50);
    rd_shot(rd, "copy-made");
    rd_press(rd, R3_BTN_A);
    if (song_by_id(a, a->kt_copy_id) >= 0) a->lsel = song_by_id(a, a->kt_copy_id);
    rd_hold(rd, 30);
    rd_shot(rd, "songs-with-copy");
    rd_press(rd, R3_BTN_A);
    if (a->mode != M_PLAYER) { r3_report_line(&rd->d.log, R3_FAIL, "keytempo", "the copy did not open: %s", a->reply); return; }
    rd->song = 1;
    rd_head_set(rd, 1);
    rd_hold(rd, 15);
    rd_press(rd, R3_BTN_START);
    rd_cap(rd, DF_KEYTEMPO, "the copy plays in the new key and tempo; its markers moved with the tempo, the Key and Tempo fields say what it is");
    rd_hold(rd, 105);
    rd_shot(rd, "copy-playing");
    rd_hold(rd, 30);
    rd_press(rd, R3_BTN_START);
    rd_hold(rd, 10);
}

/* ---- the run -------------------------------------------------------------------------------------------------------------------------------------------- */

/* Records the tour into `dir` (<data>/Demo). `mask`: the hero features to show. `video` and `r` (both NULL in a test) show the progress on the unit's screen.
 * 0 when everything is there; 75 when another recording holds the lock; 1 else. `failure` gets the first hot-plug failure's words, if one stopped it. */
static int musicue_demo(SDL_Renderer *r, R3Video *video, const char *dir, unsigned mask, int no_video, int quiet, char *failure, size_t failure_cap) {
    if (failure_cap) failure[0] = 0;
    if (!r3_demo_lock()) { fprintf(stderr, "musicue: another Rec demo is recording\n"); return 75; }
    mask &= (1u << DF_COUNT) - 1;
    if (!mask) mask = (1u << DF_COUNT) - 1;
    Rd *rd = (Rd *)calloc(1, sizeof *rd);
    App *a = (App *)calloc(1, sizeof *a);
    rd->px = (uint32_t *)malloc((size_t)R3_W * R3_H * sizeof *rd->px);
    if (!rd || !a || !rd->px) { free(rd ? rd->px : NULL); free(rd); free(a); r3_demo_unlock(); return 1; }
    rd->a = a; rd->r = r; rd->video = video; rd->mask = mask;
    snprintf(rd->dir, sizeof rd->dir, "%s", dir);
    char work[PATH_MAX + 16];
    snprintf(work, sizeof work, "%s/.work", dir);
    const char *old_noaudio = getenv("R3I6NOS_MUSICUE_NOAUDIO");
    char saved_noaudio[8] = "";
    if (old_noaudio) snprintf(saved_noaudio, sizeof saved_noaudio, "%s", old_noaudio);
    setenv("R3I6NOS_MUSICUE_NOAUDIO", "1", 1);                           /* no sound device in the tour: the mixer renders into the recording */
    r3_marquee_reset();
    r3_demo_status("MusiCue", 0, "Starting", dir);
    int rc = 1;
    mkdirs(dir);
    rm_rf(work);
    mkdirs(work);
    app_init(a, work);
    apply_theme(a);
    if (make_demo(work) != 0) { fprintf(stderr, "musicue: the demo song could not be made in %s\n", work); goto out; }
    scan_songs(a);
    rd->d.log.quiet = quiet;
    if (r3_demo_open(&rd->d, dir, "MusiCue", VERSION, "r36s-musicue", "Main menu > Rec demo", "MusiCue", TRACK_RATE) != 0) { fprintf(stderr, "musicue: Rec demo cannot write %s: %s\n", dir, strerror(errno)); goto out; }
    rd->d.no_video = no_video;
    r3_demo_build(&rd->d, "v" VERSION " " BUILD_LINE);
    if (r3_demo_aux_open(&rd->d, &rd->head, sizeof rd->head, sizeof(DemoAux), demo_paint, NULL) != 0) { rd->d.err = 1; }
    {
        char shows[256] = "";
        for (int f = 0; f < DF_COUNT; f++) {
            if (rd_on(rd, f)) snprintf(shows + strlen(shows), sizeof shows - strlen(shows), "%s%s", shows[0] ? "," : "", DF_NAME[f]);
            else { char key[48]; snprintf(key, sizeof key, "%s", DF_NAME[f]); r3_report_line(&rd->d.log, R3_SKIP, key, "not chosen"); }
        }
        r3_report_line(&rd->d.log, R3_INFO, "shows", "%s", shows);
        r3_report_line(&rd->d.log, R3_SKIP, "sync", "it needs a network: the demo shows no network");
        r3_report_line(&rd->d.log, R3_SKIP, "offset", "a per-song shift: no hero feature of its own");
        r3_report_line(&rd->d.log, R3_INFO, "song", "the app's own demo song (musicue --make-demo): 96 s, 100 bpm, original, made in code");
        r3_report_line(&rd->d.log, R3_INFO, "clock", "stepped: %d fps, %d sound frames a video frame, the sound rendered by the engine's own mixer", R3_DEMO_FPS, TRACK_RATE / R3_DEMO_FPS);
    }
    rd->plan = RD_FRAMES_FIXED;
    for (int f = 0; f < DF_COUNT; f++) if (rd_on(rd, f)) rd->plan += RD_FRAMES[f];

    /* the splash, as the app shows it at start (rule 12), with the version line */
    rd_step(rd, "The splash");
    {
        static const struct { const char *what; int pct; } steps[3] = {{"Looking for your data", 50}, {"Finding your songs", 80}, {"Ready", 100}};
        for (int i = 0; i < 3 && !rd->stop; i++) {
            r3_splash(&rd->cells, "MusiCue", VERSION, BUILD_LINE, "Markers and cues on your songs", steps[i].what, steps[i].pct);
            DemoAux none;
            memset(&none, 0, sizeof none);
            for (int k = 0; k < 30 && !rd->stop; k++) {
                float silence[(TRACK_RATE / R3_DEMO_FPS) * 2] = {0};
                r3_demo_audio(&rd->d, silence, TRACK_RATE / R3_DEMO_FPS);
                r3_demo_frame_aux(&rd->d, &rd->cells, &none);
                if (rd->d.n_frames % 8 == 0) rd_poll(rd);
            }
            if (i == 0) {
                r3_demo_shot(&rd->d, &rd->cells, "splash");
                r3_demo_caption(&rd->d, "start", "the splash: the name, the load, and the version line v%s %s", VERSION, BUILD_LINE);
            }
        }
    }
    rd_step(rd, "The songs");
    say(a, R3_ST_HINT, "%d song%s. A opens one, L1+R1 is the menu.", a->nsongs, a->nsongs == 1 ? "" : "s");
    rd_hold(rd, 60);
    rd_shot(rd, "songs");
    rd_press(rd, R3_BTN_A);                                              /* the song is decoded into memory, as a person opens it */
    if (a->mode != M_PLAYER) { fprintf(stderr, "musicue: the demo song did not open: %s\n", a->reply); r3_report_line(&rd->d.log, R3_FAIL, "open", "the demo song did not open: %s", a->reply); goto finish; }
    rd_head_set(rd, 0);
    rd_hold(rd, 15);

    if (rd_on(rd, DF_WAVEFORM) && !rd->stop) tour_waveform(rd);
    if (rd_on(rd, DF_MARKERS) && !rd->stop) tour_markers(rd);
    if (rd_on(rd, DF_JUMPS) && !rd->stop) tour_jumps(rd);
    if (rd_on(rd, DF_LOOPS) && !rd->stop) tour_loops(rd);
    if (rd_on(rd, DF_CUES) && !rd->stop) tour_cues(rd);
    if (rd_on(rd, DF_PANELS) && !rd->stop) tour_panels(rd);
    if (rd_on(rd, DF_PIANO) && !rd->stop) tour_piano(rd);
    if (rd_on(rd, DF_THEMES) && !rd->stop) tour_themes(rd);
    if (rd_on(rd, DF_KEYTEMPO) && !rd->stop) tour_keytempo(rd);

    /* back to the start screen, as the next run begins */
    if (!rd->stop) {
        rd_step(rd, "Back to the songs");
        if (a->mode == M_PLAYER) { rd_press(rd, R3_BTN_B); }
        a->overlay = OV_NONE;
        if (a->mode != M_LIBRARY) { close_song(a); a->mode = M_LIBRARY; }
        say(a, R3_ST_HINT, "%d song%s. A opens one, L1+R1 is the menu.", a->nsongs, a->nsongs == 1 ? "" : "s");
        rd_hold(rd, 30);
    }
finish:
    {
        if (rd->stop == 1) rd->d.cancelled = 1;
        char cuefile[PATH_MAX + 64];
        snprintf(cuefile, sizeof cuefile, "%s/cues/demoSong001.json", work);
        rd_step(rd, rd->d.no_video ? "Writing the log" : "Encoding the video");
        rd->d.on_progress = NULL;
        rc = r3_demo_finish(&rd->d, cuefile, 1) == 0 ? 0 : 1;
        if (rd->d.n_failures && failure_cap) snprintf(failure, failure_cap, "%s", rd->d.failure[0]);
    }
out:
    app_free(a);
    rm_rf(work);
    if (old_noaudio) setenv("R3I6NOS_MUSICUE_NOAUDIO", saved_noaudio, 1); else unsetenv("R3I6NOS_MUSICUE_NOAUDIO");
    r3_demo_status_done(rc);
    r3_demo_unlock();
    free(rd->px);
    free(rd);
    free(a);
    return rc;
}

#endif /* MUSICUE_DEMO_H */

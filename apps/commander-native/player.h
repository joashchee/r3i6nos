/* player.h: Commander's Play tab (header-only, MIT). Plays what Rec demo (docs/DEMO-RECORDINGS.md) makes, and sound and video besides:
 *
 *   PL_R3F    a kept `demo-<stamp>-frames.r3f` (records of {run, 64x24 cell screen}), drawn with the theme's own renderer, with the
 *             `demo-<stamp>.wav` beside it as its sound. Needs nothing else.
 *   PL_WAV    a PCM WAV (8 or 16 bit, mono or stereo), through SDL's audio queue. Needs nothing else.
 *   PL_VIDEO  any video (the `demo-<stamp>.mp4` Rec demo makes), and PL_AUDIO any other sound: both through `ffmpeg` when the unit has one
 *             (R3I6NOS_FFMPEG names another), which decodes to raw frames and PCM on pipes. No H.264 or AAC decoder is built in.
 *
 *   PL_NTT    MIDI, the Tracker's formats and music written as text, through `nttplay` (Neumetik's players as a command-line tool, always in the
 *             image at /usr/bin/nttplay; R3I6NOS_NTTPLAY names another; ROADMAP section 27): `nttplay probe FILE` first (the length, or why it cannot
 *             be read), then `nttplay decode FILE -ar 44100`, 16-bit stereo PCM on a pipe, read the way ffmpeg's is.
 *
 * The clock is the sound: the seconds fed to the audio device minus what it still holds (so picture and sound cannot drift); with no sound
 * it is the caller's millisecond clock. Nothing here blocks: pipes are non-blocking, the UI loop calls pl_pump about every 16 ms.
 * R3I6NOS_FM_NOAUDIO=1 plays silently on the caller's clock (tests, and units whose sound device will not open).
 * Include r3i6nos_ansiapps.h first. Every figure about the unit (does ffmpeg exist there, does a pipe keep up) is untested. */
#ifndef COMMANDER_PLAYER_H
#define COMMANDER_PLAYER_H

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#define PL_FPS 30
#define PL_W R3_W                                 /* the picture area: the whole display view */
#define PL_H (R3_DISPLAY_ROWS * R3_CELL_H)
#define PL_FRAME_BYTES ((size_t)PL_W * PL_H * 4)

enum { PL_NONE, PL_R3F, PL_WAV, PL_VIDEO, PL_AUDIO, PL_NTT };

typedef struct {
    int mode, has_video, has_audio, playing, ended;
    char path[PATH_MAX + 32], wavpath[PATH_MAX + 64];
    char note[160];                 /* what is not working (no sound device), shown under the picture; empty when all is well */
    uint32_t *px;                   /* PL_W x PL_H, the picture now */
    double total;                   /* seconds, 0 when not known */
    double now;                     /* seconds played */
    double last_ms;                 /* the caller's clock at the last pump (the clock when there is no sound) */
    /* frames from an r3f file */
    FILE *r3f;
    long frame_end, rec_left;       /* the frame the current record ends on; records not read yet */
    R3Screen cur;
    uint32_t *full;                 /* the 640 x 480 render of `cur`, scaled into px */
    /* frames from ffmpeg */
    int vfd, afd;                   /* pipes; -1 when none */
    pid_t vpid, apid;
    unsigned char *vbuf;
    size_t vgot;
    long vidx;
    int v_eof, a_eof;
    /* sound */
    SDL_AudioDeviceID dev;
    FILE *wav;                      /* a WAV file's data, read from its data chunk on */
    long wav_left;
    int rate, chans, bps;           /* sample rate, channels, bytes in one frame of sound */
    unsigned long long fed;         /* bytes queued so far */
    int primed;
} Player;

static void pl_zero(Player *p) {
    memset(p, 0, sizeof *p);
    p->vfd = p->afd = -1;
}

static void pl_note(Player *p, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void pl_note(Player *p, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p->note, sizeof p->note, fmt, ap);
    va_end(ap);
}

static void pl_stop_child(int *fd, pid_t *pid) {
    if (*fd > 0) close(*fd);          /* > 0: a zeroed Player has fd 0, which is not ours */
    *fd = -1;
    if (*pid > 0) {
        kill(*pid, SIGKILL);        /* ffmpeg blocks on a full pipe: closing alone would not end it */
        waitpid(*pid, NULL, 0);
    }
    *pid = 0;
}

static void pl_close(Player *p) {
    if (p->dev) { SDL_CloseAudioDevice(p->dev); p->dev = 0; }
    if (p->r3f) fclose(p->r3f);
    if (p->wav) fclose(p->wav);
    pl_stop_child(&p->vfd, &p->vpid);
    pl_stop_child(&p->afd, &p->apid);
    free(p->px);
    free(p->full);
    free(p->vbuf);
    pl_zero(p);
}

/* Finds a helper program: the environment variable's file, else `name` in PATH, /usr/local/bin or /usr/bin. */
static int pl_find_tool(const char *envvar, const char *name, char *out, size_t cap) {
    const char *e = getenv(envvar);
    if (e && *e) { snprintf(out, cap, "%s", e); return access(e, X_OK) == 0; }
    const char *path = getenv("PATH");
    char list[4096];
    snprintf(list, sizeof list, "%s:/usr/local/bin:/usr/bin", path ? path : "");
    for (char *save = NULL, *dir = strtok_r(list, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        snprintf(out, cap, "%s/%s", dir, name);
        if (access(out, X_OK) == 0) return 1;
    }
    return 0;
}

static int pl_find_ffmpeg(char *out, size_t cap) { return pl_find_tool("R3I6NOS_FFMPEG", "ffmpeg", out, cap); }
static int pl_find_nttplay(char *out, size_t cap) { return pl_find_tool("R3I6NOS_NTTPLAY", "nttplay", out, cap); }

/* Starts argv with its standard output on a non-blocking pipe (standard error to nowhere). 0 or -1. */
static int pl_spawn(char *const argv[], int *fd, pid_t *pid) {
    int fds[2];
    if (pipe(fds) != 0) return -1;
    pid_t c = fork();
    if (c < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (c == 0) {
        dup2(fds[1], 1);
        int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) { dup2(nul, 2); dup2(nul, 0); }
        for (int i = 3; i < 64; i++) close(i);
        execv(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
    *fd = fds[0];
    *pid = c;
    return 0;
}

static const char *pl_wav_data(Player *p, FILE *f, long *left, int *rate, int *chans, int *bits) {
    unsigned char h[12], c[8];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return "not a WAV file";
    int fmt = 0;
    for (int guard = 0; guard < 200 && fread(c, 1, 8, f) == 8; guard++) {
        unsigned long len = c[4] | (c[5] << 8) | (c[6] << 16) | ((unsigned long)c[7] << 24);
        if (!memcmp(c, "fmt ", 4) && len >= 16) {
            unsigned char d[16];
            if (fread(d, 1, 16, f) != 16) return "damaged WAV header";
            fmt = d[0] | (d[1] << 8);
            *chans = d[2] | (d[3] << 8);
            *rate = (int)(d[4] | (d[5] << 8) | (d[6] << 16) | ((unsigned long)d[7] << 24));
            *bits = d[14] | (d[15] << 8);
            fseek(f, (long)(len - 16 + (len & 1)), SEEK_CUR);
        } else if (!memcmp(c, "data", 4)) {
            if (fmt != 1 && fmt != 0xFFFE) return "not PCM sound";
            if ((*bits != 8 && *bits != 16) || (*chans != 1 && *chans != 2) || *rate < 8000 || *rate > 192000) return "only 8 and 16 bit mono or stereo PCM plays";
            long here = ftell(f), end;
            fseek(f, 0, SEEK_END);
            end = ftell(f);
            fseek(f, here, SEEK_SET);
            *left = (len == 0xFFFFFFFFul || (long)len > end - here) ? end - here : (long)len;
            (void)p;
            return NULL;
        } else {
            fseek(f, (long)(len + (len & 1)), SEEK_CUR);
        }
    }
    return "no sound data in the WAV";
}

/* Opens the sound device for the stream just described. 0 when open; -1 leaves the note and plays silently. */
static int pl_audio_open(Player *p, int rate, int chans, int bits) {
    p->rate = rate;
    p->chans = chans;
    p->bps = chans * (bits / 8);
    const char *off = getenv("R3I6NOS_FM_NOAUDIO");
    if (off && *off == '1') return -1;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { pl_note(p, "No sound: %s", SDL_GetError()); return -1; }
    SDL_AudioSpec want, got;
    SDL_zero(want);
    want.freq = rate;
    want.format = bits == 8 ? AUDIO_U8 : AUDIO_S16SYS;
    want.channels = (Uint8)chans;
    want.samples = 2048;
    p->dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);   /* no changes allowed: SDL converts to what the device takes */
    if (!p->dev) { pl_note(p, "No sound: %s", SDL_GetError()); return -1; }
    SDL_PauseAudioDevice(p->dev, 1);
    return 0;
}

/* Reads up to n bytes of sound: >0 bytes, 0 nothing yet, -1 the end. */
static int pl_audio_read(Player *p, unsigned char *buf, int n) {
    if (p->wav) {
        if (p->wav_left <= 0) return -1;
        size_t got = fread(buf, 1, (size_t)(n < p->wav_left ? n : p->wav_left), p->wav);
        if (got == 0) return -1;
        p->wav_left -= (long)got;
        return (int)got;
    }
    if (p->afd >= 0) {
        ssize_t got = read(p->afd, buf, (size_t)n);
        if (got > 0) return (int)got;
        if (got < 0 && (errno == EAGAIN || errno == EINTR)) return 0;
        return -1;
    }
    return -1;
}

/* Keeps about half a second of sound queued; the first fill starts the device. */
static void pl_audio_feed(Player *p) {
    if (p->a_eof) return;
    if (!p->dev) {                  /* silent: read the pipe and drop it, so ffmpeg is not left blocked and its end is seen */
        unsigned char junk[16384];
        for (int guard = 0; guard < 8 && p->afd >= 0; guard++) {
            int n = pl_audio_read(p, junk, (int)sizeof junk);
            if (n < 0) p->a_eof = 1;
            if (n <= 0) break;
        }
        return;
    }
    unsigned char buf[16384];
    unsigned want = (unsigned)p->rate * (unsigned)p->bps / 2;
    for (int guard = 0; guard < 8 && SDL_GetQueuedAudioSize(p->dev) < want; guard++) {
        int n = pl_audio_read(p, buf, (int)(sizeof buf - sizeof buf % (size_t)p->bps));
        if (n < 0) { p->a_eof = 1; break; }
        if (n == 0) break;
        if (SDL_QueueAudio(p->dev, buf, (Uint32)n) != 0) { p->a_eof = 1; break; }
        p->fed += (unsigned)n;
    }
    if (!p->primed && (p->fed >= want / 2 || p->a_eof)) { p->primed = 1; if (p->playing) SDL_PauseAudioDevice(p->dev, 0); }
}

/* Draws the 640x480 render of `cur` into px at 5/6 size (533 x 400, centred, black either side). */
static void pl_draw_cells(Player *p) {
    r3_render(&p->cur, p->full);
    int ow = PL_W * PL_H / R3_H;
    int x0 = (PL_W - ow) / 2;
    memset(p->px, 0, PL_FRAME_BYTES);
    for (int y = 0; y < PL_H; y++) {
        const uint32_t *src = p->full + (size_t)(y * R3_H / PL_H) * R3_W;
        uint32_t *dst = p->px + (size_t)y * PL_W + x0;
        for (int x = 0; x < ow; x++) dst[x] = src[x * R3_W / ow];
    }
}

static int pl_r3f_next(Player *p) {
    uint32_t run;
    if (p->rec_left <= 0 || fread(&run, sizeof run, 1, p->r3f) != 1 || fread(&p->cur, sizeof p->cur, 1, p->r3f) != 1) { p->rec_left = 0; return 0; }
    p->rec_left--;
    p->frame_end += run ? (long)run : 1;
    pl_draw_cells(p);
    return 1;
}

/* The sound file beside an r3f: demo-<stamp>-frames.r3f -> demo-<stamp>.wav. */
static int pl_sibling_wav(const char *r3f, char *out, size_t cap) {
    size_t n = strlen(r3f);
    if (n < 11 || strcmp(r3f + n - 11, "-frames.r3f")) return 0;
    snprintf(out, cap, "%.*s.wav", (int)(n - 11), r3f);
    return access(out, R_OK) == 0;
}

static int pl_open_wav(Player *p, const char *path, const char **why) {
    p->wav = fopen(path, "rb");
    if (!p->wav) { *why = "the file could not be opened"; return -1; }
    int rate = 0, chans = 0, bits = 0;
    *why = pl_wav_data(p, p->wav, &p->wav_left, &rate, &chans, &bits);
    if (*why) return -1;
    p->has_audio = 1;
    p->total = (double)p->wav_left / ((double)rate * chans * (bits / 8));
    pl_audio_open(p, rate, chans, bits);
    return 0;
}

static int pl_ffmpeg_start(Player *p, const char *path, int video, const char **why) {
    char ff[PATH_MAX];
    if (!pl_find_ffmpeg(ff, sizeof ff)) { *why = "needs ffmpeg, not installed here"; return -1; }
    char *vf = "scale=640:400:force_original_aspect_ratio=decrease:flags=neighbor,pad=640:400:(ow-iw)/2:(oh-ih)/2,fps=30";
    if (video) {
        char *argv[] = {ff, "-v", "error", "-i", (char *)path, "-an", "-vf", vf, "-f", "rawvideo", "-pix_fmt", "bgra", "pipe:1", NULL};
        if (pl_spawn(argv, &p->vfd, &p->vpid) != 0) { *why = "ffmpeg would not start"; return -1; }
        p->vbuf = (unsigned char *)malloc(PL_FRAME_BYTES);
        p->has_video = 1;
        if (!p->vbuf) { *why = "out of memory"; return -1; }
    }
    char *argv2[] = {ff, "-v", "error", "-i", (char *)path, "-vn", "-f", "s16le", "-ar", "44100", "-ac", "2", "pipe:1", NULL};
    if (pl_spawn(argv2, &p->afd, &p->apid) != 0) { *why = "ffmpeg would not start"; return -1; }
    p->has_audio = 1;
    pl_audio_open(p, 44100, 2, 16);
    return 0;
}

/* `nttplay probe FILE`, waited for (at most 10 s: it only reads the file): 0 and `*total` the length in seconds (0 when unknown), or -1 and `why` (nttplay's own
 * one-line reason, "nttplay: " and the file name dropped). */
static int pl_ntt_probe(const char *nt, const char *path, double *total, char *why, size_t cap) {
    int fds[2];
    *total = 0;
    if (pipe(fds) != 0) { snprintf(why, cap, "nttplay would not start"); return -1; }
    pid_t c = fork();
    if (c < 0) { close(fds[0]); close(fds[1]); snprintf(why, cap, "nttplay would not start"); return -1; }
    if (c == 0) {
        dup2(fds[1], 1);
        dup2(fds[1], 2);                     /* its one-line complaint comes back on the same pipe */
        int nul = open("/dev/null", O_RDONLY);
        if (nul >= 0) dup2(nul, 0);
        for (int i = 3; i < 64; i++) close(i);
        char *argv[] = {(char *)nt, "probe", (char *)path, NULL};
        execv(nt, argv);
        _exit(127);
    }
    close(fds[1]);
    fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
    char out[4096];
    size_t n = 0;
    double t0 = (double)SDL_GetTicks();
    int timed_out = 0;
    for (;;) {
        ssize_t got = n < sizeof out - 1 ? read(fds[0], out + n, sizeof out - 1 - n) : 0;
        if (got > 0) { n += (size_t)got; continue; }
        if (got == 0) break;                                  /* the end, or a full buffer: enough either way */
        if (errno != EAGAIN && errno != EINTR) break;
        if ((double)SDL_GetTicks() - t0 > 10000.0) { timed_out = 1; break; }
        usleep(2000);
    }
    close(fds[0]);
    if (timed_out) kill(c, SIGKILL);
    int st = 0;
    waitpid(c, &st, 0);
    out[n] = 0;
    if (timed_out) { snprintf(why, cap, "nttplay took too long to read it"); return -1; }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        char *m = out;
        if (!strncmp(m, "nttplay: ", 9)) m += 9;
        size_t pn = strlen(path);
        if (!strncmp(m, path, pn) && m[pn] == ':' && m[pn + 1] == ' ') m += pn + 2;
        m[strcspn(m, "\r\n")] = 0;
        snprintf(why, cap, "%s", *m ? m : "nttplay could not read it");
        return -1;
    }
    const char *d = strstr(out, "duration=");
    if (d && (d == out || d[-1] == '\n')) *total = strtod(d + 9, NULL);
    if (!(*total >= 0 && *total < 1e7)) *total = 0;
    return 0;
}

static int pl_ntt_start(Player *p, const char *path, const char **why) {
    static char reason[200];
    char nt[PATH_MAX];
    if (!pl_find_nttplay(nt, sizeof nt)) { *why = "needs nttplay, not installed here"; return -1; }
    if (pl_ntt_probe(nt, path, &p->total, reason, sizeof reason) != 0) { *why = reason; return -1; }
    char *argv[] = {nt, "decode", (char *)path, "-ar", "44100", NULL};
    if (pl_spawn(argv, &p->afd, &p->apid) != 0) { *why = "nttplay would not start"; return -1; }
    p->has_audio = 1;
    pl_audio_open(p, 44100, 2, 16);
    return 0;
}

/* Opens `path` for playing as `mode`. 0 and it plays at once (`now_ms` the caller's clock); -1 and `*why` says why not. */
static int pl_open(Player *p, const char *path, int mode, double now_ms, const char **why) {
    pl_close(p);
    pl_zero(p);
    p->mode = mode;
    snprintf(p->path, sizeof p->path, "%s", path);
    p->px = (uint32_t *)calloc(1, PL_FRAME_BYTES);
    if (!p->px) { *why = "out of memory"; return -1; }
    int rc = -1;
    if (mode == PL_WAV) rc = pl_open_wav(p, path, why);
    else if (mode == PL_NTT) rc = pl_ntt_start(p, path, why);
    else if (mode == PL_VIDEO || mode == PL_AUDIO) rc = pl_ffmpeg_start(p, path, mode == PL_VIDEO, why);
    else if (mode == PL_R3F) {
        p->r3f = fopen(path, "rb");
        p->full = (uint32_t *)malloc((size_t)R3_W * R3_H * 4);
        if (!p->r3f || !p->full) { *why = "the file could not be opened"; rc = -1; }
        else {
            const size_t rec = sizeof(uint32_t) + sizeof(R3Screen);
            fseek(p->r3f, 0, SEEK_END);
            long size = ftell(p->r3f);
            rewind(p->r3f);
            p->rec_left = size / (long)rec;
            if (size % (long)rec || p->rec_left == 0) { *why = "not a whole number of recorded screens"; rc = -1; }
            else {
                long frames = 0;
                uint32_t run;
                for (long i = 0; i < p->rec_left; i++) {      /* its length: add the runs, skipping the screens */
                    if (fseek(p->r3f, i * (long)rec, SEEK_SET) != 0 || fread(&run, sizeof run, 1, p->r3f) != 1) break;
                    frames += run ? (long)run : 1;
                }
                rewind(p->r3f);
                p->total = (double)frames / PL_FPS;
                p->has_video = 1;
                char wav[PATH_MAX + 64];
                if (pl_sibling_wav(path, wav, sizeof wav)) {
                    snprintf(p->wavpath, sizeof p->wavpath, "%s", wav);
                    p->wav = fopen(wav, "rb");
                    int rate = 0, chans = 0, bits = 0;
                    const char *w = p->wav ? pl_wav_data(p, p->wav, &p->wav_left, &rate, &chans, &bits) : "could not be opened";
                    if (w) { pl_note(p, "No sound: the .wav %s", w); if (p->wav) { fclose(p->wav); p->wav = NULL; } }
                    else { p->has_audio = 1; pl_audio_open(p, rate, chans, bits); }
                } else {
                    pl_note(p, "No sound: the matching .wav is not beside the frames");
                }
                pl_r3f_next(p);
                rc = 0;
            }
        }
    }
    if (rc != 0) { pl_close(p); return -1; }
    p->playing = 1;
    p->last_ms = now_ms;
    if (p->dev) { pl_audio_feed(p); }
    return 0;
}

static void pl_pause(Player *p, int on) {
    if (p->ended) return;
    p->playing = !on;
    if (p->dev && p->primed) SDL_PauseAudioDevice(p->dev, on ? 1 : 0);
}

/* The seconds played: the sound's own count when there is sound, else the caller's clock. */
static void pl_clock(Player *p, double now_ms) {
    if (p->dev && p->primed) {
        double heard = ((double)p->fed - (double)SDL_GetQueuedAudioSize(p->dev)) / ((double)p->rate * p->bps);
        if (heard > p->now) p->now = heard;
    } else if (p->playing && !(p->has_audio && p->dev)) {
        p->now += (now_ms - p->last_ms) / 1000.0;
    }
    p->last_ms = now_ms;
}

/* Advances: feeds the sound, takes the frames that are due. Returns 1 when the picture or the position changed (the caller redraws). */
static int pl_pump(Player *p, double now_ms) {
    if (!p->mode || p->ended) return 0;
    if (!p->playing) { p->last_ms = now_ms; return 0; }
    double before = p->now;
    int changed = 0;
    pl_audio_feed(p);
    pl_clock(p, now_ms);
    if (p->mode == PL_R3F) {
        long f = (long)(p->now * PL_FPS);
        while (f >= p->frame_end && p->rec_left > 0) changed |= pl_r3f_next(p);
        if (f >= p->frame_end && p->rec_left <= 0) p->ended = 1;
    } else if (p->has_video) {
        for (int guard = 0; guard < 16 && !p->v_eof; guard++) {
            if (p->vgot < PL_FRAME_BYTES) {
                ssize_t n = read(p->vfd, p->vbuf + p->vgot, PL_FRAME_BYTES - p->vgot);
                if (n > 0) p->vgot += (size_t)n;
                else if (n == 0) { p->v_eof = 1; break; }
                else if (errno != EAGAIN && errno != EINTR) { p->v_eof = 1; break; }
                else break;
            }
            if (p->vgot < PL_FRAME_BYTES) break;
            if ((double)p->vidx / PL_FPS > p->now) break;    /* not due yet: hold it */
            memcpy(p->px, p->vbuf, PL_FRAME_BYTES);
            p->vgot = 0;
            p->vidx++;
            changed = 1;
        }
        if (p->v_eof && (!p->has_audio || !p->dev || p->a_eof) && (!p->dev || SDL_GetQueuedAudioSize(p->dev) == 0)) p->ended = 1;
    }
    if (!p->has_video || p->mode == PL_WAV) {   /* sound only: done when the sound is */
        if (p->a_eof && (!p->dev || SDL_GetQueuedAudioSize(p->dev) == 0)) p->ended = 1;
        else if (!p->dev && p->total > 0 && p->now >= p->total) p->ended = 1;
    }
    if (p->mode == PL_R3F && p->has_audio && !p->dev && p->now >= p->total) p->ended = 1;
    if (p->ended) { if (p->total > 0) p->now = p->total; if (p->dev) SDL_PauseAudioDevice(p->dev, 1); changed = 1; }
    return changed || (int)p->now != (int)before;
}

/* A still picture (WebP, TIFF, PSD) decoded by ffmpeg to `w` x `h` BGRA, which is also ARGB in a uint32: the first frame at its own size. The size comes
 * from the file's own header (Info), so exactly w*h*4 bytes are read; a stalled ffmpeg is given up on after 15 s. 0 and `px` (w*h) filled, else -1 and `*why`. */
static int pl_still(const char *path, int w, int h, uint32_t *px, const char **why) {
    char ff[PATH_MAX];
    if (!pl_find_ffmpeg(ff, sizeof ff)) { *why = "needs ffmpeg, not installed here"; return -1; }
    char *argv[] = {ff, "-v", "error", "-noautorotate", "-i", (char *)path, "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "bgra", "pipe:1", NULL};
    int fd = -1;
    pid_t pid = 0;
    if (pl_spawn(argv, &fd, &pid) != 0) { *why = "ffmpeg would not start"; return -1; }
    size_t want = (size_t)w * h * 4, got = 0;
    unsigned char *dst = (unsigned char *)px;
    double t0 = (double)SDL_GetTicks();
    *why = NULL;
    while (got < want) {
        ssize_t n = read(fd, dst + got, want - got);
        if (n > 0) { got += (size_t)n; continue; }
        if (n == 0) break;
        if (errno != EAGAIN && errno != EINTR) break;
        if ((double)SDL_GetTicks() - t0 > 15000.0) { *why = "ffmpeg took too long"; break; }
        usleep(5000);
    }
    pl_stop_child(&fd, &pid);
    if (got != want && !*why) *why = "ffmpeg could not decode it";
    return got == want ? 0 : -1;
}

/* "1:05" */
static void pl_time(char *out, size_t cap, double s) {
    int t = s < 0 ? 0 : (int)s;
    snprintf(out, cap, "%d:%02d", t / 60, t % 60);
}

#endif /* COMMANDER_PLAYER_H */

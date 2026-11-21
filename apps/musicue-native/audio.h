/* audio.h: MusiCue's sound (header-only, MIT). A whole track lives in RAM as 16-bit stereo at 44.1 kHz, so a jump to a marker is a pointer move and an
 * A/B loop wraps with no gap (the audio callback does the wrap inside the buffer it is filling).
 *
 *   track_load   a PCM WAV (8 or 16 bit, mono or stereo, any rate: resampled linearly) is read here, with no other program; anything else (m4a, mp3, ogg, flac,
 *                opus ...) is decoded by `ffmpeg` (R3I6NOS_FFMPEG names another) on a pipe: -vn -f s16le -ar 44100 -ac 2. Nothing is decoded by anything built in
 *                (the image's one ffmpeg does it, as for Commander's Play tab; docs/STREAM-TOOLS.md). A track is cut at TRACK_MAX_SECONDS and says so.
 *   Engine       play, pause, seek, an A/B loop, the position in seconds. SDL's audio callback feeds the device; R3I6NOS_MUSICUE_NOAUDIO=1 (or a device that will
 *                not open) runs the same logic on the caller's clock (engine_tick), which is also what the tests use.
 *
 * 44.1 kHz stereo 16-bit is 176,400 bytes a second: a 4-minute song is 42 MB, 12 minutes 127 MB, on a 1 GB unit. Include SDL.h first. Every figure about
 * how long a decode takes on the unit is untested.
 *
 * The Engine also holds a few synth voices for the Piano panel (engine_note_on / engine_note_off): a soft sine with two overtones and a decay, mixed over the
 * song in the same callback, so a person can find a song's key by ear while it plays. */
#ifndef MUSICUE_AUDIO_H
#define MUSICUE_AUDIO_H

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define TRACK_RATE 44100
#define TRACK_MAX_SECONDS (12 * 60)

typedef struct {
    int16_t *pcm;               /* frames x 2 (left, right) */
    int64_t frames;
    int truncated;              /* the file was longer than TRACK_MAX_SECONDS */
    char via[16];               /* "wav" or "ffmpeg" */
} Track;

static void track_free(Track *t) { free(t->pcm); memset(t, 0, sizeof *t); }

static double track_seconds(const Track *t) { return t->pcm ? (double)t->frames / TRACK_RATE : 0.0; }

typedef struct { int16_t *pcm; int64_t frames, cap; } PcmBuf;

static int pcm_reserve(PcmBuf *b, int64_t frames) {
    if (frames <= b->cap) return 1;
    int64_t nc = b->cap ? b->cap : 1 << 16;
    while (nc < frames) nc *= 2;
    int16_t *np = (int16_t *)realloc(b->pcm, (size_t)nc * 4);
    if (!np) return 0;
    b->pcm = np;
    b->cap = nc;
    return 1;
}

static uint32_t rd32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* A PCM WAV into the track. 1 loaded; 0 not a WAV this reads (the caller tries ffmpeg); -1 out of memory or unreadable. */
static int track_load_wav(Track *t, const char *path, double max_seconds, char *err, size_t errsz) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errsz, "cannot open: %s", strerror(errno)); return -1; }
    unsigned char h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(f); return 0; }
    int chans = 0, bits = 0, rate = 0, fmt = 0, have_fmt = 0;
    for (;;) {
        unsigned char ch[8];
        if (fread(ch, 1, 8, f) != 8) { fclose(f); return 0; }
        uint32_t len = rd32(ch + 4);
        if (!memcmp(ch, "fmt ", 4) && len >= 16) {
            unsigned char fm[16];
            if (fread(fm, 1, 16, f) != 16) { fclose(f); return 0; }
            fmt = rd16(fm); chans = rd16(fm + 2); rate = (int)rd32(fm + 4); bits = rd16(fm + 14);
            have_fmt = 1;
            if (len > 16 && fseek(f, (long)(len - 16 + (len & 1)), SEEK_CUR) != 0) { fclose(f); return 0; }
        } else if (!memcmp(ch, "data", 4)) {
            if (!have_fmt || fmt != 1 || (bits != 8 && bits != 16) || chans < 1 || chans > 2 || rate < 8000 || rate > 192000) { fclose(f); return 0; }
            int bps = bits / 8 * chans;
            int64_t src_frames = len / (uint32_t)bps;
            if (len == 0 || len == 0xFFFFFFFFu) {              /* a streamed WAV: the length is not known, take the file's end */
                long here = ftell(f);
                fseek(f, 0, SEEK_END);
                src_frames = (ftell(f) - here) / bps;
                fseek(f, here, SEEK_SET);
            }
            int64_t out_frames = (int64_t)((double)src_frames * TRACK_RATE / rate);
            int64_t cap_frames = (int64_t)(max_seconds * TRACK_RATE);
            if (out_frames > cap_frames) { out_frames = cap_frames; t->truncated = 1; src_frames = (int64_t)((double)cap_frames * rate / TRACK_RATE) + 2; }
            if (out_frames < 1) { fclose(f); return 0; }
            unsigned char *raw = (unsigned char *)malloc((size_t)src_frames * (size_t)bps);
            int16_t *out = (int16_t *)malloc((size_t)out_frames * 4);
            if (!raw || !out) { free(raw); free(out); fclose(f); snprintf(err, errsz, "not enough memory"); return -1; }
            size_t got = fread(raw, (size_t)bps, (size_t)src_frames, f);
            fclose(f);
            if (got == 0) { free(raw); free(out); return 0; }
            src_frames = (int64_t)got;
            for (int64_t i = 0; i < out_frames; i++) {
                double pos = (double)i * rate / TRACK_RATE;
                int64_t i0 = (int64_t)pos;
                double fr = pos - (double)i0;
                if (i0 >= src_frames - 1) { i0 = src_frames - 1; fr = 0; }
                int64_t i1 = i0 + 1 < src_frames ? i0 + 1 : i0;
                for (int c = 0; c < 2; c++) {
                    int cc = chans == 1 ? 0 : c;
                    double a, b;
                    if (bits == 16) {
                        a = (int16_t)rd16(raw + (size_t)i0 * (size_t)bps + (size_t)cc * 2);
                        b = (int16_t)rd16(raw + (size_t)i1 * (size_t)bps + (size_t)cc * 2);
                    } else {
                        a = (raw[(size_t)i0 * (size_t)bps + (size_t)cc] - 128) * 256.0;
                        b = (raw[(size_t)i1 * (size_t)bps + (size_t)cc] - 128) * 256.0;
                    }
                    out[i * 2 + c] = (int16_t)lrint(a + (b - a) * fr);
                }
            }
            free(raw);
            t->pcm = out;
            t->frames = out_frames;
            snprintf(t->via, sizeof t->via, "wav");
            return 1;
        } else if (fseek(f, (long)(len + (len & 1)), SEEK_CUR) != 0) { fclose(f); return 0; }
    }
}

/* Called about five times a second while a long decode runs, with the seconds decoded so far and the song's length when ffmpeg said it
 * (its "Duration:" line, cut at the cap; 0 when it did not: the caller then shows a moving indicator, CLAUDE.md rule 18); return nonzero to stop. */
typedef int (*TrackProgress)(void *ud, double seconds_so_far, double total_seconds);

static double track_now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (double)ts.tv_sec + ts.tv_nsec / 1e9; }

/* "  Duration: 00:03:45.12, start: ..." in ffmpeg's banner: the seconds, or 0. */
static double track_parse_duration(const char *text) {
    const char *d = strstr(text, "Duration: ");
    int h, m;
    double s;
    if (d && sscanf(d + 10, "%d:%d:%lf", &h, &m, &s) == 3 && h >= 0 && m >= 0 && s >= 0) return h * 3600.0 + m * 60.0 + s;
    return 0;
}

/* Anything else, through ffmpeg. 1 loaded, 0 failed (err says why). */
static int track_load_ffmpeg(Track *t, const char *path, double max_seconds, TrackProgress prog, void *ud, char *err, size_t errsz) {
    const char *ff = getenv("R3I6NOS_FFMPEG") && *getenv("R3I6NOS_FFMPEG") ? getenv("R3I6NOS_FFMPEG") : "ffmpeg";
    int fd[2], ef[2];
    if (pipe(fd) != 0) { snprintf(err, errsz, "no pipe"); return 0; }
    if (pipe(ef) != 0) { close(fd[0]); close(fd[1]); snprintf(err, errsz, "no pipe"); return 0; }
    pid_t pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); close(ef[0]); close(ef[1]); snprintf(err, errsz, "cannot start ffmpeg"); return 0; }
    if (pid == 0) {
        dup2(fd[1], 1);
        dup2(ef[1], 2);                         /* the banner's Duration line: the bar's total */
        int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) dup2(nul, 0);
        close(fd[0]); close(fd[1]); close(ef[0]); close(ef[1]);
        execlp(ff, ff, "-v", "info", "-hide_banner", "-nostats", "-nostdin", "-i", path, "-vn", "-f", "s16le", "-ar", "44100", "-ac", "2", "pipe:1", (char *)NULL);
        _exit(127);
    }
    close(fd[1]);
    close(ef[1]);
    fcntl(ef[0], F_SETFL, fcntl(ef[0], F_GETFL) | O_NONBLOCK);
    char banner[2048];
    size_t nbanner = 0;
    double total = 0, last_call = track_now_s();
    PcmBuf b = {0};
    int64_t cap_frames = (int64_t)(max_seconds * TRACK_RATE);
    unsigned char chunk[65536], carry[4];
    int ncarry = 0, stopped = 0;
    for (;;) {
        ssize_t got = read(fd[0], chunk, sizeof chunk);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        size_t off = 0, n = (size_t)got;
        while (ncarry && ncarry < 4 && off < n) carry[ncarry++] = chunk[off++];
        if (ncarry == 4) {
            if (!pcm_reserve(&b, b.frames + 1)) { stopped = 2; break; }
            memcpy(b.pcm + b.frames * 2, carry, 4);
            b.frames++;
            ncarry = 0;
        }
        int64_t whole = (int64_t)((n - off) / 4);
        if (b.frames + whole > cap_frames) { whole = cap_frames - b.frames; t->truncated = 1; stopped = 1; }
        if (whole > 0) {
            if (!pcm_reserve(&b, b.frames + whole)) { stopped = 2; break; }
            memcpy(b.pcm + b.frames * 2, chunk + off, (size_t)whole * 4);
            b.frames += whole;
            off += (size_t)whole * 4;
        }
        if (stopped) break;
        ncarry = 0;
        while (off < n) carry[ncarry++] = chunk[off++];
        for (;;) {                              /* drain ffmpeg's messages (it must never block on them), and read the length once */
            char eb[512];
            ssize_t en = read(ef[0], eb, sizeof eb);
            if (en <= 0) break;
            if (total <= 0 && nbanner < sizeof banner - 1) {
                size_t take = (size_t)en < sizeof banner - 1 - nbanner ? (size_t)en : sizeof banner - 1 - nbanner;
                memcpy(banner + nbanner, eb, take);
                banner[nbanner += take] = 0;
                total = track_parse_duration(banner);
                if (total > max_seconds) total = max_seconds;
            }
        }
        if (prog && track_now_s() - last_call >= 0.2) {
            last_call = track_now_s();
            if (prog(ud, (double)b.frames / TRACK_RATE, total)) { stopped = 3; break; }
        }
    }
    close(fd[0]);
    close(ef[0]);
    if (stopped) kill(pid, SIGKILL);
    int status = 0;
    waitpid(pid, &status, 0);
    if (stopped == 2) { free(b.pcm); snprintf(err, errsz, "not enough memory"); return 0; }
    if (stopped == 3) { free(b.pcm); snprintf(err, errsz, "cancelled"); return 0; }
    if (b.frames < 1) {
        free(b.pcm);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 127) snprintf(err, errsz, "ffmpeg is not installed");
        else snprintf(err, errsz, "ffmpeg could not decode it");
        return 0;
    }
    t->pcm = b.pcm;
    t->frames = b.frames;
    snprintf(t->via, sizeof t->via, "ffmpeg");
    return 1;
}

static int track_load(Track *t, const char *path, double max_seconds, TrackProgress prog, void *ud, char *err, size_t errsz) {
    memset(t, 0, sizeof *t);
    err[0] = 0;
    int w = track_load_wav(t, path, max_seconds, err, errsz);
    if (w == 1) return 1;
    if (w < 0) return 0;
    return track_load_ffmpeg(t, path, max_seconds, prog, ud, err, errsz);
}

/* ---- the engine ------------------------------------------------------------------------------------------------------------------------ */

#define VOICES 12
typedef struct { int active, held, key; double freq, phase, age, rel; } Voice;      /* a piano note: key 0-11 within the octave shown, age and release time in seconds */

typedef struct {
    const Track *t;
    SDL_AudioDeviceID dev;
    int noaudio;
    int asleep;                         /* the sound device is closed while nothing plays (an open one wakes about 40 times a second feeding silence, and keeps the amplifier on) */
    volatile int playing, ended;
    volatile int64_t pos;               /* frame to play next */
    volatile int loop;                  /* 1: wrap from loop_b back to loop_a */
    volatile int64_t loop_a, loop_b;    /* frames; a < b when the loop is set */
    double carry;                       /* frames not yet whole, on the caller's clock */
    int stepped;                        /* Rec demo: the caller renders the sound itself (engine_render) on a stepped clock, so engine_tick must not move the position */
    Voice voice[VOICES];                /* the Piano panel's notes, mixed over the song */
} Engine;

/* Add the sounding voices to out (frames x 2, 16-bit), and drop the ones that have faded. */
static void engine_mix_voices(Engine *e, int16_t *out, int64_t frames) {
    for (int v = 0; v < VOICES; v++) {
        Voice *p = &e->voice[v];
        if (!p->active) continue;
        const double dt = 1.0 / TRACK_RATE, w = 2 * M_PI * p->freq;
        for (int64_t i = 0; i < frames; i++) {
            double att = p->age < 0.004 ? p->age / 0.004 : 1.0;
            double env = att * exp(-p->age / 1.1);
            if (!p->held) env *= exp(-(p->age - p->rel) / 0.05);
            double ph = p->phase;
            double smp = (sin(ph) + 0.35 * sin(2 * ph) + 0.12 * sin(3 * ph)) * env * 7000.0;
            p->phase += w * dt;
            if (p->phase > 2 * M_PI * 64) p->phase = fmod(p->phase, 2 * M_PI);
            p->age += dt;
            for (int c = 0; c < 2; c++) {
                int x = out[i * 2 + c] + (int)smp;
                out[i * 2 + c] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
            }
            if ((p->age > 0.02 && env < 0.0008) || p->age > 8.0) { p->active = 0; break; }
        }
    }
}

/* Move the position on by up to `frames`, copying them to out when it is not NULL, wrapping an A/B loop with no gap. Returns the frames moved. */
static int64_t engine_step(Engine *e, int16_t *out, int64_t frames) {
    int64_t done = 0;
    while (done < frames) {
        int64_t pos = e->pos, limit = e->t->frames;
        if (pos >= limit) break;
        if (e->loop && e->loop_b > e->loop_a && pos < e->loop_b) limit = e->loop_b;
        int64_t n = limit - pos;
        if (n > frames - done) n = frames - done;
        if (out) memcpy(out + done * 2, e->t->pcm + pos * 2, (size_t)n * 4);
        done += n;
        pos += n;
        if (e->loop && e->loop_b > e->loop_a && pos >= e->loop_b) pos = e->loop_a;
        e->pos = pos;
    }
    return done;
}

/* `frames` of sound into out (16-bit stereo): the song, the A/B loop's wrap and the piano's voices, exactly what the device gets. The sound device's callback is this,
 * and so is Rec demo's tour on its stepped clock. */
static void engine_render(Engine *e, int16_t *out, int64_t frames) {
    int64_t done = 0;
    if (e->playing) done = engine_step(e, out, frames);
    if (done < frames) memset(out + done * 2, 0, (size_t)(frames - done) * 4);
    engine_mix_voices(e, out, frames);
    if (e->playing && e->pos >= e->t->frames) { e->playing = 0; e->ended = 1; }
}

static void engine_callback(void *ud, Uint8 *stream, int len) { engine_render((Engine *)ud, (int16_t *)stream, len / 4); }

/* Open the sound device (unless there is none or R3I6NOS_MUSICUE_NOAUDIO=1). The track must outlive the engine. */
/* Opens the sound device with the engine's own settings. 1 when it is open; 0 leaves noaudio set. */
static int engine_dev_open(Engine *e) {
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { e->noaudio = 1; return 0; }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = TRACK_RATE; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = engine_callback; want.userdata = e;
    e->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (e->dev == 0 || have.freq != TRACK_RATE || have.channels != 2 || have.format != AUDIO_S16SYS) {
        if (e->dev) SDL_CloseAudioDevice(e->dev);
        e->dev = 0;
        e->noaudio = 1;
        return 0;
    }
    SDL_PauseAudioDevice(e->dev, 0);
    return 1;
}

/* Open the sound device (unless there is none or R3I6NOS_MUSICUE_NOAUDIO=1). The track must outlive the engine. */
static void engine_open(Engine *e, const Track *t) {
    memset(e, 0, sizeof *e);
    e->t = t;
    e->loop_a = e->loop_b = 0;
    const char *na = getenv("R3I6NOS_MUSICUE_NOAUDIO");
    if (na && *na == '1') { e->noaudio = 1; return; }
    engine_dev_open(e);
}

/* Close the device while nothing plays and wake it when something does (docs/POWER.md: SDL's audio thread feeds an open, paused device silence about 40 times a
 * second and the amplifier stays powered). The position, the loop and the voices stay as they were. */
static void engine_sleep(Engine *e) {
    if (e->dev && !e->playing) { SDL_CloseAudioDevice(e->dev); e->dev = 0; e->asleep = 1; }
}
/* Something is sounding: the song plays or a piano voice is still ringing. */
static int engine_busy(const Engine *e) {
    if (e->playing) return 1;
    for (int v = 0; v < VOICES; v++) if (e->voice[v].active) return 1;
    return 0;
}
static void engine_wake(Engine *e) {
    if (e->asleep && !e->dev && !e->noaudio) engine_dev_open(e);
    e->asleep = 0;
}

static void engine_close(Engine *e) {
    if (e->dev) SDL_CloseAudioDevice(e->dev);
    e->dev = 0;
}

#define ENGINE_LOCK(e) do { if ((e)->dev) SDL_LockAudioDevice((e)->dev); } while (0)
#define ENGINE_UNLOCK(e) do { if ((e)->dev) SDL_UnlockAudioDevice((e)->dev); } while (0)

static double engine_pos_sec(const Engine *e) { return (double)e->pos / TRACK_RATE; }

static void engine_seek(Engine *e, double sec) {
    int64_t f = (int64_t)(sec * TRACK_RATE);
    if (f < 0) f = 0;
    if (f > e->t->frames) f = e->t->frames;
    ENGINE_LOCK(e);
    e->pos = f;
    e->ended = 0;
    ENGINE_UNLOCK(e);
}

static void engine_play(Engine *e, int on) {
    if (on) engine_wake(e);
    ENGINE_LOCK(e);
    if (on && e->pos >= e->t->frames) e->pos = 0;
    e->playing = on ? 1 : 0;
    if (on) e->ended = 0;
    ENGINE_UNLOCK(e);
}

/* Set the loop from seconds a to b (a < b, at least 50 ms apart) and switch it on; clear it with engine_loop_clear. Returns 1 if set. */
static int engine_loop_set(Engine *e, double a, double b) {
    int64_t fa = (int64_t)(a * TRACK_RATE), fb = (int64_t)(b * TRACK_RATE);
    if (fa < 0) fa = 0;
    if (fb > e->t->frames) fb = e->t->frames;
    if (fb - fa < TRACK_RATE / 20) return 0;
    ENGINE_LOCK(e);
    e->loop_a = fa; e->loop_b = fb; e->loop = 1;
    ENGINE_UNLOCK(e);
    return 1;
}

static void engine_loop_clear(Engine *e) {
    ENGINE_LOCK(e);
    e->loop = 0; e->loop_a = e->loop_b = 0;
    ENGINE_UNLOCK(e);
}

/* A piano note on: freq in Hz, key 0-11 names it so engine_note_off finds it (a held note decays slowly, a released one fades in 50 ms). */
static void engine_note_on(Engine *e, int key, double freq) {
    engine_wake(e);
    ENGINE_LOCK(e);
    int slot = -1;
    for (int v = 0; v < VOICES; v++) if (e->voice[v].active && e->voice[v].key == key) slot = v;       /* the same key again restarts its voice */
    for (int v = 0; v < VOICES && slot < 0; v++) if (!e->voice[v].active) slot = v;
    if (slot < 0) { slot = 0; for (int v = 1; v < VOICES; v++) if (e->voice[v].age > e->voice[slot].age) slot = v; }   /* all busy: the oldest goes */
    Voice *p = &e->voice[slot];
    p->active = 1; p->held = 1; p->key = key; p->freq = freq; p->phase = 0; p->age = 0; p->rel = 0;
    ENGINE_UNLOCK(e);
}

static void engine_note_off(Engine *e, int key) {
    ENGINE_LOCK(e);
    for (int v = 0; v < VOICES; v++) if (e->voice[v].active && e->voice[v].key == key && e->voice[v].held) { e->voice[v].held = 0; e->voice[v].rel = e->voice[v].age; }
    ENGINE_UNLOCK(e);
}

static void engine_notes_off(Engine *e) {
    ENGINE_LOCK(e);
    for (int v = 0; v < VOICES; v++) if (e->voice[v].active && e->voice[v].held) { e->voice[v].held = 0; e->voice[v].rel = e->voice[v].age; }
    ENGINE_UNLOCK(e);
}

/* On the caller's clock: advance by ms when there is no sound device. A no-op when a device is feeding the position. */
static void engine_tick(Engine *e, double ms) {
    if (!e->noaudio || !e->playing || e->stepped) return;
    e->carry += ms * TRACK_RATE / 1000.0;
    int64_t whole = (int64_t)e->carry;
    e->carry -= (double)whole;
    engine_step(e, NULL, whole);
    if (e->pos >= e->t->frames) { e->playing = 0; e->ended = 1; }
}

#endif /* MUSICUE_AUDIO_H */

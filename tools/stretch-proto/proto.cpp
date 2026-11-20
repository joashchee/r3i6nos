// proto.cpp: command line and self-test for stretch_render.hpp (MIT).
//   stretch-proto make-song OUT.wav SECS   the synthetic test song as a WAV
//   stretch-proto render IN.wav OUT.wav [--ratio R] [--semitones S] [--workers N] [--chunks N] [--cheaper] [--equal-power] [--no-quiet-seams]
//   stretch-proto selftest      the checks (length, pitch, seams, determinism); exit 1 on a failure
//   stretch-proto bench [SECS] [--cheaper] [--workers N]   the same synthetic song with 1, 2, 3 and 4 workers: wall time, CPU time, speed against real time
#include "stretch_render.hpp"
#include <cstdlib>
#include <sys/resource.h>
#include <unistd.h>

#ifdef SIGNALSMITH_USE_PFFFT
#define FFT_NAME "pffft"
#else
#define FFT_NAME "default"
#endif

using namespace sr;

static int g_fail = 0;
#define CHECK(c, ...) do { if (c) printf("PASS "); else { printf("FAIL "); g_fail++; } printf(__VA_ARGS__); printf("\n"); } while (0)

// A test song: a 440 Hz sine for the pitch and seam checks, or "music": a drifting chord, a bass line and a click on every beat (120 BPM).
static Pcm make_sine(double secs, double hz, int rate = 44100) {
    Pcm p; p.rate = rate; p.channels = 2;
    int64_t n = (int64_t)(secs * rate);
    p.ch.assign(2, std::vector<float>(n));
    for (int64_t i = 0; i < n; i++) { float v = 0.5f * (float)std::sin(2 * M_PI * hz * (double)i / rate); p.ch[0][i] = v; p.ch[1][i] = v; }
    return p;
}
static Pcm make_music(double secs, int rate = 44100) {
    Pcm p; p.rate = rate; p.channels = 2;
    int64_t n = (int64_t)(secs * rate);
    p.ch.assign(2, std::vector<float>(n));
    static const double chord[4][3] = {{220.0, 277.18, 329.63}, {246.94, 293.66, 369.99}, {196.0, 246.94, 293.66}, {174.61, 220.0, 261.63}};
    for (int64_t i = 0; i < n; i++) {
        double t = (double)i / rate, v = 0;
        const double *c = chord[((int64_t)(t / 2)) % 4];
        for (int k = 0; k < 3; k++) for (int h = 1; h <= 3; h++) v += std::sin(2 * M_PI * c[k] * h * t) / (h * 2.0);
        v = 0.12 * v + 0.2 * std::sin(2 * M_PI * c[0] / 2 * t) * (std::fmod(t, 0.5) < 0.4 ? 1.0 : 0.0);
        double b = std::fmod(t, 0.5);                                                 // a click each beat: a decaying burst of noise
        if (b < 0.03) v += 0.4 * std::exp(-b * 150.0) * ((double)((uint32_t)(i * 2654435761u) >> 16) / 32768.0 - 1.0);
        p.ch[0][i] = (float)v; p.ch[1][i] = (float)(v * 0.9);
    }
    return p;
}

// The frequency of a steady tone from the rising zero crossings in [from, to) frames.
static double tone_hz(const Pcm &p, int64_t from, int64_t to) {
    double first = -1, last = -1;
    int64_t cnt = 0;
    for (int64_t i = from + 1; i < to; i++) if (p.ch[0][i - 1] < 0 && p.ch[0][i] >= 0) {
        double f = (double)(i - 1) + (-p.ch[0][i - 1]) / (p.ch[0][i] - p.ch[0][i - 1]);
        if (first < 0) first = f;
        last = f;
        cnt++;
    }
    return cnt > 1 ? (double)(cnt - 1) * p.rate / (last - first) : 0;
}
static double rms(const Pcm &p, int64_t from, int64_t to) {
    double e = 0; for (int64_t i = from; i < to; i++) e += (double)p.ch[0][i] * p.ch[0][i];
    return std::sqrt(e / std::max<int64_t>(1, to - from));
}
static float max_step(const Pcm &p, int64_t from, int64_t to) {
    float m = 0; for (int64_t i = std::max<int64_t>(1, from); i < std::min(to, p.frames()); i++) m = std::max(m, std::fabs(p.ch[0][i] - p.ch[0][i - 1]));
    return m;
}

static void test_sine(double ratio, double semis, int workers) {
    const double hz = 440.0, secs = 40.0;
    Pcm in = make_sine(secs, hz);
    Job j; j.ratio = ratio; j.semitones = semis; j.workers = workers; j.chunks = 4;
    Stats st; Pcm o = render(in, j, &st);
    char name[96]; snprintf(name, sizeof name, "ratio %.2f, %+.1f st, %d workers, %d chunks", ratio, semis, workers, st.chunks);
    int64_t want = (int64_t)std::llround((double)in.frames() / ratio);
    CHECK(llabs(o.frames() - want) <= 2, "length %lld = %lld (%s)", (long long)o.frames(), (long long)want, name);
    double want_hz = hz * std::pow(2.0, semis / 12.0), got = tone_hz(o, o.rate * 2, o.rate * 6);
    CHECK(std::fabs(got - want_hz) / want_hz < 0.004, "pitch %.2f Hz, want %.2f (%s)", got, want_hz, name);
    // every cut: no click (the biggest sample step stays near a sine's own) and no dip (the level over the cut stays near the sine's 0.354)
    Job one = j; one.workers = 1; one.chunks = 1;                                     // the same render in one piece: the stretcher's own ripple
    Pcm ref = render(in, one);
    const float lim = max_step(ref, ref.rate, ref.frames() - ref.rate) * 1.15f + 0.002f;
    float worst = 0; double dip = 1e9;
    for (int64_t s : st.seams_out) {
        worst = std::max(worst, max_step(o, s - o.rate / 10, s + o.rate / 10));
        for (int64_t w = s - o.rate / 20; w < s + o.rate / 20; w += o.rate / 200) dip = std::min(dip, rms(o, w, w + o.rate / 100));
    }
    CHECK(worst <= lim, "no click at the %zu cuts: biggest step %.4f, limit %.4f = 1.15 x the one-piece render's (%s)", st.seams_out.size(), worst, lim, name);
    CHECK(dip >= 0.354 * 0.8, "no dip at the cuts: lowest 10 ms level %.3f of 0.354 (%s)", dip, name);
}

static void test_music() {
    Pcm in = make_music(60.0);
    Job one; one.ratio = 1.25; one.semitones = 2; one.workers = 1; one.chunks = 1;
    Job four = one; four.workers = 4; four.chunks = 4;
    Stats s1, s4; Pcm a = render(in, one, &s1), b = render(in, four, &s4), b2 = render(in, four, nullptr);
    CHECK(llabs(a.frames() - b.frames()) <= 1, "music: 1 chunk and 4 chunks are the same length (%lld, %lld)", (long long)a.frames(), (long long)b.frames());
    CHECK(a.frames() > 0 && b.frames() == b2.frames() && !memcmp(b.ch[0].data(), b2.ch[0].data(), b.ch[0].size() * sizeof(float)), "music: a second run gives the same bytes (fixed seeds, any thread order)");
    // the loudness over each 0.5 s must match the single render's (the cuts do not change the sound's level or timing)
    double worst = 0;
    for (int64_t i = 0; i + a.rate / 2 < std::min(a.frames(), b.frames()); i += a.rate / 2) {
        double ra = rms(a, i, i + a.rate / 2), rb = rms(b, i, i + a.rate / 2);
        worst = std::max(worst, std::fabs(ra - rb) / std::max(ra, 1e-6));
    }
    CHECK(worst < 0.25, "music: the level in every half second is within %.0f%% of the single render's (limit 25%%)", worst * 100);
}

static void test_wav() {
    Pcm p = make_sine(2.0, 440.0), q; std::string err;
    const char *tmp = getenv("TMPDIR");                           // the working folder may be read-only (the menu starts a report from anywhere)
    std::string path_s = std::string(tmp && *tmp ? tmp : "/tmp") + "/stretch-proto-test-" + std::to_string((long)getpid()) + ".wav";
    const char *path = path_s.c_str();
    if (!write_wav(path, p)) err = "cannot write " + path_s;
    int clip = -1;
    bool ok = write_wav(path, p, &clip) && read_wav(path, q, err);
    CHECK(ok && clip == 0 && q.frames() == p.frames() && q.channels == 2 && q.rate == 44100, "WAV round trip keeps the length, channels and rate (%s)", err.c_str());
    double worst = 0;
    for (int64_t i = 0; ok && i < p.frames(); i++) worst = std::max(worst, (double)std::fabs(p.ch[1][i] - q.ch[1][i]));
    CHECK(ok && worst < 1.0 / 32768, "WAV round trip is exact to 16 bits (worst %.6f)", worst);
    remove(path);
}

static long peak_kib() { struct rusage ru; getrusage(RUSAGE_SELF, &ru); return ru.ru_maxrss; }

// Times 1 to 4 workers on the same synthetic song. One line a worker count, in the device report's words:
//   RESULT fft=default preset=default workers=2 chunks=4 wall_s=1.23 cpu_s=2.31 x_realtime=48.7 speedup=1.9
static void bench(double secs, bool cheaper, int only) {
    Pcm in = make_music(secs);
    printf("INFO bench: %.0f s of 44.1 kHz stereo, ratio 0.80, +3 semitones, fft %s, preset %s, hardware threads %u\n", secs, FFT_NAME, cheaper ? "cheaper" : "default", std::thread::hardware_concurrency());
    printf("%7s %7s %9s %9s %12s %10s\n", "workers", "chunks", "wall s", "cpu s", "x real time", "speed-up");
    double base = 0;
    for (int w = 1; w <= 4; w++) {
        if (only && w != only && w != 1) continue;          // --workers N: one worker (the speed-up's base) and N
        Job j; j.ratio = 0.8; j.semitones = 3; j.workers = w; j.chunks = w == 3 ? 6 : 4; j.cheaper = cheaper;     // whole rounds of work: 4 segments on 3 workers would be two rounds, as slow as 2 workers
        Stats st; render(in, j, &st);
        if (w == 1) base = st.wall_s;
        printf("%7d %7d %9.2f %9.2f %12.1f %9.2fx\n", w, st.chunks, st.wall_s, st.cpu_s, st.audio_s / st.wall_s, base / st.wall_s);
        printf("RESULT fft=%s preset=%s workers=%d chunks=%d wall_s=%.3f cpu_s=%.3f assemble_s=%.3f align_s=%.3f x_realtime=%.2f speedup=%.2f\n", FFT_NAME, cheaper ? "cheaper" : "default", w, st.chunks, st.wall_s, st.cpu_s, st.assemble_s, st.align_s, st.audio_s / st.wall_s, base / st.wall_s);
    }
    printf("INFO peak memory: %ld KiB (the input, the pieces and the output of the last run)\n", peak_kib());
}

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "selftest")) {
        for (double r : {0.75, 1.0, 1.25, 1.5}) test_sine(r, r == 1.0 ? 3.0 : -2.0, 4);
        test_sine(0.8, 0.0, 1);
        test_music();
        test_wav();
        printf(g_fail ? "stretch-proto: %d FAILED\n" : "stretch-proto: all passed\n", g_fail);
        return g_fail ? 1 : 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "bench")) {
        double secs = 60; bool cheaper = false; int only = 0;
        for (int i = 2; i < argc; i++) { if (!strcmp(argv[i], "--cheaper")) cheaper = true; else if (!strcmp(argv[i], "--workers") && i + 1 < argc) only = atoi(argv[++i]); else secs = atof(argv[i]); }
        bench(secs, cheaper, only);
        return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "make-sine")) {            // make-sine OUT.wav SECS HZ
        return write_wav(argv[2], make_sine(atof(argv[3]), atof(argv[4]))) ? 0 : 1;
    }
    if (argc >= 5 && !strcmp(argv[1], "make-loud")) {            // make-loud OUT.wav SECS GAIN: the test song times GAIN, hard-clipped at the 16-bit limit as a loud master is
        Pcm q = make_music(atof(argv[3]));
        const float gn = (float)atof(argv[4]);
        for (auto &c : q.ch) for (auto &v : c) v = std::max(-1.f, std::min(1.f, v * gn));
        return write_wav(argv[2], q) ? 0 : 1;
    }
    if (argc >= 3 && !strcmp(argv[1], "info")) {                 // info FILE.wav: frames, rate, channels, and the tone's frequency over 2 s in the middle
        Pcm q; std::string err;
        if (!read_wav(argv[2], q, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        int64_t mid = q.frames() / 2;
        float pk = 0;
        for (auto &c : q.ch) for (float v : c) pk = std::max(pk, std::fabs(v));
        printf("frames=%lld rate=%d channels=%d peak=%.4f hz=%.2f\n", (long long)q.frames(), q.rate, q.channels, pk, tone_hz(q, mid - q.rate, mid + q.rate));
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "make-song")) {            // the synthetic test song as a WAV, so a measurement can time the render alone
        double secs = atof(argv[3]);
        if (secs < 5 || secs > 3600) { fprintf(stderr, "seconds 5-3600\n"); return 2; }
        return write_wav(argv[2], make_music(secs)) ? 0 : 1;
    }
    if (argc >= 4 && !strcmp(argv[1], "render")) {
        Job j; j.workers = 4;
        for (int i = 4; i < argc; i++) {
            if (!strcmp(argv[i], "--ratio") && i + 1 < argc) j.ratio = atof(argv[++i]);
            else if (!strcmp(argv[i], "--semitones") && i + 1 < argc) j.semitones = atof(argv[++i]);
            else if (!strcmp(argv[i], "--workers") && i + 1 < argc) j.workers = atoi(argv[++i]);
            else if (!strcmp(argv[i], "--chunks") && i + 1 < argc) j.chunks = atoi(argv[++i]);
            else if (!strcmp(argv[i], "--equal-power")) j.equal_power = true;
            else if (!strcmp(argv[i], "--cheaper")) j.cheaper = true;
            else if (!strcmp(argv[i], "--no-quiet-seams")) j.quiet_seams = false;
            else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
        }
        if (j.ratio < 0.25 || j.ratio > 4 || std::fabs(j.semitones) > 24) { fprintf(stderr, "ratio 0.25-4, semitones within 24\n"); return 2; }
        Pcm in; std::string err;
        if (!read_wav(argv[2], in, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        Stats st; Pcm o = render(in, j, &st);
        int clip = 0;
        if (!write_wav(argv[3], o, &clip)) { fprintf(stderr, "cannot write %s\n", argv[3]); return 1; }
        printf("%d chunks, %.2f s of audio in %.2f s (%.1fx real time, %.2f s of CPU)%s\n", st.chunks, st.audio_s, st.wall_s, st.audio_s / st.wall_s, st.cpu_s, clip ? ", CLIPPED" : "");
        if (clip) printf("WARN %d samples clipped\n", clip);
        return 0;
    }
    fprintf(stderr, "usage: stretch-proto render IN.wav OUT.wav [--ratio R] [--semitones S] [--workers N] [--chunks N] | make-song OUT.wav SECS | selftest | bench [SECS] [--cheaper] [--workers N]\n");
    return 2;
}

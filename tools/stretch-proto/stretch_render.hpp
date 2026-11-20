// stretch_render.hpp: render a whole song at a new tempo and key on several cores (MusiCue's "Key and Tempo" job, ROADMAP section 40).
// Header-only, MIT. The song is cut into overlapping segments at quiet points, each segment is rendered by its own Signalsmith Stretch
// (MIT, one instance per thread: the library is single-threaded) and the pieces are crossfaded at the cuts. A prototype: it keeps the
// whole input and every rendered piece in RAM; the app's version will stream them (docs/MUSICUE.md, "Key and Tempo: render a copy").
#pragma once
#include "signalsmith-stretch.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace sr {

struct Pcm {                       // planar float, any channel count (MusiCue's songs are 2 at 44100)
    int channels = 2, rate = 44100;
    std::vector<std::vector<float>> ch;
    int64_t frames() const { return ch.empty() ? 0 : (int64_t)ch[0].size(); }
};

struct Job {
    double ratio = 1.0;            // tempo ratio: target BPM / base BPM (2 = twice as fast, the song half as long)
    double semitones = 0.0;        // key change
    int workers = 4;               // threads
    int chunks = 0;                // 0: about chunk_s seconds each, and at least `workers`
    double chunk_s = 20.0;         // target length of a segment
    double overlap_s = 1.0;        // each segment is rendered this much further on each side, to settle the phase vocoder, then cropped
    double seam_s = 0.05;          // crossfade length at a cut
    bool equal_power = false;      // crossfade law: linear (the pieces are aligned, so they add up; equal power would swell a cut by up to 3 dB) or equal power
    double align_ms = 15.0;        // each piece may move this far to be in phase with the one before it (0: no alignment)
    bool quiet_seams = true;       // move each cut to the quietest 50 ms within +-1 s of where it would fall
};

struct Stats {
    int chunks = 0;
    double wall_s = 0, cpu_s = 0, audio_s = 0;      // audio_s: the input's length
    std::vector<int64_t> seams_out;                 // the cuts, in output frames
};

inline int64_t out_pos(int64_t in, double ratio) { return (int64_t)std::llround((double)in / ratio); }
inline int64_t out_frames(int64_t in_frames, double ratio) { return out_pos(in_frames, ratio); }

// Where the cuts fall, in input frames: 0, the seams, then the frame count. With quiet_seams each seam is the quietest 50 ms near its even place.
inline std::vector<int64_t> plan_cuts(const Pcm &p, const Job &j, int &chunks_out) {
    const int64_t N = p.frames();
    int n = j.chunks > 0 ? j.chunks : std::max(j.workers, (int)std::llround((double)N / p.rate / j.chunk_s));
    n = std::max(1, std::min<int>(n, (int)std::max<int64_t>(1, N / (p.rate * 4))));   // never shorter than 4 s a segment
    std::vector<int64_t> cuts{0};
    const int win = std::max(1, (int)(0.05 * p.rate)), hop = std::max(1, win / 5);
    for (int k = 1; k < n; k++) {
        int64_t even = N * k / n, best = even;
        if (j.quiet_seams) {
            double bestE = 1e30;
            int64_t lo = std::max<int64_t>(cuts.back() + 2 * (int64_t)p.rate, even - p.rate), hi = std::min<int64_t>(N - 2 * (int64_t)p.rate, even + p.rate);
            for (int64_t s = lo; s + win < hi; s += hop) {
                double e = 0;
                for (int c = 0; c < p.channels; c++) for (int i = 0; i < win; i += 4) e += (double)p.ch[c][s + i] * p.ch[c][s + i];
                if (e < bestE) { bestE = e; best = s + win / 2; }
            }
        }
        cuts.push_back(best);
    }
    cuts.push_back(N);
    chunks_out = n;
    return cuts;
}

// One segment: input frames [a, b) rendered with `lo` extra frames before and `hi` after, and the output cropped back to [out_pos(a), out_pos(b)).
// Returns the cropped output (planar) and the CPU seconds the thread spent.
inline std::vector<std::vector<float>> render_segment(const Pcm &p, int64_t a, int64_t b, int64_t lo, int64_t hi, const Job &j, int seed, double &cpu) {
    using signalsmith::stretch::SignalsmithStretch;
    const int64_t N = p.frames(), x0 = std::max<int64_t>(0, a - lo), x1 = std::min<int64_t>(N, b + hi);
    const int64_t y0 = out_pos(x0, j.ratio), y1 = out_pos(x1, j.ratio), L = y1 - y0;
    SignalsmithStretch<float> st(seed);                // a fixed seed: the same input gives the same bytes, so a test can compare
    st.presetDefault(p.channels, (float)p.rate);
    st.setTransposeSemitones((float)j.semitones);
    std::vector<const float *> in(p.channels);
    std::vector<std::vector<float>> out(p.channels, std::vector<float>(L));
    std::vector<float *> op(p.channels);
    for (int c = 0; c < p.channels; c++) { in[c] = p.ch[c].data() + x0; op[c] = out[c].data(); }
    auto t0 = std::chrono::steady_clock::now();
    st.exact(in.data(), (int)(x1 - x0), op.data(), (int)L);
    cpu = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const int64_t c0 = out_pos(a, j.ratio) - y0, c1 = out_pos(b, j.ratio) - y0;     // the part that is this segment's own
    for (int c = 0; c < p.channels; c++) { out[c].erase(out[c].begin() + c1, out[c].end()); out[c].erase(out[c].begin(), out[c].begin() + c0); }
    return out;
}

inline Pcm render(const Pcm &p, const Job &j, Stats *stats = nullptr) {
    const int64_t N = p.frames();
    int n = 0;
    auto cuts = plan_cuts(p, j, n);
    const int64_t O = (int64_t)(j.overlap_s * p.rate), W = std::max<int64_t>(2, (int64_t)(j.seam_s * p.rate));
    // the input kept either side of a cut beyond the cut itself: the crossfade, the alignment search and the correlation window
    const int64_t pad_in = (int64_t)std::ceil(0.2 * p.rate * std::max(1.0, j.ratio));
    // Each segment is rendered over [cut - O, next cut + O) and used over [cut - W/2, next cut + W/2) in output frames, with the crossfade at each end.
    std::vector<std::vector<std::vector<float>>> piece(n);
    std::vector<double> cpu(n, 0.0);
    std::atomic<int> next{0};
    auto t0 = std::chrono::steady_clock::now();
    auto work = [&]() {
        for (int k; (k = next.fetch_add(1)) < n;) {
            // the crossfade needs W/2 of output each side of a cut; that is W/2*ratio of input, so the segment is cropped to the cut itself and the
            // fade is built from the next segment's own overlap: both are rendered a second further, so just keep the extra W frames here
            int64_t a = cuts[k], b = cuts[k + 1];
            piece[k] = render_segment(p, k == 0 ? a : a - pad_in, k == n - 1 ? b : b + pad_in, O, O, j, 1000 + k, cpu[k]);
        }
    };
    std::vector<std::thread> th;
    for (int i = 1; i < std::max(1, j.workers); i++) th.emplace_back(work);
    work();
    for (auto &t : th) t.join();
    // assemble: each piece is placed where its own input start lands (`base`), then moved by a small shift so that it is in phase with the
    // piece before it over the cut (independent phase vocoders agree on the notes but not on the phase: without this, a steady tone dips or
    // clicks at the cut). The shift is searched in +-align_ms and kept absolute (never added to the previous one), so the sound is never more
    // than align_ms from where the new tempo puts it, and the smallest shift among the nearly-best wins.
    Pcm o; o.channels = p.channels; o.rate = p.rate;
    const int64_t M = out_frames(N, j.ratio);
    o.ch.assign(p.channels, std::vector<float>((size_t)M, 0.f));
    std::vector<int64_t> seams, base(n), shift(n, 0);
    for (int k = 1; k < n; k++) seams.push_back(out_pos(cuts[k], j.ratio));
    for (int k = 0; k < n; k++) base[k] = out_pos(k == 0 ? cuts[0] : cuts[k] - pad_in, j.ratio);
    auto sample = [&](int k, int64_t g, int64_t sh) -> float {         // piece k's mid channel at global frame g, the piece moved by sh
        int64_t i = g - base[k] - sh;
        if (i < 0 || i >= (int64_t)piece[k][0].size()) return 0.f;
        float v = 0; for (int c = 0; c < p.channels; c++) v += piece[k][c][i];
        return v / (float)p.channels;
    };
    const int64_t L = (int64_t)(j.align_ms * 0.001 * p.rate), CW = (int64_t)(0.08 * p.rate);
    for (int k = 1; k < n && L > 0; k++) {
        const int64_t sm = seams[k - 1];
        std::vector<double> cc(2 * L + 1);
        double best = -2;
        for (int64_t s = -L; s <= L; s++) {
            double ab = 0, aa = 0, bb = 0;
            for (int64_t g = sm - CW / 2; g < sm + CW / 2; g += 2) {
                double a = sample(k - 1, g, shift[k - 1]), b = sample(k, g, s);
                ab += a * b; aa += a * a; bb += b * b;
            }
            cc[s + L] = aa > 0 && bb > 0 ? ab / std::sqrt(aa * bb) : 0;
            best = std::max(best, cc[s + L]);
        }
        int64_t pick = 0; double pickc = -2;
        for (int64_t s = -L; s <= L; s++)
            if (cc[s + L] >= best - 0.02 && (pickc < -1 || std::llabs(s) < std::llabs(pick))) { pick = s; pickc = cc[s + L]; }
        shift[k] = pick;
    }
    for (int k = 0; k < n; k++) {
        const int64_t sl = k > 0 ? seams[k - 1] : -1, sr_ = k < n - 1 ? seams[k] : -1;
        const int64_t len = (int64_t)piece[k][0].size();
        for (int64_t i = 0; i < len; i++) {
            int64_t g = base[k] + shift[k] + i;
            if (g < 0 || g >= M) continue;
            double w = 1.0;
            if (sl >= 0) {                                                         // fade in over [sl - W/2, sl + W/2)
                double t = (double)(g - (sl - W / 2)) / (double)W;
                if (t <= 0) w = 0; else if (t < 1) w *= j.equal_power ? std::sin(t * M_PI / 2) : t;
            }
            if (sr_ >= 0) {                                                        // fade out over [sr - W/2, sr + W/2)
                double t = (double)(g - (sr_ - W / 2)) / (double)W;
                if (t >= 1) w = 0; else if (t > 0) w *= j.equal_power ? std::cos(t * M_PI / 2) : 1 - t;
            }
            if (w == 0) continue;
            for (int c = 0; c < p.channels; c++) o.ch[c][g] += (float)(w * piece[k][c][i]);
        }
    }
    if (stats) {
        stats->chunks = n; stats->seams_out = seams; stats->audio_s = (double)N / p.rate;
        stats->wall_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        stats->cpu_s = 0; for (double c : cpu) stats->cpu_s += c;
    }
    return o;
}

// ---- WAV (16-bit PCM only: what MusiCue's songs are) ----------------------------------------------------------------------------------------
inline bool read_wav(const std::string &path, Pcm &p, std::string &err) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) { err = "cannot open " + path; return false; }
    std::vector<unsigned char> d; unsigned char buf[65536]; size_t r;
    while ((r = fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + r);
    fclose(f);
    auto u32 = [&](size_t o) { return (uint32_t)d[o] | d[o + 1] << 8 | d[o + 2] << 16 | (uint32_t)d[o + 3] << 24; };
    auto u16 = [&](size_t o) { return (uint16_t)(d[o] | d[o + 1] << 8); };
    if (d.size() < 44 || memcmp(&d[0], "RIFF", 4) || memcmp(&d[8], "WAVE", 4)) { err = "not a WAV"; return false; }
    size_t o = 12; int bits = 0, chn = 0; uint32_t rate = 0;
    while (o + 8 <= d.size()) {
        uint32_t sz = u32(o + 4);
        if (!memcmp(&d[o], "fmt ", 4) && o + 8 + 16 <= d.size()) { chn = u16(o + 10); rate = u32(o + 12); bits = u16(o + 22); if (u16(o + 8) != 1) { err = "not PCM"; return false; } }
        if (!memcmp(&d[o], "data", 4)) {
            if (bits != 16 || chn < 1 || chn > 2) { err = "only 16-bit mono or stereo"; return false; }
            size_t n = std::min<size_t>(sz, d.size() - o - 8) / (2 * chn);
            p.channels = chn; p.rate = (int)rate; p.ch.assign(chn, std::vector<float>(n));
            for (size_t i = 0; i < n; i++) for (int c = 0; c < chn; c++) p.ch[c][i] = (int16_t)u16(o + 8 + (i * chn + c) * 2) / 32768.f;
            return true;
        }
        o += 8 + sz + (sz & 1);
    }
    err = "no data chunk"; return false;
}

inline bool write_wav(const std::string &path, const Pcm &p, int *clipped = nullptr) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t n = (uint32_t)p.frames(), bytes = n * p.channels * 2;
    unsigned char h[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,16,0,'d','a','t','a',0,0,0,0};
    auto w32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; i++) h[o + i] = (unsigned char)(v >> (8 * i)); };
    w32(4, 36 + bytes); h[22] = (unsigned char)p.channels; w32(24, p.rate); w32(28, p.rate * p.channels * 2); h[32] = (unsigned char)(p.channels * 2); w32(40, bytes);
    fwrite(h, 1, 44, f);
    std::vector<int16_t> buf((size_t)n * p.channels);
    int clip = 0;
    for (uint32_t i = 0; i < n; i++) for (int c = 0; c < p.channels; c++) {
        float v = p.ch[c][i] * 32768.f;
        if (v > 32767.f) { v = 32767.f; clip++; } else if (v < -32768.f) { v = -32768.f; clip++; }
        buf[(size_t)i * p.channels + c] = (int16_t)std::lrintf(v);
    }
    fwrite(buf.data(), 2, buf.size(), f);
    fclose(f);
    if (clipped) *clipped = clip;
    return true;
}

}  // namespace sr

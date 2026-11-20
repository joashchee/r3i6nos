// stream_render.hpp: the Key-and-Tempo render as the app runs it (ROADMAP section 40 step 3; docs/MUSICUE.md, "Key and Tempo: render a copy"). MIT.
// The same method as stretch_render.hpp (segments cut at quiet points, one Signalsmith Stretch per thread, pieces aligned by cross-correlation and
// crossfaded), built to run in 100 MB on a unit with 919 MB:
//   - the song is held as 16-bit (a 12-minute song is 121 MiB); a segment is turned into floats only while a worker renders it;
//   - pieces are mixed into a small window and written to <out>.part as they finish, in order, so memory holds the segments in flight, not the song;
//   - after every flush a journal says how far it has got, so a power cut or a cancel is resumed from the last flush, not from the start;
//   - the segment plan depends on the song only (not on the number of workers), and every segment has a fixed seed, so the output is the same bytes
//     whatever the number of workers, and the same after a resume.
#pragma once
#include "signalsmith-stretch.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace sr {

struct Song16 {                    // interleaved 16-bit
    int channels = 2, rate = 44100;
    std::vector<int16_t> d;
    int64_t frames() const { return channels ? (int64_t)d.size() / channels : 0; }
};

struct Params {
    double ratio = 1.0, semitones = 0.0;
    bool cheaper = false;
    int workers = 4;
    double chunk_s = 20.0, overlap_s = 1.0, seam_s = 0.05, align_ms = 15.0;
};

struct LimitStats { double peak = 0, min_gain = 1.0; int64_t frames = 0, ducked = 0; };      // the loudest sample before the limiter, the deepest gain, frames seen and frames pulled down by more than 0.1 dB
struct Piece { std::vector<std::vector<float>> ch; int64_t base = 0; LimitStats lim; };      // planar floats; base = the output frame of ch[c][0]

inline int64_t opos(int64_t in, double ratio) { return (int64_t)std::llround((double)in / ratio); }

// ---- the plan ------------------------------------------------------------------------------------------------------------------------------
// The cuts in input frames: 0, the seams, the length. Segments of about chunk_s seconds, at least 4 for a song of 16 s or more (so a short song still
// uses the cores), never under 4 s. Each seam moves to the quietest 50 ms within a second of its even place.
inline std::vector<int64_t> plan_cuts(const Song16 &s, const Params &p) {
    const int64_t N = s.frames();
    int n = std::max((int)std::llround((double)N / s.rate / p.chunk_s), (int)std::min<int64_t>(4, N / ((int64_t)s.rate * 4)));
    n = std::max(1, std::min<int>(n, (int)std::max<int64_t>(1, N / ((int64_t)s.rate * 4))));
    std::vector<int64_t> cuts{0};
    const int win = std::max(1, (int)(0.05 * s.rate)), hop = std::max(1, win / 5);
    for (int k = 1; k < n; k++) {
        int64_t even = N * k / n, best = even;
        double bestE = 1e30;
        const int64_t lo = std::max<int64_t>(cuts.back() + 2 * (int64_t)s.rate, even - s.rate), hi = std::min<int64_t>(N - 2 * (int64_t)s.rate, even + s.rate);
        for (int64_t t = lo; t + win < hi; t += hop) {
            double e = 0;
            for (int i = 0; i < win; i += 4) for (int c = 0; c < s.channels; c++) { double v = s.d[(size_t)((t + i) * s.channels + c)]; e += v * v; }
            if (e < bestE) { bestE = e; best = t + win / 2; }
        }
        cuts.push_back(best);
    }
    cuts.push_back(N);
    return cuts;
}

// The input range segment k is rendered from (before the overlap): the cut, less the pad that the crossfade and the alignment need.
struct Geometry {
    std::vector<int64_t> cuts, ia, ib, seams, base;     // per segment: input range [ia, ib), the output frame of its first sample; per seam: its output frame
    int64_t pad, O, W, L, CW, M;
    int n = 0;
};
inline Geometry geometry(const Song16 &s, const Params &p) {
    Geometry g;
    g.cuts = plan_cuts(s, p);
    g.n = (int)g.cuts.size() - 1;
    const int64_t N = s.frames();
    g.pad = (int64_t)std::ceil(0.2 * s.rate * std::max(1.0, p.ratio));
    g.O = (int64_t)(p.overlap_s * s.rate);
    g.W = std::max<int64_t>(2, (int64_t)(p.seam_s * s.rate));
    g.L = (int64_t)(p.align_ms * 0.001 * s.rate);
    g.CW = (int64_t)(0.08 * s.rate);
    g.M = opos(N, p.ratio);
    for (int k = 0; k < g.n; k++) {
        g.ia.push_back(k == 0 ? 0 : g.cuts[k] - g.pad);
        g.ib.push_back(k == g.n - 1 ? N : g.cuts[k + 1] + g.pad);
        g.base.push_back(opos(g.ia[k], p.ratio));
        if (k > 0) g.seams.push_back(opos(g.cuts[k], p.ratio));
    }
    return g;
}

// ---- the limiter ---------------------------------------------------------------------------------------------------------------------------
// A phase vocoder overshoots at transients, and a loud master has no room: the copy would clip, which is audible. So every piece goes through a lookahead
// limiter whose ceiling is a little under full scale. It is done per piece, on the whole piece at once, so it needs no state across the pieces: the same
// bytes come out with any number of workers and after a resume. A piece that never passes the ceiling is not touched at all (bit for bit).
//   need[i]  the gain that would bring frame i to the ceiling (1 when it is under it)
//   m[i]     the smallest need within +-N frames   (the duck starts N frames before a peak and holds N after)
//   g[i]     the average of m over +-N frames       (so the gain glides in and out, and g[i] <= need[i] for every i: no frame passes the ceiling)
//   release  g comes back up no faster than a step a frame, and the step depends on the program: after a brief spike (under 30 ms of ducking since the gain was
//            last at rest) it is fast, a full swing in 40 ms, so a lone peak costs a dip of a few milliseconds and not a hundred; while the music keeps asking for
//            more than that, it is slow, a full swing in 300 ms, so it does not pump on a loud passage.
// N is 1.5 ms (64 frames at 44.1 kHz). The release only ever holds g below the average, never above it, so the ceiling is kept whatever the step.
inline LimitStats limit_piece(std::vector<std::vector<float>> &ch, double ceiling, int rate) {
    const size_t n = ch[0].size();
    LimitStats st;
    st.frames = (int64_t)n;
    std::vector<float> need(n, 1.f);
    for (size_t i = 0; i < n; i++) {
        float a = 0;
        for (auto &c : ch) a = std::max(a, std::fabs(c[i]));
        st.peak = std::max(st.peak, (double)a);
        if (a > ceiling) need[i] = (float)(ceiling / a);
    }
    if (st.peak <= ceiling) return st;
    const int64_t N = std::max(1, (int)std::lround(0.0015 * rate)), nn = (int64_t)n;
    std::vector<float> m(n);
    {   // sliding minimum over [i-N, i+N]: a monotonic deque of indices
        std::vector<int64_t> dq(n + 1); size_t head = 0, tail = 0;
        int64_t next = 0;
        for (int64_t i = 0; i < nn; i++) {
            for (; next <= std::min(nn - 1, i + N); next++) {
                while (tail > head && need[(size_t)dq[tail - 1]] >= need[(size_t)next]) tail--;
                dq[tail++] = next;
            }
            while (dq[head] < i - N) head++;
            m[(size_t)i] = need[(size_t)dq[head]];
        }
    }
    std::vector<double> pre(n + 1, 0.0);
    for (size_t i = 0; i < n; i++) pre[i + 1] = pre[i] + m[i];
    const double fast = 1.0 / (0.04 * rate), slow = 1.0 / (0.3 * rate);
    const int64_t brief = (int64_t)(0.03 * rate);
    double prev = 1.0;
    int64_t asked = 0;                                  // frames since the gain was last at rest that the average asked for a duck
    for (int64_t i = 0; i < nn; i++) {
        const int64_t lo = std::max<int64_t>(0, i - N), hi = std::min(nn - 1, i + N);
        const double avg = (pre[(size_t)hi + 1] - pre[(size_t)lo]) / (double)(hi - lo + 1);
        if (avg < 0.995) asked++;
        double g = std::min(avg, prev + (asked < brief ? fast : slow));
        if (g >= 0.9999 && avg >= 0.9999) { g = 1.0; asked = 0; }       // back at rest: the next duck is judged afresh
        prev = g;
        if (g < st.min_gain) st.min_gain = g;
        if (g < 0.9886) st.ducked++;                    // more than 0.1 dB down
        for (auto &c : ch) c[(size_t)i] = (float)(c[(size_t)i] * g);
    }
    return st;
}
const double LIMIT_CEILING = 0.985;                     // a little under full scale: the rounding to 16 bits and the sum of two pieces in a crossfade stay under 1

// ---- one segment ---------------------------------------------------------------------------------------------------------------------------
inline Piece render_segment(const Song16 &s, const Params &p, const Geometry &g, int k) {
    using signalsmith::stretch::SignalsmithStretch;
    const int64_t N = s.frames(), x0 = std::max<int64_t>(0, g.ia[k] - g.O), x1 = std::min<int64_t>(N, g.ib[k] + g.O);
    const int64_t y0 = opos(x0, p.ratio), y1 = opos(x1, p.ratio), L = y1 - y0;
    const int C = s.channels;
    std::vector<std::vector<float>> in(C, std::vector<float>((size_t)(x1 - x0)));
    for (int64_t i = 0; i < x1 - x0; i++) for (int c = 0; c < C; c++) in[c][(size_t)i] = s.d[(size_t)((x0 + i) * C + c)] / 32768.f;
    std::vector<std::vector<float>> out(C, std::vector<float>((size_t)L));
    std::vector<const float *> ip(C); std::vector<float *> op(C);
    for (int c = 0; c < C; c++) { ip[c] = in[c].data(); op[c] = out[c].data(); }
    {
        SignalsmithStretch<float> st(1000 + k);                                   // a fixed seed per segment: the same bytes whoever renders it, and after a resume
        if (p.cheaper) st.presetCheaper(C, (float)s.rate); else st.presetDefault(C, (float)s.rate);
        st.setTransposeSemitones((float)p.semitones);
        st.exact(ip.data(), (int)(x1 - x0), op.data(), (int)L);
    }
    in.clear(); in.shrink_to_fit();
    Piece pc; pc.base = g.base[k];
    const int64_t c0 = opos(g.ia[k], p.ratio) - y0, c1 = opos(g.ib[k], p.ratio) - y0;
    pc.ch.resize(C);
    for (int c = 0; c < C; c++) pc.ch[c].assign(out[c].begin() + c0, out[c].begin() + c1);
    pc.lim = limit_piece(pc.ch, LIMIT_CEILING, s.rate);
    return pc;
}

// ---- the joining ---------------------------------------------------------------------------------------------------------------------------
inline void mid_of(const Piece &pc, int64_t g0, int64_t g1, int64_t sh, std::vector<float> &v) {
    v.assign((size_t)(g1 - g0), 0.f);
    const int C = (int)pc.ch.size();
    const int64_t len = (int64_t)pc.ch[0].size();
    for (int64_t g = g0; g < g1; g++) {
        int64_t i = g - pc.base - sh;
        if (i < 0 || i >= len) continue;
        float x = 0; for (int c = 0; c < C; c++) x += pc.ch[c][(size_t)i];
        v[(size_t)(g - g0)] = x / (float)C;
    }
}

// How far piece `cur` moves to be in phase with `prev` (placed at `prev_shift`) over the cut at output frame `seam`: the smallest shift within +-L
// among the nearly-best correlations (an absolute shift, never added to the last one).
inline int64_t align_shift(const Geometry &g, const Piece &prev, int64_t prev_shift, const Piece &cur, int64_t seam) {
    if (g.L <= 0) return 0;
    const int64_t lo = seam - g.CW / 2, hi = seam + g.CW / 2, m = hi - lo;
    std::vector<float> A, B0;
    mid_of(prev, lo, hi, prev_shift, A);
    mid_of(cur, lo - g.L, hi + g.L, 0, B0);
    double aa = 0;
    for (int64_t i = 0; i < m; i++) aa += (double)A[(size_t)i] * A[(size_t)i];
    std::vector<double> cc((size_t)(2 * g.L + 1));
    double best = -2;
    for (int64_t s = -g.L; s <= g.L; s++) {
        const float *b = B0.data() + (g.L - s);
        float ab = 0, bb = 0;
        for (int64_t i = 0; i < m; i++) { ab += A[(size_t)i] * b[i]; bb += b[i] * b[i]; }
        cc[(size_t)(s + g.L)] = aa > 0 && bb > 0 ? (double)ab / std::sqrt(aa * (double)bb) : 0;
        best = std::max(best, cc[(size_t)(s + g.L)]);
    }
    int64_t pick = 0; double pickc = -2;
    for (int64_t s = -g.L; s <= g.L; s++)
        if (cc[(size_t)(s + g.L)] >= best - 0.02 && (pickc < -1 || std::llabs(s) < std::llabs(pick))) { pick = s; pickc = cc[(size_t)(s + g.L)]; }
    return pick;
}

// The output as it is written: floats from `base` on, flushed in order.
struct Window {
    std::vector<std::vector<float>> ch;
    int64_t base = 0;                                  // the output frame of ch[c][0]: everything before it is already in the file
    void need(int64_t upto) { for (auto &v : ch) if ((int64_t)v.size() < upto - base) v.resize((size_t)(upto - base), 0.f); }
};

// Piece k, moved by its shift and faded at its cuts, added into the window; frames before the window's start are already written and are skipped.
inline void mix_piece(const Geometry &g, const Params &p, Window &w, const Piece &pc, int k, int64_t shift) {
    (void)p;
    const int C = (int)pc.ch.size();
    const int64_t len = (int64_t)pc.ch[0].size(), gs = pc.base + shift, W = g.W;
    const int64_t sl = k > 0 ? g.seams[k - 1] : -1, sr = k < g.n - 1 ? g.seams[k] : -1;
    const int64_t inA = sl >= 0 ? sl - W / 2 : gs, inB = sl >= 0 ? sl + W / 2 : gs;
    const int64_t outA = sr >= 0 ? sr - W / 2 : gs + len, outB = sr >= 0 ? sr + W / 2 : gs + len;
    const int64_t g0 = std::max<int64_t>(std::max(gs, inA), w.base), g1 = std::min(std::min(gs + len, outB), g.M);
    if (g1 <= g0) return;
    w.need(g1);
    const int64_t b0 = std::min(std::max(g0, inB), g1), b1 = std::max(std::min(g1, outA), b0);
    auto faded = [&](int64_t from, int64_t to) {
        for (int64_t q = from; q < to; q++) {
            double wt = 1.0;
            if (q < inB) wt = (double)(q - inA) / (double)W;                          // a linear fade: the pieces are aligned, so they add up
            if (q >= outA) wt *= 1.0 - (double)(q - outA) / (double)W;
            for (int c = 0; c < C; c++) w.ch[c][(size_t)(q - w.base)] += (float)(wt * pc.ch[c][(size_t)(q - gs)]);
        }
    };
    faded(g0, b0);
    for (int c = 0; c < C; c++) {
        const float *src = pc.ch[c].data() + (b0 - gs); float *dst = w.ch[c].data() + (b0 - w.base);
        for (int64_t q = 0; q < b1 - b0; q++) dst[q] += src[q];
    }
    faded(b1, g1);
}

// ---- the output file and the journal -------------------------------------------------------------------------------------------------------
struct Out {
    FILE *f = nullptr;
    int channels = 2, rate = 44100;
    int64_t frames = 0;                                // frames written
    int clipped = 0;
    bool open_new(const std::string &path, int ch, int rt) {
        channels = ch; rate = rt; frames = 0;
        f = fopen(path.c_str(), "wb");
        if (!f) return false;
        unsigned char h[44] = {};
        return fwrite(h, 1, 44, f) == 44;               // the header is written at the end, when the length is known
    }
    bool open_resume(const std::string &path, int ch, int rt, int64_t emitted) {
        channels = ch; rate = rt; frames = emitted;
        f = fopen(path.c_str(), "r+b");
        if (!f) return false;
        const int64_t want = 44 + emitted * ch * 2;
        struct stat st;
        if (fstat(fileno(f), &st) != 0 || st.st_size < want) return false;      // the journal says more than the file has: start again
        if (ftruncate(fileno(f), want) != 0) return false;
        return fseek(f, 0, SEEK_END) == 0;
    }
    bool write(const Window &w, int64_t upto) {        // frames [w.base, upto) of the window
        const int64_t n = upto - w.base;
        std::vector<int16_t> buf((size_t)(n * channels));
        for (int64_t i = 0; i < n; i++) for (int c = 0; c < channels; c++) {
            float v = ((size_t)i < w.ch[c].size() ? w.ch[c][(size_t)i] : 0.f) * 32768.f;
            if (v > 32767.f) { v = 32767.f; clipped++; } else if (v < -32768.f) { v = -32768.f; clipped++; }
            buf[(size_t)(i * channels + c)] = (int16_t)std::lrintf(v);
        }
        if (n > 0 && fwrite(buf.data(), 2, buf.size(), f) != buf.size()) return false;
        frames += n;
        return true;
    }
    bool sync() { return fflush(f) == 0 && fsync(fileno(f)) == 0; }
    bool finish() {                                    // the header, now the length is known
        const uint32_t bytes = (uint32_t)(frames * channels * 2);
        unsigned char h[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,16,0,'d','a','t','a',0,0,0,0};
        auto w32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; i++) h[o + i] = (unsigned char)(v >> (8 * i)); };
        w32(4, 36 + bytes); h[22] = (unsigned char)channels; w32(24, (uint32_t)rate); w32(28, (uint32_t)(rate * channels * 2)); h[32] = (unsigned char)(channels * 2); w32(40, bytes);
        if (fflush(f) != 0 || fseek(f, 0, SEEK_SET) != 0 || fwrite(h, 1, 44, f) != 44 || !sync()) return false;
        bool ok = fclose(f) == 0;
        f = nullptr;
        return ok;
    }
};

struct Journal {
    int64_t frames_in = 0;                             // the song's length: a different song starts again
    double ratio = 0, semitones = 0;
    int cheaper = 0, n = 0;
    int done = 0;                                      // 1: the whole song is rendered into the part (the encode that follows is what a power cut interrupts)
    int c = 0;                                         // pieces 0..c-1 are mixed and written as far as `emitted`; piece c is joined next
    int64_t emitted = 0;                               // output frames in the file
    std::vector<int64_t> shifts;                       // shifts[0..c]
    bool same_job(const Journal &o) const { return frames_in == o.frames_in && ratio == o.ratio && semitones == o.semitones && cheaper == o.cheaper && n == o.n; }
    bool save(const std::string &path) const {
        std::string tmp = path + ".tmp";
        FILE *f = fopen(tmp.c_str(), "w");
        if (!f) return false;
        fprintf(f, "musicue-render-journal 1\nframes_in=%lld\nratio=%.17g\nsemitones=%.17g\ncheaper=%d\nn=%d\ndone=%d\nc=%d\nemitted=%lld\nshifts=",
                (long long)frames_in, ratio, semitones, cheaper, n, done, c, (long long)emitted);
        for (size_t i = 0; i < shifts.size(); i++) fprintf(f, "%s%lld", i ? "," : "", (long long)shifts[i]);
        fprintf(f, "\nend\n");
        bool ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
        ok = fclose(f) == 0 && ok;
        return ok && rename(tmp.c_str(), path.c_str()) == 0;
    }
    bool load(const std::string &path) {
        FILE *f = fopen(path.c_str(), "r");
        if (!f) return false;
        char line[4096]; bool head = false, endl_ = false;
        long long a = 0, e = 0;
        while (fgets(line, sizeof line, f)) {
            if (!strncmp(line, "musicue-render-journal 1", 24)) head = true;
            else if (sscanf(line, "frames_in=%lld", &a) == 1) frames_in = a;
            else if (sscanf(line, "ratio=%lf", &ratio) == 1) {}
            else if (sscanf(line, "semitones=%lf", &semitones) == 1) {}
            else if (sscanf(line, "cheaper=%d", &cheaper) == 1) {}
            else if (sscanf(line, "n=%d", &n) == 1) {}
            else if (sscanf(line, "done=%d", &done) == 1) {}
            else if (sscanf(line, "c=%d", &c) == 1) {}
            else if (sscanf(line, "emitted=%lld", &e) == 1) emitted = e;
            else if (!strncmp(line, "shifts=", 7)) {
                shifts.clear();
                for (char *t = strtok(line + 7, ",\n"); t; t = strtok(nullptr, ",\n")) shifts.push_back(atoll(t));
            } else if (!strncmp(line, "end", 3)) endl_ = true;
        }
        fclose(f);
        if (head && endl_ && done) return emitted >= 0;
        return head && endl_ && c >= 1 && c < n && (int)shifts.size() == c + 1 && emitted >= 0;
    }
};

// ---- the job -------------------------------------------------------------------------------------------------------------------------------
struct Result { bool ok = false; std::string error; bool cancelled = false; int segments = 0, resumed_from = 0; int clipped = 0; LimitStats lim; int64_t frames = 0; double wall_s = 0; };

// Renders `song` into `out_part`, journalling to `journal`; the caller renames the part to the final name. `resume` lets a valid journal continue.
// progress(done_segments, total_segments) is called after each segment is joined. `cancel` is polled between segments. `die_after`, if > 0, makes the
// process exit as a power cut would right after that checkpoint (for the tests only).
inline Result render_song(const Song16 &song, const Params &p, const std::string &out_part, const std::string &journal_path, bool resume,
                          const std::function<void(int, int)> &progress, const std::atomic<bool> &cancel, int die_after = 0) {
    Result r;
    auto t0 = std::chrono::steady_clock::now();
    const Geometry g = geometry(song, p);
    r.segments = g.n;
    const int C = song.channels;
    Journal want; want.frames_in = song.frames(); want.ratio = p.ratio; want.semitones = p.semitones; want.cheaper = p.cheaper; want.n = g.n;
    Journal j; Out out; Window win; win.ch.assign(C, {});
    if (resume && j.load(journal_path) && j.done && j.same_job(want)) {            // rendered already: only what came after it (the encode) was cut short
        struct stat st;
        if (stat(out_part.c_str(), &st) == 0 && (int64_t)st.st_size == 44 + g.M * C * 2) { r.ok = true; r.frames = g.M; r.resumed_from = g.n; return r; }
    }
    j = Journal();
    int start = 0;                                      // the first segment to render
    bool resumed = false;
    if (resume && j.load(journal_path) && !j.done && j.same_job(want) && out.open_resume(out_part, C, song.rate, j.emitted)) { resumed = true; start = std::max(0, j.c - 1); r.resumed_from = j.c; }
    if (!resumed) {
        if (out.f) { fclose(out.f); out.f = nullptr; }
        j = want; j.c = 0; j.emitted = 0; j.shifts.clear();
        unlink(journal_path.c_str());
        if (!out.open_new(out_part, C, song.rate)) { r.error = "cannot write " + out_part; return r; }
    }
    win.base = resumed ? j.emitted : 0;
    std::vector<int64_t> shift(g.n, 0);
    for (size_t i = 0; i < j.shifts.size() && i < shift.size(); i++) shift[i] = j.shifts[i];

    // workers render segments in order, never more than workers + 1 ahead of the join, so memory holds a few segments, not the song
    std::mutex mu; std::condition_variable cv;
    std::map<int, Piece> done; int next = start, joined = start; bool stop = false;
    auto work = [&]() {
        for (;;) {
            int k;
            {
                std::unique_lock<std::mutex> lk(mu);
                cv.wait(lk, [&] { return stop || next >= g.n || next < joined + std::max(1, p.workers) + 1; });
                if (stop || next >= g.n) return;
                k = next++;
            }
            Piece pc = render_segment(song, p, g, k);
            { std::lock_guard<std::mutex> lk(mu); r.lim.peak = std::max(r.lim.peak, pc.lim.peak); r.lim.min_gain = std::min(r.lim.min_gain, pc.lim.min_gain); r.lim.frames += pc.lim.frames; r.lim.ducked += pc.lim.ducked; done[k] = std::move(pc); }
            cv.notify_all();
        }
    };
    std::vector<std::thread> th;
    for (int i = 0; i < std::max(1, p.workers); i++) th.emplace_back(work);
    auto get = [&](int k, Piece &dst) {
        std::unique_lock<std::mutex> lk(mu);
        cv.wait(lk, [&] { return done.count(k) > 0 || stop; });
        if (!done.count(k)) return false;
        dst = std::move(done[k]); done.erase(k);
        return true;
    };
    auto halt = [&]() { { std::lock_guard<std::mutex> lk(mu); stop = true; } cv.notify_all(); for (auto &t : th) t.join(); th.clear(); };
    auto fail = [&](const std::string &why) { halt(); r.error = why; if (out.f) fclose(out.f); return r; };

    Piece prev;
    int k = start;
    if (resumed) {                                      // piece c-1 again, only to refill the window; piece c is the one to join next
        Piece before;
        if (!get(j.c - 1, before)) return fail("cancelled");
        mix_piece(g, p, win, before, j.c - 1, shift[j.c - 1]);
        if (!get(j.c, prev)) return fail("cancelled");
        { std::lock_guard<std::mutex> lk(mu); joined = j.c + 1; } cv.notify_all();
        k = j.c + 1;
        if (progress) progress(j.c, g.n);
    } else {
        if (!get(0, prev)) return fail("cancelled");
        { std::lock_guard<std::mutex> lk(mu); joined = 1; } cv.notify_all();
        k = 1;
        if (progress) progress(1, g.n);
    }
    for (; k < g.n; k++) {
        if (cancel.load()) { halt(); r.cancelled = true; r.error = "cancelled"; if (out.f) { out.sync(); fclose(out.f); } return r; }
        Piece cur;
        if (!get(k, cur)) return fail("cancelled");
        { std::lock_guard<std::mutex> lk(mu); joined = k + 1; } cv.notify_all();
        shift[k] = align_shift(g, prev, shift[k - 1], cur, g.seams[k - 1]);
        mix_piece(g, p, win, prev, k - 1, shift[k - 1]);
        const int64_t safe = std::min(g.seams[k - 1] - g.W / 2, g.M);                      // nothing later than piece k can add to a frame before this
        if (safe > win.base) {
            if (!out.write(win, safe)) return fail("cannot write " + out_part + " (is the card full?)");
            for (auto &v : win.ch) v.erase(v.begin(), v.begin() + std::min<int64_t>((int64_t)v.size(), safe - win.base));
            win.base = safe;
        }
        if (!out.sync()) return fail("cannot write " + out_part);
        j.c = k; j.emitted = win.base; j.shifts.assign(shift.begin(), shift.begin() + k + 1);
        if (!j.save(journal_path)) return fail("cannot write " + journal_path);
        if (die_after > 0 && k == die_after) _exit(9);                                  // the tests: a power cut right after a checkpoint
        prev = std::move(cur);
        if (progress) progress(k + 1, g.n);
    }
    halt();
    mix_piece(g, p, win, prev, g.n - 1, shift[g.n - 1]);
    if (!out.write(win, g.M)) { r.error = "cannot write " + out_part + " (is the card full?)"; if (out.f) fclose(out.f); return r; }
    if (!out.finish()) { r.error = "cannot finish " + out_part; return r; }
    j.done = 1; j.c = g.n; j.emitted = g.M;                                             // the caller deletes the journal when it has finished with the part
    if (!j.save(journal_path)) { r.error = "cannot write " + journal_path; return r; }
    r.ok = true; r.frames = out.frames; r.clipped = out.clipped;
    r.wall_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

}  // namespace sr

// musicue_render.cpp: MusiCue's Key-and-Tempo render, run by the app as a child process (the way it runs musicue-sync). MIT.
//
//   musicue-render --in SONG --out COPY.wav --ratio R --semitones S [--workers N] [--cheaper] [--no-resume]
//
// SONG is a 16-bit 44.1 kHz stereo WAV, or anything ffmpeg ($R3I6NOS_FFMPEG, else `ffmpeg` on the PATH) can read. The copy is written to COPY.wav.part
// with COPY.wav.journal beside it and renamed to COPY.wav when it is whole. Run again with the same arguments, it continues from the journal (a
// power cut, a cancel); --no-resume starts again. Says what it is doing on stdout in the lines the app's job screen reads:
//   PROGRESS x/y text     a step done (x of y) and what is next
//   BAR x/y               the bar only
//   DONE <frames> frames  finished; the file is COPY.wav
//   ERROR text            failed (exit 1); a cancel (SIGTERM, SIGINT) leaves the part and the journal and exits 130
// Exit 127 is "no such program" to the app, so this exits 2 for a usage error. $R3I6NOS_RENDER_DIE_AFTER=k (tests only) exits as a power cut after the
// k-th checkpoint.
#include "stream_render.hpp"
#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdlib>
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <sys/wait.h>

using namespace sr;

static std::atomic<bool> g_cancel{false};
static void on_term(int) { g_cancel.store(true); }

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    putchar('\n'); fflush(stdout);
}

// A 16-bit 44.1 kHz stereo WAV straight into memory; false when it is anything else (the caller then asks ffmpeg).
static bool read_wav16(const std::string &path, Song16 &s) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(f); return false; }
    int fmt = 0, ch = 0, bits = 0; uint32_t rate = 0;
    for (;;) {
        unsigned char c[8];
        if (fread(c, 1, 8, f) != 8) break;
        uint32_t sz = (uint32_t)c[4] | (uint32_t)c[5] << 8 | (uint32_t)c[6] << 16 | (uint32_t)c[7] << 24;
        if (!memcmp(c, "fmt ", 4) && sz >= 16) {
            unsigned char b[16];
            if (fread(b, 1, 16, f) != 16) break;
            fmt = b[0] | b[1] << 8; ch = b[2] | b[3] << 8; rate = (uint32_t)b[4] | (uint32_t)b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24; bits = b[14] | b[15] << 8;
            if (fseek(f, (long)(sz - 16 + (sz & 1)), SEEK_CUR) != 0) break;
        } else if (!memcmp(c, "data", 4)) {
            if (fmt != 1 || ch != 2 || bits != 16 || rate != 44100) break;
            struct stat st; fstat(fileno(f), &st);
            const int64_t avail = st.st_size - ftell(f);
            const int64_t bytes = std::min<int64_t>(sz == 0xFFFFFFFFu ? avail : sz, avail) / 4 * 4;
            s.channels = 2; s.rate = 44100; s.d.resize((size_t)(bytes / 2));
            bool ok = fread(s.d.data(), 2, s.d.size(), f) == s.d.size();
            fclose(f);
            return ok;                                  // little-endian machine (the unit and the Mac): the samples are already in order
        } else if (fseek(f, (long)(sz + (sz & 1)), SEEK_CUR) != 0) break;
    }
    fclose(f);
    return false;
}

static bool decode_with_ffmpeg(const std::string &in, const std::string &tmp, std::string &err) {
    const char *ff = getenv("R3I6NOS_FFMPEG");
    if (!ff || !*ff) ff = "ffmpeg";
    pid_t pid = fork();
    if (pid < 0) { err = "cannot start ffmpeg"; return false; }
    if (pid == 0) {
        execlp(ff, ff, "-v", "error", "-nostdin", "-y", "-i", in.c_str(), "-vn", "-acodec", "pcm_s16le", "-ar", "44100", "-ac", "2", "-f", "wav", tmp.c_str(), (char *)nullptr);
        _exit(127);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    if (g_cancel.load()) { err = "cancelled"; return false; }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) { err = WIFEXITED(st) && WEXITSTATUS(st) == 127 ? "ffmpeg is not installed" : "ffmpeg could not read the song"; return false; }
    return true;
}

int main(int argc, char **argv) {
    std::string in, out;
    Params p; bool resume = true;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> const char * { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--in") in = val();
        else if (a == "--out") out = val();
        else if (a == "--ratio") p.ratio = atof(val());
        else if (a == "--semitones") p.semitones = atof(val());
        else if (a == "--workers") p.workers = atoi(val());
        else if (a == "--cheaper") p.cheaper = true;
        else if (a == "--no-resume") resume = false;
        else { fprintf(stderr, "musicue-render: unknown option %s\n", a.c_str()); return 2; }
    }
    if (in.empty() || out.empty() || !(p.ratio >= 0.25 && p.ratio <= 4) || !(std::fabs(p.semitones) <= 24) || p.workers < 1 || p.workers > 16) {
        fprintf(stderr, "usage: musicue-render --in SONG --out COPY.wav --ratio R(0.25-4) --semitones S(-24..24) [--workers N] [--cheaper] [--no-resume]\n");
        return 2;
    }
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = on_term; sigaction(SIGTERM, &sa, nullptr); sigaction(SIGINT, &sa, nullptr);

    say("PROGRESS 0/1 Reading the song...");
    Song16 song;
    if (!read_wav16(in, song)) {
        const std::string tmp = out + ".in.wav";
        std::string err;
        say("PROGRESS 0/1 Decoding the song...");
        if (!decode_with_ffmpeg(in, tmp, err)) { unlink(tmp.c_str()); if (g_cancel.load()) return 130; say("ERROR %s", err.c_str()); return 1; }
        bool ok = read_wav16(tmp, song);
        unlink(tmp.c_str());
        if (!ok) { say("ERROR the decoded song is not readable"); return 1; }
    }
    if (song.frames() < 44100 * 8) { say("ERROR the song is under 8 seconds"); return 1; }

    // room for the copy (and 16 MiB over), before anything is written
    {
        std::string dir = out.find('/') == std::string::npos ? "." : out.substr(0, out.rfind('/'));
        struct statvfs sv;
        const double need = (double)song.frames() / p.ratio * 4.0 * 1.02 + 16.0 * 1048576.0;
        if (statvfs(dir.c_str(), &sv) == 0 && (double)sv.f_bavail * (double)sv.f_frsize < need) { say("ERROR not enough room for the copy (%.0f MiB needed)", need / 1048576.0); return 1; }
    }

    const std::string part = out + ".part", journal = out + ".journal";
    int die = 0; if (const char *d = getenv("R3I6NOS_RENDER_DIE_AFTER")) die = atoi(d);
    say("PROGRESS 0/1 Rendering with %d workers...", p.workers);
    Result r = render_song(song, p, part, journal, resume, [&](int done, int total) {
        say("PROGRESS %d/%d Segment %d of %d joined", done, total, done, total);
    }, g_cancel, die);
    if (r.cancelled || (!r.ok && g_cancel.load())) return 130;
    if (!r.ok) { say("ERROR %s", r.error.c_str()); return 1; }
    if (rename(part.c_str(), out.c_str()) != 0) { say("ERROR cannot rename the copy into place"); return 1; }
    if (r.resumed_from) say("PROGRESS %d/%d Resumed from segment %d", r.segments, r.segments, r.resumed_from);
    struct rusage ru; getrusage(RUSAGE_SELF, &ru);
    say("DONE %lld frames in %.1f s, peak memory %ld MiB%s", (long long)r.frames, r.wall_s, ru.ru_maxrss / 1024, r.clipped ? " (some samples clipped)" : "");
    return 0;
}

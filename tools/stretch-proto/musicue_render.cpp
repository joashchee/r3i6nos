// musicue_render.cpp: MusiCue's Key-and-Tempo render, run by the app as a child process (the way it runs musicue-sync). MIT.
//
//   musicue-render --in SONG --out COPY.wav|COPY.m4a --ratio R --semitones S [--workers N] [--cheaper] [--no-resume] [--encode aac[:KBPS[:CODER]]]
//   musicue-render --compress-dir DIR [--encode aac[:KBPS[:CODER]]]
//
// SONG is a 16-bit 44.1 kHz stereo WAV, or anything ffmpeg ($R3I6NOS_FFMPEG, else `ffmpeg` on the PATH) can read. The copy is written to COPY.wav.part
// with COPY.wav.journal beside it and renamed to COPY.wav when it is whole. With --encode the copy is then compressed to AAC in an M4A (ffmpeg's native aac
// encoder, default 192 kbps with the fast coder: the image's ffmpeg has the encoder and the `ipod` muxer and nothing else for audio) as COPY.m4a: the WAV is
// COPY.m4a.part until then, a power cut during the encode is not rendered again, and if the encode fails the copy is kept as a WAV beside where the M4A would
// have been. --compress-dir turns every copy WAV in a folder (a name with " [kt-" in it) into an M4A the same way and deletes the WAV. Run again with the same arguments, it continues from the journal (a
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
#include <dirent.h>
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

// Anything else through ffmpeg, the way the app's own decoder does it: raw 16-bit 44.1 kHz stereo on a pipe (the image's ffmpeg has the s16le muxer and no
// wav one), straight into the song's memory. ffmpeg's own error lines are left on stderr, which the app shows on the job screen. A song over max_s seconds
// is refused (the app opens at most 12 minutes of a song, and a copy of part of one would be wrong).
static bool decode_with_ffmpeg(const std::string &in, double max_s, Song16 &song, std::string &err) {
    const char *ff = getenv("R3I6NOS_FFMPEG");
    if (!ff || !*ff) ff = "ffmpeg";
    int fd[2];
    if (pipe(fd) != 0) { err = "no pipe"; return false; }
    pid_t pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); err = "cannot start ffmpeg"; return false; }
    if (pid == 0) {
        dup2(fd[1], 1);
        close(fd[0]); close(fd[1]);
        execlp(ff, ff, "-v", "error", "-nostdin", "-i", in.c_str(), "-vn", "-f", "s16le", "-ar", "44100", "-ac", "2", "pipe:1", (char *)nullptr);
        _exit(127);
    }
    close(fd[1]);
    const size_t cap_bytes = (size_t)(max_s * 44100.0) * 4;
    song.channels = 2; song.rate = 44100; song.d.clear(); song.d.reserve(cap_bytes / 2 + 4096);
    size_t have = 0;                                   // bytes read so far
    bool too_long = false;
    for (;;) {
        if (g_cancel.load()) { kill(pid, SIGTERM); break; }
        if (song.d.size() * 2 < have + 65536) song.d.resize(std::min(cap_bytes + 65536, have + (size_t)4 * 1048576) / 2 + 1);
        ssize_t got = read(fd[0], (char *)song.d.data() + have, std::min<size_t>(65536, cap_bytes + 65536 - have));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        have += (size_t)got;
        if (have > cap_bytes) { too_long = true; kill(pid, SIGTERM); break; }
    }
    close(fd[0]);
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    if (g_cancel.load()) { err = "cancelled"; return false; }
    if (too_long) {
        char buf[96];
        if (max_s >= 120 && std::fmod(max_s, 60.0) == 0) snprintf(buf, sizeof buf, "the song is longer than %.0f minutes; it cannot be copied", max_s / 60);
        else snprintf(buf, sizeof buf, "the song is longer than %.0f seconds; it cannot be copied", max_s);
        err = buf;
        return false;
    }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) { err = WIFEXITED(st) && WEXITSTATUS(st) == 127 ? "ffmpeg is not installed" : "ffmpeg could not read the song"; return false; }
    song.d.resize(have / 4 * 2);
    song.d.shrink_to_fit();
    return true;
}

struct Encode { bool on = false; int kbps = 192; std::string coder = "fast"; };
static bool parse_encode(const std::string &spec, Encode &e) {         // aac[:KBPS[:CODER]]
    e = Encode();
    if (spec.compare(0, 3, "aac") != 0) return false;
    size_t p = spec.find(':');
    if (p != std::string::npos) {
        std::string rest = spec.substr(p + 1);
        e.kbps = atoi(rest.c_str());
        size_t q = rest.find(':');
        if (q != std::string::npos) e.coder = rest.substr(q + 1);
    }
    if (e.kbps < 64 || e.kbps > 320) return false;
    if (e.coder != "fast" && e.coder != "twoloop" && e.coder != "anmr") return false;
    e.on = true;
    return true;
}

// WAV in, AAC in an M4A out, through ffmpeg: -progress on a pipe says how far it is (a BAR line each time). `total_frames` is the WAV's length.
static bool encode_aac(const std::string &wav, const std::string &out, const Encode &e, int64_t total_frames, std::string &err) {
    const char *ff = getenv("R3I6NOS_FFMPEG");
    if (!ff || !*ff) ff = "ffmpeg";
    int fd[2];
    if (pipe(fd) != 0) { err = "no pipe"; return false; }
    pid_t pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); err = "cannot start ffmpeg"; return false; }
    if (pid == 0) {
        dup2(fd[1], 1);
        close(fd[0]); close(fd[1]);
        const std::string kb = std::to_string(e.kbps) + "k";
        execlp(ff, ff, "-v", "error", "-nostdin", "-nostats", "-y", "-progress", "pipe:1", "-f", "wav", "-i", wav.c_str(), "-vn", "-c:a", "aac", "-aac_coder", e.coder.c_str(),
               "-b:a", kb.c_str(), "-f", "ipod", out.c_str(), (char *)nullptr);
        _exit(127);
    }
    close(fd[1]);
    std::string buf;
    int last = -1;
    char chunk[512];
    for (;;) {
        if (g_cancel.load()) { kill(pid, SIGTERM); break; }
        ssize_t got = read(fd[0], chunk, sizeof chunk);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        buf.append(chunk, (size_t)got);
        size_t nl;
        while ((nl = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, nl); buf.erase(0, nl + 1);
            if (line.compare(0, 12, "out_time_us=") == 0 && total_frames > 0) {
                int pct = (int)std::min(100.0, atof(line.c_str() + 12) / ((double)total_frames / 44100.0 * 1e6) * 100.0);
                if (pct != last && pct >= 0) { last = pct; say("BAR %d/100", pct); }
            }
        }
    }
    close(fd[0]);
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    if (g_cancel.load()) { err = "cancelled"; return false; }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) { err = WIFEXITED(st) && WEXITSTATUS(st) == 127 ? "ffmpeg is not installed" : "ffmpeg could not compress the copy"; return false; }
    return true;
}

static int64_t file_size(const std::string &p) { struct stat st; return stat(p.c_str(), &st) == 0 ? (int64_t)st.st_size : -1; }

// Every copy WAV in a folder (its name has " [kt-") becomes an M4A and the WAV is deleted; a copy that already has its M4A keeps both until the WAV is removed by hand.
static int compress_dir(const std::string &dir, const Encode &e) {
    std::vector<std::string> names;
    if (DIR *d = opendir(dir.c_str())) {
        while (struct dirent *de = readdir(d)) {
            std::string n = de->d_name;
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".wav") == 0 && n.find(" [kt-") != std::string::npos) names.push_back(n);
        }
        closedir(d);
    }
    std::sort(names.begin(), names.end());
    int done = 0; int64_t saved = 0;
    for (size_t i = 0; i < names.size(); i++) {
        const std::string wav = dir + "/" + names[i], m4a = wav.substr(0, wav.size() - 4) + ".m4a", tmp = m4a + ".enc";
        if (file_size(m4a) >= 0) continue;
        say("PROGRESS %zu/%zu %s", i, names.size(), names[i].c_str());
        const int64_t before = file_size(wav);
        std::string err;
        Song16 probe;
        int64_t frames = (before - 44) / 4;
        if (!encode_aac(wav, tmp, e, frames, err)) { unlink(tmp.c_str()); if (g_cancel.load()) return 130; say("ERROR %s: %s", names[i].c_str(), err.c_str()); return 1; }
        if (rename(tmp.c_str(), m4a.c_str()) != 0) { say("ERROR cannot rename %s into place", names[i].c_str()); return 1; }
        saved += before - file_size(m4a);
        unlink(wav.c_str());
        done++;
    }
    say("DONE %d copies compressed, %.1f MiB saved", done, (double)saved / 1048576.0);
    return 0;
}

int main(int argc, char **argv) {
    std::string in, out, compress, enc_spec;
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
        else if (a == "--encode") enc_spec = val();
        else if (a == "--compress-dir") compress = val();
        else { fprintf(stderr, "musicue-render: unknown option %s\n", a.c_str()); return 2; }
    }
    Encode enc;
    if (!enc_spec.empty() && !parse_encode(enc_spec, enc)) { fprintf(stderr, "musicue-render: --encode wants aac[:64-320[:fast|twoloop|anmr]]\n"); return 2; }
    if (!compress.empty()) {
        if (!enc.on) parse_encode("aac", enc);
        struct sigaction sc; memset(&sc, 0, sizeof sc); sc.sa_handler = on_term; sigaction(SIGTERM, &sc, nullptr); sigaction(SIGINT, &sc, nullptr);
        return compress_dir(compress, enc);
    }
    if (in.empty() || out.empty() || !(p.ratio >= 0.25 && p.ratio <= 4) || !(std::fabs(p.semitones) <= 24) || p.workers < 1 || p.workers > 16) {
        fprintf(stderr, "usage: musicue-render --in SONG --out COPY.wav --ratio R(0.25-4) --semitones S(-24..24) [--workers N] [--cheaper] [--no-resume]\n");
        return 2;
    }
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = on_term; sigaction(SIGTERM, &sa, nullptr); sigaction(SIGINT, &sa, nullptr);

    say("PROGRESS 0/1 Reading the song...");
    Song16 song;
    double max_s = 12 * 60;                              // MusiCue's own limit for a song (audio.h TRACK_MAX_SECONDS); $R3I6NOS_RENDER_MAX_S lowers it for tests
    if (const char *m = getenv("R3I6NOS_RENDER_MAX_S")) if (atof(m) > 0) max_s = atof(m);
    if (!read_wav16(in, song)) {
        std::string err;
        say("PROGRESS 0/1 Decoding the song...");
        if (!decode_with_ffmpeg(in, max_s, song, err)) { if (g_cancel.load()) return 130; say("ERROR %s", err.c_str()); return 1; }
    }
    if (song.frames() > (int64_t)(max_s * 44100.0)) { say("ERROR the song is longer than %.0f minutes; it cannot be copied", max_s / 60); return 1; }
    if (song.frames() < 44100 * 8) { say("ERROR the song is under 8 seconds"); return 1; }

    // room for the copy (and 16 MiB over), before anything is written
    {
        std::string dir = out.find('/') == std::string::npos ? "." : out.substr(0, out.rfind('/'));
        struct statvfs sv;
        const double need = (double)song.frames() / p.ratio * 4.0 * 1.02 + 16.0 * 1048576.0 + (enc.on ? (double)song.frames() / 44100.0 / p.ratio * enc.kbps * 125.0 : 0.0);
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
    if (r.resumed_from) say("PROGRESS %d/%d Resumed from segment %d", r.segments, r.segments, r.resumed_from);
    std::string format = "WAV";
    int64_t final_bytes = 0;
    if (!enc.on) {
        if (rename(part.c_str(), out.c_str()) != 0) { say("ERROR cannot rename the copy into place"); return 1; }
        unlink(journal.c_str());
        final_bytes = file_size(out);
    } else {
        if (getenv("R3I6NOS_RENDER_DIE_AFTER_RENDER")) _exit(9);                 // the tests: a power cut between the render and the encode
        say("PROGRESS 0/100 Compressing to AAC %dk...", enc.kbps);
        std::string err;
        const std::string tmp = out + ".enc";
        if (encode_aac(part, tmp, enc, r.frames, err)) {
            if (rename(tmp.c_str(), out.c_str()) != 0) { say("ERROR cannot rename the copy into place"); return 1; }
            unlink(part.c_str()); unlink(journal.c_str());
            final_bytes = file_size(out);
            char f[48]; snprintf(f, sizeof f, "AAC %dk", enc.kbps);
            format = f;
        } else {
            unlink(tmp.c_str());
            if (g_cancel.load()) return 130;                                      // the WAV and the done journal stay: the next run only encodes
            say("PROGRESS 0/1 Could not compress (%s): the copy is kept as a WAV", err.c_str());
            const std::string wav = (out.size() > 4 && out.compare(out.size() - 4, 4, ".m4a") == 0 ? out.substr(0, out.size() - 4) : out) + ".wav";
            if (rename(part.c_str(), wav.c_str()) != 0) { say("ERROR cannot keep the WAV"); return 1; }
            unlink(journal.c_str());
            final_bytes = file_size(wav);
        }
    }
    struct rusage ru; getrusage(RUSAGE_SELF, &ru);
    char note[160] = "";
    if (r.clipped) snprintf(note, sizeof note, "%d samples clipped; ", r.clipped);
    else if (r.lim.peak > LIMIT_CEILING)
        snprintf(note, sizeof note, "limited a %.2f peak: ducked %.1f%% of the time, deepest %.1f dB; ", r.lim.peak, 100.0 * (double)r.lim.ducked / (double)std::max<int64_t>(1, r.lim.frames), 20 * std::log10(r.lim.min_gain));
    say("DONE %s%s, %.1f MiB, %lld frames in %.1f s, peak memory %ld MiB", note, format.c_str(), (double)final_bytes / 1048576.0, (long long)r.frames, r.wall_s, ru.ru_maxrss / 1024);
    return 0;
}

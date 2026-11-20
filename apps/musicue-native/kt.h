/* kt.h: MusiCue's "Key and Tempo" arithmetic (header-only, MIT): what a song's Key and Tempo fields and a wanted Key and Tempo mean for the render
 * (docs/MUSICUE.md, "Key and Tempo: render a copy"; ROADMAP section 40). Pure functions, no screen and no files but the sidecar, so --selftest checks them.
 *
 *   key      "G", "F#m", "Bb", "Gm7", "e minor", "Cmaj7": a note name, an optional # or b, then the mode: m, min or minor is minor; anything else is major.
 *   semitones  the distance from the base key's root to the target's, the nearer way round (-5 to +6), the mode kept.
 *   ratio    target Tempo / base Tempo: 2 is twice as fast and half as long. Allowed 0.5 to 2; the sound is best between 0.75 and 1.5.
 *   a copy   a WAV in media/ named "<title> (<key> <tempo>bpm) [kt-xxxxxxxx].wav" (the id is made from the base id and the numbers, so asking for the
 *            same thing twice finds the copy that is there), with its own cue file (every time divided by the ratio) and a sidecar cues/<id>.kt. */
#ifndef MUSICUE_KT_H
#define MUSICUE_KT_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int root, minor, flat; } KtKey;       /* root 0-11 (C = 0); flat: the name was written with a b, so the copy's name is too */

static const char *const KT_SHARP[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const KT_FLAT[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};

/* 1 and the key, or 0 when the text is not a key. */
static int kt_parse_key(const char *s, KtKey *k) {
    while (*s == ' ') s++;
    static const int NOTE[7] = {9, 11, 0, 2, 4, 5, 7};          /* A B C D E F G */
    char c = *s;
    if (c >= 'a' && c <= 'g') c = (char)(c - 32);
    if (c < 'A' || c > 'G') return 0;
    k->root = NOTE[c - 'A']; k->minor = 0; k->flat = 0;
    s++;
    if (*s == '#') { k->root = (k->root + 1) % 12; s++; }
    else if (*s == 'b') { k->root = (k->root + 11) % 12; k->flat = 1; s++; }
    while (*s == ' ') s++;
    if (!strncmp(s, "maj", 3) || *s == 'M') return 1;            /* Cmaj7, CM7 */
    if (*s == 'm' || !strncmp(s, "min", 3) || !strncmp(s, "Min", 3)) k->minor = 1;
    return 1;
}

static void kt_key_name(const KtKey *k, char *out, size_t cap) {
    snprintf(out, cap, "%s%s", (k->flat ? KT_FLAT : KT_SHARP)[((k->root % 12) + 12) % 12], k->minor ? "m" : "");
}

/* The shift from one root to another, the nearer way: -5 to +6 (a tritone goes up). */
static int kt_semitones(int from, int to) {
    int d = (((to - from) % 12) + 12) % 12;
    return d > 6 ? d - 12 : d;
}

/* A Tempo field as a number: positive and finite, else 0 ("", "?" and words are not a tempo). */
static double kt_tempo(const char *s) {
    char *end;
    double t = strtod(s, &end);
    return end != s && isfinite(t) && t > 0 && t < 1000 ? t : 0;
}

typedef struct {
    KtKey base, target;
    double base_tempo, target_tempo, ratio;
    int semis;
} KtPlan;

/* Why a plan cannot be rendered, or NULL. `key` and `tempo` are the song's fields; the plan is filled in as far as they allow. */
static const char *kt_plan(KtPlan *p, const char *key, const char *tempo) {
    memset(p, 0, sizeof *p);
    if (!key[0]) return "This song has no Key. Set it in Metadata first.";
    if (!kt_parse_key(key, &p->base)) return "The song's Key is not a note (G, F#m, Bb ...). Fix it in Metadata.";
    p->base_tempo = kt_tempo(tempo);
    if (p->base_tempo <= 0) return "This song has no Tempo. Set it in Metadata first.";
    p->target = p->base; p->target_tempo = p->base_tempo; p->ratio = 1; p->semis = 0;
    return NULL;
}

/* Recomputes the numbers after the target changed: returns why it cannot be done (nothing to change, a tempo out of range), or NULL. */
static const char *kt_update(KtPlan *p) {
    p->semis = kt_semitones(p->base.root, p->target.root);
    p->ratio = p->target_tempo / p->base_tempo;
    if (p->ratio < 0.5 - 1e-9 || p->ratio > 2 + 1e-9) return "The tempo can go from half to double the song's.";
    if (p->semis == 0 && fabs(p->ratio - 1) < 1e-6) return "Nothing to change: pick another Key or Tempo.";
    return NULL;
}

/* The unit's measured speed (stretch-proto-report, 2026-11-20, the second and third reports, 60 s of audio, four Cortex-A35): times real time for 2 and 4 workers and each preset. */
static double kt_speed(int workers, int cheaper) {
    static const double X[2][2] = {{6.78, 10.63}, {10.70, 16.38}};   /* [cheaper][workers == 4] */
    return X[cheaper ? 1 : 0][workers >= 4 ? 1 : 0];
}
/* Seconds the render takes for a song of `song_s` seconds, with 25% over for reading, joining and writing. */
static double kt_estimate_s(double song_s, int workers, int cheaper) { return song_s / kt_speed(workers, cheaper) * 1.25 + 2; }
/* Bytes the copy takes: 16-bit stereo 44.1 kHz of song_s / ratio seconds as a WAV (10 MB a minute), or as AAC at 192 kbps in an M4A (1.4 MB a minute). */
#define KT_AAC_KBPS 192
static double kt_copy_bytes(double song_s, double ratio, int aac) { return aac ? song_s / ratio * (KT_AAC_KBPS * 125.0) + 4096 : song_s / ratio * 176400.0 + 44; }
/* Seconds the AAC encode after the render takes for a copy of copy_s seconds: the image's ffmpeg with the fast coder at 192 kbps ran at 4.7x real time on the unit
 * (stretch-proto-report's == encode == section, 2026-11-20; twoloop 1.5x, 128 kbps 5.9x, 256 kbps 1.1x), on one core, with a second added. */
#define KT_ENCODE_X 4.7
static double kt_encode_s(double copy_s) { return copy_s / KT_ENCODE_X + 1; }

/* The copy's id: "kt-" and eight hex digits made from the base id, the shift and the ratio (to a thousandth), so the same ask is the same copy. */
static void kt_copy_id(const char *base_id, int semis, double ratio, char *out, size_t cap) {
    char buf[96];
    snprintf(buf, sizeof buf, "%s|%d|%ld", base_id, semis, (long)floor(ratio * 1000 + 0.5));
    uint32_t h = 2166136261u;
    for (const char *q = buf; *q; q++) h = (h ^ (unsigned char)*q) * 16777619u;
    snprintf(out, cap, "kt-%08x", (unsigned)h);
}

static int kt_is_copy_id(const char *id) { return !strncmp(id, "kt-", 3); }

/* "Title (G 96bpm) [kt-xxxxxxxx]": the base's name without its own " [id]" and kept to 60 characters, then what the copy is. `tempo_text` is the target
 * Tempo as the Metadata shows it (96, 104.5). */
static void kt_copy_name(const char *base_name, const KtKey *target, const char *tempo_text, const char *id, char *out, size_t cap) {
    char title[64];
    snprintf(title, sizeof title, "%.60s", base_name);
    char *lb = strrchr(title, '[');
    if (lb && lb > title && lb[-1] == ' ' && strchr(lb, ']')) lb[-1] = 0;
    for (char *q = title; *q; q++) if (*q == '/' || *q == '\\') *q = '-';
    char key[8];
    kt_key_name(target, key, sizeof key);
    snprintf(out, cap, "%s (%s %sbpm) [%s]", title, key, tempo_text, id);
}

/* Every time in a cue set divided by the ratio (a faster song is shorter): marker times and caption starts and ends. Needs cuedata.h. */
#ifdef MUSICUE_CUEDATA_H
static void kt_scale_cues(CueSet *s, double ratio) {
    for (int i = 0; i < s->nm; i++) s->m[i].time /= ratio;
    for (int i = 0; i < s->nc; i++) { s->c[i].start /= ratio; if (s->c[i].has_end) s->c[i].end /= ratio; }
}
#endif

#endif

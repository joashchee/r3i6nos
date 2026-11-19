/* cuedata.h: MusiCue's reader and writer for Cue's files (header-only, MIT). Pure C, no SDL.
 *
 * Cue (https://cue.chordica.com/, the maintainer's web app) keeps, for a video, jump MARKERS (time, label) and synced CAPTIONS (start, end, text),
 * plus five fixed metadata fields. It exchanges them as:
 *   - the server file pair <videoId>.json ({"markers":[{id,time,key,label,noKey,capId?}]}) and <videoId>.srt (an SRT whose hidden metadata block sits
 *     between two "[chordica.com/cue]" cues at the start, one "key=value" cue each, 1 ms apart, then the real captions);
 *   - "version data" (what Cue saves and what a bundle holds): {scripter, created, savedAt, markers:[{time,label,noKey}],
 *     captionEntries:[{start,end,text}], metadata:[{key,value}]};
 *   - the .cuebundle: {type:"cue-bundle", bundleVersion:1, exportedAt, defaults, setlist:[{videoId,versionId,title,addedAt}],
 *     data:{<videoId>:{versions:{<versionId>:<version data>}, favorite, mine}}}.
 * MusiCue keeps ONE file a video, <app data>/cues/<videoId>.json, in the version-data shape, and writes the server pair and bundles on export.
 *
 * Cue's rules that travel with its data (CLAUDE.md of the Cue repo, rule 3): the metadata fields are FIXED (key, tempo, sig, scripter, created);
 * nothing here adds, renames or deletes one, whatever an imported file says; values are cut to their limits; `created` is only ever set by an
 * export (cue_meta_stamp) and must look like an ISO time or it is dropped. Marker keys are Cue's 33 slots (1-9, 0, d-z) in time order.
 *
 * Everything that comes from a file is untrusted: sizes and depth are capped, strings are cut, nothing is executed. Tested by --selftest. */
#ifndef MUSICUE_CUEDATA_H
#define MUSICUE_CUEDATA_H

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CUE_DELIM "[chordica.com/cue]"
#define CUE_MAX_FILE (4u << 20)     /* a cue file or bundle over 4 MiB is refused */
#define CUE_MAX_MARKERS 512
#define CUE_MAX_CAPTIONS 4096
#define CUE_JSON_DEPTH 24

/* ---- a small JSON reader (objects, arrays, strings, numbers, true, false, null) ------------------------------------------------------- */

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;
typedef struct JNode {
    JType t;
    double num;
    int b;
    char *s;
    struct JNode *kid;      /* array elements, or object values */
    char **key;             /* object keys, parallel to kid */
    int n, cap;
} JNode;

typedef struct { const char *p, *end; int depth; char err[96]; } JParser;

static void j_free(JNode *j) {
    if (!j) return;
    for (int i = 0; i < j->n; i++) {
        j_free(&j->kid[i]);
        if (j->key) free(j->key[i]);
    }
    free(j->kid);
    free(j->key);
    free(j->s);
    memset(j, 0, sizeof *j);
}

static void j_ws(JParser *q) { while (q->p < q->end && (*q->p == ' ' || *q->p == '\t' || *q->p == '\n' || *q->p == '\r')) q->p++; }
static int j_fail(JParser *q, const char *why) { if (!q->err[0]) snprintf(q->err, sizeof q->err, "%s", why); return 0; }

static void j_utf8(char *out, int *n, unsigned cp) {
    if (cp < 0x80) out[(*n)++] = (char)cp;
    else if (cp < 0x800) { out[(*n)++] = (char)(0xC0 | cp >> 6); out[(*n)++] = (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { out[(*n)++] = (char)(0xE0 | cp >> 12); out[(*n)++] = (char)(0x80 | (cp >> 6 & 63)); out[(*n)++] = (char)(0x80 | (cp & 63)); }
    else { out[(*n)++] = (char)(0xF0 | cp >> 18); out[(*n)++] = (char)(0x80 | (cp >> 12 & 63)); out[(*n)++] = (char)(0x80 | (cp >> 6 & 63)); out[(*n)++] = (char)(0x80 | (cp & 63)); }
}

static int j_hex4(JParser *q, unsigned *v) {
    if (q->end - q->p < 4) return j_fail(q, "cut \\u escape");
    unsigned x = 0;
    for (int i = 0; i < 4; i++) {
        int c = (unsigned char)q->p[i];
        x = x * 16 + (unsigned)(c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1000);
        if (x > 0xFFFFFu) return j_fail(q, "bad \\u escape");
    }
    q->p += 4;
    *v = x;
    return 1;
}

static int j_string(JParser *q, char **out) {
    q->p++;                                  /* the opening quote */
    size_t cap = 32, n = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return j_fail(q, "out of memory");
    while (q->p < q->end && *q->p != '"') {
        if (n + 8 >= cap) { cap *= 2; char *nb = (char *)realloc(buf, cap); if (!nb) { free(buf); return j_fail(q, "out of memory"); } buf = nb; }
        unsigned char c = (unsigned char)*q->p++;
        if (c == '\\') {
            if (q->p >= q->end) { free(buf); return j_fail(q, "cut string"); }
            char e = *q->p++;
            int k = (int)n;
            switch (e) {
            case 'n': buf[k++] = '\n'; break;
            case 't': buf[k++] = '\t'; break;
            case 'r': buf[k++] = '\r'; break;
            case 'b': buf[k++] = '\b'; break;
            case 'f': buf[k++] = '\f'; break;
            case 'u': {
                unsigned cp;
                if (!j_hex4(q, &cp)) { free(buf); return 0; }
                if (cp >= 0xD800 && cp < 0xDC00 && q->end - q->p >= 6 && q->p[0] == '\\' && q->p[1] == 'u') {
                    const char *save = q->p;
                    q->p += 2;
                    unsigned lo;
                    if (j_hex4(q, &lo) && lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    else { q->p = save; q->err[0] = 0; cp = '?'; }
                } else if (cp >= 0xD800 && cp < 0xE000) cp = '?';
                j_utf8(buf, &k, cp);
                break;
            }
            default: buf[k++] = e;           /* \" \\ \/ and anything else stands for itself */
            }
            n = (size_t)k;
        } else buf[n++] = (char)c;
    }
    if (q->p >= q->end) { free(buf); return j_fail(q, "cut string"); }
    q->p++;
    buf[n] = 0;
    *out = buf;
    return 1;
}

static int j_value(JParser *q, JNode *out);

static int j_push(JNode *j, JNode *kid, char *key) {
    if (j->n == j->cap) {
        int nc = j->cap ? j->cap * 2 : 4;
        JNode *nk = (JNode *)realloc(j->kid, (size_t)nc * sizeof *nk);
        if (!nk) return 0;
        j->kid = nk;
        if (j->t == J_OBJ) {
            char **nkey = (char **)realloc(j->key, (size_t)nc * sizeof *nkey);
            if (!nkey) return 0;
            j->key = nkey;
        }
        j->cap = nc;
    }
    j->kid[j->n] = *kid;
    if (j->t == J_OBJ) j->key[j->n] = key;
    j->n++;
    return 1;
}

static int j_value(JParser *q, JNode *out) {
    memset(out, 0, sizeof *out);
    j_ws(q);
    if (q->p >= q->end) return j_fail(q, "unexpected end");
    if (++q->depth > CUE_JSON_DEPTH) return j_fail(q, "nested too deep");
    int ok = 1;
    char c = *q->p;
    if (c == '{' || c == '[') {
        out->t = c == '{' ? J_OBJ : J_ARR;
        char close = c == '{' ? '}' : ']';
        q->p++;
        j_ws(q);
        if (q->p < q->end && *q->p == close) q->p++;
        else for (;;) {
            char *key = NULL;
            j_ws(q);
            if (out->t == J_OBJ) {
                if (q->p >= q->end || *q->p != '"') { ok = j_fail(q, "object key expected"); break; }
                if (!j_string(q, &key)) { ok = 0; break; }
                j_ws(q);
                if (q->p >= q->end || *q->p != ':') { free(key); ok = j_fail(q, "':' expected"); break; }
                q->p++;
            }
            JNode kid;
            if (!j_value(q, &kid)) { free(key); ok = 0; break; }
            if (!j_push(out, &kid, key)) { j_free(&kid); free(key); ok = j_fail(q, "out of memory"); break; }
            j_ws(q);
            if (q->p < q->end && *q->p == ',') { q->p++; continue; }
            if (q->p < q->end && *q->p == close) { q->p++; break; }
            ok = j_fail(q, "',' or closing bracket expected");
            break;
        }
    } else if (c == '"') {
        out->t = J_STR;
        ok = j_string(q, &out->s);
    } else if (!strncmp(q->p, "true", q->end - q->p >= 4 ? 4 : 99)) { out->t = J_BOOL; out->b = 1; q->p += 4; }
    else if (!strncmp(q->p, "false", q->end - q->p >= 5 ? 5 : 99)) { out->t = J_BOOL; q->p += 5; }
    else if (!strncmp(q->p, "null", q->end - q->p >= 4 ? 4 : 99)) { out->t = J_NULL; q->p += 4; }
    else if (c == '-' || (c >= '0' && c <= '9')) {
        char tok[40];
        size_t k = 0;
        while (q->p < q->end && k < sizeof tok - 1 && strchr("+-.eE0123456789", *q->p)) tok[k++] = *q->p++;
        tok[k] = 0;
        char *endp;
        out->t = J_NUM;
        out->num = strtod(tok, &endp);
        if (endp == tok || *endp || !isfinite(out->num)) ok = j_fail(q, "bad number");
    } else ok = j_fail(q, "unexpected character");
    q->depth--;
    if (!ok) j_free(out);
    return ok;
}

/* Parse text into *out (the caller j_free's it). 1 on success; on failure err (if not NULL, 96 bytes) says why. */
static int j_parse(const char *text, size_t len, JNode *out, char *err) {
    JParser q = {text, text + len, 0, {0}};
    int ok = j_value(&q, out);
    if (ok) { j_ws(&q); if (q.p != q.end) { j_free(out); ok = j_fail(&q, "text after the value"); } }
    if (!ok && err) snprintf(err, 96, "%s", q.err[0] ? q.err : "not JSON");
    return ok;
}

static const JNode *j_get(const JNode *o, const char *key) {
    if (!o || o->t != J_OBJ) return NULL;
    for (int i = 0; i < o->n; i++) if (!strcmp(o->key[i], key)) return &o->kid[i];
    return NULL;
}
static const char *j_str(const JNode *n) { return n && n->t == J_STR && n->s ? n->s : NULL; }
static double j_num(const JNode *n, double dflt) { return n && n->t == J_NUM ? n->num : dflt; }

/* ---- the cue data ---------------------------------------------------------------------------------------------------------------------- */

typedef struct { double time; char label[64]; int nokey; char key[4]; } CueMarker;
typedef struct { double start, end; int has_end; char text[256]; } CueCaption;
typedef struct { char key[41], tempo[13], sig[13], scripter[61], created[31]; } CueMeta;   /* "" is not set */

typedef struct {
    char videoid[16];
    CueMarker *m; int nm, capm;
    CueCaption *c; int nc, capc;
    CueMeta meta;
    int dirty;
} CueSet;

static void cue_init(CueSet *s) { memset(s, 0, sizeof *s); }
static void cue_free(CueSet *s) { free(s->m); free(s->c); memset(s, 0, sizeof *s); }
static void cue_clear(CueSet *s) { s->nm = s->nc = 0; memset(&s->meta, 0, sizeof s->meta); }

/* The fixed metadata fields, in Cue's order, and their limits (METADATA_SCHEMA in Cue). */
static const struct { const char *key; int max; } cue_fields[] = {{"key", 40}, {"tempo", 12}, {"sig", 12}, {"scripter", 60}, {"created", 30}};

static char *cue_meta_slot(CueMeta *m, const char *key) {
    if (!strcmp(key, "key")) return m->key;
    if (!strcmp(key, "tempo")) return m->tempo;
    if (!strcmp(key, "sig")) return m->sig;
    if (!strcmp(key, "scripter")) return m->scripter;
    if (!strcmp(key, "created")) return m->created;
    return NULL;
}

/* `created` must look like 2026-09-21T08:07:26Z or 2026-09-21T08:07:26.634Z (Cue's CREATED_RE: one to three fraction digits). */
static int cue_created_ok(const char *v) {
    static const char shape[] = "dddd-dd-ddTdd:dd:dd";
    size_t i = 0;
    for (; shape[i]; i++) {
        if (shape[i] == 'd' ? !isdigit((unsigned char)v[i]) : v[i] != shape[i]) return 0;
    }
    if (v[i] == '.') {
        int digits = 0;
        for (i++; isdigit((unsigned char)v[i]); i++) digits++;
        if (digits < 1 || digits > 3) return 0;
    }
    return v[i] == 'Z' && v[i + 1] == 0;
}

/* Set a metadata field from untrusted text: unknown fields are ignored (returns 0), control characters become spaces, the value is cut to its limit
 * on a character boundary, and a `created` that is not an ISO time is dropped. */
static int cue_meta_set(CueSet *s, const char *key, const char *value) {
    char *slot = cue_meta_slot(&s->meta, key);
    if (!slot) return 0;
    int max = 0;
    for (size_t i = 0; i < sizeof cue_fields / sizeof *cue_fields; i++) if (!strcmp(cue_fields[i].key, key)) max = cue_fields[i].max;
    char tmp[128];
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)value; *p && n < sizeof tmp - 1; p++) tmp[n++] = (char)(*p < 32 || *p == 127 ? ' ' : *p);
    while (n > 0 && tmp[n - 1] == ' ') n--;
    size_t start = 0;
    while (start < n && tmp[start] == ' ') start++;
    memmove(tmp, tmp + start, n - start);
    n -= start;
    if ((int)n > max) {
        n = (size_t)max;
        while (n > 0 && ((unsigned char)tmp[n] & 0xC0) == 0x80) n--;      /* not in the middle of a UTF-8 character */
    }
    tmp[n] = 0;
    if (!strcmp(key, "created") && n && !cue_created_ok(tmp)) tmp[0] = 0;
    snprintf(slot, (size_t)max + 1, "%s", tmp);
    s->dirty = 1;
    return 1;
}

/* The export stamp: only an export sets `created`. */
static void cue_meta_stamp(CueSet *s, time_t now) {
    char buf[40];
    struct tm tm;
    gmtime_r(&now, &tm);
    strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S.000Z", &tm);
    cue_meta_set(s, "created", buf);
}

static int cue_marker_cmp(const void *a, const void *b) {
    double x = ((const CueMarker *)a)->time, y = ((const CueMarker *)b)->time;
    return x < y ? -1 : x > y;
}

/* Cue's 33 key slots: 1-9, 0, then d-z (a, b and c are the temporary loops), in time order; a marker with noKey has none. */
static void cue_assign_keys(CueSet *s) {
    static const char slots[] = "1234567890defghijklmnopqrstuvwxyz";
    int next = 0;
    for (int i = 0; i < s->nm; i++) {
        s->m[i].key[0] = 0;
        if (s->m[i].nokey || next >= (int)sizeof slots - 1) continue;
        s->m[i].key[0] = slots[next++];
        s->m[i].key[1] = 0;
    }
}

static void cue_sort(CueSet *s) {
    if (s->nm > 1) qsort(s->m, (size_t)s->nm, sizeof *s->m, cue_marker_cmp);
    for (int i = 1; i < s->nc; i++) {       /* captions: insertion sort by start (stable, and they are nearly sorted) */
        CueCaption x = s->c[i];
        int j = i - 1;
        while (j >= 0 && s->c[j].start > x.start) { s->c[j + 1] = s->c[j]; j--; }
        s->c[j + 1] = x;
    }
    cue_assign_keys(s);
}

/* src into dst (cap bytes): control characters other than newline become spaces, and a cut never lands inside a UTF-8 character. */
static void cue_clean_text(char *dst, size_t cap, const char *src) {
    size_t len = strlen(src), take = len < cap - 1 ? len : cap - 1;
    if (take < len) while (take > 0 && ((unsigned char)src[take] & 0xC0) == 0x80) take--;
    for (size_t i = 0; i < take; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[i] = (char)((c < 32 && c != '\n') || c == 127 ? ' ' : c);
    }
    dst[take] = 0;
}

static int cue_add_marker(CueSet *s, double time, const char *label, int nokey) {
    if (s->nm >= CUE_MAX_MARKERS || !isfinite(time) || time < 0 || time > 86400) return -1;
    if (s->nm == s->capm) {
        int nc = s->capm ? s->capm * 2 : 16;
        CueMarker *nm = (CueMarker *)realloc(s->m, (size_t)nc * sizeof *nm);
        if (!nm) return -1;
        s->m = nm;
        s->capm = nc;
    }
    CueMarker *m = &s->m[s->nm];
    memset(m, 0, sizeof *m);
    m->time = time;
    m->nokey = nokey ? 1 : 0;
    cue_clean_text(m->label, sizeof m->label, label ? label : "");
    s->dirty = 1;
    return s->nm++;
}

static int cue_add_caption(CueSet *s, double start, int has_end, double end, const char *text) {
    if (s->nc >= CUE_MAX_CAPTIONS || !isfinite(start) || start < 0 || start > 86400) return -1;
    if (s->nc == s->capc) {
        int nc = s->capc ? s->capc * 2 : 32;
        CueCaption *nm = (CueCaption *)realloc(s->c, (size_t)nc * sizeof *nm);
        if (!nm) return -1;
        s->c = nm;
        s->capc = nc;
    }
    CueCaption *c = &s->c[s->nc];
    memset(c, 0, sizeof *c);
    c->start = start;
    c->has_end = has_end && isfinite(end) && end >= start;
    c->end = c->has_end ? end : start;
    cue_clean_text(c->text, sizeof c->text, text ? text : "");
    s->dirty = 1;
    return s->nc++;
}

static void cue_del_marker(CueSet *s, int i) {
    if (i < 0 || i >= s->nm) return;
    memmove(&s->m[i], &s->m[i + 1], (size_t)(s->nm - i - 1) * sizeof *s->m);
    s->nm--;
    cue_assign_keys(s);
    s->dirty = 1;
}

/* Version data / server markers file (a JSON object) into the set: markers, captionEntries, metadata, scripter, created. Existing markers and
 * captions are kept unless `replace`. Returns 1, or 0 with err. */
static int cue_load_json_node(CueSet *s, const JNode *root, int replace) {
    if (!root || root->t != J_OBJ) return 0;
    if (replace) cue_clear(s);
    const JNode *mk = j_get(root, "markers");
    if (mk && mk->t == J_ARR)
        for (int i = 0; i < mk->n; i++) {
            const JNode *m = &mk->kid[i];
            if (m->t != J_OBJ) continue;
            const JNode *nk = j_get(m, "noKey");
            cue_add_marker(s, j_num(j_get(m, "time"), -1), j_str(j_get(m, "label")), nk && nk->t == J_BOOL && nk->b);
        }
    const JNode *ce = j_get(root, "captionEntries");
    if (ce && ce->t == J_ARR)
        for (int i = 0; i < ce->n; i++) {
            const JNode *c = &ce->kid[i];
            if (c->t != J_OBJ) continue;
            const JNode *en = j_get(c, "end");
            cue_add_caption(s, j_num(j_get(c, "start"), -1), en && en->t == J_NUM, j_num(en, 0), j_str(j_get(c, "text")));
        }
    const JNode *md = j_get(root, "metadata");
    if (md && md->t == J_ARR)
        for (int i = 0; i < md->n; i++) {
            const char *k = j_str(j_get(&md->kid[i], "key")), *v = j_str(j_get(&md->kid[i], "value"));
            if (k && v) cue_meta_set(s, k, v);
        }
    if (!s->meta.scripter[0] && j_str(j_get(root, "scripter"))) cue_meta_set(s, "scripter", j_str(j_get(root, "scripter")));
    if (!s->meta.created[0] && j_str(j_get(root, "created"))) cue_meta_set(s, "created", j_str(j_get(root, "created")));
    cue_sort(s);
    return 1;
}

static int cue_load_json(CueSet *s, const char *text, size_t len, int replace, char *err) {
    JNode root;
    if (len > CUE_MAX_FILE) { if (err) snprintf(err, 96, "too big"); return 0; }
    if (!j_parse(text, len, &root, err)) return 0;
    int ok = cue_load_json_node(s, &root, replace);
    if (!ok && err) snprintf(err, 96, "not a cue file");
    j_free(&root);
    return ok;
}

/* ---- SRT / WebVTT ---------------------------------------------------------------------------------------------------------------------- */

/* "00:01:02,345", "01:02.345" or "1:02:03.5" as seconds, or -1. */
static double cue_time(const char *s, const char **endp) {
    int f[3] = {0, 0, 0}, nf = 0;
    const char *p = s;
    while (isspace((unsigned char)*p)) p++;
    for (;;) {
        if (!isdigit((unsigned char)*p)) return -1;
        long v = 0;
        while (isdigit((unsigned char)*p)) { v = v * 10 + (*p - '0'); if (v > 99999) return -1; p++; }
        if (nf < 3) f[nf++] = (int)v;
        if (*p == ':' && nf < 3) { p++; continue; }
        break;
    }
    double sec;
    if (nf == 3) sec = f[0] * 3600.0 + f[1] * 60.0 + f[2];
    else if (nf == 2) sec = f[0] * 60.0 + f[1];
    else sec = f[0];
    if (*p == ',' || *p == '.') {
        p++;
        double scale = 0.1, frac = 0;
        while (isdigit((unsigned char)*p)) { frac += (*p - '0') * scale; scale /= 10; p++; }
        sec += frac;
    }
    if (endp) *endp = p;
    return sec;
}

static void cue_fmt_time(double t, char *out, size_t cap) {      /* 00:01:02,345 */
    if (t < 0) t = 0;
    long ms = (long)llround(t * 1000.0);
    snprintf(out, cap, "%02ld:%02ld:%02ld,%03ld", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000);
}

/* Captions from SRT or WebVTT text, with Cue's hidden metadata block pulled out into the metadata (and kept out of the captions). Existing
 * captions are replaced. Returns the number of captions. */
static int cue_load_srt(CueSet *s, const char *text, size_t len, char *err) {
    if (len > CUE_MAX_FILE) { if (err) snprintf(err, 96, "too big"); return -1; }
    char *buf = (char *)malloc(len + 1);
    if (!buf) return -1;
    size_t n = 0;
    for (size_t i = 0; i < len; i++) if (text[i] != '\r') buf[n++] = text[i];
    buf[n] = 0;
    struct E { double start, end; int has_end; char text[256]; } *e = NULL;
    int ne = 0, cap = 0;
    char *p = buf;
    while (*p) {
        char *blank = strstr(p, "\n\n");
        size_t blen = blank ? (size_t)(blank - p) : strlen(p);
        char *block = (char *)malloc(blen + 1);
        if (!block) break;
        memcpy(block, p, blen);
        block[blen] = 0;
        p += blen + (blank ? 2 : 0);
        while (*p == '\n') p++;
        char *line = block, *arrow = NULL;
        for (int guard = 0; line && guard < 3; guard++) {          /* an index line, a WEBVTT line, then the time line */
            char *nl = strchr(line, '\n');
            size_t ll = nl ? (size_t)(nl - line) : strlen(line);
            int found = 0;
            for (size_t k = 0; k + 3 <= ll; k++) if (!memcmp(line + k, "-->", 3)) { found = 1; break; }
            if (found) { arrow = line; break; }
            if (!nl) break;
            line = nl + 1;
        }
        if (arrow) {
            const char *a;
            double st = cue_time(arrow, &a), en = -1;
            const char *ar = strstr(arrow, "-->");
            if (ar) en = cue_time(ar + 3, NULL);
            char *tx = strchr(arrow, '\n');
            if (st >= 0 && tx) {
                tx++;
                for (char *q = tx; *q; q++) if (*q == '\n') *q = ' ';       /* a cue is one line here, as Cue's panel shows it */
                if (ne == cap) { cap = cap ? cap * 2 : 64; struct E *ne2 = (struct E *)realloc(e, (size_t)cap * sizeof *e); if (!ne2) { free(block); break; } e = ne2; }
                e[ne].start = st; e[ne].end = en >= st ? en : st; e[ne].has_end = en >= st;
                cue_clean_text(e[ne].text, sizeof e[ne].text, tx);
                ne++;
            }
        }
        free(block);
    }
    free(buf);
    s->nc = 0;
    int open = -1, close = -1;
    for (int i = 0; i < ne; i++) {
        char t[64];
        snprintf(t, sizeof t, "%s", e[i].text);
        size_t tl = strlen(t);
        while (tl && t[tl - 1] == ' ') t[--tl] = 0;
        if (!strcmp(t, CUE_DELIM)) { if (open < 0) open = i; else { close = i; break; } }
    }
    for (int i = 0; i < ne; i++) {
        if (open >= 0 && close >= 0 && i >= open && i <= close) {
            if (i > open && i < close) {
                char *eq = strchr(e[i].text, '=');
                if (eq) { *eq = 0; cue_meta_set(s, e[i].text, eq + 1); }
            }
            continue;
        }
        cue_add_caption(s, e[i].start, e[i].has_end, e[i].end, e[i].text);
    }
    free(e);
    cue_sort(s);
    return s->nc;
}

/* ---- writing --------------------------------------------------------------------------------------------------------------------------- */

typedef struct { char *p; size_t n, cap; int bad; } CueBuf;
static void cb_put(CueBuf *b, const char *s, size_t len) {
    if (b->bad) return;
    if (b->n + len + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 256;
        while (nc < b->n + len + 1) nc *= 2;
        char *np = (char *)realloc(b->p, nc);
        if (!np) { b->bad = 1; return; }
        b->p = np;
        b->cap = nc;
    }
    memcpy(b->p + b->n, s, len);
    b->n += len;
    b->p[b->n] = 0;
}
static void cb_str(CueBuf *b, const char *s) { cb_put(b, s, strlen(s)); }
static void cb_fmt(CueBuf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void cb_fmt(CueBuf *b, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n > 0) cb_put(b, tmp, (size_t)(n < (int)sizeof tmp ? n : (int)sizeof tmp - 1));
}
static void cb_json_str(CueBuf *b, const char *s) {
    cb_str(b, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') { char t[3] = {'\\', (char)*p, 0}; cb_str(b, t); }
        else if (*p == '\n') cb_str(b, "\\n");
        else if (*p < 32) cb_fmt(b, "\\u%04x", *p);
        else cb_put(b, (const char *)p, 1);
    }
    cb_str(b, "\"");
}

/* The set as version data (MusiCue's own file, and what a bundle holds). */
static void cue_write_version(const CueSet *s, CueBuf *b, time_t now) {
    char saved[40];
    struct tm tm;
    gmtime_r(&now, &tm);
    strftime(saved, sizeof saved, "%Y-%m-%dT%H:%M:%S.000Z", &tm);
    cb_str(b, "{\n  \"scripter\": "); cb_json_str(b, s->meta.scripter[0] ? s->meta.scripter : "Anonymous");
    cb_str(b, ",\n  \"created\": "); cb_json_str(b, s->meta.created);
    cb_str(b, ",\n  \"savedAt\": "); cb_json_str(b, saved);
    cb_str(b, ",\n  \"markers\": [");
    for (int i = 0; i < s->nm; i++) {
        cb_str(b, i ? ",\n    {" : "\n    {");
        cb_fmt(b, "\"time\": %.3f, \"label\": ", s->m[i].time); cb_json_str(b, s->m[i].label);
        cb_fmt(b, ", \"noKey\": %s}", s->m[i].nokey ? "true" : "false");
    }
    cb_str(b, s->nm ? "\n  ],\n  \"captionEntries\": [" : "],\n  \"captionEntries\": [");
    for (int i = 0; i < s->nc; i++) {
        cb_str(b, i ? ",\n    {" : "\n    {");
        cb_fmt(b, "\"start\": %.3f", s->c[i].start);
        if (s->c[i].has_end) cb_fmt(b, ", \"end\": %.3f", s->c[i].end);
        cb_str(b, ", \"text\": "); cb_json_str(b, s->c[i].text);
        cb_str(b, "}");
    }
    cb_str(b, s->nc ? "\n  ],\n  \"metadata\": [" : "],\n  \"metadata\": [");
    int first = 1;
    for (size_t i = 0; i < sizeof cue_fields / sizeof *cue_fields; i++) {
        const char *v = cue_meta_slot((CueMeta *)&s->meta, cue_fields[i].key);
        if (!v || !v[0]) continue;
        cb_str(b, first ? "\n    {" : ",\n    {");
        first = 0;
        cb_str(b, "\"key\": "); cb_json_str(b, cue_fields[i].key); cb_str(b, ", \"value\": "); cb_json_str(b, v); cb_str(b, "}");
    }
    cb_str(b, first ? "]\n}\n" : "\n  ]\n}\n");
}

/* The server markers file ("Export markers"). */
static void cue_write_server_json(const CueSet *s, CueBuf *b) {
    cb_str(b, "{\n  \"markers\": [");
    for (int i = 0; i < s->nm; i++) {
        cb_str(b, i ? ",\n    {" : "\n    {");
        cb_fmt(b, "\"id\": %d, \"time\": %.3f, ", i + 1, s->m[i].time);
        if (s->m[i].key[0]) { cb_str(b, "\"key\": "); cb_json_str(b, s->m[i].key); cb_str(b, ", "); }
        cb_str(b, "\"label\": "); cb_json_str(b, s->m[i].label);
        cb_fmt(b, ", \"noKey\": %s}", s->m[i].nokey ? "true" : "false");
    }
    cb_str(b, s->nm ? "\n  ]\n}\n" : "]\n}\n");
}

/* Cue's .srt: the hidden metadata block (the opening delimiter at 0, one field a cue 1 ms apart, a closing delimiter held up to 5 s or until the
 * first real caption), then the captions. Only the fixed fields that have a value are written. */
static void cue_write_srt(const CueSet *s, CueBuf *b) {
    int idx = 1, nf = 0;
    char a[32], z[32];
    const char *vals[8], *names[8];
    for (size_t i = 0; i < sizeof cue_fields / sizeof *cue_fields; i++) {
        const char *v = cue_meta_slot((CueMeta *)&s->meta, cue_fields[i].key);
        if (v && v[0]) { names[nf] = cue_fields[i].key; vals[nf++] = v; }
    }
    if (nf) {
        cue_fmt_time(0, a, sizeof a); cue_fmt_time(0.001, z, sizeof z);
        cb_fmt(b, "%d\n%s --> %s\n%s\n\n", idx++, a, z, CUE_DELIM);
        for (int i = 0; i < nf; i++) {
            cue_fmt_time((i + 1) / 1000.0, a, sizeof a); cue_fmt_time((i + 2) / 1000.0, z, sizeof z);
            cb_fmt(b, "%d\n%s --> %s\n%s=%s\n\n", idx++, a, z, names[i], vals[i]);
        }
        double cs = (nf + 1) / 1000.0, ce = cs + 5;
        if (s->nc && s->c[0].start < ce) ce = s->c[0].start;
        cue_fmt_time(cs, a, sizeof a); cue_fmt_time(ce, z, sizeof z);
        cb_fmt(b, "%d\n%s --> %s\n%s\n\n", idx++, a, z, CUE_DELIM);
    }
    for (int i = 0; i < s->nc; i++) {
        cue_fmt_time(s->c[i].start, a, sizeof a);
        cue_fmt_time(s->c[i].has_end ? s->c[i].end : (i + 1 < s->nc ? s->c[i + 1].start : s->c[i].start + 3), z, sizeof z);
        cb_fmt(b, "%d\n%s --> %s\n%s\n\n", idx++, a, z, s->c[i].text);
    }
}

/* ---- bundles --------------------------------------------------------------------------------------------------------------------------- */

typedef struct { char videoid[16]; char title[96]; CueSet set; } CueBundleItem;
typedef struct { CueBundleItem *it; int n; } CueBundle;

static int cue_videoid_ok(const char *id) {
    size_t n = strlen(id);
    if (n < 1 || n > 15) return 0;
    for (size_t i = 0; i < n; i++) if (!isalnum((unsigned char)id[i]) && id[i] != '_' && id[i] != '-') return 0;
    return 1;
}

static void cue_bundle_free(CueBundle *b) {
    for (int i = 0; i < b->n; i++) cue_free(&b->it[i].set);
    free(b->it);
    memset(b, 0, sizeof *b);
}

/* A .cuebundle (or a plain version-data .json backup, which has no "data") into one item for each video: the favorite version, else "mine", else
 * the first. Video ids that are not 1-15 letters, digits, '_' or '-' are skipped (they become file names). Returns the count, or -1 with err. */
static int cue_load_bundle(CueBundle *out, const char *text, size_t len, char *err) {
    memset(out, 0, sizeof *out);
    if (len > CUE_MAX_FILE) { if (err) snprintf(err, 96, "too big"); return -1; }
    JNode root;
    if (!j_parse(text, len, &root, err)) return -1;
    const JNode *data = j_get(&root, "data"), *setlist = j_get(&root, "setlist");
    if (!data || data->t != J_OBJ) { j_free(&root); if (err) snprintf(err, 96, "not a cue bundle"); return -1; }
    out->it = (CueBundleItem *)calloc((size_t)(data->n ? data->n : 1), sizeof *out->it);
    if (!out->it) { j_free(&root); return -1; }
    for (int i = 0; i < data->n; i++) {
        const char *vid = data->key[i];
        const JNode *v = &data->kid[i], *vers = j_get(v, "versions");
        if (!cue_videoid_ok(vid) || !vers || vers->t != J_OBJ || vers->n == 0) continue;
        const char *want = j_str(j_get(v, "favorite"));
        if (!want) want = j_str(j_get(v, "mine"));
        const JNode *pick = want ? j_get(vers, want) : NULL;
        if (!pick) pick = &vers->kid[0];
        CueBundleItem *it = &out->it[out->n];
        cue_init(&it->set);
        snprintf(it->videoid, sizeof it->videoid, "%s", vid);
        snprintf(it->set.videoid, sizeof it->set.videoid, "%s", vid);
        if (!cue_load_json_node(&it->set, pick, 1)) { cue_free(&it->set); continue; }
        if (setlist && setlist->t == J_ARR)
            for (int k = 0; k < setlist->n; k++)
                if (j_str(j_get(&setlist->kid[k], "videoId")) && !strcmp(j_str(j_get(&setlist->kid[k], "videoId")), vid) && j_str(j_get(&setlist->kid[k], "title")))
                    cue_clean_text(it->title, sizeof it->title, j_str(j_get(&setlist->kid[k], "title")));
        it->set.dirty = 0;
        out->n++;
    }
    j_free(&root);
    return out->n;
}

/* A bundle holding these sets, to send back to Cue on a phone or computer. */
static void cue_write_bundle(const CueSet *const *sets, const char *const *titles, int n, CueBuf *b, time_t now) {
    char at[40];
    struct tm tm;
    gmtime_r(&now, &tm);
    strftime(at, sizeof at, "%Y-%m-%dT%H:%M:%S.000Z", &tm);
    cb_str(b, "{\n  \"type\": \"cue-bundle\",\n  \"bundleVersion\": 1,\n  \"exportedAt\": "); cb_json_str(b, at);
    cb_str(b, ",\n  \"defaults\": {},\n  \"setlist\": [");
    for (int i = 0; i < n; i++) {
        cb_str(b, i ? ",\n    {" : "\n    {");
        cb_str(b, "\"videoId\": "); cb_json_str(b, sets[i]->videoid);
        cb_str(b, ", \"versionId\": \"musicue\", \"title\": "); cb_json_str(b, titles && titles[i] ? titles[i] : sets[i]->videoid);
        cb_fmt(b, ", \"addedAt\": %lld}", (long long)now * 1000);
    }
    cb_str(b, n ? "\n  ],\n  \"data\": {" : "],\n  \"data\": {");
    for (int i = 0; i < n; i++) {
        cb_str(b, i ? ",\n    " : "\n    ");
        cb_json_str(b, sets[i]->videoid);
        cb_str(b, ": {\"versions\": {\"musicue\": ");
        cue_write_version(sets[i], b, now);
        if (b->n && b->p[b->n - 1] == '\n') b->n--, b->p[b->n] = 0;
        cb_str(b, "}, \"favorite\": \"musicue\", \"mine\": \"musicue\"}");
    }
    cb_str(b, n ? "\n  }\n}\n" : "}\n}\n");
}

/* ---- files ----------------------------------------------------------------------------------------------------------------------------- */

static char *cue_read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = NULL;
    size_t n = 0, cap = 0;
    for (;;) {
        if (n + 4097 > cap) { cap = cap ? cap * 2 : 8192; char *nb = (char *)realloc(buf, cap); if (!nb) { free(buf); fclose(f); return NULL; } buf = nb; }
        size_t got = fread(buf + n, 1, 4096, f);
        n += got;
        if (got < 4096) break;
        if (n > CUE_MAX_FILE) { free(buf); fclose(f); return NULL; }
    }
    fclose(f);
    buf[n] = 0;
    if (len) *len = n;
    return buf;
}

/* Write a file by writing path.part and renaming it over: a power cut leaves the old file or the new one. */
static int cue_write_file(const char *path, const char *data, size_t len) {
    char tmp[PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.part", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    int ok = fwrite(data, 1, len, f) == len;
    ok = fflush(f) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp, path) != 0) { remove(tmp); return -1; }
    return 0;
}

#endif /* MUSICUE_CUEDATA_H */

/* fmt_media.h: details of pictures, sound, music modules and video for Commander's Info page (included by formats.h). */
#ifndef COMMANDER_FMT_MEDIA_H
#define COMMANDER_FMT_MEDIA_H

#include <math.h>

/* ---- text encodings into UTF-8 -------------------------------------------------------------------------------------------------- */

static size_t utf8_put(char *out, size_t cap, size_t k, unsigned cp) {
    char b[4];
    int n;
    if (cp < 0x80) { b[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { b[0] = (char)(0xC0 | cp >> 6); b[1] = (char)(0x80 | (cp & 63)); n = 2; }
    else if (cp < 0x10000) { b[0] = (char)(0xE0 | cp >> 12); b[1] = (char)(0x80 | (cp >> 6 & 63)); b[2] = (char)(0x80 | (cp & 63)); n = 3; }
    else { b[0] = (char)(0xF0 | cp >> 18); b[1] = (char)(0x80 | (cp >> 12 & 63)); b[2] = (char)(0x80 | (cp >> 6 & 63)); b[3] = (char)(0x80 | (cp & 63)); n = 4; }
    if (k + (size_t)n >= cap) return k;
    memcpy(out + k, b, (size_t)n);
    return k + (size_t)n;
}

static void latin1_to_utf8(char *out, size_t cap, const uint8_t *s, size_t n) {
    size_t k = 0;
    for (size_t i = 0; i < n && s[i]; i++) k = utf8_put(out, cap, k, s[i]);
    out[k] = 0;
}

/* UTF-16 (be: big-endian unless a BOM says otherwise). */
static void utf16_to_utf8(char *out, size_t cap, const uint8_t *s, size_t n, int be) {
    size_t k = 0, i = 0;
    if (n >= 2 && s[0] == 0xFF && s[1] == 0xFE) { be = 0; i = 2; }
    else if (n >= 2 && s[0] == 0xFE && s[1] == 0xFF) { be = 1; i = 2; }
    for (; i + 1 < n; i += 2) {
        unsigned u = be ? rd16be(s + i) : rd16le(s + i);
        if (!u) break;
        if (u >= 0xD800 && u < 0xDC00 && i + 3 < n) {
            unsigned lo = be ? rd16be(s + i + 2) : rd16le(s + i + 2);
            if (lo >= 0xDC00 && lo < 0xE000) { u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00); i += 2; }
        }
        k = utf8_put(out, cap, k, u);
    }
    out[k] = 0;
}

/* ---- TIFF and EXIF -------------------------------------------------------------------------------------------------------------- */

typedef struct { const FFile *f; off_t base; int le; off_t limit; } Tiff;

static unsigned tf16(const Tiff *t, const uint8_t *p) { return t->le ? rd16le(p) : rd16be(p); }
static uint32_t tf32(const Tiff *t, const uint8_t *p) { return t->le ? rd32le(p) : rd32be(p); }

/* A tag's value bytes (inline or at its offset), at most cap. Returns the bytes read. */
static size_t tiff_value(const Tiff *t, const uint8_t *e, uint8_t *out, size_t cap, unsigned *type, uint32_t *count) {
    static const int sizes[13] = {0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8};
    *type = tf16(t, e + 2);
    *count = tf32(t, e + 4);
    if (*type == 0 || *type > 12) return 0;
    uint64_t total = (uint64_t)sizes[*type] * *count;
    size_t want = total > cap ? cap : (size_t)total;
    if (total <= 4) { memcpy(out, e + 8, want); return want; }
    return ff_read(t->f, t->base + (off_t)tf32(t, e + 8), out, want);
}

static double tiff_rational(const Tiff *t, const uint8_t *v) {
    uint32_t a = tf32(t, v), b = tf32(t, v + 4);
    return b ? (double)a / b : 0;
}

typedef struct { int orientation, w, h; } TiffOut;

static void tiff_ifd(const Tiff *t, uint32_t off, FInfo *in, TiffOut *o, int depth) {
    uint8_t cnt[2];
    if (depth > 3 || ff_read(t->f, t->base + (off_t)off, cnt, 2) != 2) return;
    unsigned n = tf16(t, cnt);
    if (n > 512) return;
    uint8_t *ents = (uint8_t *)malloc((size_t)n * 12 + 1);
    if (!ents) return;
    if (ff_read(t->f, t->base + (off_t)off + 2, ents, (size_t)n * 12) != (size_t)n * 12) { free(ents); return; }
    for (unsigned i = 0; i < n; i++) {
        const uint8_t *e = ents + i * 12;
        unsigned tag = tf16(t, e), type;
        uint32_t count;
        uint8_t v[256];
        memset(v, 0, sizeof v);
        size_t got = tiff_value(t, e, v, sizeof v - 1, &type, &count);
        unsigned num = type == 3 ? tf16(t, v) : (type == 4 ? tf32(t, v) : (type == 1 ? v[0] : 0));
        switch (tag) {
        case 0x0100: if (o) o->w = (int)num; break;
        case 0x0101: if (o) o->h = (int)num; break;
        case 0x010E: info_field(in, "Description", v, got); break;
        case 0x010F: info_field(in, "Camera make", v, got); break;
        case 0x0110: info_field(in, "Camera model", v, got); break;
        case 0x0112:
            if (o) o->orientation = (int)num;
            if (num > 1) info_add(in, "Orientation", "%s", num == 3 ? "upside down" : num == 6 ? "turned right" : num == 8 ? "turned left" : "mirrored");
            break;
        case 0x0131: info_field(in, "Software", v, got); break;
        case 0x0132: info_field(in, "Changed", v, got); break;
        case 0x013B: info_field(in, "Artist", v, got); break;
        case 0x8298: info_field(in, "Copyright", v, got); break;
        case 0x9003: info_field(in, "Taken", v, got); break;
        case 0x829A: if (got >= 8) { double x = tiff_rational(t, v); if (x > 0) info_add(in, "Exposure", x < 1 ? "1/%.0f s" : "%.1f s", x < 1 ? 1 / x : x); } break;
        case 0x829D: if (got >= 8) info_add(in, "Aperture", "f/%.1f", tiff_rational(t, v)); break;
        case 0x8827: if (num) info_add(in, "ISO", "%u", num); break;
        case 0x920A: if (got >= 8) info_add(in, "Focal length", "%.1f mm", tiff_rational(t, v)); break;
        case 0x9209: if (type == 3) info_add(in, "Flash", "%s", (num & 1) ? "fired" : "did not fire"); break;
        case 0xA434: info_field(in, "Lens", v, got); break;
        case 0xA002: if (o && !o->w) o->w = (int)num; break;
        case 0xA003: if (o && !o->h) o->h = (int)num; break;
        case 0x8825: info_add(in, "Location", "has GPS position"); break;
        case 0x8769: tiff_ifd(t, num, in, o, depth + 1); break;
        default: break;
        }
    }
    free(ents);
}

/* A TIFF structure at base (a TIFF file, or the EXIF block in a JPEG). Fills o (may be NULL). */
static int tiff_parse(const FFile *f, off_t base, FInfo *in, TiffOut *o) {
    uint8_t h[8];
    if (ff_read(f, base, h, 8) != 8) return -1;
    Tiff t = {f, base, h[0] == 'I', 0};
    if (!((h[0] == 'I' && h[1] == 'I') || (h[0] == 'M' && h[1] == 'M')) || tf16(&t, h + 2) != 42) return -1;
    tiff_ifd(&t, tf32(&t, h + 4), in, o, 0);
    return 0;
}

/* ---- pictures --------------------------------------------------------------------------------------------------------------------- */

static void info_dims(FInfo *in, long w, long h) {
    if (w > 0 && h > 0) info_add(in, "Dimensions", "%ld x %ld pixels", w, h);
}

static void info_png(const FFile *f, FInfo *in) {
    off_t p = 8;
    int chunks = 0;
    while (p + 8 <= f->size && chunks++ < 10000) {
        uint8_t hd[8];
        if (ff_read(f, p, hd, 8) != 8) break;
        uint32_t len = rd32be(hd);
        if (!memcmp(hd + 4, "IHDR", 4)) {
            uint8_t d[13];
            if (ff_read(f, p + 8, d, 13) == 13) {
                static const char *types[7] = {"greyscale", "?", "RGB", "palette", "greyscale + alpha", "?", "RGBA"};
                info_dims(in, (long)rd32be(d), (long)rd32be(d + 4));
                info_add(in, "Colour", "%s, %d bits a channel%s", d[9] < 7 ? types[d[9]] : "?", d[8], d[12] ? ", interlaced" : "");
            }
        } else if (!memcmp(hd + 4, "acTL", 4)) {
            uint8_t d[8];
            if (ff_read(f, p + 8, d, 8) == 8) info_add(in, "Animation", "%u frames (APNG)", rd32be(d));
        } else if (!memcmp(hd + 4, "pHYs", 4)) {
            uint8_t d[9];
            if (ff_read(f, p + 8, d, 9) == 9 && d[8] == 1) info_add(in, "Resolution", "%.0f dpi", rd32be(d) * 0.0254);
        } else if ((!memcmp(hd + 4, "tEXt", 4) || !memcmp(hd + 4, "iTXt", 4)) && len < 4096) {
            uint8_t d[4097];
            size_t got = ff_read(f, p + 8, d, len);
            d[got] = 0;
            size_t kl = strnlen((const char *)d, got);
            if (kl < got) {
                const uint8_t *v = d + kl + 1;
                size_t vl = got - kl - 1;
                if (hd[4] == 'i') { /* compression flag, method, language, translated keyword */
                    if (vl < 2 || v[0]) { p += 12 + (off_t)len; continue; }
                    v += 2; vl -= 2;
                    for (int z = 0; z < 2; z++) { size_t l = strnlen((const char *)v, vl); if (l >= vl) { vl = 0; break; } v += l + 1; vl -= l + 1; }
                }
                char key[40];
                snprintf(key, sizeof key, "%.27s", (const char *)d);
                if (vl) info_field(in, key, v, vl);
            }
        } else if (!memcmp(hd + 4, "eXIf", 4)) {
            tiff_parse(f, p + 8, in, NULL);
        } else if (!memcmp(hd + 4, "IEND", 4)) {
            break;
        }
        p += 12 + (off_t)len;
    }
}

static int info_jpeg(const FFile *f, FInfo *in) {
    off_t p = 2;
    int orientation = 0, guard = 0;
    while (p + 4 <= f->size && guard++ < 1000) {
        uint8_t m[4];
        if (ff_read(f, p, m, 4) != 4 || m[0] != 0xFF) break;
        if (m[1] == 0xFF) { p++; continue; }
        if (m[1] == 0xD9 || m[1] == 0xDA) break;
        unsigned len = rd16be(m + 2);
        if (m[1] == 0xE0) {
            uint8_t d[14];
            if (ff_read(f, p + 4, d, 14) == 14 && !memcmp(d, "JFIF", 5)) {
                info_add(in, "JFIF", "version %d.%02d", d[5], d[6]);
                if (d[7] == 1 && rd16be(d + 8) > 1) info_add(in, "Resolution", "%u dpi", rd16be(d + 8));
            }
        } else if (m[1] == 0xE1) {
            uint8_t d[6];
            if (ff_read(f, p + 4, d, 6) == 6 && !memcmp(d, "Exif\0", 5)) {
                TiffOut o = {0, 0, 0};
                tiff_parse(f, p + 10, in, &o);
                orientation = o.orientation;
            } else if (ff_read(f, p + 4, d, 5) == 5 && !memcmp(d, "http:", 5)) {
                info_add(in, "XMP", "yes");
            }
        } else if (m[1] == 0xE2) {
            uint8_t d[12];
            if (ff_read(f, p + 4, d, 12) == 12 && !memcmp(d, "ICC_PROFILE", 11)) info_add(in, "Colour profile", "embedded ICC");
        } else if (m[1] == 0xFE && len > 2 && len < 1024) {
            uint8_t d[1024];
            size_t got = ff_read(f, p + 4, d, len - 2);
            info_field(in, "Comment", d, got);
        } else if (m[1] >= 0xC0 && m[1] <= 0xCF && m[1] != 0xC4 && m[1] != 0xC8 && m[1] != 0xCC) {
            uint8_t d[6];
            if (ff_read(f, p + 4, d, 6) == 6) {
                info_dims(in, rd16be(d + 3), rd16be(d + 1));
                const char *how = m[1] == 0xC2 ? "progressive" : (m[1] == 0xC3 ? "lossless" : (m[1] >= 0xC9 ? "arithmetic" : "baseline"));
                info_add(in, "Coding", "%s, %d-bit, %d channel%s", how, d[0], d[5], d[5] == 1 ? " (greyscale)" : (d[5] == 4 ? "s (CMYK)" : "s"));
            }
        }
        p += 2 + len;
    }
    return orientation;
}

/* The EXIF orientation of a JPEG (0 when none), for the picture view. */
static int ff_jpeg_orientation(const FFile *f) {
    FInfo tmp = {0, 0, 0};
    int o = info_jpeg(f, &tmp);
    info_free(&tmp);
    return o;
}

static void info_gif(const FFile *f, FInfo *in) {
    const uint8_t *h = f->head;
    if (f->hn < 13) return;
    info_dims(in, rd16le(h + 6), rd16le(h + 8));
    info_add(in, "Version", "%.6s", (const char *)h);
    if (h[10] & 0x80) info_add(in, "Colours", "%d in the global table", 2 << (h[10] & 7));
    size_t n;
    uint8_t *b = ff_load(f, 0, FF_MAX_LOAD, &n);
    if (!b) return;
    size_t p = 13 + ((h[10] & 0x80) ? (size_t)(2 << (h[10] & 7)) * 3 : 0);
    int frames = 0, loop = -1;
    while (p < n) {
        int c = b[p++];
        if (c == 0x2C) {
            frames++;
            if (p + 9 > n) break;
            int fl = b[p + 8];
            p += 9 + ((fl & 0x80) ? (size_t)(2 << (fl & 7)) * 3 : 0);
            p++; /* LZW size */
            while (p < n && b[p]) p += (size_t)b[p] + 1;
            p++;
        } else if (c == 0x21) {
            if (p >= n) break;
            int label = b[p++];
            if (label == 0xFF && p + 15 < n && b[p] == 11 && !memcmp(b + p + 1, "NETSCAPE2.0", 11) && b[p + 12] >= 3) loop = (int)rd16le(b + p + 14);
            if (label == 0xFE && p < n && b[p]) { char key[] = "Comment"; info_field(in, key, b + p + 1, b[p]); }
            while (p < n && b[p]) p += (size_t)b[p] + 1;
            p++;
        } else {
            break;
        }
    }
    free(b);
    if (frames > 1) info_add(in, "Animation", "%d frames%s", frames, loop == 0 ? ", loops forever" : "");
}

static void info_bmp(const FFile *f, FInfo *in) {
    const uint8_t *h = f->head;
    if (f->hn < 30) return;
    uint32_t hs = rd32le(h + 14);
    if (hs == 12) { info_dims(in, rd16le(h + 18), rd16le(h + 20)); info_add(in, "Colour", "%u bits a pixel (OS/2 header)", rd16le(h + 24)); return; }
    if (f->hn < 54) return;
    int32_t w = (int32_t)rd32le(h + 18), ht = (int32_t)rd32le(h + 22);
    info_dims(in, w, ht < 0 ? -ht : ht);
    static const char *comp[] = {"none", "RLE8", "RLE4", "bit fields", "JPEG", "PNG", "bit fields + alpha"};
    uint32_t c = rd32le(h + 30);
    info_add(in, "Colour", "%u bits a pixel, compression %s%s", rd16le(h + 28), c < 7 ? comp[c] : "unknown", ht < 0 ? ", top-down" : "");
    if (rd32le(h + 38) > 1) info_add(in, "Resolution", "%.0f dpi", rd32le(h + 38) * 0.0254);
}

static void info_ico(const FFile *f, FInfo *in) {
    unsigned n = rd16le(f->head + 4);
    info_add(in, "Pictures", "%u", n);
    for (unsigned i = 0; i < n && i < 16 && 6 + i * 16 + 16 <= f->hn; i++) {
        const uint8_t *e = f->head + 6 + i * 16;
        uint8_t sig[4] = {0, 0, 0, 0};
        ff_read(f, rd32le(e + 12), sig, 4);
        char k[16];
        snprintf(k, sizeof k, "Picture %u", i + 1);
        info_add(in, k, "%d x %d, %s", e[0] ? e[0] : 256, e[1] ? e[1] : 256, !memcmp(sig, "\x89PNG", 4) ? "PNG" : "bitmap");
    }
}

static void info_webp(const FFile *f, FInfo *in) {
    const uint8_t *h = f->head;
    if (f->hn < 30) return;
    if (!memcmp(h + 12, "VP8 ", 4) && h[23] == 0x9D && h[24] == 0x01 && h[25] == 0x2A) {
        info_dims(in, rd16le(h + 26) & 0x3FFF, rd16le(h + 28) & 0x3FFF);
        info_add(in, "Coding", "lossy (VP8)");
    } else if (!memcmp(h + 12, "VP8L", 4) && h[20] == 0x2F) {
        uint32_t b = rd32le(h + 21);
        info_dims(in, (b & 0x3FFF) + 1, ((b >> 14) & 0x3FFF) + 1);
        info_add(in, "Coding", "lossless (VP8L)");
    } else if (!memcmp(h + 12, "VP8X", 4)) {
        info_dims(in, (h[24] | h[25] << 8 | h[26] << 16) + 1, (h[27] | h[28] << 8 | h[29] << 16) + 1);
        int fl = h[20];
        info_add(in, "Features", "%s%s%s%s", (fl & 2) ? "animated " : "", (fl & 16) ? "alpha " : "", (fl & 32) ? "ICC " : "", (fl & 8) ? "EXIF" : "");
    }
}

static void info_heif(const FFile *f, FInfo *in) {
    size_t n;
    uint8_t *b = ff_load(f, 0, 256 * 1024, &n);
    if (!b) return;
    for (size_t i = 4; i + 16 <= n; i++)
        if (!memcmp(b + i, "ispe", 4)) { info_dims(in, (long)rd32be(b + i + 8), (long)rd32be(b + i + 12)); break; }
    free(b);
    info_add(in, "Note", "the picture itself is not drawn here (HEVC/AV1)");
}

static void info_psd(const FFile *f, FInfo *in) {
    const uint8_t *h = f->head;
    if (f->hn < 26) return;
    static const char *modes[] = {"bitmap", "greyscale", "indexed", "RGB", "CMYK", "?", "?", "multichannel", "duotone", "Lab"};
    unsigned mode = rd16be(h + 24);
    info_dims(in, (long)rd32be(h + 18), (long)rd32be(h + 14));
    info_add(in, "Colour", "%s, %u channels, %u bits", mode < 10 ? modes[mode] : "?", rd16be(h + 12), rd16be(h + 22));
    info_add(in, "Version", "%s", rd16be(h + 4) == 2 ? "large document (PSB)" : "1");
}

static void info_svg(const FFile *f, FInfo *in) {
    char buf[FF_HEAD + 1];
    memcpy(buf, f->head, f->hn);
    buf[f->hn] = 0;
    const char *s = strstr(buf, "<svg");
    if (!s) return;
    const char *attrs[] = {"width", "height", "viewBox"};
    for (int i = 0; i < 3; i++) {
        char pat[16];
        snprintf(pat, sizeof pat, " %s=\"", attrs[i]);
        const char *a = strstr(s, pat);
        if (!a) continue;
        a += strlen(pat);
        const char *e = strchr(a, '"');
        if (e && e - a < 80) info_add(in, i == 2 ? "View box" : (i ? "Height" : "Width"), "%.*s", (int)(e - a), a);
    }
}

static void info_simple_images(const FFile *f, int kind, FInfo *in) {
    const uint8_t *h = f->head;
    if (kind == K_TGA && f->hn >= 18) {
        static const char *t[] = {"none", "colour-mapped", "true colour", "greyscale"};
        info_dims(in, rd16le(h + 12), rd16le(h + 14));
        info_add(in, "Colour", "%s, %d bits a pixel%s", t[h[2] & 3], h[16], (h[2] & 8) ? ", RLE" : "");
    } else if (kind == K_QOI && f->hn >= 14) {
        info_dims(in, (long)rd32be(h + 4), (long)rd32be(h + 8));
        info_add(in, "Colour", "%s, %s", h[12] == 4 ? "RGBA" : "RGB", h[13] ? "linear" : "sRGB");
    } else if (kind == K_PCX && f->hn >= 128) {
        info_dims(in, (long)rd16le(h + 8) - rd16le(h + 4) + 1, (long)rd16le(h + 10) - rd16le(h + 6) + 1);
        info_add(in, "Colour", "%d bits x %d planes, version %d", h[3], h[65], h[1]);
    } else if (kind == K_PNM && f->hn >= 3) {
        size_t p = 2;
        int w = pnm_int(h, f->hn, &p), ht = pnm_int(h, f->hn, &p);
        info_dims(in, w, ht);
        static const char *k[] = {"", "bitmap (text)", "greymap (text)", "pixmap (text)", "bitmap", "greymap", "pixmap"};
        info_add(in, "Kind", "P%c, %s", h[1], k[h[1] - '0']);
    } else if (kind == K_TIFF) {
        TiffOut o = {0, 0, 0};
        FInfo tmp = {0, 0, 0};
        tiff_parse(f, 0, &tmp, &o);
        info_dims(in, o.w, o.h);
        for (int i = 0; i < tmp.n; i++) info_add(in, tmp.r[i].k, "%s", tmp.r[i].v);
        info_free(&tmp);
    }
}

/* ---- sound -------------------------------------------------------------------------------------------------------------------- */

static void info_sound(FInfo *in, int channels, double rate, int bits, double secs) {
    char d[32];
    if (channels > 0 && rate > 0)
        info_add(in, "Sound", "%s, %.0f Hz%s%.0d%s", channels == 1 ? "mono" : channels == 2 ? "stereo" : "multichannel", rate, bits ? ", " : "",
                 bits, bits ? "-bit" : "");
    if (secs > 0) { fmt_duration(secs, d, sizeof d); info_add(in, "Duration", "%s", d); }
}

/* Vorbis comments (FLAC, Ogg): vendor, then "KEY=value" entries. */
static void info_vorbis_comments(FInfo *in, const uint8_t *b, size_t n) {
    if (n < 8) return;
    uint32_t vl = rd32le(b);
    if (vl > n - 8) return;
    info_field(in, "Encoder", b + 4, vl);
    size_t p = 4 + vl;
    uint32_t count = rd32le(b + p);
    p += 4;
    for (uint32_t i = 0; i < count && i < 64 && p + 4 <= n; i++) {
        uint32_t l = rd32le(b + p);
        p += 4;
        if (l > n - p) break;
        const char *s = (const char *)b + p;
        const char *eq = memchr(s, '=', l);
        if (eq && eq - s < 24) {
            char k[28];
            int kl = (int)(eq - s);
            for (int j = 0; j < kl; j++) k[j] = (char)(j ? tolower((unsigned char)s[j]) : toupper((unsigned char)s[j]));
            k[kl] = 0;
            if (strcmp(k, "Metadata_block_picture")) info_field(in, k, (const uint8_t *)eq + 1, l - (size_t)kl - 1);
            else info_add(in, "Cover art", "yes");
        }
        p += l;
    }
}

static void info_wav(const FFile *f, FInfo *in) {
    off_t p = 12;
    int ch = 0, bits = 0, fmt = 0;
    uint32_t rate = 0, byterate = 0;
    uint64_t data = 0;
    for (int guard = 0; p + 8 <= f->size && guard < 200; guard++) {
        uint8_t hd[8];
        if (ff_read(f, p, hd, 8) != 8) break;
        uint32_t len = rd32le(hd + 4);
        if (!memcmp(hd, "fmt ", 4)) {
            uint8_t d[16];
            if (ff_read(f, p + 8, d, 16) == 16) { fmt = (int)rd16le(d); ch = (int)rd16le(d + 2); rate = rd32le(d + 4); byterate = rd32le(d + 8); bits = (int)rd16le(d + 14); }
        } else if (!memcmp(hd, "data", 4)) {
            data = len == 0xFFFFFFFFu ? (uint64_t)(f->size - p - 8) : len;
        } else if (!memcmp(hd, "LIST", 4) && len < 65536) {
            uint8_t *d = (uint8_t *)malloc(len + 1);
            if (d && ff_read(f, p + 8, d, len) == len && !memcmp(d, "INFO", 4)) {
                for (uint32_t q = 4; q + 8 <= len;) {
                    uint32_t l = rd32le(d + q + 4);
                    if (l > len - q - 8) break;
                    static const char *ids[][2] = {{"INAM", "Title"}, {"IART", "Artist"}, {"IPRD", "Album"}, {"ICMT", "Comment"}, {"ICRD", "Date"},
                                                   {"IGNR", "Genre"}, {"ISFT", "Software"}, {"ICOP", "Copyright"}};
                    for (int k = 0; k < 8; k++) if (!memcmp(d + q, ids[k][0], 4)) info_field(in, ids[k][1], d + q + 8, l);
                    q += 8 + l + (l & 1);
                }
            }
            free(d);
        }
        p += 8 + (off_t)len + (len & 1);
    }
    static const struct { int id; const char *name; } fmts[] = {{1, "PCM"}, {2, "Microsoft ADPCM"}, {3, "floating point"}, {6, "A-law"}, {7, "mu-law"},
                                                                {0x11, "IMA ADPCM"}, {0x55, "MP3"}, {0xFFFE, "PCM (extensible)"}};
    const char *fn = "other";
    for (size_t k = 0; k < sizeof fmts / sizeof fmts[0]; k++) if (fmts[k].id == fmt) fn = fmts[k].name;
    if (fmt) info_add(in, "Coding", "%s", fn);
    info_sound(in, ch, rate, bits, byterate ? (double)data / byterate : 0);
}

static void info_aiff(const FFile *f, FInfo *in) {
    off_t p = 12;
    for (int guard = 0; p + 8 <= f->size && guard < 200; guard++) {
        uint8_t hd[8];
        if (ff_read(f, p, hd, 8) != 8) break;
        uint32_t len = rd32be(hd + 4);
        if (!memcmp(hd, "COMM", 4)) {
            uint8_t d[22];
            if (ff_read(f, p + 8, d, 18) == 18) {
                int ch = (int)rd16be(d), bits = (int)rd16be(d + 6);
                uint32_t frames = rd32be(d + 2);
                int ex = ((d[8] & 0x7F) << 8 | d[9]) - 16383;
                double mant = (double)rd64be(d + 10);
                double rate = ldexp(mant, ex - 63);
                info_sound(in, ch, rate, bits, rate > 0 ? frames / rate : 0);
                if (len >= 22 && ff_read(f, p + 26, d, 4) == 4 && FF_AT(f, 8, "AIFC")) info_add(in, "Coding", "%.4s", (const char *)d);
            }
        } else if ((!memcmp(hd, "NAME", 4) || !memcmp(hd, "AUTH", 4) || !memcmp(hd, "(c) ", 4) || !memcmp(hd, "ANNO", 4)) && len < 1024) {
            uint8_t d[1024];
            size_t got = ff_read(f, p + 8, d, len);
            info_field(in, hd[0] == 'N' ? "Title" : hd[0] == 'A' && hd[1] == 'U' ? "Author" : hd[0] == '(' ? "Copyright" : "Comment", d, got);
        }
        p += 8 + (off_t)len + (len & 1);
    }
}

static void info_flac(const FFile *f, FInfo *in) {
    off_t p = 4;
    for (int guard = 0; p + 4 <= f->size && guard < 64; guard++) {
        uint8_t hd[4];
        if (ff_read(f, p, hd, 4) != 4) break;
        int type = hd[0] & 0x7F, last = hd[0] & 0x80;
        uint32_t len = (uint32_t)hd[1] << 16 | hd[2] << 8 | hd[3];
        if (type == 0) {
            uint8_t d[34];
            if (ff_read(f, p + 4, d, 34) == 34) {
                uint32_t rate = (uint32_t)d[10] << 12 | d[11] << 4 | d[12] >> 4;
                int ch = ((d[12] >> 1) & 7) + 1, bits = (((d[12] & 1) << 4) | d[13] >> 4) + 1;
                uint64_t samples = ((uint64_t)(d[13] & 15) << 32) | rd32be(d + 14);
                info_sound(in, ch, rate, bits, rate ? (double)samples / rate : 0);
            }
        } else if (type == 4 && len < 1024 * 1024) {
            uint8_t *d = (uint8_t *)malloc(len + 1);
            if (d && ff_read(f, p + 4, d, len) == len) info_vorbis_comments(in, d, len);
            free(d);
        } else if (type == 6) {
            info_add(in, "Cover art", "yes");
        }
        if (last) break;
        p += 4 + (off_t)len;
    }
}

/* ID3v2 text frame into UTF-8. */
static void id3_text(const uint8_t *d, size_t n, char *out, size_t cap) {
    out[0] = 0;
    if (n < 1) return;
    int enc = d[0];
    if (enc == 0) latin1_to_utf8(out, cap, d + 1, n - 1);
    else if (enc == 1 || enc == 2) utf16_to_utf8(out, cap, d + 1, n - 1, enc == 2);
    else { size_t l = n - 1 < cap - 1 ? n - 1 : cap - 1; memcpy(out, d + 1, l); out[l] = 0; }
}

static uint32_t syncsafe(const uint8_t *p) { return (uint32_t)(p[0] & 127) << 21 | (uint32_t)(p[1] & 127) << 14 | (uint32_t)(p[2] & 127) << 7 | (p[3] & 127); }

static off_t info_id3v2(const FFile *f, FInfo *in) {
    const uint8_t *h = f->head;
    if (f->hn < 10 || memcmp(h, "ID3", 3)) return 0;
    int ver = h[3];
    uint32_t size = syncsafe(h + 6);
    off_t end = 10 + (off_t)size + ((h[5] & 0x10) ? 10 : 0);
    info_add(in, "Tags", "ID3v2.%d", ver);
    size_t n;
    uint8_t *b = ff_load(f, 10, size, &n);
    if (!b) return end;
    size_t p = 0;
    if (ver >= 3 && (h[5] & 0x40) && n >= 4) p += ver == 4 ? syncsafe(b) : rd32be(b) + 4;
    static const char *ids[][3] = {{"TIT2", "TT2", "Title"}, {"TPE1", "TP1", "Artist"}, {"TALB", "TAL", "Album"}, {"TPE2", "TP2", "Album artist"},
                                   {"TYER", "TYE", "Year"}, {"TDRC", "", "Date"}, {"TCON", "TCO", "Genre"}, {"TRCK", "TRK", "Track"},
                                   {"TCOM", "TCM", "Composer"}, {"TENC", "TEN", "Encoded by"}, {"TSSE", "TSS", "Encoder"}};
    while (p + (ver == 2 ? 6 : 10) <= n) {
        const uint8_t *fr = b + p;
        if (!fr[0]) break;
        uint32_t len = ver == 2 ? ((uint32_t)fr[3] << 16 | fr[4] << 8 | fr[5]) : (ver == 4 ? syncsafe(fr + 4) : rd32be(fr + 4));
        size_t hs = ver == 2 ? 6 : 10;
        if (len > n - p - hs) break;
        const uint8_t *d = fr + hs;
        char v[232];
        for (size_t k = 0; k < sizeof ids / sizeof ids[0]; k++) {
            if (ver == 2 ? (ids[k][1][0] && !memcmp(fr, ids[k][1], 3)) : !memcmp(fr, ids[k][0], 4)) {
                id3_text(d, len, v, sizeof v);
                if (v[0]) info_add(in, ids[k][2], "%s", v);
            }
        }
        if ((ver == 2 ? !memcmp(fr, "COM", 3) : !memcmp(fr, "COMM", 4)) && len > 4) {
            /* encoding, language, description, text */
            int enc = d[0];
            size_t q = 4;
            if (enc == 1 || enc == 2) { while (q + 1 < len && (d[q] || d[q + 1])) q += 2; q += 2; }
            else { while (q < len && d[q]) q++; q++; }
            if (q < len) {
                uint8_t tmp[512];
                size_t l = len - q < sizeof tmp - 1 ? len - q : sizeof tmp - 1;
                tmp[0] = (uint8_t)enc;
                memcpy(tmp + 1, d + q, l);
                id3_text(tmp, l + 1, v, sizeof v);
                if (v[0]) info_add(in, "Comment", "%s", v);
            }
        }
        if (ver == 2 ? !memcmp(fr, "PIC", 3) : !memcmp(fr, "APIC", 4)) info_add(in, "Cover art", "yes");
        if (ver == 2 ? !memcmp(fr, "ULT", 3) : !memcmp(fr, "USLT", 4)) info_add(in, "Lyrics", "yes");
        p += hs + len;
    }
    free(b);
    return end;
}

static void info_mp3(const FFile *f, FInfo *in) {
    off_t start = info_id3v2(f, in);
    uint8_t tag[128];
    int v1 = f->size >= 128 && ff_read(f, f->size - 128, tag, 128) == 128 && !memcmp(tag, "TAG", 3);
    if (v1) {
        int have_title = 0;
        for (int i = 0; i < in->n; i++) if (!strcmp(in->r[i].k, "Title")) have_title = 1;
        if (!have_title) {
            char v[64];
            info_add(in, "Tags", "ID3v1");
            latin1_to_utf8(v, sizeof v, tag + 3, 30); info_add(in, "Title", "%s", v);
            latin1_to_utf8(v, sizeof v, tag + 33, 30); info_add(in, "Artist", "%s", v);
            latin1_to_utf8(v, sizeof v, tag + 63, 30); info_add(in, "Album", "%s", v);
            latin1_to_utf8(v, sizeof v, tag + 93, 4); info_add(in, "Year", "%s", v);
        }
    }
    uint8_t b[8192];
    size_t n = ff_read(f, start, b, sizeof b);
    for (size_t i = 0; i + 4 <= n; i++) {
        if (b[i] != 0xFF || (b[i + 1] & 0xE0) != 0xE0) continue;
        int ver = (b[i + 1] >> 3) & 3, layer = (b[i + 1] >> 1) & 3, bri = b[i + 2] >> 4, sri = (b[i + 2] >> 2) & 3, mode = b[i + 3] >> 6;
        if (ver == 1 || layer == 0 || bri == 0 || bri == 15 || sri == 3) continue;
        static const int br[2][3][16] = {
            {{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448}, {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},
             {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320}},
            {{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256}, {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
             {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}}};
        static const int sr[4][3] = {{11025, 12000, 8000}, {0, 0, 0}, {22050, 24000, 16000}, {44100, 48000, 32000}};
        int mpeg1 = ver == 3, li = 3 - layer;
        int kbps = br[mpeg1 ? 0 : 1][li][bri], rate = sr[ver][sri];
        int spf = li == 0 ? 384 : (li == 1 || mpeg1) ? 1152 : 576;
        static const char *modes[] = {"stereo", "joint stereo", "dual channel", "mono"};
        info_add(in, "Coding", "MPEG-%s layer %s", ver == 3 ? "1" : ver == 2 ? "2" : "2.5", li == 0 ? "I" : li == 1 ? "II" : "III");
        size_t xo = i + 4 + (mpeg1 ? (mode == 3 ? 17 : 32) : (mode == 3 ? 9 : 17));
        double secs = 0;
        int vbr = 0;
        if (xo + 12 <= n && (!memcmp(b + xo, "Xing", 4) || !memcmp(b + xo, "Info", 4)) && (b[xo + 7] & 1)) {
            secs = (double)rd32be(b + xo + 8) * spf / rate;
            vbr = !memcmp(b + xo, "Xing", 4);
        } else if (i + 36 + 18 <= n && !memcmp(b + i + 36, "VBRI", 4)) {
            secs = (double)rd32be(b + i + 36 + 14) * spf / rate;
            vbr = 1;
        } else if (kbps) {
            secs = (double)(f->size - start - (v1 ? 128 : 0)) * 8 / (kbps * 1000.0);
        }
        if (vbr && secs > 0) info_add(in, "Bit rate", "%.0f kbit/s average (VBR)", (double)(f->size - start) * 8 / secs / 1000);
        else info_add(in, "Bit rate", "%d kbit/s", kbps);
        info_sound(in, mode == 3 ? 1 : 2, rate, 0, secs);
        info_add(in, "Mode", "%s", modes[mode]);
        break;
    }
}

/* The packets at the start of an Ogg stream (one logical stream: the first). Returns how many were gathered. */
static int ogg_packets(const uint8_t *b, size_t n, uint8_t **pk, size_t *pl, int want) {
    size_t p = 0;
    int got = 0;
    uint32_t serial = 0;
    int have_serial = 0;
    uint8_t *cur = NULL;
    size_t cl = 0;
    while (p + 27 <= n && got < want) {
        if (memcmp(b + p, "OggS", 4)) { p++; continue; }
        int segs = b[p + 26];
        if (p + 27 + (size_t)segs > n) break;
        uint32_t s = rd32le(b + p + 14);
        size_t data = p + 27 + (size_t)segs;
        if (have_serial && s != serial) {
            size_t total = 0;
            for (int i = 0; i < segs; i++) total += b[p + 27 + i];
            p = data + total;
            continue;
        }
        serial = s;
        have_serial = 1;
        for (int i = 0; i < segs && got < want; i++) {
            size_t l = b[p + 27 + i];
            if (data + l > n) { free(cur); return got; }
            uint8_t *g = (uint8_t *)realloc(cur, cl + l + 1);
            if (!g) { free(cur); return got; }
            cur = g;
            memcpy(cur + cl, b + data, l);
            cl += l;
            data += l;
            if (l < 255) { pk[got] = cur; pl[got] = cl; got++; cur = NULL; cl = 0; }
        }
        p = data;
    }
    free(cur);
    return got;
}

static void info_ogg(const FFile *f, FInfo *in) {
    size_t n;
    uint8_t *b = ff_load(f, 0, 512 * 1024, &n);
    if (!b) return;
    uint8_t *pk[2] = {NULL, NULL};
    size_t pl[2] = {0, 0};
    int np = ogg_packets(b, n, pk, pl, 2);
    free(b);
    double rate = 0;
    int preskip = 0, opus = 0;
    if (np >= 1) {
        const uint8_t *d = pk[0];
        if (pl[0] >= 30 && !memcmp(d, "\x01vorbis", 7)) {
            rate = rd32le(d + 12);
            info_add(in, "Coding", "Vorbis");
            info_sound(in, d[11], rate, 0, 0);
            if ((int32_t)rd32le(d + 20) > 0) info_add(in, "Bit rate", "%u kbit/s nominal", rd32le(d + 20) / 1000);
        } else if (pl[0] >= 19 && !memcmp(d, "OpusHead", 8)) {
            opus = 1;
            rate = 48000;
            preskip = (int)rd16le(d + 10);
            info_add(in, "Coding", "Opus");
            info_sound(in, d[9], rd32le(d + 12) ? rd32le(d + 12) : 48000, 0, 0);
        } else if (pl[0] >= 42 && !memcmp(d, "\x80theora", 7)) {
            info_add(in, "Coding", "Theora");
            info_dims(in, (long)((uint32_t)d[14] << 16 | d[15] << 8 | d[16]), (long)((uint32_t)d[17] << 16 | d[18] << 8 | d[19]));
        } else if (pl[0] >= 51 && !memcmp(d, "\x7f" "FLAC", 5)) {
            const uint8_t *si = d + 17;
            rate = (uint32_t)si[10] << 12 | si[11] << 4 | si[12] >> 4;
            info_add(in, "Coding", "FLAC");
            info_sound(in, ((si[12] >> 1) & 7) + 1, rate, 0, 0);
        }
    }
    if (np >= 2) {
        if (pl[1] > 7 && !memcmp(pk[1], "\x03vorbis", 7)) info_vorbis_comments(in, pk[1] + 7, pl[1] - 7);
        else if (pl[1] > 8 && !memcmp(pk[1], "OpusTags", 8)) info_vorbis_comments(in, pk[1] + 8, pl[1] - 8);
    }
    free(pk[0]);
    free(pk[1]);
    if (rate > 0) { /* the last page's granule position is the length in samples */
        size_t tn;
        off_t from = f->size > 65536 ? f->size - 65536 : 0;
        uint8_t *t = ff_load(f, from, 65536, &tn);
        if (t) {
            for (size_t i = tn >= 14 ? tn - 14 : 0; i > 0; i--)
                if (!memcmp(t + i, "OggS", 4)) {
                    double g = (double)rd64le(t + i + 6);
                    char d[32];
                    fmt_duration((g - (opus ? preskip : 0)) / rate, d, sizeof d);
                    info_add(in, "Duration", "%s", d);
                    break;
                }
            free(t);
        }
    }
}

/* ---- MIDI --------------------------------------------------------------------------------------------------------------------- */

static uint32_t midi_vlq(const uint8_t *b, size_t n, size_t *p) {
    uint32_t v = 0;
    for (int i = 0; i < 4 && *p < n; i++) {
        uint8_t c = b[(*p)++];
        v = v << 7 | (c & 127);
        if (!(c & 128)) break;
    }
    return v;
}

typedef struct { uint32_t tick, tempo; } MidiTempo;

static int midi_tempo_cmp(const void *a, const void *b) {
    uint32_t x = ((const MidiTempo *)a)->tick, y = ((const MidiTempo *)b)->tick;
    return x < y ? -1 : x > y;
}

/* MIDI data at b (the MThd chunk). text, if given, gets the names, lyrics and markers. */
static void midi_parse(const uint8_t *b, size_t n, FInfo *in, FText *text) {
    if (n < 14 || memcmp(b, "MThd", 4)) return;
    unsigned fmt = rd16be(b + 8), ntr = rd16be(b + 10), div = rd16be(b + 12);
    if (in) {
        info_add(in, "MIDI format", "%u (%s)", fmt, fmt == 0 ? "one track" : fmt == 1 ? "tracks played together" : "separate songs");
        info_add(in, "Tracks", "%u", ntr);
        if (div & 0x8000) info_add(in, "Timing", "SMPTE %d fps, %u ticks a frame", -(int8_t)(div >> 8), div & 0xFF);
        else info_add(in, "Timing", "%u ticks a quarter note", div);
    }
    MidiTempo tempos[512];
    int nt = 0;
    uint32_t endtick = 0;
    long notes = 0;
    unsigned chans = 0;
    size_t p = 8 + rd32be(b + 4);
    char first_name[128] = "", timesig[32] = "", keysig[32] = "";
    for (unsigned t = 0; t < ntr && p + 8 <= n; t++) {
        if (memcmp(b + p, "MTrk", 4)) break;
        size_t len = rd32be(b + p + 4), q = p + 8, end = p + 8 + len > n ? n : p + 8 + len;
        uint32_t tick = 0;
        int status = 0;
        while (q < end) {
            tick += midi_vlq(b, end, &q);
            if (q >= end) break;
            int c = b[q];
            if (c & 0x80) { status = c; q++; }
            if (status == 0xFF) {
                if (q >= end) break;
                int type = b[q++];
                uint32_t l = midi_vlq(b, end, &q);
                if (l > end - q) break;
                const uint8_t *d = b + q;
                if (type == 0x51 && l == 3 && nt < 512) { tempos[nt].tick = tick; tempos[nt].tempo = (uint32_t)d[0] << 16 | d[1] << 8 | d[2]; nt++; }
                else if (type == 0x58 && l >= 2 && !timesig[0]) snprintf(timesig, sizeof timesig, "%d/%d", d[0], 1 << (d[1] & 15));
                else if (type == 0x59 && l >= 2 && !keysig[0]) {
                    static const char *maj[] = {"Cb", "Gb", "Db", "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#", "C#"};
                    static const char *min[] = {"Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#", "C#", "G#", "D#", "A#"};
                    int sf = (int8_t)d[0];
                    if (sf >= -7 && sf <= 7) snprintf(keysig, sizeof keysig, "%s %s", d[1] ? min[sf + 7] : maj[sf + 7], d[1] ? "minor" : "major");
                } else if (type >= 1 && type <= 7) {
                    char v[200];
                    ff_clean(v, sizeof v, d, l);
                    if (type == 3 && t == 0 && !first_name[0]) snprintf(first_name, sizeof first_name, "%.*s", (int)sizeof first_name - 1, v);   /* a long name is cut */
                    if (in && type == 2 && v[0]) info_add(in, "Copyright", "%s", v);
                    static const char *kinds[] = {"", "Text", "Copyright", "Track name", "Instrument", "Lyric", "Marker", "Cue"};
                    if (text && v[0]) text_line(text, "%-5u %-10s %s", t + 1, kinds[type], v);
                }
                q += l;
            } else if (status == 0xF0 || status == 0xF7) {
                uint32_t l = midi_vlq(b, end, &q);
                q += l;
            } else if (status >= 0x80) {
                int hi = status & 0xF0, need = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
                if (hi == 0x90 && q + 1 < end && b[q + 1]) notes++;
                chans |= 1u << (status & 15);
                q += (size_t)need;
            } else {
                break; /* data with no status: damaged */
            }
        }
        if (tick > endtick) endtick = tick;
        p += 8 + len;
    }
    if (!in) return;
    if (first_name[0]) info_add(in, "Title", "%s", first_name);
    if (timesig[0]) info_add(in, "Time signature", "%s", timesig);
    if (keysig[0]) info_add(in, "Key", "%s", keysig);
    int nch = 0;
    for (int c = 0; c < 16; c++) nch += (chans >> c) & 1;
    info_add(in, "Notes", "%ld on %d channel%s%s", notes, nch, nch == 1 ? "" : "s", (chans & (1u << 9)) ? " (drums on 10)" : "");
    double secs = 0;
    if (div & 0x8000) {
        int fps = -(int8_t)(div >> 8);
        if (fps > 0 && (div & 0xFF)) secs = (double)endtick / (fps * (div & 0xFF));
    } else if (div) {
        qsort(tempos, (size_t)nt, sizeof tempos[0], midi_tempo_cmp);
        uint32_t last = 0, tempo = 500000;
        for (int i = 0; i < nt && tempos[i].tick <= endtick; i++) {
            secs += (double)(tempos[i].tick - last) * tempo / div / 1e6;
            last = tempos[i].tick;
            tempo = tempos[i].tempo;
        }
        secs += (double)(endtick - last) * tempo / div / 1e6;
        if (nt) info_add(in, "Tempo", "%.0f BPM at the start%s", 60e6 / (tempos[0].tick == 0 ? tempos[0].tempo : 500000), nt > 1 ? ", changes" : "");
    }
    char d[32];
    fmt_duration(secs, d, sizeof d);
    info_add(in, "Duration", "%s", d);
}

static void midi_file(const FFile *f, int kind, FInfo *in, FText *text) {
    size_t n;
    uint8_t *b = ff_load(f, 0, 8u * 1024 * 1024, &n);
    if (!b) return;
    size_t off = 0;
    if (kind == K_RMID) {
        for (size_t i = 12; i + 4 <= n && i < 64; i++) if (!memcmp(b + i, "MThd", 4)) { off = i; break; }
    }
    midi_parse(b + off, n - off, in, text);
    free(b);
}

/* ---- tracker modules ---------------------------------------------------------------------------------------------------------- */

static void mod_name_lines(FText *t, const char *what, const uint8_t *s, size_t len, int idx) {
    char v[64];
    ff_clean(v, sizeof v, s, len);
    if (v[0] && t) text_line(t, "%s %2d  %s", what, idx, v);
}

static void module_file(const FFile *f, int kind, FInfo *in, FText *t) {
    size_t n;
    uint8_t *b = ff_load(f, 0, 8u * 1024 * 1024, &n);
    if (!b) return;
    char v[64];
    if (kind == K_MOD && n >= 1084) {
        ff_clean(v, sizeof v, b, 20);
        if (in) {
            if (v[0]) info_add(in, "Title", "%s", v);
            info_add(in, "Channels", "%d", ff_mod_channels(b, n));
            int pats = 0;
            for (int i = 0; i < 128; i++) if (b[952 + i] + 1 > pats) pats = b[952 + i] + 1;
            info_add(in, "Song", "%d positions, %d patterns", b[950], pats);
            int used = 0;
            for (int i = 0; i < 31; i++) if (rd16be(b + 20 + i * 30 + 22)) used++;
            info_add(in, "Samples", "%d", used);
            info_add(in, "Tag", "%.4s", (const char *)b + 1080);
        }
        for (int i = 0; i < 31; i++) mod_name_lines(t, "Sample", b + 20 + i * 30, 22, i + 1);
    } else if (kind == K_S3M && n >= 96) {
        ff_clean(v, sizeof v, b, 28);
        unsigned ord = rd16le(b + 32), ins = rd16le(b + 34), pat = rd16le(b + 36), cwt = rd16le(b + 40);
        if (in) {
            if (v[0]) info_add(in, "Title", "%s", v);
            int ch = 0, adlib = 0;
            for (int i = 0; i < 32; i++) { if (b[64 + i] < 16) ch++; else if (b[64 + i] < 32) adlib++; }
            info_add(in, "Channels", "%d%s", ch + adlib, adlib ? " (some AdLib)" : "");
            info_add(in, "Song", "%u orders, %u patterns, %u instruments", ord, pat, ins);
            static const char *trk[] = {"?", "Scream Tracker", "Imago Orpheus", "Impulse Tracker", "Schism Tracker", "OpenMPT", "BeRoTracker", "CreamTracker"};
            int k = cwt >> 12;
            info_add(in, "Made with", "%s %x.%02x", k < 8 ? trk[k] : "?", (cwt >> 8) & 15, cwt & 255);
            info_add(in, "Speed", "%d, tempo %d", b[49], b[50]);
        }
        for (unsigned i = 0; i < ins && i < 255; i++) {
            size_t pp = 96 + ord + i * 2;
            if (pp + 2 > n) break;
            size_t ip = (size_t)rd16le(b + pp) * 16;
            if (ip + 76 <= n) mod_name_lines(t, "Instrument", b + ip + 48, 28, (int)i + 1);
        }
    } else if (kind == K_XM && n >= 80) {
        ff_clean(v, sizeof v, b + 17, 20);
        unsigned hs = rd32le(b + 60), npat = rd16le(b + 70), nins = rd16le(b + 72);
        if (in) {
            if (v[0]) info_add(in, "Title", "%s", v);
            info_field(in, "Made with", b + 38, 20);
            info_add(in, "Version", "%x.%02x", b[59], b[58]);
            info_add(in, "Channels", "%u", rd16le(b + 68));
            info_add(in, "Song", "%u positions, %u patterns, %u instruments", rd16le(b + 64), npat, nins);
            info_add(in, "Speed", "%u, BPM %u%s", rd16le(b + 76), rd16le(b + 78), (rd16le(b + 74) & 1) ? ", linear slides" : "");
        }
        size_t p = 60 + (size_t)hs;
        for (unsigned i = 0; i < npat && p + 9 <= n; i++) p += rd32le(b + p) + rd16le(b + p + 7);
        for (unsigned i = 0; i < nins && p + 29 <= n; i++) {
            uint32_t isz = rd32le(b + p);
            unsigned ns = rd16le(b + p + 27);
            mod_name_lines(t, "Instrument", b + p + 4, 22, (int)i + 1);
            if (isz < 29 || isz > n) break;
            size_t q = p + isz;
            if (ns && p + 33 <= n) {
                uint32_t shs = rd32le(b + p + 29);
                size_t data = 0;
                for (unsigned s = 0; s < ns && q + 4 <= n; s++) { data += rd32le(b + q); q += shs; }
                q += data;
            }
            p = q;
        }
    } else if (kind == K_IT && n >= 192) {
        ff_clean(v, sizeof v, b + 4, 26);
        unsigned ord = rd16le(b + 32), ins = rd16le(b + 34), smp = rd16le(b + 36), pat = rd16le(b + 38), cwt = rd16le(b + 40);
        if (in) {
            if (v[0]) info_add(in, "Title", "%s", v);
            int ch = 0;
            for (int i = 0; i < 64; i++) if (b[64 + i] < 128) ch++;
            info_add(in, "Channels", "%d", ch);
            info_add(in, "Song", "%u orders, %u patterns, %u instruments, %u samples", ord, pat, ins, smp);
            info_add(in, "Made with", "%s %x.%02x", (cwt >> 12) == 1 ? "Schism Tracker" : (cwt >> 12) == 5 ? "OpenMPT" : "Impulse Tracker", (cwt >> 8) & 15, cwt & 255);
            info_add(in, "Speed", "%d, tempo %d", b[50], b[51]);
        }
        if (t && (rd16le(b + 46) & 1)) {
            unsigned ml = rd16le(b + 54);
            uint32_t mo = rd32le(b + 56);
            if (ml && mo < n) {
                if (ml > n - mo) ml = (unsigned)(n - mo);
                text_line(t, "Message:");
                char line[256];
                size_t k = 0;
                for (unsigned i = 0; i < ml && b[mo + i]; i++) {
                    char c = (char)b[mo + i];
                    if (c == '\r' || c == '\n' || k + 1 >= sizeof line) { line[k] = 0; text_line(t, "%s", line); k = 0; if (c == '\r' || c == '\n') continue; }
                    line[k++] = (unsigned char)c < 32 ? ' ' : c;
                }
                line[k] = 0;
                if (k) text_line(t, "%s", line);
                text_line(t, "%s", "");
            }
        }
        size_t base = 192 + ord;
        for (unsigned i = 0; i < ins && base + i * 4 + 4 <= n; i++) {
            uint32_t o = rd32le(b + base + i * 4);
            if (o + 58 <= n) mod_name_lines(t, "Instrument", b + o + 32, 26, (int)i + 1);
        }
        base += (size_t)ins * 4;
        for (unsigned i = 0; i < smp && base + i * 4 + 4 <= n; i++) {
            uint32_t o = rd32le(b + base + i * 4);
            if (o + 46 <= n) mod_name_lines(t, "Sample", b + o + 20, 26, (int)i + 1);
        }
    } else if (kind == K_MTM && n >= 66 && in) {
        info_field(in, "Title", b + 4, 20);
        info_add(in, "Song", "%u patterns, %u samples, %u channels", b[26] + 1u, b[30], b[33]);
    } else if (kind == K_STM && n >= 48 && in) {
        info_field(in, "Title", b, 20);
        info_field(in, "Made with", b + 20, 8);
    } else if (kind == K_UMF && n >= 12) {
        /* Neumetik's own song file: "UMF\x1a", a version, then chunks (4-letter name, 32-bit length); INFO starts with the title */
        if (in) info_add(in, "Version", "%u.%u", rd16le(b + 4), rd16le(b + 6));
        size_t p = 8;
        int chunks = 0;
        while (p + 8 <= n && chunks < 64) {
            uint32_t l = rd32le(b + p + 4);
            if (l > n - p - 8) break;
            if (!memcmp(b + p, "INFO", 4) && l >= 2 && in) {
                unsigned tl = rd16le(b + p + 8);
                if (tl <= l - 2) info_field(in, "Title", b + p + 10, tl);
            }
            if (t) text_line(t, "Chunk %.4s, %u bytes", (const char *)b + p, l);
            p += 8 + l;
            chunks++;
        }
        if (in) info_add(in, "Chunks", "%d", chunks);
    }
    free(b);
}

/* ---- video -------------------------------------------------------------------------------------------------------------------- */

typedef struct { double dur; uint32_t scale; int tracks; } Mp4State;

static void mp4_boxes(const uint8_t *b, size_t n, FInfo *in, Mp4State *st, int depth, const char *parent) {
    size_t p = 0;
    while (p + 8 <= n && depth < 8) {
        uint64_t len = rd32be(b + p);
        size_t hs = 8;
        if (len == 1 && p + 16 <= n) { len = rd64be(b + p + 8); hs = 16; }
        else if (len == 0) len = n - p;
        if (len < hs || len > n - p) break;
        const uint8_t *type = b + p + 4, *d = b + p + hs;
        size_t dl = (size_t)len - hs;
        if (!memcmp(type, "mvhd", 4) && dl >= 20) {
            if (d[0] == 1 && dl >= 32) { st->scale = rd32be(d + 20); st->dur = (double)rd64be(d + 24); }
            else { st->scale = rd32be(d + 12); st->dur = rd32be(d + 16); }
        } else if (!memcmp(type, "tkhd", 4) && dl >= 84) {
            size_t o = d[0] == 1 ? 88 : 76;
            if (o + 8 <= dl) {
                uint32_t w = rd32be(d + o) >> 16, h = rd32be(d + o + 4) >> 16;
                if (w && h) info_dims(in, w, h);
            }
        } else if (!memcmp(type, "stsd", 4) && dl >= 16) {
            char k[24];
            snprintf(k, sizeof k, "Track %d codec", st->tracks);
            info_add(in, k, "%.4s", (const char *)d + 12);
            if (!memcmp(d + 12, "mp4a", 4) && dl >= 16 + 28) info_sound(in, rd16be(d + 8 + 16 + 8), rd32be(d + 8 + 16 + 16) >> 16, 0, 0);
        } else if (!memcmp(type, "hdlr", 4) && dl >= 12 && !strcmp(parent, "mdia")) {
            st->tracks++;
            char k[24];
            snprintf(k, sizeof k, "Track %d", st->tracks);
            info_add(in, k, "%s", !memcmp(d + 8, "vide", 4) ? "video" : !memcmp(d + 8, "soun", 4) ? "sound" : !memcmp(d + 8, "text", 4) || !memcmp(d + 8, "sbtl", 4) ? "subtitles" : "other");
        } else if (!strcmp(parent, "ilst") && dl >= 16 && !memcmp(d + 4, "data", 4)) {
            static const char *tags[][2] = {{"\xA9nam", "Title"}, {"\xA9" "ART", "Artist"}, {"\xA9" "alb", "Album"}, {"\xA9" "day", "Date"},
                                            {"\xA9too", "Encoder"}, {"\xA9gen", "Genre"}, {"\xA9wrt", "Composer"}, {"aART", "Album artist"}};
            for (int i = 0; i < 8; i++)
                if (!memcmp(type, tags[i][0], 4)) { uint32_t l = rd32be(d); if (l >= 16 && l <= dl) info_field(in, tags[i][1], d + 16, l - 16); }
            if (!memcmp(type, "covr", 4)) info_add(in, "Cover art", "yes");
        }
        static const char *containers[] = {"moov", "trak", "mdia", "minf", "stbl", "udta", "ilst", "edts", "meta"};
        for (int i = 0; i < 9; i++)
            if (!memcmp(type, containers[i], 4)) {
                char name[5];
                memcpy(name, type, 4);
                name[4] = 0;
                size_t skip = i == 8 ? 4 : 0; /* meta has a version and flags first */
                if (skip <= dl) mp4_boxes(d + skip, dl - skip, in, st, depth + 1, name);
            }
        p += (size_t)len;
    }
}

/* Rec demo's kept frames (theme/r3i6nos_demo.h): records of {run: u32, one 64x24 cell screen}, a run being how many frames the screen stays. */
static void info_r3f(const FFile *f, FInfo *in) {
    const off_t rec = (off_t)(sizeof(uint32_t) + sizeof(R3Screen));
    off_t n = f->size / rec;
    info_add(in, "Made by", "Rec demo (docs/DEMO-RECORDINGS.md): the cells of each screen, not pixels");
    if (f->size % rec || n == 0) { info_add(in, "Records", "damaged: %lld bytes is not a whole number of %lld-byte records", (long long)f->size, (long long)rec); return; }
    unsigned long long frames = 0;
    for (off_t i = 0; i < n; i++) {
        uint8_t b[4];
        if (ff_read(f, i * rec, b, 4) != 4) break;
        uint32_t run = rd32le(b);
        frames += run ? run : 1;
    }
    info_add(in, "Screens", "%lld different, %llu frames at 30 a second", (long long)n, frames);
    info_add(in, "Duration", "%.1f s", (double)frames / 30.0);
    info_add(in, "Size", "64 x 24 cells (640 x 480 pixels when drawn)");
    info_add(in, "Sound", "the demo-<time>.wav beside it, if it is there");
}

static void info_mp4(const FFile *f, FInfo *in) {
    off_t p = 0;
    for (int guard = 0; p + 8 <= f->size && guard < 1000; guard++) {
        uint8_t hd[16];
        if (ff_read(f, p, hd, 16) < 8) break;
        uint64_t len = rd32be(hd);
        if (len == 1) len = rd64be(hd + 8);
        else if (len == 0) len = (uint64_t)(f->size - p);
        if (len < 8) break;
        if (!memcmp(hd + 4, "ftyp", 4)) info_add(in, "Brand", "%.4s", (const char *)hd + 8);
        if (!memcmp(hd + 4, "moov", 4)) {
            size_t n;
            uint8_t *b = ff_load(f, p, (size_t)len, &n);
            if (b) {
                Mp4State st = {0, 0, 0};
                mp4_boxes(b, n, in, &st, 0, "");
                free(b);
                if (st.scale) { char d[32]; fmt_duration(st.dur / st.scale, d, sizeof d); info_add(in, "Duration", "%s", d); }
            }
        }
        p += (off_t)len;
    }
}

static void info_avi(const FFile *f, FInfo *in) {
    size_t n;
    uint8_t *b = ff_load(f, 0, 256 * 1024, &n);
    if (!b) return;
    for (size_t i = 12; i + 64 <= n; i++) {
        if (!memcmp(b + i, "avih", 4)) {
            const uint8_t *d = b + i + 8;
            uint32_t us = rd32le(d), frames = rd32le(d + 16);
            info_dims(in, (long)rd32le(d + 32), (long)rd32le(d + 36));
            if (us) {
                char dd[32];
                info_add(in, "Frame rate", "%.2f fps", 1e6 / us);
                fmt_duration((double)frames * us / 1e6, dd, sizeof dd);
                info_add(in, "Duration", "%s", dd);
            }
            info_add(in, "Streams", "%u", rd32le(d + 24));
        } else if (!memcmp(b + i, "strh", 4) && i + 16 <= n) {
            info_add(in, !memcmp(b + i + 8, "vids", 4) ? "Video codec" : "Audio stream", "%.4s", !memcmp(b + i + 8, "vids", 4) ? (const char *)b + i + 12 : "yes");
        }
    }
    free(b);
}

/* Matroska: an EBML element's ID and size. */
static int ebml_vint(const uint8_t *b, size_t n, size_t *p, uint64_t *v, int keep_marker) {
    if (*p >= n) return -1;
    int c = b[*p], len = 1;
    while (len <= 8 && !(c & (0x80 >> (len - 1)))) len++;
    if (len > 8 || *p + (size_t)len > n) return -1;
    uint64_t x = keep_marker ? (uint64_t)c : (uint64_t)(c & (0xFF >> len));
    for (int i = 1; i < len; i++) x = x << 8 | b[*p + i];
    *p += (size_t)len;
    *v = x;
    return len;
}

static double ebml_float(const uint8_t *d, size_t l) {
    if (l == 4) { union { uint32_t u; float f; } x; x.u = rd32be(d); return x.f; }
    if (l == 8) { union { uint64_t u; double f; } x; x.u = rd64be(d); return x.f; }
    return 0;
}

static uint64_t ebml_uint(const uint8_t *d, size_t l) {
    uint64_t v = 0;
    for (size_t i = 0; i < l && i < 8; i++) v = v << 8 | d[i];
    return v;
}

typedef struct { double dur, scale; int track; } MkvState;

static void mkv_walk(const uint8_t *b, size_t n, FInfo *in, MkvState *st, int depth) {
    size_t p = 0;
    while (p < n && depth < 6) {
        uint64_t id, size;
        int il = ebml_vint(b, n, &p, &id, 1);
        int sl = ebml_vint(b, n, &p, &size, 0);
        if (il < 0 || sl < 0) return;
        int unknown = size == ((uint64_t)1 << (7 * sl)) - 1;
        if (unknown || size > n - p) size = n - p;
        const uint8_t *d = b + p;
        size_t l = (size_t)size;
        switch (id) {
        case 0x4282: info_field(in, "Kind", d, l); break; /* DocType */
        case 0x18538067: case 0x1549A766: case 0x1654AE6B: case 0xE0: case 0xE1: mkv_walk(d, l, in, st, depth + 1); break;
        case 0xAE: st->track++; mkv_walk(d, l, in, st, depth + 1); break;
        case 0x2AD7B1: st->scale = (double)ebml_uint(d, l); break;
        case 0x4489: st->dur = ebml_float(d, l); break;
        case 0x7BA9: info_field(in, "Title", d, l); break;
        case 0x4D80: info_field(in, "Muxer", d, l); break;
        case 0x5741: info_field(in, "Writer", d, l); break;
        case 0x86: { char k[24]; snprintf(k, sizeof k, "Track %d codec", st->track); info_field(in, k, d, l); break; }
        case 0xB0: { uint64_t w = ebml_uint(d, l); size_t q = p + l; uint64_t id2, s2; /* the height usually follows */
            if (ebml_vint(b, n, &q, &id2, 1) > 0 && id2 == 0xBA && ebml_vint(b, n, &q, &s2, 0) > 0 && s2 <= n - q) info_dims(in, (long)w, (long)ebml_uint(b + q, (size_t)s2));
            break; }
        case 0xB5: info_add(in, "Sample rate", "%.0f Hz", ebml_float(d, l)); break;
        case 0x9F: info_add(in, "Sound channels", "%llu", (unsigned long long)ebml_uint(d, l)); break;
        case 0x1F43B675: return; /* the first cluster: the details are over */
        default: break;
        }
        p += l;
    }
}

static void info_mkv(const FFile *f, FInfo *in) {
    size_t n;
    uint8_t *b = ff_load(f, 0, 1024 * 1024, &n);
    if (!b) return;
    MkvState st = {0, 1000000, 0};
    mkv_walk(b, n, in, &st, 0);
    free(b);
    if (st.dur > 0) { char d[32]; fmt_duration(st.dur * st.scale / 1e9, d, sizeof d); info_add(in, "Duration", "%s", d); }
}

/* ---- the media dispatcher ------------------------------------------------------------------------------------------------------- */

static int info_media(const FFile *f, int kind, FInfo *in) {
    switch (kind) {
    case K_PNG: info_png(f, in); return 1;
    case K_JPEG: info_jpeg(f, in); return 1;
    case K_GIF: info_gif(f, in); return 1;
    case K_BMP: info_bmp(f, in); return 1;
    case K_ICO: info_ico(f, in); return 1;
    case K_WEBP: info_webp(f, in); return 1;
    case K_HEIF: info_heif(f, in); return 1;
    case K_PSD: info_psd(f, in); return 1;
    case K_SVG: info_svg(f, in); return 1;
    case K_TGA: case K_QOI: case K_PCX: case K_PNM: case K_TIFF: info_simple_images(f, kind, in); return 1;
    case K_WAV: info_wav(f, in); return 1;
    case K_AIFF: info_aiff(f, in); return 1;
    case K_FLAC: info_flac(f, in); return 1;
    case K_MP3: info_mp3(f, in); return 1;
    case K_OGG: info_ogg(f, in); return 1;
    case K_MIDI: case K_RMID: midi_file(f, kind, in, NULL); return 1;
    case K_MOD: case K_S3M: case K_XM: case K_IT: case K_MTM: case K_STM: case K_UMF: module_file(f, kind, in, NULL); return 1;
    case K_R3F: info_r3f(f, in); return 1;
    case K_MP4: info_mp4(f, in); return 1;
    case K_AVI: info_avi(f, in); return 1;
    case K_MKV: info_mkv(f, in); return 1;
    case K_FLV: if (f->hn > 4) info_add(in, "Streams", "%s%s", (f->head[4] & 1) ? "video " : "", (f->head[4] & 4) ? "sound" : ""); return 1;
    default: return 0;
    }
}

#endif

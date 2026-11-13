/* ase.h: Aseprite sprites (.ase, .aseprite) for Commander's viewer, written for r3i6nos (MIT), no library.
 *
 * Written from the public Aseprite file-format description (docs/ase-file-specs.md upstream), not from Aseprite's code. Reading only.
 *
 * ase_open parses the whole file's chunks into a small index (layers, tags, palette, one record for each cel pointing into the file);
 * ase_render composites one frame: the visible image layers in order, each cel raw or zlib-compressed (or linked to another frame's),
 * with layer and cel opacity, the cel's offset and the separable blend modes. Drawn through the palette for indexed sprites.
 *
 * What is not drawn is counted, never guessed: tilemap layers, the blend modes hue, saturation, colour and luminosity, and the opacity
 * and blend mode of a group (its visibility is honoured). A linked cel takes its pixels from the cel it links to and keeps its own
 * offset and opacity.
 *
 * Every read is bounds-checked against the file; a damaged or cut-short file gives fewer frames or an error, never a crash. The file's
 * bytes must outlive the Ase (the cel records point into them). */
#ifndef COMMANDER_ASE_H
#define COMMANDER_ASE_H

#include "images.h"

#define ASE_MAX_CELS 200000
#define ASE_MAX_LAYERS 4096
#define ASE_MAX_TAGS 256

typedef struct { int flags, type, level, blend, opacity, visible; char name[48]; } AseLayer;
typedef struct { int from, to, dir; char name[40]; } AseTag;
typedef struct { int frame, layer, x, y, opacity, type, z, w, h, link; size_t off, len; } AseCel;

typedef struct {
    const uint8_t *buf;
    size_t n;
    int w, h, depth, nframes, transp, pal_count, header_flags;
    uint16_t *dur;      /* milliseconds, nframes */
    int *fstart;        /* the first cel of each frame in cel[] (nframes + 1) */
    AseLayer *layer;
    int nlayer;
    AseTag *tag;
    int ntag;
    AseCel *cel;
    int ncel;
    uint32_t pal[256];  /* ARGB */
    int cut;            /* the file ends or breaks before its last frame */
    int n_tile, n_blend, n_group; /* layers not drawn: tilemaps; hue/saturation/colour/luminosity; groups with opacity or a blend mode */
} Ase;

static void ase_free(Ase *a) {
    free(a->dur);
    free(a->fstart);
    free(a->layer);
    free(a->tag);
    free(a->cel);
    memset(a, 0, sizeof *a);
}

/* Does this look like an Aseprite file: size, magic, a frame, a depth, a canvas. */
static int ase_header_ok(const uint8_t *h, size_t hn, uint64_t size) {
    if (hn < 128 || img_le16(h + 4) != 0xA5E0) return 0;
    uint32_t sz = img_le32(h);
    unsigned depth = img_le16(h + 12);
    return sz >= 128 && sz <= size && img_le16(h + 6) > 0 && img_le16(h + 8) > 0 && img_le16(h + 10) > 0 && (depth == 8 || depth == 16 || depth == 32);
}

static void ase_name(char *out, size_t cap, const uint8_t *s, size_t avail) {
    size_t len = avail >= 2 ? img_le16(s) : 0, k = 0;
    if (len > avail - (avail >= 2 ? 2 : 0)) len = avail >= 2 ? avail - 2 : 0;
    for (size_t i = 0; i < len && k + 1 < cap; i++) {
        unsigned char c = s[2 + i];
        out[k++] = (c < 32 || c == 127) ? ' ' : (char)c;   /* UTF-8 bytes stay as they are; the viewer shows what it can */
    }
    out[k] = 0;
}

static int ase_grow(void **p, int n, int *cap, size_t size, int limit) {
    if (n >= limit) return -1;
    if (n >= *cap) {
        int nc = *cap ? *cap * 2 : 16;
        if (nc > limit) nc = limit;
        void *q = realloc(*p, (size_t)nc * size);
        if (!q) return -1;
        *p = q;
        *cap = nc;
    }
    return 0;
}

static int ase_open(Ase *a, const uint8_t *buf, size_t n, char *err, size_t errsz) {
    memset(a, 0, sizeof *a);
    if (!ase_header_ok(buf, n, n)) { snprintf(err, errsz, "not an Aseprite file"); return -1; }
    a->buf = buf;
    a->n = n;
    int frames = (int)img_le16(buf + 6);
    a->w = (int)img_le16(buf + 8);
    a->h = (int)img_le16(buf + 10);
    a->depth = (int)img_le16(buf + 12);
    a->header_flags = (int)img_le32(buf + 14);
    a->transp = buf[28];
    a->pal_count = img_le16(buf + 32);
    if (a->pal_count == 0) a->pal_count = 256;
    if ((uint64_t)a->w * a->h > IMG_MAX_PIXELS) { snprintf(err, errsz, "%dx%d is too big to draw here", a->w, a->h); return -1; }
    a->dur = (uint16_t *)calloc((size_t)frames, sizeof(uint16_t));
    a->fstart = (int *)calloc((size_t)frames + 1, sizeof(int));
    if (!a->dur || !a->fstart) { ase_free(a); snprintf(err, errsz, "out of memory"); return -1; }
    int lcap = 0, tcap = 0, ccap = 0, new_pal = 0;
    size_t p = 128;
    for (int f = 0; f < frames; f++) {
        if (p + 16 > n || img_le16(buf + p + 4) != 0xF1FA) { a->cut = 1; break; }
        uint32_t fsize = img_le32(buf + p);
        unsigned nch = img_le32(buf + p + 12) ? img_le32(buf + p + 12) : img_le16(buf + p + 6);
        size_t end = p + fsize;
        if (fsize < 16) { a->cut = 1; break; }
        if (end > n) { end = n; a->cut = 1; }
        a->dur[f] = (uint16_t)img_le16(buf + p + 8);
        a->fstart[f] = a->ncel;
        a->nframes = f + 1;
        size_t q = p + 16;
        for (unsigned c = 0; c < nch; c++) {
            if (q + 6 > end) break;
            uint32_t csz = img_le32(buf + q);
            unsigned type = img_le16(buf + q + 4);
            if (csz < 6 || q + csz > end) { a->cut = 1; break; }
            const uint8_t *d = buf + q + 6;
            size_t dl = csz - 6;
            if (type == 0x2004 && dl >= 18) {
                if (ase_grow((void **)&a->layer, a->nlayer, &lcap, sizeof(AseLayer), ASE_MAX_LAYERS) == 0) {
                    AseLayer *L = &a->layer[a->nlayer++];
                    memset(L, 0, sizeof *L);
                    L->flags = (int)img_le16(d);
                    L->type = (int)img_le16(d + 2);
                    L->level = (int)img_le16(d + 4);
                    L->blend = (int)img_le16(d + 10);
                    L->opacity = (a->header_flags & 1) ? d[12] : 255;
                    ase_name(L->name, sizeof L->name, d + 16, dl - 16);
                }
            } else if (type == 0x2005 && dl >= 18) {
                if (ase_grow((void **)&a->cel, a->ncel, &ccap, sizeof(AseCel), ASE_MAX_CELS) == 0) {
                    AseCel *C = &a->cel[a->ncel];
                    memset(C, 0, sizeof *C);
                    C->frame = f;
                    C->layer = (int)img_le16(d);
                    C->x = (int16_t)img_le16(d + 2);
                    C->y = (int16_t)img_le16(d + 4);
                    C->opacity = d[6];
                    C->type = (int)img_le16(d + 7);
                    C->z = (int16_t)img_le16(d + 9);
                    int ok = 1;
                    if (C->type == 0 || C->type == 2) {
                        if (dl < 20) ok = 0;
                        else {
                        C->w = (int)img_le16(d + 16);
                        C->h = (int)img_le16(d + 18);
                        C->off = (size_t)(d + 20 - buf);
                        C->len = dl - 20;
                        }
                    } else if (C->type == 1) {
                        C->link = (int)img_le16(d + 16);
                    } else if (C->type != 3) ok = 0;
                    if (ok) a->ncel++;
                } else a->cut = 1;
            } else if (type == 0x2018 && dl >= 10 && f == 0) {
                unsigned nt = img_le16(d);
                size_t t = 10;
                for (unsigned i = 0; i < nt && t + 19 <= dl; i++) {
                    if (ase_grow((void **)&a->tag, a->ntag, &tcap, sizeof(AseTag), ASE_MAX_TAGS) != 0) break;
                    AseTag *T = &a->tag[a->ntag++];
                    memset(T, 0, sizeof *T);
                    T->from = (int)img_le16(d + t);
                    T->to = (int)img_le16(d + t + 2);
                    T->dir = d[t + 4];
                    ase_name(T->name, sizeof T->name, d + t + 17, dl - (t + 17));
                    size_t sl = img_le16(d + t + 17);
                    t += 19 + sl;
                }
            } else if (type == 0x2019 && dl >= 20) {
                uint32_t size = img_le32(d), first = img_le32(d + 4), last = img_le32(d + 8);
                size_t t = 20;
                new_pal = 1;
                a->pal_count = size > 100000 ? 100000 : (int)size;
                for (uint32_t i = first; i <= last && t + 6 <= dl; i++) {
                    unsigned fl = img_le16(d + t);
                    if (i < 256) a->pal[i] = img_argb(d[t + 5], d[t + 2], d[t + 3], d[t + 4]);
                    t += 6;
                    if (fl & 1) { if (t + 2 > dl) break; t += 2 + img_le16(d + t); }
                    if (i == 0xFFFFFFFFu) break;
                }
            } else if ((type == 0x0004 || type == 0x0011) && dl >= 2 && !new_pal) {
                unsigned packets = img_le16(d);
                size_t t = 2;
                unsigned idx = 0;
                for (unsigned i = 0; i < packets && t + 2 <= dl; i++) {
                    idx += d[t];
                    unsigned cnt = d[t + 1] ? d[t + 1] : 256;
                    t += 2;
                    for (unsigned k = 0; k < cnt && t + 3 <= dl; k++, idx++, t += 3) {
                        int r = d[t], g = d[t + 1], b = d[t + 2];
                        if (type == 0x0011) { r = r << 2 | r >> 4; g = g << 2 | g >> 4; b = b << 2 | b >> 4; }
                        if (idx < 256) a->pal[idx] = img_argb(255, r & 255, g & 255, b & 255);
                    }
                }
            }
            q += csz;
        }
        p = end;
        if (a->cut) break;
    }
    if (a->nframes == 0) { ase_free(a); snprintf(err, errsz, "the file has no complete frame"); return -1; }
    a->fstart[a->nframes] = a->ncel;
    /* visibility: a layer is shown when it and every group above it are; count what is not drawn */
    int last[256];
    memset(last, -1, sizeof last);
    int shown[ASE_MAX_LAYERS];
    for (int i = 0; i < a->nlayer; i++) {
        AseLayer *L = &a->layer[i];
        int lv = L->level > 255 ? 255 : L->level;
        int parent_ok = lv == 0 || (last[lv - 1] >= 0 && shown[last[lv - 1]]);
        shown[i] = (L->flags & 1) && parent_ok;
        L->visible = shown[i];
        last[lv] = i;
        for (int k = lv + 1; k < 256; k++) last[k] = -1;
        if (!L->visible) continue;
        if (L->type == 2) a->n_tile++;
        else if (L->type == 1) { if (L->opacity < 255 || L->blend != 0) a->n_group++; }
        else if (L->blend >= 12 && L->blend <= 15) a->n_blend++;
    }
    return 0;
}

/* One colour channel of a separable blend mode (all in 0..1): cb the backdrop, cs the source. Returns -1 for a mode not done. */
static float ase_fn(int mode, float cb, float cs) {
    switch (mode) {
    case 0: return cs;
    case 1: return cb * cs;
    case 2: return cb + cs - cb * cs;
    case 3: return ase_fn(8, cs, cb);
    case 4: return cb < cs ? cb : cs;
    case 5: return cb > cs ? cb : cs;
    case 6: return cb <= 0 ? 0 : cs >= 1 ? 1 : (cb / (1 - cs) > 1 ? 1 : cb / (1 - cs));
    case 7: return cb >= 1 ? 1 : cs <= 0 ? 0 : 1 - ((1 - cb) / cs > 1 ? 1 : (1 - cb) / cs);
    case 8: return cs <= 0.5f ? cb * 2 * cs : cb + (2 * cs - 1) - cb * (2 * cs - 1);
    case 9: {
        if (cs <= 0.5f) return cb - (1 - 2 * cs) * cb * (1 - cb);
        float d = cb <= 0.25f ? ((16 * cb - 12) * cb + 4) * cb : sqrtf(cb);
        return cb + (2 * cs - 1) * (d - cb);
    }
    case 10: return cb > cs ? cb - cs : cs - cb;
    case 11: return cb + cs - 2 * cb * cs;
    case 16: return cb + cs > 1 ? 1 : cb + cs;
    case 17: return cb - cs < 0 ? 0 : cb - cs;
    case 18: return cb <= 0 ? 0 : cs <= 0 ? 1 : (cb / cs > 1 ? 1 : cb / cs);
    default: return -1;
    }
}

static void ase_blend(uint32_t *dst, int sr, int sg, int sb, float sa, int mode) {
    uint32_t d = *dst;
    float ba = (float)(d >> 24) / 255.0f;
    if (sa <= 0) return;
    if (ba <= 0) { *dst = img_argb((int)(sa * 255 + 0.5f), sr, sg, sb); return; }
    float ra = sa + ba * (1 - sa);
    float cb[3] = {(float)(d >> 16 & 255) / 255.0f, (float)(d >> 8 & 255) / 255.0f, (float)(d & 255) / 255.0f};
    float cs[3] = {(float)sr / 255.0f, (float)sg / 255.0f, (float)sb / 255.0f};
    int o[3];
    for (int i = 0; i < 3; i++) {
        float b = ase_fn(mode, cb[i], cs[i]);
        float co = (1 - sa / ra) * cb[i] + (sa / ra) * ((1 - ba) * cs[i] + ba * b);
        o[i] = (int)(co * 255 + 0.5f);
        if (o[i] < 0) o[i] = 0;
        if (o[i] > 255) o[i] = 255;
    }
    *dst = img_argb((int)(ra * 255 + 0.5f), o[0], o[1], o[2]);
}

/* A cel's pixels as ARGB (w*h), or NULL when it is too big, damaged or short. `bg` is the layer's background flag. */
static uint32_t *ase_cel_pixels(const Ase *a, const AseCel *c, int bg) {
    int bpp = a->depth / 8;
    if (c->w <= 0 || c->h <= 0 || (uint64_t)c->w * c->h > IMG_MAX_PIXELS) return NULL;
    size_t px = (size_t)c->w * c->h, need = px * (size_t)bpp;
    uint8_t *tmp = NULL;
    const uint8_t *raw;
    if (c->type == 0) {
        if (c->len < need) return NULL;
        raw = a->buf + c->off;
    } else {
        tmp = (uint8_t *)malloc(need);
        if (!tmp) return NULL;
        long got = fi_zlib(a->buf + c->off, c->len, tmp, need, NULL);
        if (got < (long)need) { free(tmp); return NULL; }
        raw = tmp;
    }
    uint32_t *out = (uint32_t *)malloc(px * sizeof(uint32_t));
    if (!out) { free(tmp); return NULL; }
    for (size_t i = 0; i < px; i++) {
        if (bpp == 4) out[i] = img_argb(raw[i * 4 + 3], raw[i * 4], raw[i * 4 + 1], raw[i * 4 + 2]);
        else if (bpp == 2) out[i] = img_argb(raw[i * 2 + 1], raw[i * 2], raw[i * 2], raw[i * 2]);
        else out[i] = (raw[i] == a->transp && !bg) ? 0 : a->pal[raw[i]];
    }
    free(tmp);
    return out;
}

/* Frame `frame` as an ARGB picture of the sprite's size. */
static int ase_render(const Ase *a, int frame, FImage *im, char *err, size_t errsz) {
    memset(im, 0, sizeof *im);
    if (frame < 0 || frame >= a->nframes) { snprintf(err, errsz, "no such frame"); return -1; }
    if (img_alloc(im, a->w, a->h, err, errsz) != 0) return -1;
    int first = a->fstart[frame], count = a->fstart[frame + 1] - first;
    int *order = (int *)malloc((size_t)(count ? count : 1) * sizeof(int));
    if (!order) { img_free(im); snprintf(err, errsz, "out of memory"); return -1; }
    for (int i = 0; i < count; i++) {          /* by layer + z, then z: an insertion sort, stable, cels are few */
        int k = i;
        const AseCel *c = &a->cel[first + i];
        while (k > 0) {
            const AseCel *o = &a->cel[first + order[k - 1]];
            if (o->layer + o->z < c->layer + c->z || (o->layer + o->z == c->layer + c->z && o->z <= c->z)) break;
            order[k] = order[k - 1];
            k--;
        }
        order[k] = i;
    }
    for (int oi = 0; oi < count; oi++) {
        const AseCel *c = &a->cel[first + order[oi]];
        if (c->layer < 0 || c->layer >= a->nlayer) continue;
        const AseLayer *L = &a->layer[c->layer];
        if (L->type != 0 || c->type == 3 || !L->visible || (L->blend >= 12 && L->blend <= 15) || ase_fn(L->blend, 0.5f, 0.5f) < 0) continue;
        const AseCel *src = c;
        for (int hop = 0; src->type == 1 && hop < 16; hop++) {      /* a linked cel: the pixels of the cel it links to */
            const AseCel *found = NULL;
            if (src->link >= 0 && src->link < a->nframes && src->link != src->frame)
                for (int j = a->fstart[src->link]; j < a->fstart[src->link + 1]; j++)
                    if (a->cel[j].layer == c->layer) { found = &a->cel[j]; break; }
            if (!found) { src = NULL; break; }
            src = found;
        }
        if (!src || src->type == 1 || src->type == 3) continue;
        uint32_t *pix = ase_cel_pixels(a, src, (L->flags & 8) != 0);
        if (!pix) continue;
        float op = (float)L->opacity / 255.0f * (float)c->opacity / 255.0f;
        for (int y = 0; y < src->h; y++) {
            int dy = c->y + y;
            if (dy < 0 || dy >= a->h) continue;
            for (int x = 0; x < src->w; x++) {
                int dx = c->x + x;
                if (dx < 0 || dx >= a->w) continue;
                uint32_t s = pix[(size_t)y * src->w + x];
                if (!(s >> 24)) continue;
                ase_blend(&im->px[(size_t)dy * a->w + dx], (int)(s >> 16 & 255), (int)(s >> 8 & 255), (int)(s & 255), (float)(s >> 24) / 255.0f * op, L->blend);
            }
        }
        free(pix);
    }
    free(order);
    return 0;
}

#endif

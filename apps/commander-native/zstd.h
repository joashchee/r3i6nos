/* zstd.h: Zstandard decompression (RFC 8878) for Commander, with no library. Written for r3i6nos (MIT) from the public format
 * specification.
 *
 * fi_unzstd() unpacks a whole .zst file held in memory (one or several frames, skippable frames skipped) into a caller's buffer of
 * `cap` bytes. A frame that needs a dictionary returns -1, and the content checksum is skipped, not verified. The output buffer is
 * the window: a match can point anywhere back to the start of its frame. Every read of the input and every write of the output is
 * bounds-checked: damaged input returns -1, never reads or writes outside the buffers. Output past `cap` is cut there (*cut set). */
#ifndef COMMANDER_ZSTD_H
#define COMMANDER_ZSTD_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ZS_BLOCK_MAX (128 * 1024)

typedef struct { uint8_t sym[512], nb[512]; uint16_t base[512]; int al; } ZsFse;
typedef struct { uint8_t sym[2048], nb[2048]; int maxbits; } ZsHuf;

/* A bit stream read backwards from its last byte (a sentinel 1 bit ends it); bits before the start read as 0. */
typedef struct { const uint8_t *p; long long off; } ZsBack;

static int zs_back_init(ZsBack *b, const uint8_t *p, size_t n) {
    if (n == 0 || p[n - 1] == 0) return -1;
    int hi = 7;
    while (!((p[n - 1] >> hi) & 1)) hi--;
    b->p = p;
    b->off = (long long)n * 8 - (8 - hi);
    return 0;
}

static uint64_t zs_back(ZsBack *b, int n) {
    if (n == 0) return 0;
    b->off -= n;
    long long o = b->off;
    int bits = n;
    int shift = 0;
    if (o < 0) { shift = (int)-o; bits = n + (int)o; o = 0; if (bits <= 0) return 0; }
    long long b0 = o >> 3, b1 = (o + bits - 1) >> 3;
    uint64_t v = 0;
    for (long long i = b1; i >= b0; i--) v = v << 8 | b->p[i];
    v >>= (o & 7);
    if (bits < 64) v &= (1ull << bits) - 1;
    return v << shift;
}

/* A forward little-endian bit reader (for FSE table descriptions). */
typedef struct { const uint8_t *p; size_t n; size_t bit; } ZsFwd;

static uint32_t zs_fwd(ZsFwd *f, int nb) {
    uint32_t v = 0;
    for (int i = 0; i < nb; i++, f->bit++) {
        size_t by = f->bit >> 3;
        if (by < f->n) v |= (uint32_t)((f->p[by] >> (f->bit & 7)) & 1) << i;
    }
    return v;
}

static uint32_t zs_le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static int zs_hibit(unsigned v) { int r = 0; while (v >>= 1) r++; return r; }

/* Builds a decoding table from normalized counts (-1: a probability below 1). */
static int zs_fse_build(ZsFse *t, const short *norm, int nsym, int al) {
    int size = 1 << al, high = size - 1;
    unsigned short next[256];
    if (al > 9 || nsym > 256) return -1;
    t->al = al;
    for (int s = 0; s < nsym; s++) {
        if (norm[s] == -1) { t->sym[high--] = (uint8_t)s; next[s] = 1; }
        else next[s] = (unsigned short)norm[s];
    }
    int pos = 0, step = (size >> 1) + (size >> 3) + 3, mask = size - 1;
    for (int s = 0; s < nsym; s++) {
        for (int i = 0; i < norm[s]; i++) {
            t->sym[pos] = (uint8_t)s;
            do pos = (pos + step) & mask; while (pos > high);
        }
    }
    if (pos != 0) return -1;
    for (int i = 0; i < size; i++) {
        int s = t->sym[i];
        unsigned nx = next[s]++;
        int nb = al - zs_hibit(nx);
        t->nb[i] = (uint8_t)nb;
        t->base[i] = (uint16_t)((nx << nb) - (unsigned)size);
    }
    return 0;
}

/* Reads an FSE table description; returns the bytes used, or -1. */
static int zs_fse_header(ZsFse *t, const uint8_t *p, size_t n, int max_al, int max_sym) {
    ZsFwd f = {p, n, 0};
    int al = 5 + (int)zs_fwd(&f, 4);
    if (al > max_al) return -1;
    short norm[256];
    int remaining = 1 << al, sym = 0;
    while (remaining > 0 && sym <= max_sym) {
        int bits = zs_hibit((unsigned)remaining + 1) + 1;
        int val = (int)zs_fwd(&f, bits);
        int lower = (1 << (bits - 1)) - 1, threshold = (1 << bits) - 1 - (remaining + 1);
        if ((val & lower) < threshold) { f.bit--; val &= lower; }
        else if (val > lower) val -= threshold;
        int proba = val - 1;
        remaining -= proba < 0 ? -proba : proba;
        norm[sym++] = (short)proba;
        if (proba == 0) {
            int rep;
            do { /* zeros: a 2-bit count, and another count follows when it is 3 */
                rep = (int)zs_fwd(&f, 2);
                for (int r = 0; r < rep; r++) { if (sym > max_sym) return -1; norm[sym++] = 0; }
            } while (rep == 3);
        }
        if (f.bit > n * 8 + 64) return -1;
    }
    if (remaining != 0 || f.bit > n * 8) return -1;
    if (zs_fse_build(t, norm, sym, al) != 0) return -1;
    return (int)((f.bit + 7) / 8);
}

/* Huffman: the tree from its description; returns the bytes it took, or -1. */
static int zs_huf_header(ZsHuf *h, const uint8_t *p, size_t n) {
    if (n < 1) return -1;
    unsigned hb = p[0];
    uint8_t w[256];
    int nw, used;
    if (hb >= 128) {
        nw = (int)hb - 127;
        used = 1 + (nw + 1) / 2;
        if ((size_t)used > n) return -1;
        for (int i = 0; i < nw; i++) w[i] = (uint8_t)((i & 1) ? p[1 + i / 2] & 15 : p[1 + i / 2] >> 4);
    } else {
        used = 1 + (int)hb;
        if ((size_t)used > n || hb == 0) return -1;
        ZsFse t;
        int hl = zs_fse_header(&t, p + 1, hb, 6, 12);
        if (hl < 0 || (unsigned)hl >= hb) return -1;
        ZsBack b;
        if (zs_back_init(&b, p + 1 + hl, hb - (unsigned)hl) != 0) return -1;
        unsigned s1 = (unsigned)zs_back(&b, t.al), s2 = (unsigned)zs_back(&b, t.al);
        nw = 0;
        for (;;) {
            if (nw >= 254) return -1;
            w[nw++] = t.sym[s1];
            s1 = t.base[s1] + (unsigned)zs_back(&b, t.nb[s1]);
            if (b.off < 0) { w[nw++] = t.sym[s2]; break; }
            w[nw++] = t.sym[s2];
            s2 = t.base[s2] + (unsigned)zs_back(&b, t.nb[s2]);
            if (b.off < 0) { w[nw++] = t.sym[s1]; break; }
        }
    }
    unsigned total = 0;
    for (int i = 0; i < nw; i++) { if (w[i] > 11) return -1; if (w[i]) total += 1u << (w[i] - 1); }
    if (total == 0) return -1;
    int maxbits = zs_hibit(total) + 1;
    if (maxbits > 11) return -1;
    unsigned rest = (1u << maxbits) - total; /* the last symbol takes what is left, which must be a power of two */
    if (rest == 0 || (rest & (rest - 1))) return -1;
    if (nw >= 256) return -1;
    w[nw] = (uint8_t)(zs_hibit(rest) + 1);
    nw++;
    h->maxbits = maxbits;
    unsigned rank_count[13] = {0}, rank_idx[13];
    for (int i = 0; i < nw; i++) rank_count[w[i]]++;
    rank_idx[0] = rank_idx[1] = 0; /* the lowest weight (the longest codes) takes the table from its start */
    for (int i = 1; i < maxbits; i++) rank_idx[i + 1] = rank_idx[i] + rank_count[i] * (1u << (i - 1));
    for (int i = 0; i < nw; i++) {
        if (!w[i]) continue;
        unsigned len = 1u << (w[i] - 1), at = rank_idx[w[i]];
        if (at + len > (1u << maxbits)) return -1;
        for (unsigned j = at; j < at + len; j++) { h->sym[j] = (uint8_t)i; h->nb[j] = (uint8_t)(maxbits + 1 - w[i]); }
        rank_idx[w[i]] += len;
    }
    return used;
}

/* One Huffman stream of `count` symbols into out. */
static int zs_huf_stream(const ZsHuf *h, const uint8_t *p, size_t n, uint8_t *out, size_t count) {
    ZsBack b;
    if (zs_back_init(&b, p, n) != 0) return -1;
    unsigned state = (unsigned)zs_back(&b, h->maxbits), mask = (1u << h->maxbits) - 1;
    for (size_t i = 0; i < count; i++) {
        out[i] = h->sym[state];
        int nb = h->nb[state];
        state = ((state << nb) + (unsigned)zs_back(&b, nb)) & mask;
    }
    return 0;
}

typedef struct {
    ZsHuf huf;
    int have_huf;
    ZsFse ll, of, ml;
    int have_ll, have_of, have_ml;
    uint32_t rep[3];
    uint8_t lits[ZS_BLOCK_MAX + 8];
} ZsCtx;

static const short ZS_LL_DEF[36] = {4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1, -1, -1, -1, -1};
static const short ZS_ML_DEF[53] = {1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1};
static const short ZS_OF_DEF[29] = {1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1};

static const uint32_t ZS_LL_BASE[36] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 20, 22, 24, 28, 32, 40, 48, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};
static const uint8_t ZS_LL_BITS[36] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
static const uint32_t ZS_ML_BASE[53] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
                                        35, 37, 39, 41, 43, 47, 51, 59, 67, 83, 99, 131, 259, 515, 1027, 2051, 4099, 8195, 16387, 32771, 65539};
static const uint8_t ZS_ML_BITS[53] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                       1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

/* One sequence table by its mode; returns the header bytes used, or -1. */
static int zs_seq_table(ZsFse *t, int *have, int mode, const uint8_t *p, size_t n, const short *def, int ndef, int def_al, int max_al, int max_sym) {
    if (mode == 0) { if (zs_fse_build(t, def, ndef, def_al) != 0) return -1; *have = 1; return 0; }
    if (mode == 1) { /* one symbol for every state */
        if (n < 1 || p[0] > max_sym) return -1;
        t->al = 0;
        t->sym[0] = p[0];
        t->nb[0] = 0;
        t->base[0] = 0;
        *have = 1;
        return 1;
    }
    if (mode == 3) return *have ? 0 : -1;
    int used = zs_fse_header(t, p, n, max_al, max_sym);
    if (used < 0) return -1;
    *have = 1;
    return used;
}

/* A compressed block: literals, then sequences. Output is appended at out[*op]; base is the start of the frame's output. */
static int zs_block(ZsCtx *x, const uint8_t *p, size_t n, uint8_t *out, size_t cap, size_t *op, size_t base, int *cut) {
    uint8_t *lits = x->lits;
    size_t nlits = 0, ip = 0;
    if (n < 1) return -1;
    unsigned type = p[0] & 3, sf = (p[0] >> 2) & 3;
    if (type <= 1) { /* raw or RLE */
        size_t hs, size;
        if (sf == 0 || sf == 2) { hs = 1; size = p[0] >> 3; }
        else if (sf == 1) { if (n < 2) return -1; hs = 2; size = (size_t)(p[0] >> 4) | (size_t)p[1] << 4; }
        else { if (n < 3) return -1; hs = 3; size = (size_t)(p[0] >> 4) | (size_t)p[1] << 4 | (size_t)p[2] << 12; }
        if (size > ZS_BLOCK_MAX) return -1;
        if (type == 0) { if (hs + size > n) return -1; memcpy(lits, p + hs, size); ip = hs + size; }
        else { if (hs + 1 > n) return -1; memset(lits, p[hs], size); ip = hs + 1; }
        nlits = size;
    } else {
        size_t hs, regen, comp;
        int streams = sf == 0 ? 1 : 4;
        if (sf <= 1) { if (n < 3) return -1; hs = 3; uint32_t v = p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16; regen = (v >> 4) & 0x3FF; comp = (v >> 14) & 0x3FF; }
        else if (sf == 2) { if (n < 4) return -1; hs = 4; uint32_t v = zs_le32(p); regen = (v >> 4) & 0x3FFF; comp = (v >> 18) & 0x3FFF; }
        else { if (n < 5) return -1; hs = 5; uint64_t v = (uint64_t)zs_le32(p) | (uint64_t)p[4] << 32; regen = (v >> 4) & 0x3FFFF; comp = (v >> 22) & 0x3FFFF; }
        if (regen > ZS_BLOCK_MAX || hs + comp > n) return -1;
        const uint8_t *d = p + hs;
        size_t dn = comp;
        if (type == 2) {
            int u = zs_huf_header(&x->huf, d, dn);
            if (u < 0) return -1;
            x->have_huf = 1;
            d += u;
            dn -= (size_t)u;
        } else if (!x->have_huf) return -1;
        if (streams == 1) {
            if (zs_huf_stream(&x->huf, d, dn, lits, regen) != 0) return -1;
        } else {
            if (dn < 6) return -1;
            size_t s1 = d[0] | (size_t)d[1] << 8, s2 = d[2] | (size_t)d[3] << 8, s3 = d[4] | (size_t)d[5] << 8;
            if (6 + s1 + s2 + s3 >= dn) return -1;
            size_t s4 = dn - 6 - s1 - s2 - s3, seg = (regen + 3) / 4;
            if (seg * 3 > regen) return -1;
            const uint8_t *q = d + 6;
            if (zs_huf_stream(&x->huf, q, s1, lits, seg) != 0) return -1;
            if (zs_huf_stream(&x->huf, q + s1, s2, lits + seg, seg) != 0) return -1;
            if (zs_huf_stream(&x->huf, q + s1 + s2, s3, lits + 2 * seg, seg) != 0) return -1;
            if (zs_huf_stream(&x->huf, q + s1 + s2 + s3, s4, lits + 3 * seg, regen - 3 * seg) != 0) return -1;
        }
        nlits = regen;
        ip = hs + comp;
    }
    /* sequences */
    if (ip >= n) return -1;
    size_t nseq = p[ip++];
    if (nseq >= 128) {
        if (nseq == 255) { if (ip + 2 > n) return -1; nseq = (size_t)p[ip] + ((size_t)p[ip + 1] << 8) + 0x7F00; ip += 2; }
        else { if (ip >= n) return -1; nseq = ((nseq - 128) << 8) + p[ip++]; }
    }
    size_t lp = 0; /* literals used */
    if (nseq > 0) {
        if (ip >= n) return -1;
        unsigned modes = p[ip++];
        int u;
        if ((u = zs_seq_table(&x->ll, &x->have_ll, (int)(modes >> 6), p + ip, n - ip, ZS_LL_DEF, 36, 6, 9, 35)) < 0) return -1;
        ip += (size_t)u;
        if ((u = zs_seq_table(&x->of, &x->have_of, (int)((modes >> 4) & 3), p + ip, n - ip, ZS_OF_DEF, 29, 5, 8, 31)) < 0) return -1;
        ip += (size_t)u;
        if ((u = zs_seq_table(&x->ml, &x->have_ml, (int)((modes >> 2) & 3), p + ip, n - ip, ZS_ML_DEF, 53, 6, 9, 52)) < 0) return -1;
        ip += (size_t)u;
        ZsBack b;
        if (zs_back_init(&b, p + ip, n - ip) != 0) return -1;
        unsigned sl = (unsigned)zs_back(&b, x->ll.al), so = (unsigned)zs_back(&b, x->of.al), sm = (unsigned)zs_back(&b, x->ml.al);
        for (size_t i = 0; i < nseq; i++) {
            unsigned ofc = x->of.sym[so], mlc = x->ml.sym[sm], llc = x->ll.sym[sl];
            if (ofc > 31 || mlc > 52 || llc > 35) return -1;
            uint32_t offv = (1u << ofc) + (uint32_t)zs_back(&b, (int)ofc);
            size_t ml = ZS_ML_BASE[mlc] + (size_t)zs_back(&b, ZS_ML_BITS[mlc]);
            size_t ll = ZS_LL_BASE[llc] + (size_t)zs_back(&b, ZS_LL_BITS[llc]);
            if (i + 1 < nseq) {
                sl = x->ll.base[sl] + (unsigned)zs_back(&b, x->ll.nb[sl]);
                sm = x->ml.base[sm] + (unsigned)zs_back(&b, x->ml.nb[sm]);
                so = x->of.base[so] + (unsigned)zs_back(&b, x->of.nb[so]);
            }
            uint32_t off;
            if (offv > 3) { off = offv - 3; x->rep[2] = x->rep[1]; x->rep[1] = x->rep[0]; x->rep[0] = off; }
            else {
                unsigned idx = offv - 1 + (ll == 0 ? 1 : 0);
                if (idx == 0) off = x->rep[0];
                else {
                    if (idx == 3) { off = x->rep[0] - 1; if (off == 0) return -1; }
                    else off = x->rep[idx];
                    if (idx >= 2) x->rep[2] = x->rep[1];
                    x->rep[1] = x->rep[0];
                    x->rep[0] = off;
                }
            }
            if (ll > nlits - lp) return -1;
            size_t room = cap - *op;
            if (ll > room) { memcpy(out + *op, lits + lp, room); *op = cap; *cut = 1; return 0; }
            memcpy(out + *op, lits + lp, ll);
            *op += ll;
            lp += ll;
            if (off > *op - base) return -1;
            room = cap - *op;
            size_t m = ml > room ? room : ml;
            for (size_t k = 0; k < m; k++, (*op)++) out[*op] = out[*op - off];
            if (m < ml) { *cut = 1; return 0; }
        }
    }
    size_t rest = nlits - lp, room = cap - *op;
    if (rest > room) { memcpy(out + *op, lits + lp, room); *op = cap; *cut = 1; return 0; }
    memcpy(out + *op, lits + lp, rest);
    *op += rest;
    return 0;
}

/* A whole .zst file in memory to out (cap bytes). Returns the bytes written, or -1. *cut (if given) is set when out filled up. */
static long fi_unzstd(const uint8_t *in, size_t n, uint8_t *out, size_t cap, int *cut) {
    size_t ip = 0, op = 0;
    int cutx = 0, frames = 0;
    ZsCtx *x = (ZsCtx *)malloc(sizeof *x);
    if (!x) return -1;
    while (ip + 4 <= n && !cutx) {
        uint32_t magic = in[ip] | (uint32_t)in[ip + 1] << 8 | (uint32_t)in[ip + 2] << 16 | (uint32_t)in[ip + 3] << 24;
        if ((magic & 0xFFFFFFF0u) == 0x184D2A50u) { /* skippable */
            if (ip + 8 > n) break;
            uint32_t sz = in[ip + 4] | (uint32_t)in[ip + 5] << 8 | (uint32_t)in[ip + 6] << 16 | (uint32_t)in[ip + 7] << 24;
            if (sz > n - ip - 8) break;
            ip += 8 + sz;
            continue;
        }
        if (magic != 0xFD2FB528u) break;
        ip += 4;
        if (ip >= n) { free(x); return -1; }
        unsigned fd = in[ip++];
        int single = (fd >> 5) & 1, checksum = (fd >> 2) & 1, dict = fd & 3, fcs = fd >> 6;
        if (fd & 0x08) { free(x); return -1; }
        if (!single) ip++;
        static const int dict_len[4] = {0, 1, 2, 4};
        ip += (size_t)dict_len[dict];
        if (dict) { free(x); return -1; } /* a dictionary: not here */
        ip += (size_t)(fcs == 0 ? (single ? 1 : 0) : fcs == 1 ? 2 : fcs == 2 ? 4 : 8);
        if (ip > n) { free(x); return -1; }
        memset(x, 0, sizeof *x);
        x->rep[0] = 1; x->rep[1] = 4; x->rep[2] = 8;
        size_t base = op;
        frames++;
        for (;;) {
            if (ip + 3 > n) { free(x); return -1; }
            uint32_t bh = in[ip] | (uint32_t)in[ip + 1] << 8 | (uint32_t)in[ip + 2] << 16;
            ip += 3;
            int last = bh & 1, bt = (int)((bh >> 1) & 3);
            size_t bs = bh >> 3;
            if (bt == 3 || bs > ZS_BLOCK_MAX) { free(x); return -1; }
            if (bt == 1) { /* RLE */
                if (ip >= n) { free(x); return -1; }
                size_t room = cap - op, m = bs > room ? room : bs;
                memset(out + op, in[ip], m);
                op += m;
                ip++;
                if (m < bs) cutx = 1;
            } else {
                if (bs > n - ip) { free(x); return -1; }
                if (bt == 0) {
                    size_t room = cap - op, m = bs > room ? room : bs;
                    memcpy(out + op, in + ip, m);
                    op += m;
                    if (m < bs) cutx = 1;
                } else if (zs_block(x, in + ip, bs, out, cap, &op, base, &cutx) != 0) { free(x); return -1; }
                ip += bs;
            }
            if (cutx || last) break;
        }
        if (checksum && !cutx) ip += 4;
    }
    free(x);
    if (!frames) return -1;
    if (cut && cutx) *cut = 1;
    return (long)op;
}

#endif

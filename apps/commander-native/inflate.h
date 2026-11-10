/* inflate.h: DEFLATE (RFC 1951) decompression for Commander, with the zlib (RFC 1950) and gzip (RFC 1952) wrappers.
 *
 * Small and dependency-free, written for r3i6nos (MIT). It unpacks ZIP members, .gz files, PNG image data, the control part of a
 * .deb and compressed PDF streams, so Commander can show what is inside them. Output is capped by the caller: a member bigger than
 * the cap is cut there (fi_inflate reports how much it wrote and whether it stopped early), never read to the end into memory.
 * Every read is bounds-checked; corrupt input returns -1, never reads or writes out of its buffers. */
#ifndef COMMANDER_INFLATE_H
#define COMMANDER_INFLATE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    const uint8_t *in;
    size_t inlen, pos;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;
    size_t outcap, outpos;
    int full; /* the output cap was reached */
} FiState;

typedef struct {
    short count[16]; /* codes of each length */
    short symbol[320];
} FiHuff;

/* -1: the input ended. */
static int fi_bits(FiState *s, int need) {
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->pos >= s->inlen) return -1;
        val |= (uint32_t)s->in[s->pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

/* Build a canonical Huffman table; 0 if usable (complete or a single code), else nonzero. */
static int fi_build(FiHuff *h, const short *length, int n) {
    short offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[length[i]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len < 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return -1;
    }
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (int i = 0; i < n; i++)
        if (length[i]) h->symbol[offs[length[i]]++] = (short)i;
    return left;
}

static int fi_decode(FiState *s, const FiHuff *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        int b = fi_bits(s, 1);
        if (b < 0) return -1;
        code |= b;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -2;
}

static const short FI_LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short FI_LEXT[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const short FI_DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                                   4097, 6145, 8193, 12289, 16385, 24577};
static const short FI_DEXT[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/* 0 ok, 1 output full, -1 bad data. */
static int fi_codes(FiState *s, const FiHuff *lit, const FiHuff *dist) {
    for (;;) {
        int sym = fi_decode(s, lit);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (s->outpos >= s->outcap) return 1;
            s->out[s->outpos++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) return -1;
            int e = fi_bits(s, FI_LEXT[sym]);
            if (e < 0) return -1;
            int len = FI_LBASE[sym] + e;
            int ds = fi_decode(s, dist);
            if (ds < 0 || ds >= 30) return -1;
            e = fi_bits(s, FI_DEXT[ds]);
            if (e < 0) return -1;
            size_t d = (size_t)(FI_DBASE[ds] + e);
            if (d > s->outpos) return -1;
            while (len--) {
                if (s->outpos >= s->outcap) return 1;
                s->out[s->outpos] = s->out[s->outpos - d];
                s->outpos++;
            }
        }
    }
}

static int fi_fixed(FiState *s) {
    static FiHuff lit, dist;
    static int ready;
    if (!ready) {
        short len[288];
        int i = 0;
        for (; i < 144; i++) len[i] = 8;
        for (; i < 256; i++) len[i] = 9;
        for (; i < 280; i++) len[i] = 7;
        for (; i < 288; i++) len[i] = 8;
        fi_build(&lit, len, 288);
        for (i = 0; i < 30; i++) len[i] = 5;
        fi_build(&dist, len, 30);
        ready = 1;
    }
    return fi_codes(s, &lit, &dist);
}

static int fi_dynamic(FiState *s) {
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short len[320];
    FiHuff lit, dist;
    int nlen = fi_bits(s, 5), ndist = fi_bits(s, 5), ncode = fi_bits(s, 4);
    if (nlen < 0 || ndist < 0 || ncode < 0) return -1;
    nlen += 257;
    ndist += 1;
    ncode += 4;
    if (nlen > 286 || ndist > 30) return -1;
    int i = 0;
    for (; i < ncode; i++) {
        int b = fi_bits(s, 3);
        if (b < 0) return -1;
        len[order[i]] = (short)b;
    }
    for (; i < 19; i++) len[order[i]] = 0;
    if (fi_build(&lit, len, 19) != 0) return -1;
    i = 0;
    while (i < nlen + ndist) {
        int sym = fi_decode(s, &lit);
        if (sym < 0) return -1;
        if (sym < 16) { len[i++] = (short)sym; continue; }
        short rep = 0;
        int count;
        if (sym == 16) {
            if (i == 0) return -1;
            rep = len[i - 1];
            count = fi_bits(s, 2);
            if (count < 0) return -1;
            count += 3;
        } else if (sym == 17) {
            count = fi_bits(s, 3);
            if (count < 0) return -1;
            count += 3;
        } else {
            count = fi_bits(s, 7);
            if (count < 0) return -1;
            count += 11;
        }
        if (i + count > nlen + ndist) return -1;
        while (count--) len[i++] = rep;
    }
    if (len[256] == 0) return -1;
    int err = fi_build(&lit, len, nlen);
    if (err < 0 || (err > 0 && nlen - lit.count[0] != 1)) return -1;
    err = fi_build(&dist, len + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - dist.count[0] != 1)) return -1;
    return fi_codes(s, &lit, &dist);
}

/* Raw DEFLATE. Returns the bytes written to out (at most outcap) or -1 for bad data; *full is set when the cap cut it short and
 * *used (if given) to the input bytes consumed. */
static long fi_inflate(const uint8_t *in, size_t inlen, uint8_t *out, size_t outcap, int *full, size_t *used) {
    FiState s;
    memset(&s, 0, sizeof s);
    s.in = in;
    s.inlen = inlen;
    s.out = out;
    s.outcap = outcap;
    if (full) *full = 0;
    int last, rc = 0;
    do {
        last = fi_bits(&s, 1);
        int type = fi_bits(&s, 2);
        if (last < 0 || type < 0) return -1;
        if (type == 0) {
            s.bitbuf = 0;
            s.bitcnt = 0;
            if (s.pos + 4 > s.inlen) return -1;
            unsigned n = s.in[s.pos] | (unsigned)s.in[s.pos + 1] << 8, nc = s.in[s.pos + 2] | (unsigned)s.in[s.pos + 3] << 8;
            s.pos += 4;
            if (n != (~nc & 0xFFFF)) return -1;
            if (s.pos + n > s.inlen) return -1;
            size_t room = s.outcap - s.outpos, take = n < room ? n : room;
            memcpy(s.out + s.outpos, s.in + s.pos, take);
            s.outpos += take;
            s.pos += n;
            if (take < n) rc = 1;
        } else if (type == 1) {
            rc = fi_fixed(&s);
        } else if (type == 2) {
            rc = fi_dynamic(&s);
        } else {
            return -1;
        }
        if (rc < 0) return s.outpos ? (long)s.outpos : -1; /* keep what came out before the damage */
        if (rc == 1) { if (full) *full = 1; break; }
    } while (!last);
    if (used) *used = s.pos;
    return (long)s.outpos;
}

/* zlib wrapper (PNG, PDF FlateDecode). */
static long fi_zlib(const uint8_t *in, size_t inlen, uint8_t *out, size_t outcap, int *full) {
    if (inlen < 2 || (in[0] & 0x0F) != 8 || ((in[0] << 8) | in[1]) % 31 != 0 || (in[1] & 0x20)) return -1;
    return fi_inflate(in + 2, inlen - 2, out, outcap, full, NULL);
}

/* The start of a gzip member's DEFLATE data, or 0 when it is not gzip. Fills the stored name (if any). */
static size_t fi_gzip_start(const uint8_t *in, size_t inlen, char *name, size_t namecap) {
    if (name && namecap) name[0] = 0;
    if (inlen < 18 || in[0] != 0x1F || in[1] != 0x8B || in[2] != 8) return 0;
    int flg = in[3];
    size_t p = 10;
    if (flg & 4) {
        if (p + 2 > inlen) return 0;
        p += 2 + (in[p] | (size_t)in[p + 1] << 8);
    }
    if (flg & 8) {
        size_t k = 0;
        while (p < inlen && in[p]) {
            if (name && k + 1 < namecap) name[k++] = (char)in[p];
            p++;
        }
        if (name && namecap) name[k] = 0;
        p++;
    }
    if (flg & 16) {
        while (p < inlen && in[p]) p++;
        p++;
    }
    if (flg & 2) p += 2;
    return p < inlen ? p : 0;
}

static long fi_gunzip(const uint8_t *in, size_t inlen, uint8_t *out, size_t outcap, int *full) {
    size_t p = fi_gzip_start(in, inlen, NULL, 0);
    if (!p) return -1;
    return fi_inflate(in + p, inlen - p, out, outcap, full, NULL);
}

#endif

/* lzma.h: xz (LZMA2) decompression for Commander, with no library. Written for r3i6nos (MIT) from the public xz file format and
 * LZMA specifications.
 *
 * fi_unxz() unpacks a whole .xz file held in memory into a caller's buffer of `cap` + FI_XZ_SLACK bytes: one or several streams, one or several
 * blocks, LZMA2 as the only filter (what `xz` and Debian's .deb data parts use; a BCJ, delta or other filter returns -1). The output
 * buffer is the dictionary, so a match can only point at bytes already written since the block began. The integrity check (CRC32,
 * CRC64, SHA-256) is skipped over, not verified. Every read of the input and every write of the output is bounds-checked: damaged
 * input returns -1, never reads or writes outside the buffers. Output past `cap` is cut there (*cut set), never written. */
#ifndef COMMANDER_LZMA_H
#define COMMANDER_LZMA_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The output buffer a caller gives fi_unxz holds cap + FI_XZ_SLACK bytes: the chunk that crosses cap is decoded whole (at most 2 MiB). */
#define FI_XZ_SLACK (2u * 1024u * 1024u + 1024u)

typedef struct {
    const uint8_t *in;      /* this chunk's packed bytes */
    size_t pos, end;
    uint32_t range, code;
    int bad;                /* the input ended early */
} LzRc;

typedef struct {
    uint16_t is_match[12][16], is_rep[12], is_rep0[12], is_rep1[12], is_rep2[12], is_rep0_long[12][16];
    uint16_t pos_slot[4][64], pos_special[115], align[16];
    uint16_t len_choice[2], len_choice2[2], len_low[2][16][8], len_mid[2][16][8], len_high[2][256];
    uint16_t lit[0x300 * 16];
    int lc, lp, pb, state;
    uint32_t rep[4];
} LzModel;

static void lz_probs_reset(LzModel *m) {
    uint16_t *p = (uint16_t *)m;
    size_t n = offsetof(LzModel, lc) / sizeof(uint16_t);
    for (size_t i = 0; i < n; i++) p[i] = 1024;
    m->state = 0;
    m->rep[0] = m->rep[1] = m->rep[2] = m->rep[3] = 0;
}

static void lz_rc_norm(LzRc *r) {
    if (r->range < (1u << 24)) {
        r->range <<= 8;
        if (r->pos < r->end) r->code = (r->code << 8) | r->in[r->pos++];
        else { r->code <<= 8; r->bad = 1; }
    }
}

static int lz_bit(LzRc *r, uint16_t *prob) {
    uint32_t bound = (r->range >> 11) * *prob;
    int bit;
    if (r->code < bound) { r->range = bound; *prob = (uint16_t)(*prob + ((2048 - *prob) >> 5)); bit = 0; }
    else { r->range -= bound; r->code -= bound; *prob = (uint16_t)(*prob - (*prob >> 5)); bit = 1; }
    lz_rc_norm(r);
    return bit;
}

static unsigned lz_tree(LzRc *r, uint16_t *probs, int bits) {
    unsigned m = 1;
    for (int i = 0; i < bits; i++) m = (m << 1) + (unsigned)lz_bit(r, &probs[m]);
    return m - (1u << bits);
}

static unsigned lz_tree_rev(LzRc *r, uint16_t *probs, int bits) {
    unsigned m = 1, sym = 0;
    for (int i = 0; i < bits; i++) {
        unsigned b = (unsigned)lz_bit(r, &probs[m]);
        m = (m << 1) + b;
        sym |= b << i;
    }
    return sym;
}

static unsigned lz_direct(LzRc *r, int bits) {
    unsigned res = 0;
    for (int i = 0; i < bits; i++) {
        r->range >>= 1;
        r->code -= r->range;
        uint32_t t = 0u - (r->code >> 31);
        r->code += r->range & t;
        res = (res << 1) + (t + 1);
        lz_rc_norm(r);
    }
    return res;
}

static unsigned lz_len(LzRc *r, LzModel *m, int rep, unsigned pos_state) {
    if (!lz_bit(r, &m->len_choice[rep])) return lz_tree(r, m->len_low[rep][pos_state], 3);
    if (!lz_bit(r, &m->len_choice2[rep])) return 8 + lz_tree(r, m->len_mid[rep][pos_state], 3);
    return 16 + lz_tree(r, m->len_high[rep], 8);
}

/* One LZMA chunk: decode until `want` bytes are written at out[*op..]; `base` is where the dictionary began. 0, or -1 if damaged. */
static int lz_chunk(LzRc *r, LzModel *m, uint8_t *out, size_t *op, size_t base, size_t want) {
    size_t end = *op + want, o = *op;
    int state = m->state;
    uint32_t rep0 = m->rep[0], rep1 = m->rep[1], rep2 = m->rep[2], rep3 = m->rep[3];
    unsigned pb_mask = (1u << m->pb) - 1, lp_mask = (1u << m->lp) - 1;
    while (o < end) {
        if (r->bad) return -1;
        size_t pos = o - base;
        unsigned pos_state = (unsigned)pos & pb_mask;
        if (!lz_bit(r, &m->is_match[state][pos_state])) {
            unsigned prev = pos ? out[o - 1] : 0;
            uint16_t *probs = m->lit + 0x300 * ((((unsigned)pos & lp_mask) << m->lc) + (prev >> (8 - m->lc)));
            unsigned sym = 1;
            if (state >= 7) {
                if (rep0 >= pos) return -1;
                unsigned match = out[o - rep0 - 1];
                do {
                    unsigned mb = (match >> 7) & 1;
                    match <<= 1;
                    unsigned b = (unsigned)lz_bit(r, &probs[((1 + mb) << 8) + sym]);
                    sym = (sym << 1) | b;
                    if (mb != b) break;
                } while (sym < 0x100);
            }
            while (sym < 0x100) sym = (sym << 1) | (unsigned)lz_bit(r, &probs[sym]);
            out[o++] = (uint8_t)sym;
            state = state < 4 ? 0 : state < 10 ? state - 3 : state - 6;
            continue;
        }
        unsigned len;
        if (!lz_bit(r, &m->is_rep[state])) {
            rep3 = rep2; rep2 = rep1; rep1 = rep0;
            len = lz_len(r, m, 0, pos_state);
            state = state < 7 ? 7 : 10;
            unsigned ls = len < 3 ? len : 3;
            unsigned slot = lz_tree(r, m->pos_slot[ls], 6);
            if (slot < 4) rep0 = slot;
            else {
                int nd = (int)(slot >> 1) - 1;
                uint32_t dist = (2u | (slot & 1)) << nd;
                if (slot < 14) dist += lz_tree_rev(r, m->pos_special + dist - slot, nd);
                else {
                    dist += lz_direct(r, nd - 4) << 4;
                    dist += lz_tree_rev(r, m->align, 4);
                }
                rep0 = dist;
            }
            if (rep0 == 0xFFFFFFFFu) return -1; /* the end marker: LZMA2 never has one */
        } else {
            if (!lz_bit(r, &m->is_rep0[state])) {
                if (!lz_bit(r, &m->is_rep0_long[state][pos_state])) { /* a one-byte match at rep0 */
                    if (rep0 >= pos) return -1;
                    out[o] = out[o - rep0 - 1];
                    o++;
                    state = state < 7 ? 9 : 11;
                    continue;
                }
            } else {
                uint32_t dist;
                if (!lz_bit(r, &m->is_rep1[state])) dist = rep1;
                else {
                    if (!lz_bit(r, &m->is_rep2[state])) dist = rep2;
                    else { dist = rep3; rep3 = rep2; }
                    rep2 = rep1;
                }
                rep1 = rep0;
                rep0 = dist;
            }
            len = lz_len(r, m, 1, pos_state);
            state = state < 7 ? 8 : 11;
        }
        len += 2;
        if (rep0 >= pos || len > end - o) return -1; /* before the dictionary, or over the chunk's end */
        for (unsigned i = 0; i < len; i++, o++) out[o] = out[o - rep0 - 1];
    }
    if (r->bad) return -1;
    m->state = state;
    m->rep[0] = rep0; m->rep[1] = rep1; m->rep[2] = rep2; m->rep[3] = rep3;
    *op = o;
    return 0;
}

/* A variable-length integer (7 bits a byte, low first); -1 if it runs out or is over 63 bits. */
static int lz_vli(const uint8_t *b, size_t n, size_t *p, uint64_t *v) {
    *v = 0;
    for (int i = 0; i < 9; i++) {
        if (*p >= n) return -1;
        uint8_t c = b[(*p)++];
        *v |= (uint64_t)(c & 0x7F) << (7 * i);
        if (!(c & 0x80)) return 0;
    }
    return -1;
}

/* Unpacks the LZMA2 data of one block at in[*ip..]; appends to out. 0, or -1 (damaged); *cut when out filled up. */
static int lz_lzma2(const uint8_t *in, size_t n, size_t *ip, uint8_t *out, size_t cap, size_t *op, int *cut, LzModel *m) {
    size_t base = *op;
    int have_props = 0;
    for (;;) {
        if (*ip >= n) return -1;
        unsigned c = in[(*ip)++];
        if (c == 0) return 0;
        if (c == 1 || c == 2) { /* stored */
            if (*ip + 2 > n) return -1;
            size_t size = (((size_t)in[*ip] << 8) | in[*ip + 1]) + 1;
            *ip += 2;
            if (*ip + size > n) return -1;
            if (c == 1) base = *op; /* a dictionary reset */
            if (size > cap + FI_XZ_SLACK - *op) return -1;
            memcpy(out + *op, in + *ip, size);
            *op += size;
            *ip += size;
            if (*op > cap) { *op = cap; *cut = 1; return 0; }
            continue;
        }
        if (c < 0x80) return -1;
        if (*ip + 4 > n) return -1;
        size_t unpacked = ((size_t)(c & 0x1F) << 16 | (size_t)in[*ip] << 8 | in[*ip + 1]) + 1;
        size_t packed = (((size_t)in[*ip + 2] << 8) | in[*ip + 3]) + 1;
        *ip += 4;
        int reset = (int)((c >> 5) & 3);
        if (reset == 3) base = *op;
        if (reset >= 2) {
            if (*ip >= n) return -1;
            unsigned pr = in[(*ip)++];
            if (pr >= 9 * 5 * 5) return -1;
            m->pb = (int)(pr / 45);
            m->lp = (int)((pr % 45) / 9);
            m->lc = (int)(pr % 9);
            if (m->lc + m->lp > 4) return -1;
            have_props = 1;
        }
        if (!have_props) return -1;
        if (reset >= 1) lz_probs_reset(m);
        if (*ip + packed > n || packed < 5) return -1;
        LzRc r;
        r.in = in;
        r.pos = *ip + 1;
        r.end = *ip + packed;
        r.bad = 0;
        if (in[*ip] != 0) return -1;
        r.code = (uint32_t)in[*ip + 1] << 24 | (uint32_t)in[*ip + 2] << 16 | (uint32_t)in[*ip + 3] << 8 | in[*ip + 4];
        r.pos = *ip + 5;
        r.range = 0xFFFFFFFFu;
        if (unpacked > cap + FI_XZ_SLACK - *op) return -1; /* the caller's slack holds the chunk that crosses cap */
        if (lz_chunk(&r, m, out, op, base, unpacked) != 0) return -1;
        *ip += packed;
        if (*op >= cap) { if (*op > cap) *cut = 1; else if (*ip < n && in[*ip] != 0) *cut = 1; *op = cap; return 0; }
    }
}

/* A whole .xz file in memory to out (cap bytes). Returns the bytes written, or -1. *cut (if given) is set when out filled up. */
static long fi_unxz(const uint8_t *in, size_t n, uint8_t *out, size_t cap, int *cut) {
    static const uint8_t magic[6] = {0xFD, '7', 'z', 'X', 'Z', 0};
    int cutx = 0;
    size_t ip = 0, op = 0;
    LzModel *m = (LzModel *)malloc(sizeof *m);
    if (!m) return -1;
    memset(m, 0, sizeof *m);
    int streams = 0;
    while (ip + 12 <= n) {
        if (memcmp(in + ip, magic, 6)) break;
        static const int check_len[16] = {0, 4, 4, 4, 8, 8, 8, 16, 16, 16, 32, 32, 32, 64, 64, 64};
        int check = check_len[in[ip + 7] & 15];
        ip += 12;
        streams++;
        for (;;) { /* blocks, then the index */
            if (ip >= n) { free(m); return -1; }
            if (in[ip] == 0) break;
            size_t start = ip, hsize = ((size_t)in[ip] + 1) * 4;
            if (ip + hsize > n || hsize < 8) { free(m); return -1; }
            size_t p = ip + 1;
            unsigned flags = in[p++];
            uint64_t v;
            if (flags & 0x3C) { free(m); return -1; }
            if (flags & 0x40 && lz_vli(in, ip + hsize, &p, &v) != 0) { free(m); return -1; }
            if (flags & 0x80 && lz_vli(in, ip + hsize, &p, &v) != 0) { free(m); return -1; }
            if ((flags & 3) != 0) { free(m); return -1; } /* more than the one LZMA2 filter */
            uint64_t id, psize;
            if (lz_vli(in, ip + hsize, &p, &id) != 0 || lz_vli(in, ip + hsize, &p, &psize) != 0 || id != 0x21 || psize != 1) { free(m); return -1; }
            if (p >= ip + hsize || in[p] > 40) { free(m); return -1; }
            ip += hsize;
            if (lz_lzma2(in, n, &ip, out, cap, &op, &cutx, m) != 0) { free(m); return -1; }
            if (cutx) { if (cut) *cut = 1; free(m); return (long)op; }
            ip += (4 - ((ip - start) & 3)) & 3; /* block padding */
            ip += (size_t)check;
        }
        /* the index: 0x00, a count, two integers a record, padding, a CRC32; then the 12-byte footer */
        size_t p = ip + 1;
        uint64_t count, a;
        if (lz_vli(in, n, &p, &count) != 0) { free(m); return -1; }
        for (uint64_t i = 0; i < count; i++) if (lz_vli(in, n, &p, &a) != 0 || lz_vli(in, n, &p, &a) != 0) { free(m); return -1; }
        p += (4 - ((p - ip) & 3)) & 3;
        p += 4 + 12;
        if (p > n) { free(m); return -1; }
        ip = p;
        while (ip + 4 <= n && in[ip] == 0 && in[ip + 1] == 0 && in[ip + 2] == 0 && in[ip + 3] == 0) ip += 4; /* stream padding */
    }
    free(m);
    if (!streams) return -1;
    if (cut && cutx) *cut = 1;
    return (long)op;
}

#endif

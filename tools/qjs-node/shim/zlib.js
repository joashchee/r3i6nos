// node:zlib's inflateSync and deflateSync for QuickJS, in plain JS. inflate is complete (stored, fixed and dynamic Huffman blocks), so any PNG
// opens. deflate writes fixed-Huffman blocks with greedy LZ77 matching: valid, smaller than raw, but NOT byte-identical to zlib's output (a PNG
// made here has the same pixels as one made by Node, in a larger file). Pure. deflate is table-driven (2026-11-09) and writes the same bytes
// as before; tests/test_qjs_node.mjs keeps the first version as its reference.
import { Buffer } from './buffer.js';

const LBASE = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258];
const LEXT = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0];
const DBASE = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577];
const DEXT = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13];
const CLORDER = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15];


/** A canonical Huffman decoder from code lengths: counts per length and symbols in code order. */
function huff(lengths) {
  const count = new Uint16Array(16), symbol = new Uint16Array(lengths.length);
  for (const l of lengths) count[l]++;
  count[0] = 0;
  const offs = new Uint16Array(16);
  for (let l = 1; l < 15; l++) offs[l + 1] = offs[l] + count[l];
  lengths.forEach((l, s) => { if (l) symbol[offs[l]++] = s; });
  return { count, symbol };
}

export function inflateSync(input) {
  const b = input instanceof Uint8Array ? input : new Uint8Array(input);
  if (b.length < 2 || (b[0] & 15) !== 8 || ((b[0] << 8) | b[1]) % 31) throw new Error('incorrect header check');
  let pos = 2, bitBuf = 0, bitCnt = 0, out = new Uint8Array(Math.max(1024, b.length * 4)), n = 0;
  const bits = (need) => {
    while (bitCnt < need) { if (pos >= b.length) throw new Error('unexpected end of file'); bitBuf |= b[pos++] << bitCnt; bitCnt += 8; }
    const v = bitBuf & ((1 << need) - 1); bitBuf >>>= need; bitCnt -= need; return v;
  };
  const room = (k) => { if (n + k > out.length) { const bigger = new Uint8Array(Math.max(out.length * 2, n + k)); bigger.set(out.subarray(0, n)); out = bigger; } };
  const decode = (h) => {
    let code = 0, first = 0, index = 0;
    for (let len = 1; len <= 15; len++) {
      code |= bits(1);
      const c = h.count[len];
      if (code - c < first) return h.symbol[index + (code - first)];
      index += c; first += c; first <<= 1; code <<= 1;
    }
    throw new Error('invalid code');
  };
  const fixedL = huff(Array.from({ length: 288 }, (_, i) => (i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8))), fixedD = huff(new Array(30).fill(5));
  for (let last = 0; !last;) {
    last = bits(1);
    const type = bits(2);
    if (type === 0) {
      bitBuf = 0; bitCnt = 0; // stored: byte aligned
      const len = b[pos] | (b[pos + 1] << 8);
      if ((len ^ 0xffff) !== (b[pos + 2] | (b[pos + 3] << 8))) throw new Error('invalid stored block lengths');
      pos += 4; room(len); out.set(b.subarray(pos, pos + len), n); n += len; pos += len;
      continue;
    }
    let L, D;
    if (type === 1) { L = fixedL; D = fixedD; }
    else if (type === 2) {
      const nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4, cl = new Array(19).fill(0);
      for (let i = 0; i < ncode; i++) cl[CLORDER[i]] = bits(3);
      const ch = huff(cl), lens = [];
      while (lens.length < nlen + ndist) {
        const sym = decode(ch);
        if (sym < 16) lens.push(sym);
        else { let rep, val = 0; if (sym === 16) { if (!lens.length) throw new Error('invalid bit length repeat'); val = lens.at(-1); rep = 3 + bits(2); } else if (sym === 17) rep = 3 + bits(3); else rep = 11 + bits(7); for (; rep; rep--) lens.push(val); }
      }
      L = huff(lens.slice(0, nlen)); D = huff(lens.slice(nlen, nlen + ndist));
    } else throw new Error('invalid block type');
    for (;;) {
      const sym = decode(L);
      if (sym < 256) { room(1); out[n++] = sym; continue; }
      if (sym === 256) break;
      const li = sym - 257;
      if (li >= 29) throw new Error('invalid literal/length code');
      const len = LBASE[li] + bits(LEXT[li]), di = decode(D);
      if (di >= 30) throw new Error('invalid distance code');
      const dist = DBASE[di] + bits(DEXT[di]);
      if (dist > n) throw new Error('invalid distance too far back');
      room(len);
      for (let k = 0; k < len; k++, n++) out[n] = out[n - dist]; // may overlap: byte by byte
    }
  }
  return Buffer.from(out.subarray(0, n)); // a copy of exactly the output
}

// The fixed Huffman codes, bit-reversed once (deflate writes a Huffman code most significant bit first into a stream filled from the low
// bit): LITREV/LITLEN for the 286 literal/length symbols, DISTREV for the 30 distance codes. LCODE gives a match length's symbol index
// (0-28) and DCODE a distance's (1-32768). Tables, so a byte costs no allocation: under QuickJS (no JIT) the old per-byte `[code, bits]`
// arrays and spread calls made deflate half of Painter's `sheet item` (10 s of 20 s on a Mac, about 190 s on the R36S, 2026-11-09).
const rev = (code, k) => { let r = 0; for (let i = 0; i < k; i++) r |= ((code >> i) & 1) << (k - 1 - i); return r; };
const LITREV = new Uint16Array(288), LITLEN = new Uint8Array(288);
for (let s = 0; s < 288; s++) {
  const [code, k] = s < 144 ? [0x30 + s, 8] : s < 256 ? [0x190 + s - 144, 9] : s < 280 ? [s - 256, 7] : [0xc0 + s - 280, 8];
  LITREV[s] = rev(code, k); LITLEN[s] = k;
}
const DISTREV = new Uint8Array(30);
for (let d = 0; d < 30; d++) DISTREV[d] = rev(d, 5);
const LCODE = new Uint8Array(259);
for (let len = 3, li = 0; len <= 258; len++) { while (li < 28 && LBASE[li + 1] <= len) li++; LCODE[len] = li; }
const DCODE = new Uint8Array(32769);
for (let dist = 1, di = 0; dist <= 32768; dist++) { while (di < 29 && DBASE[di + 1] <= dist) di++; DCODE[dist] = di; }

/** Adler-32 in runs of 5552 bytes (the most before the sums can pass 2^32), so a byte costs two adds, not two divisions. Same value. */
const adler32 = (b) => {
  let a = 1, c = 0;
  for (let i = 0; i < b.length;) {
    const end = Math.min(b.length, i + 5552);
    for (; i < end; i++) { a += b[i]; c += a; }
    a %= 65521; c %= 65521;
  }
  return ((c << 16) | a) >>> 0;
};

/** One final fixed-Huffman block with greedy LZ77 matching (a 32 KB window, chains of one). The output is byte for byte what this shim
 *  wrote before it was made faster (2026-11-09; tests/test_qjs_node.mjs keeps the old one as a reference). */
export function deflateSync(input) {
  const b = input instanceof Uint8Array ? input : new Uint8Array(input), n = b.length;
  let out = new Uint8Array(Math.max(64, (n >> 2) + 64)), o = 0;
  let acc = 0, nacc = 0;
  const grow = () => { const bigger = new Uint8Array(out.length * 2); bigger.set(out); out = bigger; };
  const put = (v, k) => { // v is at most 13 bits and nacc below 8 here, so acc stays within 21 bits
    acc |= v << nacc; nacc += k;
    while (nacc >= 8) { if (o >= out.length) grow(); out[o++] = acc & 255; acc >>>= 8; nacc -= 8; }
  };
  out[o++] = 0x78; out[o++] = 0x01;
  put(1, 1); put(1, 2); // one final block, fixed Huffman
  const head = new Int32Array(1 << 15).fill(-1);
  for (let i = 0; i < n;) {
    let len = 0, dist = 0;
    if (i + 2 < n) {
      const h = ((b[i] << 10) ^ (b[i + 1] << 5) ^ b[i + 2]) & 0x7fff, j = head[h];
      head[h] = i;
      if (j >= 0 && i - j <= 32768) { const max = Math.min(258, n - i); while (len < max && b[j + len] === b[i + len]) len++; dist = i - j; }
    }
    if (len >= 3) {
      const li = LCODE[len], ls = 257 + li;
      put(LITREV[ls], LITLEN[ls]); if (LEXT[li]) put(len - LBASE[li], LEXT[li]);
      const di = DCODE[dist];
      put(DISTREV[di], 5); if (DEXT[di]) put(dist - DBASE[di], DEXT[di]);
      for (let k = 1; k < len && i + k + 2 < n; k++) { const p = i + k; head[((b[p] << 10) ^ (b[p + 1] << 5) ^ b[p + 2]) & 0x7fff] = p; }
      i += len;
    } else { const s = b[i]; put(LITREV[s], LITLEN[s]); i++; }
  }
  put(LITREV[256], LITLEN[256]);
  if (nacc) { if (o >= out.length) grow(); out[o++] = acc & 255; }
  const sum = adler32(b);
  while (o + 4 > out.length) grow();
  out[o++] = sum >>> 24; out[o++] = (sum >>> 16) & 255; out[o++] = (sum >>> 8) & 255; out[o++] = sum & 255;
  return Buffer.from(out.subarray(0, o));
}
export default { inflateSync, deflateSync };

// The part of Node's Buffer the sprite, music and animation tools use, on Uint8Array. Pure.
import { TextEncoder, TextDecoder } from './text.js';

const enc = new TextEncoder();
const latin1 = (s) => Uint8Array.from(s, (c) => c.charCodeAt(0) & 255);

export class Buffer extends Uint8Array {
  static alloc(n, fill = 0) { const b = new Buffer(n); if (fill) b.fill(fill); return b; }
  static from(x, a, b) {
    if (typeof x === 'string') return Buffer.#of(a === 'latin1' || a === 'binary' ? latin1(x) : a === 'hex' ? Uint8Array.from(x.match(/../g) ?? [], (h) => parseInt(h, 16)) : enc.encode(x));
    if (x instanceof ArrayBuffer) return new Buffer(x, a ?? 0, b ?? x.byteLength - (a ?? 0)); // a view on the same memory, as in Node
    if (ArrayBuffer.isView(x) && !Array.isArray(x)) return Buffer.#of(new Uint8Array(x.buffer.slice(x.byteOffset, x.byteOffset + x.byteLength))); // a copy
    return Buffer.#of(Uint8Array.from(x));
  }
  static #of(u8) { return new Buffer(u8.buffer, u8.byteOffset, u8.length); }
  static concat(list, total) {
    const n = total ?? list.reduce((s, x) => s + x.length, 0), out = Buffer.alloc(n);
    let at = 0;
    for (const x of list) { out.set(x.subarray(0, Math.max(0, n - at)), at); at += x.length; if (at >= n) break; }
    return out;
  }
  static isBuffer(x) { return x instanceof Buffer; }
  static byteLength(s) { return typeof s === 'string' ? enc.encode(s).length : s.byteLength; }
  subarray(a, b) { const u = super.subarray(a, b); return new Buffer(u.buffer, u.byteOffset, u.length); }
  slice(a, b) { return this.subarray(a, b); }
  equals(o) { return this.length === o.length && this.every((v, i) => v === o[i]); }
  copy(target, tStart = 0, sStart = 0, sEnd = this.length) { const part = this.subarray(sStart, sEnd); target.set(part.subarray(0, Math.max(0, target.length - tStart)), tStart); return Math.min(part.length, target.length - tStart); }
  write(str, offset = 0, encoding = 'utf8') {
    if (typeof offset === 'string') { encoding = offset; offset = 0; }
    const bytes = encoding === 'latin1' || encoding === 'binary' ? latin1(str) : enc.encode(str), n = Math.min(bytes.length, this.length - offset);
    this.set(bytes.subarray(0, n), offset); return n;
  }
  toString(encoding = 'utf8', a = 0, b = this.length) {
    const part = this.subarray(a, b);
    if (encoding === 'latin1' || encoding === 'binary') return Array.from(part, (c) => String.fromCharCode(c)).join('');
    if (encoding === 'hex') return Array.from(part, (c) => c.toString(16).padStart(2, '0')).join('');
    return new TextDecoder().decode(part);
  }
  readUInt8(o = 0) { return this[o]; }
  readUInt16BE(o = 0) { return (this[o] << 8) | this[o + 1]; }
  readUInt32BE(o = 0) { return ((this[o] << 24) | (this[o + 1] << 16) | (this[o + 2] << 8) | this[o + 3]) >>> 0; }
  readUInt32LE(o = 0) { return ((this[o + 3] << 24) | (this[o + 2] << 16) | (this[o + 1] << 8) | this[o]) >>> 0; }
  writeUInt8(v, o = 0) { this[o] = v; return o + 1; }
  writeUInt16BE(v, o = 0) { this[o] = v >>> 8; this[o + 1] = v; return o + 2; }
  writeUInt32BE(v, o = 0) { this[o] = v >>> 24; this[o + 1] = v >>> 16; this[o + 2] = v >>> 8; this[o + 3] = v; return o + 4; }
  writeUInt32LE(v, o = 0) { this[o + 3] = v >>> 24; this[o + 2] = v >>> 16; this[o + 1] = v >>> 8; this[o] = v; return o + 4; }
}
export default { Buffer };

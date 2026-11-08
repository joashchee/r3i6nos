// TextEncoder and TextDecoder (UTF-8 only) for QuickJS, which has neither. Pure: no host calls, so Node can test it against its own.
export class TextEncoder {
  get encoding() { return 'utf-8'; }
  encode(s = '') {
    const out = [];
    for (const ch of String(s)) {
      let c = ch.codePointAt(0);
      if (c >= 0xd800 && c <= 0xdfff) c = 0xfffd; // a lone surrogate, as the platform's encoder writes it
      if (c < 0x80) out.push(c);
      else if (c < 0x800) out.push(0xc0 | (c >> 6), 0x80 | (c & 63));
      else if (c < 0x10000) out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
      else out.push(0xf0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
    }
    return Uint8Array.from(out);
  }
}

export class TextDecoder {
  constructor(label = 'utf-8', { fatal = false } = {}) {
    if (!/^utf-?8$/i.test(label)) throw new RangeError(`TextDecoder: only utf-8 is here, not ${label}`);
    this.fatal = !!fatal;
  }
  get encoding() { return 'utf-8'; }
  /** The WHATWG UTF-8 decoder: a bad sequence is one U+FFFD (or a TypeError when `fatal`), with the same byte-by-byte rules as the platform's. */
  decode(input = new Uint8Array(0)) {
    const b = input instanceof ArrayBuffer ? new Uint8Array(input) : new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
    let i = b[0] === 0xef && b[1] === 0xbb && b[2] === 0xbf ? 3 : 0; // a byte order mark is dropped, as the platform does
    const out = [];
    let cp = 0, need = 0, seen = 0, lower = 0x80, upper = 0xbf;
    const bad = () => { if (this.fatal) throw new TypeError('The encoded data was not valid for encoding utf-8'); out.push(0xfffd); };
    while (i < b.length) {
      const x = b[i];
      if (need === 0) {
        if (x < 0x80) out.push(x);
        else if (x >= 0xc2 && x <= 0xdf) { need = 1; cp = x & 0x1f; }
        else if (x >= 0xe0 && x <= 0xef) { if (x === 0xe0) lower = 0xa0; if (x === 0xed) upper = 0x9f; need = 2; cp = x & 0xf; }
        else if (x >= 0xf0 && x <= 0xf4) { if (x === 0xf0) lower = 0x90; if (x === 0xf4) upper = 0x8f; need = 3; cp = x & 7; }
        else bad();
        i++;
      } else if (x < lower || x > upper) { cp = need = seen = 0; lower = 0x80; upper = 0xbf; bad(); } // not consumed: it is read again as a first byte
      else { lower = 0x80; upper = 0xbf; cp = (cp << 6) | (x & 0x3f); i++; if (++seen === need) { out.push(cp); cp = need = seen = 0; } }
    }
    if (need) bad();
    let s = '';
    for (let k = 0; k < out.length; k += 4096) s += String.fromCodePoint(...out.slice(k, k + 4096));
    return s;
  }
}

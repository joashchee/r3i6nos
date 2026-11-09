// The Node-compat shims for QuickJS (tools/qjs-node/shim) against Node's own implementations. The shims that touch the host (fs, process) need QuickJS
// and are run by scripts/test_qjs_node.sh; the pure ones are checked here, in both directions where there is one (our deflate through Node's inflate,
// Node's deflate through our inflate).
import assert from 'node:assert/strict';
import nodeUtil from 'node:util';
import nodePath from 'node:path';
import nodeZlib from 'node:zlib';
import nodeCrypto from 'node:crypto';
import { parseArgs } from '../tools/qjs-node/shim/util.js';
import * as path from '../tools/qjs-node/shim/path.js';
import { inflateSync, deflateSync } from '../tools/qjs-node/shim/zlib.js';
import { createHash } from '../tools/qjs-node/shim/crypto.js';
import { Buffer as B } from '../tools/qjs-node/shim/buffer.js';
import { TextEncoder as E, TextDecoder as D } from '../tools/qjs-node/shim/text.js';

let n = 0;
const ok = (name, f) => { f(); n++; console.log(`  ok ${name}`); };

ok('parseArgs gives what Node gives for the options the three CLIs use', () => {
  const options = { out: { type: 'string' }, dir: { type: 'string' }, strict: { type: 'boolean' }, 'steps-per-beat': { type: 'string' }, rows: { type: 'boolean' }, s: { type: 'string', short: 's' }, v: { type: 'boolean', short: 'v' }, tag: { type: 'string', multiple: true }, scale: { type: 'string', default: '4' } };
  const cases = [[], ['make', 'song', 'form=loop', 'key=A', '--out', 'x.mid'], ['--out=x.wav', 'make', 'bass'], ['kits', '--strict'], ['a', '--', '--out', 'b'], ['--tag', 'a', '--tag', 'b', 'c'],
    ['-s', 'x', '-v'], ['-sx'], ['-vs', 'y'], ['--steps-per-beat', '4', 'p'], ['show', 'bass', 'pattern=walking', '--rows'], ['x', '--scale', '8'], ['--out', ''], ['-', 'a']];
  for (const args of cases) assert.deepEqual({ ...parseArgs({ args, options, allowPositionals: true }).values }, { ...nodeUtil.parseArgs({ args, options, allowPositionals: true }).values }, JSON.stringify(args)),
    assert.deepEqual(parseArgs({ args, options, allowPositionals: true }).positionals, nodeUtil.parseArgs({ args, options, allowPositionals: true }).positionals, JSON.stringify(args));
  for (const args of [['--nope'], ['--out'], ['--strict=1'], ['--out', '--strict'], ['-z'], ['--out', '-x']]) {
    let mine, theirs;
    try { parseArgs({ args, options, allowPositionals: true }); } catch (e) { mine = e.code; }
    try { nodeUtil.parseArgs({ args, options, allowPositionals: true }); } catch (e) { theirs = e.code; }
    assert.equal(mine, theirs, `error for ${JSON.stringify(args)}`);
  }
  assert.throws(() => parseArgs({ args: ['x'], options }), { code: 'ERR_PARSE_ARGS_UNEXPECTED_POSITIONAL' });
});

ok('path: join, normalize, resolve, basename, dirname, extname match Node', () => {
  path.setCwd(() => process.cwd());
  const xs = ['a/b', 'a//b/', './a/../b', '/a/b/../../c', '../x', 'a/b.txt', '/', '', '.', 'a/', '/a.b/c.d.e', '.hidden', 'a/.b', '//a'];
  for (const x of xs) {
    assert.equal(path.normalize(x), nodePath.normalize(x), `normalize ${x}`);
    assert.equal(path.basename(x), nodePath.basename(x), `basename ${x}`);
    assert.equal(path.dirname(x), nodePath.dirname(x), `dirname ${x}`);
    assert.equal(path.extname(x), nodePath.extname(x), `extname ${x}`);
    assert.equal(path.resolve(x), nodePath.resolve(x), `resolve ${x}`);
    assert.equal(path.isAbsolute(x), nodePath.isAbsolute(x));
  }
  for (const [a, b] of [['a', 'b'], ['/a', 'b/../c'], ['', 'x'], ['a/', '/b'], ['out', 'night.mid']]) assert.equal(path.join(a, b), nodePath.join(a, b), `join ${a} ${b}`);
  assert.equal(path.basename('x/a.mid', '.mid'), nodePath.basename('x/a.mid', '.mid'));
});

ok('TextEncoder and TextDecoder match the platform on good and bad UTF-8', () => {
  for (const t of ['', 'abc', 'é€😀', 'a\ud800b', '\u0000\u007f\u0080߿ࠀ￿']) assert.deepEqual([...new E().encode(t)], [...new TextEncoder().encode(t)], JSON.stringify(t));
  for (const b of [[0x61, 0xc3, 0xa9, 0xe2, 0x82, 0xac, 0xf0, 0x9f, 0x98, 0x80], [0xff, 0x61], [0xc3], [0xe2, 0x82], [0xed, 0xa0, 0x80], [0xf0, 0x80, 0x80, 0x80], [0xef, 0xbb, 0xbf, 0x41], [0xc0, 0xaf], [0xf4, 0x90, 0x80, 0x80], [0xe0, 0x80, 0x80], [0x80], [0xe2, 0x28, 0xa1], [0xf0, 0x9f, 0x98]])
    assert.equal(new D().decode(Uint8Array.from(b)), new TextDecoder().decode(Uint8Array.from(b)), JSON.stringify(b));
  assert.throws(() => new D('utf-8', { fatal: true }).decode(Uint8Array.from([0xff])), TypeError);
  assert.throws(() => new TextDecoder('utf-8', { fatal: true }).decode(Uint8Array.from([0xff])), TypeError);
});

ok('Buffer: the calls the tools make behave as Node\'s', () => {
  const a = B.from('héllo'), r = Buffer.from('héllo');
  assert.deepEqual([...a], [...r]); assert.equal(a.toString(), 'héllo'); assert.equal(a.toString('hex'), r.toString('hex'));
  assert.deepEqual([...B.concat([B.from([1, 2]), B.from([3])])], [1, 2, 3]);
  const x = B.alloc(12), y = Buffer.alloc(12);
  for (const q of [x, y]) { q.writeUInt32BE(0xdeadbeef, 0); q.write('IHDR', 4, 'latin1'); q.writeUInt32BE(7, 8); }
  assert.deepEqual([...x], [...y]); assert.equal(x.readUInt32BE(0), 0xdeadbeef); assert.equal(x.readUInt8(4), 73);
  assert.ok(B.from([1, 2, 3]).subarray(1) instanceof B); assert.ok(B.from([1, 2]).equals(B.from([1, 2]))); assert.ok(!B.from([1, 2]).equals(B.from([1, 3])));
  const t = B.alloc(6); B.from([9, 8, 7]).copy(t, 2); assert.deepEqual([...t], [0, 0, 9, 8, 7, 0]);
  const src = new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8]), view = B.from(src.buffer, 2, 4); assert.deepEqual([...view], [3, 4, 5, 6]); src[2] = 99; assert.equal(view[0], 99, 'from(ArrayBuffer) shares memory');
  const rgba = new Uint8Array(16).map((_, i) => i); assert.deepEqual([...B.from(rgba.buffer, rgba.byteOffset + 4, 8)], [4, 5, 6, 7, 8, 9, 10, 11]);
});

ok('zlib: inflate reads what Node deflates at every level, deflate is read by Node, and it compresses', () => {
  let seed = 12345; const rnd = () => (seed = (seed * 1103515245 + 12345) & 0x7fffffff) >>> 16;
  const sets = { empty: new Uint8Array(0), one: Uint8Array.of(7), zeros: new Uint8Array(100000), text: new TextEncoder().encode('the quick brown fox '.repeat(2000)), noise: Uint8Array.from({ length: 70000 }, rnd),
    image: Uint8Array.from({ length: 640 * 4 * 100 }, (_, i) => ((i >> 2) % 640 < 320 ? 16 : (i * 7) & 255)), tiny: new TextEncoder().encode('abcabcabcabc') };
  for (const [name, data] of Object.entries(sets)) {
    for (const level of [0, 1, 6, 9]) assert.deepEqual([...inflateSync(nodeZlib.deflateSync(data, { level }))], [...data], `${name} level ${level}`);
    const mine = deflateSync(data);
    assert.deepEqual([...nodeZlib.inflateSync(mine)], [...data], `${name}: Node reads our deflate`);
    assert.deepEqual([...inflateSync(mine)], [...data], `${name}: we read our deflate`);
    if (data.length > 5000 && name !== 'noise') assert.ok(mine.length < data.length / 4, `${name} shrinks: ${data.length} -> ${mine.length}`);
  }
  assert.throws(() => inflateSync(Uint8Array.of(0, 0, 0)), /header/);
  assert.throws(() => inflateSync(nodeZlib.deflateSync(sets.text).subarray(0, 20)));
  // The table-driven deflate (2026-11-09, made faster for Painter's `sheet` on the R36S) writes exactly what the first one did: that one is
  // kept here as the reference, verbatim but for its name.
  const LB = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258], LX = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0];
  const DB = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577], DX = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13];
  const fixedCode = (s) => (s < 144 ? [0x30 + s, 8] : s < 256 ? [0x190 + s - 144, 9] : s < 280 ? [s - 256, 7] : [0xc0 + s - 280, 8]);
  const adler = (b) => { let a = 1, c = 0; for (let i = 0; i < b.length; i++) { a = (a + b[i]) % 65521; c = (c + a) % 65521; } return ((c << 16) | a) >>> 0; };
  const reference = (b) => {
    const out = [0x78, 0x01]; let acc = 0, nacc = 0;
    const put = (v, k) => { acc |= v << nacc; nacc += k; while (nacc >= 8) { out.push(acc & 255); acc >>>= 8; nacc -= 8; } };
    const putCode = (code, k) => { let r = 0; for (let i = 0; i < k; i++) r |= ((code >> i) & 1) << (k - 1 - i); put(r, k); };
    put(1, 1); put(1, 2);
    const head = new Int32Array(1 << 15).fill(-1), hash = (i) => ((b[i] << 10) ^ (b[i + 1] << 5) ^ b[i + 2]) & 0x7fff;
    for (let i = 0; i < b.length;) {
      let len = 0, dist = 0;
      if (i + 2 < b.length) { const h = hash(i), j = head[h]; head[h] = i; if (j >= 0 && i - j <= 32768) { const max = Math.min(258, b.length - i); while (len < max && b[j + len] === b[i + len]) len++; dist = i - j; } }
      if (len >= 3) {
        let li = 28; while (LB[li] > len) li--; putCode(...fixedCode(257 + li)); put(len - LB[li], LX[li]);
        let di = 29; while (DB[di] > dist) di--; putCode(di, 5); put(dist - DB[di], DX[di]);
        for (let k = 1; k < len && i + k + 2 < b.length; k++) head[hash(i + k)] = i + k;
        i += len;
      } else { putCode(...fixedCode(b[i])); i++; }
    }
    putCode(...fixedCode(256)); if (nacc) out.push(acc & 255);
    const sum = adler(b); out.push(sum >>> 24, (sum >>> 16) & 255, (sum >>> 8) & 255, sum & 255);
    return out;
  };
  const edge = new Uint8Array(32768 + 600); for (let i = 0; i < 300; i++) edge[i] = rnd() & 255; edge.set(edge.subarray(0, 300), 32768);
  const all = { ...sets, edge, bytes: Uint8Array.from({ length: 256 * 40 }, (_, i) => i & 255), two: Uint8Array.of(1, 2), three: Uint8Array.of(1, 2, 3) };
  for (const [name, data] of Object.entries(all)) assert.deepEqual([...deflateSync(data)], reference(data), `${name}: the same bytes as the first deflate`);
});

ok('crypto: sha256 matches Node for every length around the block edges, strings and bytes', () => {
  for (let len = 0; len <= 130; len++) { const d = Uint8Array.from({ length: len }, (_, i) => (i * 31 + 7) & 255); assert.equal(createHash('sha256').update(d).digest('hex'), nodeCrypto.createHash('sha256').update(d).digest('hex'), `len ${len}`); }
  const big = Uint8Array.from({ length: 100000 }, (_, i) => (i * 13) & 255);
  assert.equal(createHash('sha256').update(big).digest('hex'), nodeCrypto.createHash('sha256').update(big).digest('hex'));
  assert.equal(createHash('sha256').update('é€').update(Buffer.from('x')).digest('hex'), nodeCrypto.createHash('sha256').update('é€').update(Buffer.from('x')).digest('hex'));
  assert.equal(createHash('SHA256').update('a').digest().length, 32);
  assert.throws(() => createHash('md5'));
});

console.log(`test_qjs_node: ok (${n} groups)`);

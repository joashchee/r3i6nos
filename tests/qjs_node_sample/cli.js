// A sample Node-style CLI that uses everything the shims provide. scripts/test_qjs_node.sh builds it into a QuickJS binary and checks it does what
// Node does with it.
import { parseArgs } from 'node:util';
import { mkdirSync, readFileSync, writeFileSync, existsSync, readdirSync, statSync } from 'node:fs';
import { deflateSync, inflateSync } from 'node:zlib';
import { digest, where } from './util.js';

const { values, positionals } = parseArgs({ allowPositionals: true, options: { dir: { type: 'string' }, loud: { type: 'boolean' }, n: { type: 'string' } } });
const [cmd, ...rest] = positionals;
const fail = (m) => { console.error(m); process.exit(2); };

if (cmd === 'echo') process.stdout.write(`${rest.join(' ')}${values.loud ? '!' : ''}\n`);
else if (cmd === 'files') {
  const dir = values.dir ?? fail('files needs --dir');
  mkdirSync(where(dir, 'a/b'), { recursive: true });
  writeFileSync(where(dir, 'a/hello.txt'), 'héllo €\n');
  const bytes = Uint8Array.from({ length: 5000 }, (_, i) => (i * 7) & 255);
  writeFileSync(where(dir, 'a/b/data.bin'), bytes);
  console.log(readFileSync(where(dir, 'a/hello.txt'), 'utf8').trim(), readFileSync(where(dir, 'a/b/data.bin')).length, existsSync(where(dir, 'a')), existsSync(where(dir, 'nope')));
  console.log(readdirSync(where(dir, 'a')).join(','), statSync(where(dir, 'a/b/data.bin')).size, statSync(where(dir, 'a')).isDirectory());
  const z = deflateSync(readFileSync(where(dir, 'a/b/data.bin')));
  console.log('zlib', digest(inflateSync(z)) === digest(bytes), z.length < bytes.length * 2);
  try { readFileSync(where(dir, 'missing')); } catch (e) { console.log('error code', e.code); }
}
else if (cmd === 'misc') {
  const o = { a: [1, 2, { b: new Date(0) }], m: new Map([[1, 'x']]) };
  const c = structuredClone(o);
  console.log(c !== o, c.a[2].b.getTime(), c.m.get(1), JSON.stringify(new TextEncoder().encode('é€')), new TextDecoder().decode(new TextEncoder().encode('héllo')));
  console.log(digest(Buffer.from('abc')), Buffer.concat([Buffer.from([1]), Buffer.from([2, 3])]).toString('hex'), Buffer.from('hi').readUInt8(1), 2n ** 70n);
  console.log(values.n === undefined ? 'no n' : `n=${Number(values.n) * 2}`);
}
else if (cmd === 'throws') throw new Error('boom from the sample');
else if (cmd === 'reject') await Promise.reject(new Error('rejected from the sample'));
else if (cmd === 'exit') process.exit(Number(rest[0] ?? 0));
else fail(`unknown command ${cmd ?? ''}`);

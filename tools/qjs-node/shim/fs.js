// node:fs for QuickJS (std and os): the synchronous calls the CLIs use. Errors carry Node's `code`.
import * as std from 'std';
import * as os from 'os';
import { Buffer } from './buffer.js';
import { TextEncoder, TextDecoder } from './text.js';

const fail = (code, syscall, p, text) => Object.assign(new Error(`${code}: ${text}, ${syscall} '${p}'`), { code, syscall, path: p });
const kind = (st) => st.mode & os.S_IFMT;

export function readFileSync(p, options) {
  const [st, e] = os.stat(String(p));
  if (e) throw fail('ENOENT', 'open', p, 'no such file or directory');
  if (kind(st) === os.S_IFDIR) throw fail('EISDIR', 'read', p, 'illegal operation on a directory');
  const f = std.open(String(p), 'rb');
  if (!f) throw fail('EACCES', 'open', p, 'permission denied');
  let bytes;
  if (st.size > 0) { bytes = new Uint8Array(st.size); const n = f.read(bytes.buffer, 0, st.size); if (n !== st.size) bytes = bytes.subarray(0, Math.max(0, n)); }
  else { const chunks = []; for (;;) { const c = new Uint8Array(65536), n = f.read(c.buffer, 0, c.length); if (n <= 0) break; chunks.push(c.subarray(0, n)); } bytes = Buffer.concat(chunks); }
  f.close();
  const enc = typeof options === 'string' ? options : options?.encoding;
  const buf = new Buffer(bytes.buffer, bytes.byteOffset, bytes.length);
  return enc ? buf.toString(enc === 'utf-8' ? 'utf8' : enc) : buf;
}

export function writeFileSync(p, data, options) {
  const enc = typeof options === 'string' ? options : options?.encoding;
  const bytes = typeof data === 'string' ? (enc === 'latin1' || enc === 'binary' ? Buffer.from(data, 'latin1') : new TextEncoder().encode(data)) : ArrayBuffer.isView(data) ? data : new Uint8Array(data);
  const f = std.open(String(p), 'wb');
  if (!f) throw fail('ENOENT', 'open', p, 'no such file or directory');
  const n = f.write(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  f.close();
  if (n !== bytes.byteLength) throw fail('EIO', 'write', p, 'i/o error');
}

export function existsSync(p) { return os.stat(String(p))[1] === 0; }

export function mkdirSync(p, options) {
  const recursive = typeof options === 'object' && options?.recursive;
  const make = (d) => {
    const [st, e] = os.stat(d);
    if (!e) { if (kind(st) === os.S_IFDIR && recursive) return; throw fail('EEXIST', 'mkdir', d, 'file already exists'); }
    const r = os.mkdir(d, 0o777);
    if (r < 0) throw fail('ENOENT', 'mkdir', d, 'no such file or directory');
  };
  const path = String(p).replace(/\/+$/, '') || '/';
  if (!recursive) return make(path);
  const parts = path.split('/');
  for (let i = path.startsWith('/') ? 2 : 1; i <= parts.length; i++) { const d = parts.slice(0, i).join('/'); if (d) make(d); }
}

export function readdirSync(p) {
  const [names, e] = os.readdir(String(p));
  if (e) throw fail('ENOENT', 'scandir', p, 'no such file or directory');
  return names.filter((n) => n !== '.' && n !== '..').sort();
}

export function statSync(p) {
  const [st, e] = os.stat(String(p));
  if (e) throw fail('ENOENT', 'stat', p, 'no such file or directory');
  return { size: st.size, mtimeMs: st.mtime, mode: st.mode, isFile: () => kind(st) === os.S_IFREG, isDirectory: () => kind(st) === os.S_IFDIR };
}

export function unlinkSync(p) { if (os.remove(String(p)) < 0) throw fail('ENOENT', 'unlink', p, 'no such file or directory'); }
export default { readFileSync, writeFileSync, existsSync, mkdirSync, readdirSync, statSync, unlinkSync };

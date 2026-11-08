// What a Node program expects to find around it, for QuickJS: process, console.error and friends, TextEncoder and TextDecoder, Buffer,
// structuredClone. Imported first by the entry, so it runs before the program's own modules.
import * as std from 'std';
import * as os from 'os';
import { TextEncoder, TextDecoder } from './text.js';
import { Buffer } from './buffer.js';
import { setCwd } from './path.js';

globalThis.TextEncoder ??= TextEncoder;
globalThis.TextDecoder ??= TextDecoder;
globalThis.Buffer ??= Buffer;
setCwd(() => os.getcwd()[0]);

/** A deep copy of plain data: objects, arrays, typed arrays, Map, Set, Date, with shared references and cycles kept. */
function clone(v, seen = new Map()) {
  if (v === null || typeof v !== 'object') { if (typeof v === 'function' || typeof v === 'symbol') throw Object.assign(new Error(`${typeof v === 'function' ? 'function' : 'symbol'} could not be cloned`), { name: 'DataCloneError' }); return v; }
  if (seen.has(v)) return seen.get(v);
  let out;
  if (Array.isArray(v)) { out = []; seen.set(v, out); v.forEach((x, i) => { out[i] = clone(x, seen); }); }
  else if (ArrayBuffer.isView(v)) { out = new v.constructor(v.buffer.slice(v.byteOffset, v.byteOffset + v.byteLength)); seen.set(v, out); }
  else if (v instanceof Date) out = new Date(v.getTime());
  else if (v instanceof Map) { out = new Map(); seen.set(v, out); for (const [k, x] of v) out.set(clone(k, seen), clone(x, seen)); }
  else if (v instanceof Set) { out = new Set(); seen.set(v, out); for (const x of v) out.add(clone(x, seen)); }
  else { out = {}; seen.set(v, out); for (const k of Object.keys(v)) out[k] = clone(v[k], seen); }
  return out;
}
globalThis.structuredClone ??= (v) => clone(v);

/** A small `util.inspect`: what console.log prints for a value that is not a string. Primitives, Errors, arrays and plain objects as Node prints them
 * when they are short (one line up to 72 characters, nested three deep); the long and exotic cases (arrays of more than six numbers in columns, class
 * instances, getters) are not matched. A top-level string is printed as it is. */
function inspect(v, depth = 0, nested = false) {
  if (typeof v === 'string') return nested ? `'${v.replace(/\\/g, '\\\\').replace(/'/g, "\\'").replace(/\n/g, '\\n')}'` : v;
  if (typeof v === 'bigint') return `${v}n`;
  if (typeof v === 'number') return Object.is(v, -0) ? '-0' : String(v);
  if (typeof v === 'function') return `[Function: ${v.name || '(anonymous)'}]`;
  if (typeof v === 'symbol') return v.toString();
  if (v === null || typeof v !== 'object') return String(v);
  if (v instanceof Error) return v.stack ?? `${v.name}: ${v.message}`;
  if (v instanceof Date) return v.toISOString();
  if (ArrayBuffer.isView(v) && !(v instanceof DataView)) return `${v.constructor.name}(${v.length}) [ ${Array.from(v.subarray(0, 100)).join(', ')}${v.length > 100 ? `, ... ${v.length - 100} more items` : ''} ]`;
  if (depth > 2) return Array.isArray(v) ? '[Array]' : '[Object]';
  const parts = v instanceof Map ? Array.from(v, ([k, x]) => `${inspect(k, depth + 1, true)} => ${inspect(x, depth + 1, true)}`)
    : v instanceof Set ? Array.from(v, (x) => inspect(x, depth + 1, true))
    : Array.isArray(v) ? v.map((x) => inspect(x, depth + 1, true))
    : Object.keys(v).map((k) => `${/^[A-Za-z_$][\w$]*$/.test(k) ? k : `'${k}'`}: ${inspect(v[k], depth + 1, true)}`);
  const open = v instanceof Map ? `Map(${v.size}) {` : v instanceof Set ? `Set(${v.size}) {` : Array.isArray(v) ? '[' : '{', close = open.endsWith('{') ? '}' : ']';
  if (!parts.length) return `${open}${close}`;
  const single = `${open} ${parts.join(', ')} ${close}`;
  if (single.length <= 72 && !single.includes('\n')) return single;
  const pad = '  '.repeat(depth + 1);
  return `${open}\n${parts.map((x) => pad + x.replace(/\n/g, `\n`)).join(',\n')}\n${'  '.repeat(depth)}${close}`;
}
const show = (x) => inspect(x);
const to = (stream) => (...a) => { stream.puts(`${a.map(show).join(' ')}\n`); stream.flush(); };
console.log = to(std.out);
console.error = console.warn = console.debug ??= to(std.err);
console.info = console.log;

const text = (s) => (typeof s === 'string' ? s : new TextDecoder().decode(s));
const env = {};
for (const [k, v] of Object.entries(std.getenviron())) env[k] = v;
globalThis.process = {
  argv: ['qjs', ...scriptArgs], // qjs puts the script (or the binary) first, so argv.slice(2) is the arguments, as in Node
  env, platform: 'linux', exitCode: undefined,
  cwd: () => os.getcwd()[0],
  exit(code) { std.out.flush(); std.err.flush(); std.exit(code ?? process.exitCode ?? 0); },
  stdout: { write(s) { std.out.puts(text(s)); std.out.flush(); return true; }, isTTY: false },
  stderr: { write(s) { std.err.puts(text(s)); std.err.flush(); return true; }, isTTY: false },
};

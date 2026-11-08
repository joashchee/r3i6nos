// node:path (POSIX) for QuickJS: the parts the CLIs use. Pure.
export const sep = '/';
const clean = (p) => {
  const abs = p.startsWith('/'), out = [];
  for (const s of p.split('/')) { if (!s || s === '.') continue; if (s === '..') { if (out.length && out.at(-1) !== '..') out.pop(); else if (!abs) out.push('..'); } else out.push(s); }
  return (abs ? '/' : '') + out.join('/');
};
export const normalize = (p) => { const r = clean(p); return (r || (p.startsWith('/') ? '/' : '.')) + (p.length > 1 && p.endsWith('/') && r && r !== '/' ? '/' : ''); };
export const join = (...parts) => { const p = parts.filter((x) => x !== '').join('/'); return p ? normalize(p) : '.'; };
export const isAbsolute = (p) => p.startsWith('/');
let cwdFn = () => '/';
export const setCwd = (f) => { cwdFn = f; };
export const resolve = (...parts) => {
  let r = '';
  for (let i = parts.length - 1; i >= 0 && !r.startsWith('/'); i--) if (parts[i]) r = r ? `${parts[i]}/${r}` : parts[i];
  if (!r.startsWith('/')) r = `${cwdFn()}/${r}`;
  return clean(r) || '/';
};
export const basename = (p, ext) => { const t = p.replace(/\/+$/, ''); const b = t.slice(t.lastIndexOf('/') + 1); return ext && b.endsWith(ext) && b !== ext ? b.slice(0, -ext.length) : b; };
export const dirname = (path) => { // Node's own algorithm, so its quirks (a//b/ is a/, //a is //) come out the same
  if (path.length === 0) return '.';
  const hasRoot = path.charCodeAt(0) === 47;
  let end = -1, matchedSlash = true;
  for (let i = path.length - 1; i >= 1; --i) {
    if (path.charCodeAt(i) === 47) { if (!matchedSlash) { end = i; break; } } else matchedSlash = false;
  }
  if (end === -1) return hasRoot ? '/' : '.';
  if (hasRoot && end === 1) return '//';
  return path.slice(0, end);
};
export const extname = (p) => { const b = basename(p), i = b.lastIndexOf('.'); return i <= 0 ? '' : b.slice(i); };
export default { sep, normalize, join, isAbsolute, resolve, basename, dirname, extname };

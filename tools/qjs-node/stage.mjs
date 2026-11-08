// Stages a Node CLI for QuickJS: copies the import closure of its entry file into OUT (keeping folders), rewrites each `node:X` import to the
// shim in OUT/shim, and writes OUT/main.js, which loads the shims' globals first and then the program. Run with Node.
//   node stage.mjs APPDIR ENTRY OUT      e.g.  node stage.mjs ~/Coding/composer cli.js build/composer
// A `node:` module the shims do not have stops the build, so nothing fails later on the device. Files read at run time (not imported) are not
// copied: stage them yourself.
import { readFileSync, writeFileSync, mkdirSync, rmSync, cpSync, existsSync } from 'node:fs';
import { join, dirname, resolve, relative } from 'node:path';

const [appDir, entry, out] = process.argv.slice(2).map((p) => p && resolve(p));
const entryName = process.argv[3];
if (!appDir || !entry || !out) throw new Error('usage: node stage.mjs APPDIR ENTRY OUT');
const here = dirname(new URL(import.meta.url).pathname);
const SHIMS = { 'node:fs': 'fs.js', 'node:path': 'path.js', 'node:util': 'util.js', 'node:zlib': 'zlib.js', 'node:crypto': 'crypto.js', 'node:buffer': 'buffer.js' };

rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });
cpSync(join(here, 'shim'), join(out, 'shim'), { recursive: true });

const re = /(?:\bfrom\s*|\bimport\s*\(\s*|\bimport\s+)(['"])([^'"]+)\1/g;
const done = new Set(), todo = [entryName], bad = [];
while (todo.length) {
  const name = todo.pop();
  if (done.has(name)) continue;
  done.add(name);
  const src = join(appDir, name);
  if (!existsSync(src)) throw new Error(`cannot find ${name} in ${appDir}`);
  const to = join(out, name);
  const rel = (shim) => { const r = relative(dirname(to), join(out, 'shim', shim)); return r.startsWith('.') ? r : `./${r}`; };
  const text = readFileSync(src, 'utf8').replace(re, (whole, q, spec) => {
    if (spec.startsWith('node:')) { if (!SHIMS[spec]) { bad.push(`${name}: ${spec}`); return whole; } return whole.replace(spec, rel(SHIMS[spec])); }
    if (spec.startsWith('.')) todo.push(relative(appDir, join(dirname(src), spec)));
    return whole;
  });
  mkdirSync(dirname(to), { recursive: true });
  writeFileSync(to, text);
}
if (bad.length) throw new Error(`no shim for: ${bad.join(', ')}`);
writeFileSync(join(out, 'main.js'), `import './shim/globals.js';\nimport './${entryName}';\n`);
console.log(`staged ${done.size} modules of ${entryName} into ${out}`);

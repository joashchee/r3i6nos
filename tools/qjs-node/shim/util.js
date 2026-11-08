// node:util's parseArgs for QuickJS: options of type string or boolean (short names, multiple, default), allowPositionals, strict, `--`.
// Errors are TypeErrors with Node's codes. Checked against Node's own in tests/test_qjs_node.mjs.
const err = (code, msg) => Object.assign(new TypeError(msg), { code });

export function parseArgs({ args = globalThis.process?.argv?.slice(2) ?? [], options = {}, strict = true, allowPositionals = !strict } = {}) {
  const values = { __proto__: null }, positionals = [];
  const shortOf = new Map(Object.entries(options).filter(([, o]) => o.short).map(([n, o]) => [o.short, n]));
  const set = (name, v) => { const o = options[name]; if (o?.multiple) (values[name] ??= []).push(v); else values[name] = v; };
  const unknown = (shown) => { if (strict) throw err('ERR_PARSE_ARGS_UNKNOWN_OPTION', `Unknown option '${shown}'`); };
  for (let i = 0; i < args.length; i++) {
    const a = args[i];
    if (a === '--') { for (const rest of args.slice(i + 1)) { if (strict && !allowPositionals) throw err('ERR_PARSE_ARGS_UNEXPECTED_POSITIONAL', `Unexpected argument '${rest}'`); positionals.push(rest); } break; }
    let name, inline;
    if (a.startsWith('--')) { const eq = a.indexOf('='); name = eq < 0 ? a.slice(2) : a.slice(2, eq); inline = eq < 0 ? undefined : a.slice(eq + 1); }
    else if (a.length > 1 && a[0] === '-' && a[1] !== '-') {
      // -o value, -o=value or a group -abc: every letter but the last must be a boolean
      const letters = a.slice(1);
      let j = 0;
      for (; j < letters.length; j++) {
        const n = shortOf.get(letters[j]);
        if (n === undefined) { unknown(`-${letters[j]}`); if (!strict) set(letters[j], true); continue; }
        if (options[n].type === 'string') {
          let v = letters.slice(j + 1).replace(/^=/, '');
          if (!v) { v = args[++i]; if (v === undefined) throw err('ERR_PARSE_ARGS_INVALID_OPTION_VALUE', `Option '-${letters[j]}, --${n} <value>' argument missing`); }
          set(n, v); break;
        }
        set(n, true);
      }
      continue;
    } else { if (strict && !allowPositionals) throw err('ERR_PARSE_ARGS_UNEXPECTED_POSITIONAL', `Unexpected argument '${a}'`); positionals.push(a); continue; }
    const o = options[name];
    if (!o) { unknown(`--${name}`); if (!strict) set(name, inline ?? true); continue; }
    if (o.type === 'string') {
      let v = inline;
      if (v === undefined) {
        v = args[i + 1];
        if (v === undefined || (strict && v.startsWith('-') && v !== '-')) throw err('ERR_PARSE_ARGS_INVALID_OPTION_VALUE', `Option '--${name} <value>' argument ${v === undefined ? 'missing' : 'is ambiguous'}`);
        i++;
      }
      set(name, v);
    } else {
      if (inline !== undefined) throw err('ERR_PARSE_ARGS_INVALID_OPTION_VALUE', `Option '--${name}' does not take an argument`);
      set(name, true);
    }
  }
  for (const [n, o] of Object.entries(options)) if (o.default !== undefined && values[n] === undefined) values[n] = o.default;
  return { values, positionals };
}
export default { parseArgs };

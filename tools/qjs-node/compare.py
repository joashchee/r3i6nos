#!/usr/bin/env python3
"""compare.py APP-DIR ENTRY BINARY COMMANDS-FILE

Runs every command line of COMMANDS-FILE twice, once with `node APP-DIR/ENTRY` and once with BINARY (the QuickJS build of the same program), each in its own
fresh folder that has an empty `o/` in it (a command may write there and a later one read it), and compares: the exit status, stdout, stderr and every file
written. A `.png` is compared by its decoded pixels (the shim's deflate is valid but not zlib's, so the bytes differ), after checking its CRCs and that Python's
zlib reads it. One command per line; `#` starts a comment; a line `!SLOW ...` is listed but NOT run (a maintainer's report that takes minutes under QuickJS).
Exit status 0 when everything matches.
"""
import os, struct, subprocess, sys, tempfile, shutil, zlib

def png_pixels(data, name):
    assert data[:8] == b'\x89PNG\r\n\x1a\n', f'{name}: not a PNG'
    i, idat, hdr = 8, b'', None
    while i < len(data):
        n = struct.unpack('>I', data[i:i + 4])[0]; kind = data[i + 4:i + 8]; body = data[i + 8:i + 8 + n]
        assert zlib.crc32(data[i + 4:i + 8 + n]) & 0xffffffff == struct.unpack('>I', data[i + 8 + n:i + 12 + n])[0], f'{name}: bad CRC in {kind!r}'
        if kind == b'IHDR': hdr = struct.unpack('>IIBBBBB', body)
        if kind == b'IDAT': idat += body
        i += 12 + n
    return hdr, zlib.decompress(idat)

def tree(d):
    out = {}
    for root, _, files in os.walk(d):
        for f in files:
            p = os.path.join(root, f)
            out[os.path.relpath(p, d)] = open(p, 'rb').read()
    return out

def main(app, entry, binary, commands):
    node_cmd = ['node', os.path.join(app, entry)]
    a, b = tempfile.mkdtemp(), tempfile.mkdtemp()
    for d in (a, b): os.mkdir(os.path.join(d, 'o'))
    bad = total = 0
    try:
        for line in open(commands, encoding='utf-8'):
            line = line.strip()
            if not line or line.startswith('#'): continue
            if line.startswith('!SLOW '): print(f'  --   {line[6:]}  (not run: takes minutes under QuickJS)'); continue
            args = line.split()
            ra = subprocess.run(node_cmd + args, cwd=a, capture_output=True, timeout=600)
            rb = subprocess.run([binary] + args, cwd=b, capture_output=True, timeout=600)
            total += 1
            notes = []
            if ra.returncode != rb.returncode: notes.append(f'exit {ra.returncode} (node) vs {rb.returncode}')
            if ra.stdout != rb.stdout: notes.append(f'stdout differs ({len(ra.stdout)} vs {len(rb.stdout)} bytes): {ra.stdout[:70]!r} vs {rb.stdout[:70]!r}')
            if ra.stderr != rb.stderr: notes.append(f'stderr differs: {ra.stderr[:90]!r} vs {rb.stderr[:90]!r}')
            ta, tb = tree(a), tree(b)
            for k in sorted(set(ta) | set(tb)):
                if k not in ta or k not in tb: notes.append(f'file {k} only from {"node" if k in ta else "the binary"}'); continue
                if ta[k] == tb[k]: continue
                if k.lower().endswith('.png'):
                    try:
                        if png_pixels(ta[k], k) == png_pixels(tb[k], k): continue
                        notes.append(f'file {k}: PNG pixels differ')
                    except Exception as e: notes.append(f'file {k}: {e}')
                else: notes.append(f'file {k} differs ({len(ta[k])} vs {len(tb[k])} bytes)')
            if notes: bad += 1
            print(('  ok   ' if not notes else '  DIFF ') + line + ('' if not notes else '\n         ' + '\n         '.join(notes)))
            if notes:  # carry on from the same state: make the binary's folder equal Node's, so one difference is reported once
                shutil.rmtree(b); shutil.copytree(a, b)
    finally:
        shutil.rmtree(a, ignore_errors=True); shutil.rmtree(b, ignore_errors=True)
    print(f'compare: {total - bad} of {total} commands identical' + ('' if not bad else f', {bad} DIFFERENT'))
    return 1 if bad else 0

if __name__ == '__main__':
    if len(sys.argv) != 5: sys.exit(__doc__)
    sys.exit(main(*sys.argv[1:]))

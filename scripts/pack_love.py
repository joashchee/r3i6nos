#!/usr/bin/env python3
"""pack_love.py <app-dir> <out.love>

Zips a LÖVE app (main.lua at the top of the archive, as LÖVE requires) the same way every time:
sorted names, fixed timestamps, so a rebuild of an unchanged app gives an identical file.
Standard library only.
"""

import os
import sys
import zipfile

STAMP = (1980, 1, 1, 0, 0, 0)


def pack(src: str, out: str) -> int:
    if not os.path.isfile(os.path.join(src, "main.lua")):
        raise SystemExit(f"pack_love: no main.lua in {src}")
    names = []
    for d, dirs, files in os.walk(src):
        dirs[:] = sorted(x for x in dirs if not x.startswith("."))
        names += [os.path.relpath(os.path.join(d, f), src) for f in sorted(files) if not f.startswith(".")]
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for rel in sorted(names):
            info = zipfile.ZipInfo(rel.replace(os.sep, "/"), STAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            with open(os.path.join(src, rel), "rb") as f:
                z.writestr(info, f.read())
    return len(names)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    print(f"pack_love: {pack(*sys.argv[1:])} files -> {sys.argv[2]}")

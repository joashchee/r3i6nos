#!/usr/bin/env python3
"""check_py_imports.py <rootfs> <script.py>...

Fails if a script imports a standard-library module that the rootfs's Python does not have.
Debian's python3-minimal leaves out shutil, and nothing ran deb_watcher.py in CI, so a missing
module only showed up on the device. This looks for each imported module under the rootfs's
usr/lib/python3*/ (a .py file, a package directory, or a compiled lib-dynload module).
A module built into the interpreter (sys, time, ...) is accepted by name.
Standard library only; it reads files, it does not run the rootfs's Python.
"""

import ast
import glob
import os
import sys


def imported_modules(path):
    with open(path, encoding="utf-8") as f:
        tree = ast.parse(f.read(), path)
    names = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            names.update(a.name.split(".")[0] for a in node.names)
        elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
            names.add(node.module.split(".")[0])
    return names


def available(rootfs, name):
    if name in sys.builtin_module_names:
        return True
    for lib in glob.glob(os.path.join(rootfs, "usr/lib/python3*")):
        for candidate in (f"{name}.py", name, f"lib-dynload/{name}.*.so", f"lib-dynload/{name}.so"):
            if glob.glob(os.path.join(lib, candidate)):
                return True
    return False


def missing(rootfs, scripts):
    out = []
    for script in scripts:
        for name in sorted(imported_modules(script)):
            if not available(rootfs, name):
                out.append((script, name))
    return out


def main(argv):
    if len(argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    if not glob.glob(os.path.join(argv[1], "usr/lib/python3*")):
        print(f"check_py_imports: no usr/lib/python3* under {argv[1]}", file=sys.stderr)
        return 1
    bad = missing(argv[1], argv[2:])
    for script, name in bad:
        print(f"check_py_imports: {script} imports {name}, which the rootfs does not have", file=sys.stderr)
    if not bad:
        print(f"check_py_imports: all imports of {len(argv) - 2} script(s) are in the rootfs")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

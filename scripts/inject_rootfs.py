#!/usr/bin/env python3
"""inject_rootfs.py - plan and verify what goes from the user's card into the image's root.

  inject_rootfs.py plan   <assets-dir> <debugfs-commands-out>   write a debugfs script, print a summary
  inject_rootfs.py verify <assets-dir> <dump-dir>               compare a debugfs rdump with the plan

What is injected (all from capture-card.sh's local_hardware_assets/, none of it in the repo):
  rootfs/lib/modules, rootfs/lib/firmware  ->  /usr/lib/modules, /usr/lib/firmware
      (Debian 12 has /lib -> usr/lib, so the real path is used)
  rootfs/gpu/libmali*                      ->  /opt/libmali/
      plus symlinks recreated from info/stock-gpu-libs.txt, but only those that resolve
      to a captured libmali file. loveos-session puts /opt/libmali first on
      LD_LIBRARY_PATH, so the blob's libEGL, libGLESv2 and libgbm are the ones SDL finds.

The GPU blob is the vendor's proprietary code: images built with it are for your own cards.
Standard library only.
"""

import hashlib
import os
import sys

REQUIRED_GPU_NAMES = ("libEGL.so.1", "libGLESv2.so.2", "libgbm.so.1")


class PlanError(Exception):
    pass


def needed(data: bytes):
    """DT_NEEDED names of a 64-bit little-endian ELF shared object (what it links against)."""
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise PlanError("not a 64-bit little-endian ELF file")
    u16 = lambda o: int.from_bytes(data[o:o + 2], "little")
    u32 = lambda o: int.from_bytes(data[o:o + 4], "little")
    u64 = lambda o: int.from_bytes(data[o:o + 8], "little")
    phoff, phentsize, phnum = u64(32), u16(54), u16(56)
    loads, dynamic = [], None
    for i in range(phnum):
        ph = phoff + i * phentsize
        ptype, offset, vaddr, filesz = u32(ph), u64(ph + 8), u64(ph + 16), u64(ph + 32)
        if ptype == 1:
            loads.append((vaddr, offset, filesz))
        elif ptype == 2:
            dynamic = (offset, filesz)
    if dynamic is None:
        return []

    def file_offset(vaddr):
        for v, o, size in loads:
            if v <= vaddr < v + size:
                return o + vaddr - v
        raise PlanError("dynamic string table is outside the loaded segments")

    names, strtab = [], None
    for pos in range(dynamic[0], dynamic[0] + dynamic[1], 16):
        tag, val = u64(pos), u64(pos + 8)
        if tag == 0:
            break
        if tag == 1:
            names.append(val)
        elif tag == 5:
            strtab = file_offset(val)
    out = []
    for n in names:
        start = strtab + n
        out.append(data[start:data.index(b"\0", start)].decode("ascii", "replace"))
    return out


def _quote(path: str) -> str:
    if '"' in path or "\n" in path:
        raise PlanError(f"cannot inject a file name with a quote or newline: {path!r}")
    return f'"{path}"'


def _tree_ops(src: str, dest: str):
    ops = [("mkdir", dest)]
    for d, dirs, files in os.walk(src):
        dirs.sort()
        rel = os.path.relpath(d, src)
        base = dest if rel == "." else f"{dest}/{rel}"
        if rel != ".":
            ops.append(("mkdir", base))
        for name in sorted(files):
            path = os.path.join(d, name)
            if os.path.islink(path):
                ops.append(("symlink", f"{base}/{name}", os.readlink(path)))
            else:
                ops.append(("write", path, f"{base}/{name}"))
        # os.walk lists symlinks to directories under dirs: keep them as links too.
        for name in list(dirs):
            path = os.path.join(d, name)
            if os.path.islink(path):
                ops.append(("symlink", f"{base}/{name}", os.readlink(path)))
                dirs.remove(name)
    return ops


def _read_listing(path: str):
    files, links = set(), {}
    if not os.path.isfile(path):
        return files, links
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("file "):
                files.add(line[5:])
            elif line.startswith("link ") and " -> " in line:
                name, target = line[5:].split(" -> ", 1)
                links[name] = os.path.basename(target.strip())
    return files, links


def gpu_ops(gpu_dir: str, listing_path: str, allow_partial: bool = False):
    captured = sorted(n for n in os.listdir(gpu_dir) if n.lower().startswith("libmali")
                      and os.path.isfile(os.path.join(gpu_dir, n)))
    if not captured:
        raise PlanError(f"no libmali file in {gpu_dir}: re-run capture-card.sh and read info/stock-gpu-libs.txt")
    _, links = _read_listing(listing_path)

    def resolves(name):
        for _ in range(10):
            if name in captured:
                return True
            name = links.get(name)
            if name is None:
                return False
        return False

    kept_links = sorted(n for n in links if resolves(n) and n not in captured)
    provided = set(captured) | set(kept_links)
    missing = [n for n in REQUIRED_GPU_NAMES if n not in provided]
    if missing and not allow_partial:
        raise PlanError(
            "the stock card's libmali links do not provide " + ", ".join(missing) +
            ". SDL needs them from /opt/libmali. Read info/stock-gpu-libs.txt and tell the maintainer, "
            "or set R3I6NOS_GPU_ALLOW_PARTIAL=1 to inject anyway.")

    ops = [("mkdir", "/opt/libmali")]
    for n in captured:
        ops.append(("write", os.path.join(gpu_dir, n), f"/opt/libmali/{n}"))
    for n in kept_links:
        ops.append(("symlink", f"/opt/libmali/{n}", links[n]))
    return ops


def build_plan(assets: str, allow_partial: bool = False):
    ops = []
    lib = os.path.join(assets, "rootfs", "lib")
    for name in ("modules", "firmware"):
        src = os.path.join(lib, name)
        if os.path.isdir(src):
            ops += _tree_ops(src, f"/usr/lib/{name}")
    gpu = os.path.join(assets, "rootfs", "gpu")
    if os.path.isdir(gpu):
        ops += gpu_ops(gpu, os.path.join(assets, "info", "stock-gpu-libs.txt"), allow_partial)
    if not ops:
        raise PlanError(f"nothing to inject under {assets}/rootfs (run capture-card.sh)")
    return ops


def to_commands(ops) -> str:
    lines = []
    for op in ops:
        if op[0] == "mkdir":
            lines.append(f"mkdir {_quote(op[1])}")
        elif op[0] == "write":
            lines.append(f"write {_quote(op[1])} {_quote(op[2])}")
        elif op[0] == "symlink":
            lines.append(f"symlink {_quote(op[1])} {_quote(op[2])}")
    return "\n".join(lines) + "\n"


def _sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.digest()


def verify(ops, dump_dir: str):
    """Returns a list of problems (empty when the dump matches the plan)."""
    problems = []
    for op in ops:
        dest = dump_dir.rstrip("/") + (op[2] if op[0] == "write" else op[1])
        if op[0] == "mkdir":
            if not os.path.isdir(dest):
                problems.append(f"missing directory {op[1]}")
        elif op[0] == "write":
            if not os.path.isfile(dest):
                problems.append(f"missing file {op[2]}")
            elif _sha(dest) != _sha(op[1]):
                problems.append(f"content differs: {op[2]}")
        elif op[0] == "symlink":
            if not os.path.islink(dest):
                problems.append(f"missing symlink {op[1]}")
            elif os.readlink(dest) != op[2]:
                problems.append(f"symlink {op[1]} points to {os.readlink(dest)}, expected {op[2]}")
    return problems


def main(argv) -> int:
    if len(argv) < 4 or argv[1] not in ("plan", "verify"):
        print(__doc__, file=sys.stderr)
        return 2
    allow = os.environ.get("R3I6NOS_GPU_ALLOW_PARTIAL") == "1"
    try:
        ops = build_plan(argv[2], allow)
    except PlanError as err:
        print(f"inject_rootfs: {err}", file=sys.stderr)
        return 1
    if argv[1] == "plan":
        with open(argv[3], "w", encoding="utf-8") as f:
            f.write(to_commands(ops))
        n_files = sum(1 for o in ops if o[0] == "write")
        size = sum(os.path.getsize(o[1]) for o in ops if o[0] == "write")
        for o in ops:
            if o[0] == "write" and os.path.basename(o[1]).lower().startswith("libmali"):
                try:
                    with open(o[1], "rb") as f:
                        print(f"{os.path.basename(o[1])} needs: " + " ".join(needed(f.read())))
                except PlanError as err:
                    print(f"{os.path.basename(o[1])}: {err}")
        print(f"{n_files} files ({size // 1048576} MiB), "
              f"{sum(1 for o in ops if o[0] == 'symlink')} symlinks, "
              f"{sum(1 for o in ops if o[0] == 'mkdir')} directories")
        return 0
    problems = verify(ops, argv[3])
    for p in problems[:20]:
        print(f"inject_rootfs: {p}", file=sys.stderr)
    if problems:
        print(f"inject_rootfs: {len(problems)} problems", file=sys.stderr)
        return 1
    print(f"verified {len(ops)} entries")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

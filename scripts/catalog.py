#!/usr/bin/env python3
"""scripts/catalog.py - make the package catalog's index.json from .deb files (docs/PACKAGES-OTA.md, "The catalog"; ROADMAP section 29 phase 3).

    scripts/catalog.py OUT.json URL_PREFIX FILE.deb [FILE.deb ...]

Run by scripts/publish-packages.sh inside Debian 12 (it needs dpkg-deb), which then signs OUT.json. One entry for each file, named by the
file's stem, which is also what the unit installs it as (native_deb/<stem>.deb) and the asset's name under URL_PREFIX:

    {"format": 1, "generated": "...Z", "packages": [{"name", "package", "version", "kind", "size", "sha256", "url", "min_system", "description",
                                                       "data_dir", "size_installed", "arch"}]}

kind comes from the control field X-R3i6nos-Kind (app, tool, system; default app), min_system from X-R3i6nos-Min-System (default 0.1.0) and data_dir
from X-R3i6nos-Data-Dir (the app's data folder, CLAUDE.md rule 16). It FAILS (and writes nothing) on a file that is not a Debian archive, is built for
another architecture than arm64 or all, has an unsafe stem, carries its own nttplay (CLAUDE.md rule 15), is a system package whose Package is not its
stem, or repeats a name. SOURCE_DATE_EPOCH sets the "generated" time (so a rebuild is identical).
"""
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

NAME_OK = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,63}")
KINDS = ("app", "tool", "system")
ARCHES = ("arm64", "all")
MAX_DEB = 2 * 1024 ** 3


class Bad(Exception):
    pass


def control(deb, field):
    r = subprocess.run(["dpkg-deb", "-f", str(deb), field], capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def entry(deb, prefix):
    deb = Path(deb)
    stem = deb.name[:-4] if deb.name.endswith(".deb") else ""
    if not NAME_OK.fullmatch(stem):
        raise Bad(f"{deb.name}: not a usable package file name (letters, digits and . _ + - only, ending in .deb)")
    package, version, arch = control(deb, "Package"), control(deb, "Version"), control(deb, "Architecture")
    if not package or not version:
        raise Bad(f"{deb.name}: not a Debian package (dpkg-deb could not read it)")
    if arch not in ARCHES:
        raise Bad(f"{deb.name}: built for {arch or 'no architecture'}; the unit needs arm64")
    kind = control(deb, "X-R3i6nos-Kind") or "app"
    if kind not in KINDS:
        raise Bad(f"{deb.name}: kind '{kind}' is not one of {', '.join(KINDS)}")
    if kind == "system" and package != stem:
        raise Bad(f"{deb.name}: a system package's file name must be its Package name ({package})")
    listing = subprocess.run(["dpkg-deb", "-c", str(deb)], capture_output=True, text=True).stdout
    for line in listing.splitlines():
        path = line.split(None, 5)[-1].split(" -> ")[0] if line.strip() else ""
        if path.rstrip("/").rsplit("/", 1)[-1] == "nttplay":
            raise Bad(f"{deb.name}: carries its own nttplay (CLAUDE.md rule 15: one installed copy, in the image)")
    data = deb.read_bytes()
    if len(data) > MAX_DEB:
        raise Bad(f"{deb.name}: over {MAX_DEB >> 30} GB")
    try:
        installed_kb = int(control(deb, "Installed-Size") or 0)
    except ValueError:
        installed_kb = 0
    desc = control(deb, "Description").splitlines()
    return {
        "name": stem, "package": package, "version": version, "kind": kind, "arch": arch, "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(), "url": f"{prefix.rstrip('/')}/{stem}.deb",
        "min_system": control(deb, "X-R3i6nos-Min-System") or "0.1.0", "description": desc[0] if desc else "",
        "data_dir": control(deb, "X-R3i6nos-Data-Dir"), "size_installed": installed_kb * 1024,
    }


def build(files, prefix):
    seen, out = {}, []
    for f in files:
        e = entry(f, prefix)
        if e["name"] in seen:
            raise Bad(f"two files make the name {e['name']}: {seen[e['name']]} and {f}")
        seen[e["name"]] = str(f)
        out.append(e)
    when = int(os.environ.get("SOURCE_DATE_EPOCH") or time.time())
    return {"format": 1, "generated": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(when)), "packages": sorted(out, key=lambda e: e["name"])}


def main(argv):
    if len(argv) < 3:
        print((__doc__ or "").split("\n\n")[0], file=sys.stderr)
        return 2
    out, prefix, files = Path(argv[0]), argv[1], argv[2:]
    try:
        catalog = build(files, prefix)
    except Bad as why:
        print(f"catalog: {why}", file=sys.stderr)
        return 1
    tmp = out.with_name(out.name + ".part")
    tmp.write_text(json.dumps(catalog, indent=1, sort_keys=True) + "\n")
    os.replace(tmp, out)
    print(f"catalog: {len(catalog['packages'])} package(s) in {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

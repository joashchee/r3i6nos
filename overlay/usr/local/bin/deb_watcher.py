#!/usr/bin/env python3
"""deb_watcher.py - extract native ARM64 .deb packages and write launchers.

Runs at boot (loveos-session). Every *.deb in the native_deb folder is extracted
with `dpkg-deb -x` (no dpkg database, no locks) into /opt/native_apps/<name>/
and gets a launcher script in /opt/launchers/native/<name>.sh.

The system image also carries built-in debs (the input checker) in /opt/loveos/defaults/native_deb;
they are handled the same way, and a deb of the same name on the card replaces the built-in one.
An app whose deb is gone is removed only when the card's folder is there to say so: a card that is
not mounted must never uninstall anything.

A deb that carries the file usr/share/r3i6nos/tool is a command-line TOOL, not an app: it gets no launcher (a menu entry that prints a usage line and
exits would only confuse), and each executable in its usr/bin gets a small wrapper in the tools folder (/opt/loveos/bin), which run_app.sh and SSH
logins put on PATH, so a LÖVE or native app can run it (os.execute, io.popen) by name. A tool never replaces another package's command, and its
wrappers go when its deb does (under the same rule: only when the card's folder is there to say so).

Both cards (docs/PACKAGES-OTA.md): the system card's native_deb (/roms) and the data card's (/roms2) are read, and a package on both, or
also built in, is installed from the one with the higher Debian version (the later folder when a version cannot be read). Which folder a
package came from is written beside it (.r3i6nos-src); it is removed only when THAT folder is there to say it is gone.

A package that would not fit in the root filesystem (Installed-Size against free space, keeping a reserve) is refused before it is extracted,
and the reason is in LAST for r3i6nos-pkg to print; it is not half-installed and an older version of it stays as it was.

Paths can be overridden with environment variables so the tests run anywhere:
R3I6NOS_DEB_DIR, R3I6NOS_DEB_DIR2, R3I6NOS_DEFAULT_DEB_DIR, R3I6NOS_APP_ROOT, R3I6NOS_LAUNCHER_DIR, R3I6NOS_TOOLS_DIR,
R3I6NOS_PKG_RESERVE_MB (200) and R3I6NOS_PKG_RESERVE_PCT (10), R3I6NOS_PKG_PROGRESS.

Progress (CLAUDE.md rule 18, ROADMAP section 41 step 9): while packages are extracted the watcher writes PROGRESS_FILE (step=, steps=, doing=: one
step for each package it has to extract, the file gone when it ends) for the menu to show a bar, and calls PROGRESS(step, steps, doing) when a caller
set one (r3i6nos-pkg prints a bar on a terminal). Nothing to extract, nothing written.
"""

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

DEB_DIR = Path(os.environ.get("R3I6NOS_DEB_DIR", "/roms/native_deb"))
DEB_DIR2 = Path(os.environ.get("R3I6NOS_DEB_DIR2", "/roms2/native_deb"))   # the data card's
DEFAULT_DEB_DIR = Path(os.environ.get("R3I6NOS_DEFAULT_DEB_DIR", "/opt/loveos/defaults/native_deb"))
APP_ROOT = Path(os.environ.get("R3I6NOS_APP_ROOT", "/opt/native_apps"))
LAUNCHER_DIR = Path(os.environ.get("R3I6NOS_LAUNCHER_DIR", "/opt/launchers/native"))
TOOLS_DIR = Path(os.environ.get("R3I6NOS_TOOLS_DIR", "/opt/loveos/bin"))
TOOL_MARKER = "usr/share/r3i6nos/tool"
TOOL_LIST = ".r3i6nos-tools"  # in the extracted tree: the wrappers this package made
WRAPPER_TAG = "# r3i6nos-tool "
STAMP = ".r3i6nos-deb"  # records which deb (size:mtime) an extraction came from
SRC = ".r3i6nos-src"    # the folder that deb was in (removal needs that folder to be there)
META = ".r3i6nos-meta"  # json: version, installed size
MIN_RESERVE = int(os.environ.get("R3I6NOS_PKG_RESERVE_MB", "200")) * 1024 * 1024  # a package may not leave the root filesystem with
RESERVE_PCT = int(os.environ.get("R3I6NOS_PKG_RESERVE_PCT", "10"))                # less than this, or this % of it, free (the larger)
PROGRESS_FILE = Path(os.environ.get("R3I6NOS_PKG_PROGRESS", "/run/loveos/pkg-progress"))
PROGRESS = None         # a caller's callback(step, steps, doing)
LAST = {}               # name -> (status, message) of the latest process_debs: installed, kept, refused, failed

BIN_DIRS = ("usr/games", "usr/bin", "bin", "usr/local/games", "usr/local/bin")


def control(deb: Path, field: str):
    """A field of the deb's control file (dpkg-deb -f), or None when it cannot be read."""
    try:
        out = subprocess.run(["dpkg-deb", "-f", str(deb), field], check=True, capture_output=True, text=True, timeout=30).stdout.strip()
    except (subprocess.SubprocessError, OSError):
        return None
    return out or None


def _order(c: str) -> int:
    if c == "~":
        return -1
    if c.isdigit():
        return 0
    return ord(c) if c.isalpha() else ord(c) + 256


def _cmp_part(a: str, b: str) -> int:
    """dpkg's comparison of one version part (upstream or revision)."""
    i = j = 0
    while i < len(a) or j < len(b):
        while (i < len(a) and not a[i].isdigit()) or (j < len(b) and not b[j].isdigit()):
            ca = _order(a[i]) if i < len(a) and not a[i].isdigit() else 0
            cb = _order(b[j]) if j < len(b) and not b[j].isdigit() else 0
            if ca != cb:
                return -1 if ca < cb else 1
            i += i < len(a) and not a[i].isdigit()
            j += j < len(b) and not b[j].isdigit()
        ni = nj = 0
        while i < len(a) and a[i].isdigit():
            ni, i = ni * 10 + int(a[i]), i + 1
        while j < len(b) and b[j].isdigit():
            nj, j = nj * 10 + int(b[j]), j + 1
        if ni != nj:
            return -1 if ni < nj else 1
    return 0


def vercmp(a: str, b: str) -> int:
    """Compare two Debian versions: -1, 0 or 1 (epoch, upstream, revision; ~ sorts before anything)."""
    def split(v):
        epoch, _, rest = v.rpartition(":") if ":" in v else ("0", "", v)
        up, _, rev = rest.rpartition("-") if "-" in rest else (rest, "", "")
        return int(epoch) if epoch.isdigit() else 0, up, rev
    (ea, ua, ra), (eb, ub, rb) = split(a), split(b)
    if ea != eb:
        return -1 if ea < eb else 1
    return _cmp_part(ua, ub) or _cmp_part(ra, rb)


def tree_size(path: Path) -> int:
    total = 0
    for root, _dirs, files in os.walk(path):
        for f in files:
            try:
                total += os.lstat(os.path.join(root, f)).st_size
            except OSError:
                pass
    return total


def fits(deb: Path, replacing: Path):
    """None when the package fits in the root filesystem, else the reason it does not (before anything is extracted)."""
    kb = control(deb, "Installed-Size")
    need = int(kb) * 1024 if kb and kb.isdigit() else deb.stat().st_size * 3
    try:
        probe = APP_ROOT if APP_ROOT.exists() else APP_ROOT.parent
        usage = shutil.disk_usage(probe)
    except OSError:
        return None
    free = usage.free + (tree_size(replacing) if replacing.is_dir() else 0)
    reserve = max(MIN_RESERVE, usage.total * RESERVE_PCT // 100)
    if need + reserve <= free:
        return None
    mb = lambda n: f"{n / 1048576:.0f} MB"  # noqa: E731
    return (f"too big for the system card's root: needs {mb(need)}, {mb(max(free, 0))} free, {mb(reserve)} kept back; "
            "it needs the squashfs-on-card phase (docs/PACKAGES-OTA.md)")


def read_text(path: Path):
    try:
        return path.read_text().strip()
    except OSError:
        return None


def extract(deb: Path, dest: Path) -> bool:
    """Extract a deb into dest. Returns False if dpkg-deb refuses it."""
    try:
        subprocess.run(["dpkg-deb", "-x", str(deb), str(dest)], check=True)
        return True
    except (subprocess.CalledProcessError, FileNotFoundError) as err:
        print(f"deb_watcher: cannot extract {deb.name}: {err}", file=sys.stderr)
        return False


def signature(deb: Path) -> str:
    st = deb.stat()
    return f"{st.st_size}:{int(st.st_mtime)}"


def find_executable(app_dir: Path, name: str):
    """Prefer an executable named like the package, else the first one."""
    found = []
    for rel in BIN_DIRS:
        d = app_dir / rel
        if d.is_dir():
            found += sorted(p for p in d.iterdir() if p.is_file() and os.access(p, os.X_OK))
    for path in found:
        if path.name == name or name.startswith(path.name):
            return path
    return found[0] if found else None


def library_path(app_dir: Path) -> str:
    """Bundled shared libraries, so a deb works without its dependencies installed."""
    dirs = []
    for lib in ("usr/lib", "lib"):
        base = app_dir / lib
        if base.is_dir():
            dirs.append(base)
            dirs += sorted(p for p in base.iterdir() if p.is_dir() and "linux-gnu" in p.name)
    return ":".join(str(d) for d in dirs)


def write_launcher(launcher: Path, app_dir: Path, executable: Path) -> None:
    libs = library_path(app_dir)
    launcher.write_text(
        "#!/bin/bash\n"
        f'cd "{app_dir}" || exit 1\n'
        f'export LD_LIBRARY_PATH="{libs}${{LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}}"\n'
        f'exec "{executable}" "$@"\n'
    )
    launcher.chmod(0o755)


def is_tool(app_dir: Path) -> bool:
    return (app_dir / TOOL_MARKER).is_file()


def tool_wrapper_owner(wrapper: Path):
    """Which package a wrapper in the tools folder belongs to (its tag line), or None for anything else."""
    try:
        for line in wrapper.read_text().splitlines()[:3]:
            if line.startswith(WRAPPER_TAG):
                return line[len(WRAPPER_TAG):].strip()
    except OSError:
        pass
    return None


def write_tools(name: str, app_dir: Path) -> int:
    """Wrappers for every executable of a tool package. Never replaces a command that is not this package's own."""
    TOOLS_DIR.mkdir(parents=True, exist_ok=True)
    libs, made = library_path(app_dir), []
    for rel in BIN_DIRS:
        d = app_dir / rel
        for exe in sorted(p for p in d.iterdir() if p.is_file() and os.access(p, os.X_OK)) if d.is_dir() else []:
            wrapper = TOOLS_DIR / exe.name
            if (wrapper.exists() or wrapper.is_symlink()) and tool_wrapper_owner(wrapper) != name:
                print(f"deb_watcher: {name}: {exe.name} is already a command of another package; not replaced", file=sys.stderr)
                continue
            wrapper.write_text(
                "#!/bin/sh\n"
                f"{WRAPPER_TAG}{name}\n"
                f'LD_LIBRARY_PATH="{libs}${{LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}}" exec "{exe}" "$@"\n'
            )
            wrapper.chmod(0o755)
            made.append(exe.name)
    (app_dir / TOOL_LIST).write_text("".join(f"{n}\n" for n in made))
    return len(made)


def remove_tools(name: str, app_dir: Path) -> None:
    """Take a package's wrappers away (only ones that are its own)."""
    try:
        names = (app_dir / TOOL_LIST).read_text().split()
    except OSError:
        return
    for n in names:
        w = TOOLS_DIR / n
        if tool_wrapper_owner(w) == name:
            w.unlink(missing_ok=True)


def pick(candidates):
    """The deb to install out of [(path, folder)] (earlier = lower priority): the highest version; the later one when a version is unknown."""
    best = candidates[0]
    best_v = control(best[0], "Version") if len(candidates) > 1 else None
    for cand in candidates[1:]:
        v = control(cand[0], "Version")
        if best_v is not None and v is not None:
            if vercmp(v, best_v) >= 0:
                best, best_v = cand, v
        else:
            best, best_v = cand, v
    return best


def _report(step: int, steps: int, doing: str) -> None:
    """Tell whoever is watching how far the extraction has got. A file that cannot be written never stops an install."""
    try:
        tmp = PROGRESS_FILE.with_name(PROGRESS_FILE.name + ".tmp")
        tmp.write_text(f"step={step}\nsteps={steps}\ndoing={doing}\n")
        os.replace(tmp, PROGRESS_FILE)
    except OSError:
        pass
    if PROGRESS:
        PROGRESS(step, steps, doing)


def _unreport() -> None:
    try:
        PROGRESS_FILE.unlink()
    except OSError:
        pass


def process_debs() -> int:
    try:
        return _process_debs()
    finally:
        _unreport()


def pending_count() -> int:
    """How many packages the next process_debs must extract (a new or replaced deb); nothing is changed."""
    found = {}
    for folder in (DEFAULT_DEB_DIR, DEB_DIR, DEB_DIR2):
        if folder.is_dir():
            for d in sorted(folder.glob("*.deb")):
                found.setdefault(d.stem, []).append((d, folder))
    n = 0
    for name, c in found.items():
        deb, _folder = pick(c)
        stamp = APP_ROOT / name / STAMP
        if not (stamp.is_file() and stamp.read_text().strip() == signature(deb)):
            n += 1
    return n


def _process_debs() -> int:
    APP_ROOT.mkdir(parents=True, exist_ok=True)
    LAUNCHER_DIR.mkdir(parents=True, exist_ok=True)
    LAST.clear()

    found = {}
    for folder in (DEFAULT_DEB_DIR, DEB_DIR, DEB_DIR2):      # later wins on a tie: a card replaces a built-in
        if folder.is_dir():
            for d in sorted(folder.glob("*.deb")):
                found.setdefault(d.stem, []).append((d, folder))
    debs = {name: pick(c) for name, c in found.items()}
    installed = 0

    for stale in APP_ROOT.glob("*.new"):                      # an extraction a power-cut interrupted
        shutil.rmtree(stale, ignore_errors=True)

    todo = [n for n, (d, _f) in debs.items() if not ((APP_ROOT / n / STAMP).is_file() and (APP_ROOT / n / STAMP).read_text().strip() == signature(d))]
    steps, done = len(todo), 0

    for name, (deb, folder) in debs.items():
        dest = APP_ROOT / name
        stamp = dest / STAMP
        sig = signature(deb)
        if not (stamp.is_file() and stamp.read_text().strip() == sig):
            _report(done, steps, f"Installing {name}")
            done += 1
            # New or replaced deb: extract fresh into <name>.new, then swap, never over an old tree.
            if control(deb, "X-R3i6nos-Kind") == "system":      # root's to install, from the signed catalog only (r3i6nos-sysupdate)
                why = "a system package is installed only by r3i6nos-sysupdate, from the signed catalog"
                print(f"deb_watcher: {name}: {why}", file=sys.stderr)
                LAST[name] = ("refused", why)
                continue
            why = fits(deb, dest)
            if why:
                print(f"deb_watcher: {name}: {why}", file=sys.stderr)
                LAST[name] = ("refused", why)
                continue
            fresh = APP_ROOT / f"{name}.new"
            shutil.rmtree(fresh, ignore_errors=True)
            fresh.mkdir(parents=True)
            if not extract(deb, fresh):
                shutil.rmtree(fresh, ignore_errors=True)
                LAST[name] = ("failed", "dpkg-deb could not extract it")
                continue
            (fresh / STAMP).write_text(sig)
            (fresh / SRC).write_text(str(folder))
            (fresh / META).write_text(json.dumps({"version": control(deb, "Version"), "installed_kb": control(deb, "Installed-Size")}))
            if dest.exists():
                remove_tools(name, dest)
                shutil.rmtree(dest)
            os.rename(fresh, dest)
        elif read_text(dest / SRC) != str(folder):
            (dest / SRC).write_text(str(folder))
        if is_tool(dest):
            (LAUNCHER_DIR / f"{name}.sh").unlink(missing_ok=True)  # a package that became a tool leaves the menu
            if write_tools(name, dest) == 0:
                print(f"deb_watcher: no command installed from {deb.name} (none in it, or all taken)", file=sys.stderr)
                LAST[name] = ("failed", "no command installed (none in it, or all taken)")
                continue
            installed += 1
            LAST[name] = ("installed", "")
            continue
        executable = find_executable(dest, name)
        if executable is None:
            print(f"deb_watcher: no executable in {deb.name}", file=sys.stderr)
            LAST[name] = ("failed", "no executable in it")
            continue
        write_launcher(LAUNCHER_DIR / f"{name}.sh", dest, executable)
        installed += 1
        LAST[name] = ("installed", "")

    if steps:
        _report(steps, steps, "Done")

    # A deb that was removed from its folder takes its app and launcher with it, but only when that folder is there to say so:
    # a card that is not mounted must never uninstall anything.
    for app in sorted(APP_ROOT.iterdir()):
        if app.suffix == ".new" or not app.is_dir() or not (app / STAMP).is_file() or app.name in debs:
            continue
        src = read_text(app / SRC)
        if Path(src if src else DEB_DIR).is_dir():
            remove_tools(app.name, app)
            shutil.rmtree(app)
            (LAUNCHER_DIR / f"{app.name}.sh").unlink(missing_ok=True)

    return installed


def installed_packages_by_name():
    return {name: version for name, version, _src, _size in installed_packages()}


def installed_packages():
    """[(name, version, source folder, size in bytes)] of what is extracted now."""
    out = []
    for app in sorted(APP_ROOT.iterdir()) if APP_ROOT.is_dir() else ():
        if app.suffix == ".new" or not (app / STAMP).is_file():
            continue
        try:
            meta = json.loads(read_text(app / META) or "{}")
        except ValueError:
            meta = {}
        out.append((app.name, meta.get("version") or "?", read_text(app / SRC) or str(DEB_DIR), tree_size(app)))
    return out


if __name__ == "__main__":
    if "--pending" in sys.argv[1:]:
        # loveos-session asks before the menu starts: "pending" (and nothing else) on stdout means packages are waiting to be extracted, so the
        # menu's splash runs the watcher and shows the bar (ROADMAP section 41 step 9). Anything else (or a failure) and the session runs it itself.
        try:
            if pending_count() > 0:
                print("pending")
        except OSError:
            pass
    elif "--progress" in sys.argv[1:]:
        # Run as the menu's splash does: one line "PROGRESS <step> <steps> <doing>" for each step, flushed, so a bar can follow it.
        PROGRESS = lambda step, steps, doing: print(f"PROGRESS {step} {steps} {doing}", flush=True)
        process_debs()
    else:
        process_debs()

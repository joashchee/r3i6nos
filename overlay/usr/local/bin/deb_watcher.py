#!/usr/bin/env python3
"""deb_watcher.py - extract native ARM64 .deb packages and write launchers.

Runs at boot (loveos-session). Every *.deb in the native_deb folder is extracted
with `dpkg-deb -x` (no dpkg database, no locks) into /opt/native_apps/<name>/
and gets a launcher script in /opt/launchers/native/<name>.sh.

The system image also carries built-in debs (the input checker) in /opt/loveos/defaults/native_deb;
they are handled the same way, and a deb of the same name on the card replaces the built-in one.
An app whose deb is gone is removed only when the card's folder is there to say so: a card that is
not mounted must never uninstall anything.

Paths can be overridden with environment variables so the tests run anywhere:
R3I6NOS_DEB_DIR, R3I6NOS_DEFAULT_DEB_DIR, R3I6NOS_APP_ROOT, R3I6NOS_LAUNCHER_DIR.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

DEB_DIR = Path(os.environ.get("R3I6NOS_DEB_DIR", "/roms/native_deb"))
DEFAULT_DEB_DIR = Path(os.environ.get("R3I6NOS_DEFAULT_DEB_DIR", "/opt/loveos/defaults/native_deb"))
APP_ROOT = Path(os.environ.get("R3I6NOS_APP_ROOT", "/opt/native_apps"))
LAUNCHER_DIR = Path(os.environ.get("R3I6NOS_LAUNCHER_DIR", "/opt/launchers/native"))
STAMP = ".r3i6nos-deb"  # records which deb (size:mtime) an extraction came from

BIN_DIRS = ("usr/games", "usr/bin", "bin", "usr/local/games", "usr/local/bin")


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


def process_debs() -> int:
    APP_ROOT.mkdir(parents=True, exist_ok=True)
    LAUNCHER_DIR.mkdir(parents=True, exist_ok=True)
    card_present = DEB_DIR.is_dir()

    debs = {}
    for folder in (DEFAULT_DEB_DIR, DEB_DIR):      # later wins: the card replaces a built-in
        if folder.is_dir():
            debs.update({d.stem: d for d in sorted(folder.glob("*.deb"))})
    installed = 0

    for name, deb in debs.items():
        dest = APP_ROOT / name
        stamp = dest / STAMP
        sig = signature(deb)
        if not (stamp.is_file() and stamp.read_text().strip() == sig):
            # New or replaced deb: extract fresh, never over an old tree.
            if dest.exists():
                shutil.rmtree(dest)
            dest.mkdir(parents=True)
            if not extract(deb, dest):
                shutil.rmtree(dest, ignore_errors=True)
                continue
            stamp.write_text(sig)
        executable = find_executable(dest, name)
        if executable is None:
            print(f"deb_watcher: no executable in {deb.name}", file=sys.stderr)
            continue
        write_launcher(LAUNCHER_DIR / f"{name}.sh", dest, executable)
        installed += 1

    # A deb that was removed from the card takes its app and launcher with it.
    for app in (APP_ROOT.iterdir() if card_present else ()):
        if app.is_dir() and (app / STAMP).is_file() and app.name not in debs:
            shutil.rmtree(app)
            (LAUNCHER_DIR / f"{app.name}.sh").unlink(missing_ok=True)

    return installed


if __name__ == "__main__":
    process_debs()

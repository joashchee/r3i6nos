# Game-engine runtimes: Pyxel, Ren'Py, Solarus (ROADMAP section 33)

Why: each is a data-driven engine that an on-device no/low-code game maker can target, as NoCoGaMa does for LÖVE: **Pyxel** (retro arcade, Python), **Ren'Py**
(visual novels), **Solarus** (Zelda-like action RPGs). They are PortMaster's runtimes, taken **pre-extracted** and packed as ordinary packages, so nothing
needs root, a loop mount, a sudo, PortMaster or a kernel change. (Chosen from PortMaster's list on 2026-11-19: the others are code-first, play-only or support layers;
the comparison is in ROADMAP section 33.)

**Status (2026-11-19): built and tested in Debian 12 arm64 (Docker); not run on a real R36S.**

## What is here

| Piece | Where |
|---|---|
| The manifests: pinned source, sha256, trim, licences | `tools/runtimes/<name>/runtime.conf` |
| The command each package installs | `tools/runtimes/<name>/run` (`r3i6nos-run-pyxel`, `-renpy`, `-solarus`) |
| One command to build all three | `scripts/build-runtimes.sh` (`--stage-only`, or names): `build/runtimes/r3i6nos-runtime-<name>.deb` |
| Sample games to try on the unit | `tools/runtimes/samples/{Pyxel,Renpy,Solarus}/` (original, CC0) |
| The menu | `menu/scan.lua` lists a game in a card folder `Pyxel/`, `Renpy/` or `Solarus/`; `run_app.sh` types `pyxel`, `renpy`, `solarus` |
| The unit check | `== runtimes ==` in `r3i6nos-report` (Settings > Device reports > r3i6nos) |
| Tests | `tests/test_runtimes.sh`, `tests/test_run_app_runtime.sh`, `tests/test_runtimes_docker.sh` (Docker), `tests/test_scan.lua`, `tests/test_r3i6nos_report.py` |

## How a runtime is packed

PortMaster ships each runtime as a squashfs and mounts it with `mount -o loop` as root. Here `scripts/build-runtimes.sh` fetches the squashfs from the **pinned
commit** of PortMaster-New (URL and sha256 in the manifest; a mismatch is an error), extracts it with `unsquashfs` (host, or Docker with squashfs-tools),
trims it, and builds a **tool package** (`usr/share/r3i6nos/tool`, see `deb_watcher.py`): the watcher extracts it to
`/opt/native_apps/r3i6nos-runtime-<name>/usr/lib/r3i6nos-runtimes/<name>/` and puts `r3i6nos-run-<name>` on the `PATH` (`/opt/loveos/bin`). It is installed
like any package: the `.deb` in `native_deb/` on a card, or `scripts/push-packages.sh UNIT build/runtimes/r3i6nos-runtime-pyxel.deb` over SSH (no card out).
Nothing is added to the image's default apps: the base image stays small, and a person who makes games installs the runtime they want.

| Runtime | Source file (PortMaster-New `16a1a4b`) | Unpacked here | Package (xz) | Licence |
|---|---|---|---|---|
| Pyxel 2.2.8 | `pyxel_2.2.8_python_3.11_aarch64.squashfs` (111 MB) | 9 MB (only the wheel) | 2.0 MB | MIT (+ PSF text for Python) |
| Ren'Py 8.3.4 | `renpy_8.3.4.aarch64.squashfs` (72 MB) | 58 MB (`lib/python3.9` dropped) | 14 MB | MIT, with LGPL and zlib parts (`renpy/LICENSE.txt`) |
| Solarus 1.6.5 | `solarus-1.6.5.aarch64.squashfs` (5.8 MB) | 5.9 MB | 2.0 MB | GPL-3.0-or-later (engine), MIT (bundled LuaJIT); source: tag `v1.6.5` of gitlab.com/solarus-games/solarus |

Together about 73 MiB on the root filesystem if all three are installed (`deb_watcher.py` refuses a package that would leave less than its reserve free).
The versions are the ones PortMaster's own test ports use (the crew's CFW guide names `pyxel_2.2.8`, `renpy_8.3.4`, `solarus-1.6.5` and `python_3.11`); newer Pyxel builds
(2.3.18, 2.4.6, 2.9.5) are in PortMaster-New and can be pinned the same way. Licence texts and the source URL travel in `/usr/share/doc/<package>/` (NOTICE, SOURCE); the GPL's
source for Solarus is the upstream tag named there (not legal advice: check before a release).

### Pyxel runs on the image's own Python

The PortMaster Pyxel runtime is a venv: its `python3.11` links Debian 11 libraries (`libssl.so.1.1`, `libffi.so.7`, `libnsl.so.1`) the image does not have. Only the wheel
(`pyxel/`, `pyxel_wrapper.abi3.so`, 9 MB) is needed, and it runs on the image's Python 3.11.2 (checked: version 2.2.8, a window and nine frames under Mesa GL).
`r3i6nos-run-pyxel` sets `PYTHONPATH` to it. The `python_3.11` runtime (140 MB, with its own libraries) is not needed.

## ZSTD in squashfs: not needed

The kernel is 4.4.189, which has no squashfs ZSTD (added in 4.14). **It does not matter here:** (1) the runtimes are extracted, not mounted, and `unsquashfs` on the Mac (or in
Docker) reads ZSTD; (2) the four PortMaster runtimes read (Pyxel, Ren'Py, Solarus, `python_3.11`) are **zlib-compressed** (`file` says "zlib compressed"), which the stock kernel
does have (the stock `Image` carries squashfs's zlib workspace and no ZSTD; strings only: there is no `IKCONFIG` in it; the report's `== runtimes ==` reads `/proc/config.gz` when the unit has one).
A ZSTD backport would need our own kernel build, which collides with CLAUDE.md rule 2; not planned. Only a runtime PortMaster downloads and mounts itself (docs/PORTMASTER.md, item 1) would need it.

## Libraries

What the packages link (found with `readelf` and `ldd` in Debian 12 arm64): SDL2, SDL2_image (Ren'Py, Solarus), SDL2_ttf, OpenAL, PhysFS, Vorbis, ModPlug, FreeType, libstdc++ (GLIBCXX 3.4.26), glibc 2.30
at most (the image has 2.36). The image already carries all of them for LÖVE and PortMaster **except `libphysfs1`, added to `scripts/build_rootfs.sh` on 2026-11-19** (about 0.1 MB; `check_image.sh` fails an image
without it). No system-wide `LD_PRELOAD`; Solarus's own LuaJIT and `libsolarus` are found through `LD_LIBRARY_PATH` set by its command only.
The image has no `libGL` (SDL's KMS/DRM driver gives GLES): **whether Pyxel's renderer and Ren'Py start on GLES from the Mali blob is the first thing the unit must answer.**

## What was tested (Docker, Debian 12 arm64, Apple silicon host)

- The three packages build, are accepted by the real `deb_watcher.py`, and each command is on the `PATH`.
- **Pyxel:** a game opens a window (virtual X server, Mesa software GL), runs 9 frames and quits with 0; the sample game runs 12 frames.
- **Ren'Py:** a one-line script compiles (so `lib/python3.9` is not needed); the sample reaches its first line and quits.
- **Solarus:** loads its libraries, answers `-help`, opens a quest. Its Lua then panics with `bad light userdata pointer`: a LuaJIT limit when the host gives 48-bit user addresses (Docker on a current kernel). The
  unit's 4.4 kernel is probably 39-bit (an arm64 default of that era, **unread**: the report prints it), and LÖVE, which uses LuaJIT too, runs on it. **Untested on the unit.**
- Not tested: GLES on the Mali, the pad, sound, memory, time to the first frame.

## On the unit (the next step, needs the maintainer)

1. Build and put the packages on the card: `scripts/build-runtimes.sh` then copy `build/runtimes/*.deb` to `native_deb/` (or `scripts/push-packages.sh UNIT build/runtimes/*.deb`).
2. Copy the samples to the card's games partition: `cp -R tools/runtimes/samples/* /Volumes/EASYROMS/` (adds `Pyxel/`, `Renpy/`, `Solarus/`).
3. Boot, run **Settings > Device reports > r3i6nos**, and give `r3i6nos/r36s-report.txt` to Claude: the `== runtimes ==` section says whether each runtime links and starts, the kernel's squashfs options,
   the address-space size and `/dev/uinput`.
4. Start each sample from the menu (types **Pyxel**, **Ren'Py**, **Solarus**) and say: does a picture appear, do the d-pad and buttons work, is there sound, how long to the first frame, does holding Select and Start
   (2 seconds) return to the menu. Ren'Py and Solarus may need an SDL renderer or pad tweak (`RENPY_RENDERER`, Solarus's joypad settings): that is what the unit run is for.
5. If a runtime fails, it is dropped (order of expected difficulty: Pyxel, Solarus, Ren'Py) and the maker for it is not started (ROADMAP section 33, step 5).

## Quitting

A game for a runtime runs in its own process group and `r3i6nos-portquit` ends it when Select and Start are held for 2 seconds (the same guard as a PortMaster port), because a Pyxel game has no menu and an engine
screen may ignore the pad. The sample games also quit on Start (Pyxel, Solarus) or by Ren'Py's own menu.

## The makers (not started)

ROADMAP section 33 step 5: one maker for each runtime that passes the unit run, in the order Pyxel, Ren'Py, Solarus, each its own repository and port following rules 8-14 and 16 (theme, device report with
power, Rec demo, splash, hot-plug toast, data card). They write a game folder in the card's `Pyxel/`, `Renpy/` or `Solarus/` folder (through `r3_appdata_init`'s folder, rule 16) and start it by the same types.

# Default apps

> New apps start from the r3i6nos-ansiapps theme (`docs/R3I6NOS-ANSIAPPS-THEME.md`). These input checkers predate it and draw their own screen.

Built into every image (`/opt/loveos/defaults`), so the menu is never empty. Anything on the games card
(`EASYROMS`) is listed beside them, and a file of the same name there replaces the built-in one.

All three are the **R36S Input Checker** from `docs/R36S_Input_Checker_Specification.md`: a 640×480
diagnostic that shows every button, axis and the D-pad as the pad reports them, raw and as SDL's gamepad
mapping, with a log of the last events. It exists to find out a unit's real input layout (ROADMAP: the
gamepad mapping in `run_app.sh` is unverified).

| App | Where it is built in | Source |
|---|---|---|
| LÖVE 11.5 | `Love 11.5/Input_Checker.love` | `input-checker-love/` |
| LÖVE 12.0 | `Love 12.0/Input_Checker.love` | `input-checker-love/` (same code, packed twice; it prints the runtime it runs on) |
| Native ARM64 | `native_deb/Input_Checker_Native.deb` | `input-checker-native/` (C and SDL2, no other libraries) |

- LÖVE: `scripts/configure_rootfs.sh` packs the folder with `scripts/pack_love.py` (same bytes every time).
  The logic is in `model.lua` and tested by `tests/test_input_checker.lua`, which also runs `main.lua` against a
  fake LÖVE and a fake gamepad.
- Native: `scripts/build_native_apps.sh` cross-compiles it in a Debian 12 container and packs the `.deb`
  (the `native` job in `.github/workflows/build.yml`); `scripts/smoke_native.sh` runs it under qemu with the
  dummy video driver. At boot `deb_watcher.py` installs it like any `.deb` on the card.
  `input-checker-native --screenshot out.bmp` draws a demo frame to a file, for looking at the layout.

**Leaving:** hold Select+Start (raw buttons 14 and 17) for a second, or Esc on a keyboard. Every version also
leaves by itself after **15 seconds** without input, with a countdown in the last 10, so nobody gets stuck in it.

**Button numbers** on screen count from 1, as LÖVE does. The layout is from a real unit (`docs/hardware-findings.md`):
the D-pad is buttons 9-12, not a hat, and SDL has no matching gamepad mapping, so the checker reads raw buttons.
L1/R1/L2/R2 (5-8) are a guess until measured. Power and the volume keys are not joystick buttons: the VOL-, VOL+ and
PWR pills light from keyboard key events, if the system delivers them (not yet seen). Event lines are numbered
(`#27 btn 11 DOWN (held)`) and say whether the button is actually held right then.

**Run on a real R36S:** the first LÖVE 11.5 version (2026-11-07), which found its layout bugs. This revision has not been.
Tested here: the logic, the LÖVE code against a fake LÖVE, the native app's draw and self-test on a Mac (dummy video).
The LÖVE layout itself has only been drawn by LÖVE on the device; the native one is checked by screenshot here.

## Commander, the file manager

`native_deb/Commander.deb` (`commander-native/`, C and SDL2): a two-panel file manager in the style of Norton Commander and Midnight
Commander, and the first app on the **r3i6nos-ansiapps theme** (`docs/R3I6NOS-ANSIAPPS-THEME.md`, C header `theme/r3i6nos_ansiapps.h`).
Row 0 is the top bar (free space), rows 1-20 are two 32-column panels (the active one double-framed, 18 entries a page, `Page` as `n/m` in
its edge), rows 21-23 are the command line: a tip or the last result, the folder as the prompt (or the line being typed), and the selected
file's mode, size and date with its place in the list (a progress bar while it works). The left panel starts on the games card (`/roms`), the right on `$HOME`.

| Button | Does |
|---|---|
| D-pad up / down | move |
| D-pad left / right | switch panel (the left panel is active after left) |
| L1 / R1, L2 / R2 | page up / down, top / bottom |
| A | open a folder, or view a file (text, or hex for binary; whole pages, 4 MiB at most) |
| B | parent folder (the folder you came from stays selected); cancels a running job |
| X | tag / untag the entry and move down; **Y** tags all or none |
| Start | the menu: View, Copy, Move, Rename, Make directory, Delete, Tag all, Untag all, Sort by, Hidden files, Swap panels, Other panel = this folder, Quit |
| Select + Start | quit to the menu |

A keyboard works too: F2 rename, F3 view, F5 copy, F6 move, F7 new folder, F8 delete, F10 quit, Tab switch panel, Insert/Space tag.
Rename and new folder use the command line: Up/Down step the last letter through a list, Right adds a letter, Left deletes one, A accepts,
B cancels (a keyboard just types).

**What it will and won't do:** copy and move go to the other panel's folder and **never overwrite**: a name that already exists is skipped and
counted in the result. Delete asks first with **No** preselected, says when folders are included, and never follows a link (a link is
copied as a link and deleted as a link). A folder cannot be copied into itself, and `/` is never moved or deleted. Work runs in
slices from the main loop with a progress bar, so B can cancel it (the half-written file is removed). A move on one filesystem is a rename; across
filesystems it copies, then deletes each item only after that item copied without error. Tagged entries are what an action works on; with none
tagged, it is the entry under the cursor. It runs as the `loveos` user, so system folders are read-only to it.

**Checked here** (by `commander --selftest`, run by `scripts/check.sh` and in CI under qemu): the file operations against a temporary folder
(copy, links, skipped names, copy-into-itself, move by rename and by copy, a failed copy keeping its source, delete not following links, cancel,
rename, new folder), the panels (sorting, hidden files, paging), every dialog and the viewer's text. Looked at as pictures
(`R3I6NOS_FM_SHOTS=dir commander --selftest` saves each screen). **Not run on a real R36S:** the buttons, how it looks on the panel, speed on the
SD card, and exFAT quirks (no permissions or links there, so copies of links to the card fail and say so).

## Adding an app: its own build, apart from the firmware

Each native app builds on its own; the firmware only bundles the results.

- `apps/<name>-native/app.conf` is the app's manifest (deb name, binary, package, source file, `THEME=1` to compile against `theme/`,
  `DEFAULT=1` to put it in the image, description). `scripts/build_native_app.sh <name>-native [--smoke]` builds that one app in a Debian 12 container
  into `build/native/<NAME>.deb`, and `--smoke` runs its `--selftest` under qemu. `scripts/check.sh` compiles and self-tests every `app.conf`.
- `.github/workflows/native-app.yml` is the reusable build (cache keyed on that app's files, smoke test, artifact `native-<name>-native-arm64`).
  An app's own `.github/workflows/app-<name>.yml` calls it on changes to that app (paths filter), by hand, and on a `<name>-v*` tag, which also publishes
  a GitHub release with the `.deb`. Copy `app-commander.yml` for a new app.
- The firmware (`build.yml`, `scripts/build_native_apps.sh`) builds only the `DEFAULT=1` apps, through the same reusable workflow. An app that is
  not a default (a port, an extra) is released by its own workflow and never enters the image; the user drops its `.deb` in `native_deb/`.
- **LÖVE apps work the same way:** `apps/<name>-love/app.conf` (`TYPE=love`, `NAME`, `RUNTIMES`, optional `TESTS`, `DEFAULT`),
  `scripts/build_love_app.sh <name>-love` (Lua syntax check of every file, the app's tests under luajit, then `build/love-apps/Love <version>/<NAME>.love` per runtime),
  the reusable `.github/workflows/love-app.yml` (artifact `loveapp-<name>-love`) and a caller `app-<name>-love.yml` (tag `<name>-love-v*` releases one `.love` per runtime).
  It needs only python3 and luajit, no container. It does not run the app under real LÖVE (the runtimes are built by the firmware workflow); the app's fake-LÖVE tests stand in for that.
  `configure_rootfs.sh` only copies the built `.love` files into the image.
- **Ports** (open-source programs built from pinned upstream source) have `TYPE=port`, `scripts/build_port.sh` and `port-app.yml`; `apps/_port-template/` says how. They are
  user-installed `.deb` files in `native_deb/`, never part of the image, and may link only what the rootfs has. The first planned one is KlamANSI (`docs/KLAMANSI-PORT.md`).
- **NoCoGaMa** (`docs/NOCOGAMA-PORT.md`) is a user-installed **LÖVE** app of the maintainer's, built in its own private repo as one `.love` per runtime for `Love 11.5/` and `Love 12.0/`; nothing of it is in this repo.
- **Neumetik** (`docs/NEUMETIK-PORT.md`) is a user-installed native `.deb` (`Neumetik.deb`, built and tested under Docker, not yet on a unit): a C and SDL2 host over a Rust static library of Neumetik's crates, built in the Neumetik repo's `r36s/`; nothing of it is in this repo.
- **Command-line tools** (Composer, Painter, Animator; `docs/QJS-CLI-PORTS.md`) are `.deb` packages with `usr/share/r3i6nos/tool`: no menu entry, their commands in `/opt/loveos/bin` on every app's `PATH`. `tools/qjs-node/build.sh` makes them from a Node CLI.
- Not tried yet: the workflows themselves (they run on GitHub; first push will show) and `build_native_app.sh` in a Debian 12 container (no Docker here).

## Trying them on a Mac

```sh
scripts/fetch_love_mac.sh 11.5                 # official release, sha256-pinned -> vendor/love-mac/11.5/love.app
gh auth login                                  # once: the 12.0 build is a CI artifact and needs a login
scripts/fetch_love_mac.sh 12.0                 # the macOS build of the commit build_love.sh pins -> vendor/love-mac/12.0/
vendor/love-mac/11.5/love.app/Contents/MacOS/love apps/input-checker-love     # run it (no pad: "NO GAMEPAD")
scripts/preview_input_checker.sh 11.5          # draws it once with a fake R36S pad -> build/preview/input-checker-11.5.png
scripts/preview_input_checker.sh 12.0
```

`vendor/` and `build/` are gitignored. The preview needs a desktop session (it opens a window for a moment).
The 12.0 download has not been run end to end: artifacts expire after 90 days, and the first run prints the
artifact's sha256 so it can be pinned in `fetch_love_mac.sh`. A Mac cannot run the native ARM64 `.deb`; build the C app
with `cc ... $(sdl2-config --cflags --libs)` instead (see `scripts/check.sh`) and run `--screenshot out.bmp` for a demo frame.


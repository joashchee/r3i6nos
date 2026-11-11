# r3i6nos

Dedicated firmware for the R36S handheld (Rockchip RK3326): it boots to a
menu and runs only LÖVE games (11.5 stable, 12.0 nightly) and native
ARM64 Debian apps. No emulators. The spec is
**`docs/r36s_love_only_firmware_specification.md`**; read it before any
architectural change. This file is the condensed operating rules only.

**`CHANGELOG.md`**: add a bullet whenever a change lands.
**`ROADMAP.md`**: tick items in the same commit as the work, and say what
is blocked and on what.

## Layout

- `overlay/`: files copied onto the image's root filesystem as-is
  (`usr/local/bin/`, `etc/systemd/system/`). Paths mirror the device.
- `menu/`: the boot menu, a LÖVE 11.5 app installed at `/opt/loveos/menu`; its Settings has Network and SSH (a root helper, `overlay/usr/local/bin/r3i6nos-net`, does the work) Battery (`docs/BATTERY.md`; its top bar shows the charge) Sound mixer (`docs/SOUND.md`: Volume and Boost from `/etc/asound.conf`, the codec, a test sound) Date and time (`docs/CLOCK.md`; asked on a card's first boot, set by the root helper `r3i6nos-clock`, kept across restarts) Device reports (`menu/reports.lua`, rule 9) and About (`docs/ABOUT.md`: the build, the unit, Check for updates).
- `apps/`: the apps (each has an `app.conf`, native or LÖVE, and its own workflow, `.github/workflows/app-<name>.yml`; only `DEFAULT=1` apps enter the image). The default apps built into the image (`/opt/loveos/defaults`): the IO Checker (`docs/IO-CHECKER.md`: input, video, sound, files, a device report) as LÖVE 11.5, LÖVE 12.0 and native ARM64, and Commander (a native ARM64 file manager whose viewer identifies and opens about 150 formats: pictures, ANSI art, archives, packages, disk images, documents, and plays Rec demo recordings, WAV and, with ffmpeg, video; `apps/README.md`). See `apps/README.md`.
- `theme/`: the r3i6nos-ansiapps text-mode theme for LÖVE (`.lua`) and C/SDL2 (`.h`): font, screen, list dialogs; installed at `/opt/loveos/theme`. See `docs/R3I6NOS-ANSIAPPS-THEME.md`.
- `tools/qjs-node/`: the shims and builder that turn a Node-style CLI into one QuickJS executable (`docs/QJS-CLI-PORTS.md`); an app's folder is only ever read, never copied in.
- `corpus/`: the test corpus, one file of each format the apps read (`corpus/README.md`), installed at `/opt/loveos/test-files` and copied to EASYROMS by `flash-macos.sh`; `scripts/build_corpus.sh` remakes it.
- `scripts/`: host-side tools. `check.sh` runs everything CI runs.
- `tests/`: Python (`unittest`) and Lua tests; no hardware needed.
- `vendor/`, `build/`, `local_hardware_assets/`: fetched, built or captured, gitignored.
- `docs/DEVICE-REPORTS.md`: the device report every R36S port has (rule 9) and its file format.
- `docs/POWER.md`: what the background work (the hot-plug daemon, the toast poll, the periodic jobs) costs the battery, and the `== power ==` section of `r3i6nos-report` that measures it on the unit.
- `docs/HOTPLUG.md`: the toast over the top bar when headphones, a USB device or DC power is plugged in or out (rule 13); `r3i6nos-hotplug` writes the events, `theme/r3i6nos_toast.{h,lua}` show them.
- `docs/DEMO-RECORDINGS.md`: Rec demo, the screenshots and video every R36S port records to a `Demo` folder (rule 11).
- `docs/STORAGE.md`: the two card slots: the system card (TF1, `/roms`) and the data card (TF2, `/roms2`), mounted by role by `r3i6nos-cards`.
- `docs/hardware-findings.md`: what the stock card showed (kernel 4.4.189, no panfrost, boot flow). Read it before touching boot or graphics.

## Rules

1. **Say what was not run on a real R36S.** Nothing in this repo has booted
   on hardware yet. A claim about behaviour on the device is "untested"
   until ROADMAP section 2 says otherwise. The gamepad mapping in
   `run_app.sh` is unverified.
2. **No copyrighted or proprietary content in the repo, in CI artifacts or in a release:** no ROMs, BIOS files,
   or proprietary emulator assets. **The Mali GPU blob (`libmali`), kernel modules,
   bootloader and boot files come from the user's own card at flash time
   (`capture-card.sh`, `inject-boot.sh`, `inject-rootfs.sh`) and live only in
   the gitignored `local_hardware_assets/` and on that user's cards.** Record the licence and source of every
   vendored base, kernel or runtime.
3. **Only RK3326 R36S boards.** Allwinner and other clones (spec section 1)
   must never get this image; keep the identification steps in the flashing docs.
4. **Overlay scripts stay small and dependency-light** (bash, Python 3
   standard library): they run at boot on 1 GB of RAM. Path overrides go
   through `R3I6NOS_*` environment variables so tests can run on a laptop.
5. **The menu never scrolls:** whole pages, `Page n/m` in the foot.
6. **No telemetry, no network at boot, no accounts.** Settings > Network and SSH (`docs/NETWORK.md`) and About's Check for updates (`docs/ABOUT.md`) only act when a person chooses them; SSH is off and has no shared host keys in the image.
7. **Keep the user's data safe:** the games card (`EASYROMS`) is theirs. Boot
   scripts only read it; never format, repartition or delete from it.
8. **Every app made for r3i6nos starts from the r3i6nos-ansiapps theme** (`theme/`, `docs/R3I6NOS-ANSIAPPS-THEME.md`): the IBM VGA
   8x16 font scaled to 10x20, **64 columns by 24 rows** on the 640x480 screen, row 0 the reserved top bar, rows 1-20 the display view, rows 21-23
   the three-row command line (reply, prompt, status), list dialogs and their button rules as KlamANSI does them, only the 16 VGA colours in
   pairs that pass 4.5:1, nothing scrolls. New apps, LÖVE or native, copy the menu's structure; an app that cannot follow it says why in its README.
   The font is CC BY-SA 4.0: keep `NOTICE`, `theme/font/` (licence, credit, changes) and the headers of the generated font files intact.
9. **Every R36S port has a device report** (`docs/DEVICE-REPORTS.md`): one action on the unit (an app's Start menu item
   **Device report** and a `--report` flag; a command-line tool's `<command>-report`, which Settings > Device reports starts)
   that checks and measures what only the R36S can answer (the system, the pad's GUID, sound, the games card and its filesystem,
   speed against the app's goal, peak memory, every "untested on the unit" item a program can answer) and writes
   **`/roms/<App>/r36s-report.txt`** for Claude to read: `STATUS key: value` lines (PASS, FAIL, INFO, SKIP), sections, a summary,
   and `end` last. Native ports use `theme/r3i6nos_report.h`; tools get `tools/qjs-node/report.py` from `package-tool.sh`. A new port
   is not done until its report exists and is tested; when a report comes back, update the port and both roadmaps from it.
10. **Every R36S port has a local build script** that makes its card file on this Mac with one command and no arguments: a native or
   tool port's **`r36s/build-deb.sh`** in its own repo (Docker only; it builds in Debian 12 arm64 and runs the package's self-test;
   `--clean-room` also installs it with `deb_watcher.py` in a clean Debian 12), a LÖVE port's `scripts/build.sh`. It fetches what it
   needs (a pinned tarball, checked by hash) or says where to put it, and leaves the file where `flash-macos.sh`'s `PORTS` list looks
   for it; that list names the script as the port's build command. A new port is not done until its script has been run.
11. **Every R36S port has a Rec demo** (`docs/DEMO-RECORDINGS.md`) next to its Device report: an item **Rec demo** in its menu right after
   **Device report** (and a `--rec-demo` flag; a command-line tool's `<command>-demo`, which Settings > Rec demos starts). It first opens a
   **checklist of the hero features** (all ticked) so the person picks which ones the demo shows; then it runs the real app through a scripted
   tour of the ticked ones on a **pre-generated demo file** (original, made by a script in the port, using every hero feature in the port's README
   hero list) and saves, in **`/roms/<App>/Demo/`**: numbered PNG screenshots of the running app, **`demo-<timestamp>.mp4`**
   (30-90 s, with the app's own audio; the timestamp is `YYYYMMDD-HHMMSS`), a copy of the demo file and `rec-demo.txt` (the log, in the report
   grammar, `end` last). **The video always opens on the app's splash screen (rule 12) with a clear version line: the version, the git commit and the
   date**, so a video says which build made it. **While it runs, everything else is locked out:** no input but a cancel, the menu's and other
   processes' work paused or stopped, so the unit's full power goes to this one task, and a **progress bar** (step, bar, time) shows. The tour
   drives the app's real input path on a stepped clock, so a recording is the same on any unit. A new port is not done until Rec demo
   exists and a self-test checks the tour covers every hero feature; say untested what has not run on the unit.
12. **Every R36S port has a splash screen** made to the r3i6nos-ansiapps theme (rule 8; KlamANSI's is the example): the app's name and art on the 64x24
   screen, the version, commit and date, and the load's progress and any error, shown at once at start and before anything heavy runs. Rec demo's
   video starts on it.

13. **Every R36S port shows the hot-plug toast** (`docs/HOTPLUG.md`): when the 3.5 mm headphone jack, a USB device (OTG) or the DC power port is plugged in or
   out, a **toast over the top bar flashes for 3 seconds** saying what changed (from `/run/r3i6nos-hotplug/events`, written by the root service `r3i6nos-hotplug`;
   `theme/r3i6nos_toast.h` for native ports, `theme/r3i6nos_toast.lua` for LÖVE). **A failure stays until a key is pressed**, and that key is used up (it clears the
   toast and does nothing else). A new port wires it in (poll each frame, `key` first on every input, draw after composing) and says untested what has not run on the unit. It also announces a pad, the battery (low, critical, full) and the TF cards in and out. **A Rec demo shows no toast; a failure stops and closes it, then the failure(s) show** (rule 11).

## Check before committing

`scripts/check.sh` (Python tests, Lua test, `bash -n`, shellcheck). Report
tools that were not installed rather than calling the run clean.

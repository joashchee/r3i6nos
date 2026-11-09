# Roadmap

What is planned and what is left, as of 2026-11-06 (phases planned from the flash guide). Each section names the
document that has the detail. Nothing here has run on a real R36S yet.

## 1. Scaffold (spec: `docs/r36s_love_only_firmware_specification.md`)

- [x] **Repository:** README, CLAUDE.md, CHANGELOG, MIT licence, CI, `scripts/check.sh`.
- [x] **Overlay scripts:** `deb_watcher.py` (tested), `run_app.sh`,
  `loveos-session` in `overlay/`, rewritten from the spec's drafts (see CHANGELOG for what changed).
- [x] **Menu:** LÖVE 11.5 launcher in `menu/`, paged list, scan tested.
- [x] **r3i6nos-ansiapps theme** (2026-11-07, `docs/R3I6NOS-ANSIAPPS-THEME.md`, CLAUDE.md rule 8): VGA 8x16 scaled to 10x20, 64x24 cells on 640x480,
  reserved top bar, 20-row display view, three-row command line, KlamANSI list dialogs on the pad's buttons. The menu and its Settings
  dialog use it. Font data and layout, list logic and the menu's screen are tested with luajit. **Untested:** `theme.init`/`theme.draw` under LÖVE
  with a display (the sandbox has none) and everything on the R36S: that `require` finds `/opt/loveos/theme` through `package.path` under LÖVE,
  that the canvas and sprite batch draw on the Mali blob's GLES, and how the 10x20 glyphs look on the panel.
  - [ ] **Look at it on a Mac first:** run the menu under `vendor/love-mac` with `R3I6NOS_THEME=theme` and `R3I6NOS_ROMS` set to a folder of dummy games, and check the glyphs.
  - [ ] **Move the input checkers onto the theme** (LÖVE and the C app) when next changed; they keep their own pad picture for now.
  - [x] **A C version of the theme** (2026-11-07): `theme/r3i6nos_ansiapps.h`, checked against the Lua one by a glyph hash and used by Commander.
- [x] **Commander, a two-panel file manager** (2026-11-07, `apps/commander-native/`, default app `native_deb/Commander.deb`; see `apps/README.md`): browse, view (text and hex),
  copy, move, rename, new folder, delete, tag, sort, hidden files; no overwrite, no links followed, delete asks with No first, work in slices with a progress bar and cancel.
  File operations, panels, dialogs and the viewer are tested on the laptop and under qemu; screens were looked at as pictures. **Untested on the R36S:** the buttons, the look on the panel,
  speed on the SD card, exFAT (no links or permissions). Not done: overwrite choices, a text-mode picker for long names, opening a `.love` from the list.
- [x] **Per-app build workflows, apart from the firmware** (2026-11-07; `apps/README.md`, "Adding an app"): `app.conf` + `scripts/build_native_app.sh` + reusable `native-app.yml` + `app-commander.yml`
  (tag `commander-v*` releases the `.deb`). **Untested:** the workflows on GitHub and the build script in a Debian 12 container.
  - [x] `app-input-checker-native.yml` added the same way (tag `input-checker-native-v*`).
  - [x] **LÖVE apps' equivalent** (`TYPE=love`, `build_love_app.sh`, `love-app.yml`, `app-input-checker-love.yml`). **Untested:** on GitHub; the `.love` is not run under real LÖVE by it.
  - [ ] Ports (prebuilt or cross-built ARM64 games) have no workflow yet: decide their shape (a `TYPE=port` manifest that builds or fetches, then packs a `.deb`) when the first one lands.
- [x] **Settings dialog with Shutdown** (2026-11-07; Start / S opens it, Shutdown asks "Yes / No" with No preselected, then the session
  leaves a flag, exits 0, and `loveos.service`'s root `ExecStopPost` runs `r3i6nos-poweroff` -> `systemctl --no-block poweroff`).
  Logic, session and poweroff script are tested on the laptop. **Untested on the R36S:** that the Start button (raw 14) opens it, that
  systemd 252 on kernel 4.4 powers the board off (and not just halts), and that the games card is unmounted cleanly.
- [x] **Settings > Network and SSH** (2026-11-07, `docs/NETWORK.md`): status, wired and Wi-Fi setup with an on-screen keyboard, tests, instructions, SSH start/stop/password; a root helper per request, nothing at boot.
  Tested on the laptop with a fake system. **Untested on the R36S:**
  - [ ] Does the stock kernel have a driver for any USB Ethernet or Wi-Fi adapter? Which ones? (Status lists none when it does not; write the working models in `docs/NETWORK.md`.)
  - [ ] Does the unit's USB port work as a host?
  - [ ] `wpa_supplicant`, `udhcpc` (does it fill `/etc/resolv.conf`?), `usermod` and `chpasswd` on the rootfs; the path unit under systemd 252; the rootfs CI build with the new packages.
  - [ ] Decide whether a game being able to ask for SSH is acceptable, or build a confirmation it cannot fake.
  - [ ] Optional, rule 6 permitting: auto-join the saved network at boot (not done: ask first).
- [x] **Battery indicator and Settings > Battery** (2026-11-08, `docs/BATTERY.md`): the charge at the launcher's top right (low turns red, charging green), and a history sampled at boot, app start and end,
  every 5 minutes in an app and at shutdown, with Status (health, drain, time left, abrupt ends), Use by app, History and Clear history. Tested on the laptop with a fake supply; CI green on `d182bb1` (including the rootfs build and `tests/test_image.sh`). **Untested on the R36S:**
  - [ ] Which `power_supply` the fuel gauge is and what it reports (`scripts/device-info.sh` should list `/sys/class/power_supply`); check the indicator, health and cycle figures against it.
  - [ ] That `StateDirectory=r3i6nos-battery` gives `loveos` a writable folder under systemd 252, and that the ticker does not disturb a game.
  - [ ] Whether an app's name (its file name) is a good enough label for `.deb` apps.
- [x] **First push** to GitHub (`joashchee/r3i6nos`, 2026-11-06).

## 2. Phase 1: ground truth from the working card (needs the R36S and a Mac)

Why: the guide's image has no bootloader or kernel modules (`docs/build-plan.md`
gaps 1 and 2). Everything after depends on what is on the user's own card.
All of it goes in the gitignored `local_hardware_assets/`.

- [x] **`scripts/capture-card.sh`** written (see phase 4); **now run it on your card** (needs `brew install e2fsprogs` for the modules).
- [ ] **Run `scripts/device-info.sh` on the R36S** (stock OS booted, over SSH) and keep the output in `local_hardware_assets/info/`.
- [x] **Confirm the board** is RK3326 (spec section 1): yes, see Phase 1b.
- [x] **Capture the gamepad mapping** on a real unit (2026-11-07, `docs/hardware-findings.md` "Input layout"): `run_app.sh`'s `SDL_GAMECONTROLLERCONFIG` now has the measured buttons and axes (D-pad as buttons 8-11, not a hat; tested as text).
  - [ ] **Read the pad's real GUID** (`scripts/device-info.sh`, "input devices") and put it in that mapping: it is still the spec's guess, so SDL may apply the mapping to nothing. **Untested on the R36S:** whether any SDL gamepad mapping takes effect.
- [x] **Kernel:** reuse the stock 4.4.189 for now (it is what the DTBs and the Mali blob go with); mainline is phase C above.
- [ ] **Decide gptokeyb:** keep or drop it, and `etc/love_keys.cfg` with it.

## 2b. Phase 1b: what the card showed (2026-11-06; facts in `docs/hardware-findings.md`)

- [x] **Board is supported:** genuine RK3326 DTBs, no clone markers. Card is MBR; BOOT at sector 32768, same as ours.
- [x] **Bootloader captured** (15.1 MB from sector 64) and `boot.ini` understood; `inject-boot.sh` rewrites `root=` to the image's UUID.
- [x] **Kernel is the Rockchip vendor 4.4.189.** Consequences below.
- [ ] **Re-run `capture-card.sh`** (move `local_hardware_assets/` aside first): the first run predates the GPU
  library capture (`rootfs/gpu/`, `info/stock-gpu-libs.txt`). Paste `stock-gpu-libs.txt` here.
- [x] **GPU path decided: A** (2026-11-06): the stock `libmali` blob, captured from your own card and injected locally
  into `/opt/libmali`; never in the repo, CI artifacts or releases. `LOVE_GRAPHICS_USE_OPENGLES=1` is set when it is present.
  - [x] `capture-card.sh` copies the blob and records the stock links (`rootfs/gpu/`, `info/stock-gpu-libs.txt`).
  - [x] `scripts/inject-rootfs.sh` + `inject_rootfs.py` (2026-11-06, **never run on a real card**; the planner has 9 unit tests and
    CI's `tests/test_image.sh` runs it against real `debugfs` on a tiny image): modules and firmware to `/usr/lib`, the blob
    to `/opt/libmali` with only the links that resolve to it; refuses unless `libEGL.so.1`, `libGLESv2.so.2` and `libgbm.so.1` are provided.
  - [x] `loveos-session` puts `/opt/libmali` first on `LD_LIBRARY_PATH` when it exists (tested).
  - [x] **Stock listing read** (2026-11-06; kept as `tests/fixtures/stock-gpu-libs.txt`): one blob, `libmali-bifrost-g31-rxp0-wayland-gbm.so`,
    reached by `libMali.so`; `libEGL.so.1`, `libGLESv2.so.2` and `libgbm.so.1` all link to it, so the planner's assumption holds (tested on the real listing).
    Stock Mesa/libdrm leftovers on that card are correctly left out.
  - [x] **Wrong blob found on hardware** (2026-11-07): that listing was the `/usr/lib` blob (r6p0, API 10.6); the kernel wants 11.x
    (`file /dev/mali0 is not of a compatible version`). The stock card also has `/usr/local/lib/aarch64-linux-gnu/libmali-bifrost-g31-rxp0-gbm.so`
    (r13p0), first in its `ld.so.conf`, with its own EGL/GLES/gbm links. `capture-card.sh` now prefers it; the maintainer's local assets were
    re-pulled from the backup. **Untested:** whether this blob opens the GPU and SDL's kmsdrm then works (next boot). See `docs/hardware-findings.md`.
  - [x] (superseded) **Blob dependencies:** the wayland build links `libwayland-client`/`-server` (added to the rootfs, a guess). `inject-rootfs.sh` now prints the blob's
    real `needs:` list: compare it with the rootfs packages the first time you run it.
  - [ ] **Does SDL's KMS/DRM driver work with the blob's gbm/EGL?** Only a boot answers that (phase 5). Fallback **B** (Mesa llvmpipe, remove `/opt/libmali`) stays available and shippable.
  - [ ] **Legal note** for the README: images with the blob are not redistributable; say so wherever images are mentioned. (Written in `docs/making-a-card.md`.)
- [x] **Getting the card's modules and blob into the image:** `debugfs` on the Mac (`docs/making-a-card.md`), no Linux needed.
- [ ] **systemd 252 on kernel 4.4:** unknown until it boots (phase 5). Watch for cgroup, seccomp and `/proc` errors in the journal.
- [x] **Games card:** `flash-macos.sh` makes it by default (2026-11-07; `--no-games` skips it, `--games-only` adds it to a card already flashed) and adds partition 3 (exFAT, `EASYROMS`, the stock card used exfat too) in the
  space after ROOTFS and creates `Love 11.5/`, `Love 12.0/`, `native_deb/`; the MBR edit is tested (`add_games_partition.py`). **Untried on a real
  card** (the first hardware try hit a root-owned MBR file, fixed; every failure now explains itself, tested with fake `diskutil`/`dd` in `tests/test_flash_macos.sh`). Without it, use a second card labelled `EASYROMS` in TF2 (`docs/making-a-card.md`).

## 3. Phase 2: rootfs in CI (no hardware)

- [x] **`scripts/build_rootfs.sh`** (written 2026-11-06, **never run**: needs root on Linux with
  debootstrap and qemu): two-stage debootstrap, Bookworm arm64, `loveos` user, root locked; output under `build/`.
- [x] **`scripts/configure_rootfs.sh`:** overlay, menu, hostname, fstab, volatile journal, unit
  enabled, docs trimmed (licence texts kept). Tested on a fake tree by `tests/test_configure_rootfs.sh`.
- [x] **systemd unit `loveos.service`** and `loveos-session` (replace `S99loveos`): restart on crash,
  log to the journal. The session loop is tested with fakes; the unit is not yet verified by systemd.
- [x] **fstab:** games card by label `EASYROMS` with `nofail`. Assumes FAT/exFAT (`uid=1000`); phase 1 confirms.
- [x] **Manual CI job** `.github/workflows/build.yml (was rootfs.yml)` (2026-11-06, workflow_dispatch only): checks, build, sanity checks, `systemd-analyze verify`, uploads `r3i6nos-rootfs.tar.zst`.
- [x] **First successful run of that job** (2026-11-06, after pinning `ubuntu-24.04` and unmounting `/proc` before `du`): rootfs builds, sanity checks pass, artifact uploaded (444 MB tree).
- [x] **`systemd-analyze verify` fails the job** (2026-11-06; its only message was a false alarm from checking the runner's paths, now `--root=build/rootfs`).
- [ ] **Confirm that run:** the `--root` form is not yet run; fix any real warnings it shows.
- [ ] **Slim the 444 MB tree** (probably Mesa's LLVM stack); see phase 6.
- [ ] **Chroot tests under qemu:** Python and Lua tests, `deb_watcher.py` with a real `.deb`, `systemd-analyze verify`.

## 4. Phase 3: LÖVE runtimes (no hardware)

- [x] **LÖVE 11.5** `scripts/build_love.sh` (2026-11-06, built in CI): cross-compiles the pinned release tarball in a
  Debian 12 container, installed to `/opt/love/11.5/` by `configure_rootfs.sh`; the `love` job in `build.yml` builds it first.
- [x] **LÖVE 12.0** (2026-11-06, built in CI): `build_love.sh 12.0`, pinned to `love2d/love` commit `def2451`
  (2026-11-05), CMake. It needs **SDL3** (>= 3.1.3), which Bookworm lacks, so SDL3 3.4.20 is built too (KMS/DRM, ALSA,
  no X11/Wayland) and shipped in the runtime's `lib/`. Both pins are sha256-checked. Matrix job in `build.yml`.
- [x] **First green 12.0 build and smoke test** (2026-11-06, run reported all green; the 12.0 smoke output line was not
  read): SDL3 needed `SDL_UNIX_CONSOLE_BUILD`; shellcheck notes fixed.
- [ ] **Re-pin 12.0** when it matters: it is a moving nightly, so keep the commit, note why each move, and test games on both.
- [x] **Smoke test** (2026-11-06, passed in CI: `R3I6NOS_SMOKE ok love=11.5.0 jit=LuaJIT 2.1.0-beta3`): `scripts/smoke_love.sh` runs the rootfs's `love` under
  `qemu-aarch64-static -L`, dummy video and audio, on `tests/smoke/love` (data, zlib, filesystem, PNG, physics, sound
  modules); a step in `build.yml` that runs on every runtime in the rootfs (11.5 and 12.0). Does not prove drawing.
- [x] **Cache** the built runtime in CI (`actions/cache`, keyed on `build_love.sh`).
- [x] **First green `love` job** (2026-11-06): LÖVE 11.5 cross-compiles for arm64 and links against the rootfs.

## 5. Phase 4: image and flashing

- [x] **`scripts/assemble_image.sh`** (2026-11-06, **not yet run**): MBR (signature `0x52333669`, so
  `PARTUUID=52333669-02` is the root), BOOT FAT32 at 16 MiB, ROOTFS ext4; written straight into the image
  with `mke2fs -d` and `mkfs.fat --offset`, no loop devices. Refuses a rootfs with a mounted `/proc`.
- [x] **`scripts/check_image.sh`:** partition table, `fsck`, labels, and the files the device needs.
- [x] **`scripts/inject-boot.sh`:** raw bootloader from sector 64 and the BOOT files, both read back
  and compared; never touches the MBR. **Layout unverified:** sector 64 is the usual Rockchip start.
- [x] **`scripts/flash-macos.sh`** (**never run on a card**; `diskutil` is blocked in the dev sandbox, so even its
  refusals are unproven): internal, partition, huge or tiny disks refused; checksum; type the disk name; `rdisk`, `bs=4m`.
- [x] **`.github/workflows/build.yml`** (was `rootfs.yml`): manual or `v*` tags; an `image` job runs the image tests,
  assembles, checks, compresses and uploads `r36s-loveos.img.gz` with a checksum.
- [x] **First green `image` job** (2026-11-07): `tests/test_image.sh` (assemble, check, inject-boot, inject-rootfs against real `debugfs`) and the image build pass in CI. Still never booted on hardware.
- [x] **Refusal tried** (2026-11-07, on the maintainer's Mac): `flash-macos.sh <image> disk0 --dry-run` says "disk0 is an INTERNAL disk. Refusing." The happy path then ran on a real 128 GB card (2026-11-07): 2.4 GB written in 38 s, ejected. Whether that card boots is not yet known.
- [x] **`scripts/capture-card.sh`** (2026-11-06, **never run on a card**; the MBR parsing is unit-tested on synthetic cards):
  read-only copy of the bootloader, BOOT files, modules and firmware into `local_hardware_assets/`, plus a report.
  `scripts/device-info.sh` runs on the R36S itself for the real kernel command line and controller mapping.
- [ ] **First-boot resize** of ROOTFS to fill the card (the image is ~2.3 GB; `resize2fs` in a oneshot unit).
- [ ] **Match the stock kernel command line:** the stock `boot.ini` or `extlinux.conf` names its own `root=`;
  it must point at partition 2 (or `PARTUUID=52333669-02`). Found out in phase 1.

## 6. Phase 5: bring-up on the R36S (needs the unit)

- [x] **First boot reaches the menu** (2026-11-07, one R36S, 128 GB card, rebuilt image with the matching Mali blob): the menu is drawn on the panel. Still to check on hardware: backlight, D-pad and buttons, audio, a game, `.deb`, reboot.
- [x] **Default apps** (2026-11-07): the R36S Input Checker (spec: `docs/R36S_Input_Checker_Specification.md`) as a LÖVE 11.5 app, a LÖVE 12.0 app
  and a native ARM64 `.deb` (C, SDL2), built into the image at `/opt/loveos/defaults`; the menu and `deb_watcher.py` list them beside the card's
  files. Tested on a laptop (logic, fake LÖVE, native draw and self-test); the `native` CI job and qemu smoke are new and **not yet green**.
  **Not yet seen on a real R36S.** The checker is how the real button layout gets recorded (below).
- [x] **Input layout measured** (2026-11-07, first run of the LÖVE 11.5 checker, one unit): A-Y 1-4, D-pad is buttons 9 left / 10 down / 11 up / 12 right
  (no hat), Fn 13, Select 14, L3 15, R3 16, Start 17; table in `docs/hardware-findings.md`. The checker was fixed from it (raw buttons, whole-stick
  highlight, SEL/FN/STA order, raw Select+Start exit, 15 s idle-out, 2x fonts, power/volume pills).
- [ ] **Still to measure:** L1, R1, L2, R2 (checker assumes 5-8); what power and the volume keys send (keys? names?); whether event lines are really
  inverted (the checker now prints held/free beside each event). Then fix `SDL_GAMECONTROLLERCONFIG` in `run_app.sh` and the menu's controls from the layout.
- [ ] **First boot (full):** reaches the menu; panel, backlight, D-pad and buttons, audio, TF2 mount.
  - Done (2026-11-07, one R36S, Panel 4 DTB from the stock card): ArkOS splash, kernel, initramfs, root by UUID, systemd to
    the end of boot, text console on the panel. `EASYROMS` timeout is expected (no such partition).
  - **Open:** after that nothing is drawn. Blocked on the log: flash an image injected with `R3I6NOS_BOOT_DEBUG=1`, boot, read
    `r3i6nos-bootlog.txt` from BOOT on the Mac. Result of that boot (2026-11-07): `loveos.service` ran and looped; LÖVE could not create a window (wrong Mali blob, see 1b)
    and `deb_watcher.py` crashed (`No module named 'shutil'`: `python3-minimal` has none; fixed with `libpython3-stdlib` and a CI check).
    Next: rebuild, inject the right blob, flash, boot again.
  - [x] **`Failed to start wpa_supplicant` at boot** (seen 2026-11-09 on the unit): the wpasupplicant package's own D-Bus service, enabled by its install script, with no D-Bus daemon in the image.
    `configure_rootfs.sh` now unwires and masks it (r3i6nos-net starts `wpa_supplicant` itself, only on request). **Untested:** a boot of a rebuilt image without the message, and joining Wi-Fi with the service masked.
  - **macOS will not mount BOOT** (`fsck_msdos` is clean). Suspect: FAT hidden-sectors field is 0 (`mkfs.fat -h`). Not yet fixed.
- [ ] **A LÖVE 11.5 game and a 12.0 game** run and return to the menu.
- [ ] **A `.deb`** installs, launches and uninstalls by removal.
- [ ] **Fix** what hardware shows; record each unit tested in the README.

## 7. Phase 6: slim and release

- [ ] **Strip** services; measure free RAM and boot time against the spec's claims.
- [ ] **Boot splash** with progress, errors shown on screen, never a blank panel.
- [ ] **Release:** image uploaded by hand with `gh release upload`, checksums in the notes.
- [ ] **Flashing guide** for macOS (from the script), Windows and Linux.
- [ ] **Native apps:** dependency handling beyond bundled libs; choosing which binary a package launches.

## 8. Ports (apps built from other people's or the maintainer's source; `docs/KLAMANSI-PORT.md`)

- [x] **Port build workflow** (2026-11-07; `scripts/build_port.sh`, `.github/workflows/port-app.yml`, `apps/_port-template/`, `tests/test_build_port.sh`): a `TYPE=port`
  manifest pins public upstream source by sha256, applies patches, cross-builds in a Debian 12 container, link-checks the binary against the rootfs's
  packages (deb_watcher installs no dependencies), ships the upstream licence, source URL and hash in the `.deb`, and smoke-tests under qemu. Not in the image.
  The manifest checks and the fetch, pin and patch steps are tested offline; **untested:** the container build and the workflow on GitHub (no port exists yet to run them on).
- [ ] **KlamANSI, the first port** (plan: `docs/KLAMANSI-PORT.md`; built in the private KlamANSI repo, tested headless; **never run on a real R36S**). A native SDL2 host with QuickJS running KlamANSI's pure modules; 48 x 24 display flush
  top-left, a 16-column side column (line, status, radial legend, panels), no tool bar or command-line rows (a documented departure from rule 8); pad mapping per the plan.
  - [x] **Decided** (2026-11-08, `docs/KLAMANSI-PORT.md` section 0): code in the private KlamANSI repo; free, no shareware notices; the typed line is the three-row command line, floating flush to the bottom only while typing or showing a message, toggled with Fn together with the virtual keyboard.
  - [ ] **Spike:** QuickJS cross-built; load `klam.js` and `screens.js`, draw an example `.kan` and compare with Node. Go/no-go on browser-only APIs, speed and memory.
    - [x] **Static audit of browser-only APIs** (2026-11-08): across `klam.js`, `screens.js`, `ansi.js`, `vinput.js`, `listdlg.js` and their imports, the only real hit is
      `screens.js:50` (`new URL(import.meta.url).searchParams.get('v')`, the build tag); QuickJS has no `URL`, so the host must define a tiny `URL` shim or that line must
      guard it. Nothing else touches `document`, `window`, `performance`, timers, `fetch`, audio or `localStorage`. **Not run:** nothing has executed in QuickJS yet.
    - [x] **Spike, on a Mac (2026-11-08): GO on loading and drawing.** QuickJS 2024-01-13 (sha256 `3c4bf8f8...8212e03`) built natively; `klam.js` and `screens.js` load once a
      `URL` shim exists (a 1-line class in the host) and `?v=124` is stripped from imports; for all 7 `examples/*.kan`, `load` + `viewScreen` + `cmdScreen` give
      **byte-identical text to Node** (hash 183434222). All 7 files plus load took 23 ms in QuickJS on the Mac (29 ms in Node). **Also builds and matches on aarch64 Debian 12 in Docker (Colima, 2026-11-08). **Untested:** speed on the 1.3 GHz
      Cortex-A35 (expect roughly 10x slower; a drawn frame must be timed there), peak memory (not measured), the `qjsc` bytecode path, colours (only the characters were compared), `route` and key input, and the aarch64 cross-build. A statically imported module that throws shows no error under `qjs -m`: the host must report load errors on the splash.
  - [x] **Host** (2026-11-08, in the KlamANSI repo `r36s/host/`): C, SDL2 and QuickJS; display, side column, floating command line, splash and errors; headless self-test passes on this Mac and in Debian 12 arm64. **Untested:** a real window, Cortex-A35 speed and memory, anything on the R36S.
  - [x] **Controls** (2026-11-08, KlamANSI repo `r36s/host/`): Fn keyboard overlay (rows 10-20, above the line), pointer and clicks, radial with valid Klam lines, Start menu with four contexts, radial setup kept in `settings.json` under `/roms/KlamANSI/`. Tested with luajit-free Node tests, the headless self-test on this Mac and in Debian 12 arm64. **Untested:** the feel on a real pad, a real card (exFAT), speed and memory.
  - [x] **Files** (2026-11-08, KlamANSI repo `r36s/host/library.js`): drawings and folders as real files under `/roms/KlamANSI/drawings/`, autosave and restore; tested on an in-memory card and by the headless self-test (Mac and Debian 12 arm64). **Untested:** a real card (exFAT), a pulled or full card. The Files panel now has the card's own wording (2026-11-08); so does the Help panel (2026-11-08).
  - [x] **Package** (2026-11-08): `KlamANSI.deb` (needs only SDL2, libc, libm) installed by the real `deb_watcher` and launched in a clean Debian 12 under Docker; CI workflow green on GitHub (2026-11-08, manual run: `npm test`, then the arm64 build and smoke test).
  - [x] **Export to the card** (2026-11-08): `.ANS` with SAUCE (and anim, txt, md, ascii) to `/roms/KlamANSI/export/`; round-trips through KlamANSI's importer; not tried in a real viewer.
  - [x] Grant confirmed (2026-11-08, maintainer), marker deleted.
  - [ ] **A try on a real R36S.**

## 9. NoCoGaMa (a LÖVE app; code in the private NoCoGaMa repo, `joashchee/nocogama`; status and what r3i6nos gives it: `docs/NOCOGAMA-PORT.md`; its plan: that repo's `docs/R3I6NOS-PORT-PLAN.md`)

- [ ] **NoCoGaMa, a no-code game maker.** A `TYPE=love` app packed once per runtime for `Love 11.5/` and `Love 12.0/` on the games card, on the r3i6nos-ansiapps theme (rule 8, no departure: the level view is the one pixel region), user-installed, never in the image. Free for the R36S under a grant the maintainer confirmed (2026-11-08). **Nothing has run on a real R36S, and nothing has been drawn by LÖVE with a display.**
  - [x] **Scaffold and licence** (2026-11-08, in the NoCoGaMa repo): app shell, build and check scripts, plan, proprietary `LICENSE`, `NOTICE`, grant in `package/copyright` with the `NOCOGAMA_RELEASE=1` gate (as KlamANSI's), private remote pushed.
  - [x] **Spike, laptop part** (2026-11-08): fixed-step benchmark, 200 actors: 0.003 ms a step with the JIT on and 0.084 ms with it off on this Mac (the Cortex-A35 is a guess, about 10x); the same code under real LÖVE 11.5 and 12.0 headless from this repo's `vendor/love-mac`. **Finding: the official macOS LÖVE 11.5 runs LuaJIT with the JIT off, 12.0 with it on.** The device's own state is the first thing its Info page will say; r3i6nos links Debian's system LuaJIT, so the arm64 builds should have it on (**expected, not seen**). This says nothing about drawing.
  - [x] **Device spike written** (`spike/device/`, `scripts/build_spike.sh`): six pages (Info with the pad's real GUID, Buttons, Card, Bench, Draw, Report to `/roms/NoCoGaMa/spike-result.txt`); its model and card code tested with luajit. **Not drawn by LÖVE, not run on a unit.**
  - [ ] **Maintainer, on the Mac then the R36S:** run the two spike `.love` files (instructions in the NoCoGaMa README and roadmap) and paste `spike-result.txt` back. Its answers (the pad's GUID, write to `/roms`, exFAT, whether a LÖVE app returns to the menu) are also open items here: section 6 and `docs/KLAMANSI-PORT.md`. Go/no-go on 60 Hz at 200 actors on both runtimes.
  - [x] **Data core written** (2026-11-08, committed in the NoCoGaMa repo): `json`, `project` (validate, migrate, atomic save that leaves the old file whole when a rename fails), the fixed 60 Hz world and the six behaviours; its luajit tests pass and the data core (save, load, play to a win) runs under real LÖVE 11.5 and 12.0 headless. About 0.01 ms a step for 200 actors on a Mac. Still no editor, no screens beyond the empty state, nothing drawn.
  - [x] **Editor, play and files, export** (2026-11-08, committed in the NoCoGaMa repo, 13 commits): four editor tabs, undo, several scenes with doors, naming, editing texts; the Games screen with an on-screen keyboard, library and autosave; export to `Love 11.5/<Name>/` and `Love 12.0/<Name>/` (this repo's `menu/scan.lua` lists it, checked by its test); the LÖVE API rule tested against a strict stub. Its `scripts/check.sh` passes (re-run today), including headless real LÖVE 11.5 (JIT off) and 12.0 (JIT on). **Not done:** a frame drawn by LÖVE with a display (`pixels.lua`, `theme.draw`, the glyph atlas); `io.popen` + `ls` for the game list and a rename over an existing file on exFAT, on the unit.
  - [x] **Release workflow** (`.github/workflows/release.yml`, `ci.yml`, `scripts/release_assets.sh`): a `v*` tag builds both `.love` files twice (byte-identical), checksums them, drafts a release; refuses while `RELEASE_NOTES.md` or `package/copyright` is a draft. Tested locally; **never run on GitHub**.
  - [x] **r3i6nos pointer:** this section and `docs/NOCOGAMA-PORT.md` are the pointer; ticked in the NoCoGaMa roadmap.
  - [ ] **Maintainer:** run the NoCoGaMa release workflow once as a dry run; run the spike and the app on the Mac with a display (`R3I6NOS_THEME=~/Coding/r3i6nos/theme`), then on the R36S, and a full try on both runtimes.

## 10. Lean CLI ports of Composer, Painter and Animator (QuickJS; `docs/QJS-CLI-PORTS.md`)

Command-line tools other apps can call (NoCoGaMa's sprites, jingles and animations; any app's assets), built from the three Node CLIs with no change to them. One shared MIT builder here (`tools/qjs-node/`), a thin manifest in each app's repo.

- [x] **Shims, stager, builder, tool package, `deb_watcher` tool support** (2026-11-08): `node:` shims for QuickJS checked against Node's own (6 groups); `build.sh` makes one bytecode-only executable per tool (libc and libm only); `make-deb.sh` packs it; a deb with `usr/share/r3i6nos/tool` is a tool (no menu entry, commands in `/opt/loveos/bin`, on `PATH` for apps and SSH). Tests: `tests/test_qjs_node.mjs`, `scripts/test_qjs_node.sh`, 5 more `test_deb_watcher.py` cases.
- [x] **The three tools run** (2026-11-08): the same commands under Node and QuickJS: Composer 14 of 14 and Animator 10 of 10 identical (the `.wav` byte for byte); Painter identical except PNG bytes (same pixels, bigger files). On arm64 Debian 12 under Docker: built, packed, installed by the real `deb_watcher.py`, output equal to Node's from the Mac. **Untested:** a real R36S, the Cortex-A35's speed and memory, a LÖVE app's `io.popen` there.
- [x] **Each app's repo wired** (2026-11-08): `r36s/` (manifest, `commands.txt`, `check.sh`, `build-deb.sh`, licence note, README), a workflow and a changelog entry in `~/Coding/composer`, `painter` and `animator`; the scripts are this repo's (`tools/qjs-node/compare.py`, `check-tool.sh`, `package-tool.sh`). Composer 15 of 15 identical, Painter 11 of 11 (PNG by pixels), Animator 10 of 10; each package built, installed by the real `deb_watcher.py` and run in Debian 12 arm64 under Docker. Their own suites pass (62, 154, 23). The four app workflows then passed on GitHub (2026-11-08, manual runs): Composer, Painter and Animator `compare` and `package` (arm64 under qemu, 8-11 min), KlamANSI `test` and `package`. **Not run:** a real R36S.
- [x] **Licences** (2026-11-08, maintainer's decisions): the three apps follow Composer's proprietary licence (Painter and Animator were Apache-2.0: relicensed, `UNLICENSED`, `private`; copies already out, such as Coupler's, stay Apache-2.0), and all three R36S packages ship **KlamANSI's grant** (confirmed by the maintainer 2026-11-08, markers deleted; the gates `COMPOSER_RELEASE`, `PAINTER_RELEASE`, `ANIMATOR_RELEASE` stay and their refusals stay tested). Holder is **ansiapps** everywhere (Composer's `LICENSE` and README said Joash Chee: changed). Painter keeps the CoffeeMUD attribution and its Apache-2.0 text in `NOTICE`.
- [ ] **The maintainer confirms the grant** (KlamANSI's and these three share one wording) and settles whose name Composer's `LICENSE` and `package.json` carry (Joash Chee; the grants and the other apps say ansiapps).
- [ ] Painter's ANSI-to-PNG render needs its VGA atlas embedded (CC BY-SA credit); a stronger deflate if PNG size matters; a `tools.lua` helper for LÖVE apps (quoting, exit status); the first real use in NoCoGaMa.

## 11. Neumetik (a native port; code in the Neumetik repo, `joashchee/neumetik`, `r36s/`; plan and decisions: `docs/NEUMETIK-PORT.md`)

- [ ] **Neumetik, ansiapps' synthesizer, as a player on the R36S.** A C and SDL2 host linking a Rust static library over Neumetik's crates (MIDI, tracker modules, music as text, BGN, Visual), on the r3i6nos-ansiapps theme with no departure (Visual's canvas is the 64 x 20 display view), user-installed (`native_deb/Neumetik.deb`), never in the image. **Built and tested without the unit; nothing run on a real R36S.**
  - [x] **Plan** (2026-11-08, `docs/NEUMETIK-PORT.md`): shape, screen, pad, files, tests, phases.
  - [x] **Decided** (2026-11-08, maintainer: all five of plan section 0 as proposed): code in the Neumetik repo's `r36s/`; free under KlamANSI's grant wording; a player first; rule 8 with no departure; a C host over a Rust static library.
  - [x] **Library and speed test** (2026-11-08, Neumetik repo `r36s/lib`): C API, music generated in code, the device report (rule 9: Start > Device report, `--report`) writing `/roms/Neumetik/r36s-report.txt`. Tests pass on a Mac (MIDI and modules equal Neumetik's own players sample for sample). Mac: 60x real time for the busiest engine; Debian 12 arm64 under Docker on the same Mac: 58x.
  - [ ] **Speed on the unit:** run Start > Device report on an R36S and bring back `r36s-report.txt` (go/no-go at 2x real time per engine). Also settles whether the unit's sound works through ALSA (`/proc/asound` never captured: `scripts/device-info.sh`). Blocked on the maintainer and a unit.
  - [x] **Host** (2026-11-08, `r36s/host/neumetik.c`): window, audio callback, splash, Now Playing with Visual; files open on a thread.
  - [x] **Controls and panels:** Music, Background noise (sections, then scenes), Visual, Now Playing; the pad of plan section 3; the Start menu (repeat, shuffle, output rate, device report); the queue. Headless self-test with pictures. **Untested:** the pad on a unit.
  - [x] **Files and settings** under `/roms/Neumetik/` (`music/` read only, `settings.txt` by temp file and rename, `r36s-report.txt`). **Untested:** exFAT on a real card.
  - [x] **Package** (2026-11-08): `r36s-neumetik` (needs SDL2, libc, libgcc_s, libm; glibc 2.34 symbols at most), installed by the real `deb_watcher.py` in a clean Debian 12 arm64 under Docker and self-tested there.
  - [ ] **The Neumetik `r36s.yml` workflow on GitHub** (written; never run), and **a try on a real R36S**.
  - [ ] **Later:** Endless Mode (needs moving out of the Tauri app into a crate, a Neumetik-side change), Keys on the pad, WAV export, the Tracker's pattern view, SoundFonts, the editors.

## 12. Device reports: every R36S port reports back to Claude (CLAUDE.md rule 9; `docs/DEVICE-REPORTS.md`)

- [x] **The rule and the format** (2026-11-09): one action on the unit writes `/roms/<App>/r36s-report.txt` (`STATUS key: value` lines, sections, a summary, `end`).
- [x] **Shared code** (2026-11-09): `theme/r3i6nos_report.h` for native ports (system, pads and GUIDs, card and filesystem, memory; `tests/test_report.c` in `check.sh`); `tools/qjs-node/report.py` for command-line tools (`<command>-report`, shipped and smoke-tested by `package-tool.sh`; `tests/test_tool_report.py`); **Settings > Device reports** in the menu starts a tool's report (`menu/reports.lua`, `tests/test_reports.lua`, `tests/test_settings.lua`).
- [x] **Every port has one** (2026-11-09): Neumetik (Start > Device report, `--report`; replaced its speed test), KlamANSI (Start > Device report, `--report`), the NoCoGaMa device spike (its Report page), Composer, Painter and Animator (`compose-report`, `sprites-report`, `animate-report`). Each is checked by its own tests and, for the packages, in a clean Debian 12 arm64 under Docker. **None has run on a real R36S.**
- [ ] **The NoCoGaMa app's own Device report** (its menu) before its release.
- [ ] **The first reports from a unit:** run each on the R36S, give the files to Claude, and update the ports and these roadmaps from them. Blocked on the maintainer and a unit.
- [ ] Rebuild an image so the menu has Settings > Device reports (the tools' reports also run over SSH without it).


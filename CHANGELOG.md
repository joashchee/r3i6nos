# Changelog

All notable changes to r3i6nos are recorded here, newest first. Update it
whenever a change lands, dated the day it was made.

## Unreleased

- **`flash-macos.sh`: games partition by default, and every failure explained** (2026-11-07). The rest of the card becomes `EASYROMS` unless `--no-games`
  (`--games` is still accepted); new `--games-only DISK` adds it to a card that is already flashed (found when a card flashed without `--games` showed 125.7 GB
  free and no `/roms`). Each failure (unreadable MBR, not an r3i6nos card, partition 3 already there, under 1 GiB free, write-protected card, macOS not
  seeing the partition, exFAT format failing, not mounted, folders) prints the step, what happened and what to do, says whether the partition table was
  already changed (so whether to re-run), leaves the card un-ejected and exits 1; the flash, unmount, size, checksum and image errors are wordier too.
  New `tests/test_flash_macos.sh` drives it with fake `diskutil`, `plutil`, `sudo` and `dd`. **Still not run on a real card.**
- **`flash-macos.sh --games` could not edit the MBR** (2026-11-07, found adding the games partition to a flashed card): `sudo dd of=mbr` made the file
  root-owned, so the unprivileged `add_games_partition.py` got "Permission denied". The sector is now read into a file the shell owns. **Not re-run on a real card.**
- **Dependabot for the GitHub Actions** (2026-11-07): `.github/dependabot.yml`, weekly, one grouped pull request for all the actions the workflows use.
  The scripts' pinned downloads (LÖVE, SDL3) are not covered and stay pinned by hand.
- **Font licence terms done** (2026-11-07), keeping VileR's IBM VGA 8x16 (CC BY-SA 4.0): new root `NOTICE` (credit, licence link, what changed, and that the two
  generated font files are CC BY-SA, not MIT); `README.md`'s licence section says so; `theme/font/README.txt` rewritten (the old one claimed the font was never
  converted, which stopped being true); both generated files now carry the full credit, licence link, change statement and warranty notice
  (`build_vga_font.py` writes them); a test checks the notices, the licence text and `NOTICE`. Not legal advice: it follows CC's attribution and ShareAlike terms.
- **The r3i6nos-ansiapps theme, and the menu on it** (2026-11-07). New rule (CLAUDE.md 8): every app made for r3i6nos starts from it. `theme/`:
  the IBM VGA 8x16 font (WebPlus, VileR's pack, CC BY-SA 4.0; original and licence in `theme/font/`) rasterised by `scripts/build_vga_font.py`
  into `vga8x16.lua` and `r3i6nos_vga8x16.h` and scaled to **10x20** by nearest neighbour; a **64x24-cell** screen on 640x480 with the top row
  reserved for a bar, rows 1-20 the display view and rows 21-23 the three-row command line (reply, prompt, status) as in KlamANSI; box and list
  dialogs with whole pages (`Page x/y`), KlamANSI's list rule on the R36S's buttons (D-pad select, L1/R1 or left/right page, L2/R2 top/end, A
  accept, B back, Start more); 16 VGA colours only, every pair checked at 4.5:1. The menu (`menu/main.lua`, new `menu/view.lua`) and the Settings and
  Shutdown dialogs are drawn with it: 18 games a page instead of 12. The menu now reads **raw buttons only** (its SDL gamepad mapping did not match this pad).
  `configure_rootfs.sh` installs `theme/` at `/opt/loveos/theme`; CI installs Pillow so the generated font is compared with the font.
  Tests: `tests/test_theme.lua`, `tests/test_menu_view.lua`. **`theme.init`/`theme.draw` have not run with a display, nor on an R36S.** The input checkers do not use it yet.
- **Power pill moved left of the volume pills** (2026-11-07), both checkers: PWR, VOL-, VOL+ (power is on the left of the R36S, volume on the right).
  L1/R1/L2/R2 (5-8) and the power/volume key names are confirmed correct on the unit, so they are no longer "assumed".
- **Button layout corrected from the R36S** (2026-11-07): D-pad up 9, down 10, left 11, right 12; Select 13, Start 14, Fn 17; **A is 2 and B is 1**
  (they were the other way round), and A and B swap places on the drawn pad (A right, B bottom). Changed in both LÖVE checkers (`model.lua`,
  `main.lua`), the native C checker (`B_*`), the menu's raw buttons, `docs/hardware-findings.md` and the tests; the exit chord is now raw 13+14.
  L1/R1/L2/R2 and the power/volume keys are still unmeasured. Not re-run on the device.
- **Settings dialog with Shutdown** (2026-11-07). Start (raw button 14) or S/F1 opens it from the menu; items are Shutdown and Back, and
  Shutdown asks for confirmation with "No" preselected (B steps back). Confirming writes `system<TAB>poweroff`; `loveos-session` leaves
  `/run/loveos/poweroff` and exits 0, and the new root `ExecStopPost` (`r3i6nos-poweroff`) runs `systemctl --no-block poweroff`, so systemd
  stops services and unmounts the filesystems. No sudo or polkit needed. `loveos.service` is now `Restart=on-failure` (a clean exit stays
  stopped). The menu also reads raw joystick buttons (D-pad up 9, down 10, left 11, right 12, A 2, B 1; L1/R1 5/6, confirmed), de-duplicated against the gamepad mapping.
  Tests: `tests/test_settings.lua`, and the session/poweroff cases in `test_configure_rootfs.sh`. **Not run on a real R36S.**
- **LÖVE for the Mac, to try apps before they go on the card** (2026-11-07). `scripts/fetch_love_mac.sh`: 11.5 is the official release,
  checked against a pinned sha256 (tried: downloads, verifies, `LOVE 11.5 (Mysterious Mysteries)`, universal arm64 and x86_64, ad-hoc signed
  as upstream ships it); 12.0 has no release, so it comes from the CI artifact `love-macos` of the run for the commit `build_love.sh` pins
  (run 41957924703) through `gh`, which needs a login (stops with a message without one; **not run past that check**). Into gitignored
  `vendor/love-mac/`. `scripts/preview_input_checker.sh` draws the real checker once under a real LÖVE with a fake R36S pad and saves a
  PNG (`scripts/preview/`); needs a desktop, so it was **not run** here (the sandbox has no display: LÖVE says "the video driver did not add any displays").
- **Input checker fixed from the first run on a real R36S** (2026-11-07). The unit's D-pad is four buttons (9 left, 10 down, 11 up, 12 right),
  Fn 13, Select 14, L3 15, R3 16, Start 17, and SDL's gamepad mapping does not match, so the first version lit nothing for the D-pad, Select,
  Fn and Start, and its Select+Start exit (which needed the mapping) never fired. Now: everything reads raw buttons from one `LAYOUT` table
  (`model.lua`, `B_*` in the C app); the whole stick circle lights when L3/R3 is pressed; the row is SEL, FN, STA; the exit chord is raw 14+17;
  idle-out is 15 s (countdown from 10); fonts are about twice as large and the screen re-laid out; VOL-, VOL+ and PWR pills light from key
  events (the power and volume keys are not joystick buttons); event lines are numbered and say whether the button is held at that moment
  (the report that UP/DOWN looked swapped could not be seen in the code: this makes a real inversion visible). Button numbers are 1-based
  in both apps. Tests updated (D-pad from buttons, raw chord, one-button-no-quit, 15 s idle, keys; a deliberate break of the chord is caught).
  `docs/hardware-findings.md` has the measured table. L1/R1/L2/R2 (assumed 5-8) and the power/volume key names are still unmeasured.
- **Default apps: the R36S Input Checker, three ways** (2026-11-07), from `docs/R36S_Input_Checker_Specification.md`. `apps/input-checker-love`
  (LÖVE 11.5 and 12.0: the same code packed into `Love 11.5/` and `Love 12.0/`, prints its runtime) and `apps/input-checker-native`
  (C and SDL2 with its own 5x7 font, cross-built into `Input_Checker_Native.deb`). Built into the image at `/opt/loveos/defaults`, so the
  menu is never empty: `menu/scan.lua` lists that folder beside `/roms` (the card wins on a name clash), `deb_watcher.py` installs built-in
  debs too (and never uninstalls anything when the card is missing). Beyond the spec: it shows SDL's gamepad mapping, a seen-buttons
  checklist and the last six events; fonts are made once (the spec's sample made new ones every frame); it leaves by Select+Start held 1 s,
  Esc, or 2 minutes without input, so an unmapped pad cannot trap the user. New: `scripts/pack_love.py`, `build_native_apps.sh`,
  `smoke_native.sh`, a `native` CI job and a qemu smoke step, `check.sh` compiles and self-tests the C app. Tests: 48 Python, scan and checker
  Lua (the checker's `main.lua` runs against a fake LÖVE; the fake rejects invalid gamepad button names). **Not run on a real R36S.**
- **The menu runs on the R36S** (2026-11-07): the rebuilt image with the stock card's matching Mali blob and the Python fix
  boots to the r3i6nos menu on the panel. It showed "Nothing here yet" because the card had no games partition.
  Added `flash-macos.sh --games`: partition 3 (exFAT, `EASYROMS`) from just after ROOTFS to the end of the card, formatted by macOS,
  with `Love 11.5/`, `Love 12.0/`, `native_deb/`. The MBR edit is `scripts/add_games_partition.py` (refuses foreign or already-
  partitioned disks and cards with under 1 GiB free; 6 tests; tried against the real MBR). The macOS half (diskutil) is untried.
- **First boot log read; two bugs** (2026-11-07). (1) The injected Mali blob was the wrong one: the stock card keeps an older
  wayland-gbm blob in `/usr/lib` (userspace API 10.6) and the one it really uses in `/usr/local/lib/aarch64-linux-gnu`
  (r13p0, first in `ld.so.conf`); the kernel's driver is 11.7, so LÖVE failed with "file /dev/mali0 is not of a compatible
  version". `capture-card.sh` now prefers `/usr/local/lib`; new fixture and tests; `docs/hardware-findings.md` records both.
  (2) `deb_watcher.py` crashed on `import shutil`: `python3-minimal` has no `shutil`. The rootfs now installs
  `libpython3-stdlib`, and `scripts/check_py_imports.py` (run in the build workflow, tested) fails CI if a boot script imports a
  module the rootfs lacks. The bootlog's `free` (not installed) became `/proc/meminfo`.
- **First boot on hardware** (2026-11-07): ArkOS splash, kernel, initramfs and systemd all ran from the new card (root found by
  UUID); `EASYROMS` timed out as expected (no such partition on this card, `nofail`). Boot stopped after
  `systemd-update-utmp` with nothing on screen: `loveos.service` output goes to the journal. Added **`r3i6nos-bootlog`**
  (opt-in: a file named `r3i6nos-bootlog` on BOOT makes it write `r3i6nos-bootlog.txt` there at 25 s and 60 s: loveos journal,
  DRM/panel/GPU kernel lines, device nodes, unresolved libraries) and `R3I6NOS_BOOT_DEBUG=1` for `inject-boot.sh` (console on
  screen instead of serial, plus the marker file). The stock `boot.ini` loads the panel DTB from BOOT; the stock card's was Panel 4's.
- **`inject-rootfs` read-back fooled by macOS's case-insensitive disk** (2026-11-07, first real run): `libmali.so` and
  `libMali.so` are different links on ext4 but the same name in a Mac dump folder, so one read back wrong. Symlink targets
  are now read from the filesystem with `debugfs stat`, not from the dump (files are still compared from the dump). Run
  end to end on a scratch copy of the CI image with the maintainer's real assets: 2881 entries verified, fsck clean.
- **`inject-boot` tripped on Finder's `.DS_Store`** (2026-11-07, first run on the maintainer's Mac): the copy glob skipped the
  dotfile but the read-back found it. Finder litter (`.DS_Store`, `._*`) is now removed from the staged copy and top-level
  entries are copied one by one with overwrite (`-D o`), so no hidden file is skipped and a re-run over a half-injected image works. Tested on a copy of the CI image with fake assets, not yet with your real ones.
- **`inject-rootfs` found its first real bug in CI** (2026-11-06): debugfs's exit status and error text cannot be
  trusted (it exits 0 on failure and its missing-parent message matched none of my patterns), so the read-back compare is
  the real check. The test rootfs has no `/usr/lib`, so the module directories silently failed to be created. The plan now
  creates `/usr`, `/usr/lib` and `/opt` first (a real root already has them), and a failed read-back prints debugfs's own
  messages. 28 tests.
- **Real stock GPU listing read** (2026-11-06): the card has one Mali blob, `libmali-bifrost-g31-rxp0-wayland-gbm.so`, with
  `libEGL`, `libGLESv2`, `libgbm` and others linked to it through `libMali.so`; the planner handles it (tests use the real listing).
  The wayland build links `libwayland-client0` and `libwayland-server0`, so the rootfs now installs them; `inject_rootfs.py`
  reads the blob's ELF `NEEDED` list (own 64-bit parser, tested on a built ELF) and `inject-rootfs.sh` prints it.
- **GPU decision A: the stock Mali blob, injected locally** (2026-11-06). `scripts/inject-rootfs.sh`
  (debugfs, works on a Mac) writes the card's kernel modules and firmware to `/usr/lib` and the `libmali` blob to
  `/opt/libmali` of an image, after fsck and a read-back compare, and leaves the image alone if anything fails;
  `scripts/inject_rootfs.py` plans it (9 tests). `loveos-session` puts `/opt/libmali` first on `LD_LIBRARY_PATH` and sets
  `LOVE_GRAPHICS_USE_OPENGLES=1` when it exists (tested). New `docs/making-a-card.md`; CLAUDE.md rule 2 now says the blob,
  modules and bootloader never go in the repo, CI artifacts or releases. Not run on a real card.
- **First card capture read** (2026-11-06): genuine RK3326, MBR card, bootloader 15.1 MB from sector 64, BOOT at
  32768, `boot.ini` plus per-panel DTB folders, **kernel 4.4.189 (Rockchip vendor)**. Findings in
  `docs/hardware-findings.md`; the big one is that Mesa's panfrost cannot drive the GPU on a 4.4 kernel.
- **Image UUIDs fixed and `root=` rewritten** (2026-11-06): `scripts/image-layout.sh` holds the layout
  constants; the ROOTFS ext4 UUID is fixed; `inject-boot.sh` rewrites `root=` in every `.ini`/`.conf` of a temporary
  copy of the boot files to that UUID (same `root=UUID='...'` form as the stock card), writes nothing to the image
  until the files are staged, works on macOS (no `sfdisk`, no GNU `dd` flags), and `test_image.sh` covers it.
- **`capture-card.sh` also lists and copies the stock GPU libraries** (`libmali*`) into `rootfs/gpu/`.
- **Card capture** (2026-11-06): `scripts/capture-card.sh <disk>` (read-only on the card) copies the raw
  bootloader area, BOOT files, kernel modules and firmware into `local_hardware_assets/`, writes a report
  (partitions, DTBs, `root=` lines) and flags Allwinner clones; `scripts/capture_mbr.py` does the MBR parsing
  (8 tests); `scripts/device-info.sh` runs on the R36S for the kernel command line and controller mapping.
  **`local_hardware_assets/` is now in `.gitignore`** (it was only in the guide's draft). Never run on a card.
- **Phase 4 written, not yet run** (2026-11-06): `assemble_image.sh`, `check_image.sh`, `inject-boot.sh`,
  `flash-macos.sh`, `tests/test_image.sh` (Linux and root only, "NOT RUN" elsewhere), and an `image` job.
  `rootfs.yml` is now `build.yml` ("Build (manual and tags)"), also run on `v*` tags. The image has no
  bootloader or kernel until `inject-boot.sh` adds them from the user's own card.
- **12.0 smoke test output read** (2026-11-06): `R3I6NOS_SMOKE ok love=12.0.0 jit=LuaJIT 2.1.0-beta3`, with two deprecation warnings (`love.data.hash` old form, `love.math.noise`). The smoke game now uses the new forms where they exist and falls back on 11.5. Untested on either version until the next CI run.
- **Phase 3 done in CI** (2026-11-06): LÖVE 11.5 and 12.0 (with SDL3) build for arm64, install into the rootfs, and pass the qemu smoke test; shellcheck is clean.
- **First shellcheck run** (2026-11-06) failed the job on four info-level notes in `build_love.sh` (SC2015, SC2012 twice, SC2016); fixed (if/then, `find`, and a justified `disable` for the literal `$ORIGIN`). Every other script was already clean.
- **12.0 build, first run** (2026-11-06): SDL3 3.4.20 configured on arm64 (KMS/DRM, ALSA, udev found) but refused to finish without X11 or Wayland; added `-DSDL_UNIX_CONSOLE_BUILD=ON`, and set `-DSDL_INSTALL=ON` explicitly because the summary showed it OFF. The SDL3 install is now checked before LÖVE is built.
- **LÖVE 12.0 build** (2026-11-06): `build_love.sh 12.0` builds `love2d/love` at the pinned commit `def2451`
  with CMake. 12.0 needs SDL3, which Bookworm does not ship, so SDL3 3.4.20 is built from its release tarball
  and bundled in the runtime's `lib/` (RUNPATH `$ORIGIN`). Both downloads are sha256-pinned. `rootfs.yml`
  builds 11.5 and 12.0 as a matrix, the rootfs takes whatever was built, the smoke test runs on each, and the
  rootfs gains `libharfbuzz0b`. Not run yet.
- **LÖVE 11.5 builds and passes the smoke test in CI** (2026-11-06): `R3I6NOS_SMOKE ok love=11.5.0 jit=LuaJIT 2.1.0-beta3` under qemu, dummy video and audio.
- **LÖVE smoke test under qemu** (2026-11-06): `scripts/smoke_love.sh` and `tests/smoke/love` check that the arm64
  runtime in a built rootfs starts and its non-graphical modules work; a step in the manual rootfs workflow.
  Not run yet. `check.sh` now also syntax-checks the Lua files.
- **LÖVE 11.5 build script** (2026-11-06): `scripts/build_love.sh` cross-compiles the pinned release (sha256
  checked) for arm64 inside a Debian 12 container; `configure_rootfs.sh` installs `build/love/*` to
  `/opt/love/`; the manual workflow builds it (cached) before the rootfs; the rootfs gains `libluajit-5.1-2`.
  Not run yet. 12.0 is refused until a commit is pinned.
- **`systemd-analyze verify` now fails the job and checks inside the rootfs** (2026-11-06): its first output, "/usr/local/bin/loveos-session is not executable", was a false alarm from looking at the runner's filesystem. Untested form: `--root=build/rootfs`.
- **Rootfs workflow succeeded** (2026-11-06): the manual job now builds, checks and uploads `r3i6nos-rootfs.tar.zst`.
- **First rootfs run** (2026-11-06): debootstrap, both stages, configure and the chroot steps all completed (444 MB tree). The job then failed because `build_rootfs.sh` ran `du` with `/proc` still mounted; it now unmounts first, and removes the qemu binary before finishing.
- **CI pinned to `ubuntu-24.04`** (2026-11-06): the first rootfs run died with exit code 100 (an `apt-get` failure) on `ubuntu-latest`, which is moving to Ubuntu 26. Cause not confirmed from the log given.
- **Manual rootfs CI job** (2026-11-06): `.github/workflows/rootfs.yml`, run by hand from the Actions tab. Not run yet.
- **Rootfs build and systemd unit** (2026-11-06): `scripts/build_rootfs.sh` (two-stage Bookworm arm64
  debootstrap, `loveos` user, root locked), `scripts/configure_rootfs.sh` (overlay, menu, fstab, volatile
  journal), `loveos.service`, and `loveos-session`, which replaces `S99loveos` (removed). Config and session
  loop are tested on a laptop; the debootstrap run itself has not been run.
- **Phases planned from the flash guide** (2026-11-06): `docs/build-plan.md` lists ten gaps in the guide (no bootloader or kernel modules, foreign-arch debootstrap, unguarded `dd`, others); ROADMAP sections 2 to 7 are now phases 1 to 6.
- **Project scaffolded** (2026-11-06) from `docs/r36s_love_only_firmware_specification.md`.
- **`deb_watcher.py` reworked from the spec's draft:** re-extracts a replaced
  deb, removes the app and launcher of a deb taken off the card, prefers the
  executable named like the package, sets `LD_LIBRARY_PATH` to the package's
  bundled libs, and is covered by tests.
- **`run_app.sh` reworked:** checks the target and runtime exist, kills only
  the gptokeyb it started (not `killall -9`), uses KMS (`SDL_VIDEODRIVER=kmsdrm`)
  instead of the spec's `DISPLAY=:0`, and marks the gamepad mapping unverified.
- **`S99loveos` reworked:** the menu is a LÖVE app that hands its choice back to
  a boot loop, so a game runs with the menu's memory freed and returns to the menu on exit.
- **Menu added** (`menu/`): paged list of LÖVE games and native apps.

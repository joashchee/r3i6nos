# Roadmap

What is planned and what is left, as of 2026-11-06 (phases planned from the flash guide). Each section names the
document that has the detail. Nothing here has run on a real R36S yet.

## 1. Scaffold (spec: `docs/r36s_love_only_firmware_specification.md`)

- [x] **Repository:** README, CLAUDE.md, CHANGELOG, MIT licence, CI, `scripts/check.sh`.
- [x] **Overlay scripts:** `deb_watcher.py` (tested), `run_app.sh`,
  `loveos-session` in `overlay/`, rewritten from the spec's drafts (see CHANGELOG for what changed).
- [x] **Menu:** LÖVE 11.5 launcher in `menu/`, paged list, scan tested.
- [x] **First push** to GitHub (`joashchee/r3i6nos`, 2026-11-06).

## 2. Phase 1: ground truth from the working card (needs the R36S and a Mac)

Why: the guide's image has no bootloader or kernel modules (`docs/build-plan.md`
gaps 1 and 2). Everything after depends on what is on the user's own card.
All of it goes in the gitignored `local_hardware_assets/`.

- [ ] **`scripts/capture-card.sh`:** from a working card, save the first 16 MiB
  raw (bootloader sectors), the partition table, the `BOOT` files, `/lib/modules`
  and firmware, and print the kernel version and DTB name. Read-only on the card.
- [ ] **Confirm the board** is RK3326 (spec section 1) and record which DTBs are supported.
- [ ] **Capture the gamepad mapping** on a real unit; replace the unverified `SDL_GAMECONTROLLERCONFIG` in `run_app.sh`.
- [ ] **Decide the kernel:** reuse the stock one (default) or build our own later.
- [ ] **Decide gptokeyb:** keep or drop it, and `etc/love_keys.cfg` with it.

## 3. Phase 2: rootfs in CI (no hardware)

- [x] **`scripts/build_rootfs.sh`** (written 2026-11-06, **never run**: needs root on Linux with
  debootstrap and qemu): two-stage debootstrap, Bookworm arm64, `loveos` user, root locked; output under `build/`.
- [x] **`scripts/configure_rootfs.sh`:** overlay, menu, hostname, fstab, volatile journal, unit
  enabled, docs trimmed (licence texts kept). Tested on a fake tree by `tests/test_configure_rootfs.sh`.
- [x] **systemd unit `loveos.service`** and `loveos-session` (replace `S99loveos`): restart on crash,
  log to the journal. The session loop is tested with fakes; the unit is not yet verified by systemd.
- [x] **fstab:** games card by label `EASYROMS` with `nofail`. Assumes FAT/exFAT (`uid=1000`); phase 1 confirms.
- [x] **Manual CI job** `.github/workflows/rootfs.yml` (2026-11-06, workflow_dispatch only): checks, build, sanity checks, `systemd-analyze verify`, uploads `r3i6nos-rootfs.tar.zst`.
- [x] **First successful run of that job** (2026-11-06, after pinning `ubuntu-24.04` and unmounting `/proc` before `du`): rootfs builds, sanity checks pass, artifact uploaded (444 MB tree).
- [x] **`systemd-analyze verify` fails the job** (2026-11-06; its only message was a false alarm from checking the runner's paths, now `--root=build/rootfs`).
- [ ] **Confirm that run:** the `--root` form is not yet run; fix any real warnings it shows.
- [ ] **Slim the 444 MB tree** (probably Mesa's LLVM stack); see phase 6.
- [ ] **Chroot tests under qemu:** Python and Lua tests, `deb_watcher.py` with a real `.deb`, `systemd-analyze verify`.

## 4. Phase 3: LÖVE runtimes (no hardware)

- [x] **LÖVE 11.5** `scripts/build_love.sh` (2026-11-06, built in CI): cross-compiles the pinned release tarball in a
  Debian 12 container, installed to `/opt/love/11.5/` by `configure_rootfs.sh`; the `love` job in `rootfs.yml` builds it first.
- [x] **LÖVE 12.0** (2026-11-06, built in CI): `build_love.sh 12.0`, pinned to `love2d/love` commit `def2451`
  (2026-11-05), CMake. It needs **SDL3** (>= 3.1.3), which Bookworm lacks, so SDL3 3.4.20 is built too (KMS/DRM, ALSA,
  no X11/Wayland) and shipped in the runtime's `lib/`. Both pins are sha256-checked. Matrix job in `rootfs.yml`.
- [x] **First green 12.0 build and smoke test** (2026-11-06, run reported all green; the 12.0 smoke output line was not
  read): SDL3 needed `SDL_UNIX_CONSOLE_BUILD`; shellcheck notes fixed.
- [ ] **Re-pin 12.0** when it matters: it is a moving nightly, so keep the commit, note why each move, and test games on both.
- [x] **Smoke test** (2026-11-06, passed in CI: `R3I6NOS_SMOKE ok love=11.5.0 jit=LuaJIT 2.1.0-beta3`): `scripts/smoke_love.sh` runs the rootfs's `love` under
  `qemu-aarch64-static -L`, dummy video and audio, on `tests/smoke/love` (data, zlib, filesystem, PNG, physics, sound
  modules); a step in `rootfs.yml` that runs on every runtime in the rootfs (11.5 and 12.0). Does not prove drawing.
- [x] **Cache** the built runtime in CI (`actions/cache`, keyed on `build_love.sh`).
- [x] **First green `love` job** (2026-11-06): LÖVE 11.5 cross-compiles for arm64 and links against the rootfs.

## 5. Phase 4: image and flashing

- [ ] **`scripts/assemble_image.sh`:** MBR layout, `mktemp` mount points with a `trap`, rsync the rootfs; no proprietary files.
- [ ] **`scripts/inject-boot.sh`:** writes the raw bootloader sectors and `BOOT` files from `local_hardware_assets/` into the image.
- [ ] **`scripts/flash-macos.sh`:** refuses internal disks, shows name and size, requires typing the identifier, verifies the `.sha256`, writes with `rdisk` and `bs=4m`, ejects.
- [ ] **`.github/workflows/build.yml`:** tags and manual runs only; uploads the image `.gz` and checksum.
- [ ] **Image checks:** partition layout, `fsck`, raw-sector readback.

## 6. Phase 5: bring-up on the R36S (needs the unit)

- [ ] **First boot:** reaches the menu; panel, backlight, D-pad and buttons, audio, TF2 mount.
- [ ] **A LÖVE 11.5 game and a 12.0 game** run and return to the menu.
- [ ] **A `.deb`** installs, launches and uninstalls by removal.
- [ ] **Fix** what hardware shows; record each unit tested in the README.

## 7. Phase 6: slim and release

- [ ] **Strip** services; measure free RAM and boot time against the spec's claims.
- [ ] **Boot splash** with progress, errors shown on screen, never a blank panel.
- [ ] **Release:** image uploaded by hand with `gh release upload`, checksums in the notes.
- [ ] **Flashing guide** for macOS (from the script), Windows and Linux.
- [ ] **Native apps:** dependency handling beyond bundled libs; choosing which binary a package launches.

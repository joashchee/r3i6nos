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

- [x] **`scripts/capture-card.sh`** written (see phase 4); **now run it on your card** (needs `brew install e2fsprogs` for the modules).
- [ ] **Run `scripts/device-info.sh` on the R36S** (stock OS booted, over SSH) and keep the output in `local_hardware_assets/info/`.
- [x] **Confirm the board** is RK3326 (spec section 1): yes, see Phase 1b.
- [ ] **Capture the gamepad mapping** on a real unit; replace the unverified `SDL_GAMECONTROLLERCONFIG` in `run_app.sh`.
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
- [ ] **Games card:** stock has `/roms` on partition 3 of the same card; ours expects TF2 by label. Add an optional
  partition 3 (EASYROMS exFAT) to the image, or document TF2 only.

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

- [ ] **First boot:** reaches the menu; panel, backlight, D-pad and buttons, audio, TF2 mount.
  - Done (2026-11-07, one R36S, Panel 4 DTB from the stock card): ArkOS splash, kernel, initramfs, root by UUID, systemd to
    the end of boot, text console on the panel. `EASYROMS` timeout is expected (no such partition).
  - **Open:** after that nothing is drawn. Blocked on the log: flash an image injected with `R3I6NOS_BOOT_DEBUG=1`, boot, read
    `r3i6nos-bootlog.txt` from BOOT on the Mac. Result of that boot (2026-11-07): `loveos.service` ran and looped; LÖVE could not create a window (wrong Mali blob, see 1b)
    and `deb_watcher.py` crashed (`No module named 'shutil'`: `python3-minimal` has none; fixed with `libpython3-stdlib` and a CI check).
    Next: rebuild, inject the right blob, flash, boot again.
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

# Changelog

All notable changes to r3i6nos are recorded here, newest first. Update it
whenever a change lands, dated the day it was made.

## Unreleased

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

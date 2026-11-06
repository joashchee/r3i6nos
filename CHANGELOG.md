# Changelog

All notable changes to r3i6nos are recorded here, newest first. Update it
whenever a change lands, dated the day it was made.

## Unreleased

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

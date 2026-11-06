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
  (`usr/local/bin/`, `etc/init.d/`). Paths mirror the device.
- `menu/`: the boot menu, a LÖVE 11.5 app installed at `/opt/loveos/menu`.
- `scripts/`: host-side tools. `check.sh` runs everything CI runs.
- `tests/`: Python (`unittest`) and Lua tests; no hardware needed.
- `vendor/`, `build/`: fetched or built, gitignored.

## Rules

1. **Say what was not run on a real R36S.** Nothing in this repo has booted
   on hardware yet. A claim about behaviour on the device is "untested"
   until ROADMAP section 2 says otherwise. The gamepad mapping in
   `run_app.sh` is unverified.
2. **No copyrighted content in the repo or the image:** no ROMs, BIOS files,
   or proprietary emulator assets. Record the licence and source of every
   vendored base, kernel or runtime.
3. **Only RK3326 R36S boards.** Allwinner and other clones (spec section 1)
   must never get this image; keep the identification steps in the flashing docs.
4. **Overlay scripts stay small and dependency-light** (bash, Python 3
   standard library): they run at boot on 1 GB of RAM. Path overrides go
   through `R3I6NOS_*` environment variables so tests can run on a laptop.
5. **The menu never scrolls:** whole pages, `Page n/m` in the foot.
6. **No telemetry, no network at boot, no accounts.**
7. **Keep the user's data safe:** the games card (`EASYROMS`) is theirs. Boot
   scripts only read it; never format, repartition or delete from it.

## Check before committing

`scripts/check.sh` (Python tests, Lua test, `bash -n`, shellcheck). Report
tools that were not installed rather than calling the run clean.

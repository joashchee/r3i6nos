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
- `menu/`: the boot menu, a LÖVE 11.5 app installed at `/opt/loveos/menu`.
- `apps/`: the apps (each native one has an `app.conf` and its own workflow, `.github/workflows/app-<name>.yml`; only `DEFAULT=1` apps enter the image). The default apps built into the image (`/opt/loveos/defaults`): the input checker as LÖVE 11.5, LÖVE 12.0 and native ARM64, and Commander (a native ARM64 file manager). See `apps/README.md`.
- `theme/`: the r3i6nos-ansiapps text-mode theme for LÖVE (`.lua`) and C/SDL2 (`.h`): font, screen, list dialogs; installed at `/opt/loveos/theme`. See `docs/R3I6NOS-ANSIAPPS-THEME.md`.
- `scripts/`: host-side tools. `check.sh` runs everything CI runs.
- `tests/`: Python (`unittest`) and Lua tests; no hardware needed.
- `vendor/`, `build/`, `local_hardware_assets/`: fetched, built or captured, gitignored.
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
6. **No telemetry, no network at boot, no accounts.**
7. **Keep the user's data safe:** the games card (`EASYROMS`) is theirs. Boot
   scripts only read it; never format, repartition or delete from it.
8. **Every app made for r3i6nos starts from the r3i6nos-ansiapps theme** (`theme/`, `docs/R3I6NOS-ANSIAPPS-THEME.md`): the IBM VGA
   8x16 font scaled to 10x20, **64 columns by 24 rows** on the 640x480 screen, row 0 the reserved top bar, rows 1-20 the display view, rows 21-23
   the three-row command line (reply, prompt, status), list dialogs and their button rules as KlamANSI does them, only the 16 VGA colours in
   pairs that pass 4.5:1, nothing scrolls. New apps, LÖVE or native, copy the menu's structure; an app that cannot follow it says why in its README.
   The font is CC BY-SA 4.0: keep `NOTICE`, `theme/font/` (licence, credit, changes) and the headers of the generated font files intact.

## Check before committing

`scripts/check.sh` (Python tests, Lua test, `bash -n`, shellcheck). Report
tools that were not installed rather than calling the run clean.

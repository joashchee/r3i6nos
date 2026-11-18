# PortMaster in the image (for testing)

PortMaster (https://portmaster.games, https://github.com/PortsMaster/PortMaster-GUI) is a curated catalog and installer of game ports for Linux handhelds.
It is in the image **for testing** (maintainer's request 2026-11-18), to find out what works on an R36S under r3i6nos. ROADMAP section 31 has the state.

## What was checked (2026-11-18)
- **Licence:** the GUI (`PortMaster-GUI`, "Copyright (c) 2023-2025 Jacob", MIT, read from its LICENSE) and the port repository (`PortMaster-New`, MIT by GitHub's badge, text not read) are MIT.
  The release zip's own `licenses/` folder carries its third-party parts. **Ports carry their own licences** (each package has a `licenses/` folder; the
  project's developer-permissions page lists developers who gave permission, some "unclear" or "missing licence file", one denied). It says it ships no copyrighted content.
- **Game data:** *Ready to Run* ports are open source, freeware or distributed with the developer's permission; all others need the person's own game files.
- **Curation:** ports go in by pull request, are checked by tools (`build_release.py --do-check`) and reviewed by the crew.
- **Support:** ROCKNIX, ArkOS, muOS, KNULLI, AmberELEC and UnofficialOS; "stock firmware is generally not supported". The R36S is listed, on three of those. r3i6nos is an unsupported firmware.
  Many ports list needs (`power`, `opengl`, 2 GB RAM, above 640x480) the R36S (RK3326, 1 GB, 640x480) cannot meet.
- **Network:** installing from inside it needs WiFi; offline, a port's zip goes in `autoinstall/`.
- **Not read:** the full port list (some engines or ports may be emulators, which the project rule "no emulators" excludes: look before installing), and what a new firmware must provide.

## What the image carries
`PortMaster.deb` (package `r36s-portmaster`, built by `scripts/build-portmaster.sh` from the pinned release in `apps/portmaster/app.conf`: tag, URL, sha256; the build stops on a mismatch):
PortMaster's arm64 files (about 30 MiB unpacked), `usr/games/portmaster` (the launcher), `r3i6nos/launch_hooks.txt`, `shim/sudo`, and `usr/share/doc/portmaster/{NOTICE,SOURCE,copyright}`.
`deb_watcher.py` installs it at boot like Commander, so it is a menu entry named PortMaster. `NOTICE` records the licence and source (CLAUDE.md rule 2).
The rootfs also gains the four SDL2 add-on libraries its GUI loads and `procps`.

## How it runs
- **First start:** the launcher copies PortMaster to `/roms/ports/PortMaster` (the folder its own scripts look in; **system card**), again only when the package is newer than the copy
  (PortMaster updates itself from its GUI and must not be overwritten by an older package). Runtimes and ports it downloads go on `/roms/ports` too: hundreds of MB, not the root filesystem.
- **Hooks:** the launcher starts PortMaster's own `utils/pm_launch.txt r3i6nos`, whose `r3i6nos/launch_hooks.txt` sets where Debian's SDL2 is and skips what needs tty or root.
- **No root:** a `sudo` shim on `PATH` says no, so PortMaster and the ports run as `loveos` (`ESUDO` empty). A real `sudo` would wait for a password nobody can type.
- **The pad:** the measured R36S mapping (GUID `1900bb3e...` and the CRC-less one, as `run_app.sh`) is added once to PortMaster's controller database.
- **Installed ports** appear in the menu: `menu/scan.lua` lists `<card>/ports/*.sh` (not `PortMaster.sh`) as type `port`; `run_app.sh port <script>` runs the script from its folder with the shim.
- **No network at boot** (rule 6): PortMaster talks to the network only when a person opens it and chooses to; its GUI also checks for its own updates when it opens (hourly at most).

## Rules that do not apply
It is third-party, so the theme (rule 8), the device report (9), Rec demo (11), splash (12), toast (13), power section (14) and the app data folder (16: PortMaster's scripts fix `/roms/ports`) are not done for it.

## Tested, untested
- Tested without a unit: `tests/test_portmaster.sh` (fake zip), `tests/test_scan.lua`; and a hand run in Debian 12 arm64 with the image's library set and a login user: the launcher installs it, pugwash extracts its libraries, finds the device and builds its window; it stops at the
  SDL renderer with the dummy video driver (no accelerated renderer), as expected headless. Without the four SDL libraries it stopped at the first import.
- **Untested on an R36S:** everything that draws, takes the pad, joins a network, installs and runs a port; `/dev/uinput` for `gptokeyb` without root; the Mali blob's GLES for ports; memory on 1 GB; whether the first start fits.
- **To try first on a unit:** open PortMaster from the menu; read `/roms/ports/PortMaster/log.txt`; join Wi-Fi in Settings > Network; install one small open-source port; run it from the menu; bring the log back.

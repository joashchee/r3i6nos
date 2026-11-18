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
- **Run on an R36S (2026-11-18, maintainer's report):** it starts, the menu works and the audio is heard. Its probe read: `debian gnu/linux` 12, kernel 4.4.189, 640x480, `mali`, aarch64, 1024 MB, glibc 2.36, no swap, no zram; with no network it runs offline.
- **Found on the unit (2026-11-18):** Wi-Fi was detected and PortMaster began its OTA update, then crashed: `No space left on device` writing `images.zip` into `/tmp` (64 MB). Fixed in the launcher by `TMPDIR` on the card (`/roms/ports/PortMaster/.tmp`); not yet rerun.
- **Untested on an R36S:** pad mapping in the GUI, joining a network and installing and running a port; `/dev/uinput` for `gptokeyb` without root; the Mali blob's GLES for ports; memory on 1 GB; whether the first start fits.
- **To try first on a unit:** open PortMaster from the menu; read `/roms/ports/PortMaster/log.txt`; join Wi-Fi in Settings > Network; install one small open-source port; run it from the menu; bring the log back.

## The crew's guide for a custom firmware, against r3i6nos (read 2026-11-18)
Source: the PortMaster crew's gist for integrating a CFW (https://gist.github.com/bmdhacks/f26dbb60dc0ff0b9bc8e043fac8b813b, by bmdhacks). The page was read through a summariser, so exact wording is
not quoted and some details may be lost; the checks against r3i6nos are from this repository's docs and the 2026-11-18 Debian 12 run.

**Fits r3i6nos**
- Debian 12: GNU bash 5.2 and coreutils, glibc 2.36 (they ask 2.34 or newer), systemd, `mount`/`umount`/`zramctl` (util-linux), `tar`, `gzip`, `getconf`, `ldd`.
- SDL2 with mixer, image and ttf (they ask for all three; the image has them since 2026-11-18, and gfx too), ALSA, OpenAL, freetype, libpng.
- aarch64 (the one required architecture); GLES 2 through the Mali `libmali` blob and SDL's KMS/DRM driver (they prefer panfrost but accept others); exFAT and squashfs and loop devices are in the stock kernel (`docs/hardware-findings.md`).
- Python 3 with `lzma` (they require it; the run in Debian 12 extracted its `.tar.xz` fonts); SSH (off until a person turns it on); `$HOME` writable (`/home/loveos`); no system-wide `LD_PRELOAD`.
- The control folder search order ends in `/roms/ports/PortMaster`, which is where the launcher puts it; the hook mechanism it uses (`pm_launch.txt r3i6nos`) is PortMaster's own.
- Their test advice helps: the `pretend_device` debug mode (`DEVELOPING.md` in PortMaster-GUI) tests detection on a computer, and the ready-to-run smoke list starts with native ports.

**Conflicts and divergences, most serious first**
1. **Root.** PortMaster assumes `$ESUDO` (`sudo` or `doas`) works without a password, for mounting squashfs runtimes (`mount -o loop`), `chmod 666 /dev/uinput` for `gptokeyb`, `/dev/tty0` and `/dev/tty1`, and `zramctl`.
   r3i6nos runs everything as `loveos`, `sudo` needs a password (SSH only) and the launcher's shim says no. **Consequence: every port that needs a runtime (Mono, Godot, Java/Weston, the Love2D ports through PortMaster's LÖVE runtime) cannot mount it, and `gptokeyb`
   has no `/dev/uinput`.** Ports that are plain native binaries are not affected. Choices for the maintainer: (a) leave it and test native ports only; (b) a root helper like `r3i6nos-clock` and `r3i6nos-format` that mounts only `*.squashfs` files under `/roms/ports` (and `/roms2/ports`) on a request; (c) a udev rule giving the `input` group `/dev/uinput`; (d) a scoped, passwordless `sudoers.d` rule for those commands only. (b) to (d) widen what the login user can do and need a rule 7 or rule 6 note; `PM_CAN_MOUNT=N` is their switch for a platform that cannot mount, and what it costs is unread.
2. **No armhf.** They rate 32-bit ARM compatibility "strongly recommended": many runtimes have armhf builds and some ports are armhf-only. The image is aarch64 only. Adding it means multiarch, the 32-bit loader and libraries (SDL2, libc, libstdc++, ALSA, Mali GLES for armhf: the blob on the card is aarch64): size, and the blob has no 32-bit twin.
3. **Kernel 4.4.189 against what they list.** Squashfs with ZLIB, LZO, XZ and ZSTD: ZSTD in squashfs arrived in Linux 4.14, so it is probably missing (which compressions the PortMaster runtimes use is unread). `CONFIG_INPUT_UINPUT`, `CONFIG_ZRAM`, `CONFIG_SWAP` and `loop.max_loop=16` are unknown (the stock boot files set the command line; `r3i6nos-report` could read `/proc/config.gz` or `/proc/filesystems` and the loop count). exFAT is the vendor driver, not the 5.4+ native one they prefer; no FUSE deadlocks then. perf options are not needed. **This is the section that needs a unit.**
4. **Memory.** 1 GB, no swap or zram in the image (they strongly recommend swap). PortMaster filters its list by `1gb`/`2gb`/`4gb`, so most of it will be hidden or will run out of memory.
5. **Detection and support.** Their cascade ends at `/etc/os-release`, so r3i6nos is seen as `CFW_NAME` "Debian GNU/Linux" (no `mod_*.txt` for it, device "Unknown"); being supported means a pull request to PortMaster-GUI: a `PlatformBase` subclass registered in `HM_PLATFORMS` in `harbourmaster/platform.py`
   (`first_run()`, `portmaster_install()`, `gamelist_file()` and the directory properties) and entries in `hardware.py`. Until then PortMaster treats the firmware as generic, ports' own compatibility lists will not name it, and **the launcher's hook file is our own route that PortMaster's crew did not design**.
   Setting `ID=`/`NAME=` in the image's `/etc/os-release` to something stable would at least give detection a name. Whether the crew would accept a Debian-based firmware that is not an ES frontend is for them to say.
6. **Libraries they say a CFW should provide.** They list darkOS-style compatibility for Debian 11 sonames (FFmpeg 4.3 libs, `libx264.so.160`, `libwebp.so.6`, `libFLAC.so.8`, `libjpeg.so.8` and others) and expect `libfontconfig`, `libjpeg.so.62`. The image has none of FFmpeg's shared libraries (its `ffmpeg` is one static binary), no fontconfig, and Bookworm's newer sonames, so a port that links the system's FFmpeg or WebP fails to start.
7. **Tools they call required or strongly recommended** that the image lacks: `unzip`, `dos2unix`, `rsync`, `dialog`, `psmisc` (`killall`, `pidof`; `pgrep` and `pkill` came with `procps`). `xdelta3`, `innoextract` and `7z` come inside PortMaster. A few megabytes in all.
8. **`/tmp` is a 64 MB tmpfs** (`configure_rootfs.sh`); PortMaster and some ports unpack into it.
9. **The project's own rules.** RetroArch is on their smoke list (the Cave Story and 2048 cores) and some ports bundle `box86`/`box64`: running RetroArch cores and an x86 emulator is **an emulator** under "No emulators" (CLAUDE.md and the spec). PortMaster checks for its own updates and installs from the network (rule 6: only when a person chose it). It writes, replaces and deletes under `/roms/ports` on the system card
   (rule 7 lists exceptions one by one: this one would need adding, as `r3i6nos-pkg`'s was). Its data cannot follow rule 16's data-card move (its scripts fix `/roms/ports`, and its second-card switch reads `fstab`, which r3i6nos does not use for cards).
10. **Display and tty:** they say many ports write to `/dev/tty0` and `/dev/tty1`; the session owns tty1 for `loveos`, `tty0` is root's.

**What to do first on a unit (cheapest answers)**
Run `r3i6nos-report` plus `zcat /proc/config.gz | grep -E 'SQUASHFS|UINPUT|ZRAM|SWAP|BLK_DEV_LOOP'` (if the file exists), `ls -l /dev/uinput /dev/tty0`, and PortMaster's `log.txt`; try one **native** port from their smoke list (DevilutionX or VVVVVV: needs no runtime). That decides items 1 and 3 before any helper is written.

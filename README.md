# r3i6nos

Dedicated firmware for the **R36S** handheld (Rockchip RK3326, 3.5" 640×480).
It boots straight to a small menu and runs two kinds of thing, nothing else:

- **LÖVE games and apps**, on LÖVE 11.5 (stable) or 12.0 (nightly), each in its own runtime.
- **Native ARM64 Debian apps**, dropped in as `.deb` files.

No EmulationStation, no RetroArch, no ROM databases.

> **Status: scaffold.** The scripts and menu are written and tested on a
> laptop; nothing has booted on a real R36S yet. See `ROADMAP.md`.

## Using it

Put files on the second microSD card (`EASYROMS`):

```text
Love 11.5/    .love files or game folders for LÖVE 11.5
Love 12.0/    .love files or game folders for LÖVE 12.0
native_deb/   ARM64 .deb packages
```

Reboot. The menu lists everything it finds; D-pad to move, A to launch, L/R for pages.

**Check your hardware first.** Only genuine RK3326 R36S boards are supported;
Allwinner-based clones are not. How to tell is in section 1 of
[the spec](docs/r36s_love_only_firmware_specification.md).

## Making a card

The board's bootloader, kernel modules and GPU driver come from your own working card and are
added to the image on your Mac: see [docs/making-a-card.md](docs/making-a-card.md).

## Developing

```sh
scripts/check.sh    # Python and Lua tests, bash -n, shellcheck
luajit tests/test_scan.lua
love menu           # run the menu on a desktop (set R3I6NOS_ROMS to a test folder)
```

Layout and rules are in `CLAUDE.md`; what's next is in `ROADMAP.md`.

## Disclaimer

r3i6nos is an independent hobby project. It is **not made, endorsed or supported by** the makers or sellers of the R36S, by Rockchip, Arm, the LÖVE
project, Debian or anyone else whose names appear here; those names belong to their owners and are used only to say what this works with.

- **Use it at your own risk.** The software is provided as is, without warranty (`LICENSE`). Nothing here has booted on a real R36S yet.
- **Flashing writes to a disk.** The scripts refuse internal disks and ask first, but you choose the target: check the disk identifier twice, and keep a backup of your working card.
- **No games, ROMs, BIOS files or proprietary drivers are included**, and the project does not help find them. The GPU driver, kernel modules and bootloader are copied
  from *your own* card onto *your own* images. An image that contains them is for your card only; do not share it.
- **Games and `.deb` files you add are your responsibility.** They run on the device without a sandbox, so install only what you trust.
- Security problems: see [SECURITY.md](SECURITY.md).

## Licence

MIT, see `LICENSE`, **except the theme's font** (`theme/font/`, `theme/vga8x16.lua`, `theme/r3i6nos_vga8x16.h`), which is VileR's
IBM VGA 8x16 under CC BY-SA 4.0 (credit: *The Ultimate Oldschool PC Font Pack*, <https://int10h.org/oldschool-pc-fonts/>; see `NOTICE`).
LÖVE is zlib-licensed; the base system and runtimes keep their own licences.

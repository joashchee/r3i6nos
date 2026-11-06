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

## Developing

```sh
scripts/check.sh    # Python and Lua tests, bash -n, shellcheck
luajit tests/test_scan.lua
love menu           # run the menu on a desktop (set R3I6NOS_ROMS to a test folder)
```

Layout and rules are in `CLAUDE.md`; what's next is in `ROADMAP.md`.

## Licence

MIT, see `LICENSE`. LÖVE is zlib-licensed; the base system and runtimes keep their own licences.

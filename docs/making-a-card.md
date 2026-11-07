# Making a card (macOS)

Your own R36S, your own working stock card, and a Mac. The public CI image has
no bootloader, kernel modules or GPU blob in it (they are the vendor's); you add
them from your own card, so the result is for your cards only.

One-off: `brew install mtools e2fsprogs`.

1. **Capture your stock card** (card in a reader; nothing is written to it):
   `scripts/capture-card.sh disk4`  (`diskutil list external physical` finds the disk).
   Everything lands in the gitignored `local_hardware_assets/`.
2. **Get the image.** In the repo's Actions tab, run "Build (manual and tags)", download
   the `r3i6nos-image` artifact, and `gunzip` it to `r36s-loveos.img`.
3. **Add the board's boot files:** `scripts/inject-boot.sh r36s-loveos.img`.
   Writes the bootloader, copies the boot files, and points `root=` at the image.
4. **Add the modules, firmware and Mali blob:** `scripts/inject-rootfs.sh r36s-loveos.img`.
   Works on a copy and writes it back only after fsck and a full read-back pass.
   Run it once per fresh image.
5. **Flash it** to a different microSD card:
   `scripts/flash-macos.sh r36s-loveos.img disk5 --dry-run`, then without `--dry-run`.
   The rest of the card, whatever its size, becomes the games partition (below); add `--no-games` to skip that.
6. Put the card in TF1 and power on.

## Built-in apps

Every image carries the **R36S Input Checker** three ways (LÖVE 11.5, LÖVE 12.0, native ARM64; `apps/README.md`), so the menu
is never empty and you can test the pad before adding any games. They live on the system partition, not on `EASYROMS`.

## Where the games go

The menu lists what it finds under `/roms`, the partition labelled `EASYROMS`:

| Folder | What goes in it |
|---|---|
| `Love 11.5/` | `.love` files, or game folders with a `main.lua` |
| `Love 12.0/` | the same, for games that need LÖVE 12 |
| `native_deb/` | ARM64 `.deb` packages; they are installed on the next boot |

Two ways to have that partition:

- **One card (the default):** `scripts/flash-macos.sh r36s-loveos.img disk5` makes everything after the
  system partition an exFAT partition named `EASYROMS` and creates the three folders. A Mac or Windows PC
  can write to it. The card must be at least about 4 GB. If this step fails the script says which step, what happened and what to do,
  leaves the card in place (not ejected) and exits non-zero; the system partitions are never touched by it.
- **A card already flashed without it:** `scripts/flash-macos.sh --games-only disk5` adds the partition without writing the image (the
  existing partitions are kept; it refuses a card that is not an untouched r3i6nos card).
- **Two cards:** a second card in TF2, formatted FAT32 or exFAT, labelled `EASYROMS`, with the folders above.

The games partition is never formatted by the device: only `flash-macos.sh` creates it, once, on a
card it has just erased (or `--games-only` on a card with free space and no partition 3). Without one, boot still works; the menu is just empty.

**Never share the result.** The image contains the vendor's Mali blob, kernel
modules and bootloader. Share the repo, not the card image.

If a step says what is wrong, believe it: each one refuses rather than guesses.

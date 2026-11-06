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
6. Put the card in TF1, a games card labelled `EASYROMS` in TF2, and power on.

**Never share the result.** The image contains the vendor's Mali blob, kernel
modules and bootloader. Share the repo, not the card image.

If a step says what is wrong, believe it: each one refuses rather than guesses.

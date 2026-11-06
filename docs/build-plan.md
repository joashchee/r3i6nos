# Build plan and gaps in the flash guide

Read with `R36S_LÖVE_OS_Automated_Build-Flash_Guide.md` (the guide) and
`r36s_love_only_firmware_specification.md` (the spec). The phases are in
`ROADMAP.md`; this file says why they are shaped as they are. Written
2026-11-06 from the guide as first received; nothing here has run.

## What the guide gets right

- Build in CI, flash locally: the proprietary board files never reach the public repo.
- `local_hardware_assets/` is gitignored and holds what comes off the user's own working card.
- `rdisk` for the write, `diskutil unmountDisk` first, `eject` after.

## What would not boot or would not run as written

1. **No bootloader.** An RK3326 boots from U-Boot and its loader written
   to raw sectors *before* the first partition (idbloader around sector 64,
   `u-boot.itb` around sector 16384, layout to be confirmed from the card).
   Copying the files in the `BOOT` partition does not carry them, and
   `assemble_image.sh` starts from zeros. The image needs those sectors from
   the user's working card (or a U-Boot we build, a later decision).
2. **No kernel modules.** Stock `Image` and `.dtb` files come with a matching
   `/lib/modules/<version>` and firmware in the rootfs. The debootstrap
   rootfs has neither; a mismatched kernel loses the panel, audio, input or SD
   on the second slot. The rootfs must take the modules from the same card.
3. **Foreign-architecture debootstrap.** On an x86 runner a single
   `debootstrap --arch=arm64` fails at its second stage; use `--foreign`, copy
   `qemu-aarch64-static`, then `chroot ... /debootstrap/debootstrap --second-stage`.
4. **Nothing of ours is installed:** no LÖVE runtimes, menu, overlay scripts,
   `dpkg` (for `dpkg-deb`; minbase has it), mesa/panfrost (the Mali-G31 needs
   `libgl1-mesa-dri` and `libdrm`), audio (`alsa-utils`), fstab for the games
   card, hostname, a user. `S99loveos` is SysV; the guide's rootfs is
   systemd, so the boot hook becomes a systemd unit running the same loop.
5. **Workflow trigger:** building a 4 GB image on every push to `main` is
   wasteful. Build on tags and `workflow_dispatch`; `ci.yml` keeps running the fast checks.
6. **Artifact names disagree:** CI makes `r36s-loveos-custom.img.gz`; step 2
   downloads `loveos-base-image.img.gz` (that is the artifact *folder* name).
7. **macOS `dd`:** BSD `dd` has no `status=progress` and wants `bs=4m`
   (lowercase suffix). Press Ctrl-T for progress, or use `pv`. Untested here.
8. **Wrong disk is the worst failure.** The guide's `dd` line has no guard.
   The flashing script refuses internal disks, shows the disk's name and
   size, and makes the user type the identifier.
9. `local_hardware_assets/` is ignored in the guide's `.gitignore`, but its
   rootfs working directory is not; build output goes under `build/` (ignored).
10. Hard-coded `/mnt/...` mount points and no `trap` to unmount: a failed
    build leaves loop devices attached. Use `mktemp -d` and a `trap`.

## Test without hardware

- Rootfs in CI: after the second stage, run in the chroot under qemu-user:
  the Python tests, `deb_watcher.py` against a real `.deb` with real
  `dpkg-deb`, and the menu scan test.
- LÖVE under qemu with `SDL_VIDEODRIVER=dummy` and `SDL_AUDIODRIVER=dummy`: a
  test `.love` that prints its version and quits proves the runtime links
  and starts, not that it draws.
- Image: `sfdisk -d` against the expected layout, `fsck` both partitions, the
  raw-sector offsets checked by reading them back, and the unit file with `systemd-analyze verify`.
- Anything on the real panel, pad, audio, SD slot, battery or Mali driver stays
  "untested" until phase 5.

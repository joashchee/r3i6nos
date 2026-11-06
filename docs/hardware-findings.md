# What the stock card showed

From `scripts/capture-card.sh` on a working R36S card, 2026-11-06. Only facts
that decide our design are recorded here; the files themselves (kernel, DTBs,
bootloader, modules) are the vendor's and stay in the gitignored
`local_hardware_assets/`.

## The board is supported

DTBs on the card: `rk3326-r35s-linux.dtb`, `rk3326-rg351mp-linux.dtb`,
`rg351mp-kernel.dtb`, `rg351p-kernel.dtb`. None of the clone markers from the
spec (`rk3326-evb-lp3-v12`, `rf3536k4ka`, `eGON.BT0`). Genuine RK3326.

## Card layout (MBR)

| Partition | Type | Start (sector) | Size | Use on the stock OS |
|---|---|---|---|---|
| 1 | 0x0b FAT32 | 32768 (16 MiB) | 112 MiB | `/boot`: `Image`, `uInitrd`, DTBs, `boot.ini` |
| 2 | 0x83 ext4 | 262144 | about 10.6 GiB | `/` |
| 3 | 0x07 exFAT | 22531262 | about 108 GiB | `/roms` (`uid=1002`, `umask=000`) |

- The bootloader is raw data from sector 64 to about sector 29,500 (15.1 MB),
  before partition 1. Our layout (`scripts/image-layout.sh`) puts BOOT at the
  same sector, 32768, so the captured bootloader fits unchanged.
- **The stock OS keeps the games on partition 3 of the same card.** The spec
  and our fstab expect a second card (TF2) labelled `EASYROMS`. Mounting by
  label works for either, but the image has only two partitions.

## How it boots

U-Boot reads `boot.ini` (a Hardkernel-style script), loads `Image` (kernel),
`uInitrd` (an Ubuntu initramfs) and a DTB, and sets
`root=UUID='...' rootwait rw fsck.repair=yes ... console=/dev/ttyFIQ0`. The
`ScreenFiles/Panel 0..5` folders hold a `boot.ini` and DTBs per display panel
variant (a `panel=` argument selects one). Our image keeps all of it and only
rewrites `root=` to the image's own ROOTFS UUID (`inject-boot.sh`).

## The kernel is 4.4.189 (Rockchip vendor kernel)

This matters more than anything else on this page:

- **No Mesa panfrost.** Panfrost is a mainline kernel driver (5.2 and later).
  On 4.4 the Mali-G31 works only through the vendor's proprietary `libmali`
  userland. The rootfs's `libgl1-mesa-dri` cannot drive the GPU here.
  Options are in ROADMAP "Phase 1b".
- **The modules must match the kernel.** `/lib/modules/4.4.189` is captured; it
  has to go into the image's root, or anything not built into the kernel fails.
- **Debian 12 on a 4.4 kernel is a risk.** glibc 2.36 is fine (needs 3.2+);
  systemd 252 is the question. Unknown until it boots.
- The stock `fstab` mounts `exfat` through the kernel, so the kernel has
  an exFAT driver.

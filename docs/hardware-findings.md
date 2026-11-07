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

## The stock card has two Mali blobs; only one matches the kernel

Found on the first boot (2026-11-07), by reading the root partition of a full backup of the stock card:

| Where | Blob | Userspace API | Used by stock? |
|---|---|---|---|
| `/usr/lib/aarch64-linux-gnu` | `libmali-bifrost-g31-rxp0-wayland-gbm.so` (r6p0, 2020) | 10.6 | no |
| `/usr/local/lib/aarch64-linux-gnu` | `libmali-bifrost-g31-rxp0-gbm.so` (r13p0, 2021) | matches | **yes** |

- `/etc/ld.so.conf.d/libc.conf` lists `/usr/local/lib` first, and the `/usr/local` directory has its own
  `libEGL`, `libGLESv1_CM`, `libGLESv2`, `libgbm`, `libOpenCL` and `libmali.so` links to the gbm blob.
- With the wrong (`/usr/lib`) blob every GL program fails with
  `file /dev/mali0 is not of a compatible version (user 10.6, kernel 11.7)`, then
  `Can't load EGL/GL library on window creation`. The kernel's Mali driver (built in, `/dev/mali0`) speaks 11.7.
- The gbm blob needs only libc-family libraries and `libdrm.so.2`; no Wayland.
- `capture-card.sh` now prefers `/usr/local/lib/aarch64-linux-gnu`. Fixtures: `tests/fixtures/stock-gpu-libs.txt`
  (the old `/usr/lib` listing) and `stock-gpu-libs-usr-local.txt` (the right one).

## Boot on the real panel (first boot)

- The stock `boot.ini` loads the panel DTB from `ScreenFiles/Panel N/` when `PanelNum` is saved in U-Boot's
  environment, else from the BOOT root. The stock card's root DTB equals Panel 4's, and a card with no saved
  `PanelNum` booted with it (`panel=unset` on the command line) and drew the console.
- The kernel prints `/dsi@ff450000/panel@0/display-timings: could not find phandle` about three times
  every time a program opens the display. It is noise: the console and display work.

## Input layout (measured on one R36S, 2026-11-07, with the input checker)

Raw button numbers as LÖVE reports them (counting from 1; SDL's own index is one less):

| Input | Button |
|---|---|
| A, B, X, Y | 1, 2, 3, 4 |
| D-pad left, down, up, right | 9, 10, 11, 12 (**buttons, not a hat**) |
| Fn, Select | 13, 14 |
| Left stick press (L3), right stick press (R3) | 15, 16 |
| Start | 17 |
| L1, R1, L2, R2 | **not measured** (the checker assumes 5, 6, 7, 8) |
| Power, volume up, volume down | not on the joystick (17 buttons, all used): expected as keyboard keys; **not yet seen** |

- The joystick has 4 axes (left stick 1-2, right stick 3-4) and no hat in use.
- SDL's gamepad mapping for this pad does not match the raw layout above (Select, Fn and Start did not light when
  the checker read them through the mapping), so anything that needs the real layout should read raw buttons or
  set its own `SDL_GAMECONTROLLERCONFIG`.
- `apps/input-checker-love/model.lua` (`LAYOUT`) and `apps/input-checker-native/input_checker.c` (`B_*`) hold these numbers.
- A report that the event lines looked inverted (UP on press, DOWN on release) was not reproduced by reading the code;
  the checker's event lines now say whether SDL reports the button held at that moment, so a real inversion shows.


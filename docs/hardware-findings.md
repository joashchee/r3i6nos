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
  and our fstab expected a second card (TF2) labelled `EASYROMS`. Mounting by
  label works for either, but the image has only two partitions. (Since 2026-11-10 the cards are mounted by role, not label:
  `docs/STORAGE.md`.)

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
| B, A, X, Y | 1, 2, 3, 4 (A is 2 and B is 1; corrected 2026-11-07) |
| D-pad up, down, left, right | 9, 10, 11, 12 (**buttons, not a hat**; corrected 2026-11-07) |
| Select, Start | 13, 14 |
| Left stick press (L3), right stick press (R3) | 15, 16 |
| Fn | 17 |
| L1, R1, L2, R2 | 5, 6, 7, 8 (confirmed by the maintainer, 2026-11-07) |
| Power, volume up, volume down | not on the joystick (17 buttons, all used): keyboard keys; the checker's key names are confirmed correct (2026-11-07). Physically, Power is on the left side and the volume keys are on the right |

- The joystick has 4 axes (left stick 1-2, right stick 3-4) and no hat in use.
- SDL's gamepad mapping for this pad does not match the raw layout above (Select, Fn and Start did not light when
  the checker read them through the mapping), so anything that needs the real layout should read raw buttons or
  set its own `SDL_GAMECONTROLLERCONFIG`. `run_app.sh` now carries this layout in its default mapping (2026-11-07), but its **GUID is still the
  spec's guess**, never read from a unit, so SDL may not apply it: read the real one from `device-info.sh` ("input devices").
  **Read 2026-11-10** (below): `1900bb3e4b4800000011000000010000`, now in `run_app.sh`.
- `apps/input-checker-love/model.lua` (`LAYOUT`) and `apps/input-checker-native/input_checker.c` (`B_*`) hold these numbers.
- A report that the event lines looked inverted (UP on press, DOWN on release) was not reproduced by reading the code;
  the checker's event lines now say whether SDL reports the button held at that moment, so a real inversion shows.

## First device report (KlamANSI, 2026-11-10, one R36S, r3i6nos booted)

The first report from a unit (`docs/DEVICE-REPORTS.md`; KlamANSI's Start > Device report, a native SDL2 and QuickJS app installed from
`native_deb/` by `deb_watcher.py`). What it settles:

| What | Measured |
|---|---|
| Kernel | Linux 4.4.189 aarch64 (the stock kernel, as captured) |
| CPU | 4 cores, part `0xd04` (Cortex-A35), **1512 MHz** max (not 1.3 GHz as the plans guessed) |
| Memory | 919232 kB total (about 898 MB for Linux), 848420 kB available with KlamANSI running |
| SDL | **2.26.5** (Debian 12's), video driver **KMSDRM**: a native app draws on the panel with the Mali blob from the card |
| Sound | one ALSA card, `rockchip,rk817-codec` (RK817 PMIC codec); SDL opens it with the `alsa` driver, one output device. Neumetik's report: opened at 44100 Hz, 2 channels, 1024 frames, and the callback ran 43 times in a second (as expected). Not yet heard |
| Pad | 1, **"GO-Super Gamepad"**, GUID **`1900bb3e4b4800000011000000010000`**, 17 buttons, 4 axes, 0 hats (the measured layout above) |
| Games card | the flashed card's partition 3, `/dev/mmcblk0p3` on `/roms`, **exFAT**, 125.7 GB; `loveos` can make a folder, write, rename over a file, read back and delete |
| Speed (KlamANSI) | its scripts load in 597 ms; a typed line runs and draws in 38-47 ms; a frame of its screen took 33.6 ms (goal 33); a whole 640 x 480 true-colour paint 4.6 ms. About 20 times the Mac's times (M-series, same QuickJS) |
| Memory (KlamANSI) | peak 47 MB |

- The GUID decodes as bus `0x19` (host), CRC `bb3e` of the name (SDL 2.26 adds it), vendor `0x484b`, product `0x1100`, version `0x0100`.
  `run_app.sh` maps it with and without the CRC (SDL before 2.26 leaves it zero). Whether an app using SDL's gamepad API then sees the
  right buttons is **untested**.
- The games card being `mmcblk0` means the flashed card itself (TF1), with `flash-macos.sh`'s partition 3: no second card was needed.

### The system, from r3i6nos's own report (2026-11-10, image `53f46b3`)

- The device tree's model string is **"Game Console R35S/R36S fix by lxc"** (the stock card's DTB).
- systemd 252 on the 4.4 kernel: **no failed units**. Whether the boot reaches "finished" is not known yet (`systemd-analyze time` gave no
  answer; the report now records why).
- Two mmc hosts, `mmc0` and `mmc1`; the boot card is `mmcblk0` (128.2 GB). The kernel has **squashfs**, loop devices, exfat, vfat and ext4.
- The boot card (exFAT partition 3): **32.4 MB/s** writing, **65.3 MB/s** reading.

### Graphics under LÖVE (NoCoGaMa's device spike, LÖVE 12.0, 2026-11-10)

- LÖVE 12.0.0 runs on the unit with **LuaJIT 2.1.0-beta3, JIT on** (Debian's system LuaJIT, which r3i6nos's builds link), so 11.5 should have
  it on too (not yet seen).
- Renderer **OpenGL ES 3.2**, `v1.r13p0-01rel0`, GPU **ARM Mali-G31**: the blob from the user's card works with SDL's KMSDRM and gbm.
- The window is 640x480 fullscreen. With vsync off it still draws at 60 fps, which suggests KMSDRM waits for each page flip: frame rates top
  out at the panel's 60 Hz, so a program's headroom shows in its CPU time, not its fps.
- LÖVE treats the pad as a gamepad (`isGamepad` true), through SDL's own database or `run_app.sh`'s mapping: which one, and whether its buttons
  then match the raw layout, is not known yet.
- A LÖVE program's save folder is `/home/loveos/.local/share/love/<identity>` on ROOTFS, not the games card.

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
  label works for either, but the image has only two partitions. (Since 2026-11-09 the cards are mounted by role, not label:
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
  **Read 2026-11-09** (below): `1900bb3e4b4800000011000000010000`, now in `run_app.sh`.
- `apps/io-checker-love/model.lua` (`LAYOUT`), `apps/io-checker-native/io_checker.c` (`L_*`) and the theme's `PAD` hold these numbers.
- A report that the event lines looked inverted (UP on press, DOWN on release) was not reproduced by reading the code;
  the checker's event lines now say whether SDL reports the button held at that moment, so a real inversion shows.

## First device report (KlamANSI, 2026-11-09, one R36S, r3i6nos booted)

The first report from a unit (`docs/DEVICE-REPORTS.md`; KlamANSI's Start > Device report, a native SDL2 and QuickJS app installed from
`native_deb/` by `deb_watcher.py`). What it settles:

| What | Measured |
|---|---|
| Kernel | Linux 4.4.189 aarch64 (the stock kernel, as captured) |
| CPU | 4 cores, part `0xd04` (Cortex-A35), **1512 MHz** max (not 1.3 GHz as the plans guessed) |
| Memory | 919232 kB total (about 898 MB for Linux), 848420 kB available with KlamANSI running |
| SDL | **2.26.5** (Debian 12's), video driver **KMSDRM**: a native app draws on the panel with the Mali blob from the card |
| Sound | one ALSA card, `rockchip,rk817-codec` (RK817 PMIC codec); SDL opens it with the `alsa` driver, one output device. Neumetik's report: opened at 44100 Hz, 2 channels, 1024 frames, and the callback ran 43 times in a second (as expected). **Silent** on images before 2026-11-09: the codec's `Playback Path` control starts at `OFF` (see "Sound" below) |
| Pad | 1, **"GO-Super Gamepad"**, GUID **`1900bb3e4b4800000011000000010000`**, 17 buttons, 4 axes, 0 hats (the measured layout above) |
| Games card | the flashed card's partition 3, `/dev/mmcblk0p3` on `/roms`, **exFAT**, 125.7 GB; `loveos` can make a folder, write, rename over a file, read back and delete |
| Speed (KlamANSI) | its scripts load in 597 ms; a typed line runs and draws in 38-47 ms; a frame of its screen took 33.6 ms (goal 33); a whole 640 x 480 true-colour paint 4.6 ms. About 20 times the Mac's times (M-series, same QuickJS) |
| Memory (KlamANSI) | peak 47 MB |

- The GUID decodes as bus `0x19` (host), CRC `bb3e` of the name (SDL 2.26 adds it), vendor `0x484b`, product `0x1100`, version `0x0100`.
  `run_app.sh` maps it with and without the CRC (SDL before 2.26 leaves it zero). Whether an app using SDL's gamepad API then sees the
  right buttons is **untested**.
- The games card being `mmcblk0` means the flashed card itself (TF1), with `flash-macos.sh`'s partition 3: no second card was needed.

### The system, from r3i6nos's own report (2026-11-09, image `53f46b3`)

- The device tree's model string is **"Game Console R35S/R36S fix by lxc"** (the stock card's DTB).
- systemd 252 on the 4.4 kernel: **no failed units**. Whether the boot reaches "finished" is not known yet (`systemd-analyze time` gave no
  answer; the report now records why).
- Two mmc hosts, `mmc0` and `mmc1`; the boot card is `mmcblk0` (128.2 GB). The kernel has **squashfs**, loop devices, exfat, vfat and ext4.
- The boot card (exFAT partition 3): **32.4 MB/s** writing, **65.3 MB/s** reading.

### Graphics under LÖVE (NoCoGaMa's device spike, LÖVE 12.0, 2026-11-09)

- LÖVE 12.0.0 runs on the unit with **LuaJIT 2.1.0-beta3, JIT on** (Debian's system LuaJIT, which r3i6nos's builds link).
- **LÖVE 11.5 runs with the JIT off** (its spike report, 2026-11-09), with the same LuaJIT: LÖVE 11.5 calls `jit.off()` itself on arm and arm64
  (`src/modules/love/jitsetup.lua` in its source, citing LuaJIT issue #285: compiled code can only go in a short memory range on arm64, which
  other libraries fill, and performance is unreliable). Expect pure-Lua work about 10 times slower under 11.5 than under 12.0; a game should
  not turn the JIT back on.
- Renderer **OpenGL ES 3.2**, `v1.r13p0-01rel0`, GPU **ARM Mali-G31**: the blob from the user's card works with SDL's KMSDRM and gbm.
- The window is 640x480 fullscreen. **Under LÖVE 12.0 (SDL3)** it draws at 60 fps even with vsync off; **under LÖVE 11.5 (SDL2 2.26.5)** the
  same spike with vsync off drew at 82-88 fps. So the 60 fps ceiling is 12.0's, not the panel's (likely SDL3's KMSDRM waiting for each flip; not checked): under
  12.0 a program's headroom shows in its CPU time, not its fps. (Corrected 2026-11-09: an earlier note put the ceiling on the panel.)
- LÖVE treats the pad as a gamepad (`isGamepad` true), through SDL's own database or `run_app.sh`'s mapping: which one, and whether its buttons
  then match the raw layout, is not known yet.
- A LÖVE program's save folder is `/home/loveos/.local/share/love/<identity>` on ROOTFS, not the games card.

## Sound: the RK817's Playback Path (2026-11-09)

- Seen: every app opened the sound card and played, and nothing came out of the speaker.
- Why: the stock kernel's codec driver (Rockchip BSP 4.4, `rk817_codec.c`) has an enumerated ALSA control **`Playback Path`**
  (`OFF`, `RCV`, `SPK`, `HP`, `HP_NO_MIC`, `BT`, `SPK_HP`, `RING_*`) and powers the speaker amplifier or the headphone driver only
  when it is set. It starts at `OFF`; on Android the audio HAL sets it, on Debian nothing does.
- The stock DTB's sound node is a `simple-audio-card` (`rk817-sound`) with `hp-det-gpio` and widgets `Headphone Jack` / `Mic Jack`,
  so the kernel should show the jack as an input device with `SW_HEADPHONE_INSERT`; the codec does not move the path on its own.
- r3i6nos sets it at boot and follows the jack: `overlay/usr/local/bin/r3i6nos-sound`. By hand over SSH:
  `amixer -c 0 cget name='Playback Path'`, `amixer -c 0 cset name='Playback Path' SPK`.
- **Untested on the unit:** the speaker heard, the jack's input device and its name, the headphones heard. r3i6nos's device report's
  `sound` section records the path and the jack.
- **Heard (2026-11-09, the native IO Checker's report, below):** with the path at `SPK`, the sweep came out of the speaker, so the
  path fix works. Every 440 Hz tone was marked silent at the same level. The jack switch exists ("rockchip,rk817-codec Headphones");
  headphones were not tried.

## A frame presented once is not shown (native SDL2, 2026-11-09)

- Seen: Commander and the native IO Checker started on a black screen; **any key brought the screen up** (the maintainer, one unit).
  Both drew their first screen, presented it once with `SDL_RenderPresent`, and waited for input.
- So on this stack (SDL 2.26.5, KMSDRM, the Mali blob from the stock card) a present does not reach the panel by itself: the
  frame appears when a later one is presented. LÖVE, KlamANSI and the old Input Checker present continually and never showed it.
- Rule for native apps: present each change several times, or every frame. `theme/r3i6nos_ansiapps.h`'s `r3_video_draw` presents
  each change 4 times (`R3_VIDEO_SETTLE`); apps that draw on their own must do the same. Whether 4 is enough is **untested** on the unit.

## The native IO Checker's first report (2026-11-09, one R36S, image `5f4d67c`)

`EASYROMS/IO_Checker/r36s-report-native.txt`: 22 PASS, 4 FAIL, 1 SKIP. The image predates the fix for the black start (above), so the app
ran; only its first screen was missing.

- **Sound:** the output opened (SDL's `alsa` driver, one device, "rockchip,rk817-codec", 44100 Hz) with the path at `SPK`. **The
  sweep (100 Hz to 8 kHz) was heard; the left, right and both 440 Hz tones were marked silent**, and so was the quiet one, all at the
  sweep's level (0.5, faded in and out). The only difference is the frequency, so the likely cause is the speaker: a small one gives
  little below about 500 Hz. Not yet known: where in the sweep it became audible. The checkers' tones are now 1 kHz, with a 440 Hz
  "Low" tone kept to confirm this, and the menu's Sound mixer's test sounds were moved above 340 Hz (`docs/SOUND.md`).
  Anything played on the unit needs its content well above 440 Hz to be heard from the speaker; bass will not come through it.
- **Input:** SDL 2.26.5 sees "GO-Super Gamepad", GUID `1900bb3e4b4800000011000000010000` (the one in `run_app.sh`), 17 buttons, 4 axes,
  0 hats, and **SDL maps it** (`sdl gamepad mapping: yes`). All 17 buttons were pressed with no chatter (no press within 30 ms of a
  release); all four axes reach -1.00 and +1.00 and rest at 0.00. **Power, volume up and volume down arrive as keyboard keys**
  (`power`, `volumeup`, `volumedown`), which is what `r3i6nos-sound` listens for (`KEY_VOLUMEUP`, `KEY_VOLUMEDOWN`; which input
  device sends them is still unread). L1, R1, L2 and R2 (5-8) were pressed once each, consistent with the layout table above.
- **Video:** 640x480 at 60 Hz on KMSDRM, renderer `opengles2` with vsync; the window is 640x480. The motion test was not run (SKIP),
  and how the patterns looked was not said.
- **Files:** the games card (exFAT, `/dev/mmcblk0p3` on `/roms`) passed write, rename over, read and delete; 8 MB written with sync at
  40.0 MB/s, read back byte for byte (the read speed is of a cached file).
- **System:** battery 96% discharging, backlight 51 of 160, 897 MB of memory (813 MB free), the app's peak 47.8 MB.
- **A bug in the report itself:** its System lines were cut to 31 characters (the pad GUID lost its last digit, the board and sound card
  names their ends), because the theme's `R3Item.value` held 32 bytes. Now 64 (`theme/r3i6nos_ansiapps.h`), and the list fits a long
  value to its row.

## r3i6nos's second report (2026-11-09 22:30, one R36S, image `5f4d67c`)

9 PASS, 0 FAIL, 3 SKIP, run from Settings > Device reports 18 minutes after boot.

- **Clock:** the zone `Etc/GMT-8` (UTC+8), set by a person at 22:13; the kernel has an RTC, **`rk808-rtc`**, 1 s from the system clock after
  that (`docs/CLOCK.md`). Whether it keeps time while off is still open.
- **Sound:** the path at `SPK` (of `OFF SPK HP SPK_HP`: this unit's codec lists four items, not the BSP's full set); the jack switch
  "rockchip,rk817-codec Headphones", out.
- **Storage:** the system card (`mmcblk0`, 128.2 GB, exFAT partition 3 on `/roms`) writes 39.2 MB/s and reads 66.5 MB/s (32.4 and 65.3 in
  the first report); no card in TF2. The kernel has exfat, vfat, ext4, squashfs and loop devices.
- **The boot section was SKIP:** `systemctl` and `systemd-analyze` said "Failed to connect to bus". The image has no D-Bus, and without it
  they reach systemd only as root (its private socket); the report runs from Settings as `loveos`. So did the jack follower's state
  ("unknown"). Fixed: `r3i6nos-bootstate` (root, `r3i6nos-bootstate.timer`, a minute after boot and every 5 minutes) writes what they say to
  `/run/r3i6nos/boot-state`, and the report reads it when `systemctl` will not answer. How the first report got "no failed units" is not
  known (it may have been run as root over SSH; the report does not say who ran it).

## Which Playback Path plays, and USB headphones (2026-11-10)

- Seen (the maintainer, Settings > Sound mixer's test music, Volume 30%, Boost 14%): with nothing in the 3.5 mm jack, `OFF` and `SPK` were
  silent and `HP` played from the speaker; once `HP` had played, `SPK` and `SPK_HP` kept the speaker playing. With headphones in the jack,
  `HP` and `SPK_HP` played in them and the speaker was never heard on any path. **USB headphones were silent on every path.**
- So: the jack cuts the speaker off in hardware, and `HP` serves both. `SPK` chosen after `OFF` while a stream runs stays silent (it
  did play when set at boot before any stream, 2026-11-09). `r3i6nos-sound` now sets `HP` always (`docs/SOUND.md`).
- USB: `/etc/asound.conf` played only on card 0. The stock kernel has `snd-usb-audio` built in (`modules.builtin`); whether the port gives
  a USB card at all is not known (the report did not list the cards then; it does now). The output now moves to a USB card while one is
  plugged in. **Untested on the unit.**
- The volume keys work in Neumetik (the maintainer). **The input device that sends them is `odroidgo3-keys`** (NoCoGaMa's device report, 2026-11-11, from `r3i6nos-sound`'s status: `volume keys: odroidgo3-keys`); the same report shows the software mixer controls `Volume` (10 of 0-20) and `Boost` made at boot.

## No sound at all: alsa-lib refused the Boost softvol (2026-11-10)

- Seen (the maintainer, image with `/etc/asound.conf`): every app failed to open sound, "cannot open: ALSA: Couldn't open audio device"
  (SDL's message when `snd_pcm_open` fails).
- Why: Boost's softvol had `min_dB 0.0`; alsa-lib (1.2.8, Debian 12) refuses any softvol whose `min_dB` is not negative
  ("min_dB must be a negative value", Invalid argument), so the `default` device could not be opened by anything. Reproduced in
  Debian 12 under Docker with the image's alsa-lib, the null device in place of the codec.
- Fixed: Boost runs from -3 dB with step 1 at 0 dB and step 0 (silence) never used (`docs/SOUND.md`). `tests/test_asound_conf.py`
  checks every softvol against alsa-lib's rules everywhere, and opens the chain in alsa-lib where `aplay` is installed (CI installs it).
  **Untested on the unit:** sound back, and the Volume and Boost controls made at boot.


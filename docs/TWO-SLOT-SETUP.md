# Two-slot setup: the R36S prepares its own r3i6nos card

**Status (2026-11-11): the console side is written (section 12) and tested on a laptop with fake cards; the maintainer's decisions are in section 8. Nothing here has run on a real R36S: trials T1 to T10 (section 9) are all still to do.** Every "the unit will..." below
is a hypothesis with a trial that can break it (section 9). Reference: `docs/reference-kit/` (why this, what others do).

## 1. Goal

A reader with an R36S, its original card, a second card and any computer ends up with an r3i6nos card, **without
running Mac or Linux scripts**, with the original card **backed up and (as far as the trials show) unchanged**.
The board's own boot files still come from the reader's own card (CLAUDE.md rule 2), but the *device* does the
copying with its own kernel, which reads and writes ext4 and exFAT that Windows and Mac cannot.

## 2. Facts this rests on

| Fact | From |
|---|---|
| The stock U-Boot loads everything from `mmc 1:1` (the left slot's first partition); the string `load mmc 1:1 ... boot.ini; source` is in it. **The unit boots from the left slot (TF1) only.** | `strings` on the captured `bootloader.bin` (2026-11-11) |
| The stock `boot.ini` has the root in one place: `root=UUID='e139ce78-...'` (also in the six `ScreenFiles/Panel N/boot.ini` copies, which, from the boot sequence, U-Boot does not source; inferred, trial T1 confirms). It also has a **built-in panel chooser** (hold R1 + a direction at power-on). | `local_hardware_assets/boot/boot.ini` |
| Boot partition is FAT32 (112 MiB); root ext4 (10.6 GiB); games exFAT (the rest). The bootloader is raw, sector 64 to about 29,500 (14.4 MiB). | `docs/hardware-findings.md`, `info/partitions.txt` |
| The kernel is the stock 4.4.189 with eMMC/SD, ext4, exFAT, vfat, squashfs and loop built in. **An r3i6nos image with no modules and no Mali blob has booted to a text console and a running systemd** (2026-11-07). | `docs/hardware-findings.md` |
| What must come from the card: bootloader (14 MB), boot files (41 MB), modules and firmware (520 MB, 563 `.ko`), the Mali blob (39 MB; the right one is in `/usr/local/lib/aarch64-linux-gnu`). | `local_hardware_assets/` |
| The stock OS has a `/roms/tools` folder bound to `/opt/system/Tools` (ArkOS-style Tools menu). | `info/stock-fstab` |
| The first-boot flow already mounts cards by role and never writes to the data card. | `docs/STORAGE.md` |

## 3. The constraint that shapes everything

The new card cannot boot until its sector 64 holds a bootloader, and a public image cannot contain one (rule 2).
So the first boot of the new card needs either **a host that writes raw sectors** (what `inject-boot.sh` does on a
Mac) or **the original card, in the left slot, doing the booting**. This design takes the second road, because every
computer can do what it needs.

## 4. The flow (the "carrier" boot)

```text
 Computer, any OS                          The R36S
 ---------------------------               ----------------------------------------------------
 A  back up the original card (optional,   
    host imager)                           
 B  write the r3i6nos image to the new     
    card (Raspberry Pi Imager)             
 C  on the ORIGINAL card's BOOT drive,     
    change one value in boot.ini (the web   
    page does it, section 5b)              
                                           D  original card in TF1 (left), new card in TF2 (right); power on
                                              U-Boot + the stock kernel boot from TF1 ... root= is the NEW card
                                           E  r3i6nos "setup mode" starts (text screen):
                                                 1  checks the original card is a genuine RK3326 R36S (rule 3)
                                                 2  backs the original card up to the new card (only if asked)
                                                 3  writes bootloader + boot files to the NEW card
                                                 4  installs modules, firmware and the Mali blob into itself
                                                 5  verifies, writes a report, powers off
 F  take the original card out and keep it  
    put the new card in TF1; power on      G  normal boot: the r3i6nos menu
```

Roles: **original card = source, TF1, only ever read.** **New card = target, TF2 for step E, TF1 forever after.**
The target's own ROOTFS is mounted read-write, so step E4 is plain file copying (no `debugfs`, no temporary copy of
the partition as in `inject-rootfs.sh`).

### Step C, the one edit to the original card

Open `boot.ini` on the original card's BOOT drive in a plain-text editor (Notepad, TextEdit in plain-text mode, any
Linux editor) and replace `e139ce78-9841-40fe-8823-96a304a09859` with `52333669-0000-4000-8000-000000000002` (the
r3i6nos ROOTFS UUID, `scripts/image-layout.sh`). Windows shows the first partition of a card, which is BOOT, so
this works there. **Make a copy named `boot.ini.orig` first**; to undo, put it back. The card's other partitions
are untouched. The UUID on the reader's card may differ from the one captured here (one card seen): the guide says
*the long code after `root=UUID=`*, not the literal.

### The state test (no flag, no switch)

The setup service decides from the disks, so a card never gets stuck in it:

- **Setup mode** if the root disk's sector 64 is empty (no bootloader of its own) **and** another card has a stock layout
  (MBR; partition 1 FAT with `boot.ini` and `Image`; the raw loader at sector 64).
- Otherwise, **normal boot**, and the service records "setup not needed" and exits in milliseconds.
- It is a systemd service ordered **before `r3i6nos-cards.service`**, so in setup mode the original card is never
  mounted read-write as a data card at `/roms2` (it would be, today: `docs/STORAGE.md`, "any other card").

## 5. What the device-side setup does (`r3i6nos-setup`, root, Python standard library)

Conventions of `overlay/usr/local/bin/`: small, no dependencies, paths overridable with `R3I6NOS_*` so tests run on a laptop.

1. **Find the cards.** Root disk (as `r3i6nos-cards` does), the source (the other `mmcblk*` with the stock layout).
2. **Refuse what is not an R36S** (rule 3): Allwinner markers (`eGON.BT0` at sector 16 / `rf3536k4ka`, `rk3326-evb-lp3-v12` DTBs), no
   `Image`, no Rockchip loader signature at sector 64. Say why on the screen and in the report; touch nothing.
3. **Mount the source read-only**: `mount -o ro` for BOOT and `-o ro,noload` for ROOTFS, so the kernel cannot replay
   the journal onto it. Nothing is ever written to the source (rule 7's spirit).
4. **Back it up only if the reader asked** (section 6): **off by default**, so the card is ready as fast as possible. The screen offers it for the first 10 seconds (Y), and the guide says the host backup before step A is the safer one.
4b. **Make the games partition** (decided 2026-11-11): if the target has free space after ROOTFS and no partition 3, add the MBR entry
   (type 0x07, to the end of the disk, as `scripts/add_games_partition.py` does on a host), tell the kernel (`partx -a`),
   `mkfs.exfat -L EASYROMS` (`exfatprogs` is in the image), and make `Love 11.5/`, `Love 12.0/`, `native_deb/`. Only on this card, only
   when partition 3 is absent and the space is empty (all zeros at its start); any other case is skipped and said. It never touches the source card.
5. **Write the target's boot area**: the loader to sector 64 (read back and compared, as `inject-boot.sh` does), the
   boot files to the target's BOOT with every `root=` pointed at `ROOT_UUID` (the same rewrite as `inject-boot.sh`; its
   Python moves into a shared module, and the bash script then calls it, so there is one implementation).
6. **Install the vendor files into the running ROOTFS**: the plan and checks of `scripts/inject_rootfs.py` (which
   libs, symlinks that resolve, `libEGL.so.1`/`libGLESv2.so.2`/`libgbm.so.1` present, ELF `NEEDED` satisfied) applied to
   a mounted directory. Everything is copied to a temporary name and renamed, so a power cut leaves no half-installed blob.
7. **Verify**: hashes of what was written against what was read; the blob's `NEEDED` list against ROOTFS.
8. **Report** (`STATUS key: value`, rule 9) to the target's BOOT, where the reader's computer can read it, `end` last.
9. **Finish** with the swap instruction; power off (not reboot: rebooting would boot the original again).

Screen: it runs before the menu and before the blob exists, so it uses the **kernel console on tty1** (known to work), laid
out as the theme's 64x24 where the console allows, with progress per step and any error shown (rule 12's spirit for
a pre-theme screen). Input is the pad's raw buttons read from `/dev/input/event*` with `struct` (A to go on, B to cancel);
no library needed. A hot-plug toast is not shown here (rule 13 applies to ports; this is the system).

## 5b. The web page that replaces a host tool (decided direction, 2026-11-11)

With the carrier boot, the only thing a computer has to do beyond writing the image is the one `boot.ini` edit, which needs no raw disk
access. So **a static web page can do it; no program of ours is installed on any OS, nothing is signed, nothing is distributed**
beyond a page on the project's site. (A browser cannot write or read a raw disk, which is why it could not have replaced the old step 3.)

- **What it does, all in the browser, nothing uploaded** (a CSP with `connect-src 'none'`; no analytics, rule 6):
  1. The reader drops `boot.ini` onto the page (every browser), or, in Chrome or Edge, picks the card's BOOT drive.
  2. It checks the file looks like a stock R36S `boot.ini` (`load mmc 1:1`, exactly one `root=UUID=`), shows the code it found, and says
     "this does not look like an R36S card, stop" for anything else (rule 3; with the folder picker it also lists the DTB names and
     warns on the Allwinner-clone markers).
  3. It hands back the patched file **and** `boot.ini.orig` (the original, byte for byte) with a hash of each, plus a plain "put both on the
     card, replace the file" step. With the folder picker in Chrome or Edge it writes them to the drive itself, after one permission prompt.
  4. A second tab does the undo: drop `boot.ini.orig`, get the original back.
- **Limits, stated honestly:** the folder picker exists only in Chromium browsers, and Chrome may refuse a drive root ("contains system files";
  the web docs list protected folders without naming removable drives, so this is **untested**), so **the drop-a-file route is the one that must
  work everywhere** and the folder route is a convenience. Firefox and Safari get the drop route only.
- **Rules it follows:** an installable PWA from the first commit, no service worker (global rule); the page and its tests in this repo
  (in the ansiapps-site repo, `r3i6nos/public/bootdisk/`, served at **r3i6nos.ansiapps.com/bootdisk** by Cloudflare Pages with that site's strict CSP; moved there 2026-11-11); the patching function is plain JavaScript with Node tests in that repo (`npm run test:bootdisk`), and the
  same function's behaviour is checked against `inject-boot.sh`'s rewrite so the two cannot drift.
- **Still open:** Safari and Firefox on a Mac or PC are the likeliest readers, so the drop route needs its own try with a real card reader (trial T9).

## 5c. The guide is the app (revised 2026-11-11: a hand-holding wizard, not a screenshot page)

Instead of a page of screenshots, **the web app itself walks the reader through every step** (the ansiapps-site repo, `r3i6nos/public/bootdisk/`; built as a test version; its roadmap is in that repo's `ROADMAP.md`):

- **One step at a time, always named:** "Step 7 of 10 · Original card", a progress bar and a row of numbered dots (done ones tick, you can go back,
  you cannot skip ahead), and the step's title gets keyboard focus when it changes.
- **Each step has the same shape:** a picture, one plain sentence of why, a short list of things to do (the reader **ticks each one**, and Next
  stays off until all are ticked), a green "What you should see" box, and a "Something not right?" fold-out with the likely causes and a Discord link.
- **Pictures are drawn in code** (`visuals.js` there: the console, the cards in orange for the ORIGINAL and green for the NEW, the slots labelled
  LEFT · TF1 and RIGHT · TF2, Raspberry Pi Imager's three choices, the BOOT drive with `boot.ini` highlighted, the setup screen), so
  they never go stale and need no screenshot upkeep. Real photos of the console's slots can be added later; they are not required.
- **It knows the computer:** it guesses Windows, macOS or Linux from the browser and lets the reader change it; the tool names, buttons, commands
  (a checksum command to copy) and eject steps follow that choice.
- **The one host step is built in** (step 7, section 5b): drop `boot.ini` or pick the drive, see ✓ lines for what was checked, download or write the
  two files, with the undo written next to it.
- **It keeps its place** (localStorage, wrapped so it works when blocked) and "Start over" clears it.
- **Honest about itself:** a banner says it is a test version, and the steps for the setup that runs on the console are marked as the plan until
  that software exists.

Tests (`r3i6nos/tests/bootdisk-wizard.test.mjs` there) walk all ten steps with a hand-made DOM; **what they cannot check is how it looks or behaves in a real browser**
(this was not rendered: headless Chrome would not start in the build sandbox). Trial T9 does that, on three systems, with first-time readers.

## 6. The backup

Common advice is to back up the original card because cards fail and because each model's boot files differ. Two routes:

- **Host backup (before step A):** USB Image Tool "Backup" or Win32 Disk Imager "Read" (Windows), `dd` or Disk Utility
  (Mac), `dd` or Disks (Linux). Needs space for the card's size unless "only allocated partitions" is used. The guide offers
  it as the safest choice and the device route as the easy one.
- **On-device (step E2):** a raw read of the original card from sector 0 to the end of partition 2
  (**10.7 GiB on the captured card**; partition 3 is the vendor's games and is copied as files instead, or skipped), compressed
  while reading, with a SHA-256 of the raw stream, written as `Backup/stock-<date>-<id>.img.gz` and `.sha256`.
  A restore is `gunzip | dd` (and `docs/` says so in plain words). The backup is verified by a second pass (decompress and hash).
  Time on the unit is unknown (SD read 65 MB/s measured; gzip speed on the Cortex-A35 is not): the trial measures it.

**Where it goes** is the open question (section 8): the target card's games partition (exFAT, which Windows can read to copy the
backup off), and that needs partition 3 to exist on the target, which today only `flash-macos.sh` creates.

## 7. Files this would add or change

| Path | Change |
|---|---|
| `overlay/usr/local/bin/r3i6nos-setup` | new; the flow in section 5 |
| `overlay/etc/systemd/system/r3i6nos-setup.service` | new; `Before=r3i6nos-cards.service loveos.service`, oneshot, own console |
| `scripts/boot_files.py` (shared) | new; the `root=` rewrite and boot-file staging from `inject-boot.sh`, used by it and by `r3i6nos-setup` |
| `scripts/inject_rootfs.py` | its `plan` and `verify` made callable on a mounted directory |
| `overlay/usr/local/bin/r3i6nos-cards` | in setup mode, never mount the source card |
| `overlay/usr/local/bin/r3i6nos-report` | a `setup` section (rule 9) |
| `scripts/configure_rootfs.sh` | enable the service; leave `/opt/libmali` and modules absent in the public image |
| **ansiapps-site repo:** `r3i6nos/public/bootdisk/` (`index.html`, `wizard.js`, `visuals.js`, `boot-patch.js`, `app.css`, manifest, icons), `scripts/bootdisk-icons.mjs`, `r3i6nos/tests/bootdisk-*.test.mjs`, headers in `r3i6nos/public/_headers` | **built as a test version and handed over (2026-11-11)**; sections 5b and 5c; build and testing phases in that repo's `ROADMAP.md` |
| `tests/test_bootdisk_parity.py` (this repo) | new; checks the web app's pattern, `ROOT_UUID` and rewrite against `inject-boot.sh` and `image-layout.sh`; skipped, and says so, without `../ansiapps-site` |
| `scripts/add_games_partition.py` | its partition-entry logic shared with `r3i6nos-setup` (4b) |
| `docs/making-a-card.md`, `README.md` | the guide of section 4, per platform |
| `tests/test_r3i6nos_setup.py` | fake cards: files for the partitions and a sparse "disk"; refusals; power-cut points |

Tests on a laptop can cover the decisions (state test, refusal rules, plan, rewrite, verify, atomic install, a fake
disk) with no hardware, as `tests/test_r3i6nos_cards.py` does. They cannot cover section 9.

## 8. Decisions (maintainer, 2026-11-11)

1. **Partition 3 on the new card: yes.** The setup service may create and format the exFAT games partition, but **only on the card it is
   setting up, only when it has no partition 3 and the space after ROOTFS is empty** (CLAUDE.md rule 7 now says so; `flash-macos.sh` keeps
   doing the same on a Mac). Never on the source card, never on a card with a partition 3.
2. **The host step is a web page, not a program per OS** (section 5b): no signing, no installer, no distribution. If readers' tests show the page
   cannot do the edit on some system, a tool is reconsidered then.
3. **Backup is off by default**, so the new card is ready as fast as possible; it is offered on the setup screen and recommended in the guide, with the host
   backup as the safer route (section 6).
4. **A guide page**, then revised the same day to **the app itself hand-holding through every step, with drawn pictures** (section 5c).

## 9. Trial plan

Ordered cheapest and safest first; **stop at the first failure and fix before going on.** Each trial names what it proves
and what to look at. Use a **spare** card as the "original" until T6; T6 uses a real one with the host backup made first.
Every trial's output also goes into the device report (rule 9) and `docs/hardware-findings.md`.

| # | Trial | Proves | Pass when |
|---|---|---|---|
| T0 | On the Mac: hash the captured stock card (first 11 GiB) and a test image; check, with the captured files, that rewriting the one `root=` and `ROOT_UUID` yields a `boot.ini` U-Boot would parse; check the plan against `local_hardware_assets/rootfs` | the data and the edit are right, offline | scripted, in `scripts/check.sh` |
| T1 | **The carrier boot, by hand.** Image the spare card (Imager), edit `boot.ini` on a clone of the stock card, put the clone in TF1 and the r3i6nos card in TF2, boot. Over SSH or the console, read `/proc/cmdline`, `lsblk`, `mount` | the stock U-Boot and kernel boot a rootfs on TF2; whether initramfs finds it; which `mmcblk` is which | r3i6nos is up with `/` on `mmcblk1p2`, and `systemctl` shows no new failures |
| T2 | **Is the original card left alone?** Hash the stock clone's first 11 GiB before T1 and after (`boot.ini` aside) | U-Boot and the initramfs write nothing else | hashes equal except `boot.ini` (and U-Boot environment if the panel chooser was used) |
| T3 | **Read the source read-only** from the running unit: mount BOOT `ro` and ROOTFS `ro,noload`, read modules and the blob, hash them against the host's captured copies | ext4 reading on the unit, and that "noload" leaves the card byte-identical (hash again) | file hashes equal, card hash unchanged |
| T4 | **Install into the running ROOTFS** by hand: copy modules, firmware and the blob; run a LÖVE app on the target while still in carrier mode | the blob and modules work from a copy made live, and what needs a reboot | a LÖVE app draws |
| T5 | **Write the target's boot area** (loader and boot files), power off, swap cards, boot the target **alone** in TF1 | the target boots with no original card present | the menu appears, `r3i6nos-report` has no FAIL |
| T6 | **The whole thing with `r3i6nos-setup`** (once written), twice: spare card, then a real original with its host backup beforehand; time every step, record the card hashes before and after | the flow, the timing, the refusals | as T5, plus a report with every step PASS and the original's hashes unchanged |
| T7 | **Failure injection**: power off at each step; pull the original card; a card too small; a non-R36S card layout (a captured Allwinner-style one if available); both cards swapped | no half-installed card, no write to the original, clear messages | each leaves a card that can simply be redone |
| T8 | **The backup**: time and size of the on-device backup and its verification on a real 128 GB original; a restore with `dd` onto a spare and boot it | the backup is usable and its time is acceptable | the restored card boots the stock OS |
| T9 | **Readers.** Three people who have not seen the project follow the written guide on Windows, Mac and Linux, watched, not helped | the guide, the one edit, the swap | each reaches the menu; note every stop |
| T10 | **Other boards**: a different R36S revision or panel, if the maintainer or a Discord member has one | "different models need different files" is handled by copying the reader's own | boots; panel chooser still works |

What each trial must leave behind: the exact command or the screen it used, the hashes, and the device report, with
`docs/hardware-findings.md` and both roadmaps updated (CLAUDE.md rule 9).

## 10. Risks

- **The one edit** may be done wrongly. Mitigation: `boot.ini.orig`, a guide with the exact find-and-replace, a check by
  the setup that says "this card did not boot the new one" (it would not be running at all, so the guide's last line is what to do
  when the original's own OS starts instead: redo C).
- **The stock kernel or initramfs may write to the original** (fsck, plymouth, panel env). T2 measures it.
- **Another model's U-Boot** may not hard-code `mmc 1:1`, or its `boot.ini` may differ. The guide is "the long code after
  `root=UUID=`" and T10 looks at a second board.
- **Backup time and space.** A 128 GB original is 10.7 GiB of boot-critical data; a small target card cannot hold it.
  The setup checks space first and says so; a host backup is the fallback.
- **Pre-blob screen**: the kernel console may show the vendor's splash or a rotated panel (`fbcon=rotate:0` is in the stock line).
  T1 looks.
- **Licensing**: the installed blob and modules stay on the reader's cards (rule 2); the setup never uploads or exports
  them, and the backup is theirs. The backup file must not be offered for sharing (docs say so).
- **A second image to maintain**: none; the setup lives in the one public image and exits at once when not needed.

## 11. What is not recommended

- A program of ours per OS (the page does the one host step).
- Booting from TF2 (the U-Boot looks at slot 1 only).
- Editing the original's `boot.ini` for any reason but the root.
- A host script that rewrites the original card's other files.

## 12. What was built (2026-11-11)

`overlay/usr/local/bin/r3i6nos-setup` (Python standard library, root, about 1,200 lines), `overlay/etc/systemd/system/r3i6nos-setup.service` (enabled in
every image by `configure_rootfs.sh`; ordered before `r3i6nos-cards.service` and `loveos.service`), a change to `r3i6nos-cards` (it never mounts the disk
named in `/run/r3i6nos/setup-source`), a `setup` section in `r3i6nos-report`, and checks in `check_image.sh`, `tests/test_configure_rootfs.sh` and CI
(`build.yml`: `r3i6nos-setup` joins the stdlib-import check; `partx`, `mount`, `umount`, `blkid` and `mkfs.exfat` must be in the rootfs).
Tests: `tests/test_r3i6nos_setup.py` (48), plus new cases in `test_r3i6nos_cards.py` and `test_r3i6nos_report.py`. They run on a Mac and in Debian 12 under Docker.

How it follows section 5, and where it differs:

- **State test:** as designed (`r3i6nos-setup detect` prints what it would do). A normal boot reads 4 KiB, writes a line to `/run/r3i6nos/setup-state` and exits.
- **Checks first, writes after:** the original is read-only mounted (`ro`, ext4 `ro,noload`); the genuine check (Allwinner `eGON.BT0` in the first 16 MiB, the clone board
  files, a kernel `Image`, an `rk3326-*.dtb`, a `boot.ini`), the new card's disk signature, the room for the drivers and the Mali blob's required libraries are all
  checked **before anything is written to the new card**. A refusal leaves both cards as they were and the reason on the screen and in the report.
- **No shared `boot_files.py`:** the device cannot import from `scripts/`, so the script is self-contained and **tests pin it to the host scripts**: the `root=` pattern
  and `ROOT_UUID` to `inject-boot.sh` and `image-layout.sh`, the games-partition entry to `add_games_partition.py` (same alignment, minimum size, byte for byte), the ELF
  reader to `inject_rootfs.py`; `tests/test_bootdisk_parity.py` pins the web guide's patch to the same.
- **Lookups inside the original are chroot-style** (`resolve_in`): an absolute symlink such as `/lib -> /usr/lib` is followed inside the card, never onto the running system.
- **BOOT is mounted by device, not by label** (new, found while writing it): `/etc/fstab` mounts `LABEL=BOOT` at `/boot`, and the original's boot partition may have the
  same label. In a setup boot it could be what is mounted at `/boot`, read-write, and the new card's boot files would be written onto the original. `ensure_boot_mounted`
  unmounts the original if it is there, mounts this card's partition 1 explicitly, and stops if some third device is there. **Whether the original is mounted at `/boot` in
  practice is untested** (trial T1 reads `/proc/mounts`).
- **Installing the drivers:** each piece (modules, firmware, `/opt/libmali`) is copied to `<name>.r3new`, renamed into place (the old one aside, removed after) and compared
  with the original by size and SHA-256. A power cut leaves either the old state or a finished piece, and a re-run replaces leftovers.
- **The backup is off by default, as decided, and is only offered when this card has room for it** (maintainer, 2026-11-11). Before asking, `backup_room` measures the room: the free
  space of an existing games partition (mounted read-only for a moment), or what `make_games` would give a new one after ROOTFS. A backup is guessed at a third of the original's system
  area plus 256 MB. If it does not fit, nothing is asked, no ten seconds are spent, the screen says why and the report has `SKIP backup: not offered: ...`; forcing it (a pressed button, or
  `R3I6NOS_SETUP_BACKUP=1`) cannot override that. If it fits, for 10 seconds the screen asks, with a **countdown bar**: **any button** (not a particular one: the pad's raw codes are only
  partly known) turns it on, read from `/dev/input/event*` and `js*`; volume and power keys do not count. It is made on the new card's games partition
  (`Backup/stock-<date>-<id>.img.gz` with a `.sha256`), from sector 0 to the end of the original's system partitions (not its exFAT games partition), read back and compared, and removed
  if it does not match.
- **The games partition** is made with `partx -a`, `mkfs.exfat -L EASYROMS` and the three folders, only when partition 3 is absent and the first MiB after ROOTFS is zero.
- **The screen** is the kernel console on tty1, 64 columns, redrawn from the top (nothing scrolls): the step list, a message (up to three lines), **two progress bars that are always there**
  (*This step*, and *Overall*, where each step is worth a share of the whole: checks 4, this card 2, boot area 6, drivers 40, games area 6, finish 1, and the backup 60 only if chosen), and a
  tip to keep the console charged or plugged in with the battery's level; a low battery (under 25% and not charging) is a loud line and a `FAIL battery` line in the report, but does not stop
  the setup. Every phase reports a bar: the checks and the card steps by stage, the boot files by file, the drivers by **bytes** (each byte is copied once and read back once), the games
  area by stage, the backup by bytes read and again by bytes checked, and the wait for a button by time left. It redraws only when the words or a whole percent change. On success it says to swap the cards and
  powers off (`systemctl --no-block poweroff`, as `r3i6nos-poweroff` does); **on a failure it stays on the screen** (the unit never finishes, so the menu cannot start over it)
  until a button is pressed, then powers off.
- **The report** is `r3i6nos-setup-report.txt` on BOOT, in the report grammar, `end` last; `r3i6nos-report`'s `setup` section reads the record, this boot's state and that file.

- **The bootloader is the last write** (changed 2026-11-11 after this was thought through for T1): a card with a bootloader at sector 64 is a finished card to the next boot, so if the loader
  were written first, a power cut afterwards would leave a card that looks done and has no drivers. Now the boot files, the drivers and the games partition come first and the loader goes in
  last (the step *Make the card bootable*); a failure or power cut at any earlier point leaves a setup card and the setup is simply run again (two tests prove it).
- **The report records what a trial needs to read** with no keyboard or screen capture: the kernel, the kernel command line, seconds since power on, memory available at the start and after the
  drivers, what was mounted at `/boot` before the setup (the label-clash question), the battery, and how long each step took.

**Untested on a unit, so every one a trial must answer:** the battery's sysfs name (`class/power_supply/*/type` = Battery) and that the percent and status read there; that the stock U-Boot and kernel start r3i6nos from TF2 (T1); what is mounted at `/boot`; that `mount -o ro,noload`
leaves the original byte-identical (T2, T3); the real copy time and memory of about 520 MB of modules and firmware; `partx -a` and `mkfs.exfat` on a card that is also the root disk;
that the console shows the screen (and not the vendor's splash) and takes a button; a pad's presses as `event*` or `js*`; the whole flow (T6); a power cut at each step (T7); the backup's
speed (T8). **Known limits:** a genuine R36S whose board file is not named `rk3326-*.dtb` would be refused (T10); `needed` libraries missing for the Mali blob are a FAIL line, not a stop.


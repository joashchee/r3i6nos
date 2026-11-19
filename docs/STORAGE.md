# Storage: the system card and the data card

The R36S has two microSD slots. r3i6nos gives each card one job, so the boot card almost never has to come out:

| | System card (TF1) | Data card (TF2) |
|---|---|---|
| Holds | BOOT, ROOTFS, and its partition 3 (exFAT, made by `flash-macos.sh`) | one partition a computer can read: exFAT (recommended), FAT32 or ext4 |
| Mounted at | `/roms` | `/roms2` (the name ArkOS uses for a second card) |
| Found by | being partition 3 of the disk the root filesystem is on | being any other mmc card, whatever its label |
| Taken out | almost never: only to reflash | freely (after Settings > Storage > Eject, step 3) |

Everything a person adds from day to day goes on the data card, or over SSH (`docs/NETWORK.md`). Both cards have the same folders:
`Love 11.5/`, `Love 12.0/`, `native_deb/`, and one `<App>/` folder per app for its data.

Rule 7 holds on both cards: boot only mounts them, and r3i6nos never formats, repartitions or relabels either card **by itself**. The one way it formats the data card is a person choosing **Settings > Data card > Format** (below).

## Format the data card (Settings > Data card, 2026-11-16)

A card that is new, or in a filesystem the unit cannot open, can be made the data card from the unit, with no computer:

- **Status** shows what `r3i6nos-cards` found (device, filesystem, where it is open).
- **Format as exFAT** (recommended: a Mac, Windows or Linux computer reads it) or **Format as ext4** (Linux only). Each asks "Erase the data card?" first, and the answer starts on **No**.
- FAT32 is not offered: the image has no `mkfs.fat` (`dosfstools`). A card a computer formatted as FAT32 is still opened as before.

How it works (`overlay/usr/local/bin/r3i6nos-format`, the same two-file protocol as `r3i6nos-clock`: the menu writes `/run/loveos/format/request`, `r3i6nos-format.path` starts the root service, the answer is `/run/r3i6nos-format/result` with `ok=` and `reboot=`):

1. **Which card:** every mmc disk except the one the root filesystem is on (and except a setup boot's original card). It refuses when it cannot tell which disk is the root one, when no other card is in, or when more than one is. The system card is never a target, and a test checks that nothing is written to it.
2. **Unmount** everything of that card (`/roms2`, or `/roms` when the boot card has no partition 3). If a mount is busy it stops, writes nothing and says "Nothing was changed". There is no lazy or forced unmount.
3. **Erase and partition:** the first 4 MiB and the last 1 MiB are zeroed (the old table, filesystem start and any GPT backup), then one MBR partition is written from 4 MiB to the end (type 07 for exFAT, 83 for ext4; at most about 2 TiB), with a new random disk signature.
4. **Format** it (`mkfs.exfat` or `mkfs.ext4`, label `R3I6NOSDATA`; ext4's root is owned by `loveos`, uid 1000), make `Love 11.5`, `Love 12.0` and `native_deb` through a temporary mount, then **mount by role** with `r3i6nos-cards mount`, which opens it at `/roms2`. The menu reads its game list again. It also writes `.r3i6nos-card` (a random id and the date) at the card's root, the identity `r3i6nos-appdata` keeps "don't ask again" choices against (`docs/APP-DATA.md`), so a new card has an id from the start.

**When a restart is needed.** After writing the table the helper asks the kernel to re-read it and checks that partition 1 has exactly the start and size just written (an old partition 1 is not taken for the new one). If the kernel keeps the old table, or the format worked but the card did not open, the answer carries `reboot=1`: a dialog explains it and asks **Restart now?** (starts on No, and the explanation stays on screen). Yes asks the session for `system<TAB>reboot`; the session leaves a `reboot` flag and exits, and `r3i6nos-poweroff` runs `systemctl --no-block reboot`, as Shutdown does `poweroff`. In the first case a marker `/var/lib/r3i6nos/format-pending` (disk, signature, filesystem) is left, and at the next boot `r3i6nos-format finish` (an `ExecStartPre` of `r3i6nos-cards.service`, never failing the boot) formats the partition **only if** the marker is valid, the disk is not the boot disk, the card's signature, partition type and geometry still match, and the partition has no filesystem. The marker is removed first: one try.

**Untested on the unit.** What to find out with a real card in TF2: whether the kernel re-reads the table at once (then no restart is asked) or not (then the dialog shows), how long `mkfs` takes on a big card (the menu waits up to 25 minutes), and that the result mounts and a computer can read the exFAT card.

## Saved settings (2026-11-19)

The menu's own settings (the saved Wi-Fi network and password, the time zone, the clock, the sound levels, the battery history) are kept on the data card too, in `r3i6nos/settings/`, and brought back onto a fresh system card at its first boot: `docs/SAVED-SETTINGS.md`.

## Why not labels

Before this, `/etc/fstab` mounted `LABEL=EASYROMS` at `/roms`, and `flash-macos.sh` gives the system card's partition 3 that label, while
`docs/making-a-card.md` told people to label a second card in TF2 the same. With both in, which one became `/roms` was a race. Roles are
now worked out from the hardware, so a card's label does not matter and no one's card has to be relabelled.

## The plan, in order

1. **Mount by role, and measure what the unit can do** (done 2026-11-09, below). Untested on the R36S.
2. **Both cards in the menu, and each app's data on its own card.** The list shows a tag per entry (`S` system, `D` data); the same name on
   both is listed twice, never merged. `run_app.sh` gives the app `R3I6NOS_APP_DATA`, the `<App>/` folder on the card it was started from,
   and the ports (KlamANSI, NoCoGaMa, the tools, the device reports) use it instead of `/roms/<App>`. An app whose card is out is greyed
   ("Needs the data card"), never hidden or uninstalled.
3. **Settings > Storage:** both cards' filesystem and free space and the apps on each; **Eject data card** (sync, unmount, "safe to remove");
   **Prepare data card** (makes the folders, never formats); the install target (step 4).
4. **Installing packages, with a choice of card.** `deb_watcher.py` stops installing every `.deb` it finds: a new one is listed as
   waiting, and the menu asks "Install on: System card / Data card / Not now" with each card's free space (a root helper does it, started by
   a path unit, as Settings > Network does). A setting turns the question off: Ask / System card / Data card. Each package becomes one
   read-only **squashfs image**, `r3i6nos/packages/<name>.sqfs`, on the chosen card's big partition, mounted when the app or tool starts:
   exFAT has no symlinks or Unix permissions (a `.deb`'s `libfoo.so -> libfoo.so.1` cannot be extracted onto it), ROOTFS is only about
   2.3 GB, and one file per package makes removing or moving an app one file. Rootfs cost: `squashfs-tools` (about 1 MB). If the kernel has
   no squashfs, the fallback is one ext4 image file per package. A tool on the data card keeps its wrapper in `/opt/loveos/bin`, which says
   which card to insert when it is out. The image's own default apps stay in ROOTFS.
5. **Moving an app between cards, and swapping the data card while the unit is on** (only if the slot reports insertion and removal).

## Step 1: mounting by role (2026-11-09)

`overlay/usr/local/bin/r3i6nos-cards` (Python stdlib, root) runs once at boot from `r3i6nos-cards.service`, before the session:

- **The boot disk** is the disk holding the root filesystem: `stat /` gives its device number, `/sys/dev/block/MAJ:MIN` names the partition,
  and its parent is the disk (`mmcblk0` on the unit the first device report came from).
- **The system partition** is that disk's partition 3. It is mounted at `/roms`.
- **The data card** is any other `mmcblk*` disk that has a size (eMMC `boot`/`rpmb` areas are skipped): its first partition with a filesystem
  `blkid` knows, or the whole disk when there is no partition table. It is mounted at `/roms2`. If no other disk is there yet, it looks again
  for up to a second (`R3I6NOS_CARD_WAIT`), in case the second slot is slower to come up.
- **No partition 3** (a card flashed with `--no-games`): the data card is mounted at `/roms` instead, as the old two-card setup did.
- exFAT and FAT are mounted `uid=1000,gid=1000,umask=022,noatime` (as before); ext4 `noatime`, with its own owners. Any other filesystem is
  left unmounted and said so.
- What it found goes to **`/run/r3i6nos/cards`** (`BOOT_DISK=`, `SYSTEM_DEV=`, `SYSTEM_FS=`, `SYSTEM_AT=`, `DATA_DEV=`, `DATA_FS=`, `DATA_AT=`),
  for Settings > About, the device report and later steps. `r3i6nos-cards status` prints it.
- It never fails the boot: a card it cannot mount is written down and skipped, and the session starts either way.
- The fstab no longer has a games-card line. `loveos.service` waits for `r3i6nos-cards.service`.

In this step the menu also lists the LÖVE games on `/roms2` (after the system card's: a game with the same file name on both shows once, from
the system card, until step 2). **`.deb` files on the data card are not installed yet** (step 4): `deb_watcher.py` still reads only
`/roms/native_deb`, because it removes an app whose `.deb` has gone, and it cannot yet tell a removed `.deb` from a card that is out.

### The system device report

What step 1 cannot know without the unit, the r3i6nos device report measures: **Settings > Device reports > r3i6nos (this system)**, or
`r3i6nos-report` over SSH. It writes `r3i6nos/r36s-report.txt` on the data card if one is mounted (so the boot card can stay in), else on the
system card. Its **storage** section:

- every `mmcblk*` disk with its size and whether it is the boot disk; each slot's host (`/sys/class/mmc_host`);
- what `r3i6nos-cards` mounted, and each mount's filesystem, device and free space;
- the filesystems the kernel has (`/proc/filesystems`): exfat, vfat, ext4, **squashfs** (step 4 needs it), and whether loop devices exist;
- write and read speed on each mounted card (16 MB, synced, then deleted), and the write, rename-over, read and delete check on each.

**Not answered by a report:** whether the second slot reports a card being put in or taken out while the unit is on (step 5). To find out
by hand: with the unit on, insert a card in TF2, then run `ls /sys/block` over SSH.

## On the R36S (2026-11-09, image `53f46b3`, r3i6nos's own report)

- **The system card works by role:** the boot disk is `mmcblk0` (128.2 GB); its partition 3 is mounted at `/roms` (exFAT, 125.7 GB), and the
  menu and About see it. Writing 32.4 MB/s, reading 65.3 MB/s (16 MB, synced, from the card not the cache).
- **The kernel has what step 4 needs:** squashfs and loop devices, and exfat, vfat and ext4.
- The unit has two mmc hosts, `mmc0` and `mmc1`.
- **Untested:** the data card. No card was in TF2 for that report. To find out: put a card in TF2, boot, run the report (what it shows up as,
  whether it is ready when the service runs, its speed); then take it out, boot, put it back with the unit on, and run the report again
  (it lists the disks the kernel sees, so it shows whether the slot reports a card put in while on: step 5).

## A setup boot (docs/TWO-SLOT-SETUP.md)

When the original card is in TF1 and the new card in TF2, the new card is the root disk and the original is the *other* card. `r3i6nos-setup` runs first, names the original in `/run/r3i6nos/setup-source`, and `r3i6nos-cards` then never mounts it as a data card (it is only ever read, rule 7). After the swap the new card boots from TF1 on its own and this section does not apply.

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

Rule 7 holds on both cards: r3i6nos never formats, repartitions or relabels either card, and boot only mounts them.

## Why not labels

Before this, `/etc/fstab` mounted `LABEL=EASYROMS` at `/roms`, and `flash-macos.sh` gives the system card's partition 3 that label, while
`docs/making-a-card.md` told people to label a second card in TF2 the same. With both in, which one became `/roms` was a race. Roles are
now worked out from the hardware, so a card's label does not matter and no one's card has to be relabelled.

## The plan, in order

1. **Mount by role, and measure what the unit can do** (done 2026-11-10, below). Untested on the R36S.
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

## Step 1: mounting by role (2026-11-10)

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

## Untested on the R36S

All of step 1: what TF2 shows up as (probably `mmcblk1`), whether it is ready when the service runs, the mounts themselves, and the report.

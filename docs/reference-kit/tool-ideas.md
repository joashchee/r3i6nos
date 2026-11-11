# Could custom tools simplify the steps?

Our install today (README, `docs/making-a-card.md`): capture the stock card, get the image, inject the boot files,
inject the modules and Mali blob, flash, put in TF1. Steps 1, 3 and 4 are why Windows has no path. Everything below
is **a proposal; none of it is built or tried.**

## What forces the hard step

CLAUDE.md rule 2: the bootloader, kernel, DTBs, modules and `libmali` are the vendor's and come from the reader's own
card. Facts from `docs/hardware-findings.md` that shape the tools:

| Needed from the stock card | Where it is | A host tool needs |
|---|---|---|
| Bootloader | raw sectors 64 to about 29,500 | raw read of the device |
| Kernel `Image`, `uInitrd`, DTBs, `boot.ini` | partition 1, **FAT32** | read a FAT volume (every OS can) |
| Kernel modules, firmware, Mali blob | partition 2, **ext4** | an ext4 reader (**Windows and Mac have none**) |
| and then to write them into | the image's ROOTFS, ext4 | an ext4 *writer* |

The last two rows are what needs `e2fsprogs`, `debugfs` and Mac or Linux.

## Idea 1: move the ext4 work onto the device (recommended to try first)

**Developed into a design and trial plan: [`docs/TWO-SLOT-SETUP.md`](../TWO-SLOT-SETUP.md).** The version below is the first sketch; the design
replaces its "host copies the bootloader" step with a *carrier boot* (the original card in TF1 boots r3i6nos from TF2), because a public image
cannot hold a bootloader and Windows has no raw-write tool for beginners.

The R36S's own kernel reads and writes ext4. So the host only has to do what every OS can:

1. Copy the bootloader sectors and the FAT boot files from the stock card into the image (raw read, FAT copy).
2. Flash. Put the new card in TF1 **and the original card in TF2**.
3. On first boot, a root service finds the modules, firmware and Mali blob on the stock card's ext4 partition (read-only),
   installs them into ROOTFS, and reboots. The reader never sees ext4.

Costs and unknowns: graphics need the blob, so the first boot is text-mode progress (a splash on the console, rule 12);
the modules must be in place before anything that needs them (what is built into the 4.4 kernel versus a module is
unmeasured); the stock card must stay in TF2 for that boot, which the instructions have to say. Rule 7 is untouched: the
stock card is only read. Everything stays on the reader's own cards (rule 2).

## Idea 2: one native tool per platform

A single static program (**Go or Rust**, one codebase, three builds) named `r3i6nos-flasher`:

- **Refuses what it should:** only removable, card-sized disks; never the system disk; shows the card's name and size
  and makes the reader confirm by typing it (as `flash-macos.sh` does).
- **Does:** download from a release URL, verify SHA-256, decompress (`gz`, `xz`), read the bootloader and FAT files
  from the stock card (Idea 1) or, if the ext4 reader is included, do the whole injection; write the image with a
  read-back verify; clear a stale read-only flag; eject.
- **Per OS:** Windows `\\.\PhysicalDriveN` with a UAC prompt; macOS `/dev/rdiskN` through an authorization prompt;
  Linux `/dev/sdX` via polkit or `sudo`.
- **Costs:** three signed builds. The maintainer's own rule is that macOS software is signed and notarized on the
  maintainer's Mac and never on CI; a Windows `.exe` unsigned is what antivirus removed for ArkOS4Clone, so Windows
  needs a code-signing certificate or the tool is a support burden. A GUI per OS is more work than a terminal prompt
  and is what non-technical readers need.

## Idea 3: publish an Imager repository (cheapest; do alongside either)

Raspberry Pi Imager reads a JSON catalogue (`--repo <url or file>`; Imager 2.0 added a switch in its interface, older
versions are command line only, per its manual and Raspberry Pi's guide). One hosted file listing r3i6nos images, with
their SHA-256 and sizes, gives every platform one familiar tool with download, verify, write and read-back. No code of
ours runs on the reader's computer. Limits: it cannot do the stock-card step, and it must be tried with our images first.

## Idea 4: ideas considered and set aside

- **A web page that flashes:** a browser cannot write a raw disk, so no.
- **A boot-time panel picker, as R3XS has:** not needed. We copy the reader's own DTB, so there is no panel to choose
  (a real simplification over every other firmware). Worth it only if we ever ship our own DTBs.
- **A Windows batch script around WSL:** USB card readers are not visible to WSL without extra steps; not for beginners.
- **ISO-style "installer on the card" like ROCKNIX x86:** the R36S has no internal storage to install to.

## Recommendation

1. Now: docs and a checklist (done in the README); try the image with Raspberry Pi Imager, Rufus and USB Image Tool.
2. Next: write and test Idea 1's first-boot service (`overlay/usr/local/bin/`, Python stdlib, small, `R3I6NOS_*`
   path overrides, tested on a laptop with a fake card), and the matching change to `docs/making-a-card.md`.
3. Then: Idea 3, the Imager repository, once a release exists.
4. Only if reader testing shows people still stop at the stock-card step: Idea 2, starting with Windows, the only
   platform with no path.

## Questions for the maintainer

- May the device create and format the games partition on first boot (rule 7 says only the flash script does)?
  Without it, Windows and Linux readers have no way to get the exFAT partition except a second card.
- Is a Windows code-signing certificate acceptable cost, or should Windows stay a "Raspberry Pi Imager plus
  first-boot capture" path with no `.exe` of ours?
- Does the original card have to stay in TF2 for the first boot, or must the kit work from a one-time copy of its
  files (a folder the reader makes on any computer)?

# Trial T1: the first boot of the two-slot setup (a checklist for the bench)

**Status: not run.** This is the first time any of `docs/TWO-SLOT-SETUP.md` meets a real R36S. Nothing here is verified yet; every "expect" below is what the design
predicts, and a surprise is a result, not a mistake. Write down what you see.

## What this one run answers

The setup is written, so the trials T1, T2, T3 and T5 and a first pass of T6 can be answered by **one run of the real thing** (section 9 of the design lists them):

| Question | Where you read the answer |
|---|---|
| **T1** Do the stock bootloader and kernel start r3i6nos from the card in the right slot? | the setup screen appears; the report's kernel command line |
| **T2** Is the original card left alone? | the hash of the spare "original" before and after (step 11) |
| **T3** Does reading the original read-only work on the unit? | the report's `genuine R36S`, `graphics driver` and `installed ...` lines |
| **T5** Does the new card then boot **alone** in the left slot, with working graphics? | the second boot (step 14) |
| **T6** (first pass) Does the whole flow finish, and how long does it take? | the screen, and the report's `time` lines |

If the first boot does not reach the setup screen, stop at the decision table (step 8): that is the answer to T1, and it is a useful one.

## Safety rules (read first)

1. **The real original card never goes in the console for this trial.** Make a **spare clone** of it (steps 1 to 3) and use the clone as "the original". If anything goes wrong, the real card is on a shelf, unread and unwritten.
2. **Check every disk name twice** before any `dd`: writing to the wrong disk destroys it. The card you are writing to must be the spare or the new card, never the real original and never an internal disk.
3. **Charge the R36S fully, or keep it on its charger, for the whole run.** The setup copies about 520 MB; a flat battery halfway is the one thing that can interrupt it.
4. **Do not switch the console off during the setup** until it does so itself. If power is lost anyway, nothing is lost: the bootloader is written last, so the new card is still a setup card. Put the cards back as they were and switch on again (the setup redoes everything); if it will not, rewrite the new card (step 5).

## You need

- The R36S, charged, and its charger.
- **Card A: the real original** (stays on the shelf).
- **Card B: a spare, 16 GB or bigger** (will be the "original" clone). Anything on it is erased.
- **Card C: the new card, 8 GB or bigger.** Anything on it is erased.
- A card reader and a computer (Mac or Linux for the `dd` commands; Windows works with USB Image Tool for steps 1 to 3).
- A phone for photos. **Photograph every screen the console shows**, with the time if you can.
- Pen and paper, or this file open to fill in section "Results".

## Prepare

### 1. Check the real original's size and make an image of its system part

Only the first **11,002 MiB** of the original matter (the bootloader, BOOT and the Linux root; the vendor's games partition after it is not needed).

```sh
# macOS                                         # Linux
diskutil list external physical                 lsblk -d -o NAME,SIZE,MODEL,RM
diskutil unmountDisk /dev/diskA                 sudo umount /dev/sdA* 2>/dev/null
sudo dd if=/dev/rdiskA of=original-first.img bs=1m count=11002      # Linux: bs=1M count=11002 status=progress
shasum -a 256 original-first.img                # Linux: sha256sum original-first.img
```

`diskA` / `sdA` is **card A**. Write the printed hash on paper as **H0**. Eject card A and put it away.

### 2. Write the clone to the spare (card B)

Raspberry Pi Imager (Use custom, pick `original-first.img`, choose card B), or `dd` with `of=/dev/rdiskB` / `of=/dev/sdB` after the same double check. Then take card B out and put it back in the reader.

### 3. Check the clone is exact

```sh
sudo dd if=/dev/rdiskB bs=1m count=11002 | shasum -a 256       # Linux: sudo dd if=/dev/sdB bs=1M count=11002 | sha256sum
```

It must equal **H0** (the clone is exact). Write it as **H1**. If it differs, the clone failed: redo step 2.

### 4. Point the clone at r3i6nos (the one edit)

On card B's **BOOT** drive (the small FAT drive that appears), use <https://r3i6nos.ansiapps.com/bootdisk/> step 7 (drop `boot.ini`, or pick the drive) to make `boot.ini` and `boot.ini.orig`, or edit by hand: copy `boot.ini` to `boot.ini.orig` first, then change the long code after `root=UUID=` to
`52333669-0000-4000-8000-000000000002`. Write down the **old code** (it identifies the stock image) and the tool's two SHA-256 values. Eject card B.

Then take the baseline again, **saving the image** so that any difference can be found later (it needs about 11 GB of free disk):

```sh
sudo dd if=/dev/rdiskB bs=1m count=11002 of=before.img         # Linux: bs=1M, /dev/sdB
shasum -a 256 before.img                                       # Linux: sha256sum before.img
```

Write it as **H2**. (H1 and H2 differ only because of this one file; that is expected. **H2 is the baseline for T2.**)

### 5. Make the new card (card C)

1. Get the image: on GitHub, **Actions > "Build (manual and tags)" > Run workflow**, wait for it, then download the **`r3i6nos-image`** artifact (`r36s-loveos.img.gz` and `.sha256`). **This build has never run with `r3i6nos-setup` in it: if the run fails, stop and send the log.**
2. Check it: `shasum -a 256 -c r36s-loveos.img.gz.sha256` (Linux: `sha256sum -c`).
3. Write it to **card C** with Raspberry Pi Imager (Use custom). This image has **no** bootloader, kernel, modules or Mali driver: that is intended.
4. Optional, for a boot log if the screen stays black: on card C's BOOT drive create an empty file named `r3i6nos-bootlog`. (**macOS cannot mount that BOOT drive yet** (ROADMAP section 2); use Linux or Windows, or skip this.) Do **not** create it on card B.

## Run

### 6. Insert and start

- Console **off**. Card **B** (the clone "original") in the **left** slot (TF1). Card **C** (new) in the **right** slot (TF2). **Charger connected.** Start a timer; note the battery level if the console shows it.
- Switch on. Photograph each different screen.

### 7. What to watch for, in order

1. The vendor's or ArkOS splash (a few seconds).
2. Then, if it works, the **setup screen**: a title, seven steps with `[ ]`, `[>]`, `[x]` marks, a message, **two bars (This step, Overall)**, and a line about keeping the console charged with the battery level.
3. The steps in order: *Check the original card*, *Check this card*, *Back up the original (only if asked)* (it asks only if card C has room: **do not press anything** this time), *Write the boot files*, *Install the drivers* (the long one), *Make the games area*, *Make the card bootable*.
4. A "Done" screen with the swap instructions, then the console switches **itself** off.

Write down the clock time when: the splash ended, the setup screen first appeared, each step finished, and the console switched off. (The report also lists the time of each step.)

### 8. Decision table: what did the first screen do?

| You see | Meaning | Do |
|---|---|---|
| The setup screen | **T1 passes** | go on |
| The **original system's own menu** (EmulationStation or similar) | the edit did not take, or card B is in the wrong slot | switch off; check B is in the **left** slot and `boot.ini` holds the new code; do not guess further: photograph and report |
| A **black screen** with the backlight on | unknown: wait **5 minutes** first (the setup may be working with nothing drawn) | still black: switch off, read card C's BOOT drive for `r3i6nos-setup-report.txt` and `r3i6nos-bootlog.txt` (if you made the flag); report |
| **Text with errors**, "waiting for root device", a kernel panic, a reboot loop | the kernel cannot find r3i6nos's root on card C | photograph every line; check the code in `boot.ini` was typed exactly; report |
| The setup screen **refuses**: "This is not a genuine R36S card" | a **false refusal** (card B is a clone of a real card) | photograph the sentence, then read the report's `genuine R36S` line (it names the cause: board file, kernel, clone sign); report |
| The setup screen **stops with another message** | a check failed before anything was written to card C | photograph; the report on card C's BOOT drive has the reason; report |

A button press switches off a stopped setup; otherwise hold the power button.

## Collect

### 9. Read the report

Put card **C** in the reader. On its BOOT drive is **`r3i6nos-setup-report.txt`** (written whether the setup finished or stopped). Copy it. **If your computer cannot mount card C's BOOT drive**, use a Linux or Windows machine, or run step 14 and read it from the games area instead (the report is also in the device report's `setup` section). Look for:

- `PASS genuine R36S`, `PASS graphics driver`, `PASS kernel modules`, `PASS installed ...` lines, and `PASS bootloader written ... (the last write)`;
- `INFO boot partition mounted at /boot before setup: ...`: **this answers a design question**. If it names card B's partition, the label clash is real and the setup handled it; if it says nothing was mounted, the clash did not happen;
- `INFO kernel command line: ...` (should show `root=UUID=52333669-...`), `INFO seconds since power on`, `INFO memory available at start` and `... after the drivers` (how close to the 1 GB limit it ran), `INFO time ...` for each step, `INFO battery ...`;
- any `FAIL` line, and the last line `end`.

### 10. Card C's layout

```sh
diskutil list /dev/diskC            # Linux: lsblk -f /dev/sdC
```

Expect **three partitions**: BOOT (FAT), ROOTFS (ext4), and a third **exFAT named EASYROMS** filling the rest, with folders `Love 11.5`, `Love 12.0`, `native_deb`. **No `Backup` folder** (the backup is off). Note anything else.

### 11. T2: was card B left alone?

Put card **B** in the reader (do not mount it for writing) and image it the same way:

```sh
sudo dd if=/dev/rdiskB bs=1m count=11002 of=after.img          # Linux: bs=1M, /dev/sdB
shasum -a 256 after.img                                        # Linux: sha256sum after.img
```

Write it as **H3**. **H3 must equal H2.** If it does not, find what changed (an important result: send it):

```sh
cmp -l before.img after.img | wc -l                            # how many bytes differ
cmp -l before.img after.img | head -40                         # the first byte offsets (1-based) that differ
```

An offset divided by 512 is the sector: sectors 0 to 63 are the partition table, 64 to 32767 the bootloader area, 32768 and up BOOT, and 262144 and up the Linux root.

### 12. Card A

Confirm it never left the shelf. (Optional: hash it again against H0 to prove it.)

## Second boot (T5): does card C stand on its own?

### 13. Swap and start

Take card B out and put it away. Put **card C in the LEFT slot (TF1)**, **right slot empty**, charger connected. Switch on.

Expect: the r3i6nos menu (the LÖVE menu draws: this proves the **Mali driver and kernel modules were installed correctly**), with the IO Checker listed. Note how long the boot to the menu took, and whether anything looks wrong.

### 14. Read what the card says about itself

In the menu: **Settings > About** (build, kernel, whether the Mali driver is present, the card's space), then **Settings > Device reports > r3i6nos**. Wait for it to finish; it writes `r36s-report.txt` (on the games area, `r3i6nos/`). Copy both `r36s-report.txt` and `r3i6nos-setup-report.txt` (it is on BOOT; also kept by the report's `setup` section: look for `INFO made by r3i6nos-setup` and `PASS setup report on BOOT`).

## Results (fill in)

```text
Date / console / card A, B, C sizes and brands:
Image: CI run (link or number), its sha256:
Old code in the original boot.ini (first 8 characters):
H0 =        H1 =        H2 =        H3 =        H3 == H2 ? yes / no

T1  first screen after power on (decision table row):
    time from power on to the setup screen:
    /boot before setup (report line):
T3  genuine R36S / graphics driver / installed lines: all PASS ? any FAIL (paste):
T6  step times (report):     total:
    memory available at start / after the drivers:
    did the console switch itself off? how long after "Done"?
    battery at start / end (if shown):
    backup question appeared? (expected: yes if card C has room; you did not press)
T5  second boot: menu appeared? yes / no.  time to menu:
    LÖVE menu drawing (graphics driver works)? yes / no
    Settings > About: Mali driver present?  kernel:
    Device report FAILs (paste):
Photos taken (list):
Anything odd:
```

## Send back

Give Claude: the filled-in results, `r3i6nos-setup-report.txt`, `r36s-report.txt`, the photos, and the text of any error. Claude then updates, from what is true:

- `docs/TWO-SLOT-SETUP.md` section 12 (what the unit showed) and section 9 (T1, T2, T3, T5, T6 first pass: each ticked or failed with the evidence), both ROADMAPs and `docs/hardware-findings.md`;
- the boot disk guide at r3i6nos.ansiapps.com/bootdisk (its steps 8 to 10 and the banner say the setup has not run on a real R36S; they change only when this is true);
- the code, for whatever failed.

## After this, still to do

T4's drivers-in-the-running-system check is covered by step 13. **Not covered here:** the backup (T8: run again and press a button when asked, then restore it and boot the restored card), power cuts at each step (T7), a button press read from the pad (the backup question), another console or panel (T10), and first-time readers following the guide (T9).

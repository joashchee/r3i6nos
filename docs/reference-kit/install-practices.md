# Best practices for guiding a reader through a flash

Drawn from [firmware-survey.md](firmware-survey.md) and the failure reports found with it. "Seen" means a firmware's own
page says it; "ours" means a decision for r3i6nos, not yet tried.

## How to write the guide

1. **Start with the goal and a checklist of what the reader must own** (device, working card, second card, reader,
   computer). Say what is erased, once, in bold, before the first step and again at the step that erases it.
2. **Stop the wrong reader first.** The genuine-board check comes before the download, with a look-at-the-device test,
   because a wrong image can damage some clones (seen: R3XS, ArkOS4Clone).
3. **One path per platform, same step numbers.** Steps 1 to 5 are the same everywhere; only the *how* differs.
   Use tabs or labelled sub-steps, never "if you are on...", prose.
4. **One tool per platform, named once.** Alternatives go in a footnote, not in the steps.
5. **Verify before write.** Show the hash next to the download and the command for each OS:
   - Windows: `Get-FileHash -Algorithm SHA256 .\file` (PowerShell)
   - Mac: `shasum -a 256 file`
   - Linux: `sha256sum file`
   Say what to do on a mismatch: download again, from the same page.
6. **Say what success looks like at each step** (a screen, a sound, a file) and what to do if not.
7. **Put the "do nots" at the step they apply to** (seen: second card out, charger out, do not expand partitions,
   do not format when Windows asks).
8. **Name the cost of time.** First boot takes minutes; say how many and that a black screen for that long is normal.
9. **Keep a recovery line**: the original card is the way back; keep it.
10. **Put the community link where people get stuck**, not only at the top (ours).

## Per platform

| | Windows | macOS | Linux |
|---|---|---|---|
| Unpack | 7-Zip (not WinRAR, seen); `.gz` and `.xz` both | double-click `.gz`; `.xz` needs The Unarchiver or Keka | `gunzip`, `unxz` or 7-Zip |
| Write | Raspberry Pi Imager, USB Image Tool, Rufus (seen) | Raspberry Pi Imager, ApplePi-Baker, or `diskutil` + `dd` | Raspberry Pi Imager, Disks, or `dd` |
| Find the card | the drive letter in the tool; Disk Management for EASYROMS | `diskutil list external physical` | `lsblk` |
| Elevation | tool asks (UAC) | tool asks; `dd` needs `sudo` | `sudo` or polkit |
| Verify | `Get-FileHash` | `shasum -a 256` | `sha256sum` |
| Trap | "You need to format the disk" dialogs on Linux partitions | Finder may add characters to ROM names (seen); card reader sleep | the desktop auto-mounts partitions before `dd` |

**Imaging tool choice (open):** the survey is split. Etcher is the most common name in guides but two ArkOS-family wikis
say avoid it with their images; Windows users report Etcher leaving cards read-only after an interrupted write (forum
threads, not first-party). Raspberry Pi Imager and Rufus appear on every list and on no avoid list. We list Raspberry Pi
Imager; **try our own image with it, Rufus, USB Image Tool and Etcher on a real card before we claim any is safe** (untested).

## The failure list (what to put under "Something wrong?")

| Symptom | Usual cause | Source |
|---|---|---|
| Black screen after the logo | wrong panel/DTB, or a clone | seen, every ArkOS-family wiki |
| Red light blinking, no boot | an ArkOS image on an EmuELEC-type clone | Handhelds Wiki via search |
| Hangs at first boot | partitions expanded by hand; charger in; second card in | seen |
| Writes fail or verify fails | bad or counterfeit card; cheap reader; USB hub | seen, guides and forums |
| Card reads as write-protected | the adapter's lock switch; a failed earlier write | forums |
| Image will not decompress | WinRAR; a missing part of a multi-part archive | seen |
| Windows says "format the drive" | it cannot read ext4; the answer is No | seen (Knulli) |
| An included `.exe` vanished | antivirus | seen (ArkOS4Clone) |
| Games not shown | wrong folder names; ROM file names changed by the OS | seen |

## What to measure once a release exists (ours)

- Time from download to menu for a first-timer on each platform, watched, not guessed.
- Which step people stop at (a Discord question per step works).
- Imaging tool failures per tool, so the recommendation rests on our image's numbers.

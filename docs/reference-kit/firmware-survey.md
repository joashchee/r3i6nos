# Firmware survey

Read directly: dArkOSRE-R36 wiki, ROCKNIX install page, Knulli install page, ArkOS wiki, ArkOS-R3XS README,
RetroGFX/UnofficialOS README, dArkOS README, arkos4clone repo page. **Not readable** (HTTP 403): the Handhelds
Wiki pages and Reddit; what they say below is from search summaries. Third-party guides (retrohandhelds.gg,
techtactician.com, joeysretrohandhelds.com) were seen only as search snippets. Check a claim at its source before
repeating it.

## Status at a glance

| Firmware | Based on | Status (2026-11) | Targets | Image form |
|---|---|---|---|---|
| **ArkOS** (christianhaitian) | Ubuntu | the original; the wiki is still the reference | RG351 family and many RK3326 | `.img.xz`, MD5 per download |
| **ArkOS R3XS** (AeolusUX) | ArkOS | **archived, read-only**; replaced by dArkOS | R33S, R35S, R36S, R36H, R36 Plus, others; R33S has its own image | `.img.xz`, multipanel builds |
| **ArkOS K36** (AeolusUX) | ArkOS | development stopped 2025-08-25 | K36 and R36S clones | `.img`, swap DTB until one boots |
| **ArkOS4Clone** (lcdyk0517) | dArkOS/ArkOS 4.4 kernel port | active; "early/preview" in its release notes | unsupported RK3326 clones (R36T, R36T Max, K36 and more) | split archive `.001-.003` to `.img.xz` to `.img` |
| **dArkOS** (christianhaitian) | Debian (stable) | active; online updates, built with `make <device>` on Ubuntu 24.04+ | RK3326 and RK3566 | prebuilt images; no end-user steps in the README |
| **dArkOSRE-R36** (southoz) | dArkOS | active; the community's recommended R36S build (Handhelds Wiki, via search) | R36S and clones, many DTBs | `.img` in a release archive, SHA-1 on the release page |
| **ROCKNIX** | JELOS | active | many SoCs (RK3326 to RK3588, others) | per-platform images, decompress then write |
| **UnofficialOS** (RetroGFX) | JELOS | active | RK3326, plus a separate `RK3326-CLONE` build (R33S, R35S, R36S, Kinhank K36, BatleXP G350) | `.img.gz` |
| **JELOS** | n/a | no longer maintained (ROCKNIX continues it) | | |
| **Knulli** | Batocera | active; per-device images | many, mostly Allwinner and Rockchip | multi-part archives, 7-Zip |
| **AmberELEC, Batocera** | n/a | seen in guides only; not studied | | |

## ArkOS family (ArkOS, R3XS, K36, 4Clone, dArkOS, dArkOSRE)

- **Flashing:** extract with 7-Zip (Windows), The Unarchiver or Keka (Mac), 7-Zip (Linux); write with USB Image
  Tool or Win32DiskImager (Windows), ApplePi-Baker or `dd` after `diskutil unmountDisk` (Mac), Disks (gnome-disks)
  or `dd` (Linux). **"DO NOT USE BALENA ETCHER WITH THIS IMAGE"** (ArkOS wiki). dArkOSRE says the same and adds
  Raspberry Pi Imager and Rufus as fine.
- **Verify:** ArkOS gives an MD5 per image; dArkOSRE gives a SHA-1 and shows only the Windows command
  (`Get-FileHash -Algorithm SHA1`).
- **Panel (screen) and board files, four ways:**
  1. *Boot-time picker* (R3XS): hold **R1 + a direction** at power-on, the unit saves the choice and powers off;
     no host tool, works on any platform. **R1 + Vol Down** turns the picker off. Not on first boot (setup can take 10 min).
  2. *Host script on the boot partition* (dArkOSRE, Windows): double-click **SELECT MODEL**, type the number
     from a table of board revisions and panels. On Mac and Linux: delete every `.dtb` in the boot root and copy the
     right one from `dtb/r36s/`, `dtb/clone/` or `dtb/soysauce/`.
  3. *Host DTB selector program* (ArkOS4Clone): a Windows `DTB_Selector_Win32.exe` run before first boot; one user
     had it deleted by antivirus. Plus web tools: a **DTB Analysis Tool** (identifies the clone from files on the
     stock card) and a **DTB Customization Tool** (axes, buttons, battery capacity, refresh rate).
  4. *Trial and error* (ArkOS K36): flash, swap DTB, test, repeat.
- **First boot:** reboots twice while ArkOS converts the games partition (NTFS to exFAT) and fills the card; **do not
  expand EASYROMS yourself** before it (hangs). dArkOSRE: about 2 to 3 minutes, **charger unplugged, second card removed**.
  A blank screen after install usually means a Panel 9 board.
- **Second card:** dArkOSRE: OS card in one slot, ROMs on a second card with a *unique label* (not `EASYROMS`),
  switched in EmulationStation (Options > Advanced > Switch to SD2), with `/etc/fstab` edits to move Tools.
  Compare r3i6nos, which mounts by role, not label (`docs/STORAGE.md`).
- **Clone danger:** a wrong image on some V20 clones can over-volt the speaker and damage it permanently
  (R3XS and ArkOS4Clone notes). A genuine-RK3326 check is therefore a safety step, not a courtesy.
- **Windows quirks:** give EASYROMS a drive letter in Disk Management if it has none; on Mac, watch for characters
  added to ROM filenames.
- **Updates:** online from the menu (Options > Update), Wi-Fi first. dArkOS's stated aim is that reflashing is only for
  structural changes.

## ROCKNIX

- **Windows: the ROCKNIX Image Burner**, a releases-page download: pick manufacturer and device, click *Write Image to Drive*.
  The only firmware found with its own flasher. Its source was not read here.
- **Anywhere else:** decompress, write with Rufus, Raspberry Pi Imager, Win32 Disk Imager or `dd`; insert with the device
  off; first boot installs, reboots, opens EmulationStation. Some devices need the boot order set to SD first.
- **Storage:** the OS partition is ext4 (**Windows cannot read it**); a second card may be ext4, FAT32 or exFAT and is
  detected at boot. x86 builds have an installer in the Tools menu.
- An Android route exists for people with no computer: **EtchDroid** (Handhelds Wiki, via search).

## UnofficialOS

`.img.gz` per device; write with Etcher, Raspberry Pi Imager, Win32 Disk Imager or `dd`. Separate `RK3326` and
`RK3326-CLONE` images, the clone one with its own wiki instructions. OTA updates from EmulationStation (since
20250207). Network services are off by default; SSH and Samba use root, with a password regenerated every boot and
shown in System Settings. Same stance as r3i6nos rule 6.

## Knulli (Batocera-based)

Per-device images in multi-part archives (get every part; 7-Zip). **Only the small BATOCERA partition is FAT32**; the
other partitions are Linux-only and **Windows will offer to format them: say no.** The SHARE partition expands on
first boot and is unusable until it finishes. Second slot left empty during first boot. Guides name Etcher (recommended)
or Rufus (Windows only).

## Patterns worth copying

- A table of boards and panels that a reader matches by *looking at the device* (revision text, a photo), not by knowing chip names.
- A picker that needs no computer (R3XS's R1 combo).
- Hash shown next to the download, with the command for each OS.
- "Do not" lists at the point of the step: second card out, charger out, do not expand, do not format.

## Patterns to avoid

- A cloud-drive-only download (Google Drive, Mega, Mediafire): quotas, no hash, no stable URL.
- An unsigned Windows `.exe` as a required step (antivirus removes it).
- Starting from the tool's name ("use X") rather than the reader's goal.
- Leaving the clone check to a link: the image must refuse, or the page must stop the reader first.

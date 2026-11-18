# r3i6nos

A tiny operating system for the **R36S** handheld. It turns on, shows a simple menu, and plays
**LÖVE games** and runs small **apps**. No emulators, nothing else to set up.

> **Early days.** r3i6nos has run on a real R36S since 2026-11-09: the menu, Wi-Fi, SSH, the screen mirror, the sound and battery pages and its apps have all been
> tried on a unit, but plenty is still untested there, and there is **no ready-made download yet** (see [Step 2](#step-2--get-the-r3i6nos-image)).
> Flash only a card you can afford to wipe, and keep your original card safe.

**Guided setup:** <https://r3i6nos.ansiapps.com/bootdisk> walks you through every step with pictures (a test version: the part that runs on the console itself is written but has not been tried on a unit yet).

## What you get

- **A menu that starts at once** and lists your LÖVE games (11.5 and 12.0) and your native apps. Pages, never scrolling. Nothing is installed on your card that you did not put there.
- **Built-in apps:** **Commander**, a two-panel file manager whose viewer opens about 150 formats (pictures, ANSI art, archives, packages, disk images, documents) and plays
  music and video; the **IO Checker**, which checks every button, stick, the screen, the sound, both cards and what the OTG port brings, and writes a report;
  and, for testing, **PortMaster** (see [below](#portmaster-for-testing)).
- **Settings** in the menu: **Network and SSH** (Wi-Fi or a USB Ethernet adapter, then log in from a computer), **Battery** (charge, use by app), **Sound mixer**,
  **Date and time**, **Data card** (format and open the card in the second slot), **Screen mirror** (the console's screen on a computer or TV over the network),
  **Device reports** and **Rec demos** (text files and recordings that help the maintainer see what your console did), and **About** (the build, **Check for updates**).
- **Two card slots, one job each:** the boot card (TF1) holds the system and, if you like, your games; a second card (TF2) is a **data card** a computer can read.
  Apps keep their data on the data card when one is in, and offer to move it over.
- **Free apps from the same maker** (music, drawing, a tracker, a player, a synth sandbox and more) are built on their own and added like any other `.deb`; some are still private,
  and none of them is in the image.
- **No emulators, no telemetry, no accounts, and no network at boot.** It goes online only when you choose it.

**Help, news and updates:** join the community on Discord, <https://discord.gg/6q8mrKRKqh>.

## Before you start

Check that your console is a genuine **R36S with a Rockchip RK3326** chip. Look-alike clones
(often with an Allwinner chip) **must not** be flashed. How to tell:
[the spec, section 1](docs/r36s_love_only_firmware_specification.md). Not sure? Ask on Discord first.

You need:

- Your **R36S**, and the **original card that came with it**, still working. r3i6nos copies the
  console's own start-up files from it (they belong to the maker, so we can't include them).
- A **second microSD card**, 8 GB or bigger. **Everything on it will be erased.**
- A **card reader** for your computer.
- A computer: **Windows, Mac or Linux**, with internet.

## Step 1: Install the free card-writing app

Download **Raspberry Pi Imager** from <https://www.raspberrypi.com/software/>, install it like any
app, and open it once to check it starts. It is free, works on Windows, Mac and Linux, and only
writes to the card you pick. (Other firmwares tell people to avoid balenaEtcher; Rufus or Win32 Disk Imager also work on Windows.)

## Step 2: Get the r3i6nos image

Go to <https://github.com/joashchee/r3i6nos/releases> and download the newest
**`r36s-loveos.img.gz`** (and its `.sha256` file if you like to check it).

**No release is published yet.** Until it is, ask on Discord; the image can also be built from the
project's **Actions** page on GitHub: run **Build (manual and tags)** and download the image it makes (it needs a free GitHub account).

## Step 3: Add your console's own start-up files

The image alone won't boot: it needs files from **your original card**. This step is the same on
every computer, but only the **Mac** way has been tried; Linux should work the same, and **Windows
isn't supported yet** (ask on Discord, we'll help).

### On a Mac

1. On this page click the green **Code** button, then **Download ZIP**, and open the ZIP so you get
   a folder.
2. Install the helper tools: open **Terminal** (press ⌘ Space, type *Terminal*, Enter), paste
   `/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"`,
   press Enter and follow the prompts (skip if you already have Homebrew), then paste
   `brew install mtools e2fsprogs` and press Enter.
3. Put your **original card** in the reader. In Terminal type `cd ` (with a space), drag the
   r3i6nos folder into the window, press Enter, then run
   `diskutil list external physical` and note the card's name, such as `disk4`.
4. Copy its files: `scripts/capture-card.sh disk4` (use your own disk name; it only reads the card).
5. Unzip the image to get `r36s-loveos.img`, put it in the same folder, then run
   `scripts/inject-boot.sh r36s-loveos.img` and then `scripts/inject-rootfs.sh r36s-loveos.img`.

### On Linux

Same as the Mac, with `sudo apt install mtools e2fsprogs git` (or your system's equivalent) instead
of Homebrew, `lsblk` instead of `diskutil` (the card is named like `sdb`), and `git clone
https://github.com/joashchee/r3i6nos` instead of the ZIP.

### On Windows

Not available yet. Join the Discord and say you are on Windows.

## Step 4: Write it to the new card

1. Put the **second (blank) card** in the reader and open **Raspberry Pi Imager**.
2. Choose device **No filtering**, choose OS **Use custom**, and pick `r36s-loveos.img`.
3. Choose storage: the **second** card. **Check twice: it erases the card you pick.**
4. Click **Next**, say no to customisation, confirm, and wait until it says it is done.

The card will have empty space left over. On a Mac, `scripts/flash-macos.sh` can also turn that
space into your games area in one go (`docs/making-a-card.md`).

## Step 5: Turn it on

Put the new card in the **left slot (TF1)** of the R36S and switch on. The menu appears.
Use the **D-pad** to move, **A** to start, **B** to go back, **L / R** to change page.

## Adding games and apps

Games go on a card the R36S can read as a normal drive (exFAT) in these folders:

| Folder | What goes in it |
|---|---|
| `Love 11.5/` | LÖVE games (`.love` files) for LÖVE 11.5 |
| `Love 12.0/` | LÖVE games that need LÖVE 12 |
| `native_deb/` | ARM64 apps (`.deb` files); they install at the next start |

Put the files in, put the card back, and turn the R36S on. The menu lists what it finds. Details,
including using a second card for games: [docs/making-a-card.md](docs/making-a-card.md).
Only add games and apps you trust: they run without a safety sandbox.

**Without taking the card out:** turn on **Settings > SSH** and, from a computer, send a `.deb` to the console; it appears in the menu within a second
(`r3i6nos-pkg`, [docs/PACKAGES-OTA.md](docs/PACKAGES-OTA.md)). The data card in the second slot can be formatted from the console itself
(**Settings > Data card**), so a new card needs no computer.

### PortMaster (for testing)

[PortMaster](https://portmaster.games) (MIT-licensed, a curated catalog of game ports) is in the image **for testing only**: it is not an official part of r3i6nos, the R36S
is not an officially supported device for it, many ports need more than this console has, and **nothing of it has run on a real unit yet**. It goes online only when you open it and
choose to install something; ports that need a game's own files need **your own copy**, and r3i6nos ships and finds no games. Installed ports show up in the menu.
Details and what is untested: [docs/PORTMASTER.md](docs/PORTMASTER.md).

## Something wrong?

- **Nothing on screen after flashing:** check it is a genuine RK3326 R36S, and redo Step 3.
- **A step says what is wrong:** believe it, the scripts stop rather than guess.
- **Anything else:** ask on Discord, <https://discord.gg/6q8mrKRKqh>. The menu's
  **Settings > Device reports** make a text file that helps us see what your console did.

## Updating

Join the Discord for news. On the console, **Settings > About > Check for updates** tells you
when a newer version exists (it only goes online when you choose it). To update the system, flash a new card
with Steps 2 to 4; your games stay on their own card. Apps update without a reflash (send the new `.deb`, see above). Signed system updates with rollback are built
(`docs/PACKAGES-OTA.md`) but wait for the maintainer's signing key, so they are not on yet.

## For developers

```sh
scripts/check.sh    # Python and Lua tests, bash -n, shellcheck
love menu           # run the menu on a desktop (set R3I6NOS_ROMS to a test folder)
```

Layout and rules are in `CLAUDE.md`; what's next is in `ROADMAP.md`; what changed is in `CHANGELOG.md`; the design is in
[the spec](docs/r36s_love_only_firmware_specification.md). The image carries only native `.deb` apps (Commander, the IO Checker, PortMaster) with the LÖVE 11.5 and 12.0 runtimes,
`nttplay` (the one decoder of the ansiapps music formats) and a minimal `ffmpeg`; the other apps are separate repositories, built by `scripts/build-ports.sh`.

## Disclaimer

r3i6nos is an independent hobby project. It is **not made, endorsed or supported by** the makers or sellers of the R36S, by Rockchip, Arm, the LÖVE
project, Debian or anyone else whose names appear here; those names belong to their owners and are used only to say what this works with.

- **Use it at your own risk.** The software is provided as is, without warranty (`LICENSE`). Much of it has run on a real R36S, but not all.
- **Flashing erases a card.** Check the card you pick twice, and keep your original card as a backup.
- **No games, ROMs, BIOS files or proprietary drivers are included**, and the project does not help find them. The GPU driver, kernel modules and bootloader are copied
  from *your own* card onto *your own* image. An image that contains them is for your card only; do not share it.
- **Games and `.deb` files you add are your responsibility.** They run on the device without a sandbox, so install only what you trust.
- Security problems: see [SECURITY.md](SECURITY.md).

## Licence

MIT, see `LICENSE`, **except the theme's font** (`theme/font/`, `theme/vga8x16.lua`, `theme/r3i6nos_vga8x16.h`), which is VileR's
IBM VGA 8x16 under CC BY-SA 4.0 (credit: *The Ultimate Oldschool PC Font Pack*, <https://int10h.org/oldschool-pc-fonts/>; see `NOTICE`).
LÖVE is zlib-licensed; the minimal ffmpeg is GPL-2.0-or-later and PortMaster is MIT; the base system and runtimes keep their own licences (`NOTICE`).

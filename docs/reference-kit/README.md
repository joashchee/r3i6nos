# Flashing reference kit

What the other R36S-family firmwares tell people to do to install them, what goes wrong, and what r3i6nos
should copy, avoid or build. Researched 2026-11-11 from the projects' own pages where they could be read,
and from third-party guides where not (each file says which). Nothing here was run on a device.

| File | What it holds |
|---|---|
| [firmware-survey.md](firmware-survey.md) | One section per firmware: status, devices, how it installs, what is special |
| [install-practices.md](install-practices.md) | Best practices for guiding a reader, per platform, and the failure list |
| [tool-ideas.md](tool-ideas.md) | What custom tools could remove, ranked, with a recommendation |

## The short version

1. **Every firmware installs the same way**: download a per-device image, decompress it, write it to a microSD
   card with an imaging tool, boot. Only the details differ: the DTB (panel) choice, a second card, first-boot rules.
2. **The imaging tool is the most argued-about step.** ArkOS and dArkOSRE say *do not use balenaEtcher* for their
   images; ROCKNIX, UnofficialOS and Knulli list it or Rufus, Raspberry Pi Imager, Win32 Disk Imager and `dd`.
   Our README had named Etcher; it now names Raspberry Pi Imager (one tool, all three systems, on nobody's avoid list).
3. **Most failed installs are one of**: a wrong image for the board, the wrong panel file (black screen), a bad or
   fake card or cheap reader, a second card left in, the charger plugged in during first boot, an interfered-with
   first boot (expanding partitions by hand), Windows offering to "format" Linux partitions.
4. **The hardest step for r3i6nos is ours alone**: the board's own boot files must come from the reader's own card
   (CLAUDE.md rule 2). Nobody else has this step, and it needs Mac or Linux scripts today.
5. **Recommended**: keep the docs path (Raspberry Pi Imager, checksums, a short checklist), publish a repository
   JSON so Imager lists r3i6nos, and replace Step 3 with a small native tool or, better, an on-device first-boot step
   so no host tool needs to read ext4 (see [tool-ideas.md](tool-ideas.md)).

## Decisions this needs from the maintainer

- Whether the device may create and format the games partition on first boot (today only `flash-macos.sh` does, rule 7).
- Whether to write a native host tool at all, and in which language, once the on-device path is tried.
- Whether to publish releases and an Imager repository (needs the first release, ROADMAP section 7).

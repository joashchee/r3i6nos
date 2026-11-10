# Default apps

> New apps start from the r3i6nos-ansiapps theme (`docs/R3I6NOS-ANSIAPPS-THEME.md`). The IO Checker and Commander are on it.

Built into every image (`/opt/loveos/defaults`), so the menu is never empty. Anything on the games card
(`EASYROMS`) is listed beside them, and a file of the same name there replaces the built-in one.

## IO Checker

The R36S's hardware check, three ways (spec: `docs/IO-CHECKER.md`): what goes in (every button, both sticks, the D-pad, the
power and volume keys, with presses, chatter, axis ranges and the buttons never pressed), what comes out (full-screen video
test patterns and a measured frame rate; test tones on each channel that you mark heard or silent; file checks and speed on
both cards), the system, and a **device report** at `EASYROMS/IO_Checker/r36s-report-<build>.txt`. It replaced the R36S Input
Checker (2026-11-09), which measured the pad's layout.

| App | Where it is built in | Source |
|---|---|---|
| LÖVE 11.5 | `Love 11.5/IO_Checker.love` | `io-checker-love/` |
| LÖVE 12.0 | `Love 12.0/IO_Checker.love` | `io-checker-love/` (same code, packed twice; it says the runtime it runs on) |
| Native ARM64 | `native_deb/IO_Checker_Native.deb` | `io-checker-native/` (C and SDL2 on `theme/r3i6nos_ansiapps.h`) |

- LÖVE: `scripts/configure_rootfs.sh` packs the folder with `scripts/pack_love.py` (same bytes every time). `model.lua` (logic, the
  report's text), `probe.lua` (the system and the cards, plain Lua io) and `view.lua` (the screen on the theme) are pure, and
  `tests/test_io_checker.lua` tests them and drives `main.lua` through every page under a fake LÖVE and a fake pad. The theme is
  read from `/opt/loveos/theme` (`R3I6NOS_THEME` overrides).
- Native: `scripts/build_native_apps.sh` cross-compiles it in a Debian 12 container and packs the `.deb` (the `native` job in
  `.github/workflows/build.yml`); `scripts/smoke_native.sh` runs its `--selftest` under qemu. `io-checker-native --screenshot out.bmp
  [page]` draws a page to a file; `--report` writes the device report without a window (over SSH).

**Leaving:** B or Quit on the list, or hold Select+Start (raw 13 and 14) for a second, which goes back to the list from any page.
The input page goes back by itself after **15 seconds** without input (counted down in the last 10), so a dead Select or Start cannot
trap anyone. Esc on a keyboard.

**Button numbers** on screen count from 1, as LÖVE does; the layout is the measured one (`docs/hardware-findings.md`). Power and
the volume keys are not joystick buttons: their pills light from keyboard key events, if the system delivers them (not yet seen).

**Not run on a real R36S** in this form. Tested here: the logic, every page as text, the file checks and report in temporary folders,
the LÖVE app under a fake LÖVE, and the native build's self-test and screenshots.

## Commander, the file manager

`native_deb/Commander.deb` (`commander-native/`, C and SDL2): a two-panel file manager in the style of Norton Commander and Midnight
Commander, and the first app on the **r3i6nos-ansiapps theme** (`docs/R3I6NOS-ANSIAPPS-THEME.md`, C header `theme/r3i6nos_ansiapps.h`).
Row 0 is the top bar (free space), rows 1-20 are two 32-column panels (the active one double-framed, 18 entries a page, `Page` as `n/m` in
its edge), rows 21-23 are the command line: a tip or the last result, the folder as the prompt (or the line being typed), and the selected
file's mode, size and date with its place in the list (a progress bar while it works). The left panel starts on the games card (`/roms`), the right on `$HOME`.

| Button | Does |
|---|---|
| D-pad up / down | move |
| D-pad left / right | switch panel (the left panel is active after left) |
| L1 / R1, L2 / R2 | page up / down, top / bottom |
| A | open a folder, or view a file (see **The viewer** below) |
| B | parent folder (the folder you came from stays selected); cancels a running job |
| X | tag / untag the entry and move down; **Y** tags all or none |
| Start | the menu: View, Copy, Move, Rename, Make directory, Delete, Tag all, Untag all, Sort by, Hidden files, Swap panels, Other panel = this folder, Quit |
| Select + Start | quit to the menu |

A keyboard works too: F2 rename, F3 view, F5 copy, F6 move, F7 new folder, F8 delete, F10 quit, Tab switch panel, Insert/Space tag.
Rename and new folder use the command line: Up/Down step the last letter through a list, Right adds a letter, Left deletes one, A accepts,
B cancels (a keyboard just types).

### The viewer

A on a file works out what it is from its bytes (magic numbers first, then the name, then a look at the bytes: the same order of rules
the ansiapps catalogue app Diskette uses, rewritten in C in `formats.h`) and opens it in tabs. **Left/Right** (or X/Y) switch tabs,
**Up/Down** and L1/R1 turn pages, L2/R2 go to the first and last page, A or B close it. It opens on the most useful tab: the picture,
the art or the contents when there is one, else the text, else Hex for unknown binary data.

| Tab | Shows |
|---|---|
| **Info** | the type and category, the exact size, and that format's details (below) |
| **Picture** | PNG (every colour type and depth, interlaced), JPEG (baseline; a big photo is decoded at 1/8 size, which is still sharper than the screen; EXIF orientation applied), GIF (first frame), BMP (1-32 bits, RLE), ICO/CUR, TGA, QOI, PCX, PBM/PGM/PPM, drawn over the whole display view: a small one enlarged by a whole number, a big one shrunk, transparency on a dark checkerboard |
| **Art** | ANSI art (escape codes, iCE colours, 256 and 24-bit colours brought to the 16), NFO and DIZ (code page 437), BIN, XBin (its palette and font, packed or not), ArtWorx ADF, iCE Draw IDF: drawn with the VGA font at its own 8x16, 25 rows a page (wider art in narrower cells, so nothing scrolls sideways) |
| **Contents** / **Text** / ... | what is inside: the files of a ZIP (and LÖVE `.love`, jar/apk, msix, `.mxl`), TAR, `.tar.gz`, RAR 4/5, Cabinet, cpio, LHA, ar, ISO 9660 (the top folder); a `.deb`'s control file and file list; a disk image's partitions (MBR or GPT) with each one's filesystem and label; the text of a PDF, RTF, HTML, docx/pptx/xlsx, odt and EPUB; a `.gz` unpacked; a tracker module's instrument names and message; a MIDI's names, lyrics and markers; an SQLite database's tables; a dBase table's fields; a program's libraries |
| **Text** (**Source** next to a document's Text) | the file as text: UTF-8, UTF-16 with a byte-order mark, or code page 437 byte for byte when it is not UTF-8 (DOS text and NFO files) |
| **Hex** | the bytes (the first 4 MiB) |

What **Info** knows, by family:

- **Pictures:** dimensions, colour type and depth, interlacing, resolution, PNG text chunks and APNG frames, JPEG coding (baseline,
  progressive, CMYK...) and EXIF (camera, lens, date taken, exposure, aperture, ISO, focal length, flash, orientation, GPS present),
  GIF frames and looping, ICO's pictures, WebP (lossy, lossless, extended), HEIC/AVIF and PSD size, SVG width/height/view box, TIFF tags.
- **Sound and music:** WAV (coding, rate, channels, bits, length, INFO tags), AIFF, FLAC (stream info, Vorbis comments, cover art), MP3
  (ID3v1 and v2.2-2.4 tags, MPEG version and layer, bit rate, VBR length from Xing/VBRI), Ogg Vorbis/Opus/FLAC/Theora (comments, length
  from the last page), MIDI (format, tracks, timing, tempo, time and key signature, notes and channels, length through tempo changes,
  RIFF RMID too), MOD, S3M, XM, IT, MTM, STM (title, channels, patterns, instruments, tracker), Neumetik's `.ntt`, and the names of
  AHX, HivelyTracker, Sawteeth, Klystrack, Musicline, ProTrekkr, DigiBooster, OctaMED, Oktalyzer and more. Music as text (ABC,
  MusicXML, kern, ChordPro, Alda, MML...) is named, with ABC's and MusicXML's titles.
- **Video:** MP4/MOV/M4A (brand, length, tracks and codecs, size, iTunes tags), Matroska/WebM (title, length, codecs, size, sound),
  AVI (size, frame rate, length, codecs), FLV, MPEG.
- **Archives and disks:** the counts and sizes above, compression ratio, encryption, comments; Office/OpenDocument/EPUB properties
  (title, author, dates, pages, words); a `.love`'s title and save folder from `conf.lua` (and whether `main.lua` is there); a `.deb`'s
  package, version, architecture and description; zstd, xz, bzip2, lzip, 7-Zip, RPM details; ISO volume, publisher, El Torito, Joliet.
- **Programs:** ELF kind, machine, loader, libraries, stripped or not, and **whether it runs on the R36S** (ARM64 Linux; 32-bit ARM and
  other machines are named as not); Windows PE (machine, GUI or console, .NET, build date), Mach-O, Java class version, WebAssembly, Lua
  and LuaJIT bytecode, scripts' interpreters.
- **Fonts:** TrueType/OpenType/TTC/WOFF names (family, style, version, designer, licence), glyph count, weight; WOFF2 and PSF console fonts.
- **Data and documents:** SQLite (page size, text encoding, writer's version, tables), dBase, Parquet, PDF (version, title, author,
  producer, pages, pictures, encryption), legacy Office, RTF, email (from, to, subject, date) and mbox (message count), BitTorrent
  (name, tracker, files), glTF/GLB, OBJ and STL (vertices, faces, triangles), SAUCE records on text art.
- **Text:** encoding, lines and the longest line, words, line endings, JSON well-formedness, CSV columns, XML root element, and counts
  for subtitles, playlists, calendars, contacts, cue sheets.

Everything reads with limits (32 MiB read for one page, 16 MiB unpacked from one member, 48 MiB to list a `.tar.gz`, 5,000 entries
listed, 24 megapixels decoded), so a huge or damaged file gives a shorter page, never a crash. Nothing is written. Not done: 7-Zip,
xz, zstd, bzip2 and Brotli data are not unpacked (their archives are named and described, not listed), progressive and arithmetic-coded
JPEGs, HEIC/AVIF/WebP pictures and video frames are described but not drawn, sound is not played, and a file inside an archive cannot be
opened from the listing yet.

**What it will and won't do:** copy and move go to the other panel's folder and **never overwrite**: a name that already exists is skipped and
counted in the result. Delete asks first with **No** preselected, says when folders are included, and never follows a link (a link is
copied as a link and deleted as a link). A folder cannot be copied into itself, and `/` is never moved or deleted. Work runs in
slices from the main loop with a progress bar, so B can cancel it (the half-written file is removed). A move on one filesystem is a rename; across
filesystems it copies, then deletes each item only after that item copied without error. Tagged entries are what an action works on; with none
tagged, it is the entry under the cursor. It runs as the `loveos` user, so system folders are read-only to it.

**Checked here** (by `commander --selftest`, run by `scripts/check.sh` and in CI under qemu): the file operations against a temporary folder
(copy, links, skipped names, copy-into-itself, move by rename and by copy, a failed copy keeping its source, delete not following links, cancel,
rename, new folder), the panels (sorting, hidden files, paging), every dialog, the viewer's tabs, and the formats: identification,
Info and Contents of 30 small files made in code by `make_selftest_data.py` (which writes `selftest_data.h`), with every picture
decoder checked against Pillow's pixels. Looked at as pictures
(`R3I6NOS_FM_SHOTS=dir commander --selftest` saves each screen; `commander --view FILE --screenshot out.bmp` draws one file's viewer).
`commander --info FILE` and `--content FILE` print the two pages as text, for trying formats on a laptop. **Not run on a real R36S:** the
buttons, how it looks on the panel, speed on the SD card (and how long a big JPEG or a `.tar.gz` listing takes on the RK3326), and exFAT quirks (no permissions or links there, so copies of links to the card fail and say so).

## Adding an app: its own build, apart from the firmware

Each native app builds on its own; the firmware only bundles the results.

- `apps/<name>-native/app.conf` is the app's manifest (deb name, binary, package, source file, `THEME=1` to compile against `theme/`,
  `DEFAULT=1` to put it in the image, description). `scripts/build_native_app.sh <name>-native [--smoke]` builds that one app in a Debian 12 container
  into `build/native/<NAME>.deb`, and `--smoke` runs its `--selftest` under qemu. `scripts/check.sh` compiles and self-tests every `app.conf`.
- `.github/workflows/native-app.yml` is the reusable build (cache keyed on that app's files, smoke test, artifact `native-<name>-native-arm64`).
  An app's own `.github/workflows/app-<name>.yml` calls it on changes to that app (paths filter), by hand, and on a `<name>-v*` tag, which also publishes
  a GitHub release with the `.deb`. Copy `app-commander.yml` for a new app.
- The firmware (`build.yml`, `scripts/build_native_apps.sh`) builds only the `DEFAULT=1` apps, through the same reusable workflow. An app that is
  not a default (a port, an extra) is released by its own workflow and never enters the image; the user drops its `.deb` in `native_deb/`.
- **LÖVE apps work the same way:** `apps/<name>-love/app.conf` (`TYPE=love`, `NAME`, `RUNTIMES`, optional `TESTS`, `DEFAULT`),
  `scripts/build_love_app.sh <name>-love` (Lua syntax check of every file, the app's tests under luajit, then `build/love-apps/Love <version>/<NAME>.love` per runtime),
  the reusable `.github/workflows/love-app.yml` (artifact `loveapp-<name>-love`) and a caller `app-<name>-love.yml` (tag `<name>-love-v*` releases one `.love` per runtime).
  It needs only python3 and luajit, no container. It does not run the app under real LÖVE (the runtimes are built by the firmware workflow); the app's fake-LÖVE tests stand in for that.
  `configure_rootfs.sh` only copies the built `.love` files into the image.
- **Ports** (open-source programs built from pinned upstream source) have `TYPE=port`, `scripts/build_port.sh` and `port-app.yml`; `apps/_port-template/` says how. They are
  user-installed `.deb` files in `native_deb/`, never part of the image, and may link only what the rootfs has. The first planned one is KlamANSI (`docs/KLAMANSI-PORT.md`).
- **NoCoGaMa** (`docs/NOCOGAMA-PORT.md`) is a user-installed **LÖVE** app of the maintainer's, built in its own private repo as one `.love` per runtime for `Love 11.5/` and `Love 12.0/`; nothing of it is in this repo.
- **Neumetik** (`docs/NEUMETIK-PORT.md`) is a user-installed native `.deb` (`Neumetik.deb`, built and tested under Docker, not yet on a unit): a C and SDL2 host over a Rust static library of Neumetik's crates, built in the Neumetik repo's `r36s/`; nothing of it is in this repo.
- **Crunchy** (`docs/CRUNCHY-PORT.md`) is a user-installed native `.deb` (`Crunchy.deb`, not yet on a unit): a C and SDL2 host over a Rust static library that compiles the desktop Crunchy's own core files, built in the Crunchy repo's `r36s/`; nothing of it is in this repo.
- **Chordica** (`docs/CHORDICA-PORT.md`) is a user-installed native `.deb` (`Chordica.deb`, not yet on a unit): the CHORDiCA 2009 control on the pad, a C and SDL2 host over a Rust static library of Chordica's own engine and the desktop's 2009 voicings, built in the Chordica repo's `r36s/`; nothing of it is in this repo.
- **TinyNTT** (`docs/TINYNTT.md`) is a user-installed native `.deb` (`TinyNTT.deb`, not yet on a unit): "Tiny Ant", an 8-bar, 4-track music loop editor (Song, Instruments, Tracks and Files panels on L1/R1; a piano roll; loop recording from the pad), a C and SDL2 host over a Rust static library of Neumetik's synthesizer, in the TinyNTT repo's `r36s/`. Songs are `.ntt` files in `EASYROMS/TinyNTT/songs`.
- **Command-line tools** (Composer, Painter, Animator; `docs/QJS-CLI-PORTS.md`) are `.deb` packages with `usr/share/r3i6nos/tool`: no menu entry, their commands in `/opt/loveos/bin` on every app's `PATH`. `tools/qjs-node/build.sh` makes them from a Node CLI.
- Not tried yet: the workflows themselves (they run on GitHub; first push will show) and `build_native_app.sh` in a Debian 12 container (no Docker here).

## Trying them on a Mac

```sh
scripts/fetch_love_mac.sh 11.5                 # official release, sha256-pinned -> vendor/love-mac/11.5/love.app
gh auth login                                  # once: the 12.0 build is a CI artifact and needs a login
scripts/fetch_love_mac.sh 12.0                 # the macOS build of the commit build_love.sh pins -> vendor/love-mac/12.0/
R3I6NOS_THEME=theme vendor/love-mac/11.5/love.app/Contents/MacOS/love apps/io-checker-love   # run it
scripts/preview_io_checker.sh 11.5 input      # draws a page once with a fake R36S pad -> build/preview/io-checker-11.5-input.png
scripts/preview_io_checker.sh 12.0 sound
```

`vendor/` and `build/` are gitignored. The preview needs a desktop session (it opens a window for a moment).
The 12.0 download has not been run end to end: artifacts expire after 90 days, and the first run prints the
artifact's sha256 so it can be pinned in `fetch_love_mac.sh`. A Mac cannot run the native ARM64 `.deb`; build the C app
with `cc ... $(sdl2-config --cflags --libs)` instead (see `scripts/check.sh`) and run `--screenshot out.bmp` for a demo frame.


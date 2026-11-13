# The r3i6nos test corpus

One small file of each format the apps on r3i6nos read, **sorted into sub-folders**, for trying each app's file handling on the unit: 253 files in about
1.4 MB. Commander's viewer identifies about 130 formats and the corpus has a file for each; Neumetik, Composer, Painter, Animator, KlamANSI and the menu have
theirs. `MANIFEST.tsv` lists every file: its format, the apps that read it, what made it and, for Commander's, the `Type` line Commander must give it.

**Every file was made in code**: by the apps' own generators and writers (Neumetik, Composer, Painter, Animator), or by `scripts/corpus_formats.py`
(pictures drawn from gradients, tones and a test pattern for the sound and video, archives of a few words, a font of three squares). None of it is anyone
else's music, art or program. The files are MIT, like the rest of r3i6nos (its `NOTICE` says where each came from).

## Where it is

| Where | What |
|---|---|
| `/opt/loveos/test-files/` on the system card | all of it, sub-folders kept, on every card the image is written to (read-only to the apps) |
| `Test files/` on EASYROMS | all of it, sub-folders kept, when the card was made with `flash-macos.sh` |
| `Neumetik/music/Test files/` on EASYROMS | the files Neumetik plays (no sub-folders), so its own file list shows them |
| `KlamANSI/drawings/test-files/` on EASYROMS | the KlamANSI drawing, so its Files panel shows it |

## The folders

| Folder | Files | What is in it |
|---|---|---|
| `music/midi/` | 2 | a Standard MIDI file and a RIFF RMID (Neumetik, Composer) |
| `music/trackers/` | 26 | ProTracker and 8-channel MOD, Scream Tracker 2 and 3 (also with AdLib instruments), FastTracker 2, Impulse Tracker, MultiTracker, Composer 669, OctaMED, Oktalyzer, AHX, HivelyTracker, Sawteeth, Klystrack, V2M, Musicline, ProTrekkr, DigiBooster, PolyTracker, Farandole, UltraTracker, DSMI AMF, Neumetik's `.ntt`, a C64 SID and an NES sound file. The first dozen are real songs (Neumetik plays them); the others are **headers and names only** (Commander's Info shows their titles) |
| `music/notation/` | 13 | MusicXML (and compressed `.mxl`), ABC, Humdrum, ChordPro, Alda, mini-notation, RTTTL, TMF, FlMML, SiON, Composer notation and a cue sheet |
| `music/mml/` | 11 | music macro language in eleven dialects (Mabinogi, ArcheAge, MapleStory 2, GW-BASIC, MSX, N88, C128, ZX Spectrum, Sakura) |
| `audio/` | 9 | WAV, AIFF, FLAC, MP3, Ogg Vorbis, Opus, WavPack, M4A (AAC) and WMA: a second of two tones each, which Commander's Play tab plays |
| `images/` | 25 | PNG (RGBA and interlaced palette), JPEG (baseline, progressive, grey), GIF, BMP (4 and 24 bit), ICO, TGA, PCX, PBM, PGM, PPM, PNM, QOI, Aseprite sprite, TIFF, WebP (lossless and lossy), PSD, SVG, and HEIC and AVIF stubs (only the file-type box: Commander names them, it does not draw them) |
| `art/` | 13 | text-mode art: ANSI (with SAUCE), iCE colours, NFO, DIZ, BIN, XBin (plain and with its own palette and font), ArtWorx ADF, iCE Draw IDF, TundraDraw, ASCII, and the KlamANSI drawing and Painter's ANSI |
| `video/` | 13 | MP4, QuickTime, AVI, Matroska, WebM, FLV, WMV, MPEG-1 program stream, MPEG-2 transport stream, Ogg Theora and 3GP, each two seconds at 64 x 48; and Rec demo's own `.r3f` frames with the `.wav` beside them (Commander plays those without ffmpeg) |
| `documents/` | 18 | PDF, RTF, HTML and XHTML, Word, Excel and PowerPoint (`docx`, `xlsx`, `pptx`), OpenDocument (`odt`, `ods`, `odp`), EPUB, the legacy `doc`, `xls`, `ppt` and an `msi` (OLE compound files), email (`eml`, `mbox`) and Markdown |
| `text/` | 54 | plain and code files, one for each extension Commander knows (C, Python, Rust, Go, Java, JavaScript, Lua, shell, YAML, TOML, INI, SQL, CSS, CSV, TSV, logs, subtitles, playlists, a cue sheet, calendar, contact, TeX, BibTeX, patches, PEM, a desktop entry, a systemd unit, a BDF font), UTF-8 and UTF-16 text, code page 437 text, an empty file and two binary files (the hex view) |
| `data/` | 7 | SQLite, dBase, Parquet (a footer only), BitTorrent, a Windows shortcut, a registry hive and an Animator clip |
| `archives/` | 32 | ZIP, JAR, MSIX, TAR, and tar inside gzip, xz, zstd, bzip2 and lzip; gzip, bzip2, xz, zstd and lzip alone; 7-Zip, RAR 4 and 5, Cabinet, LHA, `ar`, cpio (newc, CRC, portable), RPM, two Debian packages (xz and zstd), and the signatures of ARC, ARJ, ZOO, `.Z`, ACE and DMS |
| `disks/` | 6 | an ISO 9660 image, a disk image with an MBR and one with a GPT, a FAT12 floppy, a VHD and a DMG |
| `programs/` | 11 | an ARM64 Linux program (dynamic, made by gcc), tiny ELF files for ARM64 and x86-64, an AppImage, a Windows PE, Mach-O thin and universal files, WebAssembly, Lua and LuaJIT bytecode and a Java class |
| `fonts/` | 6 | TrueType (a font of three squares), a TrueType collection, WOFF, WOFF2 and PC Screen Fonts 1 and 2 |
| `models/` | 5 | OBJ, ASCII and binary STL, GLB and glTF |
| `apps/` | 2 | a LÖVE app (`hello.love`, for `Love 11.5/` or `Love 12.0/`) and a command-line tool package (`r3i6nos-test-tool.deb`, for `native_deb/`) |

Copy `hello.love` into a `Love` folder, or `r3i6nos-test-tool.deb` into `native_deb/`, to try installing them. Here they are only files: the menu lists apps only from
those folders. **Headers only, said so in the manifest:** the HEIC and AVIF stubs, the old DOS and Amiga archives, the SID, NSF, MED and the other tracker headers, the
registry hive, the shortcut and the Parquet file: Commander identifies them and shows what it reads from the header, and nothing more.

**Not covered:** Neumetik reads about 100 more tracker formats (including 39 MOD packers) that nothing here can write yet. A TinyNTT song (a `.ntt` with TinyNTT's `TINY`
chunk: TinyNTT refuses any other) is made by its own `--make-demo`, not here. Neumetik's SoundFonts, Chordica's and Cue's files and other apps' project files are not
here. 7-Zip, RAR, Cabinet, LHA and ISO files list their members in Commander but cannot yet give them (ROADMAP section 21).

## Rebuilding it

`scripts/build_corpus.sh` makes every file again. It needs the app repositories beside this one (Neumetik with cargo, Composer, Painter, Animator and KlamANSI with node 22+),
and **Docker** (Colima on an Apple chip): Commander's formats are made in a Debian 12 arm64 container (`scripts/corpus_formats_in_docker.sh` runs `scripts/corpus_formats.py` in it),
which has Pillow, ffmpeg, p7zip, lzip, zstd, lua and gcc, **builds Commander and opens every file with `commander --info`, and fails unless each gives the `Type` line
the manifest says**. The script then opens each app-made file with the app that reads it, sorts everything into the sub-folders (`scripts/corpus_extra.py organise`) and replaces
`corpus/files` only if all of them open. A rebuild is byte for byte identical unless an app's output has changed (the ffmpeg files use its bit-exact flags; the zips and tars fixed times).
`corpus/src` holds what is written by hand: the drawing, the cue sheet, the LÖVE app and the tool's command. `scripts/corpus_extra.py` makes the zipped and packaged app files and
the manifest; to add a format, add it to `corpus_formats.py` (the container gives its `Type` line a check) or to `corpus_extra.py`'s `FORMATS` and `WHERE`.

`tests/test_corpus.py` (run by `scripts/check.sh`) checks the manifest against the folders, that every file is committed (`.gitignore` excepts the corpus's `.love`, `.deb`, `.img` and `.zip`),
that nothing is loose at the top, the sizes, the signatures of each family, the parts made here, and, when a C compiler and SDL2 are there, **that Commander identifies each of its 208 files
as the manifest says**.

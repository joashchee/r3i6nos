# The r3i6nos test corpus

One small file of each format the apps on r3i6nos read, for trying each app's file handling on the unit. `MANIFEST.tsv` lists every
file: its format, the apps that read it and what made it.

**Every file was made in code** by the apps' own generators and writers (Neumetik, Composer, Painter, Animator) or written by hand for
this corpus. None of it is anyone else's music, art or program. The files are MIT, like the rest of r3i6nos (its `NOTICE` says where each came from).

## Where it is

| Where | What |
|---|---|
| `/opt/loveos/test-files/` on the system card | all of it, on every card the image is written to (read-only to the apps) |
| `Test files/` on EASYROMS | all of it, when the card was made with `flash-macos.sh` |
| `Neumetik/music/Test files/` on EASYROMS | the music files, so Neumetik's own file list shows them |
| `KlamANSI/drawings/test-files/` on EASYROMS | the KlamANSI drawing, so its Files panel shows it |

Commander can open any of them (text or hex view), wherever they are.

## What it covers

| App | Files |
|---|---|
| Neumetik | MIDI (`.mid`) and RIFF RMID; ProTracker and 8-channel MOD, Scream Tracker 3 (also with AdLib instruments), FastTracker 2, Impulse Tracker, Neumetik's `.ntt`, AHX, Sawteeth, Klystrack, V2M, Musicline, ProTrekkr; music as text: MusicXML (also compressed `.mxl`), ABC, Humdrum kern, ChordPro, Alda, Strudel mini-notation, RTTTL, TMF, FlMML, SiON, and MML in 11 dialects |
| Composer | the MIDI file (lint, notate), Composer notation (`.cmp`), a cue sheet (`.json`) |
| Painter | a PNG and an ANSI file (lint, convert) |
| Animator | an animator-clip (`.json`) |
| KlamANSI | a drawing (`.kan`) |
| The menu | a LÖVE app (`.love`, for `Love 11.5/` or `Love 12.0/`) and a command-line tool package (`.deb`, for `native_deb/`) |
| Commander | UTF-8 text and a binary file |

Copy `hello.love` into a `Love` folder, or `r3i6nos-test-tool.deb` into `native_deb/`, to try installing them. Here they are only files:
the menu lists apps only from those folders.

**Not covered:** Neumetik reads about 100 more tracker formats (including 39 MOD packers) that nothing here can write yet. Each needs
its own small encoder. The ones above are those Neumetik's writers or its tests can make.

## Rebuilding it

`scripts/build_corpus.sh` makes every file again from the apps' repositories beside this one. It opens each file with the app that
reads it, and replaces `corpus/files` only if all of them open. A rebuild is byte-for-byte identical unless an app's output has
changed. `corpus/src` holds what is written by hand: the drawing, the cue sheet, the LÖVE app and the tool's command.
`scripts/corpus_extra.py` makes the zipped, packaged and plain files and the manifest.

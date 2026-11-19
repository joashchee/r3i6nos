# MusiCue

Markers and synced cues on a song you have: the R36S port of the maintainer's web app Cue (https://cue.chordica.com/), rebuilt around an audio file on the card instead of a YouTube
player. Native C and SDL2, MIT. Plan, decisions and what is open: `docs/MUSICUE.md`. **Built and tested in Debian 12 arm64 (Docker) and on the Mac; never run on a real R36S.**

The whole song is **one waveform across the top of the screen (no scrolling) with a moving playhead**; everything else is the r3i6nos-ansiapps text mode in **true colour**,
with gradients and shades made from the chosen theme.

## What it does (the hero list, for Rec demo, rule 11)

1. **The whole song as a waveform** (640x120 pixels, rows 1-6): loud core and quiet halo in a gradient, the played part bright and the part to come dim, a bright playhead with a soft edge.
2. **Markers** from Cue's files: flags on the waveform, a section strip under it coloured by each marker's name (every "Chorus" the same colour), a page-at-a-time marker list, and "Chorus, then Verse 2 in 7s".
3. **Instant jumps** (X, L1, R1): the song is decoded into RAM when it opens, so a jump is a pointer move.
4. **Gapless A/B loops** (Y): set A, set B, and the loop wraps inside the audio buffer with no gap; the span is tinted on the waveform.
5. **Synced cues**: the cue playing now on a gradient bar, those before it fading away above and those to come fading below, with the song's key, tempo and time signature in the frame.
6. **Marking** (Select): stamps the playhead as a new marker, exactly (the app owns the sample clock). Naming from ten preset section names; saved, exported for Cue as `.json`, `.srt` and `.cuebundle`.
7. **Offset**: a per-song shift (+/-30 s in 50 ms steps) for a file that starts earlier or later than the video the cues were made on.
8. **Themes**: 13, with true-colour gradients (Nord is the default). Every text cell on a gradient takes black or white by contrast: all pairs pass 4.5:1 in every theme (`--selftest`).
9. **Sync** (Start, with the sync helper): Cue's server cue files and, with the yt-dlp package, Creative Commons audio, from `inbox/urls.txt`.

## The pad

| | |
|---|---|
| A | play / pause (the songs list: open) |
| B | back to the songs (the songs list: quit) |
| X | jump to the selected marker and play |
| Y | loop: set A, set B (looping), clear |
| L1 / R1 | the previous / next marker (L1 returns to the start of the current marker, or to the previous one when within 1.5 s of its start) |
| D-pad up/down | select a marker (pages of ten, no scrolling) |
| D-pad left/right | back / forward 1 s |
| L2 / R2 | back / forward 10 s |
| Select (tap) | mark the playhead |
| Start | the menu: mark, name, delete, offset, save, export, theme, back |
| Select + Start | leave |

## Files (app data, rule 16: `$R3I6NOS_APP_DATA`, the data card's `MusiCue/` first)

| Folder or file | What |
|---|---|
| `media/` | your audio files (m4a, mp3, ogg, opus, flac, wav, aac, wma, mka, webm, mp4); a name with `[<videoId>]` in it (yt-dlp writes `Title [id].m4a`) is matched to Cue's data for that id, any other name gets a local id |
| `cues/<id>.json` | a song's markers, captions and metadata in Cue's version-data shape; `cues/<id>.srt` is read when the `.json` holds markers only (Cue's server pair); `cues/<id>.offset` is the offset |
| `cues/.merge-conflicts/` | an imported bundle that disagrees with a file already there is kept here, never written over |
| `inbox/` | `.cuebundle` and `.json` files to import (Start > Import), and `urls.txt` for the sync |
| `export/` | what Start > Export writes for Cue: `<id>.json`, `<id>.srt`, `<id>.cuebundle` |
| `settings.conf` | the theme and whether the sync may fetch any video |

Cue's rules travel with its data: the five metadata fields (`key`, `tempo`, `sig`, `scripter`, `created`) are fixed and cannot be added to by any file; `created` is only ever set by an export.
Nothing here sends anything: no counter, no account, no telemetry (CLAUDE.md rule 6). Only the sync touches the network, and only when a person starts it.

## Sound

A WAV is read here; every other format is decoded by the image's one `/usr/bin/ffmpeg` (`R3I6NOS_FFMPEG` names another) on a pipe, as Commander's Play tab does. The track is held as 16-bit stereo at
44.1 kHz: **a 4-minute song is 42 MB, 12 minutes (the limit) 121 MiB**. A longer file is cut at 12 minutes and says so. If the sound device will not open the app runs on its own clock and says "no sound device".

## One departure from the theme's rules, said as the theme asks

Rows 1-6 are drawn in pixels, not cells (`wave.h`), so the screen mirror (`docs/SCREEN-MIRROR.md`) shows those rows as blank cells; the mirror carries the text and not the waveform. Nothing else leaves the grid.

## Still open (ROADMAP section 36)

The device report with its power section (rule 9, 14), Rec demo (rule 11), the hot-plug toast wiring in the sync, the on-screen keyboard for naming a marker freely (the presets and a USB keyboard's
`M` for now), and every number about the unit: decode time, memory, the sound device, the pad.

## Build and test

`scripts/build-native-local.sh musicue-native` (Docker; builds the `.deb` and runs `--selftest` under qemu), or on a Mac with SDL2: `cc -std=c11 -Wall -Wextra -O2 -I theme $(sdl2-config --cflags) apps/musicue-native/musicue.c
$(sdl2-config --libs) -lm`, then `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./a.out --selftest`. `--screenshot out.bmp [--screen library|player|menu] [--theme NAME]` draws a screen with a synthetic demo
song; `--make-demo DIR` writes that song and its cue file. `tests/test_musicue_sync.py` tests the sync helper (a local server and a fake yt-dlp).

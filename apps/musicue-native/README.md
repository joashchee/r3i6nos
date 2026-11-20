# MusiCue

Markers and synced cues on a song you have: the R36S port of the maintainer's web app Cue (https://cue.chordica.com/), rebuilt around an audio file on the card instead of a YouTube
player. Native C and SDL2, MIT. Plan, decisions and what is open: `docs/MUSICUE.md`. **Built and tested in Debian 12 arm64 (Docker) and on the Mac; never run on a real R36S.**

The whole song is **one waveform across the top of the screen (no scrolling) with a moving playhead**; everything else is the r3i6nos-ansiapps text mode in **true colour**,
with gradients and shades made from the chosen theme.

## What it does (the hero list, for Rec demo, rule 11)

1. **The whole song as a waveform** (640x120 pixels, rows 1-6): loud core and quiet halo in a gradient, the played part bright and the part to come dim, a bright playhead with a soft edge.
2. **Markers** from Cue's files: flags on the waveform and a page-at-a-time list, "Chorus, then Verse 2 in 7s". A marker is a bookmarked cue (the menu's Bookmark on a cue row, as Cue's marker box).
3. **Instant jumps** (D-pad Left and Right to the cue before and after, the Jump panel's eight one-touch buttons): the song is decoded into RAM when it opens, so a jump is a pointer move.
4. **Gapless A/B loops** (R1+D-pad Up sets A, R1+D-pad Down sets B, R1+Start switches the loop, R1+Select goes to A): the loop wraps inside the audio buffer with no gap; the span is tinted on the waveform.
5. **The cue panel** (rows 7-8, as Cue's): the cue playing now, the next one muted, a beat dot that pulses with the Tempo, and at the last cue the Assisted Cue entry's two bars draining to the middle.
6. **Cueing and Tempo** (Rec mode, L1+Start, then D-pad Down; D-pad Up takes the last cue back): a cue at the playhead, exact (the app owns the sample clock). Cue's Tempo logic: a cue's length is 16 beats (4/4) or 8 (6/8), so each cue has its own tempo, shown as a green, yellow or red dot against the Tempo; an unset Tempo is suggested from the most common cue tempo. Saved, exported for Cue as `.json`, `.srt` and `.cuebundle`.
6b. **Six panels, two at a time** (rows 9-20): Markers, Cues (with the tempo dots), Metadata (key, tempo, sig, scripter, created), Set List, Last 10 cued and Top 10 cued (the songs opened on this unit, never sent anywhere). Fn turns the carousel, which has a seventh place, the Jump panel; the right stick moves in a panel and hops between the two.
6c. **The Jump panel** (the whole width of rows 9-20): every marker with Cue's playback-progress fill (the section the playhead is in fills as the song plays), eight buttons bound to markers for one-touch jumps ([^] [v] [<] [>] [A] [B] [X] [Y] tags on the rows), and Cue's A-B progress bar.
6d. **The Piano** (the Key field in Metadata): one octave, C3 to B3, on the pad, sounding over the song, to find a key by ear; a note names the key's root.
6e. **Naming a marker with a number or a letter**: Right on a preset (Verse, Chorus ...) asks for a number or a letter after it with Up and Down: Verse 1, Chorus 1a.
7. **Offset**: a per-song shift (+/-30 s in 50 ms steps) for a file that starts earlier or later than the video the cues were made on.
8. **Themes**: 13, with true-colour gradients (Nord is the default). Every text cell on a gradient takes black or white by contrast: all pairs pass 4.5:1 in every theme (`--selftest`).
9. **Sync** (L1+R1 menu, with the sync helper): Cue's server cue files and, with the yt-dlp package, Creative Commons audio, from `inbox/urls.txt`.
   **Add: YouTube ID** (menu, first item): pick the 11 characters on an on-screen keyboard (a 16 by 4 grid: D-pad moves, A types, X or Y erases, Start fetches; a USB keyboard types it too); online, it fetches that one video's cues and audio at once and opens the song; offline, it keeps the id in `inbox/urls.txt` for the next Sync.

## The pad (the mapping of `docs/r3i6nos-mapping.md`, and the maintainer's map of 2026-11-20)

The system parts are the same in every r3i6nos app: **L1 + R1 the main menu** (About, Colour theme, Device report, Rec demo, Quit, then Songs and sync / Song options; Benchmark is hidden, MusiCue
has nothing to measure yet), **L2 + R2 (or Y) the context menu** for what the cursor is on, **Fn the next panel, L1 + Fn the first, R1 + Fn the one before**, the right stick as the D-pad (past the dead
zone 0.35, at most 6 steps a second), **R1 held is "increase"** (Page up and down, Home and End on lists). Quit is in the main menu; Select + Start no longer leaves. There is no virtual mouse or keyboard.

**Start and Rec mode**

| | |
|---|---|
| **Start** | play in **Play mode**; playing, stop |
| **L1 + Start** | play in **Rec mode** (REC in the top bar, `*` on the command line); playing, stop. Stopping ends Rec mode |
| Select | back to 0:00 |

**D-pad, by mode** (the Jump panel overrides both, below)

| | Rec mode | Play mode |
|---|---|---|
| Down | **add a cue** at the playhead | the **last** cue |
| Up | **take the last cue added** in this take back | the **first** cue |
| Left / Right | the cue before / after | the cue before (inside a cue's first 1.5 s) or its start / the cue after |

**A-B looping (R1 held, any panel)**

| | |
|---|---|
| R1 + Up / Down | set A / set B (B must be after A; an A past B clears B) |
| R1 + Start | start or stop the loop, if A and B are valid (A and B are kept; starting goes to A if the playhead is outside) |
| R1 + Select | jump to the start of the loop |
| R1 + Left / Right | 10 s back / on |

**On the Jump panel** the D-pad and the face buttons are its eight buttons: **stopped**, a press **binds** the marker the right stick has picked (the same button on the same marker frees it); **playing**, a
press **jumps** to the marker bound. The rows carry the tags ([^] [v] [<] [>] [A] [B] [X] [Y]); the bindings are kept per song in `cues/<id>.jump`. B does not leave the song there (the main menu's Song options does).

**On the Piano** (the Key field in Metadata) the buttons are the notes: D-pad Up C3, Left D3, Down E3, Right F3; Y G3, B A3, A B3, X A#3; L1 C#3, L2 D#3, R2 F#3, R1 G#3; **Select Oct-, Start Oct+**, **Fn closes** (and opens) the
piano; then A keeps the key and B cancels. **Elsewhere:** A acts on the selection of the panel the stick is in, B back to the songs, X says delete is in the context menu, Y the context menu.

Keyboard: arrows are the D-pad, Enter or Z is A, Esc is B, Space is Start, Backspace is Select, Tab is Fn, Left Shift is R1 and Left Ctrl is L1 (held), I K J L are the right stick, M marks, F10 the main menu, F9 the context
menu, Q leaves.

## Files (app data, rule 16: `$R3I6NOS_APP_DATA`, the data card's `MusiCue/` first)

| Folder or file | What |
|---|---|
| `media/` | your audio files (m4a, mp3, ogg, opus, flac, wav, aac, wma, mka, webm, mp4); a name with `[<videoId>]` in it (yt-dlp writes `Title [id].m4a`) is matched to Cue's data for that id, any other name gets a local id |
| `cues/<id>.json` | a song's markers, captions and metadata in Cue's version-data shape; `cues/<id>.srt` is read when the `.json` holds markers only (Cue's server pair); `cues/<id>.offset` is the offset, `cues/<id>.jump` the Jump panel's bindings |
| `cues/.merge-conflicts/` | an imported bundle that disagrees with a file already there is kept here, never written over |
| `inbox/` | `.cuebundle` and `.json` files to import (the main menu's Songs and sync > Import), and `urls.txt` for the sync |
| `export/` | what Song options > Export writes for Cue: `<id>.json`, `<id>.srt`, `<id>.cuebundle` |
| `settings.conf` | the theme, whether the sync may fetch any video, and whether it also saves the video |
| `video/` | videos saved by Songs and sync > "Also save the video (480p)" (off by default): H.264 + AAC mp4, at most 480p and 30 fps, named `Title [<videoId>].mp4`; kept out of `media/` so the song is not listed twice; play them in Commander |
| `setlist.tsv` | the Set List: one song a line, its id and its name (the menu adds, removes and moves) |
| `history.tsv` | the songs opened here: id, when last, how many times, name (Last 10 and Top 10 cued) |

Cue's rules travel with its data: the five metadata fields (`key`, `tempo`, `sig`, `scripter`, `created`) are fixed and cannot be added to by any file; `created` is only ever set by an export.
Nothing here sends anything: no counter, no account, no telemetry (CLAUDE.md rule 6). Only the sync touches the network, and only when a person starts it.

## Sound

A WAV is read here; every other format is decoded by the image's one `/usr/bin/ffmpeg` (`R3I6NOS_FFMPEG` names another) on a pipe, as Commander's Play tab does. The track is held as 16-bit stereo at
44.1 kHz: **a 4-minute song is 42 MB, 12 minutes (the limit) 121 MiB**. A longer file is cut at 12 minutes and says so. If the sound device will not open the app runs on its own clock and says "no sound device".

## One departure from the theme's rules, said as the theme asks

Rows 1-6 are drawn in pixels, not cells (`wave.h`), so the screen mirror (`docs/SCREEN-MIRROR.md`) shows those rows as blank cells; the mirror carries the text and not the waveform. Nothing else leaves the grid.

## Still open (ROADMAP section 36)

The device report with its power section (rule 9, 14), Rec demo (rule 11), the hot-plug toast wiring in the sync, the on-screen keyboard for naming a marker or a cue's words freely (the presets, the pad's letter picker for metadata and a USB keyboard for now), and every number about the unit: decode time, memory, the sound device, the pad.

## Build and test

`scripts/build-native-local.sh musicue-native` (Docker; builds the `.deb` and runs `--selftest` under qemu), or on a Mac with SDL2: `cc -std=c11 -Wall -Wextra -O2 -I theme $(sdl2-config --cflags) apps/musicue-native/musicue.c
$(sdl2-config --libs) -lm`, then `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./a.out --selftest`. `--screenshot out.bmp [--screen library|player|player:N|menu|context|mainmenu|about|jump|rec|piano|suffix] [--theme NAME]` draws a screen with a synthetic demo
song; `--make-demo DIR` writes that song and its cue file. `tests/test_musicue_sync.py` tests the sync helper (a local server and a fake yt-dlp).

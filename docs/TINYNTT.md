# TinyNTT ("Tiny Ant"): an 8-bar music loop editor

Status (2026-11-10): **the first version is built in the TinyNTT repo's `r36s/` and tested without the unit (its README has the
detail); never run on a real R36S.** TinyNTT is a new ansiapps app, R36S first and other platforms later: short, seamless,
8-bar loops for games, made on the spot on the pad (a game jam's music). It plays through Neumetik's synthesizer, by synthesis
only (no SoundFonts), and saves Neumetik Tracker `.ntt` files. This file is the plan and the decisions, as
`docs/NEUMETIK-PORT.md` is for Neumetik; the code is in the TinyNTT repo, never here (rule 2). `ROADMAP.md` section 17 tracks it.

## 0. Decisions

The brief is the maintainer's (2026-11-10). What it left open was decided as below; **the ones marked open need the maintainer.**

1. **Shape: a C and SDL2 host linking a Rust static library**, as Neumetik and Chordica. The library depends on `neumetik-midi`
   (the synthesizer) and `neumetik-tracker` without its repository or cover painter (UMF, for the `.ntt`), both unchanged.
2. **Where the code lives: its own repo, `~/Coding/tinyntt`** (maintainer, 2026-11-10; first built in the Neumetik repo's `tinyntt/`, then moved), in `r36s/` as the other ports. It builds against `../neumetik` (the `midi/` and `tracker/` crates, unchanged, and the licence list its NOTICE follows) and `../r3i6nos` (the theme). Its local build script is `r36s/build-deb.sh` (rule 10).
3. **Rule 8 with one departure, asked for in the brief:** row 0 is the panel carousel (the panel before with `L1`, the one
   showing in the middle, the one after with `R1`), so the app's name and the battery are not on it. Everything else is the
   theme's: 64 x 24 cells, the display view, the three-row command line, list dialogs by whole pages, nothing scrolls (the
   piano roll moves by whole bars and by keys, never smoothly), the 16 colours in pairs that pass 4.5:1.
4. **The four tracks and what they pick from.** Melody, Harmony, Bass: the 265 tonal patches of Neumetik MIDI's built-in
   libraries (General MIDI, Roland GS's variations, the classic banks of the 1980s, 1990s and 2000s). Rhythm: the 68 atonal
   ones (the 16 drum kits, then General MIDI's unpitched percussion and sound effects and their GS variations).
5. **Metadata** follows what Neumetik's tracker and its music catalogue keep: name, composer, game or project, licence,
   category (what the music is for in a game), style, mood, energy, scale, key, time signature, tempo, quantize, metronome,
   description, created and modified. Diskette reads a `.ntt`'s title, artist and message, which TinyNTT fills from these.
6. **Styles are TinyNTT's own:** 20 of them, each a bar of drums, a bass line, a chord rhythm, a two-bar tune and four bars of
   chords, written as steps and made in the song's key, scale and meter. They are common rhythms, not anyone's song (rule 2).
7. **The file: a real `.ntt` plus a `TINY` chunk** (section 5). Neumetik opens and plays what TinyNTT saves; TinyNTT opens only
   what it saved, and says why not otherwise.
8. **Exports: WAV, MIDI and TMF** (Neumetik's MIDI as text). The WAV is the loop itself: the song is played twice and the
   second pass kept, so the reverb and notes ringing past the end are already sounding at the start.
9. **Open: the release terms.** `r36s/package/copyright` carries Neumetik's grant (free on the R36S, unmodified; the music
   made is the composer's) marked **DRAFT**; `TINYNTT_RELEASE=1` refuses to build until the maintainer confirms it.
10. **Open: Select and Start.** The brief makes Select rewind and Start play, and every r3i6nos app leaves on Select + Start.
    Both hold: pressing the second while the first is down does nothing but start a one-second count, after which the app
    leaves. The first button pressed still acts (a rewind, or a play that goes on for that second).
11. **Open: a GitHub workflow.** None was added; the package is built locally.

## 1. Shape

- **`libtinyntt.a`** (`r36s/lib`): `song.rs` (the song and its edits), `scales.rs`, `patches.rs`, `styles.rs`, `engine.rs`
  (the synthesizer, the loop, recording), `file.rs`, `bench.rs`, `capi.rs` (`tinyntt.h`). Standard library only. The binary
  needs only SDL2, glibc and libgcc, all in the rootfs.
- **`tinyntt`** (`r36s/host`): `tinyntt.c`, `app.h`, `panels.h`, `input.h`, `selftest.h`, with `theme/r3i6nos_ansiapps.h`
  and `theme/r3i6nos_report.h` read from this repo. Sound through SDL2's audio callback at 44100 Hz, stereo float, a
  **256-frame buffer** (Files > Sound buffer: 128 to 1024).
- **Time is the engine's.** Notes are started in the audio callback to within 32 frames (0.7 ms), not on the screen's timer. A
  pad press is applied under SDL's audio lock before the next buffer. A recorded note is placed two buffers earlier than the
  playhead, which is roughly when the player heard the beat they played to.
- **The card never holds the sound up:** saving, opening and exporting work on a copy of the song outside the audio lock; the
  WAV export and the device report's slow part run on a thread.
- **Fast start:** the splash shows at once with a bar (settings, the synthesizer, the sound, the song).

**Unknown until the unit runs it:** whether the Cortex-A35 plays four busy tracks with reverb in a 256-frame buffer (Neumetik's
report: its MIDI test ran at 3.4x real time on the unit, 18 times slower than a Mac), what buffer ALSA gives, and whether the
two-buffer guess puts recorded notes where they were meant.

## 2. The screen

| Rows | What |
|---|---|
| 0 | The panel carousel: `◄ L1 Files`, ` Song ` (blue on white, centred), `Instruments R1 ►` |
| 1-20 | The panel |
| 21 | Reply: what the last action did, or an error |
| 22 | Prompt: what the buttons do here; `R` while recording, `►` while playing |
| 23 | Status: STOP, PLAY, ARMED, COUNT n, REC or PREVIEW; bar and beat; tempo, meter, key and scale; the file's name, `*` when changed |

- **Song** (the first panel): its 20 fields, one a row.
- **Instruments:** four boxes of 16 columns, Rhythm, Bass, Harmony, Melody: the instrument and its library, then volume, pan,
  reverb, chorus, mute, solo, preview, octave, shift, a level meter; rows 17-20 are hints, or the eight note buttons and what
  each plays when the keyboard is on.
- **Tracks:** rows 1-4 the tracks (Melody, Harmony, Bass, Rhythm), a yellow dot at the left of the selected one, then its
  instrument, volume, `m`/`M`, `s`/`S`, Draw, Clear, Quant, Fill, Undo. Row 5 the roll's ruler (the octave and shift, bar
  numbers, the playhead, punch markers); rows 6-20 fifteen keys (a kit: its drums by name), four columns of key names and 60
  of steps: three and three-quarter bars of 4/4 at sixteenths, turning by whole bars. A note's first cell is a full block and
  the rest half blocks, in the track's colour; the key's own note is yellow, notes outside the scale grey.
- **Files:** the list at the left, the song's state at the right (file, saved or changed, autosave, length, notes a track).

## 3. Controls (raw buttons, `docs/hardware-findings.md`)

| Button | Does |
|---|---|
| L1 / R1 | the panel before / after; they go round |
| Start | play / stop. Armed: a bar of count-in first (the bar before the playhead's, or the click alone at bar 1), then record |
| Select | back to bar 1 |
| Fn (raw 16) | Tracks: arm or disarm; while playing, punch-in, then punch-out, then clear. Instruments: the keyboard on / off |
| Select + Start, a second | leave (unsaved work is kept for the next start) |
| Volume keys | the volume (keyboard keys on this unit, not yet seen as events) |

| | Song | Instruments | Tracks | Tracks, Draw |
|---|---|---|---|---|
| Left stick | as the D-pad | left/right a column, up/down a setting | up/down a track, left/right an item | the same |
| D-pad | up/down a field, left/right change it | up/down a setting, left/right change it | up/down a track, left/right change the item | move the cursor |
| Right stick | | left/right shift, up/down octave | up/down the roll an octave, left/right a key | the same |
| A | type it, or the list | the instrument list; flip a switch | use the item | start a note, then end it; drop |
| B | | stop the previews | | cancel, or leave Draw |
| X / Y | | mute / solo | mute / solo | delete / pick up and drop |
| L2 / R2 | a number by 10 | by 10 (instruments: a page) | the playhead a bar | the cursor a bar |

**The note buttons** (while armed in Tracks, or with the keyboard on in Instruments), lowest note first: **Up, Left, Down,
Right, Y, B, A, X**: eight notes in a row of the song's scale from the key's note in the track's octave, moved by `shift` scale
steps. Major, C, octave 2, shift 0 is C2 D2 E2 F2 G2 A2 B2 C3; shift 1 is D2 to D3. On a drum kit they are eight drums a page
(kick, snare, closed and open hat, two toms, crash, ride first). The right stick moves shift and octave.

**Recording** goes round the loop: Melody and Bass replace what a new note overlaps, Harmony and Rhythm add. With Quantize
recording on, a note's start goes to the nearest grid step and its length to whole steps. Undo takes a whole take off a track.

## 4. Files on the games card

Only under `$R3I6NOS_ROMS/TinyNTT/` (`/roms/TinyNTT/`): `songs/` (`.ntt`, and each one's last save as `.ntt.bak`), `exports/`,
`autosave.ntt`, `settings.txt` (volume, buffer, the last song), `r36s-report.txt`. Every file is written to a temporary name,
synced and renamed. Nothing outside that folder is written (rule 7).

**Autosave and recovery:** `autosave.ntt` is written four seconds after the song last changed (every 30 s while it keeps
changing, never mid-take), and on leaving with unsaved work. A save removes it. A start that finds one opens it, marks the song
unsaved and says so: a crash, a flat battery or a pulled plug loses seconds. A failed autosave is said once on the reply row
and stays on the Files panel.

## 5. The `.ntt`

UMF with its samples (`neumetik-tracker`'s `umf::write_ntt`): 8 patterns, one a bar, 24 rows a quarter note, modern tempo; a
channel for each note a track sounds at once (one for Melody and Bass, up to eight for the others); four instruments, each a
one-frame sample with a MIDI output (channel, program, bank), which is how Neumetik plays a synthesizer patch from a pattern.
The title, artist and message carry the name, composer and the rest. **`TINY`**, an extra chunk UMF readers pass over and keep,
holds the song as text (`TinyNTT 1`, `key=value` lines, a `track` line and `n at len key vel` lines): everything exactly,
including notes off the 4-tick rows. **A `.ntt` with no `TINY` chunk is refused:** "Not a TinyNTT song ... open it in
Neumetik", and nothing changes.

## 6. Testing

- **The library** (`cargo test`, 35 tests): the brief's button example; every scale and style well formed, every style in key in
  five scale, key and meter combinations; mono overwrite and poly add; notes over the loop's end; the count-in, a take, an
  overdub, punch markers; previews on one clock; undo; the level under full scale; the `.ntt` read back the same, and **played
  by Neumetik's own tracker: a note-on for every note, each track's channel and program, 16 s for 8 bars at 120**; a foreign
  and a damaged `.ntt`; MIDI and TMF read back; the WAV's exact length and its join; the C API.
- **`tinyntt --selftest [--shots DIR]`:** every panel through the pad's buttons with no sound device (the engine rendered by
  hand, so time is exact): typing a name, lists, the carousel both ways, previews, the keyboard, Fill, Clear, Undo, drawing,
  moving a note by its middle, end and start, deleting, a recorded take and an overdub, punch markers, save, `.bak`, the three
  exports, the library, the foreign-file warning, **a simulated crash and its recovery**, the report, Select + Start.
- **`r36s/build-deb.sh --clean-room`:** section 8.
- **Not testable here:** the engine's speed and the callback's load on the unit, latency felt, the sticks' directions and Fn
  as events, the roll on the panel.

## 7. The device report (rule 9)

Files > Device report and `tinyntt --report [SECONDS]` write `/roms/TinyNTT/r36s-report.txt`: the system and the pad's GUID,
the card; start-up; **the pad** (17 buttons for Fn, 4 axes for the sticks); **the sound device** and the buffer given (goal 512
frames or fewer); the callback's pace; the screen's paint (goal 10 ms); **the speed test** at that buffer, three busy 4-track
songs (goals 2x real time and 99% of buffers in half their time) and the WAV export's time from it (goal 60 s for a 16 s loop);
**a song saved to the card**, synced and read back (goal 1 s), a MIDI export; peak memory.

## 8. Phases (mirrored in `ROADMAP.md` section 17)

1. **Built** (2026-11-10): the library, the host, the package, the self-test, the report. `r36s/build-deb.sh --clean-room` built `TinyNTT.deb` (3.4 MB; needs SDL2, libc, libm, libgcc_s; newest glibc symbol 2.34) in Debian 12 arm64, its self-test passed there, `deb_watcher.py` installed it in a clean Debian 12 and the launcher ran it, and a device report there had 21 PASS and 2 FAIL (both for no pad under Docker). Those figures are a Mac's under Docker, not the unit's: 79x to 148x real time with up to 19 voices, every 256-frame buffer in under 5% of its time, a save in 43 ms, peak memory 14 MB. The Linux run found a bug the Mac did not (reopening the last song at start copied a path onto itself; fixed).
2. **On the unit:** `TinyNTT.deb` to `native_deb/` (`flash-macos.sh` does it), Files > Device report, bring the report back;
   then make a loop: the sticks, Fn, the note buttons' feel, whether takes land on the beat, the roll on the panel.
3. **The open decisions** (section 0): the release terms, Select and Start, a workflow.
4. **Later:** velocity per note and swing; copy, paste and transpose of bars; more styles; Ogg or FLAC export; opening its
   songs from Neumetik's player on the unit; other platforms (the brief: R36S first).

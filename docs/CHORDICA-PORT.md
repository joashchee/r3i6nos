# Chordica on the R36S: the plan

Status (2026-11-10): **the first version is built in the Chordica repo's `r36s/` and tested without the unit (its README has the
detail); never run on a real R36S.** Chordica is Scoreless Music's and ansiapps' musical instrument (`~/Coding/chordica`, closed
source; on Apple's App Stores as `com.scorelessmusic.chordica` and on Google Play as `com.ansiapps.chordica`): played live from a
keyboard the CHORDiCA way, composed in a shared score, played together across devices. Its desktop app is Tauri, React and Rust,
and all its sound and timing are Rust crates with no Tauri in them. This file is the plan and the decisions, as
`docs/NEUMETIK-PORT.md` is for Neumetik; the code lives in the Chordica repo (an `r36s/` folder), never here (rule 2).
`ROADMAP.md` section 16 tracks it.

## 0. Decisions

Taken without the maintainer on 2026-11-10 (asked for: "start a new r36s port for CHORDiCA, following all the practices of r36s
ports"), each the port convention's default; **the ones marked open need the maintainer.**

1. **Where the code lives: the Chordica repo**, `r36s/`, as Neumetik's and Crunchy's. This repo keeps the plan, the theme, the pad
   mapping and `flash-macos.sh`'s entry.
2. **Shape: a C and SDL2 host linking a Rust static library**, as Neumetik and Crunchy (section 1). The library depends on
   Chordica's own `chordica-engine` crate, unchanged, and compiles the desktop's `src-tauri/src/chordica2009.rs` by path, unchanged:
   a fix on the desktop is a fix here, and a button plays the same notes on every device.
3. **The first version is the CHORDiCA 2009 control**, the play screen of the 2009 iPhone app, because it is the one Chordica
   control made for buttons held in two hands: its grid is four rows of three buttons a hand, and the R36S has four buttons under
   each thumb and two shoulders under each index finger (section 3). The mode keyboard (four rows of computer keys, 45 notes),
   Compose and the score, sessions with other devices, Notation, Cue and SoundFonts are later (section 5).
4. **Rule 8 with no departure:** 64 x 24 cells, the top bar, the display view (the grid), the three-row command line, list
   dialogs by whole pages, nothing scrolls. The 2009 buttons keep their colours, as VGA pairs that pass 4.5:1.
5. **Open: the release terms.** Chordica is sold on the App Stores. `r36s/package/copyright` carries Neumetik's grant (free on the
   R36S, unmodified) marked **DRAFT**; `CHORDICA_RELEASE=1` refuses to build until the maintainer confirms or changes it.
6. **Open: the pad mapping** (section 3) is a first guess at holding the 2009 grid in two thumbs and two index fingers. Only a
   player on the unit can say whether the rows, the shoulders and Select as M feel right; it is one table in `r36s/lib/src/pad.rs`.
7. **Open: a GitHub workflow.** Chordica's repo has no CI; none was added. The package is built locally (`r36s/build-deb.sh`).
8. **Committed in the Chordica repo** (maintainer, 2026-11-10): the work in progress there (the icon, SoundFonts through Neumetik
   MIDI) in a commit of its own, then `r36s/` with its CHANGELOG and ROADMAP lines.

## 1. Shape

- **`libchordica_r36s.a`** (`r36s/lib`, crate `chordica-r36s`): `chordica-engine` (the realtime mixer, its live-key queue that
  is drained before anything else in a buffer, Neumetik MIDI's GM and GS instruments), the desktop's `chordica2009.rs` (the 2009
  voicing table), `play.rs` (what the desktop's `chordica2009_key` and `chordica2009_off` commands do: each hand plays one thing
  at a time, a press lets go of the hand's last, letting go leaves it ringing; each hand a part of its own), `pad.rs` (the grid on
  the pad, the 2009 menu, the names), `bench.rs` (the speed test), `capi.rs` (`chordica_r36s.h`). Dependencies are the engine's own
  (Neumetik's crates, from `../neumetik` as the desktop has them) and serde for `chordica2009.rs`'s derive; nothing new comes in.
  The binary needs only SDL2, glibc (2.36 or older symbols) and libgcc, all in the rootfs.
- **`chordica`** (`r36s/host/chordica.c`): the host, with `theme/r3i6nos_ansiapps.h` and `theme/r3i6nos_report.h` read from this repo.
  Sound through SDL2's audio callback at 44100 Hz (what Neumetik's report found the unit's ALSA running at), stereo float, a
  **256-frame buffer** (5.8 ms; Start > Sound buffer: 128 to 1024); the device may give another size, and the status row and the
  report say what it gave. The desktop runs at 128 frames.
- **Latency first** (the desktop's rule 1): a press is queued on the engine's live-key queue from the event loop at once, before the
  screen is drawn; nothing on its path allocates, locks or waits. The event loop wakes on the event, not on a frame timer.
- **Fast start:** the splash shows at once with a bar (the synthesizer, the settings, the sound); nothing else holds the first screen.

**Unknown until the unit runs it:** whether the Cortex-A35 renders Neumetik MIDI fast enough at a small buffer (Neumetik's report:
its MIDI test song ran at 3.4x real time on the unit, 18 times slower than a Mac; this port's speed test runs at 43x on a Mac, so
perhaps 2.4x on the unit, which is tight for a 256-frame buffer), what buffer ALSA really gives through r3i6nos's `asound.conf`, and
the delay the codec adds after it.

## 2. The screen

| Rows | What |
|---|---|
| 0 | `Chordica · CHORDiCA 2009` at the left; at the right the key playing (and its move by the 2009 menu: `Key F (C+5)`), form and rest |
| 1 | `Bass` over the left grid, `Chords` over the right |
| 2 | Each grid's columns headed by their shoulder (`L2`, `none`, `L1`; `R1`, `none`, `R2`), the column the held shoulder picks in black on cyan |
| 3-17 | The 2009 grid: two hands of 3 x 4 buttons, each 9 x 3 cells in its 2009 colour, its degree (`b7`, `I sus2`) and what it plays in the key (`Bb`, `Csus2`); a held button black on cyan; the D-pad's arrows at the left edge and X Y A B at the right. While Select is held, the 2009 menu's labels (`Form +`, `Key b7`, `Ex`) instead |
| 19-20 | The chords' sound and the bass's |
| 21 | Reply: the last setting changed, or an error |
| 22 | Prompt: what the last press played (`Chord C (I)  G3 C4 E4`) |
| 23 | Status: volume and velocity; the buffer the device gave and the callback's load |

The Start menu, its pickers (Sound lists Neumetik MIDI's 317 patches, 18 a page) and How to play (two pages) are list dialogs and a
box over the grid.

## 3. Controls (raw buttons, `docs/hardware-findings.md`)

| Button | Plays |
|---|---|
| D-pad up / left / right / down | the bass grid's rows 1 / 2 / 3 / 4 |
| X / Y / A / B | the chord grid's rows 1 / 2 / 3 / 4 |
| no shoulder | the middle column: bass 1#, 1, 7, 6; chords VI, I, V, vi |
| L1 / R1 held | the column nearer the middle: bass 4#, 4, 3, 2; chords III, I sus2, bVII, bVI |
| L2 / R2 held | the outer column: bass M (let go of every note), 5, b7, b6; chords II, I sus4, iii, ii |
| Select held | the 2009 menu (M): the same buttons are Form + and -, Rest + and -, the key moved by a degree, Key 0, Ex |
| Start | the menu: Sound, Bass sound, Key, Form, Rest, Velocity, Volume, Sound buffer, Let go of every note, How to play, Device report, Quit |
| Select + Start | quit |
| Volume keys | the volume (they are keyboard keys on this unit, not yet seen as events) |

As in 2009, form 1 plays a chord's top two notes on the press and its third as the button is let go; the bass plays nothing on
letting go. The 2009 menu's metronome, take and playback buttons are blank until the score comes (section 5).

## 4. Files on the games card

Only under `$R3I6NOS_ROMS/Chordica/` (`/roms/Chordica/`): `settings.txt` (key, form, rest, velocity, volume, buffer, the sounds by
bank and program), written whole and renamed into place, five seconds after a change at most and on quit, never on a press's path;
`r36s-report.txt`. Nothing outside that folder is written (rule 7).

## 5. Left out of the first version

The mode keyboard (CHORDiCA's four rows of a mode: 45 notes need a scheme of their own on 12 buttons), Compose, the score and
recording takes (the 2009 menu's take and playback buttons), the metronome, sessions with other devices (Wi-Fi only when the player
chooses it, rule 6; the session code and encryption are in `chordica-session`), Notation, Cue's videos, Fontastik's SoundFonts and
the other instrument kinds (SF2, FM, tracker samplers: the engine has them; picking a file on the card is the missing part), the
arpeggiators, the analog sticks (pitch bend or expression).

## 6. Testing

- **The library's tests** (`cargo test` in `r36s/lib`, run by `r36s/check.sh`): the desktop's own `chordica2009` tests compiled in;
  the pad's grid (every button once a hand; the chord and bass names checked against the notes the 2009 voicings play, in every key);
  the hands (one thing at a time, form 1's third, the menu's clamps, a full hand, the level under full scale); the press-to-render
  probe; the speed test; the C API (NULL safety, strings cut on a character boundary, a press heard through `cr36_render`).
- **`chordica --selftest [--shots DIR]`** drives every screen through the pad's raw buttons with no sound device and checks the
  colours' contrast, then the window path; **`r36s/build-deb.sh --clean-room`** builds `Chordica.deb` in Debian 12 arm64, self-tests
  it, installs it with this repo's `deb_watcher.py` in a clean Debian 12, and writes a device report there.
- **Not testable here, so "untested" until a report says otherwise:** the engine's speed and the callback's load on the Cortex-A35,
  the buffer ALSA gives, the delay from a press to sound, how the pad mapping feels, how the grid reads on the panel, memory.

## 7. The device report (rule 9)

Start > Device report and `chordica --report [SECONDS]` write `/roms/Chordica/r36s-report.txt`: the system and the pad's GUID, the card,
start-up time and settings; **the sound device** (driver, rate, the buffer asked for and given; goal 512 frames or fewer), the callback's
pace over a second while a quiet chord is pressed every 50 ms, **the press wait** (from a press to the callback that renders it, mean
and worst) and **press to sound** (the worst wait and two buffers; goal 25 ms; the codec's own delay after that is unknown); the
callback's load (goal 50%); the screen's paint (goal 10 ms); **the speed test** at the buffer the device gave, 10 s each of Acoustic
Grand Piano, String Ensemble 1 and Electric Bass (finger) with both hands every eighth note at 140 bpm in form 3 (goals 4x real time
and 99% of buffers in half their time); memory.

## 8. Phases (mirrored in `ROADMAP.md` section 16)

1. **Built** (2026-11-10): the library, the host, the package, the self-test, the report; tested on a Mac and in Debian 12 arm64:
   `r36s/build-deb.sh --clean-room` built `Chordica.deb` (4.1 MB; needs SDL2, libc, libm, libgcc_s; newest glibc symbol 2.34), its
   self-test passed, `deb_watcher.py` installed it in a clean Debian 12 and the launcher ran it, and a device report there had 18 PASS
   and 1 FAIL (no pad, as expected under Docker). Those numbers are a Mac's under Docker, not the unit's: 48x to 130x real time, 99% of
   256-frame buffers in 5% of their time, press to sound about 18 ms with SDL's dummy driver. The 2 GB build VM killed two crates
   compiling at once, so under 4 GB the package build compiles one at a time.
2. **On the unit:** `Chordica.deb` to `native_deb/` (`flash-macos.sh` does it), Start > Device report, bring the report back; then
   play it: the pad mapping, the latency felt, the grid on the panel.
3. **The open decisions** (section 0): the release terms, the pad mapping, a workflow.
4. **Later:** the mode keyboard on the pad, SoundFonts from the card, the metronome, takes and the score, sessions over Wi-Fi.

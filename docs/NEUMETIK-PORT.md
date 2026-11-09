# Neumetik on the R36S: the plan

Status (2026-11-08): **phases 2-5 built in the Neumetik repo's `r36s/` and tested without the unit (its README has the detail); never run on a real R36S.** Neumetik is ansiapps' synthesizer (`~/Coding/neumetik`, `joashchee/neumetik`,
proprietary but for its MIT `neumetik-midi`): General MIDI and GS without a SoundFont, layered background noise (BGN), tracker modules in 112 formats played by
their own replayers, the chips they need (OPL, SID, Paula, the 2A03, the YM chips...), and music seen as animated ANSI art (Visual). Its desktop app is Tauri,
React and TypeScript. This file is the plan and the decisions, as `docs/KLAMANSI-PORT.md` is for KlamANSI; the code will live in the Neumetik repo (an `r36s/`
folder), never here (rule 2). `ROADMAP.md` section 11 tracks it.

## 0. Decisions (confirmed by the maintainer, 2026-11-08)

1. **Where the code lives: the Neumetik repo**, an `r36s/` folder and its own workflow, as KlamANSI's `r36s/`. This repo keeps the plan, the theme, the pad mapping
   and the `ROADMAP.md` pointer.
2. **Release: free for the R36S**, a gift for r3i6nos users, under the same grant wording as KlamANSI's (free to use and pass on, unmodified; holder ansiapps), in
   `r36s/package/copyright` (confirmed with the rest; `NEUMETIK_RELEASE=1` still refuses a copyright that carries a draft marker). The MIT notice of `neumetik-midi` and the theme font's CC BY-SA credit ship in the package.
3. **A player first, editors later.** The handheld has a pad, no keyboard: the first version plays (MIDI, tracker modules, BGN scenes, Endless Mode) and shows
   (Visual). The Tracker editor, the Music Editor, Compose, Synth and the samples repository are later items; Keys comes back on the pad (section 3).
4. **Rule 8 with no departure.** Visual already draws CP437 cells in the 16 VGA colours, the theme's own cells, so its display view is Visual's canvas at 64 x 20 and
   the top bar and three-row command line stay.
5. **Shape: a C and SDL2 host linking a Rust static library** (section 1), not a LÖVE app: Neumetik's engines are Rust, and a LÖVE app would need the same library
   through LuaJIT's FFI with nothing gained.

## 1. Shape: a native host around Neumetik's crates

- **`libneumetik_r36s.a`**, a small crate in `r36s/lib/` that depends on `neumetik` (the workspace's root crate) and exposes a narrow C API: open a file from bytes
  (MIDI by `neumetik::Player`, a module by `neumetik::tracker::load_with` with `beside` for companion files), start a BGN scene from `neumetik::bgn::BANK`, render
  the next N stereo frames into a buffer, seek, pause, title and length, and fill a 64 x 20 cell grid from `neumetik::visual::Visualizer::frame` (character and
  colour per cell). No new dependencies: the crates are standard library only, and the library links libc, libm, libpthread, libdl and libgcc_s, all in the rootfs.
- **`neumetik`**, the host in C and SDL2 like Commander and KlamANSI's host, with `theme/r3i6nos_ansiapps.h` (read from this repo, as KlamANSI's host does) for the
  cells, top bar, command line and list dialogs. Sound through SDL2's audio callback (ALSA underneath; the rootfs has `libasound2`), which pulls from the library on
  SDL's audio thread. The UI thread only reads a snapshot (position, levels, the analysis Visual needs).
- **Endless Mode** lives in the app (`app/src-tauri/src/endless.rs`, with `compose.rs`), not a crate, and uses serde. It needs moving into a crate (or its music
  core split from the Tauri glue) before the R36S can have it: a Neumetik-side change, so it is its own phase.
- **Left out of the first version:** the editors (decision 3), SoundFont and DLS playback, MIDI in and out (no MIDI port on the unit), the samples repository and
  soundpaks, the cover painter's export to Stylus, recording. Each is a later item.
- **Fast start** (the global rule): the window and a splash with a progress bar at once; the BGN bank and the library scan load after the first screen, off the UI thread.

**Unknown until a spike runs** (phase 1): how many times faster than real time each engine renders on the 1.3 GHz Cortex-A35 (the Mac numbers say nothing), with
Visual's analysis and drawing on top at 30 frames a second; memory for a large module next to the OS on 1 GB; whether the unit's sound works at all through ALSA
(`/proc/asound` was never captured: `scripts/device-info.sh` has an `audio` section for it), at what rate, and what the headphone jack and speaker do.

## 2. The screen: the theme's 64 x 24 cells

| Rows | What |
|---|---|
| 0 | Top bar: `Neumetik` and the panel at the left, battery at the right |
| 1-20 | The display view: the panel's list (whole pages, `Page n/m`), or Now Playing (Visual's canvas, 64 x 20) |
| 21 | Reply: the last result or a tip (`Opened 12 files`, `Cannot read song.xm: ...`) |
| 22 | Prompt: what A would do (`play song.mid`), or the title playing |
| 23 | Status: time / length, a progress bar, the volume, the format (`XM 8ch`, `GS`, `BGN rain`) |

**Panels** (the Start menu and D-pad left/right switch them): **Music** (the files on the card, MIDI and modules together, by folder), **BGN** (the bank's scenes, then
a scene's four layers with their levels), **Endless** (its styles; once it is in a crate), **Visual** (the 25 kinds, the one picked shown live behind the list), and
**Now Playing**, which is Visual full size over rows 1-20 with the command line kept.

## 3. Controls (raw buttons, `theme.PAD`; `docs/hardware-findings.md`)

| Button | In a list | In Now Playing |
|---|---|---|
| D-pad up / down | move | volume |
| D-pad left / right | switch panel | seek 10 s (a module: one order) |
| L1 / R1 | page | previous / next file in the folder |
| L2 / R2 | top / bottom | previous / next Visual kind |
| A | play, or open a folder | pause / resume |
| B | parent folder | back to the list (it keeps playing) |
| X | stop | stop |
| Y | queue the file (Sequencer: files back to back) | the song's loop on / off |
| Start | the menu: Panels, Stop, Repeat, Shuffle, Visual settings, Output rate, Quit | same |
| Select + Start (held) | quit to the menu | same |

The volume keys and Power are keyboard keys on this unit and have not been seen as events yet; if they arrive, they set the volume too.
**Keys on the pad, later:** CHORDiCA's way, the face buttons and the D-pad as the notes of one of the seven modes, L1/R1 an octave, L2/R2 the mode, from `neumetik-keys`.

## 4. Files on the games card

Only under `$R3I6NOS_ROMS/Neumetik/` (`/roms/Neumetik/`): `music/` is the user's own MIDI files and modules (read only by the app; nothing is bundled, rule 2),
`export/` for WAV renders (later), `settings.txt` (key=value: volume, Visual kind, output rate, repeat). Writes go to a temp file and a rename, so a pulled card leaves
the old file whole; nothing outside that folder is written (rule 7). A module's companion files (instruments, samples beside it) are read from its own folder.

## 5. Testing, as Commander and KlamANSI do it

- The library's C API has Rust tests in the Neumetik repo (a MIDI file and a module rendered through the C API equal `neumetik::render` and the tracker's own
  render, sample for sample).
- `neumetik --selftest [--shots DIR]` drives the host headless: every panel, a song played to the dummy audio driver for a few seconds, the pictures as BMP, the
  Select+Start chord. Under qemu in CI and in Debian 12 arm64 under Docker.
- The device report (`neumetik --report`, Start > Device report; `docs/DEVICE-REPORTS.md`) renders each engine for 30 s and writes the times-real-time to `/roms/Neumetik/r36s-report.txt`: the only meaningful speed number,
  and only on a unit.
- **Not testable here, so "untested" until a card says otherwise:** sound out of the unit, speed and memory on the Cortex-A35, the pad, how Visual looks on the panel, exFAT.

## 6. Phases (mirrored in `ROADMAP.md` section 11)

1. **Spike:** the static library cross-built in Debian 12 arm64; a headless speed test (MIDI, a module in each family, BGN, Visual) under Docker, then on the unit.
   *Go/no-go:* every engine at least 2x real time at 44.1 kHz with Visual at 30 fps on the A35; if not, a lower output rate, a polyphony cap, or fewer engines.
2. **Host:** SDL2 window and audio, the splash, the screen of section 2 with Now Playing and Visual; keys in, screens out.
3. **Controls and panels:** the pad of section 3, Music, BGN, Visual, the Start menu, the queue.
4. **Files and settings** in `/roms/Neumetik/`.
5. **Package:** `Neumetik.deb` (`r36s-neumetik`, binary `usr/games/neumetik`, needs only SDL2 and libc), installed by `deb_watcher` in a clean Debian 12 under
   Docker; the workflow in the Neumetik repo; a try on a real R36S (rule 1).
6. **Later:** Endless Mode (after it moves into a crate), Keys on the pad, WAV export, the Tracker's view of the playing pattern, SoundFonts, the editors.

## 7. What changed from this plan while building it (2026-11-08)

- **Built:** `r36s/lib` (`neumetik-r36s`: the C API, `neumetik_r36s.h`), `r36s/host/neumetik.c`, `r36s/package/` and `.github/workflows/r36s.yml` in the
  Neumetik repo. Tests: the C API against Neumetik's own players (MIDI and a module equal sample for sample), every Visual kind drawing cells, and a host
  self-test of every panel with pictures. The `.deb` (4.1 MB; needs SDL2, libc, libgcc_s, libm; newest glibc symbol 2.34) was installed by this repo's
  `deb_watcher.py` in a clean Debian 12 arm64 under Docker and self-tested there. **The workflow has not run on GitHub; nothing has run on an R36S.**
- **The speed test is in the app,** in its device report (rule 9): `neumetik --report` and Start > Device report write `/roms/Neumetik/r36s-report.txt`, so the unit's numbers need no SSH. On the Mac
  the busiest engine (a generated 7-part GM song, about 30 notes at once) plays at 60x real time and a Visual frame takes 0.07 ms; the Cortex-A35 is far slower
  and unmeasured. Test music is generated in code (`lib/src/gen.rs`), so no one else's music is in the package (rule 2).
- **Music as text** opens too (MusicXML, ABC, ChordPro, MML...), through Neumetik MIDI's `text` module, played as MIDI.
- **Endless Mode is not in the first version** (section 1: it must move out of the desktop app first); it is a later item, with Keys.
- **BGN** lists its 18 sections, then a section's scenes; the four layers' levels are not adjustable yet. **Visual:** choosing a kind shows Now Playing
  (the list does not preview it live). There is no `export/` folder yet (WAV export is later).
- **Release profile:** thin LTO, because full LTO of `neumetik-synth` with one codegen unit was killed for memory in a 2 GB build VM.
- **The package's NOTICE** is generated from the desktop app's own list of ported code (`app/scripts/third-party-licenses.py`), plus Neumetik MIDI's MIT
  licence and this repo's theme and font notices.
- **The device report** (2026-11-09, rule 9) replaced the speed test: Start > Device report and `neumetik --report` write `/roms/Neumetik/r36s-report.txt` (system and the pad's GUID, card, start-up, the sound device and whether its callback runs, screen paint, every engine's speed, Visual, memory).

## 8. First run on a real R36S (device report, 2026-11-10)

Neumetik 0.1.0 ran on one unit under r3i6nos and wrote its device report: **18 PASS, 0 FAIL, go on speed.** The report is kept in the
Neumetik repo (`r36s/reports/`); the unit's figures are in `docs/hardware-findings.md` ("First device report"). A second report
(2026-11-09 22:21) matched within 3%: MIDI 3.3x real time, Thunderstorm 5.6x, start-up 557 ms, peak 50 MB.

| Engine (30 s each, 44100 Hz stereo) | Real time | Mac | Unit vs Mac |
|---|---|---|---|
| MIDI, generated 7-part GM song | **3.4x** (the slowest) | 60x | about 18 times slower |
| ProTracker, 4 / 8 channels | 34.5x / 14.3x | - / 285x | |
| BGN, Empty room / Murmur / Light rain / Thunderstorm | 26.3x / 11.7x / 6.7x / 5.7x | 61x (Thunderstorm) | |
| Visual, 25 visualizers at 64 x 20 | 0.82-1.35 ms a frame (Kaleidoscope slowest; goal 8 ms) | 0.07 ms | |

Also: start-up 587 ms to the first screen; a whole screen painted in 4.6 ms; peak memory 49 MB; exFAT with rename over a file; ALSA at
44100 Hz, 2 channels, 1024 frames, with the callback running 43 times a second.

- **MIDI has the least room:** 3.4x at 44100 Hz would be about 3.1x at the 48000 Hz output rate, and a denser song than the generated one
  could need more. Nothing to change now; a real MIDI library is the next thing to time.
- **Not yet tried on the unit:** hearing it, the pad's feel, Visual on the panel, a real `music/` folder.

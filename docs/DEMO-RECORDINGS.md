# Rec demo: what an R36S port records to show itself

Every R36S port (an app or tool built for r3i6nos, in this repo or in its own) can show itself off. Next to **Device report** (`docs/DEVICE-REPORTS.md`)
its menu has **Rec demo**: one action that runs the real app on the unit through a scripted tour of its hero features, on a pre-generated demo file, and
saves **screenshots and a short video with sound** to a `Demo` folder. They are for the store page, the README and the maintainer's eyes. CLAUDE.md
rule 11 makes this a rule.

Status: **specified 2026-11-11; TinyNTT is the first port to have it** (2026-11-11, `theme/r3i6nos_demo.h`; ROADMAP section 18). TinyNTT's has run on the unit (2026-11-11); no other port's has.

## What it makes

Everything goes in **`/roms/<App>/Demo/`** (`EASYROMS/<App>/Demo/` on a computer), a folder of its own so a run never touches the app's other files
(CLAUDE.md rule 7: the games card is theirs). A new run replaces the earlier run's screenshots, demo file, log, frames and sound (they describe the latest run) and **keeps every earlier `demo-*.mp4`**, nothing else.

| File | What |
|---|---|
| `01-<name>.png`, `02-<name>.png`, ... | Screenshots of the **actual running app**: the frame the app drew, at the screen's own size (640x480), one per hero feature or screen, in tour order. Names say what is shown (`01-song-playing.png`). |
| `demo-<timestamp>.mp4` | A short video (30-90 seconds; H.264 and AAC, 640x480, 30 fps) of the same tour, **with the app's own audio**. The name carries the time the recording started, `demo-20261111-143005.mp4`, so runs and builds can be told apart. **A new run never replaces or deletes an earlier video**: the timestamp is why two runs' names cannot collide, and every `demo-*.mp4` stays until the person deletes it. |
| `demo-<timestamp>-frames.r3f` and `demo-<timestamp>.wav` | The recording itself, kept after the video is made (only the latest run's): the 64x24 cell screens and the 16-bit stereo sound the video was encoded from, **up to about 30 MB** (the sound is 176 KB a second at 44.1 kHz). **Commander plays them by itself, with no ffmpeg and no decoding** (`apps/README.md`, the Play tab), which is why they stay: they play at once on any unit, where the `.mp4` needs the image's ffmpeg to decode H.264 in software (speed on the unit untested). `--encode-demo` on a computer remakes the video from them. |
| `demo-file.<ext>` | A copy of the demo file the tour loaded, so a viewer can open it in the desktop version. |
| `rec-demo.txt` | The log, in the device report's grammar (`STATUS key: value`, sections, `end` last): what was shown, which hero feature each shot and each second of video covers, any SKIP, the length, the sizes. |

Screenshots are the app's own frames, not a photo of the screen and not a mock-up: the app saves what it drew (an SDL app reads its renderer back,
a LÖVE app its canvas). The video is those same frames plus the app's audio; neither is composed or edited afterwards except for the
trim and the encode.

## The demo file

- **One pre-generated file per port** (`r36s/demo/` in the port's repo, shipped in its package's share folder, never in the user's files): a song, a
  picture, a project, an archive, a game save, whatever the app opens. The app **loads it as a person would** (the open dialog or its command line),
  so the loading shows too.
- **It uses every hero feature.** The port's README keeps a **hero list** (the features the store page would name, usually 3-8) and the demo file is
  made so each one has something to show: if the hero is a synth, the song uses every voice and effect; if it is an editor, the picture has layers,
  a palette, an animation. A test checks it: **each hero feature in the list is named by at least one step of the tour script**, and the demo file
  opens without error in the port's own self-test.
- **Original and generated in code** (`r36s/demo/make_demo.*`, no binary of unknown origin), so rule 2 holds: nothing copyrighted or proprietary,
  and the file can be rebuilt byte for byte. A port that needs game data it cannot ship (a runtime's own samples) makes the demo from what it can.

## The checklist

**Rec demo** opens a list dialog first (the theme's, rule 8): one line per hero feature from the port's hero list, all ticked, Space or A toggles, Start
begins, B cancels; more than a page of them is whole pages (`Page 2/2`). The tour script marks which steps belong to which hero feature, and a
run plays only the steps of the ticked ones (the setup and the splash always play). At least one must stay ticked. The ticked list is written to
`rec-demo.txt` (`INFO shows: ...`, and `SKIP <feature>: not chosen`). The self-test that every hero feature is in the tour still covers the full list.
`--rec-demo` takes `--features a,b,c` (default: all) for SSH and tests.

## The splash and the version line

- **The video's first seconds are the app's own splash screen** (rule 12, made to the theme; KlamANSI's is the example), recorded from the app's real
  start, not a title card of the recorder's. It carries a **version line**: `v<version> <commit> <date>` (the commit is the short git hash the build was
  made from, with `+dirty` if the tree was not clean; the date is the commit's date, `YYYY-MM-DD`). The build script stamps both into the package; the
  splash, About and the log all show the same line, and `rec-demo.txt` has `INFO build: v0.1.0 a1b2c3d 2026-11-11`.
- A test checks the first recorded frame is the splash (the version line is in its cells) and the log's build line matches.

## Locked out while it records

Rec demo owns the unit until it ends: the **whole power of the unit goes to this one task**.

- **Input:** the pad does nothing except **Select+Start, held, to cancel** (the cancel keeps the files so far and the log says `FAIL cancelled`). The
  tour's own button presses go through the app's input path, not the pad.
- **Who reads the lock:** the menu's `menu/demos.lua` (a report cannot start, and Settings > Rec demos shows the recording) and the three root or
  background helpers that start something: `r3i6nos-net` (scan, wired, Wi-Fi join, test and Start SSH answer "A Rec demo is recording"; the status
  and the other reads still work), `r3i6nos-about check` (the update check writes the same words as its answer) and `r3i6nos-report` (exit 75).
  A lock whose pid is dead blocks nothing. Tests: `tests/test_demos.lua`, `tests/test_demo_lock.py`.
- **Processes:** the menu's background work is paused (or its helpers stopped) while a recording runs: no SSH or network helper starting, no update
  check, no device report, no clock or battery polling beyond the bare minimum, no second Rec demo. An app that runs a lock file
  (`/run/loveos/demo/lock`) tells the others, which refuse to start while it exists; the lock is removed at the end, including on cancel or crash.
  Screen blanking and suspend are held off: the image has no idle blank or suspend of its own (nothing in `overlay/` or `menu/` blanks the
  screen), so an SDL app calls `SDL_DisableScreenSaver()` for the run (TinyNTT does) and nothing more is needed.
- **Progress:** a **progress bar** is on screen the whole run (the theme's three-row command line: the step in the reply row, `[#####.....] 43%` in the
  prompt row, the time and the feature in the status row), written to the three progress files (`docs/DEVICE-REPORTS.md`, "Progress") so
  Settings > Rec demos and SSH show it too. The recorder writes them (`r3_demo_status`, `r3_demo_status_done`) in `/run/loveos/demo/`: `lock` (the
  pid), `running` (the app's name), `progress` (`step=`, `steps=100`, `doing=`, `file=`, the report's keys) and `done` (`exit=N`). The bar is **not in the recording**: the recorder takes the app's frames, not the screen as shown.
- **A hot-plug failure stops it** (`docs/HOTPLUG.md`): a USB device that would not start, a card pulled out in use, a critical battery, a USB sound card with no
  output. The tour stops, the recording is closed (files kept, a `FAIL hotplug failure` line each, `end`, no video), and the failure(s) are then shown. No toast is
  drawn or recorded while it runs. A plain change (headphones, a stick) is ignored. `d.n_failures` and `d.failure[]` hold them; TinyNTT's tour calls `r3_demo_failed`
  from `rc_stopped`, with Select+Start.
- The encode, which follows the tour, shows the same bar ("Encoding 62%") and is locked the same way.

## The tour

- **A script, not a person:** a list in the port (`r36s/demo/tour.txt` or inside the app) of steps: press a button, wait N frames or seconds,
  take a screenshot called X, say a caption (for `rec-demo.txt`). It drives the app through its **real input path** (the same events the pad
  makes), so it shows what the buttons do and tests them at the same time.
- **Deterministic and not real time.** The recorder steps the app's clock itself (a fixed 1/30 s a frame) and renders the audio offline into a
  WAV, so a slow unit makes a slow recording and not a stuttering video, and two runs make the same frames. Anything the app does on a real
  clock (a timer, an animation) reads the stepped one while recording.
- **The screen as the user gets it:** the theme's 64x24 text-mode screen (rule 8) at 640x480, no overlay, no cursor of the recorder's, no recorder UI in
  a frame. The recorder's own progress shows on a screen before and after, not during.
- **Under 90 seconds,** and it ends on the app's start screen, so the next run begins from the same place. It starts from a clean state (the
  app's defaults, a temporary settings folder): the person's settings, recent files and data do not show up in the video, and the run does not
  change them.
- **Sound:** the audio is what the app's mixer produced (tapped before the device), not a microphone and not the codec's path, so it is the same
  on any headphones state. An app with no sound records a silent track and the log says `SKIP audio: this app makes no sound`.

## How it is started

- **An app with a screen:** an item **Rec demo** in the menu, right after **Device report**, and a `--rec-demo` flag (for SSH). It shows it is
  running (the step, a bar and the time, like a report's wait) and, when done, where the folder is and how long the video is.
- **A command-line tool** (no screen): its package installs `<command>-demo` next to `<command>-report`, which runs the tool's demo steps and
  draws the commands and their output on the theme's 64x24 screen, frame by frame, so it records like an app does. **Settings > Rec demos** in
  the menu lists every installed one, as Device reports does, and uses the same three progress files (`docs/DEVICE-REPORTS.md`, "Progress")
  under `/run/loveos/demo/`.
- **A LÖVE app:** the same menu item, writing with plain `io` (as its report does) into `/roms/<App>/Demo/`.

## Encoding the video

The frames (PNG) and the audio (WAV) are written to a temporary folder first; the encode then makes `demo-<timestamp>.mp4`; they stay in `Demo/` (as `demo-<timestamp>-frames.r3f` and `.wav`, until the next run clears them), and only the ffmpeg log is deleted. The encoder is
`ffmpeg` (libx264 and AAC). **Decided 2026-11-11: use the image's ffmpeg-min where there is one.** The recorder looks for `/usr/bin/ffmpeg` first (the 5.3 MiB GPL build from `tools/ffmpeg-min/` with exactly this encode in it; ROADMAP section 20), then `PATH` (a computer). It is not a package dependency of any port. **Timed on the unit** (TinyNTT's report, 2026-11-11 21:14): a 53.3 s video recorded and encoded in 64.7 s on the four Cortex-A35 cores. The rule holds either way: **`Demo/demo-<timestamp>.mp4` is there when Rec demo says done**, or the
log says FAIL with why, and the frames (`demo-frames.r3f`) and the audio (`demo.wav`) are left in `Demo/` so nothing is lost, and `--encode-demo DIR` on a computer with ffmpeg makes the video from them.

## Shared code

- **Native (C and SDL2):** `theme/r3i6nos_demo.h`, header-only, MIT. `r3_demo_open` (makes `Demo/`, removes an earlier run's files and nothing else, starts
  the log), `r3_demo_frame` (a screen, 1/30 s), `r3_demo_audio` (the matching 1470 stereo float frames at 44100 Hz), `r3_demo_shot` (the next
  `NN-name.png`), `r3_demo_caption` (a log line stamped with the time and the hero feature it shows), `r3_demo_build` (the `build:` line), `r3_demo_lock` and `r3_demo_unlock` (`$R3I6NOS_RUN/demo/lock`, default `/run/loveos/demo/lock`, a pid; a dead holder's lock is taken over), `r3_demo_finish` (the demo file's copy,
  the checks, the encode, the summary, `end`) and `r3_demo_encode` (the video from the newest kept `demo-<stamp>-frames.r3f` and `.wav`: a port's `--encode-demo DIR`; `r3_demo_encode_p` adds a progress callback, which `d.on_progress` gives to `finish`). The kept files are named with the run's stamp, an earlier run's videos are never removed, and `d.cancelled = 1` before `finish` keeps the files, logs `FAIL cancelled` and skips the encode (`R3I6NOS_DEMO_STAMP` fixes the stamp for tests). Screens are kept as
  64 x 24 cells, a run of equal screens being one record, and rendered to 640 x 480 only for a PNG or for ffmpeg's pipe; audio goes to a 16-bit WAV.
  The PNG writer is dependency-free (indexed 4-bit colour, stored blocks, about 150 KB). Self-test: `tests/test_demo.c` (in `scripts/check.sh`)
  makes a 32-second tour on a fake app and reads the PNG back chunk by chunk to the pixels, checks the WAV's length, the log's grammar and `end`, that
  a missing or broken ffmpeg gives a FAIL line with the frames and audio kept, and, where ffmpeg is installed, the whole encode (ffprobe: H.264
  640x480 and AAC).
- **A port's tour** is its own code (TinyNTT's: `r36s/host/demo.h`): a function that steps a second copy of the app on a fake clock, rendering the
  sound engine by hand, pressing the pad's buttons through the app's own input path, and calling the recorder. It records the hero feature each moment
  shows (`rc_cap`) and the port checks every one on the list was named. **Anything that runs on a thread in real time (an export, a load) is waited for
  with no frames**, or two recordings would differ; a self-test records twice and compares every PNG, the audio and the frames byte for byte.
- **The shared pieces other ports use** (written 2026-11-11, tested by `tests/test_demo.c`): `r3_splash` in `theme/r3i6nos_ansiapps.h` (rule 12: the
  top bar, a framed box with the name, tagline, what is loading, a bar and the `v<version> <commit> <date>` line; TinyNTT's splash is one call) and,
  in `theme/r3i6nos_demo.h`, `R3Checklist` (`r3_checklist_init`, `_move`, `_toggle`, `_mask`, `_parse` for `--features`, `_draw`: the dialog and its
  keys, one page of up to 16 features), `r3_demo_status`/`_done` and `r3_demo_lock`/`_unlock`. A port's tour tags its steps by feature and starts every
  feature from a known place (TinyNTT's `rc_panel`, `rc_track`, `rc_play`, `rc_stop`), so a feature records alone.
- **Settings > Rec demos** (`menu/demos.lua`): the tools' `<command>-demo` programs (listed like the reports), a start, and a "Recording now" page with the
  progress of any recording, an app's own too.
- **Command-line tools** (`tools/qjs-node/demo.py`, baked into each tool's package as `<command>-demo` by `package-tool.sh`; tests `tests/test_tool_demo.py`, in
  `scripts/check.sh`; written 2026-11-12): see "Command-line tools" below. **LÖVE:** each LÖVE port has its own recorder (NoCoGaMa's `app/demo/`); no shared one yet.

## Command-line tools (`tools/qjs-node/demo.py`)

A tool has no screen, so its `<command>-demo` **draws the commands and what they print on the theme's 64x24 screen** (the IBM VGA 8x16 font scaled to 10x20, the top
bar, a framed display, the three-row command line with the caption and the step) and records that: it opens on the splash (rule 12: the app's name, tagline,
a progress bar and `v<version> <commit> <date>`), then for each step types the command, shows its output (stdout, stderr in red, the exit status, the files it made
with their sizes), and ends back on the splash. Pure Python 3 standard library: PNGs are made by hand (4-bit indexed, zlib), a screen's rows and glyph strips are cached
so a unit can draw a minute of video in about a second.

- **The tour is the tool's own `commands.txt`**, the list the device report times, with directives in comments (the report and `compare.py` ignore them):
  `#@hero id: label` (the hero features, in the README's list), `#@feature id` (the commands that follow belong to it; `#@feature -` ends it), `#@say caption` (the next
  command's caption, in the log and on the screen's reply row). `!SLOW` commands and commands outside a feature are not played. **A feature's commands must work alone**
  (an unticked feature's commands do not run), so one that reads a file draws it first; the self-test runs every feature alone. `package-tool.sh` makes `<command>-demo`
  only when the list has `#@hero` lines.
- **Checklist:** the menu starts a tool's demo without a dialog (`menu/demos.lua`: `<command>-demo --from-settings --quiet`), so every feature is ticked; over SSH
  `--features a,b` picks, and `--no-video` writes the frames and the sound only (tests, the packaging smoke test).
- **Deterministic:** the tool's commands run once, off the frame clock, and the screens are drawn from what they printed and held a fixed number of frames, so two runs make
  the same screenshots, frames and sound whatever the unit's speed (`tests/test_tool_demo.py` records twice and compares every byte). A tour over 90 s is re-planned with
  shorter holds; if it still does not fit the log says `FAIL video length`.
- **Sound:** a `.wav` a command writes (16-bit PCM, any rate, mono or stereo) is mixed into the video's track at the moment its step shows; a tool with none gets a silent
  track and `SKIP audio`.
- **Files:** `01-<name>.png` ... (the splash, one per step's output, the end), `demo-<timestamp>.mp4` (ffmpeg: `/usr/bin/ffmpeg`, then `PATH`; without it the log says FAIL and the
  frames, `demo-<timestamp>-frames.pngs`, and the sound are kept for `ffmpeg` on a computer), `demo-files/` (copies of the files the tour made, up to 40 of at most 2 MB), `rec-demo.txt`.
  An earlier run's files go, every earlier video stays.
- **Locked out:** the lock, `running`, `progress` and `done` files in `/run/loveos/demo/` (a live holder: exit 75, a dead one's lock is taken over). Cancel: SIGTERM or SIGINT, a
  hot-plug failure between steps (the log has `FAIL hotplug failure` for each, then `FAIL cancelled`, no video), or **Select+Start held for a second on the pad read from
  `/dev/input/js0`, whose button numbers (12 and 13) are the IO Checker's less one and are untested on the unit**. No toast is drawn: there is no screen of the app's own.
- **The log** (`rec-demo.txt`) has `build`, `runtime` (the tool, python, kernel, ffmpeg), `shows`, `tour`, `steps` (each command's exit, seconds, peak memory, lines and files),
  `files` (the checks), `timing`, `video`, `chapters`, `unit` (memory, temperature, load and battery at the start and the end) and **`power`** (rule 14): the battery, the idle
  draw taken first, then three phases measured the way the device report measures a command (CPU seconds, % of a core, wake-ups a second, peak memory, the draw in mW):
  **the tool's commands, drawing the frames, and the encode** (ffmpeg's own rusage), the busiest other processes, the total energy over idle, the time left, a
  wake-up goal of at most 200 a second per phase of a second or more, and a "cut first" line. It reads `report.py`'s readers from the `<command>-report` installed beside it.
- **Credit:** the font is CC BY-SA 4.0, so the package's NOTICE carries its README and licence when the demo is built in.

**Not done / untested:** nothing has run on a unit (a Mac and Debian 12 arm64 under Docker only; the packaging smoke test runs the whole tour with `--no-video`); the video with the
real ffmpeg is tested on a Mac only. The `.r3f` frames Commander plays are not written by this recorder (it keeps the PNG stream instead).

## What a port must do (the checklist)

1. A hero list in its README; a demo file made by a script; a tour script naming every hero feature.
2. **Rec demo** in the menu and `--rec-demo` (a tool: `<command>-demo`), with the feature checklist, the lock-out and the progress bar.
3. `Demo/` with numbered PNGs, `demo-<timestamp>.mp4` with audio starting on the splash and the version line, `demo-file.<ext>`, `rec-demo.txt`.
3a. A splash screen to the theme (rule 12) and a build stamp (version, commit, date) from its build script.
4. A self-test that runs the tour headless and checks the files, and that every hero feature is in the tour.
5. A local build script already (rule 10) that runs it; the run on a Mac or in Debian under Docker is **not** a run on the unit: say so.
6. After the first run on a unit, the maintainer looks at the screenshots and the video and says in words whether they show the app well
   (a program cannot judge it); the log says only what was recorded.

## Which ports have one

| Port | Where it lives | How it is started | Folder |
|---|---|---|---|
| TinyNTT | TinyNTT repo `r36s/host/demo.h` | Files > Rec demo (checklist first), `tinyntt --rec-demo [--features LIST]` (and `--make-demo`, `--encode-demo`) | `/roms/TinyNTT/Demo/` |
| KlamANSI | KlamANSI repo `r36s/host/recdemo.h` (the tour is `r36s/demo/tour.txt`, run by `host/demo.js` in a second QuickJS runtime on a drawing made in code) | Start > Rec demo (a checklist of six hero features first), `klamansi --rec-demo [--features a,b]` | `/roms/KlamANSI/Demo/` |
| NoCoGaMa (LÖVE) | NoCoGaMa repo `app/demo/` (`rec.lua`, `tour.lua`, `heroes.lua`): its own recorder, since r3i6nos has none for LÖVE | Start > Rec demo (a checklist of eight hero features first), `love app --rec-demo [--features a,b]`, `--encode-demo DIR` | `/roms/NoCoGaMa/Demo/` |

| Composer, Painter (command-line tools) | r3i6nos `tools/qjs-node/demo.py`, tours in each repo's `r36s/commands.txt` (2026-11-12; Animator's list has none yet) | `compose-demo`, `sprites-demo` (`--features a,b`, `--no-video`), Settings > Rec demos | `/roms/Composer/Demo/`, `/roms/Painter/Demo/` |

Ports still to add it to (checked in the repos 2026-11-12): Neumetik, Crunchy, Chordica, Animator, Commander and the IO Checker
(r3i6nos's menu, being the system, has a Device report but no tour). The KlamANSI and NoCoGaMa rows come from those repos' own code and roadmaps (KlamANSI's
Phase 10, NoCoGaMa's rule 11; NoCoGaMa's first recording on the unit, 2026-11-11 22:27, ran the whole tour in 87.8 s); this repo has not checked
either against every point of rule 11. Any recording that writes the lock and status files in `/run/loveos/demo/` (both ports do) is seen by the menu (Settings > Rec demos shows its progress) and by the helpers (the report, the update check and the network helper refuse to run while it holds the lock): `menu/demos.lua`, `r3i6nos-report`, `r3i6nos-about`, `r3i6nos-net`. NoCoGaMa's roadmap still says the menu and helpers do not read its lock; that is out of date.

**TinyNTT's first recording on a Mac (2026-11-11):** 16 screenshots, a 51-second video (H.264 640x480 30 fps, AAC, mean -23 dB), the demo song and the log, in
about 2 seconds; two recordings are byte-identical in frames, audio and every PNG. **Run on the unit** (2026-11-11 21:14, a card whose image has ffmpeg-min: 24 PASS, video made in 64.7 s, maintainer: it shows the app well). An image built before `/usr/bin/ffmpeg` was added has none, so there the
video step fails in the log and `--encode-demo` finishes it on a computer; the kept frames and sound play in Commander either way.

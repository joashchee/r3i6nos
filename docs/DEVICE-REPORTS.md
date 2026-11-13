# Device reports: what an R36S port brings back for Claude

Nothing in this repo is known to work on a real R36S until someone tries it there (CLAUDE.md rule 1), and the person trying it is not the one who writes
the code. So every R36S port (an app or tool built for r3i6nos, in this repo or in its own) has a **device report**: one action on the unit that
checks what can only be checked there, measures what can only be measured there, and writes the answers to **one plain text file on the games
card**. The maintainer copies the file to a computer and gives it to Claude, which reads it and updates the port and the roadmaps from it.
CLAUDE.md rule 9 makes this a rule.

## The file

- **Where:** `/roms/<App>/r36s-report.txt`, in the app's own folder on the games card (`EASYROMS/<App>/r36s-report.txt` on a computer). Written to
  `r36s-report.txt.tmp` and renamed, so a pulled card or a crash leaves the last whole report. A new run replaces it.
- **Also** printed to stdout, so a run over SSH shows it as it goes.
- **Plain ASCII text, one fact a line,** in this grammar, so Claude (or a script) can read it without guessing:

```
# r3i6nos device report
app: Neumetik 0.1.0 (r36s-neumetik)
made: 2026-11-09 14:03:11
how: Start > Device report
for: Claude. Copy EASYROMS/Neumetik/r36s-report.txt to a computer and give it to Claude in the Neumetik repo.

== system ==
INFO kernel: Linux 4.4.189 aarch64
INFO cpu: 4 x Cortex-A35 (0xd04), 1296 MHz max
...
== card ==
PASS mkdir: /roms/Neumetik
PASS rename over a file: ok
INFO filesystem: exfat, 27.1 GB free
...
== speed ==
PASS midi generated GM song: 3.2x real time (goal 2x)
FAIL bgn Thunderstorm: 1.4x real time (goal 2x)
...
== summary ==
PASS 31  FAIL 1  INFO 18  SKIP 2
end
```

- A line is `STATUS key: value`. **PASS** and **FAIL** are checks with a goal (the goal is in the value); **INFO** is a fact with no goal (a
  driver's name, a time with nothing to compare it to); **SKIP** is a check that did not run, with why. Sections start with `== name ==`.
- **The last line is `end`.** A file without it was cut short (the app crashed or the card was pulled), and Claude says so.
- No personal data: no Wi-Fi names, addresses, passwords, user files' names beyond what the app was asked to play, or anything from outside the
  app's own folder but the system facts below.

## What every report has

1. **The header:** app, version, package, date, how it was started, and the line telling the maintainer what to do with it.
2. **`== system ==`:** kernel and architecture; CPU model, count and top speed (`/proc/cpuinfo`, cpufreq); total and available memory
   (`/proc/meminfo`); the **sound route**, from `r3i6nos-sound status` (`docs/SOUND.md`): INFO lines `output` (card 0 or a USB card),
   `card N playback path` (the codec's current path and the ones it lists, e.g. `HP (of OFF SPK HP SPK_HP)`), `jack` and `headphones`
   (`in` or `out`), or one SKIP `sound route` where the helper gives nothing (a laptop); for SDL apps, SDL's version, video and audio
   drivers, the sound devices (`/proc/asound/cards`, SDL's output devices) and every pad (name, **GUID**, buttons, axes, hats). The route
   says what the output was set to, not whether sound was heard: a person says that. `r3_report_system` and `tools/qjs-node/report.py`
   write them; `R3I6NOS_SOUND` names another helper (tests).
3. **`== card ==`:** in the app's own folder: make it, write a file, write a second and rename it over the first, read it back, delete it; the
   filesystem type (from `/proc/mounts`: exFAT or FAT32 decide what works) and free space.
4. **The app's own sections:** every open question its roadmap marks "untested on the unit" that a program can answer: speed against the
   app's goal, memory (peak `VmHWM` from `/proc/self/status`), start-up time, whether a library or device it needs opens.
5. **`== power ==`** (CLAUDE.md rule 14; `docs/POWER.md`): the port's own CPU %, wake-ups a second and memory, idle and working; the draw in mW
   and the time left (battery `Discharging` only, else one SKIP saying to unplug); the busiest other processes; goals (FAIL when missed); and
   a last INFO line on what to cut first. For energy efficiency, so the unit lasts longer: it is where an idle app that draws 60 times a
   second shows up.
6. **`== summary ==`** with the counts, then `end`.

What a program can't judge (how the screen looks, whether a button feels right) is not faked: the report says what to look at, and the
maintainer says it in words.

## How it is started

- **An app with a screen:** an item **Device report** in its menu (Start), and a `--report` flag (for SSH). It shows that it is running and,
  when done, where the file is. It may stop the app's sound or work while it measures, and says so.
- **A LÖVE app:** the same menu item; it writes with plain `io` (LÖVE's own filesystem writes only to its save folder). NoCoGaMa's device spike
  (its Report page) is the first.
- **A command-line tool** (no screen, no menu): its package also installs `<command>-report`, made by `tools/qjs-node/report.py`. It runs the
  tool's own command list (`commands.txt`) on the unit, timed, with each command's peak memory, and writes `/roms/<Tool>/r36s-report.txt`.
  r3i6nos's **Settings > Device reports** lists every installed `*-report` command and starts one, so no SSH is needed.

### Progress (reports started from Settings > Device reports)

The menu (`menu/reports.lua`) starts one report at a time, in the background, and waits on a screen that shows how far it has got. It
uses three files in `/run/loveos/report/`:

- **`running`**: the command's name, written before the report starts. While it is there and `done` is not, the list says "running", and
  choosing the report again goes back to its wait instead of starting a second one.
- **`progress`**: written by the report itself, when `R3I6NOS_REPORT_PROGRESS` names it (the menu sets it), whole each time (a temporary
  file renamed over it): `step=N`, `steps=M`, `doing=<what it is checking now>`, `file=<the report's path>`. The wait screen shows `doing`, a
  bar of `step` out of `steps` and the time so far. `r3i6nos-report` and `tools/qjs-node/report.py` write it; a report that does not (a
  tool packaged before 2026-11-09) still works: the wait shows the time and says it cannot show steps.
- **`done`**: `exit=N`, written by the shell that started the report when it ends, so every report, old or new, says when it has finished.

When `done` appears, the screen shows the report's own summary line (`PASS n  FAIL n  INFO n  SKIP n`) and which card the file is on, or,
if the file is missing or has no `end`, that the report stopped and where its `.tmp` is. B leaves the wait; the report goes on.

### Setup the report needs (2026-11-13, maintainer's request)

A report whose answers depend on how the unit is set up (a charger out so the battery is measured, an adapter or a cable in the OTG port, headphones
in, a card in a slot) **says so before it runs, and waits for a keypress**: it does not start and then FAIL or SKIP for a condition a person could have
met. The report declares it in its first 80 lines as `# NEEDS: <text>` comment lines (a bare `# NEEDS:` is a blank line; at most 56 columns a line; the
menu follows deb_watcher's wrapper to the script, like `APP`). **Settings > Device reports** then shows a dialog, "<App> report: set up", titled with
the lines, and **A** starts the report, **B** cancels and starts nothing. A report with no `NEEDS` lines starts at once, as before; one already
running goes to its wait, not to the dialog. (Over SSH there is no dialog: the person is at a shell and reads the report's header.) **A report that must be
run several times** (one setup each) declares `# RUNS: n` (2 to 9) and a `# RUN k: <text>` line or more for each run's setup; `# NEEDS:` lines are what
every run needs. The menu then counts: the list says **"run 2 of 3"** next to the report, the dialog is titled **"<App> report: run 2 of 3"** and shows
that run's setup, **Y** shows another run's setup (to repeat one or skip ahead), the wait says "(run 2 of 3)", and the done screen says "run 2 of 3 is
done", **"NOT FINISHED: 1 more to run"** and what to do, or "All 3 runs are done: give Claude all 3 files". The menu remembers the last run started in
`/run/loveos/report/last-<command>` (it lasts until a reboot; after the last run the count wraps to 1), and passes `R3I6NOS_REPORT_SETUP=k/n` to the
report. **The report writes `r36s-report-run<k>.txt`** (so a later run never replaces an earlier one), says `run: k of n` in its header, and also copies it to
`r36s-report.txt` (the latest). `r3i6nos-report`'s is the example (the USB cases of the screen mirror, `docs/SCREEN-MIRROR.md`). Code: `needsOf` in `menu/reports.lua`, the `go` dialog in `menu/settings.lua`; tests `tests/test_reports.lua`,
`tests/test_settings.lua`, `tests/test_menu_view.lua`, `tests/test_r3i6nos_report.py` (`NeedsDialog`).
**An app with a screen** shows the same dialog from its own Start menu's **Device report** item (list the conditions on a screen in the app's theme, A to run,
B to cancel): not built yet, ROADMAP section 28.

## Shared code

- **Native (C and SDL2):** `theme/r3i6nos_report.h` (open, sections, lines, the system and card sections, peak memory, close with the summary; and the **power section**: `r3_report_power_section`, `r3_report_power_idle`, `r3_power_begin` / `r3_power_sample` / `r3_report_power_end`, `r3_report_power_advice`, `docs/POWER.md` "In a native port").
  Header-only, MIT, like the rest of `theme/`; its self-test is `tests/test_report.c` (run by `scripts/check.sh`).
- **Command-line tools:** `tools/qjs-node/report.py` (Python 3 standard library), shipped by `package-tool.sh`.
- **LÖVE:** no shared module yet (an app must work on an image whose `/opt/loveos/theme` predates one); follow this file's grammar.

Its sibling, **Rec demo** (`docs/DEMO-RECORDINGS.md`, CLAUDE.md rule 11), sits right after Device report in each port's menu and records screenshots and a video to `/roms/<App>/Demo/`.

## Which ports have one

| Port | Where it lives | How it is started | File |
|---|---|---|---|
| r3i6nos itself (the system, boot, both card slots, **the USB gadget and USB Ethernet questions of the screen mirror: `docs/SCREEN-MIRROR.md`**, and the battery cost of its background work: `docs/POWER.md`) | `overlay/usr/local/bin/r3i6nos-report` (`docs/STORAGE.md`) | Settings > Device reports > r3i6nos (this system), or `r3i6nos-report` | `r3i6nos/r36s-report.txt` on the data card (`/roms2`), else the system card |
| Neumetik | Neumetik repo `r36s/` | Start > Device report, `neumetik --report` | `/roms/Neumetik/r36s-report.txt` |
| TinyNTT | TinyNTT repo `r36s/` | Files > Device report, `tinyntt --report` | `/roms/TinyNTT/r36s-report.txt` |
| KlamANSI | KlamANSI repo `r36s/` | Start > Device report, `klamansi --report` | `/roms/KlamANSI/r36s-report.txt` |
| Crunchy | Crunchy repo `r36s/` | Start > Device report, `crunchy --report` | `/roms/Crunchy/r36s-report.txt` |
| Chordica | Chordica repo `r36s/` | Start > Device report, `chordica --report [SECONDS]` | `/roms/Chordica/r36s-report.txt` |
| NoCoGaMa (device spike) | NoCoGaMa repo `spike/device/` | its Report page | `/roms/NoCoGaMa/r36s-report-11.5.txt` and `-12.0.txt` (one a runtime) |
| IO Checker (the hardware: input, video, sound, cards; the native build also has the power section: idle on its list, the Motion pattern working, and the **usb sections**: devices, video out, MIDI, storage, network, keyboard, mouse, pads: what the OTG port brings, `docs/IO-CHECKER.md`) | `apps/io-checker-love/`, `apps/io-checker-native/` | its Device report item, or `io-checker-native --report` (headless: no power windows) | `/roms/IO_Checker/r36s-report-11.5.txt`, `-12.0.txt`, `-native.txt` (one a build) |
| Composer, Painter, Animator | their repos' `r36s/` (the package from `tools/qjs-node/`) | Settings > Device reports, or `compose-report`, `sprites-report`, `animate-report` | `/roms/Composer/`, `/roms/Painter/`, `/roms/Animator/r36s-report.txt` |

**A report started from Settings runs as `loveos`, not root.** The image has no D-Bus, so `systemctl` and `systemd-analyze` cannot reach
systemd from a report ("Failed to connect to bus", seen 2026-11-09). r3i6nos's own report reads what they say from `/run/r3i6nos/boot-state`,
written by the root timer `r3i6nos-bootstate`; an app's report that needs a unit's state reads it there too.

Commander has `commander --report` (2026-11-12: the system, the card, memory and the power section; **no Start menu item yet**, and no Rec demo: ROADMAP section 23); it writes `/roms/Commander/r36s-report.txt` and needs a window, so it runs on the unit's screen (or under a dummy video driver on a laptop). Beyond that it is this repo's own and tried on the unit directly. The IO Checker (`docs/IO-CHECKER.md`) is the hardware's report: buttons, sticks, keys, screen, sound and cards, with what the person saw and heard.

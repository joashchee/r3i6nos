# Device reports: what an R36S port brings back for Claude

Nothing in this repo has run on a real R36S until someone tries it there (CLAUDE.md rule 1), and the person trying it is not the one who writes
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
   (`/proc/meminfo`); for SDL apps, SDL's version, video and audio drivers, the sound devices (`/proc/asound/cards`, SDL's output devices) and every
   pad (name, **GUID**, buttons, axes, hats).
3. **`== card ==`:** in the app's own folder: make it, write a file, write a second and rename it over the first, read it back, delete it; the
   filesystem type (from `/proc/mounts`: exFAT or FAT32 decide what works) and free space.
4. **The app's own sections:** every open question its roadmap marks "untested on the unit" that a program can answer: speed against the
   app's goal, memory (peak `VmHWM` from `/proc/self/status`), start-up time, whether a library or device it needs opens.
5. **`== summary ==`** with the counts, then `end`.

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

## Shared code

- **Native (C and SDL2):** `theme/r3i6nos_report.h` (open, sections, lines, the system and card sections, peak memory, close with the summary).
  Header-only, MIT, like the rest of `theme/`; its self-test is `tests/test_report.c` (run by `scripts/check.sh`).
- **Command-line tools:** `tools/qjs-node/report.py` (Python 3 standard library), shipped by `package-tool.sh`.
- **LÖVE:** no shared module yet (an app must work on an image whose `/opt/loveos/theme` predates one); follow this file's grammar.

## Which ports have one

| Port | Where it lives | How it is started | File |
|---|---|---|---|
| r3i6nos itself (the system, boot, both card slots) | `overlay/usr/local/bin/r3i6nos-report` (`docs/STORAGE.md`) | Settings > Device reports > r3i6nos (this system), or `r3i6nos-report` | `r3i6nos/r36s-report.txt` on the data card (`/roms2`), else the system card |
| Neumetik | Neumetik repo `r36s/` | Start > Device report, `neumetik --report` | `/roms/Neumetik/r36s-report.txt` |
| KlamANSI | KlamANSI repo `r36s/` | Start > Device report, `klamansi --report` | `/roms/KlamANSI/r36s-report.txt` |
| NoCoGaMa (device spike) | NoCoGaMa repo `spike/device/` | its Report page | `/roms/NoCoGaMa/r36s-report-11.5.txt` and `-12.0.txt` (one a runtime) |
| Composer, Painter, Animator | their repos' `r36s/` (the package from `tools/qjs-node/`) | Settings > Device reports, or `compose-report`, `sprites-report`, `animate-report` | `/roms/Composer/`, `/roms/Painter/`, `/roms/Animator/r36s-report.txt` |

The default apps (Commander, the input checkers) are this repo's own and are tried on the unit directly; the input checker is itself the pad's report.

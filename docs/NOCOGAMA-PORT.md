# NoCoGaMa on r3i6nos: status and what r3i6nos gives it

Status (2026-11-08): **the editor, play, files, export and the release workflow are written and tested without a display (13 commits); run on the R36S since 2026-11-09: the spike under LÖVE 11.5 and 12.0, and the app's own Device report on 2026-11-11 (17 PASS, 2 FAIL on music slices, since cut; 60 fps drawing, the card and export pass).** NoCoGaMa is the ansiapps **No Code Game Maker**: draw a level, place actors, give them
behaviours, play it, all with the pad. The code, its plan and its tests live in the private repo `joashchee/nocogama` (`~/Coding/nocogama`); this repo never holds any of it (rule 2). Its plan is that repo's
`docs/R3I6NOS-PORT-PLAN.md` (organised like `docs/KLAMANSI-PORT.md`); `ROADMAP.md` section 9 tracks it here, and the NoCoGaMa repo's `ROADMAP.md` is the order of work.

## Shape

- **A LÖVE app, user-installed**: one `NoCoGaMa.love` packed for each of `Love 11.5/` and `Love 12.0/` on the games card, from the same code, never `DEFAULT=1` (not in the image), as the IO Checker is packed twice.
  Its own `scripts/build.sh` mirrors `scripts/build_love_app.sh` and its release workflow will mirror `app-io-checker-love.yml`; `love-app.yml` is not called while its repo is private.
- **The theme without a departure** (rule 8): 64 x 24 cells of 10 x 20, top bar, 20-row display, three-row command line. A LÖVE app can draw one pixel region inside the cells, so the level view (320 x 240) sits in the
  theme and nothing is left out (KlamANSI's native host had to give up the tool bar and command-line rows). It does not copy the theme or its CC BY-SA font: it loads `theme/` from `/opt/loveos/theme` on the device and
  from `R3I6NOS_THEME` on a Mac.
- **Pure Lua core, thin LÖVE shell**, tested with luajit; both runtimes from the first commit (only API that exists and is not deprecated in 11.5 and 12.0, no `t.version`).
- **Files** only under `$R3I6NOS_ROMS/NoCoGaMa/` (`/roms/NoCoGaMa/`), written to a temp file and renamed, so a pulled card leaves the old file whole (rule 7).
- **Licence:** proprietary to ansiapps like KlamANSI; free for the R36S under a grant in `package/copyright`, **confirmed by the maintainer 2026-11-08** (no draft marker, `NOCOGAMA_RELEASE=1` builds). Exported games carry NoCoGaMa's runtime files,
  so the grant says the user owns their games and may pass them on, free or sold, without gaining any right to the editor.

## What it uses from r3i6nos (all checked to exist, 2026-11-08)

`theme/r3i6nos_ansiapps.lua` (`theme.PAD`, the raw buttons; `theme.KEYS`; list dialogs), `menu/osk.lua` (the on-screen keyboard pattern for names), `menu/scan.lua` (an exported game must be listed by it), `scripts/build_love_app.sh`,
`.github/workflows/love-app.yml` and `app-io-checker-love.yml` (patterns), `R3I6NOS_ROMS`, the raw pad layout of `docs/hardware-findings.md`, and the `Love 11.5/` and `Love 12.0/` folders on the games card, which the menu already scans.

## Progress

| | |
|---|---|
| Scaffold, licence, private remote | done |
| Spike, laptop: a fixed 60 Hz step of 200 actors | 0.003 ms with the JIT on, 0.084 ms off (Mac). The official macOS LÖVE 11.5 has the JIT **off**, 12.0 **on** (found headless, `vendor/love-mac`). Says nothing about drawing; the Cortex-A35 is a guess |
| Spike, device: six pages, built per runtime | written and tested with luajit and headless LÖVE; **never drawn, never on a unit** |
| Data core: `json`, `project` (atomic save), the 60 Hz world, six behaviours | committed; tests pass (luajit, and real LÖVE 11.5 and 12.0 headless) |
| Editor (Level, Actors, Logic, Play tabs; undo; several scenes with doors; naming; editing texts), as screen specs drawn with the theme | committed; tested on the rules and as text dumps; **no frame drawn by LÖVE** |
| Games screen, on-screen keyboard, library, autosave (`/roms/NoCoGaMa/projects/`) | committed; tested with fakes and headless LÖVE. Listing uses `io.popen` + `ls`: **untested on the unit** (the spike's Card page tests it) |
| Export: a game becomes `Love 11.5/<Name>/` and/or `Love 12.0/<Name>/` | committed; r3i6nos's own `menu/scan.lua` lists the result and a separate luajit process plays it. **Not played under LÖVE with a display** |
| LÖVE API rule (every call on a list, checked against a strict stub and real 11.5/12.0) | committed; the `graphics` and `window` calls are **not checked against real LÖVE** (no window here) |
| Release workflow (`v*` tag, two byte-identical `.love` files, checksums, draft release) and CI | written, tested locally (`scripts/test_release.sh`); **never run on GitHub** |
| Its `scripts/check.sh` | passes (run again 2026-11-08, exit 0), including 11.5 headless (JIT **off** there, 0.078 ms for 200 actors) and 12.0 (JIT on, 0.0036 ms) |
| Full try on a real R36S, both runtimes | not done |

## What r3i6nos has to settle for it (shared with KlamANSI and `ROADMAP.md` section 6)

- **Whether a LÖVE game from the card runs and returns to the menu** (phase 5 bring-up, unconfirmed). NoCoGaMa's export depends on it.
- **That `/roms` is writable by `loveos` on a real card, and on exFAT** (configured and tested as text; untried). The spike's Card page tests mkdir, temp plus rename over an existing file, read back and delete.
- **The pad's real GUID** (still a guess in `run_app.sh`): the spike's Info page prints it, which settles `SDL_GAMECONTROLLERCONFIG`.
- **Whether the arm64 LÖVE has the LuaJIT JIT on.** r3i6nos links Debian's system `libluajit-5.1-2`, so it should (**expected, not seen**); the spike's Info page says. It is the difference between 0.003 and 0.084 ms a step.
- **The theme drawn by LÖVE with a display** (`theme.init`, `theme.draw`): never run; the NoCoGaMa scaffold under `love` on a Mac is the first thing that will, and so is the menu's check in `ROADMAP.md` section 1.

## Decisions

All five decisions in the NoCoGaMa plan's section 0 are **confirmed by the maintainer (2026-11-08)**: code in its own private repo, `TYPE=love` for both runtimes, the licence and grant (holder ansiapps), a free R36S release, rule 8 without a departure.

## The device report (2026-11-09, rule 9)

The device spike's Report page writes `/roms/NoCoGaMa/r36s-report.txt` (was `spike-result.txt`) in `docs/DEVICE-REPORTS.md`'s grammar: system, pad (17 raw buttons seen or FAIL), card, bench (p99 goal 8 ms), draw (goal 60 fps). The app gets its own Device report before its release (NoCoGaMa's roadmap).

## First run on a real R36S (the device spike, LÖVE 12.0, 2026-11-09)

**GO under LÖVE 12.0: 14 PASS, 0 FAIL.** The report is in the NoCoGaMa repo (`spike/reports/`).

- **System:** LuaJIT 2.1.0-beta3 with the JIT on; OpenGL ES 3.2 on the Mali-G31; 640x480 fullscreen; all 17 raw buttons seen; LÖVE sees a gamepad.
- **Bench** (600 steps of 1/60 s, goal p99 at most 8 ms): 200 actors 0.086 ms a step (p99 0.11), 400 actors 0.17 ms, the all-pairs worst case
  at 400 actors 3.45 ms (p99 3.54). About 29 times the Mac's JIT-on figures.
- **Draw:** 200 actors at 60 fps as rects (0.95 ms of CPU) and as printed glyphs (5.28 ms). 60 fps is LÖVE 12.0's ceiling here, not the
  panel's (11.5 drew faster, below), so the CPU time is the headroom.
- **Card:** `io.popen` + `ls` lists the folder as `loveos`, and a temp file renamed over an existing one works on exFAT: the app's game list and
  saves can work as designed.
- **Still to run:** the app itself, and whether a LÖVE app returns to the menu.

## The device spike under LÖVE 11.5 (2026-11-09)

**GO for the plan's rule, with the JIT off: 13 PASS, 1 FAIL.** LÖVE 11.5 turns LuaJIT's JIT off on arm64 itself (`docs/hardware-findings.md`),
so its figures are about 10 times 12.0's:

| Bench (p99, goal 8 ms) | 11.5, JIT off | 12.0, JIT on (two runs) |
|---|---|---|
| player rule, 200 / 400 actors | 1.04 / 1.93 ms | 0.11-0.26 / 0.21-0.35 ms |
| all-pairs, 200 / 400 actors | 5.75 / **20.3 ms (FAIL)** | 0.93-1.69 / 3.54-6.03 ms |
| draw 200 actors, rects / glyphs | 88 / 82 fps (0.89 / 1.74 ms CPU) | 60 / 60 fps (0.95-1.01 / 5.25-5.28 ms CPU) |

12.0 was run twice (the second time with the rebuilt spike that saves one file a runtime): the same code took up to 1.7 times as long the
second time, with more garbage a step, so the JIT's results vary between runs. Read 12.0's figures as a range.

The FAIL is the all-pairs worst case, not the plan's rule: keep collisions to the player against the rest, or cap an all-pairs world near
200 actors under 11.5. Card, pad and GPU as under 12.0.

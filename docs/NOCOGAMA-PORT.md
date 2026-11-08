# NoCoGaMa on r3i6nos: status and what r3i6nos gives it

Status (2026-11-08): **scaffold, spike and the data core written; never drawn by LÖVE with a display, never run on an R36S.** NoCoGaMa is the ansiapps **No Code Game Maker**: draw a level, place actors, give them
behaviours, play it, all with the pad. The code, its plan and its tests live in the private repo `joashchee/nocogama` (`~/Coding/nocogama`); this repo never holds any of it (rule 2). Its plan is that repo's
`docs/R3I6NOS-PORT-PLAN.md` (organised like `docs/KLAMANSI-PORT.md`); `ROADMAP.md` section 9 tracks it here, and the NoCoGaMa repo's `ROADMAP.md` is the order of work.

## Shape

- **A LÖVE app, user-installed**: one `NoCoGaMa.love` packed for each of `Love 11.5/` and `Love 12.0/` on the games card, from the same code, never `DEFAULT=1` (not in the image), as the input checker is packed twice.
  Its own `scripts/build.sh` mirrors `scripts/build_love_app.sh` and its release workflow will mirror `app-input-checker-love.yml`; `love-app.yml` is not called while its repo is private.
- **The theme without a departure** (rule 8): 64 x 24 cells of 10 x 20, top bar, 20-row display, three-row command line. A LÖVE app can draw one pixel region inside the cells, so the level view (320 x 240) sits in the
  theme and nothing is left out (KlamANSI's native host had to give up the tool bar and command-line rows). It does not copy the theme or its CC BY-SA font: it loads `theme/` from `/opt/loveos/theme` on the device and
  from `R3I6NOS_THEME` on a Mac.
- **Pure Lua core, thin LÖVE shell**, tested with luajit; both runtimes from the first commit (only API that exists and is not deprecated in 11.5 and 12.0, no `t.version`).
- **Files** only under `$R3I6NOS_ROMS/NoCoGaMa/` (`/roms/NoCoGaMa/`), written to a temp file and renamed, so a pulled card leaves the old file whole (rule 7).
- **Licence:** proprietary to ansiapps like KlamANSI; free for the R36S under a grant in `package/copyright`, **confirmed by the maintainer 2026-11-08** (no draft marker, `NOCOGAMA_RELEASE=1` builds). Exported games carry NoCoGaMa's runtime files,
  so the grant says the user owns their games and may pass them on, free or sold, without gaining any right to the editor.

## What it uses from r3i6nos (all checked to exist, 2026-11-08)

`theme/r3i6nos_ansiapps.lua` (`theme.PAD`, the raw buttons; `theme.KEYS`; list dialogs), `menu/osk.lua` (the on-screen keyboard pattern for names), `menu/scan.lua` (an exported game must be listed by it), `scripts/build_love_app.sh`,
`.github/workflows/love-app.yml` and `app-input-checker-love.yml` (patterns), `R3I6NOS_ROMS`, the raw pad layout of `docs/hardware-findings.md`, and the `Love 11.5/` and `Love 12.0/` folders on the games card, which the menu already scans.

## Progress

| | |
|---|---|
| Scaffold, licence, private remote | done |
| Spike, laptop: a fixed 60 Hz step of 200 actors | 0.003 ms with the JIT on, 0.084 ms off (Mac). The official macOS LÖVE 11.5 has the JIT **off**, 12.0 **on** (found headless, `vendor/love-mac`). Says nothing about drawing; the Cortex-A35 is a guess |
| Spike, device: six pages, built per runtime | written and tested with luajit and headless LÖVE; **never drawn, never on a unit** |
| Data core: `json`, `project` (atomic save), the 60 Hz world, six behaviours | written, tests pass (luajit, and real LÖVE 11.5 and 12.0 headless); **uncommitted in the NoCoGaMa repo when read** |
| Editor, play and files, export, package | not started (roadmap sections 3-6) |

## What r3i6nos has to settle for it (shared with KlamANSI and `ROADMAP.md` section 6)

- **Whether a LÖVE game from the card runs and returns to the menu** (phase 5 bring-up, unconfirmed). NoCoGaMa's export depends on it.
- **That `/roms` is writable by `loveos` on a real card, and on exFAT** (configured and tested as text; untried). The spike's Card page tests mkdir, temp plus rename over an existing file, read back and delete.
- **The pad's real GUID** (still a guess in `run_app.sh`): the spike's Info page prints it, which settles `SDL_GAMECONTROLLERCONFIG`.
- **Whether the arm64 LÖVE has the LuaJIT JIT on.** r3i6nos links Debian's system `libluajit-5.1-2`, so it should (**expected, not seen**); the spike's Info page says. It is the difference between 0.003 and 0.084 ms a step.
- **The theme drawn by LÖVE with a display** (`theme.init`, `theme.draw`): never run; the NoCoGaMa scaffold under `love` on a Mac is the first thing that will, and so is the menu's check in `ROADMAP.md` section 1.

## Unconfirmed decisions

The NoCoGaMa plan's section 0 lists five decisions with defaults; the maintainer has confirmed the **licence and grant**. Still marked proposed there, and assumed here because NoCoGaMa was asked to be included like KlamANSI: code in its
own private repo, `TYPE=love` for both runtimes, a free R36S release, rule 8 without a departure. Say if any of them changes.

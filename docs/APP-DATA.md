# Where an app keeps its data: the data card first, and one place only

Status: **rule and design, 2026-11-17** (maintainer's request; CLAUDE.md rule 16). **The engine `r3i6nos-appdata` and the C frontend `theme/r3i6nos_appdata.h` are built, and Commander and the native IO Checker call it (2026-11-17, below); the Lua frontend `theme/r3i6nos_appdata.lua` is built and tested under luajit but no LÖVE app calls it yet; the other apps' calls and the menu's part are not.** Nothing has run on a unit. Roadmap: section 30. It finishes storage step 2 (`docs/STORAGE.md`: `R3I6NOS_APP_DATA`).

## Why

The system card (TF1) should almost never come out (`docs/STORAGE.md`), and a person's data must not be on the card that a reflash wipes. So **an app's data lives on the data card (TF2) whenever a valid one is in**, and an app that finds its data on the system card offers to move it. When data is on both, the app offers to merge it into one, because two copies is how a save is lost.

"App data" is everything a person made or the app wrote for them, in the app's folder `<App>/` at the root of a card: saves, projects, settings, imports, `r36s-report.txt` and `Demo/` (rules 9 and 11). It is **not** the app itself (`/opt/native_apps/<name>/`, which an update replaces) and not other folders on the card (`Love 11.5/`, `native_deb/`, `ssh/`, and anything a person keeps there).

## What is a valid data card

`r3i6nos-appdata state` says so, from `/run/r3i6nos/cards` (`docs/STORAGE.md` step 1) and a probe. All must hold:

1. `DATA_AT` is mounted (by role, never the boot card, never a setup boot's original card).
2. The filesystem is exFAT, FAT or ext4 (what `r3i6nos-cards` mounts).
3. It is writable: a probe file is created, `fsync`ed and deleted under `<root>/.r3i6nos-probe`. A card with its write-protect set, or one the kernel remounted read-only after an error, is not valid.
4. It has at least 64 MiB free.

A card that is in but not valid is **not used** and the app says why, on one line, once per launch ("The data card is read-only; using the system card"). The card's identity is `<root>/.r3i6nos-card`: a small file (a random id and the date) written by Settings > Data card > Format, and by the first move to a card without one. A "don't ask again" choice is kept against that id, so a different card asks again.

## The launch check

Every app made for r3i6nos, native or LÖVE, does this **on every launch, after its splash (rule 12) is on screen and before its heavy work**, through one shared piece (below). Nothing is asked in a `--report`, `--rec-demo` or other unattended run: those use the place the check resolves without moving anything.

Let **D** be `<data card>/<App>/` and **S** be `<system card>/<App>/`. "Has data" means the folder exists and holds at least one file other than the app's own marker.

| Data card valid | Data in S | Data in D | What happens |
| --- | --- | --- | --- |
| no | any | any | use S (created if missing); no question. If D's card was in last time and the app's data is there, say so: "Needs the data card" (the menu greys the app, storage step 2) and offer **Use the system card (new, separate data)** / **Quit**. |
| yes | no | no | use D; no question (new data starts on the data card) |
| yes | yes | no | **Offer to move** (below) |
| yes | no | yes | use D |
| yes | yes | yes | **Offer to merge** (below) |

`R3I6NOS_APP_DATA` is set to the folder the app will use (D or S) and `R3I6NOS_DATA_CARD` to the data card's root or empty, for the app and for what it starts (tools, `ffmpeg`, `nttplay`). An app never builds `/roms/<App>` itself.

### Offer to move (data only in S)

A list dialog in the theme, default **Not now** (the safest button, as the format dialog's No):

> Move Commander's data to the data card?  1.2 GB in 340 files. The system card can then stay out.
> **Move all** · Keep on the system card · Not now

- **Move all:** copy, verify, then delete from S (below). The app waits behind a progress bar (step, bar, time, rule 11's bar) and only starts its work when it is done.
- **Keep on the system card:** the choice is stored (`~/.local/state/r3i6nos/appdata/<App>.json` of the login user, with the data card's id; a reflash forgets it and the question comes back) and not asked again until the data card's id changes or the person asks from Settings > Storage.
- **Not now:** use S this time, ask again at the next launch.

### Offer to merge (data in both)

> Commander has data on both cards: 1.2 GB on the system card, 3.4 GB on the data card.  12 files are on both and differ.
> **Merge into the data card** (recommended) · Merge into the system card · Not now

- Until merged, the app uses **D** (the data card's copy) and S is left alone, so an unmerged state can never write to both. A change in S since the last launch is shown as "1 file newer on the system card" in the dialog.
- **Merge:** each file of the source is compared with the target's. Same path and same content (size, then SHA-256): the source copy is deleted. Only in the source: copied. **Same path, different content: the newer modification time wins, and the other file is kept** as `<target>/<App>/.merge-conflicts/<YYYYMMDD-HHMMSS>/<path>`, never deleted. When the times are equal the target's wins.
- **Not now** asks again at the next launch; **Don't ask again** (a button on this dialog only) stores the choice against the pair of folders' state and asks again if either changes.

### The copy is safe or it does not happen

One engine does all of it (`r3i6nos-appdata move|merge`, Python stdlib, as `loveos`; the cards are mounted for `loveos`, so no root helper). Its rules:

1. **Plan first:** count files and bytes, check **free space on the target with a 5% reserve**, refuse a name the target filesystem cannot hold (a name differing only by case on exFAT, a name over 255 bytes, a symlink or special file on exFAT: those are listed and skipped, never lost, because they stay in the source), and refuse below **20% battery when not charging**.
2. **Copy, then verify, then delete the source file**: each file is copied to `<name>.r3i6nos-part`, `fsync`ed, compared by size and SHA-256 against the source, renamed into place, and only then is the source file deleted. A file that fails verification is retried once, then left in the source and reported.
3. **A journal** (`<target>/<App>/.r3i6nos-merge.json`, rewritten by rename) records what is done, so a power-cut or a card pulled out resumes at the next launch with "Finish moving Commander's data?". Partial files are never taken for real ones (`.r3i6nos-part`).
4. **Only the app's own folder** `<App>/` is ever touched on either card. Nothing else is read for deletion, and an `<App>` name with `/`, `..` or a leading dot is refused.
5. **B cancels** between files; what is done stays done and the journal says where it stopped.
6. When S's folder is empty at the end, it is removed. A one-line `MOVED-TO-DATA-CARD.txt` is not left: the app's own check is how the next launch knows.

**CLAUDE.md rule 7 (the games card is the user's; scripts never delete from it) gets a third exception for exactly this**: `r3i6nos-appdata` may delete files from `<App>/` on the system card, only after a person chose **Move all** or **Merge** in the dialog, only after the copy of that file is verified on the data card, never anything outside `<App>/`. It never formats or repartitions.

## One engine, thin frontends (the rule 15 way)

The logic is written once, in `overlay/usr/local/bin/r3i6nos-appdata`:

- `state <App>`: prints `key=value` lines (`card=valid|none|readonly|full|…`, `where_s`, `where_d`, bytes and counts for each, `conflicts=n`, `choice=…`, `action=none|offer-move|offer-merge|needs-card|resume`) and the folder to use.
- `path <App>`: the folder to use, no question, no change (for tools, reports, Rec demo). Warns on stderr when both have data.
- `move <App>` and `merge <App> data|system`: stream `STEP`, `PROGRESS n/total` and `DONE` or `FAIL reason` lines.
- `choice <App> keep|reset`: store or clear the "keep" choice (for a merge it also stores both folders' fingerprints, so a change on either side asks again).
- `used <App> data|system`: the frontend says where the app is now using its data; that is how `needs-card` knows the app's data is on a card that is out.
- `path <App> [--create]`: the folder to use, no question, a warning on stderr when both cards have data.

The frontends only draw it: **`theme/r3i6nos_appdata.h`** (C: `r3_appdata_init(app)` runs `state`, shows the list dialog and the progress bar with the existing theme pieces, calls `move` or `merge`, sets `R3I6NOS_APP_DATA`, returns the path) and **`theme/r3i6nos_appdata.lua`** for LÖVE apps. A native app calls `r3_appdata_init` once, after the splash. **LÖVE apps follow the same rule** (decided 2026-11-17): every app made for r3i6nos (rule 8: the theme's, so the IO Checker's LÖVE builds, KlamANSI, NoCoGaMa, the tools' reports, and the menu's own `r3i6nos/` report folder) calls `appdata.init("<App>")` from `theme/r3i6nos_appdata.lua` after its splash, gets the folder, and writes user data there. LÖVE's `love.filesystem` writes only to its save directory (in the root filesystem, which a reflash replaces and which is not on a card), so **user data goes through `io.open` on the returned folder** (`love.filesystem.mount` can read it with LÖVE's API); the save directory holds only what can be lost (caches, a setting that is rebuilt). `appdata.init` runs the same dialogs (`theme/r3i6nos_ansiapps.lua` list dialogs and the bar) over the same engine. **A third-party LÖVE game from `Love 11.5/` or `Love 12.0/` is not an r3i6nos app**: it knows nothing of this, keeps its own save directory, and is only listed. The app's `app.conf` names its folder with `DATA_DIR=<App>` (a package's `package.json` has `data_dir`), which is how the menu and Settings > Storage find what to show and move.

A test fakes both cards (temp folders, `R3I6NOS_*` overrides, as `r3i6nos-cards`'s tests) and checks each row of the table, a conflict, a power-cut at every step (kill the process after each file and resume), a full target, a read-only target, an `<App>` name that tries to escape, and that nothing outside `<App>/` is ever touched.

## What each app must do, native or LÖVE (section 30 tracks them)

- Call `r3_appdata_init` (LÖVE: `appdata.init`) after its splash and use the path it returns for **every** write and for reads of user data. No other code builds `/roms/…` or `/roms2/…`.
- Say in its README which files are app data (the move takes the whole folder; an app with a cache that must stay local keeps it under `/var/cache`, not in `<App>/`).
- Its **Device report** and **Rec demo** write to `$R3I6NOS_APP_DATA` (`r36s-report.txt`, `Demo/`), unattended, without the question (rules 9, 11).
- An app works with **the card out**: it says "Needs the data card" and quits or offers the system card, never a crash or a blank screen.

## Untested on the unit

All of it. To find out: a data card in TF2 with an app's folder only on the system card (the move), then the same folder on both (the merge), then each card out; plus how long copying 1 GB takes on the two cards (exFAT, ext4), and whether the data card's write after a hot-pull leaves a journal that resumes.

## Built: the engine (2026-11-17)

`overlay/usr/local/bin/r3i6nos-appdata` (Python stdlib, the login user, no root) and `tests/test_r3i6nos_appdata.py` (41 tests: every row of the table; read-only, unsupported, full and absent data cards; keep and its reset; needs-card; the app-name rules; move, merge in both directions, conflicts with the newer file winning and the older kept, equal times; links and special files left alone; names a FAT-like target cannot hold, including names that differ only by case; a target with no room; a low battery; a failed verification; an app folder that is a link; **an interruption after every possible number of files, then a rerun, compared with an uninterrupted run**; a stale half copy; the FAIL grammar). Two deliberate breakages of the engine (delete the original before verifying; skip the verification) were each caught by the tests. Run on a Mac and in Debian 12 arm64.

What it does beyond the design above: an empty folder in the source is kept (made in the target before its files); the target's free space is checked against only what has to be copied; `.r3i6nos-card` (the card's id) is made by the first `move`, `merge`, `choice keep` or `used data`, and, since 2026-11-17, by Settings > Data card > Format (`r3i6nos-format` writes it, so a freshly formatted card has an id from the start); the `needs-card` memory is the login user's `used.json`.

Not built: the C and Lua frontends (`theme/r3i6nos_appdata.h`, `.lua`: the dialogs and the progress bar), `DATA_DIR` in `app.conf`, the menu greying an app whose card is out, Settings > Storage, and every app's call. **Untested on the unit**, including whether the cards file is what this reads at that moment and how long 1 GB takes on each card.

## Built: the C frontend and Commander (2026-11-17)

**`theme/r3i6nos_appdata.h`** (header only; includes the theme and the toast; `_GNU_SOURCE` first): `r3_appdata_init(&data, "Commander", &ui)` asks the engine (`state`), shows the question in the theme when there is one (a framed box of text over a list, **default Not now**, A chooses, B is Not now; Quit is the default for "Needs the data card"), runs the move or merge behind a progress box (the step, the bar, `n of N files`, the time; **B stops it between files**, the engine finishes at the next launch), tells the person why when it failed, then asks the engine again for the folder and exports it as `$R3I6NOS_APP_DATA` (and the data card's root as `$R3I6NOS_DATA_CARD`) after telling the engine where the app now keeps its data (`used`). `r3_appdata_quiet` is the same with no dialog, for `--report`, `--rec-demo` and the viewer. A caller that already set `$R3I6NOS_APP_DATA` is obeyed; no engine (a laptop, an old image) means the old place and a notice, so the app still works. The hot-plug toast keeps showing during the dialogs and eats its key. The dialogs wait by napping 20 ms, not `SDL_WaitEventTimeout`, which wakes about 920 times a second under KMSDRM (`docs/POWER.md`).

**`tests/test_appdata.c`** (in `scripts/check.sh`; needs python3 for the real engine): the preset and no-engine cases, no data anywhere, quiet never moving, the move offer's wording and default, B, Move all (the file really on the data card, gone from the system card, a progress screen shown), settled afterwards, Keep and not asked again, the merge offer's wording and the conflict count, merging into either card, Don't ask again, Needs the data card (Quit, then the system card), a full data card and an unsupported filesystem (a notice and no dialog), a failing engine (its reason shown), and **B during the work** (against a fake slow engine that handles SIGTERM). Passed under clang on a Mac and gcc in Debian 12 arm64.

**Commander** (`apps/commander-native/commander.c`): the splash (rule 12, no build line yet) shows first, then `r3_appdata_init` (`--report` and `--view` use `r3_appdata_quiet`), then `app_init` and the settings load, so `settings.conf` is read from the folder that was chosen; `--report` writes `r36s-report.txt` there; `--selftest` sets `$R3I6NOS_APP_DATA` to a throwaway folder. Run against fake cards under SDL's dummy driver: `--report` wrote into the data card's `Commander/` with no dialog; a normal start with nothing anywhere settled on the data card and recorded it; with data on the system card it stopped at the question. A notice (a full or read-only data card) shows on the reply line.

Not done at that time: the Lua frontend, the IO Checker, every other app's call, `DATA_DIR` in `app.conf`, the menu greying an app whose card is out, Settings > Storage, Commander's Rec demo (it has none yet: rule 11) and its own build line on the splash. **None of it has run on a unit**: the dialogs on the panel, the pad's buttons in them, and whether the cards file is ready when an app starts.

## Built: the Lua frontend and the native IO Checker (2026-11-17)

**`theme/r3i6nos_appdata.lua`** (`appdata.init(app, {toast=})`, `appdata.quiet(app)`; the same words, defaults, and engine calls as the C header): `init` is **modal**: it runs its own loop (`love.event.pump`, `theme.draw`, `love.graphics.present`, a 20 ms nap, never a busy loop) until the question is answered and any move or merge is finished, so the call reads like C's: after `theme.init()` and the splash, before heavy work. It exports `$R3I6NOS_APP_DATA` and `$R3I6NOS_DATA_CARD` with LuaJIT's ffi `setenv`, so what the app starts sees them. **A move or merge runs in the background** (`sh -c 'echo $$ > pid; exec r3i6nos-appdata move APP > out'`, polled each frame), not in a `love.thread` (the IO Checker has that module off, and a thread cannot be stopped from a blocking read): the bar keeps drawing, **B** sends the engine SIGTERM (it stops between files), and a process that died with no `DONE` or `FAIL` line is reported as stopped (a zombie is not taken for a live one). The hot-plug toast shows over the dialogs and eats its key (`toast.key`). Pad buttons are LÖVE's raw numbers through `theme.PAD`. A caller that already set `$R3I6NOS_APP_DATA` is obeyed; no engine means the old place, `$R3I6NOS_ROMS` or `/roms`, and a notice. **`tests/test_appdata_lua.lua`** (in `check.sh`, luajit only): the same cases as `tests/test_appdata.c` against the real engine on fake cards (every row of the table, merge both ways, keep and don't-ask-again, needs-card, a full card and an unsupported filesystem), a failing engine, a slow engine stopped with B (and B pressed before the engine has said its pid is remembered and sent once it has), and the toast. **Not yet run inside LÖVE** (no LÖVE app calls it): the modal loop on the panel, `love.graphics.present` after `theme.draw`, and the pad's buttons in it.

**The native IO Checker** (`apps/io-checker-native`): the splash, then `r3_appdata_init("IO_Checker")` (`--report` uses `r3_appdata_quiet`), then everything else. Its app data is `r36s-report-native.txt` in `$R3I6NOS_APP_DATA`; the reply line and the Report page say whether it went to the "Data card" or to "EASYROMS". `DATA_DIR=IO_Checker` is in its `app.conf` (nothing reads it yet). **The Files page's own checks are not app data:** they make an `IO_Checker` folder on every card they test (the games card, the data card, a USB stick), write and delete small files and a speed file there, and leave nothing. That is testing the card, so it keeps its place on each card; an empty `IO_Checker/` left on the system card is not "data" to the engine (it counts files). The folder's name is the engine's app name, `IO_Checker`, so the existing reports move with a Move all. Run against fake cards under SDL's dummy driver: `--report` wrote to the data card's folder, to the system card's when only it had data (never moving anything), and a normal start with data on the system card stopped at the question. **Untested on the unit.** Still to do in it: Rec demo (rule 11), a build line on the splash, a card-out case in its self-test.

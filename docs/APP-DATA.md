# Where an app keeps its data: the data card first, and one place only

Status: **rule and design, 2026-11-17** (maintainer's request; CLAUDE.md rule 16). Nothing here is built. Roadmap: section 30. It finishes storage step 2 (`docs/STORAGE.md`: `R3I6NOS_APP_DATA`).

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
- **Keep on the system card:** the choice is stored (`/var/lib/r3i6nos/appdata/<App>.json`, with the data card's id) and not asked again until the data card's id changes or the person asks from Settings > Storage.
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
- `choice <App> keep|reset`: store or clear the "keep" choice.

The frontends only draw it: **`theme/r3i6nos_appdata.h`** (C: `r3_appdata_init(app)` runs `state`, shows the list dialog and the progress bar with the existing theme pieces, calls `move` or `merge`, sets `R3I6NOS_APP_DATA`, returns the path) and **`theme/r3i6nos_appdata.lua`** for LÖVE apps. A native app calls `r3_appdata_init` once, after the splash. **LÖVE apps follow the same rule** (decided 2026-11-17): every app made for r3i6nos (rule 8: the theme's, so the IO Checker's LÖVE builds, KlamANSI, NoCoGaMa, the tools' reports, and the menu's own `r3i6nos/` report folder) calls `appdata.init("<App>")` from `theme/r3i6nos_appdata.lua` after its splash, gets the folder, and writes user data there. LÖVE's `love.filesystem` writes only to its save directory (in the root filesystem, which a reflash replaces and which is not on a card), so **user data goes through `io.open` on the returned folder** (`love.filesystem.mount` can read it with LÖVE's API); the save directory holds only what can be lost (caches, a setting that is rebuilt). `appdata.init` runs the same dialogs (`theme/r3i6nos_ansiapps.lua` list dialogs and the bar) over the same engine. **A third-party LÖVE game from `Love 11.5/` or `Love 12.0/` is not an r3i6nos app**: it knows nothing of this, keeps its own save directory, and is only listed. The app's `app.conf` names its folder with `DATA_DIR=<App>` (a package's `package.json` has `data_dir`), which is how the menu and Settings > Storage find what to show and move.

A test fakes both cards (temp folders, `R3I6NOS_*` overrides, as `r3i6nos-cards`'s tests) and checks each row of the table, a conflict, a power-cut at every step (kill the process after each file and resume), a full target, a read-only target, an `<App>` name that tries to escape, and that nothing outside `<App>/` is ever touched.

## What each app must do, native or LÖVE (section 30 tracks them)

- Call `r3_appdata_init` (LÖVE: `appdata.init`) after its splash and use the path it returns for **every** write and for reads of user data. No other code builds `/roms/…` or `/roms2/…`.
- Say in its README which files are app data (the move takes the whole folder; an app with a cache that must stay local keeps it under `/var/cache`, not in `<App>/`).
- Its **Device report** and **Rec demo** write to `$R3I6NOS_APP_DATA` (`r36s-report.txt`, `Demo/`), unattended, without the question (rules 9, 11).
- An app works with **the card out**: it says "Needs the data card" and quits or offers the system card, never a crash or a blank screen.

## Untested on the unit

All of it. To find out: a data card in TF2 with an app's folder only on the system card (the move), then the same folder on both (the merge), then each card out; plus how long copying 1 GB takes on the two cards (exFAT, ext4), and whether the data card's write after a hot-pull leaves a journal that resumes.

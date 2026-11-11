# Crunchy on the R36S: the plan

Status (2026-11-10): **the first version is built in the Crunchy repo's `r36s/` and tested without the unit (its README has the
detail); run on a real R36S from 2026-11-10 (device report: 14 PASS, 0 FAIL; ROADMAP section 15).** Crunchy is ansiapps' de-duplicator (`~/Coding/crunchy`, closed source and paid on the desktop):
it finds copies of the same file, proves they are identical by content (size, then BLAKE3 of the first and last bytes, then of every
byte), and reclaims the space safely (Move to Trash, re-checked first, logged for Undo). Its desktop app is Tauri, React and Rust.
This file is the plan and the decisions, as `docs/NEUMETIK-PORT.md` is for Neumetik; the code lives in the Crunchy repo (an `r36s/`
folder), never here (rule 2). `ROADMAP.md` section 13 tracks it.

## 0. Decisions

Taken without the maintainer on 2026-11-10 (asked for: "do as much as you can without my input"), each the port convention's default;
**the ones marked open need the maintainer.**

1. **Where the code lives: the Crunchy repo**, `r36s/`, as Neumetik's and KlamANSI's. This repo keeps the plan, the theme, the pad
   mapping and `flash-macos.sh`'s entry.
2. **Shape: a C and SDL2 host linking a Rust static library**, as Neumetik. The library compiles the desktop app's own core files
   from `src-tauri/src` by path (`scan.rs`, `walk.rs`, `db.rs`, `volume.rs`, `clones.rs`, `cloudfiles.rs`, `trash.rs`, `undo.rs`,
   `smart_move.rs`), unchanged: only `lib.rs` and `first_run.rs` there touch Tauri, and every one of these already has its Linux
   path (the walk, `/proc/self/mountinfo` volumes, the freedesktop.org Trash). A fix on the desktop is a fix here.
3. **The first version: Find duplicates, Move to Trash, Undo.** Smart Move, Compress audio, Compress image, the `.diskettehashes`
   export and Diskette catalog sync are left out (section 5).
4. **Rule 8 with no departure:** the menu's 64 x 24 layout, list dialogs, the three-row command line, nothing scrolls.
5. **Decided (2026-11-11, maintainer): the release terms.** The desktop Crunchy is sold on itch.io; the R36S package carries
   Neumetik's grant (free on the R36S, unmodified), as every R36S port does. The DRAFT marker is gone from `r36s/package/copyright`.
6. **Decided (2026-11-11, maintainer): Empty the Trash.** Move to Trash on the R36S renames a file into the card's own
   `.Trash-<uid>` folder (the freedesktop.org rule for a file on another filesystem than home), so no space is freed until that
   folder is emptied, and nothing else on r3i6nos empties it (on a desktop the OS does). So the app has **Start > Empty the Trash**
   (and X on the Undo panel), after a question. It deletes for good, which the desktop Crunchy never does, so it is narrow: only
   files this app's own trash log lists as trashed and not undone, each checked against its logged size and BLAKE3 (a changed file
   stays in the Trash and is listed), with its `.trashinfo`; nothing else in the Trash is looked at (a person's own trashed files
   stay), and links are never followed. Undo stops offering what was deleted (the log row is marked `emptied`).
7. **Open: a GitHub workflow.** Neumetik's `r36s.yml` builds the package on GitHub; Crunchy's repo has no CI at all, so none was
   added. The package is built locally (`r36s/build-deb.sh`).

## 1. Shape

- **`libcrunchy_r36s.a`** (`r36s/lib`): the desktop core plus `engine.rs` (one job at a time on its own thread, the groups as a
  list, the selection with the desktop's rules) and `capi.rs` (the C API, `crunchy_r36s.h`). Dependencies are the desktop's own at
  its lock file's versions (serde, rusqlite with bundled SQLite, chrono, blake3, rayon, uuid, libc, log): no new code comes in.
  The binary needs only SDL2, glibc (2.36 or older symbols) and libgcc, all in the rootfs.
- **`crunchy`** (`r36s/host/crunchy.c`): the host, with `theme/r3i6nos_ansiapps.h` and `theme/r3i6nos_report.h` read from this repo.
- **The hash cache and trash log** stay on the system card (`~/.local/share/crunchy-r36s/crunchy.sqlite`, SQLite in WAL mode on
  ext4), never on the exFAT games card. The cache's paths are volume-relative (the desktop's rule 4), so a card keeps its hashes.
- **Fast start:** the splash shows at once with a bar (opening the cache, looking at the cards); the scan, Move to Trash and Undo
  run on a thread while the screen shows their progress, with an estimate of the time left.

## 2. The screen

| Rows | What |
|---|---|
| 0 | `Crunchy` and the three panels (the one showing in brackets); at the right, what is selected (`3 to Trash, 300 KB`) |
| 1-20 | The panel's list (whole pages, `Page n/m`), a group's copies, or a job's progress |
| 21 | Reply: the last result (`Moved 3 files to the Trash (300 KB).`) or a refusal (`Keep at least one copy.`) |
| 22 | Prompt: what A does here |
| 23 | Status: the selected file's path (`Games card/...`) or date, else the keys |

**Panels:** **Folders** (the games card and the data card when one is in, each with its top folders; the games card ticked),
**Duplicates** (the groups, biggest saving first, named by the oldest copy; `*` marks a group with copies selected), **Undo** (the
runs of Move to Trash, newest first, from the trash log). A group opens to its copies: the oldest first, `keep` or `Trash` at the right.

## 3. Controls (raw buttons, `theme/r3i6nos_ansiapps.h`'s `r3_pad_action`)

| Button | Folders | Duplicates | A group | Undo |
|---|---|---|---|---|
| Left / Right | panel | panel | (nothing) | panel |
| Up / Down, L1 / R1, L2 / R2 | move, page, top / bottom | same | same | same |
| A | tick a place | open the group | keep this copy, or send it to the Trash | put the run back (asks) |
| B | | | back to the groups | |
| X | scan the ticked places | Move to Trash (asks) | Move to Trash (asks) | |
| Y | | Select duplicates | Select duplicates | |
| Start | the menu: Select duplicates, Unselect all, Device report, Quit | | | |
| Select + Start | quit (asks while a scan runs; refused while files move) | | | |

B stops a running scan, keeping what it found. A scan's result selects every copy but the oldest of each group (the desktop's
autoSelect: oldest, then shortest path; never a read-only file); nothing moves until X and A in the dialog.

## 4. Safety (the desktop's rules, enforced in its Rust)

- Each file and a copy being kept are **checked again right before** a move (size and time, else read and hashed again); a changed
  file is not moved and is listed with why. At least one copy of every group is always kept. Read-only cards are keepers only.
- A move is a **rename into the card's own `.Trash-<uid>`** with a `.trashinfo` (freedesktop.org), never a copy and never a delete;
  every move is logged, and Undo renames back, never over a file that is there now (`rename_excl`), removing the `.trashinfo`.
- The walk skips hidden `.Trash*` folders, macOS `._` files and `.DS_Store`, and zero-byte files.
- The device report's own test files are in `Crunchy/report-test` and its trash test uses a separate hash cache and trash log, so
  the user's Undo list is never touched; it reports counts only, never a file's name (`docs/DEVICE-REPORTS.md`).

## 5. Left out of the first version

Smart Move (merge a folder into another by rules: needs a folder picker and its rule editor on the pad), Compress audio and Compress
image (their encoders are large C libraries, and slow on the Cortex-A35), the `.diskettehashes` export and Diskette catalog sync
(Diskette does not run on the R36S), APFS clones and resource forks (macOS only), (Empty the Trash is in, decision 6).

## 6. Testing

- **The desktop core's own unit tests** compile into the library with their modules, so `r36s/check.sh` runs them too (38 pass on
  a Mac); the engine has six Rust tests of its own (`r36s/lib/tests/engine.rs`): the groups and the keeper, the never-the-last-copy rule, Move to Trash and Undo end to end, a file
  changed after the scan, the C API's strings.
- **`crunchy --selftest [--shots DIR]`** drives every screen on a pretend card (dummy video): tick, scan, open a group, the
  last-copy refusal, the dialog, Move to Trash, Undo, the menu, and the device report.
- **`r36s/package/docker-package.sh`** builds `Crunchy.deb` in Debian 12 arm64, smoke-tests it, installs it with this repo's
  `deb_watcher.py` in a clean Debian 12 and writes a device report there.
- **Not testable here, so "untested" until a report says otherwise:** the speed of a scan on the unit's card and CPU, the
  freedesktop Trash on exFAT (the `.Trash-1000` folder owned by `loveos` through the mount's `uid=1000`), the pad, how the lists read.

## 7. The device report (rule 9)

Start > Device report and `crunchy --report` write `/roms/Crunchy/r36s-report.txt`: the system and the pad's GUID, the card (write,
rename over, read, delete, filesystem), the hash cache's filesystem, **the trash** (three 1 MB test files, a scan finds the one
group, Move to Trash puts the copy in `games card/.Trash-<uid>`, Undo puts it back), **BLAKE3's speed** from memory (goal 20 MB/s),
**a scan of the cards** for at most 60 s with a fresh cache (files, groups, bytes read; read-and-hash speed, goal 10 MB/s), and memory.

## 8. Phases (mirrored in `ROADMAP.md` section 13)

1. **Built** (2026-11-10): the library, the host, the package, the self-test, the report; tested on a Mac and in Debian 12 arm64:
   `docker-package.sh` built `Crunchy.deb` (2 MB; needs SDL2, libc, libm, libgcc_s; newest glibc symbol 2.34), its self-test passed,
   `deb_watcher.py` installed it in a clean Debian 12 and the launcher ran it, and a device report there had 12 PASS and 1 FAIL (no
   pad, as expected under Docker): the trash test moved and put back its file, the scan found the pretend card's copy.
2. **On the unit:** copy `Crunchy.deb` to `native_deb/` (`flash-macos.sh` does it), run Start > Device report, bring the report back.
3. **The open decisions** (section 0): a workflow. (The release terms and Empty the Trash are decided.)
4. **Later:** Smart Move on the pad, the hash export, Compress image (oxipng and WebP first: they have no `cmake` C builds).

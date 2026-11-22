# PocketIDE: a desktop editor for Adventure Game Studio games (ROADMAP section 42)

A desktop editor beside JoyrIDE (`~/Coding/joyride`, the LÖVE IDE: Tauri 2, React, Monaco), for the **AGS 3.6** engine that `r3i6nos-runtime-ags` runs
(`docs/RUNTIMES.md`). Why: AGS's own Editor is Windows-only, and with it no sample game for the engine can be made here (`tools/runtimes/samples/AGS/README.txt`).
**Status (2026-11-22): spiked, not started.** The repository does not exist yet (see "Where it lives").

## The spike (2026-11-22; `scripts/pocketide-spike.sh`, Docker, Debian 12 arm64)

Source: AGS tag `v3.6.3.2` (commit `56fad9d`, Artistic-2.0; the engine in the runtime reports 3.6.3.0). The script clones it into `build/ags-spike/`, builds, and checks.

**Answered yes: AGS's own tools build and run with no Windows and no Editor.**
- `Compiler/` is a standalone, cross-platform **script compiler, `agscc`** (CMake `-DAGS_BUILD_COMPILER=ON`, 230 KB). It compiles an `.asc` to a `SCOM` object with the built-in header
  `Editor/AGS.Editor/Resources/agsdefns.sh` (`-H`), and a wrong script is refused with its line (`Undefined token 'Dispay'`). Quirk of this version: give the input last and take the default
  output `INPUT.o`; `-o OUT.o` with a separate value mis-parses.
- `Tools/` builds `agspak` (the game package: CLIB archives), `crmpak` and others (`agfexport` reads an AGS project `Game.agf`, `agf2dlgasc` turns dialogs into script).
- `Common/game/room_file.cpp` has **`WriteRoomData`**: a `.crm` room file writer in portable C++, reusable by a small tool of ours.

**Step 1 answered (2026-11-22): the Editor's C# writer runs off Windows, and the engine opens what it writes** (`spike/run.sh` in the `pocketide` repo; see its ROADMAP). Below, what the spike started from.

**Not answered at the time: a whole game made by us and run by the engine.** What is missing is the writer of the main game file (`ac2game.dat`: game setup, characters, views, GUIs, dialogs, the compiled scripts, extension blocks).
It exists only in the Editor, as C# (`Editor/AGS.Editor/DataFileWriter.cs`, 2008 lines; `SaveThisGameToFile` and `WriteGameSetupStructBase` are about 550 of them, with the extension blocks and the CLIB packing the rest),
next to `Editor/AGS.Native/` (about 6000 lines of C++/CLI, Windows). The repository holds no compiled game or room to compare with (no `.crm`, `ac2game.dat`, `.agf` or template).
The engine reads what that writer makes (`Common/game/main_game_file.cpp`), so **the engine is the test**: every file we write can be run in Docker under Xvfb, as `Hello_Godot.pck` is.

## The plan, in the order that cuts the risk first

1. **Decide how to write `ac2game.dat`** (ROADMAP 42 step 1): (a) run the Editor's own C# writer on .NET on the Mac (`AGS.Types` and `DataFileWriter.cs`; it may need `System.Drawing`, which .NET no longer ships off Windows),
   or (b) port it to Rust or C++, with the engine's reader as the check. (a) is the cheaper if it builds; (b) is the one that leaves no .NET in the app.
2. **A command-line builder** (step 2): a project folder (scripts, a room, a few sprites, `game.json`) to `ac2game.dat` plus rooms plus the sprite file, packed with `agspak`'s code, that the 3.6 engine runs. The first game it makes is `Hello_AGS`, which is also the missing sample.
3. **The app** (step 3), the JoyrIDE way: Tauri 2, a Monaco mode for AGS Script, a Run button that starts the engine, a console. 4. **The editors** (step 4), one at a time in the order a game needs them: room (background, walkable areas, hotspots, objects), sprites, characters and views, dialogs, GUIs.

## Where it lives, and the name

**PocketIDE** is the name (maintainer's, 2026-11-22; first called AGSrIDE). Its repository is `~/Coding/pocketide`, beside JoyrIDE, created 2026-11-22 with a README, CLAUDE.md, ROADMAP and CHANGELOG; its `ROADMAP.md` now holds the order of work. The spike and this document stay here, where the script and the runtime are.
The name no longer says "AGS", which removes the clash with the engine's own name. The README says what it is: an unofficial editor for the open-source AGS engine, not made or endorsed by the AGS team; the licence and asking the AGS team are the maintainer's.

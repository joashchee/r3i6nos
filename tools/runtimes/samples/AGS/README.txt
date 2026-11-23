AGS: no sample game is made here, and no folder is copied to the card for it.

An AGS game is a compiled `ac2game.dat` (or `.ags` / `.exe`) made by the AGS Editor, a Windows program; nothing in this repository can produce one,
and a game found elsewhere is not ours to ship (CLAUDE.md rule 2). To try the engine on the unit, bring one that is yours or freeware (the AGS games
database lists freeware games and their licences) and put its folder at `<card>/AGS/<Game>/`, with `ac2game.dat` or the `.ags` / `.exe` inside.
If you make a tiny one with the Editor (one room, one character, a walk), release it CC0 and put it here as `AGS/Hello_AGS/`.

Status (2026-11-23): still no sample. PocketIDE (~/Coding/pocketide, ROADMAP step 2) can now write a one-room game that the 3.6 engine loads, gets through "Engine initialization complete",
"Loading room 1" and "Now in room 1" in Debian 12 arm64 under Xvfb, and then dies with a segmentation fault at the first frame (`scripts/build-hello.sh --engine .../ags_3.6.aarch64`). Until that
game runs, nothing is shipped here.

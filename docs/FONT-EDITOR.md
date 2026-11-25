# Font Editor: touch up ANSIapps Mono (and other bitmap fonts) on the R36S

Made 2026-11-24 at the maintainer's request, from the Perplexity reference `docs/making-r36s-font-editor.md`: **a font editor for the R36S that touches up ANSIapps Mono (ANSIapps Mono until 2026-11-25) and, in time, other fonts, with the
font's "multi-character" glyphs as the first thing it does well.** `apps/fontedit-native/` (C and SDL2, on the r3i6nos-ansiapps theme, 64 x 24 cells), built as `Font_Editor.deb` (`scripts/build-native-local.sh fontedit-native`).
It is not in the image (not `DEFAULT=1`): the `.deb` goes in `native_deb/`. **Status: built and self-tested on a Mac and in Debian 12 arm64 (GCC, SDL 2.26, under qemu); nothing has run on a real R36S** (rule 1).

## Why it is not what the reference describes

The reference lists Glyphr Studio, FontForge and FontTools: **outline** editors (Bézier points and handles, TrueType export). ANSIapps Mono is not an outline font. It is a **pixel bitmap font drawn in its own repository** (`ansiapps-mono-font`, whose build makes the 10 x 20 size from the 8 x 16 pixels; ROADMAP section 43), whose TTF is made from the pixels afterwards. So what the reference says about **the controller** (SDL's abstraction, a persistent legend, the D-pad
and stick, undo and redo, one glyph at a time, a first milestone of "browse, open one glyph, move points, undo, export") is taken; what it says about **outlines** is not (no points, no handles, no `cu2qu`; the
editor draws pixels). The reference's "use SDL's generic controller mapping" is replaced by the repository's own measured mapping (`docs/r3i6nos-mapping.md`, which says SDL's is wrong for this pad). Export is not a
TTF: the editor writes the cells it changed, and the font build (which already makes the TTF, WOFF2, BDF, PSF and the C and Lua tables) applies them (below).

## The multi-cell glyphs

ANSIapps Mono has **2,247 names (with each row of a tall glyph); 677 of them are more than one cell** (2,219 glyphs, of which 619 are two cells across and 30 bigger: four across, or two down): the gamepad's shoulders, triggers, D-pads, sticks and pills, the on-screen keyboard's key caps, switches, waveforms, knobs, every app, system and
file icon, and the `.wide` and `.tall` versions. A name such as `icon.app.hydrolik` is two consecutive code points (`U+10FD14`, `U+10FD15`), printed together as one string, and is **drawn as one 20 x 20 picture that is cut in half**: an edit that
ignores the seam breaks the icon. So the editor opens a **name**, not a cell:

- **The canvas is the whole group**, a pixel being two text cells, 20 x 20 pixels (40 columns) for a pair, drawn straight across the seam. The two cells are told apart by colour (white and green), so you always know which
  code point a pixel belongs to. A line, a box or a fill crosses the seam as if it were not there.
- **The first panel (Multi-cell glyphs) is the list of those 508**, each row showing the glyph at its real size, so a pair is chosen by how it looks. **Fn** turns to Named glyphs (all 884), Code points (all 2,919) and Changed.
  Opening a code point that belongs to a name opens the whole name; **Y > Edit this cell alone** opens just that one.
- **The side shows the group at its real size, in a line of text between H and x, and at three times**, and the **Sample** panel (Fn in the editor) shows it twice and three times the size, joined in a row, and beside
  a line of the font's own letters: so a change is judged at the size it is used, and how it joins to its neighbour is seen at once.
- **Cell commands for a pair** (L2 + R2): mirror the first cell onto the last (a symmetric button), mirror the last onto the first, copy either onto the other, swap them. Flip left-right turns the whole group, not each cell.
- Groups are one to `FE_MAXCELLS` (4) cells; ANSIapps Mono has some four across (the piano's black keys, wide) and some two down. The canvas is 2 text cells a pixel when it fits (up to 20 pixels wide), else 1.

## Controls (the mapping's defaults, `docs/r3i6nos-mapping.md`; the Keymap in the main menu shows them)

| Where | Input | Does |
|---|---|---|
| The list | D-pad up/down, R1 + up/down, R1 + left/right | move; page up/down; first/last (whole pages of 18, `Page n/m`, nothing scrolls) |
| | left/right, **Fn** | previous / next panel |
| | A | open the glyph in the editor |
| | X | revert the glyph (asked first, starts on No) |
| | Y | More: edit one cell, revert, revert every change, save now |
| The editor | D-pad (and the right stick) | move the cursor one pixel; **hold A or X and move: the Pen draws or erases as it goes** |
| | R1 + up/down, R1 + left/right | the cursor 5 rows; to the left or right edge |
| | A / X | ink / erase (Pen); set a corner, then finish (Line, Box, Rectangle); fill (Fill) |
| | Y | next tool: Pen, Line, Box, Rectangle, Fill |
| | B | cancel a corner, else back to the list |
| | **Select / Start** | the previous / next glyph in the list (what is drawn is saved first) |
| | **Fn** | Draw / Sample |
| | L2 + R2 (keyboard `\`) | Commands: undo, redo, next tool, flip, invert, clear, shift 1 px, cell commands, revert, guides, save |
| Anywhere | L1 + R1 (keyboard `` ` ``) | main menu: About, Colour theme, Keymap, Device report, Rec demo (not built), Quit |

Keyboard: the documented bindings (W A S D, I J K L, F, H, T, Q E O U). **Only the main menu's Quit leaves the app** (rule 17). One-touch buttons depart from the defaults where the editor's job needs it: Y is "next
tool" (not a menu) and Select and Start step through the list, since the editor has no virtual mouse; the Keymap says so. The editor has no virtual mouse or keyboard (L3 and R3 say so).

## Where an edit goes, and how it reaches the font

The font's source is its own repository (`~/Coding/ansiapps-mono-font`: pixel files, a provenance log, the maintainer's approval of each batch, the rule that no other font's shapes are used), so a touch-up made here is
**an edits file to carry there**, not a change to this repository's generated tables (which `scripts/build_ansiapps_font.py` overwrites):

1. **The editor saves after every stroke and every command** to `<app data>/edits-ansiapps-mono.txt` (`FontEdit/` on the data card when a valid one is in, else on the system card; rule 16), through a temporary file
   and a rename, so a power cut leaves the old file or the new. Only the cells that differ from the font are in it. A card that cannot be written says `NOT SAVED` (top bar and reply row) and the editing goes on in
   memory; **Save now** tries again. A file that cannot be read at start is kept as `.bad` and never overwritten.
2. **Folding it in is open** (ROADMAP section 44): the font's repository has no importer for this file yet. The old path, `scripts/merge_font_edits.py` into R3i6nos Mono's `edits.txt`, went with that font (2026-11-25).
   Until the importer exists, a redrawn glyph is copied by hand into the font's pixel source and logged in its provenance (the maintainer's own drawing, so rule one is kept).

The file is text and diffable (`.` paper, `#` ink, 20 rows of 10, `cell <hex code point> <name> <part>/<of>`; the name is checked against the font, so a file for an older font is refused, not applied to the wrong cell).
`fe_model.h` reads and writes it in C; **`fontedit.c` carries one file as a string and its self-test reads and writes it byte for byte**, so the format cannot drift while the font's repository has no reader (the Python reader, `scripts/r3font/edits.py`, and its test went with ANSIapps Mono).

## Other fonts

`fontedit --font FILE.bdf` opens a **monospace, cell-shaped BDF up to 16 x 20** (every glyph is placed into the `FONTBOUNDINGBOX` cell by its own `BBX`; pixels outside the cell are counted and said). There are no
names, so the list is the code points. It is saved **as a BDF to `<app data>/<font name>.bdf`** with its header kept and every glyph written as a whole cell; **the file read is never written**. ANSIapps Mono's own
BDF reads back the compiled font exactly (2,919 of 2,919, checked by the self-test), and a written BDF reads back equal. Not yet: proportional fonts, taller cells than 20 rows (a canvas that needs paging),
names for a BDF's multi-cell glyphs (a sidecar file is the idea), other formats (PSF, TTF).

## What it does and does not do

Does: browse by multi-cell glyph, name, code point and change; draw with Pen, Line, Box, Rectangle, Fill; undo and redo across glyphs (256 steps); flip, invert, clear, shift, mirror, copy, swap; revert one glyph or
all; guides (cap line, x-height, baseline) that can be turned off; the colour themes; a device report.
Does not (yet): select, copy and paste a rectangle or take a picture from another code point; add a glyph or a name (touch-ups only: names are append-only and belong to the generators); outline editing and TTF
export (above); remap buttons (the C theme's Keymap has help and test only); a Rec demo; a benchmark item (hidden: nothing to measure against a goal but the report's redraw time).
The screen mirror shows cells, so it does not show the edited glyph pictures the editor paints itself (`r3_render_hook`, below).

## How it draws an edited glyph

The theme's screen is a grid of cells whose glyphs are the font **compiled into the program**, so an edited glyph cannot be one of them. The editor's canvas is made of block characters (two cells a pixel); the glyph
previews (in the list, the side, the Sample panel) are **painted over the cells by a hook**: `theme/r3i6nos_ansiapps.h` gained `r3_render_hook` (a function pointer, NULL by default, called last by `r3_render` with the
finished pixels), and the editor repaints the cells it owns from its own edited tables. The video repaints when a signature of those glyphs changes (cells alone would not show an edited pixel). A dialog over the
screen clips the glyphs under it. The self-test compares the painted pixels with the model, pixel for pixel.

## The rules (CLAUDE.md)

| Rule | State |
|---|---|
| 8 theme, 64 x 24, ANSIapps Mono, 16 colours + the 13 themes | on the theme; whole pages, nothing scrolls; every pair is the theme's |
| 9 device report | `Device report` (and `--report`): system, the font (tables, counts), the edits file written and read back and compared, the card, redraw speed, memory, power; progress bar; `FontEdit/r36s-report.txt`. Self-tested; not run on a unit |
| 10 build script | `scripts/build-native-local.sh fontedit-native` (Docker, Debian 12 arm64, smoke test from the packed `.deb`): run, passes |
| 11 Rec demo | **not built** (the menu item says so): ROADMAP section 44 |
| 12 splash | at once, with the version, commit and date, the load's progress and any load error on it |
| 13 hot-plug toast | polled and drawn; **untested on a unit** |
| 14 power section | in the report: the app's idle window (10 s) against the idle goals; no "working" window yet (needs Rec demo's tour) |
| 15 nttplay | not used |
| 16 app data | `r3_appdata_init("FontEdit")` after the splash; data is `edits-ansiapps-mono.txt` (or `<font>.bdf`), `settings.conf`, `r36s-report.txt`; the card-out case is self-tested (a folder that cannot be written) |
| 17 main menu and mapping | the six fixed items in order (Benchmark hidden), only Quit leaves, the chords, the Keymap (two pages and the test); self-tested |
| 18 progress | the report's bar (`step = steps` checked); the editor has no other waits (a load is instant) |

## Checks

`scripts/check.sh --app fontedit-native --theme`: compiles with `-Werror` and runs `--selftest`: the model (counts 2,919 / 884 / 508 multi-cell, two consecutive code points for a pair), every canvas tool across a seam,
undo and redo, the edits file (round trip, byte-identical, five kinds of bad file refused and the font left alone, the Python fixture), the BDF (read equals the compiled tables, written and read back, a 24 x 32 and a
non-BDF refused), then the app through the pad's own buttons (open a pair, hold A and draw across the seam, undo through the commands, Box across the seam, the painted glyph against the model, the panels, revert
asked first, the main menu, Keymap, theme, Quit, the card out, the report's steps). The Python side (`tests/test_fontedit_edits.py`) went with R3i6nos Mono's build. Screens: `R3I6NOS_FE_SHOTS=dir fontedit --selftest`, or
`fontedit --screenshot out.bmp [browse|edit|sample|commands|menu]`.

**Not run on a unit:** how 2 cells a pixel reads at arm's length on the 640 x 480 panel, whether hold-A-and-move is comfortable on the D-pad (and the right stick's 6 steps a second for drawing), the colours of
the two cells (white and green) under the 13 themes, the hot-plug toast, the data card paths, and the idle power.

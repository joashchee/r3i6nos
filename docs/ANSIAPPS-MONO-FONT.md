# ANSIapps Mono: the font of every ansiapps and r3i6nos app

**ANSIapps Mono is the one official font of the ansiapps family and of every r3i6nos app** (decided 2026-11-25). It is a 10 x 20 pixel monospace bitmap font here (the font's own
repository, `ansiapps-mono-font`, also makes an 8 x 16 size): **4,583 code points and 2,219 named glyphs** (gamepad buttons and states, keys, a piano, music notation, UI widgets,
system status, coding symbols, two-cell app and file icons, the **portrait maker's 692 parts** and the **game glyphs**), with **113 wide and 28 tall counterparts** of glyphs
that otherwise take one cell. It is **clean-room** (drawn from a written brief and the Unicode character names; no other font's shapes were used) and **SIL Open Font License 1.1**
(Reserved Font Name "ANSIapps Mono"). It replaced R3i6nos Mono and, under it, the IBM VGA 8x16 font (VileR, CC BY-SA 4.0): **no CC BY-SA file is in this repository any more**.

**Status:** the tables are generated and checked on a Mac (C, Lua, JSON, the mirror page, the IO Checker's self-test, `tests/test_ansiapps_font.py`). **Not yet looked at on a real
R36S**, and the font itself is the maintainer's to approve (its repository's roadmap: "10 x 20 size ... pending approval").

## The policy

1. **Every app made for r3i6nos, and every ansiapps text-mode app, draws with ANSIapps Mono.** A native C app gets it from `theme/r3i6nos_ansiapps.h`; a LÖVE app from
   `theme/r3i6nos_ansiapps.lua` (`T.init()` turns `T.useMono(true)` on). **There is no second font:** the 256 code page 437 characters are the font's own glyphs too
   (`theme/font/ansiapps-mono/ansiapps_cp437.h|.lua`: byte, then the code point it stands for, then that glyph), so `R3_WITH_MONO` is always on and `R3_NO_MONO` is gone.
2. **Print a glyph by name, never by code point.** Names are the interface and never move. `r3_puts_mono(s, col, row, r3mono_name("pad.btn.A.on"), fg, bg)` in C;
   `scr:glyph(col, row, "pad.btn.A.on", fg, bg)` in Lua. The identifiers keep their old prefix (`r3mono_find`, `r3mono_name`, `R3I_*`) so no app changed.
3. **A glyph more than one cell is one string, printed together**: a two-cell icon on one row, never split, never in two colours. A glyph more than one cell *tall* (the `.tall`
   names and the two-row icons) is printed row by row: `<name>` on the row, `<name>.row2` on the row below, at the same column (`r3mono_entry(name)` says `cols` and `rows`).
4. **A wide or tall counterpart is chosen by name:** `piano.bk.off`, `piano.bk.off.wide` (twice across), `piano.bk.off.tall` (twice across and down). Every `.wide` and `.tall`
   is exactly that size of its base (a self-test and the report check it).
5. **State is a glyph, not only a colour** (off, on, disabled; a level).
6. **A new glyph is drawn in the font's repository** (`~/Coding/ansiapps-mono-font`, its `CLAUDE.md` rule one: clean-room, provenance log, the maintainer approves each batch), never in an
   app and never in this repository's tables. Then run `scripts/build_ansiapps_font.py` here and commit what it changes. An app never carries its own copy of the font.
7. **Say what was not run on a unit** (CLAUDE.md rule 1).

## Files (all in `theme/font/ansiapps-mono/`, all generated: do not edit)

| File | For |
|---|---|
| `ansiapps_mono10x20.h` | the bitmaps for C (`r3mono_glyph`, `r3mono_cp`, `r3mono_idx`, `r3mono_find`): the font's own header, unchanged |
| `ansiapps_mono10x20.lua`, `ansiapps-mono.json` | the same for LÖVE, and for web canvases and the screen mirror's page (the JSON also has every name with its size) |
| `ansiapps_mono_names.h`, `.lua`, `glyph-names.json` | every named glyph: its UTF-8 string (`R3I_*` macros, `r3mono_name`), its code point, and its size in cells (`r3mono_entry`, `shapes[name]`) |
| `ansiapps_cp437.h`, `.lua` | code page 437 on the font (byte to code point, the 10x20 bitmap by byte, the 8x16 bitmap for ANSI art, the glyph hash both themes check) |
| `ansiapps_portrait.h`, `.lua` | the portrait maker's parts, slot kinds, layouts, joins and the font's own join counts (for the IO Checker's checks) |
| `AnsiappsMono10x20-Regular.bdf` | the bitmap font (Pyxel, X11, the Font Editor) |
| `OFL.txt`, `README.txt`, `glyph-coverage-report.txt` | the licence, the credit, the live counts |

`scripts/build_ansiapps_font.py` reads the font's repository (`../ansiapps-mono-font`, or `$ANSIAPPS_MONO_DIR`; build it there first: `node tools/build.mjs && node tools/build10x20.mjs`).
`--check` writes nothing and exits 1 when a committed file differs; `scripts/check.sh` runs it when the repository is beside this one and says NOT RUN when it is not.
`tests/test_ansiapps_font.py` checks the committed tables against each other whether or not it is.

## How the theme draws it

- **C:** `r3_glyph_bit(ch, x, y)` reads `ansiapps_cp437_10x20[ch][y]`; an extended cell (`r3_put_mono`) is any code point of the font, found by `r3mono_find`. `R3_GLYPH_HASH` is the FNV-1a of
  all 256 character bitmaps; `r3_glyph_hash()` in C and `T.glyphHash()` in Lua both give it (`tests/test_theme.lua`, `commander --selftest`).
- **Lua:** `T.glyph10x20(index)` is the font's bitmap of a code page 437 character; the extended glyphs come from `ansiapps_mono10x20.lua` on first use (`T.useMono`, `T.glyph`).
  The old nearest-neighbour scaling (`T.COLMAP`, `T.ROWMAP`) is gone: a pixel is the font's, not a stretched 8 x 16 one. `T.glyph8` stays for tools that draw 8 x 16.
- **The 10 x 20 size is made from the 8 x 16 pixels** by the font's repository: text by inserting lines so one-pixel strokes stay one pixel, *tiling glyphs* (box drawing, blocks,
  Braille, sextants, octants, Powerline, geometric shapes and every Plane 15 and 16 glyph) by repeating columns 0 and 7 and rows 0, 4, 11 and 15, so edges between cells meet exactly as before
  (a half block is 5 columns or 10 rows; light box lines are 1 pixel, heavy 2). That is why the portrait maker's joins survive (below). Details: the font repository's `docs/SIZE-10X20.md`.
- **ANSI art** (Commander's viewer) draws with the font's 8 x 16 size (`ansiapps8x16`), 80 columns as ever.
- **The screen mirror's page** (`overlay/usr/share/r3i6nos/mirror/page.html`, `scripts/build_mirror_page.py`) embeds the 10 x 20 glyphs as base64; `tests/test_mirror_page.js` compares every pixel.

## What changed for the apps

- **Code page 437 looks different.** It is no longer VileR's VGA font scaled; it is ANSIapps Mono's own drawing of the same 256 characters at the same cells (same box lines, blocks and metrics).
  Text-mode art made of CP437 still lines up; letters, digits and the pictures (the faces, card suits, arrows) have a new design.
- **Not in the font yet: 519 code points R3i6nos Mono had** (`glyph-coverage-report.txt` has the live counts; the font's roadmap has them). No app printed them (checked 2026-11-25); the IO Checker's old page did, and was changed.
  In runs: superscripts and subscripts U+2070-208E; `↩ ↪ ↺ ↻ ⇕ ⇦⇧⇨⇩`; `∇ ≢ ⊢ ⋮ ⋯`; the keyboard pictures `⌃ ⌘ ⌥ ⌦ ⌨ ⌫ ⏎` and `⌕`; the media controls `⏩ ⏪ ⏭ ⏮ ⏯ ⏱ ⏳ ⏴-⏺`; the whole control-pictures block U+2400-2421 and `␣`;
  circled digits `① ... ⑳`, `⓪`, `❶ ... ❿`; most geometric shapes (`△ ▷ ▽ ◁ ◇ ◈ ◉ ◌ ◐-◓ ◖ ◗ ◢-◥ ◯ ◻-◾`); `★ ☆ ♡ ☀ ☁ ☐ ☑ ☒ ☰ ☾ ⚑ ⚙ ⚠ ⚡ ✉ ✏ ✓ ✔ ✗ ✘ ✦ ✧ ➔ ➤ ⬛ ⬜`; the music symbols `♩ ♬ ♭ ♮ ♯` and Plane 1's `U+1D100-1D15F`;
  the eighth blocks U+1FB7C-1FB8B; Powerline U+E0A3 and U+E0B8-E0BF; and **`U+F000 + byte`** (the raw code page 437 addressing the old font had). Use the named glyphs (`code.ok`, `ui.check.mixed`, `sys.*`, `staff.*`) where there is one.
- **Names:** every one of the 884 names of R3i6nos Mono is still there, at the same code point. 1,335 more (the `.wide` and `.tall` counterparts, `portrait.*`, the game glyphs `tile.*`, `actor.*`, `terrain.*`, `effect.*`, `item.*`, `status.*`, `wall.*`).
- **Fewer files, no CC BY-SA:** `theme/vga8x16.lua`, `theme/r3i6nos_vga8x16.h`, `theme/font/r3i6nos-mono/`, `theme/font/LICENSE.TXT` and the generator (`scripts/build_r3_font.py`, `scripts/r3font/`) are gone; `scripts/build_vga_font.py` too.
  The ansiapps TrueType, WOFF2, PSF and BMFont of the **10 x 20** size are not made yet (the repository makes TrueType, WOFF, WOFF2, BDF, PCF, PSF and a Rust table of the 8 x 16 size only), so the engines that load
  a TTF (Solarus, Godot, Ren'Py, SDL2_ttf) have no 10 x 20 file, and `tests/runtimes/e2e.sh` tests the BDF in Pyxel only.

## The IO Checker's ANSIapps Mono page (`apps/io-checker-native/mono_page.h`, `font_check.h`)

Twelve panels, turned by Fn (R1+Fn back, L1+Fn the first):

| # | Panel | What it is for |
|---|---|---|
| 1-8 | Text mode, Gamepad (live), Keyboard, Piano, UI widgets, Music, Icons: apps and files, Icons: system | the glyphs drawn the way an app should use them (as before; the text panel now says what the font does not have yet) |
| 9 | **Wide and tall** | every base glyph that has a counterpart, with its `.wide` and `.tall` beside it, in pages (left and right turn them); says whether all 141 are the size they should be |
| 10 | **Named glyphs** | all 2,219 named glyphs in code point order, in three size sets (A: one cell, two across, bigger), groups by first word (Y: all, `actor`, `code`, `effect`, `icon`, `item`, `key`, `mus`, `pad`, `piano`, `portrait`, `staff`, `status`, `sys`, `terrain`, `tile`, `ui`, `wall`), X a page back, the D-pad moves a cursor whose name, code point and size are shown |
| 11 | **Code points** | all 4,583 code points of the font, 48 to a row with the first code point of the row, six pages, a cursor (X and Y turn pages) |
| 12 | **Portrait maker** | a portrait built from the parts (A a new one, X the one before, Y the other layout) drawn 8 x 4 cells, and the checks below |

**The font checks** (`font_check.h`; the panel, the Device report's `== font ==` section (milestone 8 of 11) and `--selftest` all run them):

- every named glyph has a bitmap in each cell (`names`, `cells`), and every code point in the table has one;
- every `.wide` has a base and is twice its cells across, every `.tall` twice across and twice down (113 and 28);
- **portrait parts:** all 692 are in the font; **259 right parts are their left parts reversed** (mirrored families); **all 1,283 joins the font lists still hold on the 10 x 20 bitmaps** (651 the same pixel for pixel, 442 sloped
  joins within the step times 1.25 rounded up, 190 hair over the face by the overhang rule); **no pair of neighbouring slot kinds lost a join in the scaling** (82 pairs, 16 gained because the sloped step is looser);
  both layouts have a kind in every cell; and **the maker joins every seam**: portraits built from 84 seeds each have a part that fits its left and upper neighbour (colour models that differ are not compared, as in the font's own checks);
- every ink and paper pair a portrait draws passes 4.5:1 (the theme's rule).

The portrait is a drawing check, not the finished maker: the finished maker (hair, skin and lip colours, the join-aware picker with the compatibility lists) lives in the font's repository (the Font Browser's Portrait page) and
in whatever app uses the parts. **Untested on a unit:** the checks read tables, not the screen.

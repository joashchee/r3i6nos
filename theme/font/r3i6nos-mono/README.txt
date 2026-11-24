R3i6nos Mono: attribution and licence
=====================================

What it is:  the 10x20 pixel monospace font of r3i6nos and the ansiapps apps (docs/R3I6NOS-MONO-FONT.md): code page 437, the Unicode
             box, block and Braille sets, symbols, Latin accents, and private-use glyphs for the gamepad, the on-screen keyboard, modern UI,
             music, coding and the apps' two-cell icons.
Files:       R3i6nosMono-Regular.ttf / .woff / .woff2   TrueType and web fonts (LOVE, Godot, Ren'Py, browsers)
             R3i6nosMono-Regular.fnt + .png              BMFont (LOVE, Godot, Defold)
             R3i6nosMono-Regular.bdf                     bitmap font (Pyxel, X11, many tools)
             r3i6nos_mono10x20.h / .lua, r3i6nos-mono.json   the bitmaps as C, Lua and JSON tables (SDL2, LOVE, web canvases, the screen mirror)
             r3i6nos_mono_names.h / .lua, glyph-names.json   every named glyph and its code points (never moved: append only)
             R3i6nosMono-10x20.psf                       the Linux console (setfont)
             r3i6nos-mono.css, specimen.html, overview.png, glyph-coverage-report.txt

Material:    The 256 code page 437 letters and symbols are "WebPlus IBM VGA 8x16" from The Ultimate Oldschool PC Font Pack v2.2,
             https://int10h.org/oldschool-pc-fonts/ , (c) 2016-2020 VileR, scaled to 10x20 exactly as theme/vga8x16.lua is (theme/font/README.txt).
             Every other glyph (box drawing, blocks, Braille, symbols, accents composed from those letters, the private-use glyphs and icons)
             was drawn or generated for r3i6nos in 2026 by scripts/build_r3_font.py and scripts/r3font/.
Licence:     Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0), https://creativecommons.org/licenses/by-sa/4.0/
             Full text: ../LICENSE.TXT. The font and every file in this folder are an adaptation of VileR's font and stay under CC BY-SA 4.0
             (not MIT, unlike the scripts that make them). Warranty: none; the font is provided as is (LICENSE.TXT, section 5).

Changes made by r3i6nos (2026):
  1. The 8x16 bitmaps are scaled to 10x20 by nearest neighbour (column floor(x*8/10), row floor(y*16/20)), the same as the theme: every CP437
     glyph is pixel for pixel what the theme draws (tests/test_r3font.py).
  2. About 2,500 glyphs are added around them and share the cell grid, so lines, blocks and icons join across cells.
  3. The bitmaps are converted to outlines (one square of 100 font units per pixel) in a TrueType font with a 2000 unit em, so 20 px draws every
     pixel exactly; use 20, 40, 60 px.

Whoever receives the font, in source or in a built image, may use, share and adapt it under the same licence, and must keep this notice and
../LICENSE.TXT with it. No further restriction is placed on it.

# R3i6nos Mono: the 10x20 font of r3i6nos and the ansiapps apps

Made 2026-11-24 at the maintainer's request, from the guide `docs/making-cp437-font.md` (Perplexity). **CC BY-SA 4.0** as a whole (its CP437 letters are VileR's font:
`theme/font/r3i6nos-mono/README.txt`, `NOTICE`); the scripts that make it are MIT. Everything is generated from one source, `scripts/build_r3_font.py` and
`scripts/r3font/`, and checked by tests. **Not yet seen on a real R36S** (rule 1): the checks below ran on a Mac and in Debian.

## What it is

- **One cell, 10 x 20 px**, advance 10, ascent 15, descent 5 (capitals rows 3-14, x-height rows 7-14, descenders to row 18). 64 x 24 cells fill 640 x 480.
- **2,834 code points, 868 named glyphs.** `theme/font/r3i6nos-mono/glyph-coverage-report.txt` is the live count; `overview.png` and `specimen.html` show all of it.
- **CP437 is the theme's own, pixel for pixel**: the 256 glyphs are `theme/vga8x16.lua` scaled 8x16 to 10x20 by the theme's rule, so text-mode art does not change
  (`tests/test_r3font.py`). `U+F000 + byte` addresses a CP437 character by its byte, as the Linux console and the screen mirror do.
- **Generated, not drawn, where geometry matters:** all of U+2500-257F (the light/double lines are CP437's own; heavy, mixed, dashed, rounded arcs and diagonals are
  generated from the theme's measured strokes and a test checks the generator reproduces the VGA's light lines exactly), the block elements, quadrants, sextants
  (U+1FB00-3B), the eighths (U+1FB70-8B) and all 256 Braille patterns (2x2 px dots on a 5 px pitch, so the gap is 3 px inside a cell and across cells).
- **Symbols CP437 lacks**: geometric shapes, arrows, check and ballot marks, math and logic, typographic punctuation, super and subscripts, circled digits,
  control pictures, the whole of Latin-1 and Latin Extended-A except 12 letters (composed from the CP437 letters and accent marks), Greek and Cyrillic letters that are Latin shapes.
- **Music**: the Unicode musical symbols Neumetik's formats print (notes, rests, clefs, accidentals, barlines, repeats, fermata, segno, coda), transport symbols, and
  private-use staff cells (below).
- **Coding**: Powerline (U+E0A0-E0A3, E0B0-E0BF), VCS, debugger, diagnostics, fold marks, file tree.

## Private use: Plane 16, named, never moved

Names, not numbers, are the interface. `theme/font/r3i6nos-mono/glyph-names.json` pins every name to its code point (append only; a retired name stays reserved).
Blocks (`scripts/r3font/registry.py`): `10F000` controller buttons, `10F100` controller pairs, `10F300` on-screen keyboard, `10F600` UI, `10F700` UI pairs,
`10F800` music, `10FA00` music pairs, `10FB00` coding, `10FC00` system status, `10FD00` two-cell icons.

| Group | Names (a few) |
|---|---|
| Gamepad (any pad) | `pad.btn.<A B C X Y Z 1-8 tri cir cro squ home menu view plus minus share power blank>.<off/on/dis>`; `pad.shoulder.<l1 r1 l2 r2 lb rb lt rt zl zr l3 r3 l r>.<off/on/dis>`; `pad.pill.<sel sta fn menu mode home pwr vol ok esc>.<off/on/dis>`; `pad.trig.<l/r>.<0-8>` (analog fill); `pad.dpad.<neutral up down left right upleft ...>`; `pad.stick.<x>.<y>` (x, y in -2..2) and `pad.stick.press` |
| Keyboard | `key.u<hex code>.<up/dn>` for every printable ASCII key, `key.<esc tab ctrl alt fn del ins home end pgup pgdn caps abc sym ok cancel shift enter bksp space up down left right f1-f12 show hide>.<up/dn>`: a raised and a pressed cap |
| Piano | `piano.<N R L LR>.<white><left black><right black>` (digits 0/1: pressed), `piano.name.<C-B>`, `piano.bk.on/off` (a two-cell pair over the cells a black key straddles) |
| Music | `staff.blank`, `staff.clef.<treble bass alto perc>`, `staff.time.<2_4 3_4 4_4 6_8 ... common cut>`, `staff.note.<q/h/w>.<0-12>` (13 pitches, C4-A5), `staff.rest.*`, `staff.bar*`, `staff.repeat.*`, `mus.<accent marcato staccato tenuto fermata trill ...>`, `mus.slur.<up/down>.<l/m/r>`, `mus.wave.<sine tri saw square pulse12 pulse25 pulse75 noise ramp>`, `mus.curve.<adsr lp hp bp notch>`, `mus.knob.<s/l>.<0-12>` |
| UI | `ui.pill.<line/solid>.<l/m/r>`, `ui.bar.<l/r>.<empty/full>`, `ui.bar.m.<0-10>`, `ui.slider.*`, `ui.vslider.*`, `ui.spinner.<0-7>`, `ui.ring.<0-8>`, `ui.scroll.*`, `ui.switch.<off/on/dis>`, `ui.frame.thin.*`, `ui.chevron.*`, `ui.check.mixed`, `ui.radio.*` |
| Status and coding | `sys.<headphones usb dc card pad bat.0-4 bat.charge wifi.0-3 vol.0-3 mute ...>`, `code.<bp.on bp.off step.over error warn info ok commit merge branch ...>` |
| Icons (two cells) | `icon.app.<r3i6nos klamansi nocogama composer painter animator crunchy chordica tinyntt hydrolik musicue neumetik commander iochecker portmaster pocketide fm1ntt firmware menu>`, `icon.sys.<settings network ssh battery sound datacard clock report about update ...>`, `icon.file.<folder text code image audio video archive disk doc font exec music tracker midi data link>` |

`icon.app.r3i6nos` is a handheld, **not the logo**: the butterfly is the maintainer's hand-drawn art and is never redrawn (`docs/BRAND.md`). The pictograms are first drawings
for the maintainer to redraw; their names will not change.

## How a glyph is used (the rules the IO Checker's R3i6nos Mono page shows)

1. **By name**, never by code point: `R3I_PAD_BTN_A_ON` (known when you write it) or `r3mono_name("pad.btn.A.on")` (built at run time); Lua `names["pad.btn.A.on"]`.
2. **A two-cell icon is one string of two cells**, printed together on one row (`r3_puts_mono`): never split, never between other glyphs, never in two colours or on two backgrounds.
   This is every icon, trigger, D-pad, stick, key cap, switch, waveform and big knob, and the piano's black-key indicators.
3. **State is a glyph, not a colour**: off, on, disabled (dotted), or a level (triggers 0-8, a bar in pixel steps, knobs in 13 positions). Colour may add to it.
4. **A letter on a key is not in the glyph**: a 20 x 20 key cap holds its own character; a label next to a glyph is an ordinary cell in the same colours.
5. **Blocks and lines join**: every box, block and Braille glyph fills its cell exactly, so a row of them is continuous (tests check the joins).
6. **Sizes: 20, 40, 60 px** (whole multiples), so every pixel stays square. Anti-aliasing is off in the CSS (`r3i6nos-mono.css`).

## Using it, by runtime

| Runtime | Use | Tried |
|---|---|---|
| LÖVE 11.5 | `love.graphics.newFont("R3i6nosMono-Regular.ttf", 20, "none")`, or the BMFont `.fnt` | LÖVE 11.5's rasteriser draws all 2,831 code points exactly at hinting normal, light, mono and none (`tests/test_r3font_love.sh`); **not drawn on a screen** (no display here) |
| LÖVE 12.0, Godot, Defold | the same TTF or BMFont | not tried |
| Web apps, the screen mirror | `r3i6nos-mono.css` + `.woff2`; or draw `r3i6nos-mono.json` bitmaps on a canvas | WOFF2 checked through FreeType; no browser tried |
| Native C / SDL2 | `#define R3_WITH_MONO` before `r3i6nos_ansiapps.h`; `r3_put_mono`, `r3_puts_mono`; or `r3mono_find(cp)` in `r3i6nos_mono10x20.h` | the IO Checker (below) and `tests/test_r3font.py` (compiled lookup) |
| Pyxel, X11 tools | `R3i6nosMono-Regular.bdf` | not tried |
| Ren'Py, Solarus | the TTF | not tried |
| Linux console | a PSF export is not made yet | roadmap |

The TTF has a trivial `prep` program on purpose: FreeType auto-hints any TrueType font with no instructions, and the auto-hinter moves pixel-aligned outlines (found and fixed
2026-11-24). Its `hmtx` left side bearings equal each glyph's `xMin` (FreeType trims the blank columns otherwise: also found and fixed).

## The IO Checker page

**R3i6nos Mono** (Home, between System and Device report; `apps/io-checker-native/mono_page.h`, `docs/IO-CHECKER.md`): eight panels turned by Fn (R1+Fn back): 1 text mode (the 256
characters, lines, blocks, Braille, accents, symbols, Powerline), 2 the gamepad **live** (every control in the state the pad is in, then every state), 3 the on-screen keyboard
(D-pad moves, A types, X deletes, Y changes layer), 4 the piano (left/right a semitone, A on/off, Y a major chord, X all off; ring = note off, hole in the filled key = on, black
keys indicated by pairs above), 5 UI widgets (animated bar, slider, spinner, ring, switches, knobs), 6 music (staffs, marks, waveforms), 7-8 icons (with a "right and wrong" example of
splitting an icon). The selftest checks every glyph a panel asks for exists, the icons are adjacent pairs, the piano, keyboard and panels respond. **Untested on a unit**; screenshots
made with `R3I6NOS_IO_SHOTS=dir io-checker-native --selftest`.

## Building, and what the checks cover

`scripts/build-r3-font.sh`: tables (stdlib Python, on the Mac), then TTF, WOFF, WOFF2 with fontTools in Debian 12 under Docker (the image `r3i6nos-fontbuild`), then
`verify_ttf.py` renders every code point of the TTF and the WOFF2 at 20 px through FreeType and compares it with the source bitmap, its position and its metrics.
`scripts/build_r3_font.py --check` (in `scripts/check.sh`) fails when a committed table is stale. `tests/test_r3font.py` (CP437 identity, the generator against the VGA, joins,
Braille gaps, name locking, coverage of the Perplexity guide's symbols, the C header compiled and queried). To add a glyph: draw it in the matching `scripts/r3font/` module with a
new name, run `scripts/build-r3-font.sh`, commit `glyph-names.json` with it.

## Not done (ROADMAP section 43)

The `Þ ð þ ĳ ĸ Ŀ ŉ Ŋ ŋ Œ œ ſ` letters; Greek letters that are not Latin shapes; Latin Extended-B and Vietnamese; the Lua theme (`theme/r3i6nos_ansiapps.lua`) drawing these glyphs; the
screen mirror sending them (a font cell sends 254 for now); a PSF export; hand-redrawn music art (the treble clef is rough) and icons; true-colour themes for glyph groups; an italic or bold.

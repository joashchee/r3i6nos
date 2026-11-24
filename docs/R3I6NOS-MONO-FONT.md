# R3i6nos Mono: the font of r3i6nos and the ansiapps apps

![Every glyph of R3i6nos Mono](../theme/font/r3i6nos-mono/overview.png)

**R3i6nos Mono is the one font of every r3i6nos app and of the ansiapps family**: a 10 x 20 pixel monospace bitmap font, **2,887 code points** and **868 named glyphs**
(gamepad buttons and states, on-screen keyboard caps, a piano, music notation, UI widgets, system status, coding symbols and two-cell app and file icons), with the whole of
code page 437 pixel for pixel as the text-mode theme has always drawn it. It is licensed **CC BY-SA 4.0** (the CP437 letters are VileR's; see [Licence](#licence)).
**Status:** built and checked on a Mac, in Debian (arm64 Docker), in LÖVE 11.5, Pyxel, SDL2_ttf and Chrome; the maintainer has looked at it in the IO Checker on a Mac;
**not yet looked at on a real R36S** (the theme, the menu, the mirror and the apps that use it are listed under [In the apps](#in-the-apps-added-2026-11-24-all-untested-on-a-unit)).

## The policy: it is the default, for all future work

1. **Every app made for r3i6nos, and every ansiapps text-mode app, draws with R3i6nos Mono.** A native C app gets it from `theme/r3i6nos_ansiapps.h`, which defines
   `R3_WITH_MONO` itself (an app that must not carry the tables defines `R3_NO_MONO` first and says why in its README). A LÖVE app gets it from `theme/r3i6nos_ansiapps.lua`:
   `T.init()` turns `T.useMono(true)` on. The menu is the reference app.
2. **Text-mode art does not change.** The 256 CP437 glyphs are the theme's own, pixel for pixel; an app that only prints CP437 text looks exactly as before.
3. **Print a glyph by name, never by code point.** Names are the interface and never move (`glyph-names.json` is append only). `r3_puts_mono(s, col, row, r3mono_name("pad.btn.A.on"), fg, bg)`
   in C; `scr:glyph(col, row, "pad.btn.A.on", fg, bg)` in Lua.
4. **A two-cell icon is one string**, printed together, never split or in two colours.
5. **State is a glyph, not only a colour** (off, on, disabled; or a level).
6. **A new glyph is added to the font, not drawn inside one app**: draw it in `scripts/r3font/`, give it a new name, run `scripts/build-r3-font.sh`, commit `glyph-names.json` with it
   (see [Building and checking](#building-and-what-the-checks-cover)). An app never ships its own copy of the font or of a table of it: it uses the theme's.
7. **Do not redraw the maintainer's logo** (the butterfly "3i6", `docs/BRAND.md`): the `icon.app.r3i6nos` glyph is a handheld, not the logo.
8. **Say what was not run on a unit** (CLAUDE.md rule 1).

## Quick start

**C / SDL2 (native app).** Include the theme; nothing else to turn on.

```c
#include "r3i6nos_ansiapps.h"          // R3_WITH_MONO is already defined
...
r3_puts_mono(&s, 2, 5, r3mono_name("pad.btn.A.on"), R3_YELLOW, R3_BLACK);       // one cell, the A button pressed
r3_puts_mono(&s, 4, 5, R3I_ICON_APP_HYDROLIK, R3_WHITE, R3_BLACK);              // a two-cell icon: one string, two cells
r3_put_mono(&s, 8, 5, 0x2554, R3_LIGHT_GRAY, R3_BLACK);                        // any code point of the font (here ╔)
```

**LÖVE (Lua).** `T.init()` has already turned the font on.

```lua
local T = require("r3i6nos_ansiapps")
scr:glyph(2, 5, "pad.btn.A.on", T.YELLOW, T.BLACK)     -- a named glyph; two cells for an icon
scr:text(2, 6, "Þórr · Λόγος · ⠿", T.WHITE)             -- letters CP437 lacks are drawn when the font has them, else "?"
local cells = T.glyph("icon.app.hydrolik")             -- the cells of a name, to place yourself
```

**Web or any engine.** Use `R3i6nosMono-Regular.woff2` / `.ttf` at 20, 40 or 60 px with `r3i6nos-mono.css` (anti-aliasing off), or the BDF (Pyxel, X11), the BMFont `.fnt` + `.png`
(LÖVE, Godot, Defold) or the PSF (Linux console). The files are in `theme/font/r3i6nos-mono/`; the table below says which to pick.

## Files (all in `theme/font/r3i6nos-mono/`)

| File | For |
|---|---|
| `R3i6nosMono-Regular.ttf`, `.woff`, `.woff2`, `r3i6nos-mono.css` | LÖVE, Godot, Ren'Py, Solarus, browsers (use 20 / 40 / 60 px) |
| `R3i6nosMono-Regular.fnt` + `.png` | BMFont: LÖVE, Godot, Defold |
| `R3i6nosMono-Regular.bdf` | bitmap font: Pyxel, X11 tools |
| `R3i6nosMono-10x20.psf` | the Linux console (`setfont`): CP437 by byte, then lines, blocks, Latin accents, Powerline |
| `r3i6nos_mono10x20.h`, `r3i6nos_mono10x20.lua`, `r3i6nos-mono.json` | the bitmaps as C, Lua and JSON tables (SDL2, LÖVE, web canvases, the screen mirror) |
| `r3i6nos_mono_names.h`, `r3i6nos_mono_names.lua`, `glyph-names.json` | every named glyph and its code points (append only) |
| `overview.png`, `specimen.html`, `glyph-coverage-report.txt` | to look at it, and the live count |
| `edits.txt` (when there is one) | hand touch-ups made with the Font Editor, applied last by the build (below) |
| `README.txt` | attribution, licence and the changes made |

## Licence

**CC BY-SA 4.0** for the font and every file in the font's folder. Its 256 code page 437 letters are *WebPlus IBM VGA 8x16* by VileR (*The Ultimate Oldschool PC Font Pack*,
<https://int10h.org/oldschool-pc-fonts/>), scaled to 10 x 20; every other glyph was drawn or generated for r3i6nos. Anything that ships the font ships `theme/font/` (the licence
text, `README.txt`) and `NOTICE`; a built page or file that embeds it (the screen mirror's page does) carries the credit. The scripts that make the font are MIT.

## What it is (reference)

Made 2026-11-24 at the maintainer's request, from the guide `docs/making-cp437-font.md`. Everything is generated from one source, `scripts/build_r3_font.py` and `scripts/r3font/`, and checked by tests.

- **One cell, 10 x 20 px**, advance 10, ascent 15, descent 5 (capitals rows 3-14, x-height rows 7-14, descenders to row 18). 64 x 24 cells fill 640 x 480.
- **2,887 code points, 868 named glyphs.** `theme/font/r3i6nos-mono/glyph-coverage-report.txt` is the live count; `overview.png` and `specimen.html` show all of it.
- **CP437 is the theme's own, pixel for pixel**: the 256 glyphs are `theme/vga8x16.lua` scaled 8x16 to 10x20 by the theme's rule, so text-mode art does not change
  (`tests/test_r3font.py`). `U+F000 + byte` addresses a CP437 character by its byte, as the Linux console and the screen mirror do.
- **Generated, not drawn, where geometry matters:** all of U+2500-257F (the light/double lines are CP437's own; heavy, mixed, dashed, rounded arcs and diagonals are
  generated from the theme's measured strokes and a test checks the generator reproduces the VGA's light lines exactly), the block elements, quadrants, sextants
  (U+1FB00-3B), the eighths (U+1FB70-8B) and all 256 Braille patterns (2x2 px dots on a 5 px pitch, so the gap is 3 px inside a cell and across cells).
- **Symbols CP437 lacks**: geometric shapes, arrows, check and ballot marks, math and logic, typographic punctuation, super and subscripts, circled digits,
  control pictures, the whole of Latin-1 and Latin Extended-A (the accented ones composed from the CP437 letters and accent marks; `Þ þ ð ĳ Ĳ ĸ Ŀ ŀ ŉ Ŋ ŋ Œ œ ſ` drawn in `scripts/r3font/greek.py`), the whole Greek alphabet (the Latin shapes aliased; `Λ Ξ Π Ψ β γ ζ η θ ι κ λ ξ ς χ ψ` drawn, the tonos and dialytika forms composed), and the Cyrillic letters that are Latin shapes. **The drawn letters are first drafts** in the CP437 style, for the maintainer to redraw.
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
| LÖVE 11.5, the theme (`theme/r3i6nos_ansiapps.lua`) | `T.useMono(true)`, then `T.decode` draws what CP437 lacks and `scr:glyph(col, row, "pad.btn.A.on")` a named glyph (below); or `love.graphics.newFont("R3i6nosMono-Regular.ttf", 20, "none")`, or the BMFont `.fnt` | LÖVE 11.5's rasteriser draws all 2,887 code points exactly at hinting normal, light, mono and none (`tests/test_r3font_love.sh`); the theme's own drawing (a staging atlas, `monoSlot`) is **not drawn on a screen** (no display here); its cells, names and bitmaps are tested (`tests/test_theme.lua`) |
| LÖVE 12.0, Godot, Defold | the same TTF or BMFont | not tried |
| Web apps | `r3i6nos-mono.css` + `.woff2`; or draw `r3i6nos-mono.json` bitmaps on a canvas | WOFF2 checked through FreeType; **Chrome (2026-11-24): the specimen page loads the WOFF2 and five glyphs drawn on a canvas at 20 px match the font's bitmap, 1,000 of 1,000 pixels** |
| The screen mirror's page | the glyph table embedded in `page.html` (`scripts/build_mirror_page.py`), drawn on a 640 x 480 canvas; the unit sends the code point of every glyph CP437 lacks (`docs/SCREEN-MIRROR.md`) | the page's own script runs in node against a stub canvas and every pixel is compared with the font (`tests/test_mirror_page.js`); **no browser tried** |
| Native C / SDL2 | `#define R3_WITH_MONO` before `r3i6nos_ansiapps.h`; `r3_put_mono`, `r3_puts_mono`; or `r3mono_find(cp)` in `r3i6nos_mono10x20.h` | the IO Checker (below) and `tests/test_r3font.py` (compiled lookup) |
| Pyxel, X11 tools | `R3i6nosMono-Regular.bdf` | **Pyxel 2.2.8 (Debian 12 arm64, Docker): 1,000 of 1,000 pixels of five glyphs match; `text_width` is 10 a letter** |
| Solarus and the SDL engines | the TTF through SDL2_ttf (hinting mono) | **SDL2_ttf: 1,200 of 1,200 pixels of six glyphs match, advance 10** (Solarus itself not run: LuaJIT panics on this host) |
| Ren'Py, Godot | the TTF | not tried |
| Linux console | `R3i6nosMono-10x20.psf`: PSF2, 10 x 20, 512 glyphs (CP437 by byte, then the lines, blocks, Latin accents and Powerline CP437 lacks), with a Unicode table (`setfont R3i6nosMono-10x20.psf`; the table also answers `U+F000 + byte`) | the file is parsed back and every glyph compared with the font (`tests/test_r3font.py`); **no console tried** |

The TTF has a trivial `prep` program on purpose: FreeType auto-hints any TrueType font with no instructions, and the auto-hinter moves pixel-aligned outlines (found and fixed
2026-11-24). Its `hmtx` left side bearings equal each glyph's `xMin` (FreeType trims the blank columns otherwise: also found and fixed).

## The IO Checker page

**R3i6nos Mono** (Home, between System and Device report; `apps/io-checker-native/mono_page.h`, `docs/IO-CHECKER.md`): eight panels turned by Fn (R1+Fn back): 1 text mode (the 256
characters, lines, blocks, Braille, accents, symbols, Powerline), 2 the gamepad **live** (every control in the state the pad is in, then every state), 3 the on-screen keyboard
(D-pad moves, A types, X deletes, Y changes layer), 4 the piano (left/right a semitone, A on/off, Y a major chord, X all off; ring = note off, hole in the filled key = on, black
keys indicated by pairs above), 5 UI widgets (animated bar, slider, spinner, ring, switches, knobs), 6 music (staffs, marks, waveforms), 7-8 icons (with a "right and wrong" example of
splitting an icon). The selftest checks every glyph a panel asks for exists, the icons are adjacent pairs, the piano, keyboard and panels respond. **Untested on a unit**; screenshots
made with `R3I6NOS_IO_SHOTS=dir io-checker-native --selftest`.

## Touching it up by hand: the Font Editor

A glyph can be redrawn on the R36S itself with **the Font Editor** (`apps/fontedit-native/`, `docs/FONT-EDITOR.md`), which opens the two-cell names as one picture across their seam. Its changes are not made in the generated
files: they are kept in `edits.txt` here (a text file, one block a changed cell), which `scripts/build_r3_font.py` applies after every generator, so they reach the TTF, BDF, PSF and the C and Lua tables and survive the next
build. `scripts/merge_font_edits.py FILE` folds a device's file in. Names and code points never change by an edit.

## Building, and what the checks cover

`scripts/build-r3-font.sh`: tables (stdlib Python, on the Mac), then TTF, WOFF, WOFF2 with fontTools in Debian 12 under Docker (the image `r3i6nos-fontbuild`), then
`verify_ttf.py` renders every code point of the TTF and the WOFF2 at 20 px through FreeType and compares it with the source bitmap, its position and its metrics.
`scripts/build_r3_font.py --check` (in `scripts/check.sh`) fails when a committed table is stale. `tests/test_r3font.py` (CP437 identity, the generator against the VGA, joins,
Braille gaps, name locking, coverage of the Perplexity guide's symbols, the C header compiled and queried). To add a glyph: draw it in the matching `scripts/r3font/` module with a
new name, run `scripts/build-r3-font.sh`, commit `glyph-names.json` with it.

## In the apps (added 2026-11-24, all untested on a unit)

- **The Lua theme**: a cell holds a CP437 index (0-255) or an extended cell, `T.EXT` (0x110000) plus a code point. Nothing changes until an app calls `T.useMono(true)` (it only checks
  the font folder is there; the 205 KB table is read when the first glyph is wanted): then `T.decode` turns a character CP437 lacks into an extended cell when the font has it
  (otherwise `?`, as before; the Greek beta is its own letter, no longer the sharp s). `T.glyph("icon.app.hydrolik")` is the cells of a name (two for an icon: print them together,
  `Screen:glyph(col, row, name, fg, bg)` does), `T.monoBitmap(cell)` the 10 x 20 bitmap. Drawing uses a second atlas filled on first use (1,024 slots, a screen shows few), so an app
  that never prints one pays nothing. The mirror sends 254 for an extended cell and an `E` datagram after it.
- **The menu**: the top bar's battery is a glyph in quarters (`sys.bat.0-4`, a bolt `sys.bat.charge` while it charges) and the percent, the Wi-Fi `sys.wifi.0-3` (wired keeps CP437's `ë`).
  With the font folder missing (an older image) the text stand-ins (`BAT ███░░ 62%`) are drawn as before.
- **The hot-plug toast** (`theme/r3i6nos_toast.{lua,h}`, `docs/HOTPLUG.md`): a status glyph before the words, chosen from them (`M.icon`, `r3_toast_icon`: headphones, battery, power, USB, volume in
  steps, pad, TF card, Wi-Fi); a Lua app gets it when the font folder is there (`toast.icons = false` turns it off), a native app when it defines `R3_WITH_MONO`.
- **The Keymap** (`theme/r3i6nos_keymap.h`, `R3_WITH_MONO`): the system page's chords (`L1+R1`, `L2+R2`, `L1+Fn`, `R1+Fn`) and any row of button names are the pad's glyphs, and the test page
  shows the held buttons as pressed glyphs. **The on-screen keyboard** (`theme/r3i6nos_osk.h`): every key is a two-cell cap, the cursor's pressed.
- **Commander** (`apps/commander-native`): each row has a two-cell icon of its kind (`file_icon`: folder, open folder for `..`, link, text, doc, code, image, music, MIDI, tracker module, video, archive,
  disk image, program, font, data), the name 3 cells shorter; Commands > File icons turns it off (saved in `settings.conf`).
- **MusiCue's Piano** keeps its labelled keys (each shows its note and the button that plays it, which the glyph piano cannot carry). **Hydrolik's transport** and **a coding app's Powerline
  segments** are the apps' own roadmaps.

## Tried in the engines (2026-11-24)

`tests/runtimes/e2e.sh` (Debian 12 arm64 in Docker, `tests/test_runtimes_docker.sh`): the BDF in **Pyxel 2.2.8** and the TTF in **SDL2_ttf** (what Solarus and the SDL engines load, hinting mono): five or six glyphs
(the letter A, a box corner, a pad button, `Þ`, `θ`, a sextant) drawn and every pixel compared with the font's bitmap: all matched. A Chrome check of the WOFF2 is in the table above. **Ren'Py and Godot have not been tried.**

## Not done (ROADMAP section 43)

Latin Extended-B and Vietnamese; Cyrillic letters that are not Latin shapes; hand-redrawn music art (the treble clef is rough), the Greek and Latin letters drawn here, and the icons; true-colour themes for glyph groups; an
italic or bold; the look of the IO Checker's R3i6nos Mono page on a real screen.

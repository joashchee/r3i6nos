# The r3i6nos-ansiapps theme

**Rule (CLAUDE.md, rule 8): every app made for r3i6nos starts from this theme**: the menu, the default apps, and any new LÖVE or
native app. It is the text-mode look of the ansiapps family fitted to the R36S's 640x480 panel, and the screen of KlamANSI
(a text-mode ANSI art app by the same maintainer) is its reference for how the windows work. Code: `theme/`. The menu (`menu/main.lua`,
`menu/view.lua`) is the reference app.

## The screen

The font is **ANSIapps Mono** (10x20; **the only font of every app since 2026-11-25**, guide and policy in `docs/ANSIAPPS-MONO-FONT.md`), whose code page 437 characters are the font's own 10x20 drawings, so **64 columns by 24 rows** fill 640x480 exactly. Every character is a cell of the grid; the
whole screen is drawn only with characters in cells (code page 437), in the 16 VGA colours. Nothing scrolls: a list that is too long
turns whole pages (`Page 2/3` in its top edge).

| Rows | Window | What it holds |
|---|---|---|
| 0 | **Top bar** (reserved) | White on blue: the app's name at the left, a count or state at the right. Nothing else ever uses this row. |
| 1-20 | **Display view** (64x20) | The app's screen: lists, dialogs, its own picture. |
| 21 | Command line, row 1: **reply** | The answer to the last action or a tip. Light cyan; `ok` light green, `warn` yellow, `error` light red. Too long: cut with `»`, or, in an app that runs the marquee clock, scrolled (see "Scrolling text"). |
| 22 | Command line, row 2: **prompt** | The mode character (`>` in light green), a space, and the line (white, with a block caret). On a pad with no keyboard it shows what A would run (`run Some Game`). |
| 23 | Command line, row 3: **status** | Left: where you are (`3 of 17`). Right: a state (`native`). |

The three bottom rows work as KlamANSI's command line does, and a quiet window leaves them blank. A widescreen dashboard is **not**
part of this theme: the screen is fixed at 640x480 (on a bigger window the whole screen is scaled by a whole number and centred).

## The font: ANSIapps Mono at 10x20

The font is **ANSIapps Mono**, the official font of the ansiapps family, drawn clean-room in its own repository (`~/Coding/ansiapps-mono-font`) and licensed **SIL Open Font License 1.1**
(Reserved Font Name "ANSIapps Mono"). `scripts/build_ansiapps_font.py` imports its 10x20 size into `theme/font/ansiapps-mono/` (`scripts/check.sh` fails if the committed tables are stale, when
the font's repository is beside this one). **What the OFL requires, and where it is done:** keep the copyright and licence with every copy (`theme/font/ansiapps-mono/OFL.txt`, `README.txt`, `NOTICE`,
and the header of each generated file); do not sell the font by itself; do not use the Reserved Font Name for a modified version. **Anything that ships the theme ships `theme/font/`** (`configure_rootfs.sh`
does, and a test checks), and a release that bundles the repo ships `NOTICE`.

The 256 code page 437 characters are the font's glyphs at their Unicode code points (`ansiapps_cp437.h|.lua`: byte, code point, bitmap), so a cell holding a CP437 index and a cell holding
any other code point of the font are drawn from the same table. The 10x20 bitmaps are made from the 8x16 ones in the font's repository: text by inserting lines (one-pixel strokes stay one pixel),
tiling glyphs (box drawing, blocks, Braille, sextants, Powerline) by repeating columns 0 and 7 and rows 0, 4, 11 and 15 (a half block is exactly 5 columns or 10 rows), so box lines and blocks join across cells
exactly as in 8x16 (`tests/test_theme.lua`, `tests/test_ansiapps_font.py`). No smoothing, no bold or italic.

## ANSIapps Mono, the font of every app

**Every app has it: new apps use it and print their icons, status and key glyphs from it, not their own art.** The C theme always includes it (`R3_WITH_MONO` is on, `R3_NO_MONO` is gone: the
CP437 characters come from the same table) and the Lua theme's `T.init` turns it on. The full guide, the policy and the names are `docs/ANSIAPPS-MONO-FONT.md`.

The theme's CP437 glyphs above are also the first 256 of the font's 4,583 (`theme/font/ansiapps-mono/`), which adds box, block, Braille and octant sets, symbols, Cyrillic,
music, gamepad, keyboard, UI, icon, portrait and game glyphs by name. A native app can put any of them in a cell: `r3_put_mono(s, col, row, cp, fg, bg)`
for one code point, `r3_puts_mono(s, col, row, r3mono_name("icon.app.hydrolik"), fg, bg)` for a named glyph (a two-cell icon is one string; a glyph more than one cell tall is printed row by row, `name` then
`name.row2`). The cell keeps `ch` = 254 as a stand-in for the
mirror (a datagram `E` after the frame carries the code points). **The Lua theme draws them:** a cell is a CP437 index (0-255) or `T.EXT + code point`; an app calls
`T.useMono(true)` once (it checks the font folder; the table loads on the first glyph), after which `T.decode` draws what CP437 lacks and `scr:glyph(col, row, "icon.app.hydrolik", fg, bg)` or `T.glyph(name)` (a
list of cells, two for an icon) draws a named glyph; `T.monoBitmap(cell)` is the 10 x 20 bitmap and `T.mono()` the table (nil on an image whose theme folder has no font folder: the app then draws its text
stand-ins). The menu does this for its top bar. Details and tests: `docs/ANSIAPPS-MONO-FONT.md`, `tests/test_theme.lua`.

## Colour

Only the 16 VGA colours (`theme.RGB`). The theme's own pairs are `theme.STYLE`, and **every text/background pair must pass 4.5:1**
(WCAG AA; `theme.contrast`, `tests/test_theme.lua` and `tests/test_menu_view.lua` check). Dark gray, blue and red do not pass on black:
use them only as a frame or fill, never for words. Colour is never the only cue: errors say so in words, the selection is also a bar.
Nothing on screen changes more than twice a second.

### True colour (a native C app's choice, from 2026-11-16; Commander uses it)

The 16 VGA colours stay the default and the reference. A native app (C, `theme/r3i6nos_ansiapps.h`) may instead let the person pick a
**colour theme** of 24-bit colours:

- A cell holds a **palette slot** (0-255), not a colour. `r3_pal` is the palette (ARGB); `r3_pal_set(slot, 0xRRGGBB)` changes one slot and
  `r3_pal_reset()` puts the VGA colours back. Slots 0-15 are the VGA names (`R3_BLACK`...`R3_WHITE`) and keep their **roles** in the styles,
  whatever colour they hold: `R3_BLACK` is the page, `R3_LIGHT_GRAY` the text, `R3_WHITE` strong text, `R3_BLUE` the top bar, `R3_CYAN` the
  selected row, `R3_LIGHT_CYAN` hints, `R3_LIGHT_GREEN` good news (and the toast's ok ground), `R3_YELLOW` caution, `R3_LIGHT_RED` mistakes
  (`R3_RED` is the toast's error ground). So changing the palette themes the whole screen, the toast and the dialogs, and code that draws with
  `R3_ST(...)` needs no change. Slots 16 and up are the app's own (Commander's file kinds, `R3_FK_SLOT0`).
- `theme/r3i6nos_themes.h` has the **bundled themes** (`r3_themes[]`, `r3_theme_apply(i)`, `r3_theme_find(name)`): VGA Classic (the default,
  exactly the 16 DOS colours), Dark, Light, High Contrast, Solarized Dark, Solarized Light, Nord, Gruvbox Dark, Dracula, and the monochrome Mono
  White, Mono Green, Mono Amber and Mono Paper. Each is 16 colours and 11 file-kind colours.
- **The 4.5:1 rule is unchanged and applies to every theme:** every style pair, the toast's two grounds and each file kind on the page.
  `tests/test_themes.c` (a `scripts/check.sh` step) and Commander's self-test check all of them, in every theme, on the drawn screens too.
  A new theme is not added until it passes. Colour is still never the only cue (folders keep their `/`, tagged rows their `*`; the monochrome
  themes tell kinds apart by shade only, which is why the cues stay).
- The screen mirror sends 4 bits a colour, so it shows the **nearest VGA colour** to each slot (`r3_near`).
- LÖVE apps and the menu stay on the 16 VGA colours (the Lua theme has no palette); an app that wants themes says so in its README.

## List dialogs (as in KlamANSI)

The standard list: a double-line box, the title and `Page x/y` in the top edge, one row an item (label at the left, value at the right),
the selected row **black on cyan**, a foot of keys in the bottom edge (`theme.listDialog`). It stays open until accepted or backed out of.
Dialogs (Settings, Shut down?) are the same list in a smaller box centred in the display view. The rule every list follows
(`theme.listKey`; KlamANSI's gesture table with the pad's buttons for the swipes):

**Standardised 2026-11-19 on the input mapping (`docs/r3i6nos-mapping.md`). Built in the C theme on 2026-11-20 as `theme/r3i6nos_input.h`
(`r3_input_button`, `r3_input_tick`, `r3_list_step`; the IO Checker uses it). The old `r3_list_key`, `r3_pad_action` and the Lua `theme.listKey` / `T.PAD`
still have the old keys: Commander, MusiCue and the menu move onto the new reader one by one (ROADMAP section 39 step 2).**

| Action | R36S | Keyboard | Does |
|---|---|---|---|
| up / down | D-pad or right stick up / down | arrows | select, wrapping; a stick steps at the mapping's rate (dead zone 0.35, at most 6 a second) |
| pageup / pagedown | **R1 held** + up / down | PgUp / PgDn | a whole page (`Page n/m`) |
| home / end | **R1 held** + left / right | Home / End | top / bottom of the list |
| accept | A | Enter, Z | Enter |
| back | B | Esc, X | a step back; closes as Cancel |
| more | **Y** | Tab, F1, S | More... on the selection (the menu: its Settings) |

- **L1, R1, L2 and R2 do not page or jump on their own any more** (they are modifiers; L1 + R1 opens the main menu, L2 + R2 the contextual menu), and
  **Start is not More** (the one-touch buttons are the app's). **Left and right alone are the app's** (a column, a tab, a second list), not paging: the
  keyboard's PgUp, PgDn, Home and End do the paging.
- The list's actions are the **defaults**; an app may bind the one-touch buttons otherwise for its function and says so in its README,
  but never the modifiers or the system chords.

A list that fits one page shows no `Page` number. An app with more than one list may use left/right to switch lists (Commander does), and X and Select
for its own commands (`theme.PAD` names them `x`, `y`, `select`).

Apps read the pad's **raw joystick buttons** (`theme.PAD`), not SDL's gamepad mapping, which does not match this pad
(`docs/hardware-findings.md`). `theme.KEYS` is the keyboard for laptops.

## Using it

**LÖVE** (11.5 and 12.0): the theme is installed at `/opt/loveos/theme` (`R3I6NOS_THEME` overrides, for a laptop). In `main.lua`:

```lua
package.path = (os.getenv("R3I6NOS_THEME") or "/opt/loveos/theme") .. "/?.lua;" .. package.path
local theme = require("r3i6nos_ansiapps")
-- love.load: theme.init(); screen = theme.screen()
-- love.draw: screen:clear(); theme.topBar(screen, "My app", "state"); ...draw rows 1-20...; theme.commandLine(screen, {...}); theme.draw(screen)
```

Keep the screen composition in a pure module (the menu's `view.lua`) so luajit can test it, and test it as `tests/test_menu_view.lua` does.
`theme.init` and `theme.draw` are the only LÖVE calls.

**Native (C, SDL2)**: `theme/r3i6nos_ansiapps.h` is the C twin of the Lua theme (single header, compile with `-I theme` and SDL2's flags): the
screen (`R3Screen`), text and boxes, the top bar, the command line, the list dialog, the button and key maps, and `R3Video`, which paints
the cells into a texture and lets SDL scale it by a whole number. **Call `r3_video_draw` on every pass of the main loop**, not only after
input: a change is presented `R3_VIDEO_SETTLE` (4) times over the next calls, because on the R36S a screen presented once stayed black
(Commander and the IO Checker started blank, 2026-11-09); keep the loop short while `settle > 0` (Commander waits 16 ms then, 500 ms
idle). `apps/commander-native/commander.c` is the reference app. **`r3_render_hook`** (2026-11-24): a function pointer (NULL by default) that `r3_render` calls last with the finished 640 x 480 pixels, for an app that paints what the cell grid cannot (the Font Editor's edited glyphs, `docs/FONT-EDITOR.md`); the app must force a repaint when what it draws changes, since the video compares cells only, and a screen mirror, which sends cells, does not see it. The two themes
cannot drift on the font: both hash all 256 character bitmaps and the same number is `R3_GLYPH_HASH` in `theme/font/ansiapps-mono/ansiapps_cp437.h` (`tests/test_theme.lua` compares).
The font data it builds on: `theme/font/ansiapps-mono/ansiapps_cp437.h` has the 256 characters (`ansiapps_cp437_10x20[i][row]`, 10 bits, bit 9 leftmost; `ansiapps8x16[i][row]` for 8x16) and the Unicode of each
(`r3i6nos_cp437[i]`). Use the same 64x24 grid of 10x20 cells, the same row roles and colours as above.
(The C header includes it; the app's `.deb` carries `NOTICE` and the font licence in its doc folder.)

## The main menu (rule 17; **specified 2026-11-19; built in the C theme on 2026-11-20; the Lua theme has the reader and the Keymap since 2026-11-24 and the boot menu is the first LÖVE app on them**)

Every app made for r3i6nos has a main menu, opened by **L1 + R1** (keyboard: the backquote key, and the backslash key for the contextual menu, L2 + R2; settled 2026-11-20), drawn as the standard list dialog above (a box centred in
the display view, `Page x/y` when it needs it). The input map it belongs to is `docs/r3i6nos-mapping.md`.

**The first items are fixed, in this order, and the app's own items follow them:**

| Item | Does |
|---|---|
| **About** | The app's name, version, commit and date (the splash's version line), licence and credits. |
| **Colour theme** | The theme choice: VGA Classic by default; the true-colour themes where the app offers them (above, "True colour"). A LÖVE app with only the 16 VGA colours shows the item with the one theme it has, not nothing. |
| **Keymap** | The app's input map: help, test and remap (`docs/r3i6nos-mapping.md`, "The Keymap"). Compulsory since 2026-11-20, right after Colour theme. **Built in the C theme on 2026-11-20 as `theme/r3i6nos_keymap.h`: the help pages (the system's inputs, then a page for each panel the app describes) and the test page; remapping is not built there yet** (FM1-NTT has the first complete one). Not in the Lua theme. |
| **Device report** | Runs the app's device report (rule 9). |
| **Rec demo** | Runs the app's Rec demo (rule 11). |
| **Benchmark** | Runs the app's benchmark and writes it to the app's data folder. **Hidden when the app has nothing to measure**; the other five never are. |
| **Quit** | Leaves the app. **It is how an r3i6nos app is left: Select + Start no longer does.** |

An app never renames, reorders or drops one of the five, or puts Quit anywhere but last of these six places (its own items come after Quit only if
they are a separate group, which the theme draws below a rule; none is expected yet).

**What the C theme provides** (`theme/r3i6nos_mainmenu.h`, with `theme/r3i6nos_input.h`; tested by `tests/test_input.c`): `R3MainMenu` with the
fixed ids (`R3MM_ABOUT` ... `R3MM_QUIT`) and the app's own items after Quit (`R3MM_OWN0 + i`); `r3_mm_init(&m, has_bench)` hides Benchmark when the app has nothing to
measure; `r3_mm_open`, `r3_mm_key(&m, action)` (returns the chosen id, `R3MM_CLOSED` or `R3MM_NOTHING`) and `r3_mm_draw`; `r3_about_draw` and
`r3_theme_pick_draw` (the picker over `r3_themes`: the app applies the theme as the selection moves, keeps it on A and reverts on B); and
**`r3_mm_selftest`, which an app calls from its own self-test** so "the items are there and in this order" is checked once. The app does what each item means.
**The Lua theme has the reader (`theme/r3i6nos_input.lua`: `input.new`, `input.button`, `input.key`, `input.axis`, `input.tick`, `input.list_step`, the constants) and the Keymap's help and test (`theme/r3i6nos_keymap.lua`), tested by `tests/test_input.lua`; `theme.mainMenu` as one shared structure is still to build, so the boot menu builds its own in `menu/settings.lua` (`root`), with the six items in order. The launcher's Quit powers the unit off (it is the only screen); Benchmark is hidden.**

**The reader** (`theme/r3i6nos_input.h`): `R3Input` and `r3_input_button(in, raw_button, down)` give the action for a raw button with R1 held as "increase" and the chords
(L1+R1 `R3_MENU`, L2+R2 `R3_CONTEXT`, Fn `R3_PANEL_NEXT`, L1+Fn `R3_PANEL_FIRST`, R1+Fn `R3_PANEL_PREV`); `r3_input_axis` and `r3_input_tick` turn the right stick into steps
(dead zone `R3_STICK_DEAD` 0.35, top rate `R3_STICK_RATE` 6 a second, Page up/down with R1, Home/End once a push); `r3_input_pointer` gives the left stick's pointer
movement (`R3_POINTER_PPS` 120, `R3_R1_POINTER_X` 3); `r3_list_step` is the list rule (left and right do not page). The four numbers are constants in that header only.

**The list keys the menu uses are the standardised ones above:** up and down, A to accept, B to back, Y for More, R1 held for Page up/down and Home/End.

## The splash (rule 12)

`r3_splash(screen, app, version, build, tagline, what, percent)` in `r3i6nos_ansiapps.h` draws the start-up screen every R36S port has: the top bar, a
framed box with the app's name, a tagline, what is loading and a bar, and the line `v<version> <commit> <date>`. Draw it at once at start, with each
loading step, and put any load error in `what`. There is no Lua version yet.

## What the theme does not cover

- The IO Checker (`docs/IO-CHECKER.md`) is on the theme but for its video test patterns, which are pixels by necessity and say so.
- Not run on a real R36S, or under LÖVE with a display: `theme.init` and `theme.draw` (the atlas, the canvas, the scaling) are only
  checked by reading them; the data side (font, mapping, layout, list logic, the menu's screen) is tested by luajit.

## The screen mirror's tap

`theme/r3i6nos_mirror.lua` (called by `T.draw`) and `theme/r3i6nos_mirror.h` (called by `r3_video_draw`) hand the cells that changed to the
screen mirror (`docs/SCREEN-MIRROR.md`) when one is on. An app does nothing: the tap runs when a screen has changed and is about to be
remembered as the previous one, and with no mirror it costs one test of a flag file, at most once a second. An app that draws its own screen
without `T.draw` or `r3_video_draw` is not mirrored. Cells of ANSIapps Mono are sent as the stand-in 254 and then as an `E` datagram (code point and attribute), which the page draws. The Lua module is optional (`pcall(require)`), so an image's theme folder that predates it still works.


## Scrolling text (native C, 2026-11-19)

A line too long for its cells scrolls sideways, KlamANSI's method (its `docs/ANSI-TEXT-MODE.md`, "Command line"): a closed loop, one cell a tick, two ticks a second, a hold of two ticks at its start, and a yellow
diamond between spaces where the end meets the start. `r3_marquee_clock(ms)` (called once a frame) starts the clock; `r3_text_scroll(s, col, row, str, max, fg, bg)` draws a line that way, and `r3_marquee_cells` gives the
cells for an app that colours its own text (MusiCue's `gscroll`). The command line's reply and left status use it. **An app that never calls the clock, and any line that fits, looks exactly as before** (cut with `»`).
Each place (column, row) remembers when its text was first shown, so a new text starts at its beginning. Scroll only what the person is on (the selected row), not a whole list. Used by MusiCue; the other native apps can
call the clock to get it on their command lines. Untested on the unit.

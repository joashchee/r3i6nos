# The r3i6nos-ansiapps theme

**Rule (CLAUDE.md, rule 8): every app made for r3i6nos starts from this theme**: the menu, the default apps, and any new LÖVE or
native app. It is the text-mode look of the ansiapps family fitted to the R36S's 640x480 panel, and the screen of KlamANSI
(`~/Coding/klamansi/docs/ANSI-TEXT-MODE.md`) is its reference for how the windows work. Code: `theme/`. The menu (`menu/main.lua`,
`menu/view.lua`) is the reference app.

## The screen

IBM VGA 8x16 scaled up to **10x20**, so **64 columns by 24 rows** fill 640x480 exactly. Every character is a cell of the grid; the
whole screen is drawn only with characters in cells (code page 437), in the 16 VGA colours. Nothing scrolls: a list that is too long
turns whole pages (`Page 2/3` in its top edge).

| Rows | Window | What it holds |
|---|---|---|
| 0 | **Top bar** (reserved) | White on blue: the app's name at the left, a count or state at the right. Nothing else ever uses this row. |
| 1-20 | **Display view** (64x20) | The app's screen: lists, dialogs, its own picture. |
| 21 | Command line, row 1: **reply** | The answer to the last action or a tip. Light cyan; `ok` light green, `warn` yellow, `error` light red. Too long: cut with `»`. |
| 22 | Command line, row 2: **prompt** | The mode character (`>` in light green), a space, and the line (white, with a block caret). On a pad with no keyboard it shows what A would run (`run Some Game`). |
| 23 | Command line, row 3: **status** | Left: where you are (`3 of 17`). Right: a state (`native`). |

The three bottom rows work as KlamANSI's command line does, and a quiet window leaves them blank. A widescreen dashboard is **not**
part of this theme: the screen is fixed at 640x480 (on a bigger window the whole screen is scaled by a whole number and centred).

## The font: 8x16 to 10x20

The font is *WebPlus IBM VGA 8x16* from VileR's Ultimate Oldschool PC Font Pack, **CC BY-SA 4.0**. The original and its licence are in
`theme/font/`; `scripts/build_vga_font.py` rasterises its 256 code page 437 characters into `theme/vga8x16.lua` and
`theme/r3i6nos_vga8x16.h` (adaptations, same licence; `scripts/check.sh` fails if they are stale). **What CC BY-SA 4.0 requires, and where it is done:** credit the author, the licence and a link (`NOTICE`, `README.md`, `theme/font/README.txt`, and the
header of each generated file); say what was changed (the same places); keep the licence text (`theme/font/LICENSE.TXT`); keep the adaptations
under the same licence, so the two generated files are CC BY-SA, not MIT; add no further restriction. **Anything that ships the theme ships
`theme/font/`** (`configure_rootfs.sh` does, and a test checks), and a release that bundles the repo ships `NOTICE`.

At run time each 8x16 glyph becomes 10x20 by nearest neighbour: destination column x takes source column `floor(x*8/10)`, row y takes
`floor(y*16/20)`. So source columns 0 and 4 and rows 0, 4, 8 and 12 are doubled. The map depends only on the position in the cell, so box
lines and blocks join across cells exactly as in the 8x16 original (tested). No smoothing, no other font, no bold or italic.

## Colour

Only the 16 VGA colours (`theme.RGB`). The theme's own pairs are `theme.STYLE`, and **every text/background pair must pass 4.5:1**
(WCAG AA; `theme.contrast`, `tests/test_theme.lua` and `tests/test_menu_view.lua` check). Dark gray, blue and red do not pass on black:
use them only as a frame or fill, never for words. Colour is never the only cue: errors say so in words, the selection is also a bar.
Nothing on screen changes more than twice a second.

## List dialogs (as in KlamANSI)

The standard list: a double-line box, the title and `Page x/y` in the top edge, one row an item (label at the left, value at the right),
the selected row **black on cyan**, a foot of keys in the bottom edge (`theme.listDialog`). It stays open until accepted or backed out of.
Dialogs (Settings, Shut down?) are the same list in a smaller box centred in the display view. The rule every list follows
(`theme.listKey`; KlamANSI's gesture table with the pad's buttons for the swipes):

| Action | R36S | Keyboard | Does |
|---|---|---|---|
| up / down | D-pad up / down (9 / 10) | arrows | select, wrapping |
| left / right, pageup / pagedown | D-pad left / right (11 / 12), L1 / R1 (5 / 6) | arrows, PgUp / PgDn | turn a page |
| home / end | L2 / R2 (7 / 8) | Home / End | top / bottom of the list |
| accept | A (2) | Enter, Z | Enter |
| back | B (1) | Esc, X | a step back; closes as Cancel |
| more | Start (14) | Tab, F1, S | More... on the selection (the menu: Settings) |

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

**Native (C, SDL2)**: `theme/r3i6nos_vga8x16.h` has the 256 glyphs (`r3i6nos_vga8x16[i][row]`, top bit leftmost) and the Unicode of each
(`r3i6nos_cp437[i]`). Use the same 64x24 grid of 10x20 cells, the same row roles, colours and mapping from 8x16 to 10x20 as above.
There is no C library for the theme yet (ROADMAP).

## What the theme does not cover

- Not done: the default input checkers (`apps/`) still draw their own pad picture; they move onto the theme when they are next changed.
- Not run on a real R36S, or under LÖVE with a display: `theme.init` and `theme.draw` (the atlas, the canvas, the scaling) are only
  checked by reading them; the data side (font, mapping, layout, list logic, the menu's screen) is tested by luajit.

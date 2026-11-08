# KlamANSI on the R36S: the plan

Status (2026-11-08): **built and tested headless; never run on a real R36S.** The code lives in the private KlamANSI repo (`r36s/`, its README has the detail);
this file is the plan and the decisions. Phases 1-5 below are done there: the QuickJS spike, the host, the controls, Files on the card and the package
(`KlamANSI.deb`, installed by `deb_watcher` in a clean Debian 12 under Docker). What nobody has seen: the pad, the panel, a card, speed and memory. KlamANSI
is the ansiapps text-mode drawing REPL (`~/Coding/klamansi`, private, proprietary, a paid native app with a free shareware web version). `ROADMAP.md`
section 8 tracks it.

## 0. Decisions (made by the maintainer, 2026-11-08)

1. **Where the code lives: the private KlamANSI repo** (an `r36s/` folder and its own workflow). This repo keeps the plan, the theme, the pad
   mapping and a `ROADMAP.md` pointer, and never any KlamANSI code (rule 2). The host reads this repo's public theme
   (`theme/r3i6nos_ansiapps.h`; the CC BY-SA font credit ships in the deb).
2. **Release: free, with no shareware notices**, a gift for r3i6nos users. The R36S build disables the notices (`public/edition.js`) and
   is not sold.
3. **The typed line is the usual three-row command line** (reply, prompt, status), not a side-column box. It **floats over the bottom three rows
   of the display, flush to the bottom, and appears only while something is typed**. When a message must be shown, the line appears with it
   ("press any key to continue...") and hides on the next interaction. **Fn shows and hides it together with the virtual keyboard.**
   This replaces the LINE box of section 2: the side column keeps STATUS, RADIAL and PANELS, with the freed rows to be reassigned by looking
   at pictures.

## 1. Shape: a native host around KlamANSI's pure modules

`public/klam.js` (the language), `screens.js` (every screen and the key router `route(model, key, ctx)`), `ansi.js` (`Screen`, a grid of
characters and 16-colour cells), `vinput.js` and `listdlg.js` are **pure: no DOM**, and the tests already drive them in Node. Only `app.js` (DOM glue)
is browser-bound. So the port is:

- **`klamansi` native ARM64, C and SDL2**, like Commander, with the r3i6nos C theme drawing cells at 10x20.
- **QuickJS** (MIT, Fabrice Bellard) embedded to run those modules unchanged. Its module loader strips the `?v=124` cache-buster from imports.
  QuickJS itself is built from its pinned public tarball: **the first real use of the port workflow** (a `TYPE=port` manifest for QuickJS, or a
  static library built in the host's `build.sh`).
- **A small host layer in JS** (`r36s-host.js`, in the KlamANSI repo) replaces what `app.js` does that is not drawing: the frame clock, a model
  of "keys in, Screen out", saving `.kan` files, and the settings. The C side blits the `Screen` cells, reads the pad, and talks to the files.
- Bytecode is precompiled with `qjsc` at build time so the start is fast (CLAUDE-global "fast start": a splash at once, then ready; slow work in
  slices, never blocking the first frame).
- Left out of the first version, each its own later item: Convert (image to ANSI), the time-lapse and GIF exports, sound (`sfx.js`, `jingle.js`),
  the cloud and share features, the explainers (400 KB, loaded lazily if kept). The core loop (draw, Klam, files, help, tutorials) comes first.

**Unknown until a spike runs** (section 5, phase 1): whether every module loads in QuickJS (any browser-only API they touch), how fast `drawCanvas`
and `route` are on a 1.3 GHz Cortex-A35, and the memory use next to the OS on 1 GB.

## 2. The screen: 64 x 24 cells on 640 x 480

This port departs from CLAUDE.md rule 8 (no top bar, no three-row command line) on purpose; its README says why.

```
cols 0-47                                      cols 48-63
+------------------------------------------------+----------------+
| KlamANSI's display view, 48 x 24, flush        | LINE   (rows 0-4)   reply, mode char, the line being typed, wrapped
| top-left: the canvas, or a panel's page        | STATUS (rows 5-9)   cursor x,y, pen colours, layer, frame, pen on/off
|                                                | RADIAL (rows 10-18) the 8 shortcuts around a centre, the live one lit
| (Fn: the keyboard covers rows 13-23 of it)     | PANELS (rows 19-23) Draw Canvas Log Code Files Help, current marked
+------------------------------------------------+----------------+
```

- The display is KlamANSI's own 48 x 24 window, drawn by its own code, in the same cells; nothing about it changes.
- The side column is 16 cells wide, **non-overlapping framed boxes each given whole rows**, a cut box ending in `…`, nothing scrolls. It is
  interactive only through the pad (it is not a dashboard); a box that has nothing to say stays blank.
- **LINE** carries what the three command-line rows carry in the web app (reply, mode character and typed line, wrapped over its rows). **STATUS**
  is the status row's content stacked. **PANELS** replaces the tool bar. **RADIAL** is the legend of section 3.
- Colours: the 16 VGA colours in pairs that pass 4.5:1; the canvas shows the art's own colours. Every glyph is code page 437.
- Row budgets above are a first cut; they are fixed by looking at pictures (the `R3I6NOS_*_SHOTS` approach Commander uses), not by this table.

## 3. Controls (raw buttons, `docs/hardware-findings.md`: SDL's mapping does not match this pad)

| Pad | Does |
|---|---|
| **D-pad** (buttons 9-12) | arrow keys; in the virtual keyboard it moves the highlighted key; in a menu, the selection |
| **A** (2) | Enter; on the virtual keyboard it presses the key under the highlight |
| **B** (1) | Esc |
| **X** (3) | Backspace |
| **Y** (4) | KlamANSI's **Mode** key: Command `>`, Draw `/`, Type `@`, Erase `%`, Edit `#` |
| **Select** (13) | Tab (completion, as on a keyboard); fires on release, so it can be a chord |
| **Start** (14) | **always opens the contextual menu** (below) |
| **Fn** (17) | toggles the virtual keyboard |
| **Left stick** (axes 1-2) | the mouse pointer, a highlighted cell that moves over the whole 64 x 24 screen; speed rises with the push, half speed while R2 is held |
| **L1** (5), **R1** (6) | left and right mouse button, held for drags (a held left button paints along the pointer, as a finger on the display does) |
| **Right stick** (axes 3-4) | the **radial**: push to a direction to light that slot (8 slots, dead zone 0.5), **release to run it**; release at the centre cancels |
| Select + Start, held 1 s | quits to the menu (the platform rule, so nobody is stuck); a tap of either alone does its own job |
| L2 (7), R2 (8), L3 (15), R3 (16) | **unassigned in your list.** Proposed: L2 = Shift (held; Shift+Tab walks the panels), R2 = slow pointer, R3 = open the radial setup, L3 = centre the pointer on the cursor |

- **R1, the right mouse button:** KlamANSI has no right click today (touch first). Proposed: R1 picks the colour under the pointer (`pick`); to be
  confirmed, since it adds a gesture the web app lacks.
- **The mouse** drives what a finger does on the web page: tap a cell = `at X Y` / `plot`, hit a button in the virtual input, drag = a stroke
  (`canvasCellAt`, `strokeStatements`). The pointer can cross into the side column only as a highlight; that column takes no clicks.
- **Virtual keyboard (Fn):** only KlamANSI's keyboard (its 11 rows from `vinput.js`) is overlaid on the display's rows 13-23, so the top half of
  the canvas stays visible; the palette, touch pad and picker are not needed (the pointer, the radial and Mode cover them). Every key still types
  a command, as the web app's does. The overlay is not a mode of the pad: the D-pad is the highlight and A the press while it is open.
- **Contextual menu (Start):** a KlamANSI list dialog (`listdlg.js`, the gestures of `docs/R3I6NOS-ANSIAPPS-THEME.md`) whose items depend on
  where you are: on the canvas (Mode, Undo, Redo, Save, Open, Next panel, Virtual keyboard, Radial setup, Help, Quit); in Files (Open, Save as,
  Rename, Delete, Back); in Help or a tutorial (Next, Previous, Close); with a line half typed (Run it, Clear it, History). Quit is always there.
  What each context lists is a table in the host, tested.
- **Radial, 8 customisable commands.** Defaults from KlamANSI's own drawing commands (a proposal): undo, redo, pen (toggle), pick, erase, flood,
  line, box. Clear and other destructive commands are not defaults. A slot is a Klam command line, run as if typed and entered. The set lives in a
  small settings file in the app's own folder on the games card (never in a `.kan`, so a drawing opens the same anywhere); it is edited from the
  Start menu (Radial setup: pick a slot, then pick from the command list). Reset to defaults is there too.
- **Games card:** the port writes only inside its own `KlamANSI/` folder (drawings, settings); it never formats, repartitions or deletes outside it
  (CLAUDE.md rule 7).

## 4. Testing, as Commander does it

- The pad-to-key mapper (chords, release-fired Select, stick to pointer with the dead zone and speed, radial sector, menu context table) is a
  **pure module with unit tests**; it takes raw button and axis numbers as data.
- `klamansi --selftest` runs the host against KlamANSI's own pinned pictures (a `.kan` from `examples/` must draw the same cells as in Node), then
  every screen as a picture (`R3I6NOS_KLAM_SHOTS=dir`). Under qemu in CI with the dummy video driver.
- **Not testable here, so "untested" until a card says otherwise:** the buttons and sticks on the unit, how 10x20 cells look on the panel,
  speed and memory, and the files on exFAT.

## 5. Phases (mirrored in `ROADMAP.md` section 8)

1. **Spike:** QuickJS cross-built in the Debian 12 container (as a `TYPE=port` for its pinned source, or inside the host's build); load `klam.js`
   and `screens.js`, run an example `.kan`, dump the `Screen` as text and compare with Node's. Finds every browser-only API. *Go/no-go.*
2. **Host:** SDL2 window, the 48 x 24 display flush top-left with the theme's cells, the side column, the frame clock, the splash; keys in,
   screens out.
3. **Controls:** the pure mapper, the pointer, the radial and its setup, the virtual keyboard overlay, the Start menu and its contexts.
4. **Files and settings** in the app's own card folder.
5. **Package:** `.deb`, qemu smoke, the release workflow in the KlamANSI repo (calling this repo's reusable one only if the source is made public),
   a try on a real R36S (rule 1: say what was not run).
6. **Later:** Convert, exports, sound, the explainers.

## 6. What changed from this plan while building it (2026-11-08)

- **The typed line** is the usual three-row command line, floated over the display's bottom rows only while typing or reading a reply (section 0 item 3),
  not a box in the side column. The side column is STATUS, RADIAL and PANELS.
- **The keyboard** (Fn) is on display rows 10-20, directly above the line, not rows 13-23 (which would have covered the line).
- **The radial's default slots** are Klam lines built when they run: `pen` and `erase` toggle, `line` and `box` go to the pointer's canvas cell (the plan's bare
  `pen`, `erase`, `line`, `box` are not valid Klam lines).
- **R1** runs `at X Y` then `pick` (the plan marked it "to be confirmed"; not yet confirmed).
- **Files** are real files and directories under `/roms/KlamANSI/drawings/`, with an autosave and a welcome back. The Files panel has the card's own wording
  (no browser, download, export or import: `ctx.platform` in KlamANSI's `public/screens.js`); `download`, `export`, `import` say they are not on the R36S yet. The Help panel lists
  only the prompt commands the device runs, names the rest as not there yet, describes the pad in About and has no stroke chart (`helpLines(platform)`).
- **Release:** free with no shareware notices (the host never loads the notice code). KlamANSI's licence grants no redistribution, so the package carries a draft
  grant that the maintainer must confirm before a release.

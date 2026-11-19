# r3i6nos input mapping: the default behaviour of the R36S's inputs

Written 2026-11-19 at the maintainer's request. **This is the default every app and the menu should follow, and a specification only: nothing here is
built yet** (ROADMAP section 39). Where an app already does something else (MusiCue's own map, the Wi-Fi keyboard's Select and Start), the
"Where today's code differs" section says so. Nothing has run on a unit.

The physical inputs are the ones measured on a real R36S (`docs/hardware-findings.md`, "Input layout"): B, A, X, Y, L1, R1, L2, R2, the D-pad, Select,
Start, Fn, the two sticks and their presses (L3, R3). Power and volume are keyboard keys and are not part of this map.

## The idea

1. **L1, R1, L2 and R2 are modifiers, like Shift and Ctrl.** Held, each changes what the other inputs do (a different set), so a small pad reaches
   many commands without a menu. Two of them together are system chords (below).
2. **Everything else is a plain key or a mouse**, so an app written for a keyboard and a pointer works with the pad and the pad works like a
   keyboard everywhere.
3. **What is fixed and what is the app's.** The **modifiers, the system chords, Fn, the sticks' roles and the pointer and keyboard toggles** are the
   same in every app. **The one-touch buttons (D-pad, A, B, X, Y, Select, Start) are always subject to each app's own configuration**, because they
   follow what the app does (MusiCue's Start plays). The table of plain inputs below is the **default** an app starts from and departs from only
   for its function, saying so in its README (the same rule as the theme's, rule 8).
4. **Fn is always a panel key and never a modifier.**

## Every app has a main menu (a scaffolding rule)

**Every r3i6nos app has a main menu, opened by L1 + R1, whose first items are these, in this order, with the app's own items after them** (the
compulsory ones are never renamed, reordered or moved elsewhere; **Benchmark is hidden when the app has nothing to measure**, the other five are always there):

| Item | Does |
|---|---|
| **About** | The app's name, version, commit and date, licence and credits (the splash's version line, rule 12). |
| **Colour theme** | The theme choice (VGA Classic by default; true-colour themes where the app offers them, rule 8). |
| **Device report** | Runs the app's device report (rule 9). |
| **Rec demo** | Runs the app's Rec demo (rule 11). |
| **Benchmark** | Runs the app's benchmark: the speed against its goal, shown on screen and written to the report folder. **Hidden when the app has nothing to measure** (a viewer, a settings app); the order of the others does not change. |
| **Quit** | Leaves the app. |

- **Quit is how an app is left. r3i6nos apps do not use Select + Start to leave** (it was the old convention; each app's own map may now use Select and
  Start for its function). An app still leaves cleanly when the system closes it (shutdown, the menu's power-off).
- **This is rule 17 in `CLAUDE.md`** (2026-11-19). It is to be built into the theme (`docs/R3I6NOS-ANSIAPPS-THEME.md`) as one menu structure both the
  LÖVE and C themes provide, so a new app gets the items by calling it, and a self-test checks them (ROADMAP section 39). Not built yet.
- A third-party game or port (PortMaster, a game in `Love 11.5/`) is not an r3i6nos app and has no such menu; **it keeps the existing way out
  (`r3i6nos-portquit`, Select and Start held 2 s)**, since it never sees this map.

## The system chords (the same in every app)

| Chord | Does | Notes |
|---|---|---|
| **L1 + R1** | Opens the **main menu / settings** | The app's own menu (its Start menu items, Settings) or, in the boot menu, its Settings. |
| **L2 + R2** | Opens the **contextual menu** | For the focus: the selected item, or the panel in focus when nothing is selected. |
| **Fn** | Next panel | The app's panels turn in a **carousel**: 1, 2, 3, ..., last, then 1 again. |
| **L1 + Fn** | Jump to the **first panel** | Usually the app's main panel. |
| **R1 + Fn** | Jump to the **previous panel** | The one shown **before** the one showing now (a history, not "number minus one"). Pressed twice it comes back, like Alt+Tab between two. |

**Select + Start no longer leaves an app** (Quit in the main menu does). Fn is only ever the panel key, alone or with L1 or R1 as above; it is never
held as a modifier for other inputs.

A chord fires when its second input goes down; the modifiers alone do nothing on their own at the system level, so the chords cannot be pressed by
accident while using a modifier for something else.

## The plain inputs

**The defaults below are what an app starts from.** The one-touch buttons (**D-pad, A, B, X, Y, Select, Start**) are **always subject to the app's own
configuration**, from what the app does; the sticks, L3 and R3 keep their roles. The Select and Start rows apply only where the app has the virtual
mouse.

| Input | Default | Keyboard equivalent |
|---|---|---|
| **A** | Accept, activate, choose | Enter |
| **B** | Back, cancel, close | Esc |
| **X** | Delete the selected item, or the character before the cursor | Backspace / Delete |
| **Y** | Next field or control, "more" (a menu of further actions) | Tab |
| **Right stick** | Moves the **selection** (the highlighted item or key) | Arrow keys |
| **D-pad** | Moves the **selection**, the same as the right stick | Arrow keys |
| **Left stick** | Moves the **cursor**: the text cursor in a field, the **pointer** when it is on | Mouse or text cursor |
| **Select** | **Left mouse button**, when the pointer is on | Left click |
| **Start** | **Right mouse button**, when the pointer is on | Right click |
| **L3** (press the left stick) | Toggle the **virtual mouse** on and off, **where the app has one** | |
| **R3** (press the right stick) | Toggle the **virtual keyboard** on and off, **where the app has one** | |

- **Whether an app has the virtual mouse and the virtual keyboard is specified per app** (its README and its code say which). An app without one
  ignores L3 or R3 (a short reply on the command line is allowed, never a silent no-op that looks like a bug).
- **When the pointer is off**, Select and Start are the app's own (they are not clicks). The pointer being on is shown (a visible pointer, and a
  mark in the top bar or status so it is never "on" invisibly).
- **The D-pad and the right stick do the same job** (move the selection). The stick is analogue: past the dead zone below it steps like a held D-pad
  direction, at a rate that follows how far it is pushed.
- **The sticks' dead zone is FM1-NTT's, the one it uses to turn a knob** (`STICK_DEAD` in `fm1ntt/host/fm1ntt.c`): **0.35** of the axis's travel. Inside
  it the stick does nothing; past it the stick steps, **faster the further it goes**, from nothing at 0.35 to **a conservative top of 6 steps a second at full**
  (`(|v| - 0.35) / 0.65 * 6`, the fraction of a step kept between frames so a slow stick is not lost). FM1-NTT's knobs go to 12 a second; a list
  or a text cursor is easier to overshoot, so this map halves it. The right stick's selection and the left stick's text cursor use these numbers.
  **The pointer** uses the same dead zone and moves **120 pixels a second at full deflection** (4 seconds across the 640 pixel screen), scaled the same way.
  **Holding R1 speeds either up** (next section): the selection's top rate stays 6 but it moves by pages, the pointer goes three times as fast
  (360 px/s). All four numbers (0.35, 6, 120, 3) are constants in the theme, so they are changed in one place. **Untested on a unit**: they are
  starting points for the maintainer to try (FM1-NTT's own sticks are untested, too).
- **The left stick moves the text cursor in a text field** and the pointer when the pointer is on. With the pointer off and no field, it does
  nothing by default.
- Raw numbers, from the measured mapping (LÖVE counts from 1, SDL from 0): B 1, A 2, X 3, Y 4; L1 5, R1 6, L2 7, R2 8; D-pad 9-12; Select 13, Start 14;
  L3 15, R3 16; Fn 17; left stick axes 1-2, right stick axes 3-4. **A is 2 and B is 1**: the pad is not laid out like SDL's mapping.

## What the virtual mouse and keyboard override

**While the virtual mouse or the virtual keyboard is showing, it overrides the app's use of the inputs it needs, until it is closed:**

| Showing | Overridden | Everything else |
|---|---|---|
| **Virtual mouse** | **Select** (left click) and **Start** (right click), and the left stick (the pointer). The app's own Select and Start commands are suspended. | The app's map as it was (the D-pad, A, B, X, Y keep their app meanings). |
| **Virtual keyboard** | **Select, Start, the D-pad, the right stick, A, B, X and Y** (the table in the next section); the pointer is paused. | The system chords and Fn still work, and so does L3/R3 to close. |
| **Both** | The keyboard wins where they overlap. | |

The overridden inputs come back exactly as the app had them when the mouse or keyboard closes; a press that closed it is used up (it does nothing
else), as the hot-plug toast's key is.

## The modifier sets

L1, L2, R1 and R2 each select **another set for the inputs above** while held, like Shift on a keyboard. **The default map above is the unmodified
set.** What the modified sets hold is for each app to define, with these rules so they stay predictable:

- A modified input is **the same kind of action, more of it** (Shift+arrows extends a selection, a modifier with the stick moves faster): never an
  unrelated command.
- **R1 is "increase"**, the same in every app, held with any movement:

  | Held | Up / down | Left / right | Pointer |
  |---|---|---|---|
  | (nothing) | one item up or down | the app's own (a column, a field, a tab) | 120 px/s at full |
  | **R1** | **Page up / Page down** (a whole page of the view, `Page n/m` in the foot) | **Home / End**: to the start, to the end | **3 times as fast** |

  The right stick and the D-pad do the same (a stick past its dead zone with R1 held pages at the stick's rate, 6 a second at most; Home and End fire
  once for each push past the dead zone, not repeatedly). In a text field Home and End are the start and end of the line. In the virtual keyboard
  they are the start and end of the row, and Page up and down the top and bottom row. R1 alone, released with nothing pressed, is still the
  app's (the "on release" rule below).
- L1 + R1, L2 + R2, L1 + Fn and R1 + Fn are taken by the system chords, so an app does not use them. L1 + L2, R1 + R2, L1 + R2 and R1 + L2 are
  **reserved** for later system use; an app avoids them too.
- A modifier **pressed and released alone**, with nothing else in between, may act as its own key (MusiCue's L1/R1 step a marker "on release" so
  the chord does not trigger it). That use is the app's, and it must not fire when the modifier was used in a chord.
- The foot of the screen (the KEYBOARD page of the bottom window, in the text-mode UI) shows the set that is **active now**, so holding a modifier
  shows what it will do.

## The virtual keyboard

R3 opens it and closes it again. **It is the keyboard the menu already uses for a Wi-Fi password** (`menu/osk.lua`): a 10 by 4 grid of keys, three
layers (abc, ABC, #+=), the text shown, optionally hidden. The map above drives it:

| Input | In the keyboard |
|---|---|
| Right stick, D-pad | Move over the keys |
| **A** | Type the key under the cursor (Enter on an "enter" key) |
| **X** | Delete the last character (Backspace) |
| **Y** | Change the layer (abc, ABC, #+=) |
| **B** | Close without typing the rest (Esc) |
| **R3** | Close the keyboard (as it opened it) |

- The keyboard **takes the pad while it is open** (the override table above): the pointer is paused, and what the keys type goes to the focused field.
  **Select and Start belong to the keyboard then**: in the menu's password entry, Select shows or hides the text and Start accepts, as today, so
  that entry already behaves this way.
- **One keyboard, written once**, for LÖVE (`menu/osk.lua` becomes a theme module) and for C (a theme header), so every app has the same one; it is
  not rebuilt in each app. A USB keyboard still types directly.

## Where today's code differs

- **Select + Start leaves the app today** in the theme's apps, `run_app.sh` and every app's docs; it is to be removed from r3i6nos apps once each has
  Quit (third-party ports keep `r3i6nos-portquit`).
- **No app has the six compulsory menu items yet.** The menu has About and Device reports in Settings, not as one list; MusiCue's L1 + R1 menu
  has its own items; none has Colour theme, Rec demo and Benchmark together, and no app has a Benchmark item at all.
- **FM1-NTT** (`fm1ntt/README.md`, "The pad on MACHINE") already has L1 + R1 for the menu, Fn for the panels, the sticks as knobs and L1, L2 and R2 as modifiers, the dead zone this map takes. It differs in one place: **L1 + Fn there is "next scale" and the panel stays**, where this map's L1 + Fn is "first panel"; and its Select + Start are OCT- and OCT+, which this map allows (per-app buttons).
- **MusiCue** has its own pad map (`docs/MUSICUE.md`, "The pad, the panels and Tempo"): L1 + R1 is the menu and Fn turns the panels, as here, and
  Start, Select and D-pad Down are its own, which this map allows. L2 + R2, L1 + Fn and R1 + Fn do not exist in it yet, and its Select + Start still leaves.
- **The menu's Wi-Fi keyboard** (`menu/osk.lua`) is opened by choosing an item, not by R3.
- **Commander, the IO Checker, KlamANSI, NoCoGaMa and the others** have their own maps, none of them with the pointer, the carousel or the chords;
  the theme's `r3_pad_action` (`theme/`) is where one shared reading of the pad would live.

## Open items for the maintainer

1. **The numbers to try on a unit:** 6 steps a second, 120 px/s for the pointer and a factor of 3 for R1 are conservative guesses, not measured. They
   are meant to be raised if they feel slow (the dead zone stays 0.35).
2. **The theme doc's section** for the main menu (the rule is in `CLAUDE.md`; the theme doc is not edited yet).

Settled by the maintainer on 2026-11-19: R1 is "increase" (page up and down, home and end, a faster pointer); conservative top rates; the main menu's order (About, Colour theme, Device report, Rec demo, Benchmark, Quit) and that Benchmark is hidden when there is nothing to measure; the sticks' dead zone is FM1-NTT's; Select + Start no longer leaves; the one-touch buttons are per app; the virtual mouse and keyboard
override Select and Start (and the D-pad and others for the keyboard); their availability is per app; Fn is only ever the panel key.

## What was not run

Nothing. No code reads this map yet; it has not been tried on a unit, and the sticks' presses (L3, R3, buttons 15 and 16) are from the measured
mapping and have not been checked in any app.

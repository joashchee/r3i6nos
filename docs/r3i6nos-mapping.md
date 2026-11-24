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
5. **A panel may remap the one-touch buttons while it has the focus** (settled 2026-11-20). An app is a set of panels; each panel's own map may bind the D-pad, A, B, X, Y,
   Select and Start to what that panel does (a MACHINE panel plays notes on them, a MIDI panel sets the channel on Select and Start). It keeps as close to the defaults below
   as its job allows, and where a default means nothing in that panel's context it uses the button for something that does. The system's inputs (L1, R1, L2, R2, Fn, L3, R3, the chords,
   R1's "increase") are not the panel's to remap. A panel may give R1 a job of its own that is "increase" in its terms (a MACHINE panel: R1 + left and right the octave, R1 + up and
   down the key), since "the same kind of action, more of it" is the only rule R1 has.

## Every app has a main menu (a scaffolding rule)

**Every r3i6nos app has a main menu, opened by L1 + R1, whose first items are these, in this order, with the app's own items after them** (the
compulsory ones are never renamed, reordered or moved elsewhere; **Benchmark is hidden when the app has nothing to measure**, the other six are always there):

| Item | Does |
|---|---|
| **About** | The app's name, version, commit and date, licence and credits (the splash's version line, rule 12). |
| **Colour theme** | The theme choice (VGA Classic by default; true-colour themes where the app offers them, rule 8). |
| **Keymap** | **The app's input map, as help, test and remap** (below, "The Keymap"). Added 2026-11-20 at the maintainer's request, right after Colour theme. |
| **Device report** | Runs the app's device report (rule 9). |
| **Rec demo** | Runs the app's Rec demo (rule 11). |
| **Benchmark** | Runs the app's benchmark: the speed against its goal, shown on screen and written to the report folder. **Hidden when the app has nothing to measure** (a viewer, a settings app); the order of the others does not change. |
| **Quit** | Leaves the app. |

- **Quit is how an app is left, and leaving is a deliberate choice: only the main menu's Quit (or the system closing the app) leaves the desktop or the
  launcher. No pad button or chord, and no keyboard key (Esc, Ctrl+Q, Cmd+Q-style shortcuts a desktop build adds, Select + Start, a function key) quits an app** (2026-11-20; made a mapping rule 2026-11-22 at the maintainer's request, "always make the app quit by the main menu"). **A desktop build's keyboard map has no quit key either**; what the operating system does to a window (its close button, the system's Quit or shutdown) is the system closing the app, not the app's map. r3i6nos apps do not use Select + Start to leave (it was the old
  convention; each app's own map may now use Select and Start for its function). An app still leaves cleanly when the system closes it (shutdown, the menu's power-off).
- **This is rule 17 in `CLAUDE.md`** (2026-11-19). It is to be built into the theme (`docs/R3I6NOS-ANSIAPPS-THEME.md`) as one menu structure both the
  LÖVE and C themes provide, so a new app gets the items by calling it, and a self-test checks them (ROADMAP section 39). Not built yet.
- A third-party game or port (PortMaster, a game in `Love 11.5/`) is not an r3i6nos app and has no such menu; **it keeps the existing way out
  (`r3i6nos-portquit`, Select and Start held 2 s)**, since it never sees this map.

## The system chords (the same in every app)

| Chord | Does | Notes |
|---|---|---|
| **L1 + R1** | Opens the **main menu / settings** | The app's own menu (its Start menu items, Settings) or, in the boot menu, its Settings. **Keyboard: the backquote key (`` ` ``)** (settled 2026-11-20; see "The keyboard bindings"; it replaces the proposed F10, which an app may already use for its own). |
| **L2 + R2** | Opens the **contextual menu** | For the focus: the selected item, or the panel in focus when nothing is selected. **Keyboard: the backslash key (`\`)** (settled 2026-11-20). |
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
| **A** | Accept, activate, choose | L, Enter or Space |
| **B** | Back, cancel, close | Esc |
| **X** | Delete the selected item, or the character before the cursor | I or Backspace (Backspace deletes in a text field) |
| **Y** | Next field or control, "more" (a menu of further actions) | J or Tab |
| **Right stick** | Moves the **selection** (the highlighted item or key) | Arrow keys (up, down, left, right) |
| **D-pad** | Moves the **selection**, the same as the right stick | W A S D |
| **Left stick** | Moves the **cursor**: the text cursor in a field, the **pointer** when it is on | Mouse or text cursor |
| **Select** | **Left mouse button**, when the pointer is on | F |
| **Start** | **Right mouse button**, when the pointer is on | H |
| **L3** (press the left stick) | Toggle the **virtual mouse** on and off, **where the app has one** | P |
| **R3** (press the right stick) | Toggle the **virtual keyboard** on and off, **where the app has one** | V |

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

## The keyboard bindings (permanent defaults for every app, 2026-11-23)

**Settled by the maintainer on 2026-11-23 (replacing the 2026-11-22 map): these keyboard bindings are the permanent defaults of every r3i6nos app that runs with a keyboard (a desktop build, a USB keyboard on the unit).** Every pad input has a key, so
nothing is out of reach from a keyboard; an app does not choose other keys for these inputs (a panel's own remap of the one-touch buttons, above, changes what a button does, never which key is the button). The left hand has the D-pad, the shoulders
and the system buttons; the right hand has the face buttons and the arrow keys (the right stick).

| Pad input | Key |
|---|---|
| D-pad | **W** up, **S** down, **A** left, **D** right |
| Right stick (the selection) | **Arrow keys** (Up and Down with R1 held: Page up and down) |
| **X** | **I**, or Backspace |
| **Y** | **J**, or Tab |
| **B** | **K**, or Esc |
| **A** | **L**, or Enter, or Space |
| **Select** | **F** |
| **Fn** (next panel) | **T** (a tap) |
| **Start** | **H** |
| **L1** (held) | **Q** |
| **L2** (held) | **E** |
| **R1** (held) | **O** |
| **R2** (held) | **U** |
| **L3** | **R** |
| **R3** | **Y** |
| **L1 + R1** (main menu) | Backquote (`` ` ``), or Q + O |
| **L2 + R2** (contextual menu) | Backslash (`\`), or E + U |
| Left stick | none (the pointer and the text cursor are the mouse and the text field) |

The face buttons I J K L sit as the pad's diamond does: I (X) above, J (Y) left, L (A) right, K (B) below.

- **Built (2026-11-24):** `r3_key_button` in `theme/r3i6nos_input.h` and the native IO Checker (keys reach the same reader as the pad's buttons). The other native apps still use the older keys until each is moved over.
- **Fn is T**, a tap, never held (no Caps Lock: it is a toggle on macOS and was unreliable). A Mac keyboard's own Fn key is not seen by programs. L1 + Fn and R1 + Fn work with Q and O held as usual.
- **No Shift, Ctrl, Alt or Caps Lock binding.** They are free for the system and for typing. **Untested on a real keyboard** beyond the self-tests.
- **A text field first.** While a text field, the id field or the virtual keyboard has the focus, letters, digits and Backspace type or delete in it and are not the bindings above; the bindings return when it
  closes. (This is why every letter above is safe as a key.)
- **No key quits** (Esc, any function key, Ctrl+Q: none; Q is L1). Quit is the main menu's (the rule above).
- **Keys a desktop adds for its own sake are the system's, not the map's:** the operating system's shortcuts (Cmd+Tab, Mission Control's Ctrl+arrows, Ctrl+Space for the input source) can take a chord before the app
  sees it; an app says so in its README rather than binding other keys. Where one clashes, the fix is a system setting, not another key here.
- **Every app's Keymap page and About screen list these keys** on a desktop build, and a self-test is to check that each pad input in the table has exactly one key and that no two inputs share a key (not built yet).

## The Keymap (a compulsory main-menu item, 2026-11-20)

**Every r3i6nos app's main menu has a Keymap item, right after Colour theme. It is the app's input map as a help page, a test and a remapper.** It is the same screen in every app (to be
built into the LÖVE and C themes once, like the main menu; **the C theme has the help and the test since 2026-11-20 (`theme/r3i6nos_keymap.h`; MusiCue, the IO Checker and Commander use it); remap is still FM1-NTT's alone**); FM1-NTT built the first one (`host/fm1ntt.c`, "the Keymap").

- **Pages.** Pages of the **system's inputs** (the chords, the keyboard keys, R1's "increase", the sticks, L3 and R3, what is never bound), then **one page for each panel whose one-touch buttons
  mean something**, each row a button and what it does *in that panel*, and **extra pages for the panels that have held-button sets of their own** (FM1-NTT's MACHINE: L1, L2, R2 and R1). The Keymap
  opens on the page of the panel it was opened over. Left and right turn the pages (R1 held: first and last). Nothing scrolls: more rows than fit are whole pages.
- **Test.** The last page shows every press with what it does in the panel the Keymap was opened over, the buttons held and the sticks, and acts on none of it; L1 + R1 leaves it.
- **Remap, with conflict checking.** A on a row, then the new button. **L1, R1, L2, R2, Fn, L3 and R3 are the system's and are refused** (with the reason on screen). A button another action of the same
  panel already has is a **conflict, asked before anything changes**: A swaps the two, B cancels; a button the panel has no use for is swapped without asking. A binding is always a permutation of the ten
  bindable buttons (B, A, X, Y, the D-pad, Select, Start), so two actions never share a button and none is left without one. The menus and a question (A yes, B no) always use the defaults, so a bad map
  cannot lock a person out of the Keymap.
- **Saved, and easy to revert.** Changes are kept at once in the app's own data folder (`keymap.txt`; a damaged or hand-edited file that is not a permutation is ignored). **X puts the row back, Y the
  page, Start every panel's (asked first).** A changed row is marked `*`.
- **Unattended runs use the defaults.** A Device report and a Rec demo press buttons by their default roles, so they set the person's map aside while they run.

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

- **The boot menu (2026-11-24)** reads the pad and the keyboard through the Lua reader (`theme/r3i6nos_input.lua`): L1 + R1, backquote or Y open its main menu (About, Colour theme, Keymap, Device report, Rec demo, Quit, then its settings), R1 held pages, the stick steps the list, the keyboard map is the permanent one, no input quits, and its Quit powers the unit off. Its Rec demo and own report flags, the Keymap's remap and R3 for the Wi-Fi keyboard are not built. Untested on a unit.

- **Keyboard bindings (2026-11-22):** only MusiCue's desktop build has them so far (`key_button` in `../musicue/src/musicue.c`), and there I K J L move the selection only in the player; the C theme's own `r3_key_action` and the other apps still have their own keys and are to follow the table in "The keyboard bindings".
- **MusiCue (2026-11-20)** follows the mapping through the C reader and main menu too (its own one-touch map for Start, Rec mode, the D-pad, A-B on R1, the Jump panel and the Piano, `../musicue/docs/MUSICUE.md`); its Device report and Rec demo items say not built yet.
- **Built so far (2026-11-20): the C theme's reader and main menu** (`theme/r3i6nos_input.h`, `theme/r3i6nos_mainmenu.h`, `tests/test_input.c`) and **the native IO Checker**, which
  uses them (the six-item menu, R1 held for pages and Home/End, the chords, the right stick's rate, **R3i6nos Mapping** to try every input on the unit, Select+Start no longer leaving). Nothing here
  has run on a unit. Everything else below is still as written.
- **The list dialog's keys** (`theme.listKey`, `T.PAD`, `r3_list_key`, `r3_pad_action`, every other app's list) still page with L1 and R1, jump with L2 and R2 and use Start for
  More. The theme doc's table ("List dialogs") is standardised on this map (R1 held for pages and home/end, Y for More); the code follows in ROADMAP section 39 step 2,
  which needs the reader to know R1 is held.

- **Select + Start leaves the app today** in the theme's apps, `run_app.sh` and every app's docs; it is to be removed from r3i6nos apps once each has
  Quit (third-party ports keep `r3i6nos-portquit`).
- **Only the native IO Checker has the six compulsory menu items** (its Rec demo item says it is not built yet; its Benchmark is the Motion frame rate and the cards' speed). No other app does. The menu has About and Device reports in Settings, not as one list; MusiCue's L1 + R1 menu
  has its own items; none has Colour theme, Rec demo and Benchmark together, and no app has a Benchmark item at all.
- **FM1-NTT** (`fm1ntt/README.md`, "The pad on MACHINE") already has L1 + R1 for the menu, Fn for the panels, the sticks as knobs and L1, L2 and R2 as modifiers, the dead zone this map takes. It differs in one place: **L1 + Fn there is "next scale" and the panel stays**, where this map's L1 + Fn is "first panel"; and its Select + Start are OCT- and OCT+, which this map allows (per-app buttons).
- **MusiCue** (2026-11-20) follows this map (`../musicue/docs/MUSICUE.md`, "The pad, the panels and Tempo" and "The Jump panel, the Piano ..."): the six-item menu, L2 + R2, the Fn chords and R1 held as it says, with its own Start, Select, D-pad and face buttons (which this map allows). It has the virtual keyboard (2026-11-21, `theme/r3i6nos_osk.h`: R3 in the player names the selected marker) and no virtual mouse.
- **The menu's Wi-Fi keyboard** (`menu/osk.lua`) is opened by choosing an item, not by R3.
- **Commander, KlamANSI, NoCoGaMa and the others** (the IO Checker's native build now follows the mapping; its LÖVE builds do not) have their own maps, none of them with the pointer, the carousel or the chords;
  the theme's `r3_pad_action` (`theme/`) is where one shared reading of the pad would live.

## Open items for the maintainer

1. **The numbers to try on a unit:** 6 steps a second, 120 px/s for the pointer and a factor of 3 for R1 are conservative guesses, not measured. They
   are meant to be raised if they feel slow (the dead zone stays 0.35).

Settled by the maintainer on 2026-11-19: R1 is "increase" (page up and down, home and end, a faster pointer); conservative top rates; the main menu's order (About, Colour theme, Device report, Rec demo, Benchmark, Quit) and that Benchmark is hidden when there is nothing to measure; the sticks' dead zone is FM1-NTT's; Select + Start no longer leaves; the one-touch buttons are per app; the virtual mouse and keyboard
override Select and Start (and the D-pad and others for the keyboard); their availability is per app; Fn is only ever the panel key.

## What was not run

Nothing. No code reads this map yet; it has not been tried on a unit, and the sticks' presses (L3, R3, buttons 15 and 16) are from the measured
mapping and have not been checked in any app.

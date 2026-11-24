# R36S Font Editor — Reference Findings

A curated reference list and implementation plan for building a **simple, slim, open-source font editor** that runs well on the R36S handheld and is primarily controlled with a gamepad.

## Design goal

Build a controller-first font editor with a small, focused feature set:

- Open and inspect an existing font.
- Select and edit individual glyphs.
- Edit outline points and Bézier handles.
- Pan and zoom the glyph canvas.
- Undo and redo changes.
- Export a basic TrueType font.
- Navigate entirely with D-pad, analog stick, shoulder buttons, triggers, and face buttons.

The initial product should prioritize a comfortable editing loop over professional type-production features. Advanced capabilities such as kerning, components, OpenType features, and variable fonts should be added only after the core workflow is stable.

## Core references

| Reference | Why it matters | License / stack |
|---|---|---|
| [Glyphr Studio 2](https://github.com/glyphr-studio/Glyphr-Studio-2) | The closest model for a lightweight, beginner-oriented font editor. It is web-based and focuses on glyph drawing and simple font design rather than a full professional suite. Its source is in `/src`, with a bundled build in `/dist`. | GPL-3.0, JavaScript |
| [Glyphr Studio organization](https://github.com/glyphr-studio) | Project hub and related repositories; useful for studying UI structure, state model, and scope decisions. | GPL-3.0 |
| [Glyphr Studio website](https://www.glyphrstudio.com/) | Product overview and hosted app; useful for evaluating the intended user experience. | GPL-3.0 |
| [FontForge](https://fontforge.org/en-US/) | The canonical open-source desktop font editor; valuable for reference on OpenType, TrueType, and PostScript handling, glyph outlines, and advanced editing concepts. | Open source, C |
| [FontForge documentation](https://fontforge.org/docs/) | Explains the underlying model: glyphs as paths made of Bézier splines and line segments, with cubic and quadratic curve support. | Documentation |
| [FontForge source](https://github.com/fontforge/fontforge) | Reference implementation for file I/O, outline editing, hinting, and export workflows. More useful as a study reference than as a UI template for a small screen. | Open source |

## Font data and formats

| Reference | Why it matters |
|---|---|
| [fonttools-intro](https://github.com/arrowtype/fonttools-intro) | Practical introduction to FontTools, a Python library for reading and manipulating TTF/OTF binaries, UFO sources, and Designspace files. Includes `cu2qu` for cubic-to-quadratic conversion and `varLib` for variable fonts. |
| [ufoLib documentation](https://fonttools.readthedocs.io/en/stable/ufoLib/index.html) | Use UFO as the editable source format: it is an open, structured format for glyphs, font metadata, and related design data. |
| [designspaceLib documentation](https://fonttools.readthedocs.io/en/latest/designspaceLib/python.html) | Defines axes, sources, instances, rules, and variable fonts. Adopt only if the editor needs interpolation or variable-font support. |
| [Designspace scripting guide](https://fonttools.readthedocs.io/en/latest/designspaceLib/scripting.html) | Shows how sources point to UFO files and how instances and variable fonts are generated from a design space. |
| [varLib documentation](https://fonttools.readthedocs.io/en/stable/varLib/index.html) | Reference for building and interpolating OpenType variable fonts from a design space. |
| [Google Fonts outline guide](https://googlefonts.github.io/gf-guide/outlines) | Clear explanation of on-curve and off-curve points, cubic versus quadratic Béziers, and outline/overlap considerations. |

## Curve and editing model

- Use Bézier outlines as the core editing representation. FontForge describes glyph paths as Bézier splines plus line segments; TrueType commonly uses quadratic curves, while PostScript/CFF uses cubic curves.
- Support import and export rather than making one curve format canonical internally. FontTools includes `cu2qu` specifically for converting between cubic and quadratic Bézier representations.
- Keep glyph geometry separate from UI state. Store contours, points, guides, components, and metadata in a serializable model; treat canvas zoom, selection, undo history, and controller focus as transient UI state.
- Start with a narrow feature set: open/import a font, select a glyph, move/add/delete points, adjust handles, pan/zoom, undo/redo, and export.
- Defer variable fonts unless they are a core requirement. Variable fonts add axes, interpolation, source management, and compatibility constraints; FontTools’ Designspace and `varLib` references are the right foundation if included.

## Small-screen UI practices

| Practice | Recommended approach |
|---|---|
| Essential-first layout | Begin with the minimum viable canvas, glyph list, tool palette, and action bar. Move advanced functions into collapsible menus. |
| Readable text | Use sufficiently large text, avoid thin or decorative typefaces, and test on the actual R36S display rather than relying only on desktop emulation. |
| Focus-visible navigation | Make every actionable element reachable through D-pad or stick navigation, with a clear focus ring and predictable traversal order. |
| Input-aware prompts | Show controller-specific labels such as “A: Select” or “Y: Delete,” and update them when the active input method changes. |
| Large targets and forgiving interaction | Use generous hit areas, snapping, undo, and confirmation for destructive actions. Small screens amplify precision errors. |
| Progressive disclosure | Keep the default view focused on one glyph. Expose spacing, kerning, components, and export settings through separate screens or overlays. |

## Controller mapping

| Reference | Why it matters |
|---|---|
| [SDL GameController mapping](https://wiki.libsdl.org/SDL2/SDL_GameControllerMapping/raw) | Explains SDL’s generic console-style controller abstraction and how device-specific buttons, axes, and hats map to it. |
| [SDL GameController header](https://github.com/libsdl-org/SDL/blob/SDL2/include/SDL_gamecontroller.h) | Defines the mapping format—`bX`, `hX.Y`, and `aX`—and explains adding mappings for unsupported controllers. |
| [pygame controller documentation](https://www.pygame.org/docs/ref//sdl2_controller.html) | Useful for Python prototyping; documents reading and changing a controller mapping through `get_mapping()` and `set_mapping()`. |
| [SDL2 Rust bindings](https://docs.rs/sdl2/latest/sdl2/struct.GameControllerSubsystem.html) | Relevant for a Rust-based R36S client; includes controller detection, mapping addition, and loading mappings from files. |
| [SDL2 C mapping implementation](https://github.com/zielmicha/SDL2/blob/master/src/joystick/SDL_gamecontroller.c) | Shows real default mappings and internal handling of hats, axes, and buttons. |

## Suggested R36S control scheme

| Control | Editor action |
|---|---|
| D-pad / left stick | Move cursor or selected point; hold a modifier for fine movement |
| A | Select / confirm |
| B | Cancel / close overlay |
| X | Toggle draw / edit mode |
| Y | Delete point or clear selection, with undo available |
| L / R shoulder | Previous / next glyph |
| L2 / R2 triggers | Zoom out / zoom in |
| Start | Open main menu |
| Select | Toggle help / control legend |
| Stick click | Center canvas on selected glyph or point |

Use SDL’s generic controller mapping rather than hard-coding R36S button IDs. Provide a small local override file for the R36S so device-specific mappings can be adjusted without rebuilding the application. SDL’s mapping system is designed to translate arbitrary hardware buttons, axes, and hats into a consistent console-style controller layout.

## Recommended architecture

1. **Core font model:** UFO-compatible glyph, contour, and point structures, plus font metadata and export settings.
2. **Geometry engine:** point selection, Bézier manipulation, snapping, undo/redo, and outline validation.
3. **Import/export layer:** FontTools or an equivalent library for reading and writing TTF/OTF, UFO, and optionally Designspace files.
4. **Renderer:** a lightweight vector canvas that draws outlines, control points, handles, guides, and selection affordances at low resolution.
5. **Input layer:** SDL GameController abstraction, action bindings, focus navigation, and per-device mapping overrides.
6. **UI shell:** compact screens for glyph browser, glyph editor, font settings, and export. Avoid trying to show every tool simultaneously.

## Implementation recommendations

- Treat **Glyphr Studio 2** as the primary UI and interaction reference.
- Use **FontTools** as the preferred data, import, and export foundation.
- Use **SDL2** as the controller abstraction and mapping layer.
- Start with **UFO import, single-glyph outline editing, undo/redo, TTF export, and controller-only navigation**.
- Add kerning, components, and variable-font support only after the core editing loop feels comfortable on the R36S.
- Maintain an explicit action map so every UI action can be triggered by controller input, keyboard, and—if applicable—touch.
- Keep the editor responsive by rendering only the active glyph and deferring expensive operations such as full-font validation or export until explicitly requested.

## Suggested first milestone

**Milestone 1 — Controller-only glyph editor**

- Load a UFO or TTF/OTF font.
- Browse glyphs with D-pad or left stick.
- Open one glyph in a full-screen canvas.
- Pan and zoom with triggers and stick.
- Select and move outline points.
- Add and delete points.
- Undo and redo.
- Export a basic TTF.
- Display a persistent controller legend.

This scope is enough to validate the R36S performance, readability, input comfort, and the underlying font-editing data model before expanding into a more complete editor.

# Making a CP437 Font for LÖVE and Web Apps

A practical reference for designing and producing a monospaced, CP437-inspired font that faithfully reproduces DOS-era textmode UIs while remaining readable for programming and extensible for modern text interfaces.

## Goals

- Reproduce the visual language of IBM PC / MS-DOS textmode UIs.
- Include the complete CP437 character repertoire, mapped to Unicode.
- Work as a readable monospaced programming font.
- Eliminate common character ambiguities, especially `1`, `i`, `I`, `l`, `L`, `0`, and `O`.
- Extend CP437 with rounded corners, modern UI symbols, arrows, status icons, and other monochrome interface glyphs.
- Ship as a TTF for LÖVE and as WOFF2 for web applications.

## Primary references

| Reference | Purpose |
|---|---|
| [Unicode CP437 to Unicode mapping](https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/PC/CP437.TXT) | The authoritative byte-to-Unicode mapping for every CP437 character. Use it as the coverage checklist. |
| [IBM PC Code Page 437 reference](https://kermitproject.org/cp437.html) | Readable reference table for CP437 ASCII, accented letters, Greek, box drawing, shading, blocks, and pictographs. |
| [Unicode Box Drawing, U+2500–U+257F](https://www.unicode.org/Public/UCD/latest/charts/PDF/U2500.pdf) | Complete box-drawing repertoire, including rounded arc corners. |
| [Unicode Block Elements, U+2580–U+259F](https://www.unicode.org/Public/UCD/latest/charts/PDF/U2580.pdf) | Shading and block glyphs needed for faithful CP437 rendering. |
| [Unicode Geometric Shapes, U+25A0–U+25FF](https://www.unicode.org/Public/UCD/latest/charts/PDF/U25A0.pdf) | Squares, circles, triangles, diamonds, and related UI primitives. |
| [Unicode Miscellaneous Symbols, U+2600–U+26FF](https://www.unicode.org/Public/UCD/latest/charts/nameslist/2600/) | Monochrome symbols useful for status, weather, religious, chess, and interface iconography. |
| [Unicode Miscellaneous Symbols and Arrows, U+2B00–U+2BFF](https://www.unicode.org/Public/UCD/latest/charts/PDF/U2B00.pdf) | Additional arrows, geometric symbols, and UI-oriented symbols. |
| [LÖVE: `love.graphics.newFont`](https://love2d.org/wiki/love.graphics.newFont) | LÖVE font loading documentation; supports TTF and BMFont files. |
| [LÖVE: `love.graphics.setFont`](https://love2d.org/wiki/love.graphics.setFont) | Documentation for activating a loaded font for text drawing. |
| [LÖVE: Font object](https://love2d.org/wiki/Font) | Font metrics, glyph availability, and text-rendering APIs. |
| [How to Design a Monospace Font](https://diversekit.com/blog/how-to-design-a-monospace-font) | Practical guidance on monospace metrics, narrow-letter treatment, and code readability. |
| [Your Code Font Is Lying to You](https://dev.to/fonthubs/your-code-font-is-lying-to-you-5-things-to-check-in-a-monospace-font-1m33) | Checklist for common programming-font ambiguity problems. |

## CP437 coverage

The font should include every CP437 character, using the Unicode code point defined by the official CP437 mapping.

### Required groups

- ASCII printable characters: `U+0020` through `U+007E`.
- CP437 accented Latin letters and symbols.
- CP437 Greek letters and mathematical symbols.
- CP437 box-drawing characters.
- CP437 shading and block characters.
- CP437 pictographs, card suits, arrows, and geometric symbols.

### Representative CP437 glyphs

```text
☺ ☻ ♥ ♦ ♣ ♠ • ◘ ○ ◙ ♂ ♀ ♪ ♫ ☼ ► ◄ ↕ ‼ ¶ § ▬ ↨ ↑ ↓ → ← ∟ ↔ ▲ ▼

α ß Γ π Σ σ µ τ Φ Θ Ω δ ∞ φ ε ∩ ≡ ± ≥ ≤ ⌠ ⌡ ÷ ≈ ° ∙ · √ ⁿ ² ■

░ ▒ ▓ █ ▄ ▀

─ │ ┌ ┐ └ ┘ ├ ┤ ┬ ┴ ┼
═ ║ ╔ ╗ ╚ ╝ ╠ ╣ ╦ ╩ ╬
```

### Implementation rule

Do not design from a screenshot or informal CP437 table alone. Generate a coverage report from the official mapping and verify that every CP437 byte maps to an existing glyph in the final font.

## Rounded box corners

Use the standard Unicode rounded box-drawing arcs:

| Glyph | Unicode | Name |
|---|---:|---|
| `╭` | U+256D | Box Drawings Light Arc Down and Right |
| `╮` | U+256E | Box Drawings Light Arc Down and Left |
| `╯` | U+256F | Box Drawings Light Arc Up and Left |
| `╰` | U+2570 | Box Drawings Light Arc Up and Right |

Example:

```text
╭──────────────╮
│ Rounded panel │
╰──────────────╯
```

These glyphs must share the same stroke weight, vertical alignment, and cell metrics as `─`, `│`, `┌`, `┐`, `└`, and `┘`. Rounded and square panels should be interchangeable without producing visible seams.

## Extended UI glyph set

Prioritize monochrome, fixed-width glyphs. Avoid relying on color emoji, which may be rendered by a platform fallback font and can break the textmode aesthetic.

### Rounded and geometric UI

```text
▢ □ ■ ▪ ▫ ◻ ◼ ○ ● ◦ ◉ ◯ ◍ ◌
◆ ◇ ◈ ▲ △ ▼ ▽ ◀ ◁ ▶ ▷
```

### Progress and meters

```text
▏ ▎ ▍ ▌ ▋ ▊ ▉ █
░ ▒ ▓ █
▁ ▂ ▃ ▄ ▅ ▆ ▇ █
```

### Arrows

```text
← ↑ → ↓ ↔ ↕
⇐ ⇑ ⇒ ⇓ ⇔ ⇕
⯇ ⯈ ⯅ ⯆
```

### Status and controls

```text
✓ ✔ ✗ ✘
☐ ☑ ☒
⚠ ⚡
★ ☆ ✦ ✧
● ○ ◆ ◇ ■ □
```

### Optional additions

- Braille patterns for dense texture, patterns, and pixel-like art.
- Tessellation and geometric fill glyphs.
- Powerline-style separators, if needed for terminal-style UIs.
- A small private-use-area icon set only when the font and consuming applications are controlled together.

## Monospace programming design

A monospaced font gives every glyph the same advance width. The challenge is to make narrow characters occupy their full cell without becoming visually empty or ambiguous.

### Critical distinctions

| Set | Design approach |
|---|---|
| `1 l I |` | Give `1` a flag and base; give `l` a tail or curve; give `I` clear top and bottom serifs; keep `|` visually distinct. |
| `0 O` | Use a slashed, dotted, or otherwise distinctive zero; keep `O` round and open. |
| `i j` | Ensure dots are clearly separated from stems; give `j` a visible descender. |
| `r n m` | Preserve clear stem separation; avoid excessive joins. |
| `{ } [ ] ( ) < >` | Use strong, balanced apertures and clear pairing. |
| `: ; , .` | Make punctuation placement and weight unmistakable. |
| `` ` ' " `` | Differentiate quote forms clearly. |
| `ilj` | Use distinct dots, tails, and stem treatments. |

### Recommended visual properties

- Generous x-height for small-size readability.
- Slightly condensed letterforms to preserve a textmode feel.
- Consistent stroke weight and rhythm.
- Open apertures in `c`, `e`, `s`, and similar letters.
- Clear punctuation, even at 12–16 px rendering sizes.
- Distinct zero treatment.
- Strong differentiation among `1`, `l`, `I`, and `|`.

### Testing checklist

Test the font in real code and UI contexts at multiple sizes:

```text
0 O 1 l I | i j
rn m cl cg
{} [] () <> ""
-- == != <= >=
-> => :: ;; ,, ..
```

Also test:

- Editor rendering at 12, 14, 16, and 20 px.
- Browser rendering with `font-family: monospace`.
- LÖVE rendering at integer and non-integer scales.
- Box-drawing alignment across adjacent cells.
- Block and shading continuity.
- Rounded-corner joins against straight rules.
- Dark and light background contrast.

## LÖVE integration

LÖVE can load a TrueType font directly:

```lua
function love.load()
  font = love.graphics.newFont("CP437Mono.ttf", 16)
  love.graphics.setFont(font)
end

function love.draw()
  love.graphics.print("╭──────────╮\n│ CP437 UI │\n╰──────────╯", 40, 40)
end
```

`love.graphics.newFont` accepts a TTF or BMFont file, and `love.graphics.setFont` makes the loaded font active for drawing. [LÖVE documentation](https://love2d.org/wiki/love.graphics.newFont)

Use `Font:hasGlyphs` at load time to verify that required symbols exist:

```lua
if not font:hasGlyphs("╭╮╯╰") then
  error("Rounded box corners are missing from the font")
end
```

## Web integration

Ship the font as WOFF2 for web use, with TTF retained for LÖVE and desktop use.

```css
@font-face {
  font-family: "CP437 Mono";
  src: url("/fonts/cp437-mono.woff2") format("woff2"),
       url("/fonts/cp437-mono.ttf") format("truetype");
  font-weight: 400;
  font-style: normal;
  font-display: swap;
}

body,
pre,
code,
.term {
  font-family: "CP437 Mono", ui-monospace, monospace;
}
```

For large fonts, consider subsetting by Unicode range, but retain the complete CP437 and box-drawing coverage in the primary font.

## Production toolchain

| Tool | Use |
|---|---|
| FontForge | Open-source glyph drawing, metrics, compilation, and inspection. |
| Glyphs | Commercial font editor with strong interpolation and variable-font workflows. |
| RoboFont | Scriptable UFO-based editor, useful for automated glyph generation. |
| fontmake / ufo2ft | Compile UFO sources into TTF, OTF, or variable fonts. |
| fonttools | Inspect, subset, modify, and validate fonts; useful for `cmap` and coverage checks. |
| BMFont / Hiero | Generate bitmap font atlases for pixel-perfect LÖVE rendering. |

## Recommended deliverables

1. `CP437Mono-Regular.ttf` — primary LÖVE and desktop font.
2. `CP437Mono-Regular.woff2` — primary web font.
3. `CP437Mono-Regular.woff` — optional legacy web fallback.
4. `CP437Mono-Regular.otf` — optional desktop/printing format.
5. `CP437Mono-Bitmap.fnt` and texture atlas — optional pixel-perfect LÖVE rendering.
6. `glyph-coverage-report.txt` — generated from the official CP437 mapping.
7. `specimen.html` — browser test page for code, boxes, icons, and UI layouts.
8. `love-specimen/` — LÖVE project for rendering and alignment tests.

## Core design principle

Build the font as a Unicode monospace TTF, not as a raw CP437-encoded font. Include all CP437 glyphs at their official Unicode mappings, then extend the repertoire with rounded corners and monochrome UI symbols.

The most important technical requirement is grid alignment: every box-drawing rule, block, shading glyph, and icon must align exactly to the monospace cell. This is what makes panels, meters, borders, and textmode layouts assemble cleanly in both LÖVE and the browser.

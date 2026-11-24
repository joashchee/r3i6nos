"""Hand edits to R3i6nos Mono, kept as a text file the generators apply last (docs/FONT-EDITOR.md).

The font is generated (scripts/build_r3_font.py), so a touch-up cannot be made in the output: it would be undone by the next build. The Font Editor
(apps/fontedit-native) writes the cells a person changed to a text file; `theme/font/r3i6nos-mono/edits.txt` is the committed copy, and build() applies it
after every generator has run, so every output (TTF, BDF, C and Lua tables, PSF) carries the edit and the next build keeps it.

The file is one block a cell, readable and diffable (`.` paper, `#` ink, 20 rows of 10):

    # r3i6nos font edits 1
    font R3i6nos Mono 10x20
    cell 10FD28 icon.app.hydrolik 1/2
    ..........
    ...20 rows...

    cell 10FD29 icon.app.hydrolik 2/2
    ...

`cell <hex code point> <name or -> <part>/<of>`: only the code point is read; the name and the part say what the cell is, and a name that is not the
code point's name in the font is an error (a stale file, not a silent edit of the wrong cell). A code point the font does not have is an error too.
Edits to a code point that other code points alias (U+F000 + byte in CP437, Greek letters that share a Latin shape) show in the aliases as well.

    python3 scripts/merge_font_edits.py FILE      merge the editor's file into edits.txt (a cell equal to the generated one is dropped)
"""
from pathlib import Path

from .canvas import Bmp, CW, CH

ROOT = Path(__file__).resolve().parent.parent.parent
EDITS = ROOT / "theme" / "font" / "r3i6nos-mono" / "edits.txt"
HEADER = "# r3i6nos font edits 1"
FONT_LINE = f"font R3i6nos Mono {CW}x{CH}"


class EditError(ValueError):
    pass


def parse(text):
    """{code point: (name or None, [CH strings of CW chars])} from a file's text."""
    cells, cur, rows, lineno = {}, None, [], 0

    def close():
        nonlocal cur, rows
        if cur is None:
            return
        cp, name, at = cur
        if len(rows) != CH:
            raise EditError(f"line {at}: cell {cp:X} has {len(rows)} rows, expected {CH}")
        if cp in cells:
            raise EditError(f"line {at}: cell {cp:X} twice")
        cells[cp] = (name, rows)
        cur, rows = None, []

    seen_font = False
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.rstrip()
        if cur is not None and (line.startswith("cell ") or not line):
            close()
        if cur is not None and len(rows) < CH and len(line) == CW and not set(line) - set(".#"):
            rows.append(line)               # (a row can begin with '#': it is read before comments are)
            continue
        if not line or line.startswith("#"):
            continue
        if line.startswith("font "):
            if line != FONT_LINE:
                raise EditError(f"line {lineno}: this file is for '{line[5:]}', not '{FONT_LINE[5:]}'")
            seen_font = True
        elif line.startswith("cell "):
            if not seen_font:
                raise EditError(f"line {lineno}: no 'font' line before the first cell")
            parts = line.split()
            try:
                cp = int(parts[1], 16)
            except (IndexError, ValueError):
                raise EditError(f"line {lineno}: bad cell line")
            cur = (cp, parts[2] if len(parts) > 2 and parts[2] != "-" else None, lineno)
        elif cur is not None:
            if len(line) != CW or set(line) - set(".#"):
                raise EditError(f"line {lineno}: a row is {CW} characters of '.' and '#'")
            rows.append(line)
        else:
            raise EditError(f"line {lineno}: unexpected '{line[:20]}'")
    close()
    return cells


def to_bmp(rows):
    b = Bmp(CW, CH)
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            if c == "#":
                b.set(x, y)
    return b


def rows_of(bmp):
    return ["".join("#" if bmp.get(x, y) else "." for x in range(CW)) for y in range(CH)]


def name_of(R):
    """{code point: (name, part, of)} for every cell of a named glyph."""
    out = {}
    for name, cps in R.names.items():
        for i, cp in enumerate(cps):
            out.setdefault(cp, (name, i + 1, len(cps)))
    return out


def render(cells, R=None):
    """The file's text for {code point: rows}, sorted by code point; R (a built Registry) gives each cell its name and part."""
    names = name_of(R) if R is not None else {}
    out = [HEADER, FONT_LINE, ""]
    for cp in sorted(cells):
        rows = cells[cp][1] if isinstance(cells[cp], tuple) else cells[cp]
        n = names.get(cp)
        out.append(f"cell {cp:X} {n[0] if n else '-'} {n[1]}/{n[2]}" if n else f"cell {cp:X} - 1/1")
        out += rows
        out.append("")
    return "\n".join(out) + "\n"


def apply(R, path=None):
    """Put the file's cells into a built Registry; returns the code points changed. A missing file changes nothing."""
    p = Path(path) if path else EDITS
    if not p.exists():
        return []
    cells = parse(p.read_text())
    names = name_of(R)
    for cp, (name, rows) in cells.items():
        if cp not in R.g and cp not in R.alias:
            raise EditError(f"{p.name}: U+{cp:04X} is not in the font")
        if name is not None and (cp not in names or names[cp][0] != name):
            raise EditError(f"{p.name}: U+{cp:04X} is '{names.get(cp, ('no name',))[0]}' in the font, not '{name}' (a stale edits file?)")
        R.alias.pop(cp, None)               # an edit gives the code point its own picture; the cells that alias it follow it
        R.g.pop(cp, None)
        R.g[cp] = to_bmp(rows)
    return sorted(cells)


def merge(R, new_text, old_text=""):
    """The text of edits.txt after merging the editor's file into it: a newer cell wins, and a cell that equals what the generators draw is dropped.
    R must be built WITHOUT edits."""
    merged = {cp: v[1] for cp, v in parse(old_text).items()} if old_text.strip() else {}
    for cp, (name, rows) in parse(new_text).items():
        if cp not in R.g and cp not in R.alias:
            raise EditError(f"U+{cp:04X} is not in the font")
        merged[cp] = rows
    for cp in list(merged):
        if rows_of(R.glyph(cp)) == merged[cp]:
            del merged[cp]
    return render(merged, R)

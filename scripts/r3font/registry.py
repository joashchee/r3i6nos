"""The glyph registry: code point -> bitmap, semantic names, and the name-locked private-use allocation.

Private use lives in Plane 16 (U+10F000-10FFFF), where Nerd Fonts and the Linux console do not, in named blocks. A glyph's name is its
identity: `glyph-names.json` (committed) records name -> first code point, so adding glyphs never moves an existing one. A two-cell icon is
two consecutive code points (left half, right half), always aligned to an even offset in its block.
"""
import json
from pathlib import Path

from .canvas import Bmp, CW, CH

ROOT = Path(__file__).resolve().parent.parent.parent
LOCK = ROOT / "theme" / "font" / "r3i6nos-mono" / "glyph-names.json"

BLOCKS = {                 # block -> (first, last, what)
    "pad1": (0x10F000, 0x10F0FF, "controller: buttons, one cell"),
    "pad2": (0x10F100, 0x10F2FF, "controller: pairs of cells (shoulders, D-pad, sticks, Select, Start)"),
    "osk": (0x10F300, 0x10F5FF, "on-screen keyboard: two-cell key caps (raised and pressed) for every printable key, and the special keys"),
    "ui1": (0x10F600, 0x10F6FF, "modern UI, one cell: widgets, bars, sliders, spinners, badges"),
    "ui2": (0x10F700, 0x10F7FF, "modern UI, two cells"),
    "mus1": (0x10F800, 0x10F9FF, "music: staff cells, notation marks, tracker, piano, meters, waveforms"),
    "mus2": (0x10FA00, 0x10FAFF, "music, two cells"),
    "code": (0x10FB00, 0x10FBFF, "coding: VCS, debugger, diagnostics, folds, tree"),
    "sys": (0x10FC00, 0x10FCFF, "system status, one cell (hot-plug toast, battery, network, sound)"),
    "app": (0x10FD00, 0x10FFFF, "two-cell icons: apps, system, files"),
}


class Registry:
    def __init__(self):
        self.g = {}          # code point -> Bmp
        self.alias = {}      # code point -> code point (same glyph)
        self.names = {}      # name -> [code points]
        self.where = {}      # code point -> section label
        self.sec = "?"
        self.lock = json.loads(LOCK.read_text()) if LOCK.exists() else {}
        self.used = {b: [] for b in BLOCKS}
        for cp in self.lock.values():
            self._note(cp)
            self._note(cp + 1)      # a pair keeps its second cell; a single only wastes one slot
        self.new = []

    def section(self, name):
        self.sec = name

    def _note(self, cp):
        for b, (a, z, _) in BLOCKS.items():
            if a <= cp <= z:
                self.used[b].append(cp)

    def put(self, cp, bmp, name=None):
        if cp in self.g or cp in self.alias:
            raise ValueError(f"U+{cp:04X} drawn twice ({name or ''})")
        assert bmp.w == CW and bmp.h == CH, (hex(cp), bmp.w, bmp.h)
        self.g[cp] = bmp
        self.where[cp] = self.sec
        if name:
            self.names[name] = [cp]
        return cp

    def put_if_free(self, cp, bmp, name=None):
        if cp not in self.g and cp not in self.alias:
            self.put(cp, bmp, name)

    def same(self, cp, of):
        """cp displays the same glyph as `of`."""
        if cp in self.g or cp in self.alias:
            raise ValueError(f"U+{cp:04X} twice")
        self.alias[cp] = of

    def _alloc(self, block, name, n):
        if name in self.names:
            raise ValueError("name twice: " + name)
        cp = self.lock.get(name)
        if cp is None:
            a, z, _ = BLOCKS[block]
            top = max(self.used[block] + [a - 1])
            cp = top + 1
            if n == 2 and (cp - a) % 2:
                cp += 1
            if cp + n - 1 > z:
                raise ValueError(f"block {block} is full")
            self.lock[name] = cp
            self.new.append(name)
            for k in range(n):
                self.used[block].append(cp + k)
        return cp

    def one(self, block, name, bmp):
        cp = self._alloc(block, name, 1)
        self.put(cp, bmp, name)
        return cp

    def two(self, block, name, bmp):
        """A 20x20 Bmp as two cells."""
        cp = self._alloc(block, name, 2)
        l, r = bmp.halves()
        self.put(cp, l)
        self.put(cp + 1, r)
        self.names[name] = [cp, cp + 1]
        return cp

    def save_lock(self):
        # a name that is no longer drawn keeps its code point reserved (retired, never reused)
        LOCK.write_text(json.dumps(dict(sorted(self.lock.items(), key=lambda kv: kv[1])), indent=1) + "\n")

    def all_cps(self):
        return sorted(set(self.g) | set(self.alias))

    def glyph(self, cp):
        return self.g[self.alias.get(cp, cp)]

#!/usr/bin/env python3
"""Build R3i6nosMono-Regular.ttf, .woff and .woff2 from theme/font/r3i6nos-mono/r3i6nos-mono.json (fontTools; run by scripts/build-r3-font.sh in Debian).

Every set pixel is a square of 100 font units on a 2000 unit em, so at 20 px (or 40, 60 ...) each source pixel is exactly one screen pixel
and the grid-fitting rasteriser needs no hints. Rows of identical runs are merged into rectangles to keep the outlines small.
"""
import json
import sys
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

OUT = Path(sys.argv[1])
d = json.loads((OUT / "r3i6nos-mono.json").read_text())
W, H, BASE = d["width"], d["height"], d["base"]
UPM, PX = 2000, 100
ASC, DESC = BASE * PX, (H - BASE) * PX


def rects(hexrow):
    rows = [int(hexrow[i:i + 3], 16) for i in range(0, len(hexrow), 3)]
    runs = []
    for r in rows:
        cur, x = [], 0
        while x < W:
            if r >> (W - 1 - x) & 1:
                s = x
                while x < W and r >> (W - 1 - x) & 1:
                    x += 1
                cur.append((s, x))
            else:
                x += 1
        runs.append(cur)
    out, open_ = [], {}
    for y in range(H + 1):
        now = set(runs[y]) if y < H else set()
        for run in list(open_):
            if run not in now:
                out.append((run, open_.pop(run), y))
        for run in now:
            open_.setdefault(run, y)
    return out


def glyph(hexrow):
    pen = TTGlyphPen(None)
    for (x0, x1), y0, y1 in rects(hexrow):
        top, bot = ASC - y0 * PX, ASC - y1 * PX
        pen.moveTo((x0 * PX, bot)); pen.lineTo((x0 * PX, top)); pen.lineTo((x1 * PX, top)); pen.lineTo((x1 * PX, bot)); pen.closePath()
    return pen.glyph()


cmap = {int(k): v for k, v in d["cmap"].items()}
names = {}
order = [".notdef"]
gname = {}
for i in range(len(d["glyphs"])):
    gname[i] = f"g{i:04d}"
order += [gname[i] for i in range(len(d["glyphs"]))]
glyphs = {".notdef": glyph("".join(f"{r:03x}" for r in ([0x3ff] + [0x201] * 18 + [0x3ff])))}
for i, h in enumerate(d["glyphs"]):
    glyphs[gname[i]] = glyph(h)
fb = FontBuilder(UPM, isTTF=True)
fb.setupGlyphOrder(order)
# a glyph named by its first code point is easier to read in tools
first = {}
for cp, i in sorted(cmap.items()):
    first.setdefault(i, cp)
nm = {".notdef": ".notdef"}
for i in range(len(d["glyphs"])):
    cp = first.get(i)
    nm[gname[i]] = (f"uni{cp:04X}" if cp is not None and cp <= 0xFFFF else f"u{cp:X}") if cp is not None else gname[i]
order2 = [nm[g] for g in order]
glyphs2 = {nm[g]: glyphs[g] for g in order}
fb.setupGlyphOrder(order2)
fb.setupCharacterMap({cp: nm[gname[i]] for cp, i in cmap.items()})
fb.setupGlyf(glyphs2)
# hmtx's left side bearing must equal each glyph's xMin: FreeType moves the outline so that it does, which would trim the blank columns
lsb = {}
for i, h in enumerate(d["glyphs"]):
    rs = rects(h)
    lsb[nm[gname[i]]] = min((x0 for (x0, x1), y0, y1 in rs), default=0) * PX
lsb[".notdef"] = 0
fb.setupHorizontalMetrics({g: (W * PX, lsb[g]) for g in order2})
fb.setupHorizontalHeader(ascent=ASC, descent=-DESC, lineGap=0)
fb.setupNameTable({
    "familyName": d["family"], "styleName": "Regular", "uniqueFontIdentifier": "R3i6nos Mono 1.0", "fullName": "R3i6nos Mono Regular",
    "psName": "R3i6nosMono-Regular", "version": "Version 1.000",
    "copyright": "CP437 letters (c) 2016-2020 VileR (WebPlus IBM VGA 8x16, CC BY-SA 4.0), mapped to 10x20; other glyphs by r3i6nos, 2026.",
    "licenseDescription": "Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0). See README.txt.",
    "licenseInfoURL": "https://creativecommons.org/licenses/by-sa/4.0/", "description": "10x20 pixel monospace font for r3i6nos and the ansiapps apps.",
})
from fontTools.ttLib.tables.O_S_2f_2 import Panose  # noqa: E402
pan = Panose()
pan.bFamilyType, pan.bSerifStyle, pan.bWeight, pan.bProportion = 2, 0, 5, 9
pan.bContrast = pan.bStrokeVariation = pan.bArmStyle = pan.bLetterForm = pan.bMidline = pan.bXHeight = 0
fb.setupOS2(version=4, sTypoAscender=ASC, sTypoDescender=-DESC, sTypoLineGap=0, usWinAscent=ASC, usWinDescent=DESC, xAvgCharWidth=W * PX,
            sxHeight=8 * PX, sCapHeight=12 * PX, fsSelection=0x40 | 0x80, usWeightClass=400, usWidthClass=5,
            panose=pan, ulUnicodeRange1=0xFFFFFFFF, ulUnicodeRange2=0xFFFFFFFF, ulUnicodeRange3=0xFFFFFFFF)
fb.setupPost(isFixedPitch=1, underlinePosition=-100, underlineThickness=100)
fb.font["head"].flags |= 0x8  # ppem to integer
fb.font["head"].lowestRecPPEM = 20
from fontTools.ttLib import newTable  # noqa: E402
gasp = newTable("gasp")
gasp.version = 1
gasp.gaspRange = {0xFFFF: 0x0001}   # grid-fit, no grey scale at any size
fb.font["gasp"] = gasp
# FreeType auto-hints a TrueType font that has no instructions at all, which moves pixel-aligned outlines (grey rows, dropped rows). A
# trivial control program (dropout control and scan type, nothing that moves a point) makes it use the font's own, unhinted, outlines.
from fontTools.ttLib.tables import ttProgram  # noqa: E402
prep = newTable("prep")
prep.program = ttProgram.Program()
prep.program.fromBytecode(bytes([0xB8, 0x01, 0xFF, 0x85, 0xB0, 0x04, 0x8D]))   # PUSHW 511, SCANCTRL, PUSHB 4, SCANTYPE
fb.font["prep"] = prep
fb.save(OUT / "R3i6nosMono-Regular.ttf")
from fontTools.ttLib import TTFont  # noqa: E402
for flavor, ext in (("woff", "woff"), ("woff2", "woff2")):
    f = TTFont(OUT / "R3i6nosMono-Regular.ttf")
    f.flavor = flavor
    f.save(OUT / f"R3i6nosMono-Regular.{ext}")
print("ttf: %d glyphs, %d code points" % (len(order2), len(cmap)))

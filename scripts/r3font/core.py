"""Standard Unicode coverage: CP437, generated box/blocks/Braille, symbols. Built first; private-use sections come after."""
from . import base, boxes, symbols


def build(R):
    R.section("CP437")
    g, uni = base.cp437()
    for i in range(256):
        cp = uni[i]
        if cp in R.g:                       # 0x00 and 0x20 and 0xFF map to blanks that repeat: keep the first
            continue
        R.put(cp, g[i])
    R.put_if_free(0x00A0, g[32])
    R.section("raw CP437 (U+F000 + byte, as the Linux console and the screen mirror address it)")
    for i in range(256):
        R.same(0xF000 + i, uni[i] if i else 0x20)
    R.section("box drawing")
    for cp, (nm, b) in boxes.box_glyphs().items():
        R.put_if_free(cp, b)
    R.section("blocks, quadrants, sextants, eighths")
    for cp, b in boxes.blocks().items():
        R.put_if_free(cp, b)
    for cp, b in boxes.sextants().items():
        R.put(cp, b)
    R.section("braille")
    for cp, b in boxes.braille().items():
        R.put(cp, b)
    symbols.build(R)

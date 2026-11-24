"""The 256 code page 437 glyphs, exactly as the theme draws them (theme/vga8x16.lua scaled 8x16 -> 10x20, nearest neighbour).

CC BY-SA 4.0 (VileR's "WebPlus IBM VGA 8x16"): see theme/font/README.txt. Every glyph here is that font's, mapped as
docs/R3I6NOS-ANSIAPPS-THEME.md says: destination column x takes source column floor(x*8/10), row y takes floor(y*16/20).
"""
import re
from pathlib import Path

from .canvas import Bmp, CW, CH

ROOT = Path(__file__).resolve().parent.parent.parent


def load_vga():
    src = (ROOT / "theme" / "vga8x16.lua").read_text()
    glyphs = re.findall(r'^\s*"([0-9a-f]{32})",\s*$', src, re.M)
    uni = [int(n) for n in re.search(r"unicode = \{(.*?)\}", src, re.S).group(1).replace("\n", " ").split(",") if n.strip()]
    assert len(glyphs) == 256 and len(uni) == 256
    return glyphs, uni


def scale(hexrows):
    rows = bytes.fromhex(hexrows)
    b = Bmp(CW, CH)
    for y in range(CH):
        r = rows[y * 16 // CH]
        for x in range(CW):
            b.p[y][x] = (r >> (7 - x * 8 // CW)) & 1
    return b


_cache = {}


def cp437():
    """(list of 256 Bmp, list of 256 Unicode code points)."""
    if not _cache:
        g, u = load_vga()
        _cache["v"] = ([scale(h) for h in g], u)
    return _cache["v"]


def by_unicode():
    g, u = cp437()
    return {cp: g[i] for i, cp in enumerate(u)}

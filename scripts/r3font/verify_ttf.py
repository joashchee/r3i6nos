#!/usr/bin/env python3
"""Check the TrueType and WOFF2 files against the source bitmaps. Run by scripts/build-r3-font.sh in Debian; exits 1 on any difference.

1. Every code point is rendered at 20 px by FreeType (through Pillow, grey scale: pixel-aligned outlines give only 0 and 255, never grey) and its ink, cropped to its bounding box, must equal the
   source bitmap's and be at the source's column and row.
2. The glyph's bounds in the font (xMin, yMax, advance) must equal the source ink's left column and top row, and the advance 10 px.
3. The code points of the two files are the same.
argv[1] = the font folder."""
import json
import sys
from pathlib import Path

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

OUT = Path(sys.argv[1])
d = json.loads((OUT / "r3i6nos-mono.json").read_text())
W, H, BASE = d["width"], d["height"], d["base"]


def src_rows(gi):
    return [int(d["glyphs"][gi][i:i + 3], 16) for i in range(0, 3 * H, 3)]


def crop(rows):
    pts = [(x, y) for y, r in enumerate(rows) for x in range(W) if r >> (W - 1 - x) & 1]
    if not pts:
        return None
    x0, y0 = min(p[0] for p in pts), min(p[1] for p in pts)
    x1, y1 = max(p[0] for p in pts), max(p[1] for p in pts)
    return (x0, y0), tuple(tuple(1 if (rows[y] >> (W - 1 - x) & 1) else 0 for x in range(x0, x1 + 1)) for y in range(y0, y1 + 1))


bad = []
cm = {int(k): v for k, v in d["cmap"].items()}
for path in ("R3i6nosMono-Regular.ttf", "R3i6nosMono-Regular.woff2"):
    tt = TTFont(OUT / path)
    font = ImageFont.truetype(str(OUT / path), 20, layout_engine=ImageFont.Layout.BASIC)
    best = tt.getBestCmap()
    if set(best) != set(cm):
        bad.append((path, "cmap differs"))
    glyf, hmtx = tt["glyf"], tt["hmtx"]
    n = 0
    for cp, gi in cm.items():
        rows = src_rows(gi)
        want = crop(rows)
        gn = best[cp]
        g = glyf[gn]
        if hmtx[gn][0] != W * 100:
            bad.append((path, hex(cp), "advance"))
        if want is None:
            if g.numberOfContours != 0:
                bad.append((path, hex(cp), "should be empty"))
            continue
        (x0, y0), _ = want
        if not hasattr(g, "xMin") or g.xMin != x0 * 100 or g.yMax != (BASE - y0) * 100:
            bad.append((path, hex(cp), "bounds"))
        if hmtx[gn][1] != g.xMin:
            bad.append((path, hex(cp), "left side bearing differs from xMin"))
        im = Image.new("L", (W + 8, H + 8), 0)
        ImageDraw.Draw(im).text((4, 4 + BASE), chr(cp), font=font, fill=255, anchor="ls")
        got = []
        for y in range(H + 8):
            v = 0
            for x in range(W + 8):
                p = im.getpixel((x, y))
                if 0 < p < 255:
                    bad.append((path, hex(cp), "grey pixel"))
                v = v << 1 | (1 if p >= 128 else 0)
            got.append(v)
        pts = [(x, y) for y, r in enumerate(got) for x in range(W + 8) if r >> (W + 7 - x) & 1]
        gx0, gy0 = min(p[0] for p in pts), min(p[1] for p in pts)
        gx1, gy1 = max(p[0] for p in pts), max(p[1] for p in pts)
        shape = tuple(tuple(1 if (got[y] >> (W + 7 - x) & 1) else 0 for x in range(gx0, gx1 + 1)) for y in range(gy0, gy1 + 1))
        if shape != want[1] or gy0 - 4 != y0 or gx0 - 4 != x0:
            bad.append((path, hex(cp), "pixels or position"))
        n += 1
    print(f"{path}: {len(cm)} code points checked ({n} with ink)")
if bad:
    print("verify_ttf: %d problems, first: %s" % (len(bad), bad[:8]))
    sys.exit(1)
print("verify_ttf: every glyph of the TTF and the WOFF2 renders as its source bitmap, at its source position")

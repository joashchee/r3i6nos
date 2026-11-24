"""Generated cell geometry: box drawing (U+2500-257F), block elements, quadrants, sextants, eighths and Braille.

The light stroke is the theme's own (measured from the VGA glyphs mapped to 10x20: vertical x 4-6, horizontal y 9), so
every generated glyph joins the CP437 ones. The double and mixed light/double lines (U+2550-256C) are CP437's own and
are not generated. `test_r3font.py` checks the generator reproduces the VGA light lines exactly.
"""
import unicodedata

from .canvas import Bmp, CW, CH

# stroke extents (inclusive) per weight
VX = {"l": (4, 6), "h": (3, 7), "t": (4, 5)}     # vertical strokes: x range; t = the 2 px "thin" set (centred)
HY = {"l": (9, 9), "h": (8, 10), "t": (9, 10)}   # horizontal strokes: y range


def arms(l=None, r=None, u=None, d=None):
    """Light/heavy/thin arms joined at the centre. Each arm is None or 'l', 'h', 't'."""
    b = Bmp()
    hs = [HY[w] for w in (l, r) if w]
    vs = [VX[w] for w in (u, d) if w]
    if u:
        b.rect(VX[u][0], 0, VX[u][1] - VX[u][0] + 1, (max(y[1] for y in hs) if hs else HY[u][1]) + 1)
    if d:
        b.rect(VX[d][0], (min(y[0] for y in hs) if hs else HY[d][0]), VX[d][1] - VX[d][0] + 1, CH)
        b.rect(VX[d][0], 0, 0, 0)
    if l:
        b.rect(0, HY[l][0], (max(x[1] for x in vs) if vs else VX[l][1]) + 1, HY[l][1] - HY[l][0] + 1)
    if r:
        x0 = min(x[0] for x in vs) if vs else VX[r][0]
        b.rect(x0, HY[r][0], CW - x0, HY[r][1] - HY[r][0] + 1)
    if d and not hs:
        b.rect(VX[d][0], HY[d][0], VX[d][1] - VX[d][0] + 1, CH - HY[d][0])
    return b


def dashes(n, length, gapfrac=0.4):
    """n dashes on a line of `length` px with half a gap at each end of every cell, as pixel index ranges."""
    out = []
    per = length / n
    gap = per * gapfrac
    for k in range(n):
        a = int(k * per + gap / 2 + 0.5)
        z = int((k + 1) * per - gap / 2 + 0.5)
        out.append((a, max(a, z - 1)))
    return out


def dash(weight, n, horizontal):
    b = Bmp()
    if horizontal:
        y0, y1 = HY[weight]
        for a, z in dashes(n, CW):
            b.rect(a, y0, z - a + 1, y1 - y0 + 1)
    else:
        x0, x1 = VX[weight]
        for a, z in dashes(n, CH):
            b.rect(x0, a, x1 - x0 + 1, z - a + 1)
    return b


def arc(name, weight="l"):
    """The down-and-right rounded corner: an elliptical path from the vertical stroke to the horizontal one.

    The theme's light strokes are 3 px wide upright and 1 px tall flat, so the brush follows the tangent: as wide as the vertical
    stroke where the path runs up and down, as thin as the horizontal one where it runs across. The path reaches the cell's right
    edge on the horizontal stroke's centre line, so it joins the next cell's line."""
    import math
    x0, x1 = VX[weight]
    y0, y1 = HY[weight]
    wv, th = x1 - x0 + 1, y1 - y0 + 1                      # vertical stroke width, horizontal stroke thickness
    cx0, cy0 = (x0 + x1 + 1) / 2, (y0 + y1 + 1) / 2
    rx, ry = CW - cx0, 5.0
    ccx, ccy = cx0 + rx, cy0 + ry
    b = Bmp()
    steps = 200
    for i in range(steps + 1):
        t = math.pi / 2 * i / steps
        px, py = ccx - rx * math.cos(t), ccy - ry * math.sin(t)
        dx, dy = rx * math.sin(t), ry * math.cos(t)          # tangent
        vert = abs(dy) / (abs(dx) + abs(dy))
        w = int(round(th + (wv - th) * vert))
        h = int(round(th + (th - th) * vert))
        b.rect(int(round(px - w / 2.0)), int(round(py - h / 2.0 - 0.0)), w, h)
    b.rect(x0, int(ccy), wv, CH - int(ccy))                  # the straight part below
    return b


def diag(kind):
    b = Bmp()
    if kind in ("up", "x"):
        b.line(CW - 1, 0, 0, CH - 1, 1)
    if kind in ("down", "x"):
        b.line(0, 0, CW - 1, CH - 1, 1)
    # a 2 px wide stroke: draw a second pixel beside every pixel on the row
    out = b.copy()
    for y in range(CH):
        for x in range(CW - 1):
            if b.p[y][x]:
                out.p[y][x + 1] = 1
    return out


def box_glyphs():
    """{codepoint: (name, Bmp)} for the generated box drawing: everything but the light/double set CP437 has."""
    out = {}
    for cp in range(0x2500, 0x2580):
        nm = unicodedata.name(chr(cp))[len("BOX DRAWINGS "):]
        if "DOUBLE" in nm and "DASH" not in nm or "SINGLE" in nm:
            continue
        w = "h" if nm.startswith("HEAVY") else "l"
        if "DASH" in nm:
            n = 2 if "DOUBLE" in nm else 3 if "TRIPLE" in nm else 4
            out[cp] = (nm.lower(), dash(w, n, "HORIZONTAL" in nm))
        elif "ARC" in nm:
            g = arc(nm)
            if "UP AND LEFT" in nm:
                g = g.flip_h().flip_v()
            elif "UP AND RIGHT" in nm:
                g = g.flip_v()
            elif "DOWN AND LEFT" in nm:
                g = g.flip_h()
            out[cp] = (nm.lower(), g)
        elif "DIAGONAL" in nm:
            out[cp] = (nm.lower(), diag("x" if "CROSS" in nm else "up" if "UPPER RIGHT" in nm else "down"))
        else:
            out[cp] = (nm.lower(), arms(**parse_arms(nm)))
    return out


def parse_arms(nm):
    parts = nm.split(" AND ")
    lead = nm.split()[0] if nm.split()[0] in ("LIGHT", "HEAVY") else None
    own = all(("LIGHT" in p or "HEAVY" in p) for p in parts)
    res = {}
    for p in parts:
        words = p.split()
        w = "h" if "HEAVY" in words else "l" if "LIGHT" in words else None
        if w is None or (lead and not own):
            w = "h" if lead == "HEAVY" else "l"
        for k in words:
            if k == "UP":
                res["u"] = w
            elif k == "DOWN":
                res["d"] = w
            elif k == "LEFT":
                res["l"] = w
            elif k == "RIGHT":
                res["r"] = w
            elif k == "VERTICAL":
                res["u"] = res["d"] = w
            elif k == "HORIZONTAL":
                res["l"] = res["r"] = w
    return res


def thin_box():
    """The 2 px hairline set (centred strokes, not the VGA light): name -> Bmp."""
    S = {"light-h": dict(l="t", r="t"), "v": dict(u="t", d="t"), "tl": dict(r="t", d="t"), "tr": dict(l="t", d="t"),
         "bl": dict(r="t", u="t"), "br": dict(l="t", u="t"), "tee-l": dict(u="t", d="t", r="t"), "tee-r": dict(u="t", d="t", l="t"),
         "tee-t": dict(l="t", r="t", d="t"), "tee-b": dict(l="t", r="t", u="t"), "cross": dict(l="t", r="t", u="t", d="t")}
    S["h"] = S.pop("light-h")
    return {k: arms(**v) for k, v in S.items()}


# --- block elements -------------------------------------------------------------------------------------------

# Row boundaries of the eighths from the top. The theme's half blocks are 9 rows up and 11 down (VGA's own rows 0-7 and 8-15 mapped
# 16 -> 20), so the family follows them: an upper k/8 is U[k] rows, a lower k/8 is 20 - U[8 - k].
U = [0, 2, 4, 7, 9, 12, 15, 17, 20]


def vb(k):
    return U[k]


def hb(k):
    """Column boundaries of the eighths (the left and right halves are 5 columns each, symmetric)."""
    f = lambda i: int(i * CW / 8 + 0.5)
    return f(k) if k <= 4 else CW - f(8 - k)


def rect_frac(x0, x1, y0, y1):
    """A block from eighths: columns x0..x1, rows y0..y1 (in eighths)."""
    b = Bmp()
    b.rect(hb(x0), vb(y0), hb(x1) - hb(x0), vb(y1) - vb(y0))
    return b


def blocks():
    out = {}
    for k in range(1, 8):
        out[0x2580 + k] = rect_frac(0, 8, 8 - k, 8)          # lower k/8
        out[0x2590 - k] = rect_frac(0, k, 0, 8)              # left k/8 (2589 = 7/8 ... 258F = 1/8)
    out[0x2580] = rect_frac(0, 8, 0, 4)
    out[0x2588] = rect_frac(0, 8, 0, 8)
    out[0x2590] = rect_frac(4, 8, 0, 8)
    out[0x2594] = rect_frac(0, 8, 0, 1)
    out[0x2595] = rect_frac(7, 8, 0, 8)
    quad = {0x2596: "bl", 0x2597: "br", 0x2598: "tl", 0x259D: "tr"}
    for cp, q in quad.items():
        out[cp] = quadrants(q)
    out[0x2599] = quadrants("tl bl br")
    out[0x259A] = quadrants("tl br")
    out[0x259B] = quadrants("tl tr bl")
    out[0x259C] = quadrants("tl tr br")
    out[0x259E] = quadrants("tr bl")
    out[0x259F] = quadrants("tr bl br")
    # legacy computing: vertical and horizontal eighths 2..7, corners, quarters
    for i, k in enumerate(range(1, 7)):
        out[0x1FB70 + i] = rect_frac(k + 0, k + 1, 0, 8)     # vertical one eighth block-2 .. -7 (column k+1 of 8 -> index 1..6)
        out[0x1FB76 + i] = rect_frac(0, 8, k, k + 1)
    out[0x1FB7C] = rect_frac(0, 1, 0, 8).blit(rect_frac(0, 8, 7, 8))
    out[0x1FB7D] = rect_frac(0, 1, 0, 8).blit(rect_frac(0, 8, 0, 1))
    out[0x1FB7E] = rect_frac(7, 8, 0, 8).blit(rect_frac(0, 8, 0, 1))
    out[0x1FB7F] = rect_frac(7, 8, 0, 8).blit(rect_frac(0, 8, 7, 8))
    out[0x1FB80] = rect_frac(0, 8, 0, 1).blit(rect_frac(0, 8, 7, 8))
    out[0x1FB81] = Bmp().blit(rect_frac(0, 8, 0, 1)).blit(rect_frac(0, 8, 2, 3)).blit(rect_frac(0, 8, 4, 5)).blit(rect_frac(0, 8, 7, 8))
    for i, k in enumerate((2, 3, 5, 6, 7)):
        out[0x1FB82 + i] = rect_frac(0, 8, 0, k)             # upper 1/4, 3/8, 5/8, 3/4, 7/8
        out[0x1FB87 + i] = rect_frac(8 - k, 8, 0, 8)         # right 1/4, 3/8, 5/8, 3/4, 7/8
    return out


def quadrants(spec):
    b = Bmp()
    for q in spec.split():
        x = 0 if q[1] == "l" else CW // 2
        y, h = (0, vb(4)) if q[0] == "t" else (vb(4), CH - vb(4))
        b.rect(x, y, CW // 2, h)
    return b


def sextants():
    """U+1FB00-1FB3B: 2x3 blocks, rows 7/6/7. Bit 0 top left, 1 top right, 2 mid left, 3 mid right, 4 bottom left, 5 bottom right."""
    out = {}
    rows = [(0, 7), (7, 13), (13, 20)]
    cp = 0x1FB00
    for n in range(1, 63):
        if n in (21, 42):
            continue
        b = Bmp()
        for bit in range(6):
            if n >> bit & 1:
                r = rows[bit // 2]
                b.rect(0 if bit % 2 == 0 else CW // 2, r[0], CW // 2, r[1] - r[0])
        out[cp] = b
        cp += 1
    return out


def braille():
    """U+2800-28FF: dots 2x2 px, pitch 5 across and 5 down, so the gaps are 3 px and the same across cells."""
    xs = (1, 6)
    ys = (1, 6, 11, 16)
    # dot n (1-8) -> (col, row): 1 (0,0) 2 (0,1) 3 (0,2) 4 (1,0) 5 (1,1) 6 (1,2) 7 (0,3) 8 (1,3)
    pos = [(0, 0), (0, 1), (0, 2), (1, 0), (1, 1), (1, 2), (0, 3), (1, 3)]
    out = {}
    for n in range(256):
        b = Bmp()
        for bit in range(8):
            if n >> bit & 1:
                c, r = pos[bit]
                b.rect(xs[c], ys[r], 2, 2)
        out[0x2800 + n] = b
    return out

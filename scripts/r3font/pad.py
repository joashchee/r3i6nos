"""Gamepad and on-screen keyboard glyphs (private use). Every control has explicit off / on / disabled glyphs, and analog ones
(triggers, sticks) have levels, so a mono or fixed-colour screen shows the state without relying on colour (docs/r3i6nos-mapping.md).

Names: pad.btn.<label>.<off|on|dis>, pad.shoulder.<l1|r1|...>.<off|on>, pad.trig.<l|r>.<0-8>, pad.dpad.<neutral|up|...>,
pad.stick.<x>.<y> (x, y in -2..2), pad.stick.press, pad.pill.<sel|sta|fn|...>.<off|on>, key.<hex>.<up|dn> and key.<name>.<up|dn>.
"""
from .base import by_unicode
from .canvas import Bmp, CW, CH, micro, icon

SHAPES = {   # 5x5 face symbols (PlayStation style)
    "tri": ["..#..", ".#.#.", ".#.#.", "#...#", "#####"],
    "cir": [".###.", "#...#", "#...#", "#...#", ".###."],
    "cro": ["#...#", ".#.#.", "..#..", ".#.#.", "#...#"],
    "squ": ["#####", "#...#", "#...#", "#...#", "#####"],
}


def label_bmp(lab):
    if lab in SHAPES:
        return Bmp(5, 5).art(0, 0, SHAPES[lab])
    return micro(lab)


def face(lab, state):
    """A round button in one cell, d = 10, with a label."""
    b = Bmp()
    m = label_bmp(lab) if lab else None
    if state == "off":
        b.ring(5, 9.5, 5.0, 1)
        if m:
            b.blit(m, 5 - m.w // 2 if m.w == 5 else 3, 7)
    elif state == "on":
        b.disc(5, 9.5, 5.0)
        if m:
            b.blit(m, 5 - m.w // 2 if m.w == 5 else 3, 7, op="clear")
    else:                                       # disabled: a dotted ring, a dotted label
        r = Bmp().ring(5, 9.5, 5.0, 1)
        for y in range(CH):
            for x in range(CW):
                if r.p[y][x] and (x + y) % 2 == 0:
                    b.p[y][x] = 1
        if m:
            d = Bmp()
            d.blit(m, 5 - m.w // 2 if m.w == 5 else 3, 7)
            for y in range(CH):
                for x in range(CW):
                    if d.p[y][x] and (x + y) % 2 == 0:
                        b.p[y][x] = 1
    return b


def rrect(w, h, r, x=0, y=0, W=None, H=CH):
    """A filled rounded rectangle on a W x H canvas."""
    b = Bmp(W or (x + w), H)
    for yy in range(H):
        for xx in range(b.w):
            px, py = xx + 0.5 - x, yy + 0.5 - y
            if not (0 <= px <= w and 0 <= py <= h):
                continue
            dx = max(r - px, 0, px - (w - r))
            dy = max(r - py, 0, py - (h - r))
            if dx * dx + dy * dy <= r * r + 0.01:
                b.p[yy][xx] = 1
    return b


def outline(b):
    out = b.copy()
    for y in range(b.h):
        for x in range(b.w):
            if b.p[y][x] and all(b.get(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                out.p[y][x] = 0
    return out


def dotted(b):
    out = Bmp(b.w, b.h)
    for y in range(b.h):
        for x in range(b.w):
            if b.p[y][x] and (x + y) % 2 == 0:
                out.p[y][x] = 1
    return out


def labelled_pill(lab, state, w=20, h=10, y=5, r=None):
    body = rrect(w - 1, h, r if r is not None else h / 2.0, 0, y, w)
    m = micro(lab) if lab else None
    x = (w - m.w) // 2 if m else 0
    if state == "off":
        b = outline(body)
        if m:
            b.blit(m, x, y + (h - 5) // 2)
    elif state == "on":
        b = body
        if m:
            b.blit(m, x, y + (h - 5) // 2, op="clear")
    else:
        b = dotted(outline(body))
        if m:
            t = Bmp(w, CH).blit(m, x, y + (h - 5) // 2)
            b.blit(dotted(t))
    return b


def trigger(side, level):
    """An analog trigger: a 20x20 tall body filling from the bottom in 8 steps; level 0..8."""
    w, top, bot = 19, 2, 17
    body = rrect(w, bot - top, 4, 0, top, 20)
    b = outline(body)
    if level:
        inner_top = top + 2
        inner_bot = bot - 2
        hgt = inner_bot - inner_top
        fill_rows = int(round(hgt * level / 8.0))
        for y in range(inner_bot - fill_rows, inner_bot):
            for x in range(2, w - 1):
                b.set(x, y)
    lab = micro("L" if side == "l" else "R")
    b.blit(lab, 8 if side == "l" else 9, 6, op="xor")
    return b


def dpad(dirs):
    """The cross, 20x20: an outline, with the pressed arms filled. dirs is a set of u, d, l, r."""
    arm = 6
    c0 = (20 - arm) // 2
    parts = {"u": (c0, 0, arm, 7), "d": (c0, 13, arm, 7), "l": (0, c0, 7, arm), "r": (13, c0, 7, arm)}
    cross = Bmp(20, 20).rect(c0, 0, arm, 20).rect(0, c0, 20, arm)
    b = outline(cross)
    for k in dirs:
        x, y, w, h = parts[k]
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                if cross.p[yy][xx]:
                    b.p[yy][xx] = 1
    if not dirs:
        b.rect(8, 8, 4, 4)
        b.rect(9, 9, 2, 2, 0)
    return b


def stick(dx, dy, pressed=False):
    """A stick, 20x20: the ring, and the cap's centre moved by (dx, dy) in -2..2 (up to 4 px)."""
    b = Bmp(20, 20).ring(10, 10, 9.5, 1)
    cx, cy = 10 + dx * 2.0, 10 + dy * 2.0
    if pressed:
        b.disc(cx, cy, 5.0)
    else:
        b.ring(cx, cy, 4.5, 1).disc(cx, cy, 1.4)
    if dx or dy:
        b.rect(int(cx), int(cy), 0, 0)
    return b


def sel(label, state):
    return labelled_pill(label, state, 20, 10, 5)


def build(R):
    R.section("gamepad")
    P1, P2 = "pad1", "pad2"
    for lab in ["A", "B", "C", "X", "Y", "Z", "1", "2", "3", "4", "5", "6", "7", "8", "tri", "cir", "cro", "squ", ""]:
        nm = lab or "blank"
        for st in ("off", "on", "dis"):
            R.one(P1, f"pad.btn.{nm}.{st}", face(lab, st))
    for sym, art in (("home", [".#.", "###", "#.#"]), ("menu", ["###", "...", "###"]), ("view", ["##.", "#.#", ".##"]),
                     ("plus", [".#.", "###", ".#."]), ("minus", ["...", "###", "..."]), ("share", ["#.#", ".#.", "#.#"]),
                     ("power", ["###", "#.#", "#.#"]), ("volp", ["#.#", "...", "..."])):
        for st in ("off", "on", "dis"):
            b = face("", st)
            m = Bmp(3, 3).art(0, 0, art)
            if st == "on":
                b.blit(m, 4, 8, op="clear")
            elif st == "off":
                b.blit(m, 4, 8)
            else:
                b.blit(dotted(Bmp().blit(m, 4, 8)))
            R.one(P1, f"pad.btn.{sym}.{st}", b)
    for lab in ["L1", "R1", "L2", "R2", "LB", "RB", "LT", "RT", "ZL", "ZR", "L3", "R3", "L", "R"]:
        for st in ("off", "on", "dis"):
            R.two(P2, f"pad.shoulder.{lab.lower()}.{st}", labelled_pill(lab, st, 20, 10, 5))
    for lab, nm in (("SEL", "sel"), ("STA", "sta"), ("FN", "fn"), ("MNU", "menu"), ("MOD", "mode"), ("HOM", "home"), ("PWR", "pwr"),
                    ("VOL", "vol"), ("OK", "ok"), ("ESC", "esc")):
        for st in ("off", "on", "dis"):
            R.two(P2, f"pad.pill.{nm}.{st}", labelled_pill(lab, st, 20, 10, 5))
    for side in ("l", "r"):
        for lv in range(9):
            R.two(P2, f"pad.trig.{side}.{lv}", trigger(side, lv))
    pairs = {"neutral": "", "up": "u", "down": "d", "left": "l", "right": "r", "upleft": "ul", "upright": "ur", "downleft": "dl", "downright": "dr"}
    for nm, ds in pairs.items():
        R.two(P2, f"pad.dpad.{nm}", dpad(set(ds)))
    for ly, y in enumerate(range(-2, 3)):
        for lx, x in enumerate(range(-2, 3)):
            R.two(P2, f"pad.stick.{x}.{y}", stick(x, y))
    R.two(P2, "pad.stick.press", stick(0, 0, True))
    R.section("on-screen keyboard")
    vga = by_unicode()
    caps = {}
    for cp in range(0x21, 0x7F):
        caps[f"u{cp:04x}"] = vga[cp]
    for nm, cp in (("up", 0x2191), ("down", 0x2193), ("left", 0x2190), ("right", 0x2192)):
        caps[nm] = vga[cp]
    for nm, m in (("esc", "ESC"), ("tab", "TAB"), ("ctrl", "CTL"), ("alt", "ALT"), ("fn", "FN"), ("del", "DEL"), ("ins", "INS"),
                  ("home", "HOM"), ("end", "END"), ("pgup", "PGU"), ("pgdn", "PGD"), ("caps", "CAP"), ("abc", "ABC"), ("sym", "#+="),
                  ("ok", "OK"), ("cancel", "NO"), ("shift", "SHF"), ("enter", "ENT"), ("bksp", "BS"), ("space", "SPC"), ("sup", "SUP"),
                  ("f1", "F1"), ("f2", "F2"), ("f3", "F3"), ("f4", "F4"), ("f5", "F5"), ("f6", "F6"), ("f7", "F7"), ("f8", "F8"),
                  ("f9", "F9"), ("f10", "F10"), ("f11", "F11"), ("f12", "F12"), ("show", "SHW"), ("hide", "HID")):
        mb = micro(m)
        caps[nm] = Bmp(20, CH).blit(mb, 10 - mb.w // 2, 7)
    for nm, g in caps.items():
        for st in ("up", "dn"):
            R.two("osk", f"key.{nm}.{st}", keycap(g, st))


def keycap(g, state):
    """A 20x20 key: a rounded cap the cell's full height (so descenders stay inside), the glyph centred (a one-cell glyph, or a 20 wide
    label); raised = outline with a thick lower lip, pressed = filled with the glyph knocked out."""
    body = rrect(19, 20, 4, 0, 0, 20)
    if state == "up":
        b = outline(body)
        b.rect(3, 18, 14, 1)
        b.blit(g, 5 if g.w != 20 else 0, -1)
    else:
        b = body.copy()
        b.blit(g, 5 if g.w != 20 else 0, -1, op="clear")
    return b

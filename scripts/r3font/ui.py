"""Modern UI elements that combinations of CP437 and box glyphs cannot draw: pill and bar caps, sub-cell sliders, spinners, progress rings,
scrollbar thumbs, hairline frames, switches. Private use. Names: ui.<thing>.<part/state>."""
import math

from . import boxes
from .canvas import Bmp, CW, CH
from .pad import rrect, outline, dotted


def pill3(h, y, filled):
    """A pill of three cells (left cap, middle, right cap) of height h with its top at row y."""
    body = rrect(3 * CW - 1, h, h / 2.0, 0, y, 3 * CW)
    b = body if filled else outline(body)
    return [b.sub(i * CW, 0, CW, CH) for i in range(3)]


def bar_cells():
    """Progress bar parts: track height 10 (rows 5..14). Left/right caps filled or empty, middles with 0..10 px filled."""
    out = {}
    track = rrect(3 * CW - 1, 10, 5, 0, 5, 3 * CW)
    shell = outline(track)
    inner = rrect(3 * CW - 5, 6, 3, 2, 7, 3 * CW)
    empty = shell
    full = shell.copy().blit(inner)
    for nm, b in (("empty", empty), ("full", full)):
        out[f"bar.l.{nm}"] = b.sub(0, 0, CW, CH)
        out[f"bar.r.{nm}"] = b.sub(2 * CW, 0, CW, CH)
    for k in range(CW + 1):
        m = Bmp()
        m.rect(0, 5, CW, 1).rect(0, 14, CW, 1)
        m.rect(0, 7, k, 6)
        out[f"bar.m.{k}"] = m
    return out


def spinner(frame):
    """A ring, d = 10, with a thick comet 90 degrees long that advances one eighth of a turn a frame."""
    b = Bmp().ring(5, 9.5, 5.0, 1)
    start = frame * 45
    for y in range(CH):
        for x in range(CW):
            px, py = x + 0.5 - 5, y + 0.5 - 9.5
            r = math.hypot(px, py)
            if 3.0 <= r <= 5.0:
                a = (math.degrees(math.atan2(px, -py)) + 360) % 360
                if (a - start) % 360 < 90:
                    b.p[y][x] = 1
    return b


def progress_ring(step):
    """step 0..8: the ring filled clockwise from the top in eighths."""
    b = Bmp().ring(5, 9.5, 5.0, 1)
    for y in range(CH):
        for x in range(CW):
            px, py = x + 0.5 - 5, y + 0.5 - 9.5
            r = math.hypot(px, py)
            if 2.6 <= r <= 5.0:
                a = (math.degrees(math.atan2(px, -py)) + 360) % 360
                if a < step * 45 or step == 8:
                    b.p[y][x] = 1
    return b


def toggle(state):
    """A 20x20 switch: off = outline with the knob at the left; on = filled with the knob at the right; dis = dotted."""
    body = rrect(19, 10, 5, 0, 5, 20)
    if state == "on":
        b = body.copy()
        knob = Bmp(20, CH).disc(14.5, 10, 3.6)
        b.blit(knob, op="clear")
    else:
        b = outline(body)
        b.blit(Bmp(20, CH).disc(5.5, 10, 3.2))
        if state == "dis":
            b = dotted(b)
    return b


def _transpose(b):
    o = Bmp(b.h, b.w)
    for y in range(b.h):
        for x in range(b.w):
            o.p[x][y] = b.p[y][x]
    return o


def hourglass(frame):
    """A 20x20 wait glyph, original art: a hourglass (caps, two bulbs, a neck). Frames 0-5 drain the sand from the top bulb (a thread of
    sand falls through the neck); 6 is drained (all in the bottom); 7 is turned on its side with the sand split, so frame 0 (full, upright)
    follows it as the turn completing."""
    body = Bmp(20, 20).poly([(5, 3), (15, 3), (11, 9), (11, 11), (15, 17), (5, 17), (9, 11), (9, 9)])
    glass = outline(body)
    inner = body.copy().blit(glass, op="clear")
    b = Bmp(20, 20).rect(3, 1, 14, 2).rect(3, 17, 14, 2).blit(glass)
    drain = min(frame, 6)
    for y in range(20):
        for x in range(20):
            if not inner.p[y][x]:
                continue
            if y < 10 and y >= 4 + drain and frame != 7:
                b.p[y][x] = 1
            elif y > 10 and y >= 17 - drain and frame != 7:
                b.p[y][x] = 1
    if frame != 7 and drain < 6:
        b.rect(9, 10, 2, 1)                       # the thread of sand in the neck
    if frame == 7:                                # on its side: sand left and right of the neck
        for y in range(20):
            for x in range(20):
                if inner.p[y][x] and (y <= 6 or y >= 14):
                    b.p[y][x] = 1
        b = _transpose(b)
    return b


def orbit(frame):
    """A 20x20 wait glyph: eight dots on a circle, the lead dot big and a tail that shrinks behind it."""
    b = Bmp(20, 20)
    for k in range(8):
        a = math.radians(k * 45)
        cx, cy = 10 + 7.6 * math.sin(a), 10 - 7.6 * math.cos(a)
        age = (frame - k) % 8
        r = (2.2, 1.9, 1.5, 1.0)[age] if age < 4 else 0.7
        b.disc(cx, cy, r)
    return b


def build(R):
    R.section("modern UI")
    U1, U2 = "ui1", "ui2"
    for fill in (False, True):
        for part, c in zip("lmr", pill3(12, 4, fill)):
            R.one(U1, f"ui.pill.{'solid' if fill else 'line'}.{part}", c)
    for nm, b in bar_cells().items():
        R.one(U1, "ui." + nm, b)
    track = Bmp().rect(0, 9, CW, 2)
    R.one(U1, "ui.slider.track", track)
    R.one(U1, "ui.slider.end.l", Bmp().rect(0, 4, 2, 12).rect(2, 9, 8, 2))
    R.one(U1, "ui.slider.end.r", Bmp().rect(8, 4, 2, 12).rect(0, 9, 8, 2))
    for k in range(9):
        t = Bmp().rect(0, 9, CW, 2).rect(k, 3, 2, 14)
        R.one(U1, f"ui.slider.thumb.{k}", t)
    R.one(U1, "ui.vslider.track", Bmp().rect(4, 0, 2, CH))
    for k in range(10):
        R.one(U1, f"ui.vslider.thumb.{k}", Bmp().rect(4, 0, 2, CH).rect(1, 2 * k, 8, 3).rect(0, 2 * k + 1, 10, 1))
    for k in range(8):
        R.one(U1, f"ui.spinner.{k}", spinner(k))
    for k in range(9):
        R.one(U1, f"ui.ring.{k}", progress_ring(k))
    R.one(U1, "ui.scroll.track", Bmp().rect(4, 0, 2, 1).rect(4, 3, 2, 1).rect(4, 6, 2, 1).rect(4, 9, 2, 1).rect(4, 12, 2, 1).rect(4, 15, 2, 1).rect(4, 18, 2, 1))
    R.one(U1, "ui.scroll.thumb.top", rrect(4, 40, 2, 3, 3, CW).sub(0, 0, CW, CH))
    R.one(U1, "ui.scroll.thumb.mid", Bmp().rect(3, 0, 4, CH))
    R.one(U1, "ui.scroll.thumb.bot", rrect(4, 40, 2, 3, -23, CW).sub(0, 0, CW, CH))
    R.one(U1, "ui.scroll.thumb.solo", rrect(4, 14, 2, 3, 3, CW))
    for nm, pts in (("down", [(1, 7), (4, 11), (8, 7)]), ("up", [(1, 11), (4, 7), (8, 11)])):
        b = Bmp()
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            b.line(x0, y0, x1, y1).line(x0, y0 + 1, x1, y1 + 1)
        R.one(U1, f"ui.chevron.{nm}", b)
    for nm, pts in (("left", [(7, 4), (3, 9), (7, 14)]), ("right", [(2, 4), (6, 9), (2, 14)])):
        b = Bmp()
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            b.line(x0, y0, x1, y1).line(x0 + 1, y0, x1 + 1, y1)
        R.one(U1, f"ui.chevron.{nm}", b)
    R.one(U1, "ui.grip", Bmp().rect(7, 6, 2, 2).rect(3, 10, 2, 2).rect(7, 10, 2, 2).rect(-1, 14, 2, 2).rect(3, 14, 2, 2).rect(7, 14, 2, 2))
    R.one(U1, "ui.badge.dot", Bmp().disc(5, 9.5, 3.0)); R.one(U1, "ui.badge.ring", Bmp().ring(5, 9.5, 3.5, 1))
    R.one(U1, "ui.check.mixed", Bmp().frame(1, 5, 8, 10).rect(3, 9, 4, 2))
    R.one(U1, "ui.radio.on", Bmp().ring(5, 9.5, 4.5, 1).disc(5, 9.5, 2.2))
    R.one(U1, "ui.radio.off", Bmp().ring(5, 9.5, 4.5, 1))
    for nm, b in boxes.thin_box().items():
        R.one(U1, f"ui.frame.thin.{nm}", b)
    for nm, flips in (("tl", ()), ("tr", ("h",)), ("bl", ("v",)), ("br", ("h", "v"))):
        g = boxes.arc("", "t")
        for f in flips:
            g = g.flip_h() if f == "h" else g.flip_v()
        R.one(U1, f"ui.frame.thin.round.{nm}", g)
    for st in ("off", "on", "dis"):
        R.two(U2, f"ui.switch.{st}", toggle(st))
    # wait states: one 20x20 glyph each, printed in the same two cells every frame (CLAUDE.md rule 18)
    for k in range(8):
        R.two(U2, f"ui.wait.hourglass.{k}", hourglass(k))
        R.two(U2, f"ui.wait.orbit.{k}", orbit(k))

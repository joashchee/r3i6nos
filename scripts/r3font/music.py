"""Music: notation for Neumetik's formats (MusicXML, ABC, ChordPro, kern, MML, RTTTL, Alda, trackers, MIDI), staff cells, transport,
piano keys, waveforms, knobs. Standard code points where Unicode has the symbol (U+2669-266F, U+1D100-1D1FF, U+23E9-23FA), private use otherwise.

The staff cell: five lines at rows 4, 7, 10, 13, 16 (a 3-row pitch step of 1.5 rows), so a cell holds E4..F5 on the staff and C4..A5 with a
ledger line; cells join into a staff on one text row. Pitch index p: 0 = E4 (bottom line) ... 8 = F5 (top line); -2 = C4, 10 = A5.
"""
import math

from .base import by_unicode
from .canvas import Bmp, CW, CH, digits7, micro
from .pad import rrect, outline

LINES = (4, 7, 10, 13, 16)


def staff(b=None):
    b = b or Bmp()
    for y in LINES:
        b.rect(0, y, CW, 1)
    return b


def pitch_y(p):
    return 16 - 1.5 * p


def head(p, kind):
    """A notehead (and stem) at pitch index p on a staff cell. kind: q quarter, h half, w whole."""
    b = staff()
    yc = pitch_y(p)
    if p == -2:
        b.rect(1, 19, 8, 1)                          # C4's ledger line, through the head
    if p == 10:
        b.rect(1, 1, 8, 1)
    ry = 2.3
    if kind == "w":
        for y in range(CH):
            for x in range(CW):
                px, py = x + 0.5 - 5, y + 0.5 - yc
                if (px / 4.3) ** 2 + (py / ry) ** 2 <= 1:
                    b.p[y][x] = 0 if (px / 2.2) ** 2 + (py / 1.2) ** 2 <= 1 else 1
        return b
    for y in range(CH):
        for x in range(CW):
            px, py = x + 0.5 - 5, y + 0.5 - yc
            if (px / 3.4) ** 2 + (py / ry) ** 2 <= 1:
                b.p[y][x] = 1
    if kind == "h":
        for y in range(CH):
            for x in range(CW):
                px, py = x + 0.5 - 5, y + 0.5 - yc
                if (px / 2.0) ** 2 + (py / 1.0) ** 2 <= 1:
                    b.p[y][x] = 0
    if p < 6:                                        # stem up on the right
        b.rect(7, max(0, int(yc - 9)), 1, int(yc) - max(0, int(yc - 9)))
    else:                                            # stem down on the left
        b.rect(2, int(yc), 1, min(CH, int(yc + 9)) - int(yc))
    return b


ART = {
    "treble": ["....##....", "...#..#...", "...#..#...", "...#..#...", "...#.#....", "....##....", "...##.....", "..#.#.....",
               ".#..#.....", "#...#.....", "#...#.##..", "#...##..#.", ".#..#....#", "..#.#....#", "...##...#.", "....#..#..", "....#.##..",
               "....#.....", "..###.....", "..##......"],
    "bass": ["..####....", ".#....#...", "#..##..#.#", "#..##..#..", "#....#..#.", ".#..#.....", "..##.#.#..", "...#.#....",
             "....#.....", "....#.....", "...#......", "..#......."],
    "alto": ["#.....##..", "#....#..#.", "#...#...#.", "#...#...#.", "##..#...#.", "##.#....#.", "#.##....#.", "#..#....#.",
             "##.#....#.", "##..#...#.", "#...#...#.", "#...#...#.", "#....#..#.", "#.....##.."],
    "perc": ["..#..##..#", "..#..##..#", "..#..##..#", "..#..##..#", "..#..##..#"],
    "q_rest": ["...###....", "....##....", ".....##...", "....##....", "...##.....", "....##....", ".....#.#..", "......##.."],
    "e_rest": ["..##......", ".#..#.....", "..##..#...", ".....#....", "....#.....", "....#.....", "...#......"],
    "s_rest": ["..##......", ".#..#.....", "..##..#...", ".....#....", "..##.#....", ".#..##....", "..##.#....", "....#.....", "...#......"],
}


def put_art(b, name, y, x=0):
    return b.art(x, y, ART[name])


def timesig(n, d):
    b = staff()
    num, den = digits7(str(n)), digits7(str(d))
    # clear the lines behind the digits so they stay legible
    for (m, y) in ((num, 3), (den, 10)):
        x = (CW - m.w) // 2
        for j in range(m.h):
            for i in range(m.w):
                if m.p[j][i]:
                    b.set(x + i, y + j, 1)
    return b


def common(cut):
    b = staff()
    for y in range(CH):
        for x in range(CW):
            px, py = x + 0.5 - 5.5, y + 0.5 - 10
            r = math.hypot(px / 4.2, py / 5.2)
            if 0.62 <= r <= 1.0 and not (px > 1.5 and abs(py) < 2.5):
                b.p[y][x] = 1
    if cut:
        b.rect(5, 2, 1, 16)
    return b


def bars():
    out = {}
    out["staff.blank"] = staff()
    for nm, spec in (("bar", [9]), ("bar.double", [6, 9]), ("bar.end", [5, 8, 9]), ("bar.start", [0, 1, 4])):
        b = staff()
        for x in spec:
            b.rect(x, 4, 1, 13)
        out["staff." + nm] = b
    for nm, b in (("repeat.start", repeat(True)), ("repeat.end", repeat(False))):
        out["staff." + nm] = b
    both = staff()
    both.rect(4, 4, 1, 13).rect(5, 4, 1, 13)
    for dy in (7, 12):
        both.rect(1, dy, 2, 2).rect(7, dy, 2, 2)
    out["staff.repeat.both"] = both
    return out


def repeat(start):
    b = staff()
    if start:
        b.rect(0, 4, 2, 13).rect(3, 4, 1, 13)
        for dy in (8, 12):
            b.rect(6, dy, 2, 2)
    else:
        b.rect(8, 4, 2, 13).rect(6, 4, 1, 13)
        for dy in (8, 12):
            b.rect(2, dy, 2, 2)
    return b


def arcline(w, up, part):
    """Slur / tie pieces: a 2 px curve; part l, m, r over a 3 cell arch."""
    big = Bmp(3 * CW, CH)
    for x in range(3 * CW):
        t = (x + 0.5) / (3 * CW)
        d = 4 * t * (1 - t)                      # 0..1..0
        y = 12 - int(round(6 * d)) if up else 6 + int(round(6 * d))
        big.set(x, y).set(x, y + 1)
    i = "lmr".index(part)
    return big.sub(i * CW, 0, CW, CH)


def piano():
    """A one-row virtual piano. White key cells by the black keys beside them: 'R' black on the right (C, F), 'LR' (D, G, A), 'L' (E, B), 'N' none (the last key of a keyboard).
    Flags: w the white key is pressed (the cell fills and the note dot is a hole in it), l / r the black key on that side is pressed (an
    outline with a sunken face). Every white key has a note indicator at the bottom: a ring when the note is off, a filled dot (a hole in the
    filled key) when it is on. Black keys are 12 rows tall and straddle two cells, so their own indicators are the two-cell glyphs
    piano.bk.on / piano.bk.off, which an app prints in a row above, over the two cells the black key straddles."""
    out = {}
    black_h = 12
    for kind, opts in (("N", [(w, 0, 0) for w in (0, 1)]), ("R", [(w, 0, r) for w in (0, 1) for r in (0, 1)]), ("L", [(w, l, 0) for w in (0, 1) for l in (0, 1)]),
                       ("LR", [(w, l, r) for w in (0, 1) for l in (0, 1) for r in (0, 1)])):
        for w, l, r in opts:
            b = Bmp()
            blk = []
            if "L" in kind:
                blk.append((0, 3, l))
            if "R" in kind:
                blk.append((7, 3, r))
            if w:
                b.rect(1, 0, 9, CH)                   # the pressed white key: solid
            b.rect(0, 0, 1, CH)
            for x, wd, pressed in blk:
                if pressed:
                    b.rect(x, 0, wd, black_h, 0)
                    b.frame(x, 0, wd, black_h)
                    b.rect(x, black_h - 2, wd, 2)     # a pressed black key shows its sunken face
                else:
                    b.rect(x, 0, wd, black_h)
            # the white key's note indicator: ring = off, hole in the filled key = on
            dot = Bmp().ring(5.5, 16, 2.4, 1) if not w else Bmp().disc(5.5, 16, 2.4)
            b.blit(dot, op="clear" if w else "or")
            out["piano." + f"{kind}.{w}{l}{r}"] = b
    out["piano.edge"] = Bmp().rect(0, 0, 1, CH)
    for n in "CDEFGAB":
        out[f"piano.name.{n}"] = Bmp().blit(micro(n), 3, 7)
    bk_off = Bmp(20, CH).rect(8, 7, 4, 6, 1).rect(9, 8, 2, 4, 0)
    bk_on = Bmp(20, CH).rect(8, 7, 4, 6, 1)
    return out, {"piano.bk.off": bk_off, "piano.bk.on": bk_on}


def wave(kind):
    """A 20x20 oscillator picture: one cycle (or two) in rows 4..15."""
    b = Bmp(20, CH)
    top, bot, mid = 4, 15, 9.5
    n = 20
    if kind == "sine":
        for x in range(n):
            y = int(round(mid - 5.5 * math.sin(2 * math.pi * x / n * 1.0)))
            b.set(x, y).set(x, y + 1)
    elif kind == "tri":
        b.line(0, 10, 5, top, 1).line(5, top, 15, bot, 1).line(15, bot, 19, 10, 1)
        b.line(0, 11, 5, top + 1).line(5, top + 1, 15, bot + 0).line(15, bot, 19, 11)
    elif kind == "saw":
        b.line(0, bot, 9, top).line(9, top, 9, bot).line(10, bot, 19, top).line(1, bot, 10, top + 0)
        b.vline(9, top, bot).vline(10, top, bot)
    elif kind == "square":
        b.hline(0, 9, top, 2).vline(9, top, bot, 2).hline(10, 19, bot - 1, 2)
        b.vline(0, top, top + 5, 2)
    elif kind.startswith("pulse"):
        duty = {"pulse12": 3, "pulse25": 5, "pulse75": 15}[kind]
        b.hline(0, duty - 1, top, 2).vline(duty - 1, top, bot, 2).hline(duty, 19, bot - 1, 2)
        b.vline(0, top, bot, 2)
    elif kind == "noise":
        v = 0x2545F491
        for x in range(20):
            v = (v * 1103515245 + 12345) & 0x7FFFFFFF
            y = 4 + (v >> 8) % 12
            b.vline(x, min(y, 9), max(y, 10))
    elif kind == "ramp":
        b.line(0, top, 9, bot).line(10, top, 19, bot).vline(9, top, bot).vline(10, top, bot).line(0, top + 1, 9, bot).line(10, top + 1, 19, bot)
    return b


def curve(kind):
    b = Bmp(20, CH)
    pts = {"adsr": [(1, 16), (5, 4), (8, 8), (13, 8), (18, 16)], "lp": [(0, 6), (10, 6), (14, 10), (19, 16)],
           "hp": [(0, 16), (5, 10), (9, 6), (19, 6)], "bp": [(0, 16), (6, 12), (10, 4), (14, 12), (19, 16)],
           "notch": [(0, 6), (7, 6), (10, 16), (13, 6), (19, 6)]}[kind]
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        b.line(x0, y0, x1, y1).line(x0, y0 + 1, x1, y1 + 1)
    return b


def knob(i, big):
    """A rotary knob with 13 positions from 7 o'clock (min) round the top to 5 o'clock (max)."""
    ang = math.radians(-135 + 22.5 * i)
    if big:
        b = Bmp(20, CH).ring(10, 10, 9.5, 1).ring(10, 10, 6.5, 1)
        cx, cy, r0, r1 = 10, 10, 2.0, 8.5
    else:
        b = Bmp().ring(5, 9.5, 5.0, 1)
        cx, cy, r0, r1 = 5, 9.5, 0.5, 4.0
    steps = 24
    for k in range(steps + 1):
        r = r0 + (r1 - r0) * k / steps
        b.set(int(cx + r * math.sin(ang) - 0.5 + 0.5), int(cy - r * math.cos(ang) - 0.5 + 0.5))
        if big:
            b.set(int(cx + r * math.sin(ang)), int(cy - r * math.cos(ang) + 1))
    return b


def marks():
    vga = by_unicode()
    m = {}
    m["accent"] = Bmp().art(1, 7, ["#.......", ".##.....", "...###..", ".##.....", "#......."])
    m["marcato"] = Bmp().art(1, 6, ["...#....", "..#.#...", ".#...#..", "#.....#."])
    m["staccato"] = Bmp().rect(4, 9, 2, 2)
    m["tenuto"] = Bmp().rect(1, 9, 8, 2)
    m["fermata"] = Bmp().art(0, 6, ["...####...", "..#....#..", ".#..##..#.", ".#..##..#.", "#........#"])
    m["trill"] = Bmp().blit(micro("TR"), 1, 5).art(0, 11, ["..#...#...", ".#.#.#.#..", "#...#...#."])
    m["mordent"] = Bmp().art(0, 8, ["..#...#...", ".#.#.#.#..", "#...#...#."])
    m["turn"] = Bmp().art(0, 8, [".##...#.", "#..#.#.#", "#...#..#", "........"])
    m["segno"] = Bmp().art(0, 4, ["...###....", "..#...#...", "..#.......", "...##.....", ".....##...", ".......#..", "..#...#...", "...###...."]).rect(2, 14, 1, 1).line(0, 17, 9, 3)
    m["coda"] = Bmp().ring(5, 9.5, 4.5, 1).rect(5, 2, 1, 16).rect(1, 9, 9, 1)
    m["caesura"] = Bmp().line(2, 15, 6, 3).line(3, 15, 7, 3).line(5, 15, 9, 3).line(6, 15, 10, 3)
    m["breath"] = Bmp().art(3, 3, ["..#", ".#.", "#.."]).rect(5, 5, 1, 1)
    m["flat"] = Bmp().art(1, 3, ["#.....", "#.....", "#.##..", "##..#.", "#...#.", "#...#.", "##..#.", "#.##..", "......"])
    m["natural"] = Bmp().art(1, 3, ["#.....", "#.....", "#.....", "###...", "#..##.", "#...#.", "#..##.", "...#..", "...#..", "...#.."][:10]).shift(1, 0)
    m["sharp"] = Bmp().art(1, 3, ["..#.#.", "..#.#.", "######", "..#.#.", "..#.#.", "######", "..#.#.", "..#.#."])
    m["dsharp"] = Bmp().art(0, 6, ["#...#...", ".#.#....", "..#.....", ".#.#....", "#...#..."])
    m["dflat"] = Bmp().art(0, 3, ["#...#...", "#...#...", "#.#.#.#.", "##.##..#", "#..#...#", "#..#...#", "##.##..#", "#.#.#.#."])
    m["ped"] = Bmp().blit(micro("PED"), 0, 7)
    m["ped_up"] = Bmp().art(1, 7, ["...#....", "..###...", ".#.#.#..", "...#...."])
    return m


def build(R):
    R.section("music")
    M1, M2 = "mus1", "mus2"
    for nm, b in bars().items():
        R.one(M1, nm, b)
    for kind in ("q", "h", "w"):
        for p in range(-2, 11):
            R.one(M1, f"staff.note.{kind}.{p + 2}", head(p, kind))
    clefs = {"treble": ("treble", 0), "bass": ("bass", 4), "alto": ("alto", 3), "perc": ("perc", 6)}
    for nm, (art, y) in clefs.items():
        b = staff()
        R.one(M1, f"staff.clef.{nm}", b.art(0, y, ART[art]))
    R.one(M1, "staff.rest.whole", staff().rect(2, 7, 6, 3))
    R.one(M1, "staff.rest.half", staff().rect(2, 8, 6, 2))
    R.one(M1, "staff.rest.quarter", put_art(staff(), "q_rest", 5))
    R.one(M1, "staff.rest.eighth", put_art(staff(), "e_rest", 6))
    R.one(M1, "staff.rest.sixteenth", put_art(staff(), "s_rest", 5))
    for n, d in ((2, 4), (3, 4), (4, 4), (5, 4), (6, 4), (2, 2), (3, 8), (6, 8), (7, 8), (9, 8), (5, 8), (3, 2)):
        R.one(M1, f"staff.time.{n}_{d}", timesig(n, d))
    R.one(M1, "staff.time.common", common(False)); R.one(M1, "staff.time.cut", common(True))
    for nm, b in marks().items():
        R.one(M1, f"mus.{nm}", b)
    for up in (True, False):
        for part in "lmr":
            R.one(M1, f"mus.slur.{'up' if up else 'down'}.{part}", arcline(3, up, part))
    keys, pairs = piano()
    for nm, b in keys.items():
        R.one(M1, nm, b)
    for nm, b in pairs.items():
        R.two(M2, nm, b)
    # Unicode musical symbols, drawn without the staff (for running text and chord charts)
    R.section("music (Unicode)")
    put = R.put_if_free
    put(0x2669, Bmp().ellipse(3.5, 14, 3.0, 2.2).rect(5, 3, 1, 12))
    put(0x266D, marks()["flat"]); put(0x266E, marks()["natural"]); put(0x266F, marks()["sharp"])
    put(0x266C, Bmp().ellipse(2.5, 15, 2.4, 1.9).ellipse(7.5, 14, 2.4, 1.9).rect(4, 4, 1, 11).rect(9, 3, 1, 11).rect(4, 4, 6, 2))
    put(0x1D11E, Bmp().art(0, 0, ART["treble"])); put(0x1D122, Bmp().art(0, 4, ART["bass"])); put(0x1D121, Bmp().art(0, 3, ART["alto"]))
    put(0x1D12A, marks()["dsharp"]); put(0x1D12B, marks()["dflat"])
    put(0x1D10B, marks()["segno"]); put(0x1D10C, marks()["coda"]); put(0x1D110, marks()["fermata"]); put(0x1D112, marks()["breath"])
    put(0x1D100, Bmp().rect(5, 3, 1, 14)); put(0x1D101, Bmp().rect(3, 3, 1, 14).rect(6, 3, 1, 14))
    put(0x1D102, Bmp().rect(2, 3, 1, 14).rect(5, 3, 3, 14)); put(0x1D103, Bmp().rect(2, 3, 3, 14).rect(7, 3, 1, 14))
    put(0x1D104, Bmp().rect(5, 3, 1, 2).rect(5, 7, 1, 2).rect(5, 11, 1, 2).rect(5, 15, 1, 2)); put(0x1D105, Bmp().rect(5, 7, 1, 6))
    put(0x1D106, Bmp().rect(1, 3, 3, 14).rect(5, 3, 1, 14).rect(7, 7, 2, 2).rect(7, 11, 2, 2))
    put(0x1D107, Bmp().rect(6, 3, 3, 14).rect(4, 3, 1, 14).rect(1, 7, 2, 2).rect(1, 11, 2, 2))
    put(0x1D15D, Bmp().ellipse(5, 13, 4.3, 2.3).ellipse(5, 13, 2.2, 1.2, 0))
    put(0x1D15E, Bmp().ellipse(4, 14, 3.3, 2.2).ellipse(4, 14, 1.8, 0.9, 0).rect(6, 2, 1, 12))
    put(0x1D15F, Bmp().ellipse(4, 14, 3.3, 2.2).rect(6, 2, 1, 12))
    put(0x1D160, Bmp().ellipse(3.5, 14, 3.0, 2.2).rect(5, 2, 1, 12).art(6, 2, ["#.", "##", ".##", "..#", "..#"]))
    put(0x1D161, Bmp().ellipse(3.5, 14, 3.0, 2.2).rect(5, 1, 1, 13).art(6, 1, ["#.", "##", ".#"]).art(6, 4, ["#.", "##", ".#"]))
    put(0x1D13B, Bmp().rect(2, 6, 6, 3).rect(0, 6, 10, 1)); put(0x1D13C, Bmp().rect(2, 7, 6, 3).rect(0, 10, 10, 1))
    put(0x1D13D, Bmp().art(1, 3, ART["q_rest"])); put(0x1D13E, Bmp().art(1, 5, ART["e_rest"])); put(0x1D13F, Bmp().art(1, 4, ART["s_rest"]))
    # transport and player
    R.section("transport")
    put(0x23E9, Bmp().poly([(0, 5), (5, 9.5), (0, 14)]).poly([(5, 5), (10, 9.5), (5, 14)])); put(0x23EA, Bmp().poly([(10, 5), (5, 9.5), (10, 14)]).poly([(5, 5), (0, 9.5), (5, 14)]))
    put(0x23ED, Bmp().poly([(0, 5), (7, 9.5), (0, 14)]).rect(8, 5, 2, 10)); put(0x23EE, Bmp().poly([(10, 5), (3, 9.5), (10, 14)]).rect(0, 5, 2, 10))
    put(0x23F8, Bmp().rect(1, 4, 3, 12).rect(6, 4, 3, 12)); put(0x23F9, Bmp().rect(1, 5, 8, 10)); put(0x23FA, Bmp().disc(5, 9.5, 4.8))
    put(0x23EF, Bmp().poly([(0, 5), (5, 9.5), (0, 14)]).rect(6, 5, 1, 9).rect(8, 5, 1, 9))
    for nm, b in (("mute", Bmp().poly([(0, 7), (3, 7), (6, 4), (6, 15), (3, 12), (0, 12)]).line(7, 6, 9, 13).line(9, 6, 7, 13)),
                  ("shuffle", Bmp().line(0, 5, 8, 14).line(0, 14, 8, 5).poly([(10, 14), (6, 13), (9, 10)]).poly([(10, 5), (6, 6), (9, 9)])),
                  ("repeat", Bmp().frame(1, 6, 8, 8).rect(1, 6, 3, 1, 0).poly([(2, 3), (6, 6.5), (2, 10)]).rect(6, 13, 3, 1, 0).poly([(8, 17), (4, 13.5), (8, 10)])),
                  ("metronome", Bmp().poly([(2, 16), (8, 16), (6, 3), (4, 3)]).line(5, 14, 8, 6, 1).rect(7, 7, 2, 2, 0)),
                  ("solo", Bmp().ring(5, 9.5, 5, 1).blit(micro("S"), 3, 7)),
                  ("recarm", Bmp().ring(5, 9.5, 5, 1).disc(5, 9.5, 2.6)),
                  ("loop", Bmp().ellipse(5, 10, 4.5, 6.5, inner=(2.9, 4.9)).rect(5, 2, 5, 5, 0).poly([(5, 0.5), (5, 7.5), (9.5, 4)])),
                  ("sync", Bmp().poly([(0, 4), (9, 4), (4.5, 12)])),
                  ("cue", Bmp().rect(2, 3, 1, 14).poly([(3, 3), (9, 5.5), (3, 8)]))):
        R.one(M1, f"mus.{nm}", b)
    R.section("oscillators, filters, knobs")
    for k in ("sine", "tri", "saw", "square", "pulse12", "pulse25", "pulse75", "noise", "ramp"):
        R.two(M2, f"mus.wave.{k}", wave(k))
    for k in ("adsr", "lp", "hp", "bp", "notch"):
        R.two(M2, f"mus.curve.{k}", curve(k))
    for i in range(13):
        R.one(M1, f"mus.knob.s.{i}", knob(i, False))
        R.two(M2, f"mus.knob.l.{i}", knob(i, True))

"""Standard Unicode symbols that CP437 lacks: geometric shapes, arrows, marks, math, punctuation, super/subscripts, control pictures.

Drawn on the VGA's own grid (capitals rows 3-14, x-height rows 7-14, the cell's centre at x=5, y=9.5) so they sit beside CP437 text.
The font as a whole is CC BY-SA 4.0 because of its CP437 base (theme/font/README.txt).
"""
from .base import by_unicode
from .canvas import Bmp, CW, CH, micro, digits7

CY = 9.5


def A(rows, y=0, x=0):
    return Bmp().art(x, y, rows)


def sq(w, h=None, fill=True, cy=CY):
    h = h or w
    x, y = (CW - w) // 2, int(cy - h / 2.0 + 0.5)
    return Bmp().rect(x, y, w, h) if fill else Bmp().frame(x, y, w, h, 1)


def circ(d, fill=True, t=1, cy=CY):
    b = Bmp()
    return b.disc(5, cy, d / 2.0) if fill else b.ring(5, cy, d / 2.0, t)


def hollow(b, k):
    """The outline of a filled shape: its pixels minus those that survive a shrink by k (cheap erosion)."""
    out = b.copy()
    for y in range(b.h):
        for x in range(b.w):
            if b.p[y][x] and all(b.get(x + dx, y + dy) for dx in range(-k, k + 1) for dy in range(-k, k + 1)):
                out.p[y][x] = 0
    return out


def tri(d, fill=True, w=8, h=8, cy=CY):
    cx = 5
    pts = {"u": [(cx, cy - h / 2), (cx + w / 2, cy + h / 2), (cx - w / 2, cy + h / 2)],
           "d": [(cx - w / 2, cy - h / 2), (cx + w / 2, cy - h / 2), (cx, cy + h / 2)],
           "l": [(cx - w / 2, cy), (cx + w / 2, cy - h / 2), (cx + w / 2, cy + h / 2)],
           "r": [(cx + w / 2, cy), (cx - w / 2, cy - h / 2), (cx - w / 2, cy + h / 2)]}[d]
    b = Bmp().poly(pts)
    return b if fill else hollow(b, 1)


def diamond(w, h, fill=True):
    b = Bmp().poly([(5, CY - h / 2), (5 + w / 2, CY), (5, CY + h / 2), (5 - w / 2, CY)])
    return b if fill else hollow(b, 1)


def geometric(R):
    R.section("geometric shapes")
    put = R.put_if_free
    put(0x25A1, Bmp().frame(2, 5, 6, 9))                          # □ matches VGA's ■ (6 x 9)
    put(0x25A2, Bmp().frame(1, 4, 8, 11).set(1, 4, 0).set(8, 4, 0).set(1, 14, 0).set(8, 14, 0))
    put(0x25AA, sq(4, 5)); put(0x25AB, sq(4, 5, False))
    put(0x25AE, Bmp().rect(2, 3, 6, 14)); put(0x25AF, Bmp().frame(2, 3, 6, 14))
    put(0x25B0, Bmp().rect(0, 7, 10, 6)); put(0x25B1, Bmp().frame(0, 7, 10, 6))
    put(0x25FB, sq(8, 8, False)); put(0x25FC, sq(8, 8)); put(0x25FD, sq(6, 6, False)); put(0x25FE, sq(6, 6))
    put(0x2B1B, sq(10, 10)); put(0x2B1C, sq(10, 10, False))
    put(0x25B3, tri("u", False, 9, 9)); put(0x25B4, tri("u", True, 6, 6)); put(0x25B5, tri("u", False, 6, 6))
    put(0x25B7, tri("r", False, 8, 10)); put(0x25B8, tri("r", True, 5, 7)); put(0x25B9, tri("r", False, 5, 7))
    put(0x25BD, tri("d", False, 9, 9)); put(0x25BE, tri("d", True, 6, 6)); put(0x25BF, tri("d", False, 6, 6))
    put(0x25C1, tri("l", False, 8, 10)); put(0x25C2, tri("l", True, 5, 7)); put(0x25C3, tri("l", False, 5, 7))
    R.same(0x25B6, 0x25BA); R.same(0x25C0, 0x25C4)
    put(0x25C6, diamond(9, 14)); put(0x25C7, diamond(9, 14, False)); put(0x25CA, diamond(8, 12, False))
    put(0x25C8, diamond(9, 14, False).blit(diamond(3, 5)))
    put(0x25CF, circ(9)); put(0x25EF, circ(10, False)); put(0x25E6, circ(4))
    put(0x25CD, circ(9, False).blit(Bmp().rect(3, 6, 1, 7).rect(5, 5, 1, 9).rect(7, 6, 1, 7)))
    put(0x25CE, circ(10, False).blit(circ(4))); put(0x25C9, circ(9, False).blit(circ(5)))
    put(0x25CC, A(["...#..#...", "..........", ".#......#.", "..........", "#........#", "..........", "#........#",
                   "..........", ".#......#.", "..........", "...#..#..."], 4))
    for cp, half in ((0x25D0, "l"), (0x25D1, "r"), (0x25D2, "b"), (0x25D3, "t")):
        d, keep = circ(9), Bmp()
        for y in range(CH):
            for x in range(CW):
                if d.p[y][x] and {"l": x < 5, "r": x >= 5, "b": y >= CY, "t": y < CY}[half]:
                    keep.p[y][x] = 1
        put(cp, circ(9, False).blit(keep))
    put(0x25D6, Bmp().ellipse(9.5, 9.5, 9.4, 9.4, quad={"tl", "bl"}))     # ◖ ◗ half discs, the full height of the cell
    put(0x25D7, Bmp().ellipse(0.5, 9.5, 9.4, 9.4, quad={"tr", "br"}))
    put(0x25E2, Bmp().poly([(10, 0), (10, 20), (0, 20)])); put(0x25E3, Bmp().poly([(0, 0), (10, 20), (0, 20)]))
    put(0x25E4, Bmp().poly([(0, 0), (10, 0), (0, 20)])); put(0x25E5, Bmp().poly([(0, 0), (10, 0), (10, 20)]))
    for cp, q in ((0x25DC, "tl"), (0x25DD, "tr"), (0x25DE, "br"), (0x25DF, "bl")):
        put(cp, Bmp().ellipse(5, CY, 4.6, 4.6, inner=(3.4, 3.4), quad={q}))


def arrows(R):
    R.section("arrows")
    put = R.put_if_free
    ne = Bmp().line(1, 14, 8, 5).line(2, 14, 8, 6).hline(3, 8, 5).vline(8, 5, 11)        # ↗
    put(0x2197, ne); put(0x2196, ne.flip_h().shift(1)); put(0x2198, ne.flip_v().shift(0, 0)); put(0x2199, ne.flip_h().flip_v().shift(1))

    def dbl(k):
        b = Bmp()
        if k == "r":
            b.hline(0, 7, 8).hline(0, 7, 11).line(5, 5, 9, 9).line(9, 10, 5, 14)
        elif k == "l":
            b.hline(2, 9, 8).hline(2, 9, 11).line(4, 5, 0, 9).line(0, 10, 4, 14)
        elif k == "u":
            b.vline(3, 6, 17).vline(6, 6, 17).line(2, 5, 4, 1).line(7, 5, 5, 1).hline(4, 5, 1)
        else:
            b.vline(3, 2, 13).vline(6, 2, 13).line(2, 14, 4, 18).line(7, 14, 5, 18).hline(4, 5, 18)
        return b
    put(0x21D0, dbl("l")); put(0x21D2, dbl("r")); put(0x21D1, dbl("u")); put(0x21D3, dbl("d"))
    put(0x21D4, Bmp().hline(2, 7, 8).hline(2, 7, 11).line(3, 5, 0, 9).line(0, 10, 3, 14).line(6, 5, 9, 9).line(9, 10, 6, 14))
    put(0x21D5, Bmp().vline(3, 4, 15).vline(6, 4, 15).line(2, 5, 4, 1).line(7, 5, 5, 1).line(2, 14, 4, 18).line(7, 14, 5, 18))
    put(0x21E6, Bmp().poly([(0, 9.5), (4, 5), (4, 8), (10, 8), (10, 11), (4, 11), (4, 14)]))
    put(0x21E8, Bmp().poly([(10, 9.5), (6, 5), (6, 8), (0, 8), (0, 11), (6, 11), (6, 14)]))
    put(0x21E7, Bmp().poly([(5, 1), (9.5, 8), (6.5, 8), (6.5, 18), (3.5, 18), (3.5, 8), (0.5, 8)]))
    put(0x21E9, Bmp().poly([(5, 19), (9.5, 12), (6.5, 12), (6.5, 2), (3.5, 2), (3.5, 12), (0.5, 12)]))
    put(0x21B5, A(["........#.", "........#.", "..#.....#.", ".##.....#.", "##########"], 6))
    put(0x21A9, A(["...#......", "..##......", ".#########", "..##.....#", "...#.....#"], 6))
    put(0x21AA, A(["......#...", "......##..", "#########.", "#.....##..", "#.....#..."], 6))
    cw = Bmp().ellipse(5, 10, 4.2, 6.5, inner=(2.6, 4.9)).rect(5, 2, 5, 6, 0).blit(Bmp().poly([(2.5, 1), (2.5, 8), (8, 4.5)]))   # ↻
    put(0x21BB, cw); put(0x21BA, cw.flip_h())
    put(0x23F4, tri("l", True, 6, 10)); put(0x23F5, tri("r", True, 6, 10)); put(0x23F6, tri("u", True, 10, 8)); put(0x23F7, tri("d", True, 10, 8))
    put(0x2794, Bmp().rect(0, 9, 6, 3).poly([(10, 10.5), (6, 6), (6, 15)]))
    put(0x27A4, Bmp().poly([(10, 10), (0, 3), (3, 10), (0, 17)]))


def marks(R):
    R.section("marks and signs")
    put = R.put_if_free
    put(0x2713, A(["........##", ".......##.", "......##..", "#....##...", "##..##....", ".####.....", "..##......"], 7))
    put(0x2714, Bmp().line(1, 11, 3, 14, 2).line(2, 11, 4, 14, 2).line(4, 14, 9, 4, 2))
    put(0x2717, A(["#......#", ".#....#.", "..#..#..", "...##...", "...##...", "..#..#..", ".#....#.", "#......#"], 6, 1))
    put(0x2718, Bmp().line(1, 6, 8, 14, 2).line(8, 6, 1, 14, 2))
    put(0x2715, Bmp().line(2, 7, 7, 12).line(3, 7, 8, 12).line(7, 7, 2, 12).line(8, 7, 3, 12))
    put(0x2716, Bmp().line(1, 6, 7, 13, 2).line(8, 6, 2, 13, 2))
    box = Bmp().frame(1, 5, 8, 10)
    put(0x2610, box); put(0x2611, box.copy().blit(Bmp().line(3, 9, 4, 12).line(5, 12, 7, 6)))
    put(0x2612, box.copy().blit(Bmp().line(3, 7, 6, 12).line(6, 7, 3, 12)))
    put(0x2605, A(["....##....", "....##....", "...####...", "##########", ".########.", "..######..", "..######..", ".###..###.", ".##....##."], 5))
    put(0x2606, A(["....##....", "....##....", "...#..#...", "###....###", ".##....##.", "..#....#..", "..#....#..", ".##....##.", ".#......#."], 5))
    put(0x2726, A(["....##....", "....##....", "...####...", "##########", "##########", "...####...", "....##....", "....##...."], 5))
    put(0x2727, A(["....##....", "....##....", "...#..#...", "###....###", "###....###", "...#..#...", "....##....", "....##...."], 5))
    put(0x2661, A([".##..##.", "#..##..#", "#......#", ".#....#.", "..#..#..", "...##..."], 6, 1))
    put(0x26A0, A(["....##....", "....##....", "...#..#...", "...#..#...", "..#.##.#..", "..#.##.#..", ".#......#.", ".#..##..#.", "##########"], 5))
    put(0x26A1, A(["......##", ".....##.", "....##..", "...####.", "....##..", "...##...", "..##....", "..#....."], 5, 1))
    put(0x2318, A(["##...##.", "#.#.#.#.", "##.#.##.", ".#####..", "##.#.##.", "#.#.#.#.", "##...##."], 6, 1))
    put(0x2303, A(["...##...", "..#..#..", ".#....#.", "#......#"], 6, 1))
    put(0x2325, A(["###....#", "..#...#.", "...#.#..", "...###..", "....#...", "...#.#..", "..#...#."], 6, 1))
    R.same(0x23CE, 0x21B5)                                      # ⏎ return symbol = the return arrow
    put(0x2423, A(["#......#", "#......#", "########"], 13, 1))
    put(0x232B, Bmp().poly([(0, 9.5), (4, 4), (10, 4), (10, 15), (4, 15)]).blit(Bmp().line(5, 7, 8, 12).line(8, 7, 5, 12), op="xor"))
    put(0x2326, Bmp().poly([(10, 9.5), (6, 4), (0, 4), (0, 15), (6, 15)]).blit(Bmp().line(1, 7, 4, 12).line(4, 7, 1, 12), op="xor"))
    put(0x23CF, A(["....##....", "...####...", "..######..", ".########.", "##########", "..........", "##########", "##########"], 5))
    put(0x2328, Bmp().frame(0, 5, 10, 10).blit(A(["#.#.#.#.", "........", "#.#.#.#.", "........", ".######."], 7, 1)))
    put(0x2709, Bmp().frame(0, 5, 10, 10).line(0, 5, 4, 10).line(9, 5, 5, 10))
    put(0x2699, Bmp().disc(5, 9.5, 4.2).ellipse(5, 9.5, 2.0, 2.0, 0).blit(A(["..#..#..", ".#....#."], 3, 1)).blit(A([".#....#.", "..#..#.."], 14, 1)))
    put(0x2600, Bmp().disc(5, 9.5, 2.6).blit(Bmp().rect(5, 3, 1, 3).rect(5, 14, 1, 3).rect(1, 9, 3, 1).rect(7, 9, 3, 1).set(2, 5).set(8, 5).set(2, 14).set(8, 14)))
    put(0x263E, Bmp().ellipse(5, 9.5, 4, 6).ellipse(7.5, 9.5, 3.4, 5.2, 0))
    put(0x2601, A([".....###..", "..##.#..#.", ".#..#...#.", "#........#", "##########"], 6))
    put(0x23F3, A(["#######", ".#...#.", "..#.#..", "...#...", "..###..", ".#####.", "#######"], 6, 1))
    put(0x23F1, circ(9, False).blit(Bmp().vline(5, 6, 10).hline(5, 7, 10)).rect(4, 3, 2, 1))
    put(0x2691, A(["######..", "#######.", "######..", "#.......", "#.......", "#.......", "#......."], 4, 1))
    put(0x2690, A(["######..", "#....##.", "######..", "#.......", "#.......", "#.......", "#......."], 4, 1))
    put(0x270F, Bmp().line(2, 14, 8, 5).line(3, 14, 9, 5).line(1, 16, 3, 14).hline(1, 3, 16))
    put(0x2315, circ(6, False).shift(-1, -2).blit(Bmp().line(6, 10, 9, 15, 2)))
    put(0x22EE, Bmp().rect(4, 4, 2, 2).rect(4, 9, 2, 2).rect(4, 14, 2, 2)); put(0x22EF, Bmp().rect(0, 9, 2, 2).rect(4, 9, 2, 2).rect(8, 9, 2, 2))
    put(0x2630, Bmp().rect(0, 5, 10, 2).rect(0, 9, 10, 2).rect(0, 13, 10, 2))
    put(0x00D7, A(["#...#", ".#.#.", "..#..", ".#.#.", "#...#"], 7, 2))
    put(0x2212, Bmp().rect(1, 9, 8, 2))
    put(0x2795, Bmp().rect(1, 9, 8, 2).rect(4, 6, 2, 8)); put(0x2796, Bmp().rect(1, 9, 8, 2))
    put(0x2797, Bmp().rect(1, 9, 8, 2).rect(4, 5, 2, 2).rect(4, 13, 2, 2))


def mathsym(R):
    R.section("math and logic")
    put = R.put_if_free
    put(0x2260, A([".......#", "......#.", "########", ".....#..", "########", "...#....", "..#....."], 6, 1))
    put(0x2248, A([".##..#..", "#..##..#", "........", ".##..#..", "#..##..#"], 7, 1))
    put(0x2262, Bmp().rect(1, 6, 8, 1).rect(1, 9, 8, 1).rect(1, 12, 8, 1).line(7, 4, 2, 15))
    put(0x2264, A([".....#", "...##.", "###...", "...##.", ".....#", "......", "######"], 6, 1))
    put(0x2265, A(["#.....", ".##...", "...###", ".##...", "#.....", "......", "######"], 6, 1))
    put(0x2200, A(["#######", ".#...#.", ".#...#.", "..#.#..", "..#.#..", "...#...", "...#..."], 5, 1))
    put(0x2203, A(["#######", "......#", "......#", ".######", "......#", "......#", "#######"], 5, 1))
    inn = A([".####", "#....", "#....", "#####", "#....", "#....", ".####"], 5, 2)
    put(0x2208, inn); put(0x2209, inn.copy().blit(Bmp().line(8, 3, 1, 16)))
    put(0x2227, A(["...##...", "..#..#..", ".#....#.", "#......#"], 6, 1)); put(0x2228, A(["#......#", ".#....#.", "..#..#..", "...##..."], 6, 1))
    put(0x2295, Bmp().ring(5, 9.5, 4.8, 1).rect(5, 6, 1, 8).rect(2, 9, 7, 1))
    put(0x2297, Bmp().ring(5, 9.5, 4.8, 1).line(2, 6, 7, 13).line(7, 6, 2, 13))
    put(0x2211, A(["########", "#......#", ".#......", "..#.....", "...#....", "..#.....", ".#......", "#......#", "########"], 5, 1))
    put(0x220F, A(["########", ".#....#.", ".#....#.", ".#....#.", ".#....#.", ".#....#.", ".#....#.", ".#....#."], 6, 1))
    put(0x222B, A([".....##", "....#..", "....#..", "....#..", "...#...", "...#...", "...#...", "...#...", "..#....", "..#....", "..#....", "##....."], 3, 1))
    put(0x2202, A(["..###.", "....#.", ".####.", "#...#.", "#...#.", ".###.."], 5, 2))
    put(0x2207, A(["########", ".#....#.", "..#..#..", "...##..."], 5, 1))
    delta = A(["...##...", "..#..#..", ".#....#.", "########"], 6, 1)
    put(0x2206, delta); put(0x0394, delta)
    put(0x2282, A(["######", "#.....", "#.....", "#.....", "######"], 7, 2)); put(0x2283, A(["######", ".....#", ".....#", ".....#", "######"], 7, 2))
    put(0x222A, A(["#....#", "#....#", "#....#", "#....#", ".####."], 6, 2))
    put(0x2234, Bmp().rect(4, 5, 2, 2).rect(1, 12, 2, 2).rect(7, 12, 2, 2)); put(0x2235, Bmp().rect(1, 5, 2, 2).rect(7, 5, 2, 2).rect(4, 12, 2, 2))
    put(0x2218, circ(4, False)); put(0x22C5, Bmp().rect(4, 9, 2, 2))
    put(0x22A2, Bmp().vline(1, 5, 14).hline(1, 8, 9))


def punct(R):
    R.section("punctuation and Latin symbols")
    put = R.put_if_free
    put(0x2018, A(["..##", ".##.", ".##.", ".#.."], 3, 3)); put(0x2019, A(["..##", "..##", "..#.", ".#.."], 3, 3))
    put(0x201C, A(["..##..##", ".##..##.", ".##..##.", ".#...#.."], 3, 1)); put(0x201D, A(["..##..##", "..##..##", "..#...#.", ".#...#.."], 3, 1))
    put(0x201A, A(["..##", "..##", "..#.", ".#.."], 13, 3)); put(0x201E, A(["..##..##", "..##..##", "..#...#.", ".#...#.."], 13, 1))
    put(0x2013, Bmp().rect(0, 9, 8, 1)); put(0x2014, Bmp().rect(0, 9, 10, 1))
    put(0x2010, Bmp().rect(2, 9, 6, 1)); put(0x2011, Bmp().rect(2, 9, 6, 1))
    put(0x2026, Bmp().rect(0, 13, 2, 2).rect(4, 13, 2, 2).rect(8, 13, 2, 2))
    put(0x2020, A(["..#..", "..#..", "#####", "..#..", "..#..", "..#..", "..#..", "..#.."], 3, 2))
    put(0x2021, A(["..#..", "..#..", "#####", "..#..", "#####", "..#..", "..#.."], 3, 2))
    put(0x2032, A([".#", ".#", "#."], 3, 4)); put(0x2033, A([".#.#", ".#.#", "#.#."], 3, 3))
    put(0x2039, A(["..#", ".#.", "#..", ".#.", "..#"], 7, 3)); put(0x203A, A(["#..", ".#.", "..#", ".#.", "#.."], 7, 3))
    put(0x20AC, A([".####.", "#....#", "###...", "#.....", "###...", "#....#", ".####."], 5, 1))
    put(0x2122, A(["###.#...#", ".#..##.##", ".#..#.#.#", ".#..#...#"], 3, 0))
    put(0x00A9, Bmp().ring(5, 9.5, 5, 1).blit(A([".##", "#..", "#..", "#..", ".##"], 7, 4)))
    put(0x00AE, Bmp().ring(5, 9.5, 5, 1).blit(A(["##.", "#.#", "##.", "#.#", "#.#"], 7, 4)))
    put(0x00A6, Bmp().rect(4, 2, 2, 6).rect(4, 12, 2, 6))
    put(0x00A4, A(["#.#..#.#", ".#....#.", "#......#", "#......#", ".#....#.", "#.#..#.#"], 6, 1))
    put(0x00B9, A([".#.", "##.", ".#.", ".#.", "###"], 3, 3)); put(0x00B3, A(["##.", "..#", ".#.", "..#", "##."], 3, 3))
    put(0x00AF, Bmp().rect(1, 2, 8, 1)); put(0x00B4, A(["..#", ".#."], 3, 4)); put(0x00B8, A([".#", "..#", "##."], 15, 3))
    put(0x00A8, Bmp().rect(2, 3, 2, 2).rect(6, 3, 2, 2)); put(0x2024, Bmp().rect(4, 13, 2, 2))
    put(0x00BE, Bmp().blit(micro("3"), 0, 4).blit(micro("4"), 6, 12).line(8, 4, 2, 16))
    put(0x00A0, Bmp())
    R.same(0x00AD, 0x2D)                                       # soft hyphen shows as a hyphen


def superscripts(R):
    R.section("super and subscripts, circled digits")
    put = R.put_if_free
    for i, d in enumerate("0123456789"):
        sup = {1: 0x00B9, 2: 0x00B2, 3: 0x00B3}.get(i, 0x2070 + i)
        put(sup, Bmp().blit(digits7(d), 2, 2))
        put(0x2080 + i, Bmp().blit(digits7(d), 2, 12))
    put(0x207A, Bmp().blit(digits7("+"), 2, 2)); put(0x207B, Bmp().blit(digits7("-"), 2, 2))
    put(0x208A, Bmp().blit(digits7("+"), 2, 12)); put(0x208B, Bmp().blit(digits7("-"), 2, 12))
    put(0x207D, Bmp().blit(digits7("("), 2, 2)); put(0x207E, Bmp().blit(digits7(")"), 2, 2))
    put(0x208D, Bmp().blit(digits7("("), 2, 12)); put(0x208E, Bmp().blit(digits7(")"), 2, 12))
    for n in range(0, 21):
        s = str(n)
        m = micro(s, 0 if len(s) == 2 else 1)
        x = 5 - m.w // 2
        put(0x24EA if n == 0 else 0x2460 + n - 1, Bmp().ring(5, 9.5, 5.0, 1).blit(m, x, 7))
        if 1 <= n <= 10:
            put(0x2776 + n - 1, Bmp().disc(5, 9.5, 5.0).blit(m, x, 7, op="clear"))


def controls(R):
    R.section("control pictures")
    names = {0: "NUL", 1: "SOH", 2: "STX", 3: "ETX", 4: "EOT", 5: "ENQ", 6: "ACK", 7: "BEL", 8: "BS", 9: "HT", 10: "LF", 11: "VT",
             12: "FF", 13: "CR", 14: "SO", 15: "SI", 16: "DLE", 17: "DC1", 18: "DC2", 19: "DC3", 20: "DC4", 21: "NAK", 22: "SYN",
             23: "ETB", 24: "CAN", 25: "EM", 26: "SUB", 27: "ESC", 28: "FS", 29: "GS", 30: "RS", 31: "US", 32: "SP", 127: "DEL"}
    for n, s in names.items():
        b = Bmp()
        if len(s) == 2:
            b.blit(micro(s), 1, 7)
        else:
            b.blit(micro(s[:2]), 1, 4).blit(micro(s[2:]), 1, 11)
        R.put_if_free(0x2400 + n if n < 33 else 0x2421, b)


def build(R):
    geometric(R)
    arrows(R)
    marks(R)
    mathsym(R)
    punct(R)
    superscripts(R)
    controls(R)

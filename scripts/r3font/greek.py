"""The Latin letters and Greek letters CP437 lacks, drawn here (first drafts in the CP437 style: 3 px left stems, 2 px right stems, capitals rows 3-14,
x-height rows 7-14, descenders to row 18). Letters that are one base plus one mark are composed from the CP437 letters (latin.compose).
Art is ASCII: `#` ink, `.` blank; each glyph is (first row, rows). Cyrillic letters that are not Latin shapes are not drawn yet (ROADMAP section 43).
"""
import unicodedata

from .canvas import Bmp, CW
from . import latin

CAP, LOW = 3, 7

ART = {
    # Greek capitals
    0x39B: (CAP, ["...###...", "...###...", "..##.##..", "..##.##..", ".##...##.", ".##...##.", ".##...##.", "##.....##",
                  "##.....##", "##.....##", "##.....##", "##.....##"]),
    0x39E: (CAP, ["#########", "#########", ".........", ".........", ".........", ".#######.", ".#######.", ".........",
                  ".........", ".........", "#########", "#########"]),
    0x3A0: (CAP, ["#########", "#########"] + ["###...##."] * 10),
    0x3A8: (CAP, ["##.###.##"] * 7 + [".#######.", "..#####.."] + ["...###..."] * 3),
    # Greek lowercase
    0x3B2: (3, ["..####...", ".##..##..", ".##..##..", ".##..##..", ".##.##...", ".#####...", ".##..##..", ".##...##.", ".##...##.",
                ".##...##.", ".##..##..", ".#####...", ".##......", ".##......", ".##......", ".##......"]),
    0x3B3: (LOW, ["##.....##", "##.....##", ".##...##.", ".##...##.", "..##.##..", "..#####..", "...###...", "...###...", "...###...",
                  "...###...", "...###...", "...###..."]),
    0x3B6: (3, [".#######.", ".##......", ".##......", ".##......", ".##......", "..##.....", ".##......", "##.......", "##.......",
                "##.......", "##....##.", ".######..", "....##...", "....##...", "....###..", "...###..."]),
    0x3B9: (LOW, ["..###....", "...##....", "...##....", "...##....", "...##....", "...##....", "...##....", "...####.."]),
    0x3BB: (3, ["##.......", ".##......", ".##......", "..##.....", "..###....", ".##.##...", ".##..##..", "##...##..", "##....##.",
                "#.....##.", "#......##", "#......##"]),
    0x3BE: (3, [".#######.", ".##......", ".##......", ".##......", ".##......", "..####...", ".##......", "##.......", "##.......",
                "##.......", "##....##.", ".######..", "....##...", "....##...", "....###..", "...###..."]),
    0x3C7: (LOW, ["##.....##", ".##...##.", "..##.##..", "...###...", "...###...", "..##.##..", ".##...##.", "##.....##",
                  "##.......", "##.......", "#........", "#........"]),
    0x3C8: (3, ["...###...", "...###...", "...###...", "...###...", "##.###.##", "##.###.##", "##.###.##", "##.###.##", "##.###.##",
                "##.###.##", ".#######.", "..#####..", "...###...", "...###...", "...###...", "...###..."]),
    # Latin
    0xDE: (CAP, ["###......", "###......", "#########.", "###....##.", "###....##.", "###....##.", "###....##.", "#########.",
                 "###......", "###......", "###......", "###......"]),
    0xFE: (3, ["###......", "###......", "###......", "###......", "###.####.", "####...##", "###....##", "###....##", "###....##",
              "####...##", "###.####.", "###......", "###......", "###......", "###......", "###......"]),
    0xF0: (3, ["..##.....", "...##.##.", ".#######.", "....###..", ".####.##.", "##...####", "##....###", "##....###", "##....###",
               "##...####", ".####.##."]),
    0x17F: (CAP, ["...####..", "..###.##.", "..##.....", "..##.....", "..##.....", "..##.....", "..##.....", "..##.....", "..##.....",
                  "..##.....", "..##.....", "..##....."]),
}


def build(R, miss):
    R.section("Latin and Greek letters drawn here (first drafts)")
    g = R.g

    def put(cp, b):
        if cp not in g and cp not in R.alias:
            R.put(cp, b)

    for cp, v in ART.items():
        if v:
            b = Bmp()
            b.art(0, v[0], v[1])
            put(cp, b)
    # theta: the capital O with a bar
    t = g[ord("O")].copy()
    t.rect(2, 8, 5, 2)
    put(0x3B8, t)
    # kappa and the Latin kra: a k without its ascender
    for cp in (0x3BA, 0x138):
        put(cp, latin.dotless(g[ord("k")]))
    # eta: n with a descender on the right stem
    eta = g[ord("n")].copy().art(7, 15, ["##", "##", "##", "##"])
    put(0x3B7, eta)
    # final sigma: c with a tail
    put(0x3C2, g[ord("c")].copy().art(5, 15, ["##.", "##.", "###"]))
    # chi's tail and the Greek letters that are shapes of Latin ones
    for cp, ch in ((0x3BC, "µ"), (0x3C1, "p"), (0x3C5, "u"), (0x3C9, "w")):
        if cp not in g and cp not in R.alias and ord(ch) in g:
            R.same(cp, ord(ch))
    # thorn, eth with bars, long s, the n with an apostrophe
    put(0x149, g[ord("n")].copy().art(0, 3, ["##", "##", "#."]))
    put(0x14A, g[ord("N")].copy().art(7, 15, ["##", "##", "####"]).art(5, 17, ["####"]))
    put(0x14B, g[ord("n")].copy().art(7, 15, ["##", "##", ".#"]).art(4, 17, ["###"]))
    # ligatures: the left of O or o, one shared stem, the arms of E or e
    for cp, (a, b, top) in {0x152: ("O", "E", CAP), 0x153: ("o", "e", LOW)}.items():
        x = Bmp()
        for yy in range(x.h):
            for xx in range(CW):
                x.p[yy][xx] = g[ord(a)].p[yy][xx] if xx < 4 else g[ord(b)].p[yy][xx] if xx >= 6 else int(top <= yy <= 14 and 4 <= xx < 6)
        put(cp, x)
    # the dotted L and l
    put(0x13F, g[ord("L")].copy().rect(6, 8, 2, 2))
    put(0x140, g[ord("l")].copy().rect(7, 9, 2, 2))
    # the two-letter IJ and ij
    gl = Bmp().art(0, 3, ["..##...##.", "..##...##."])
    gl.art(0, 7, [".###..###.."] + ["..##...##.."] * 6 + [".####..##.."]).art(7, 15, ["##"] * 3).art(5, 18, ["####"])
    put(0x133, gl)
    cap = Bmp().art(0, 3, ["#####.####"] + [".###...##."] * 9 + [".###.#.##.", "#####.###."])
    put(0x132, cap)
    # Greek letters with a tonos or dialytika: base + one mark
    for cp in list(range(0x380, 0x400)):
        if cp in g or cp in R.alias:
            continue
        d = unicodedata.normalize("NFD", chr(cp))
        if len(d) == 2 and (ord(d[0]) in g or ord(d[0]) in R.alias) and ord(d[1]) in latin.ABOVE:
            b = latin.compose(R.glyph(ord(d[0])), ord(d[1]))
            if b:
                R.put(cp, b)
    return [c for c in miss if c not in g and c not in R.alias]

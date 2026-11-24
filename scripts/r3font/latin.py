"""Latin accents composed from the CP437 letters: every Latin-1 Supplement and Latin Extended-A letter that is one base letter plus one mark
(grave, acute, circumflex, tilde, macron, breve, dot, diaeresis, ring, double acute, caron, cedilla, ogonek, comma below), plus the barred
and slashed letters (Ø ø Đ đ Ł ł Ħ ħ Ŧ ŧ Ð). The accent sits just above the letter's own top row, so capitals and tall letters get it in the
three free rows and x-height letters in the four above them. Greek and Cyrillic capitals and lowercase that are the Latin shapes alias them.
"""
import unicodedata

from .canvas import Bmp, CW, CH

ABOVE = {
    0x300: ["#..", ".#.", "..#"], 0x301: ["..#", ".#.", "#.."], 0x302: ["..#..", ".#.#.", "#...#"], 0x30C: ["#...#", ".#.#.", "..#.."],
    0x303: [".##..#", "#..##."], 0x304: ["#####"], 0x306: ["#...#", ".###."], 0x307: ["##", "##"], 0x308: ["##.##", "##.##"],
    0x30A: [".#.", "#.#", ".#."], 0x30B: ["..#.#", ".#.#.", "#.#.."],
}
BELOW = {0x327: [".#.", "..#", "##."], 0x328: [".#", "#.", ".#"], 0x326: [".#", ".#", "#."]}


def ink_box(b):
    ys = [y for y in range(b.h) if any(b.p[y])]
    xs = [x for x in range(b.w) if any(b.p[y][x] for y in range(b.h))]
    return (min(xs), max(xs), min(ys), max(ys)) if ys else None


def compose(base, mark):
    b = base.copy()
    x0, x1, y0, y1 = ink_box(base)
    cx = (x0 + x1) // 2
    if mark in ABOVE:
        art = ABOVE[mark]
        h, w = len(art), max(map(len, art))
        bottom = y0 - 1 if y0 <= 4 else y0 - 2
        y = bottom - h + 1
        if y < 0:
            y = 0
        b.art(max(0, min(CW - w, cx - w // 2)), y, art)
    elif mark in BELOW:
        art = BELOW[mark]
        w = max(map(len, art))
        x = cx - w // 2 if mark != 0x328 else min(CW - w, x1 - 1)
        b.art(max(0, min(CW - w, x)), 15, art)
    else:
        return None
    return b


def dotless(base):
    b = base.copy()
    for y in range(0, 7):
        b.p[y] = [0] * CW
    return b


def build(R):
    R.section("Latin accents (composed)")
    miss = []
    ranges = list(range(0xC0, 0x100)) + list(range(0x100, 0x180))
    for cp in ranges:
        if cp in R.g or cp in R.alias:
            continue
        d = unicodedata.normalize("NFD", chr(cp))
        if len(d) == 2 and ord(d[0]) in R.g and ord(d[1]) in ABOVE.keys() | BELOW.keys():
            base = R.g[ord(d[0])]
            if d[0] in "ij":
                base = dotless(base)
            g = compose(base, ord(d[1]))
            if g:
                R.put(cp, g)
                continue
        miss.append(cp)
    # barred and slashed letters
    def over(ch, fn):
        return fn(R.g[ord(ch)].copy())

    def slash(b):
        x0, x1, y0, y1 = ink_box(b)
        return b.line(x0, y1 - 1, x1, y0 + 1)

    def bar(y, x0=0, x1=7):
        return lambda b: b.hline(x0, x1, y)
    special = {0xD8: ("O", slash), 0xF8: ("o", slash), 0x110: ("D", bar(8, 0, 5)), 0x111: ("d", bar(5, 4, 9)), 0x141: ("L", slash),
               0x142: ("l", slash), 0x126: ("H", bar(8, 0, 9)), 0x127: ("h", bar(5, 0, 5)), 0x166: ("T", bar(9, 2, 7)), 0x167: ("t", bar(9, 1, 6)),
               0xD0: ("D", bar(8, 0, 5))}
    for cp, (ch, fn) in special.items():
        if cp not in R.g:
            R.put(cp, over(ch, fn))
            if cp in miss:
                miss.remove(cp)
    R.put_if_free(0x131, dotless(R.g[ord("i")]))
    R.put_if_free(0x237, dotless(R.g[ord("j")]))
    R.section("Greek and Cyrillic letters that are Latin shapes")
    same = {0x391: "A", 0x392: "B", 0x395: "E", 0x396: "Z", 0x397: "H", 0x399: "I", 0x39A: "K", 0x39C: "M", 0x39D: "N", 0x39F: "O",
            0x3A1: "P", 0x3A4: "T", 0x3A5: "Y", 0x3A7: "X", 0x3BF: "o", 0x3BD: "v",
            0x410: "A", 0x412: "B", 0x415: "E", 0x41A: "K", 0x41C: "M", 0x41D: "H", 0x41E: "O", 0x420: "P", 0x421: "C", 0x422: "T", 0x425: "X",
            0x430: "a", 0x435: "e", 0x43E: "o", 0x440: "p", 0x441: "c", 0x443: "y", 0x445: "x"}
    for cp, ch in same.items():
        if cp not in R.g and cp not in R.alias:
            R.same(cp, ord(ch))
    return miss

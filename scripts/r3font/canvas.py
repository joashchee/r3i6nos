"""A 1-bit pixel canvas and the drawing calls every glyph module uses. Origin top left, y down."""
import math

CW, CH = 10, 20          # one text cell
ASCENT, DESCENT = 15, 5  # rows above and below the baseline (capitals are rows 3-14, descenders reach row 18)


class Bmp:
    def __init__(self, w=CW, h=CH):
        self.w, self.h = w, h
        self.p = [[0] * w for _ in range(h)]

    # -- basics
    def copy(self):
        b = Bmp(self.w, self.h)
        b.p = [r[:] for r in self.p]
        return b

    def set(self, x, y, v=1):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.p[y][x] = v
        return self

    def get(self, x, y):
        return self.p[y][x] if 0 <= x < self.w and 0 <= y < self.h else 0

    def rect(self, x, y, w, h, v=1):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set(xx, yy, v)
        return self

    def frame(self, x, y, w, h, t=1, v=1):
        self.rect(x, y, w, t, v).rect(x, y + h - t, w, t, v)
        self.rect(x, y, t, h, v).rect(x + w - t, y, t, h, v)
        return self

    def hline(self, x0, x1, y, t=1, v=1):
        return self.rect(min(x0, x1), y, abs(x1 - x0) + 1, t, v)

    def vline(self, x, y0, y1, t=1, v=1):
        return self.rect(x, min(y0, y1), t, abs(y1 - y0) + 1, v)

    def line(self, x0, y0, x1, y1, t=1, v=1):
        """Bresenham with a t x t brush (its top left on the point)."""
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx + dy
        while True:
            self.rect(x0, y0, t, t, v)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy
        return self

    def poly(self, pts, v=1):
        """Fill a polygon (pixel centres inside, even-odd)."""
        n = len(pts)
        for y in range(self.h):
            yc = y + 0.5
            xs = []
            for i in range(n):
                (xa, ya), (xb, yb) = pts[i], pts[(i + 1) % n]
                if (ya <= yc < yb) or (yb <= yc < ya):
                    xs.append(xa + (yc - ya) * (xb - xa) / (yb - ya))
            xs.sort()
            for i in range(0, len(xs) - 1, 2):
                for x in range(self.w):
                    if xs[i] <= x + 0.5 < xs[i + 1]:
                        self.p[y][x] = v
        return self

    def ellipse(self, cx, cy, rx, ry, v=1, inner=None, quad=None):
        """Filled ellipse (or a ring when inner=(rx2, ry2)) around (cx, cy), pixel centres. quad: a set of 'tl','tr','bl','br'."""
        for y in range(self.h):
            for x in range(self.w):
                px, py = x + 0.5 - cx, y + 0.5 - cy
                if quad is not None:
                    q = ("t" if py < 0 else "b") + ("l" if px < 0 else "r")
                    if q not in quad:
                        continue
                d = (px / rx) ** 2 + (py / ry) ** 2
                if d <= 1.0 and (inner is None or (px / inner[0]) ** 2 + (py / inner[1]) ** 2 > 1.0):
                    self.p[y][x] = v
        return self

    def disc(self, cx, cy, r, v=1):
        return self.ellipse(cx, cy, r, r, v)

    def ring(self, cx, cy, r, t=1, v=1):
        return self.ellipse(cx, cy, r, r, v, inner=(r - t, r - t))

    def art(self, x, y, rows, ink="#", v=1):
        """Blit ASCII art: ink characters set pixels, anything else leaves them."""
        if isinstance(rows, str):
            rows = rows.strip("\n").split("\n")
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch in ink:
                    self.set(x + i, y + j, v)
                elif ch == "~":       # knock out
                    self.set(x + i, y + j, 0)
        return self

    def blit(self, o, x=0, y=0, op="or"):
        for j in range(o.h):
            for i in range(o.w):
                if o.p[j][i]:
                    if op == "or":
                        self.set(x + i, y + j, 1)
                    elif op == "xor":
                        self.set(x + i, y + j, 1 - self.get(x + i, y + j))
                    elif op == "clear":
                        self.set(x + i, y + j, 0)
        return self

    def text(self, x, y, s, font, gap=1, op="or"):
        for ch in s:
            g = font.get(ch)
            if g:
                self.art(x, y, g.split("/") if "/" in g else g) if op == "or" else None
                x += len(g.split("/")[0]) + gap
        return self

    # -- transforms (return new)
    def flip_h(self):
        b = Bmp(self.w, self.h)
        b.p = [r[::-1] for r in self.p]
        return b

    def flip_v(self):
        b = Bmp(self.w, self.h)
        b.p = [r[:] for r in self.p[::-1]]
        return b

    def invert(self):
        b = Bmp(self.w, self.h)
        b.p = [[1 - v for v in r] for r in self.p]
        return b

    def shift(self, dx=0, dy=0):
        b = Bmp(self.w, self.h)
        b.blit(self, dx, dy)
        return b

    def sub(self, x, y, w, h):
        b = Bmp(w, h)
        for j in range(h):
            for i in range(w):
                b.p[j][i] = self.get(x + i, y + j)
        return b

    def halves(self):
        """A 20x20 icon as its two cells."""
        assert self.w == 2 * CW
        return self.sub(0, 0, CW, self.h), self.sub(CW, 0, CW, self.h)

    # -- encodings
    def rows_int(self):
        return [sum(v << (self.w - 1 - i) for i, v in enumerate(r)) for r in self.p]

    def hex(self):
        d = 2 if self.w <= 8 else 4
        return "".join(f"{r:0{d}x}" for r in self.rows_int())

    def count(self):
        return sum(map(sum, self.p))

    def __eq__(self, o):
        return isinstance(o, Bmp) and self.p == o.p

    def __hash__(self):
        return hash(self.hex())

    def show(self):
        return "\n".join("".join("#" if v else "." for v in r) for r in self.p)


def cell():
    return Bmp(CW, CH)


def from_rows(rows, w=CW):
    b = Bmp(w, len(rows))
    for j, r in enumerate(rows):
        b.p[j] = [1 if c in "#X" else 0 for c in r.ljust(w)[:w]]
    return b


def from_ints(rows, w):
    b = Bmp(w, len(rows))
    for j, r in enumerate(rows):
        b.p[j] = [(r >> (w - 1 - i)) & 1 for i in range(w)]
    return b


def icon():
    """A blank 20x20 icon (two cells)."""
    return Bmp(2 * CW, CH)


# a 3x5 micro font for labels inside buttons; rows are joined with '/'
MICRO = {
    "0": "###/#.#/#.#/#.#/###", "1": ".#./##./.#./.#./###", "2": "###/..#/###/#../###",
    "3": "###/..#/###/..#/###", "4": "#.#/#.#/###/..#/..#", "5": "###/#../###/..#/###",
    "6": "###/#../###/#.#/###", "7": "###/..#/.#./.#./.#.", "8": "###/#.#/###/#.#/###",
    "9": "###/#.#/###/..#/###",
    "A": ".#./#.#/###/#.#/#.#", "B": "##./#.#/##./#.#/##.", "C": "###/#../#../#../###",
    "D": "##./#.#/#.#/#.#/##.", "E": "###/#../##./#../###", "F": "###/#../##./#../#..",
    "G": "###/#../#.#/#.#/###", "H": "#.#/#.#/###/#.#/#.#", "I": "###/.#./.#./.#./###",
    "J": "..#/..#/..#/#.#/.#.", "K": "#.#/#.#/##./#.#/#.#", "L": "#../#../#../#../###",
    "M": "#.#/###/###/#.#/#.#", "N": "##./#.#/#.#/#.#/#.#", "O": "###/#.#/#.#/#.#/###",
    "P": "###/#.#/###/#../#..", "Q": "###/#.#/#.#/###/..#", "R": "###/#.#/##./#.#/#.#",
    "S": "###/#../###/..#/###", "T": "###/.#./.#./.#./.#.", "U": "#.#/#.#/#.#/#.#/###",
    "V": "#.#/#.#/#.#/#.#/.#.", "W": "#.#/#.#/###/###/#.#", "X": "#.#/#.#/.#./#.#/#.#",
    "Y": "#.#/#.#/.#./.#./.#.", "Z": "###/..#/.#./#../###",
    "+": ".../.#./###/.#./...", "-": ".../.../###/.../...", ".": ".../.../.../.../.#.",
    "/": "..#/..#/.#./#../#..", "#": "#.#/###/#.#/###/#.#", "_": ".../.../.../.../###", ":": ".../.#./.../.#./...",
    "(": ".#./#../#../#../.#.", ")": ".#./..#/..#/..#/.#.", "<": "..#/.#./#../.#./..#", ">": "#../.#./..#/.#./#..", "!": ".#./.#./.#./.../.#.",
    "?": "##./..#/.#./.../.#.", "%": "#.#/..#/.#./#../#.#", "@": ".../###/#.#/#../.##", "~": ".../.##/##./.../...", "'": ".#./.#./.../.../...", "*": "#.#/.#./###/.#./#.#", "=": ".../###/.../###/...",
}

# a 5x7 digit font: time signatures, superscripts, subscripts
DIG7 = {
    "0": ".###./#...#/#..##/#.#.#/##..#/#...#/.###.", "1": "..#../.##../..#../..#../..#../..#../.###.",
    "2": ".###./#...#/....#/...#./..#../.#.../#####", "3": "####./....#/....#/.###./....#/....#/####.",
    "4": "...#./..##./.#.#./#..#./#####/...#./...#.", "5": "#####/#..../####./....#/....#/#...#/.###.",
    "6": "..##./.#.../#..../####./#...#/#...#/.###.", "7": "#####/....#/...#./..#../.#.../.#.../.#...",
    "8": ".###./#...#/#...#/.###./#...#/#...#/.###.", "9": ".###./#...#/#...#/.####/....#/...#./.##..",
    "+": "...../..#../..#../#####/..#../..#../.....", "-": "...../...../...../#####/...../...../.....",
    "(": "..#../.#.../#..../#..../#..../.#.../..#..", ")": "..#../...#./....#/....#/....#/...#./..#..",
}


def micro(s, gap=1):
    """A string in the 3x5 font as a Bmp."""
    cols = [MICRO[c].split("/") for c in s]
    b = Bmp(sum(3 + gap for _ in cols) - gap, 5)
    x = 0
    for g in cols:
        b.art(x, 0, g)
        x += 3 + gap
    return b


def digits7(s, gap=1):
    cols = [DIG7[c].split("/") for c in s]
    b = Bmp(sum(5 + gap for _ in cols) - gap, 7)
    x = 0
    for g in cols:
        b.art(x, 0, g)
        x += 5 + gap
    return b

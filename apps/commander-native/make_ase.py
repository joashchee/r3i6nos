#!/usr/bin/env python3
"""make_ase.py: Aseprite files (.ase, .aseprite) made in code, for Commander's viewer tests (apps/commander-native/ase.h).

No Aseprite-made file is in the repo and none was read: every file is written here from the public file-format description
(Aseprite's docs/ase-file-specs.md), with original art (gradients and a few coloured squares). Needs only Python 3.

It does two jobs:
  * `python3 apps/commander-native/make_ase.py` writes selftest_ase.h (the files as C arrays, and the pixels a plain composite of each
    frame must give at chosen spots) for `commander --selftest`;
  * `fixtures()` is imported by tests/test_ase.sh (damaged and cut-short files under the sanitizers) and by scripts/corpus_formats.py
    (the corpus file).

The composite here is written from the spec's description of layers, cels, opacity and blend modes, on the sprite model (not by
reading the bytes back), so it is an independent check of the C reader.
"""
import os
import struct
import zlib

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "selftest_ase.h")

# ---- the file format -----------------------------------------------------------------------------------------------------------

L_VISIBLE, L_EDITABLE, L_BACKGROUND = 1, 2, 8
BLEND = {"normal": 0, "multiply": 1, "screen": 2, "overlay": 3, "darken": 4, "lighten": 5, "dodge": 6, "burn": 7, "hardlight": 8,
         "softlight": 9, "difference": 10, "exclusion": 11, "hue": 12, "saturation": 13, "color": 14, "luminosity": 15,
         "addition": 16, "subtract": 17, "divide": 18}


def chunk(kind, data):
    return struct.pack("<IH", 6 + len(data), kind) + data


def string(s):
    b = s.encode()
    return struct.pack("<H", len(b)) + b


class Layer:
    def __init__(self, name, flags=L_VISIBLE | L_EDITABLE, kind=0, level=0, blend="normal", opacity=255, tileset=0):
        self.name, self.flags, self.kind, self.level, self.blend, self.opacity, self.tileset = name, flags, kind, level, blend, opacity, tileset

    def chunk(self):
        d = struct.pack("<HHHHHHB", self.flags, self.kind, self.level, 0, 0, BLEND[self.blend], self.opacity) + b"\0\0\0" + string(self.name)
        if self.kind == 2:
            d += struct.pack("<I", self.tileset)
        return chunk(0x2004, d)


class Cel:
    """kind: 'raw', 'zlib', 'link' or 'tilemap'. pixels: a list (row by row) of RGBA tuples, (grey, alpha) tuples or indexes."""

    def __init__(self, layer, kind, x=0, y=0, opacity=255, w=0, h=0, pixels=None, link=0, z=0):
        self.layer, self.kind, self.x, self.y, self.opacity, self.w, self.h, self.pixels, self.link, self.z = layer, kind, x, y, opacity, w, h, pixels, link, z

    def chunk(self, depth):
        types = {"raw": 0, "link": 1, "zlib": 2, "tilemap": 3}
        d = struct.pack("<HhhBHh", self.layer, self.x, self.y, self.opacity, types[self.kind], self.z) + b"\0" * 5
        if self.kind in ("raw", "zlib"):
            raw = b"".join(bytes(p) if isinstance(p, tuple) else bytes([p]) for p in self.pixels)
            assert len(raw) == self.w * self.h * (depth // 8)
            d += struct.pack("<HH", self.w, self.h) + (raw if self.kind == "raw" else zlib.compress(raw, 9))
        elif self.kind == "link":
            d += struct.pack("<H", self.link)
        else:   # a tilemap cel: the reader only has to skip it, so its tile data is zeros
            d += struct.pack("<HHHIIII", 2, 2, 32, 0x1FFFFFFF, 0x80000000, 0x40000000, 0x20000000) + b"\0" * 10 + b"\0" * 16
        return chunk(0x2005, d)


class Sprite:
    def __init__(self, w, h, depth, transparent=0, header_ncolors=0, layer_opacity_valid=True, old_chunk_counts=False):
        self.w, self.h, self.depth, self.transparent, self.header_ncolors = w, h, depth, transparent, header_ncolors
        self.layer_opacity_valid, self.old_chunk_counts = layer_opacity_valid, old_chunk_counts
        self.layers, self.frames, self.tags = [], [], []     # a frame is {"ms": n, "cels": [Cel], "extra": [bytes]}
        self.palette = {}                                      # index -> (r, g, b, a)
        self.old_palette = False

    def frame(self, ms, cels):
        self.frames.append({"ms": ms, "cels": cels, "extra": []})
        return len(self.frames) - 1

    def to_bytes(self):
        out = b""
        for fi, fr in enumerate(self.frames):
            chunks = []
            if fi == 0:
                if self.tags:
                    t = struct.pack("<H", len(self.tags)) + b"\0" * 8
                    for name, a, b, direction in self.tags:
                        t += struct.pack("<HHBH", a, b, direction, 0) + b"\0" * 6 + b"\x20\x20\x20" + b"\0" + string(name)
                    chunks.append(chunk(0x2018, t))
                if self.palette and not self.old_palette:
                    last = max(self.palette)
                    p = struct.pack("<III", last + 1, 0, last) + b"\0" * 8
                    for i in range(last + 1):
                        p += struct.pack("<HBBBB", 0, *self.palette.get(i, (0, 0, 0, 0)))
                    chunks.append(chunk(0x2019, p))
                elif self.palette:
                    last = max(self.palette)
                    p = struct.pack("<H", 1) + bytes([0, last + 1])
                    for i in range(last + 1):
                        p += bytes(self.palette.get(i, (0, 0, 0, 0))[:3])
                    chunks.append(chunk(0x0004, p))
                chunks += [ly.chunk() for ly in self.layers]
            chunks += fr["extra"]
            chunks += [c.chunk(self.depth) for c in fr["cels"]]
            body = b"".join(chunks)
            n = len(chunks)
            out += struct.pack("<IHHHBBI", 16 + len(body), 0xF1FA, n if (self.old_chunk_counts or n < 0xFFFF) else 0xFFFF,
                               fr["ms"], 0, 0, 0 if self.old_chunk_counts else n) + body
        head = struct.pack("<IHHHHHIHIIBBBBHBBhhHH", 128 + len(out), 0xA5E0, len(self.frames), self.w, self.h, self.depth,
                           1 if self.layer_opacity_valid else 0, 100, 0, 0, self.transparent, 0, 0, 0, self.header_ncolors,
                           1, 1, 0, 0, 16, 16)
        return head + b"\0" * (128 - len(head)) + out

    # ---- the plain composite, from the spec's description (floating point, 0..1) ---------------------------------------------

    def _rgba(self, v, layer):
        if self.depth == 32:
            return v
        if self.depth == 16:
            return (v[0], v[0], v[0], v[1])
        if v == self.transparent and not (layer.flags & L_BACKGROUND):
            return (0, 0, 0, 0)
        return self.palette.get(v, (0, 0, 0, 0))

    def _visible(self, i):
        ly = self.layers[i]
        if not ly.flags & L_VISIBLE:
            return False
        if ly.level == 0:
            return True
        j = i - 1
        while j >= 0 and self.layers[j].level != ly.level - 1:
            j -= 1
        return j >= 0 and self._visible(j)

    def render(self, frame):
        """The frame as rows of (r, g, b, a). Layers the reader does not draw (tilemaps, hue/saturation/colour/luminosity) are left out."""
        canvas = [[(0.0, 0.0, 0.0, 0.0)] * self.w for _ in range(self.h)]
        cels = sorted(self.frames[frame]["cels"], key=lambda c: (c.layer + c.z, c.z))
        for c in cels:
            ly = self.layers[c.layer]
            if ly.kind != 0 or c.kind == "tilemap" or not self._visible(c.layer) or BLEND[ly.blend] in (12, 13, 14, 15):
                continue
            src = c
            if c.kind == "link":
                src = next(o for o in self.frames[c.link]["cels"] if o.layer == c.layer)
            op = (ly.opacity if self.layer_opacity_valid else 255) / 255 * c.opacity / 255
            for j in range(src.h):
                for i in range(src.w):
                    x, y = c.x + i, c.y + j
                    if not (0 <= x < self.w and 0 <= y < self.h):
                        continue
                    r, g, b, a = self._rgba(src.pixels[j * src.w + i], ly)
                    canvas[y][x] = blend(canvas[y][x], (r, g, b), a / 255 * op, ly.blend)
        return [[tuple(int(v * 255 + 0.5) for v in px) for px in row] for row in canvas]


def _fn(mode, cb, cs):
    if mode == "multiply": return cb * cs
    if mode == "screen": return cb + cs - cb * cs
    if mode == "overlay": return _fn("hardlight", cs, cb)
    if mode == "darken": return min(cb, cs)
    if mode == "lighten": return max(cb, cs)
    if mode == "dodge": return 0.0 if cb == 0 else 1.0 if cs >= 1 else min(1.0, cb / (1 - cs))
    if mode == "burn": return 1.0 if cb >= 1 else 0.0 if cs <= 0 else 1 - min(1.0, (1 - cb) / cs)
    if mode == "hardlight": return cb * 2 * cs if cs <= 0.5 else cb + (2 * cs - 1) - cb * (2 * cs - 1)
    if mode == "softlight":
        if cs <= 0.5: return cb - (1 - 2 * cs) * cb * (1 - cb)
        d = ((16 * cb - 12) * cb + 4) * cb if cb <= 0.25 else cb ** 0.5
        return cb + (2 * cs - 1) * (d - cb)
    if mode == "difference": return abs(cb - cs)
    if mode == "exclusion": return cb + cs - 2 * cb * cs
    if mode == "addition": return min(1.0, cb + cs)
    if mode == "subtract": return max(0.0, cb - cs)
    if mode == "divide": return 0.0 if cb == 0 else 1.0 if cs == 0 else min(1.0, cb / cs)
    return cs


def blend(dst, rgb, sa, mode):
    """dst: (r, g, b, a) and rgb in 0..1 (rgb 0..255 in, see render); the W3C compositing formula with non-premultiplied colour."""
    dr, dg, db, da = dst
    if sa <= 0:
        return (dr, dg, db, da)
    cs = [v / 255 for v in rgb]
    if da == 0:
        return (cs[0], cs[1], cs[2], sa)
    ra = sa + da * (1 - sa)
    out = []
    for cb, s in zip((dr, dg, db), cs):
        b = s if mode == "normal" else _fn(mode, cb, s)
        out.append((1 - sa / ra) * cb + (sa / ra) * ((1 - da) * s + da * b))
    return (out[0], out[1], out[2], ra)


# ---- the sprites ---------------------------------------------------------------------------------------------------------------

def solid(w, h, px):
    return [px] * (w * h)


def sprite_rgba():
    """8 x 6, RGBA, three frames, eight layers: what is drawn, what is hidden, and what is skipped."""
    s = Sprite(8, 6, 32)
    s.layers = [
        Layer("Background", L_VISIBLE | L_EDITABLE | L_BACKGROUND),            # 0
        Layer("Art"),                                                          # 1
        Layer("Hidden", L_EDITABLE),                                           # 2: not visible
        Layer("Fx", L_EDITABLE, kind=1),                                       # 3: a hidden group...
        Layer("InFx", level=1),                                                # 4: ...so its visible child is not drawn
        Layer("Tint", blend="multiply", opacity=128),                          # 5
        Layer("Hue", blend="hue"),                                             # 6: not drawn (the blend needs hue/saturation maths)
        Layer("Tiles", kind=2),                                                # 7: a tilemap: not drawn
    ]
    bg = [(x * 30, y * 40, 100, 255) for y in range(6) for x in range(8)]
    art = [((255, 0, 0, 255) if (i + j) % 2 == 0 else (0, 255, 0, 128)) if j < 2 else (0, 0, 0, 0) for j in range(3) for i in range(4)]
    s.frame(100, [Cel(0, "raw", 0, 0, 255, 8, 6, bg), Cel(1, "zlib", 2, 1, 255, 4, 3, art),
                  Cel(2, "raw", 0, 0, 255, 8, 6, solid(8, 6, (255, 0, 0, 255))), Cel(4, "zlib", 0, 0, 255, 8, 6, solid(8, 6, (0, 0, 255, 255))),
                  Cel(6, "zlib", 0, 0, 255, 8, 6, solid(8, 6, (255, 255, 0, 255))), Cel(7, "tilemap", 0, 0, 255, 2, 2)])
    s.frame(200, [Cel(0, "link", link=0), Cel(1, "raw", 3, 2, 128, 4, 3, solid(4, 3, (255, 255, 255, 255))),
                  Cel(5, "zlib", 0, 0, 255, 8, 6, solid(8, 6, (0, 128, 255, 255)))])
    s.frame(150, [Cel(0, "link", link=0), Cel(1, "link", 4, 3, 255, link=0)])
    s.tags = [("walk", 0, 1, 0), ("back", 1, 2, 2)]
    return s


def sprite_gray():
    s = Sprite(4, 4, 16)
    s.layers = [Layer("Gray")]
    s.frame(80, [Cel(0, "raw", 0, 0, 255, 4, 4, [(x * 80, 255 if y < 3 else 128) for y in range(4) for x in range(4)])])
    return s


def sprite_indexed():
    """6 x 4, indexed, the transparent index 0, a palette with one half-transparent colour, a background layer and a screen layer."""
    s = Sprite(6, 4, 8, transparent=0, header_ncolors=4)
    s.palette = {0: (0, 0, 0, 255), 1: (255, 0, 0, 255), 2: (0, 255, 0, 255), 3: (0, 0, 255, 128)}
    s.layers = [Layer("Back", L_VISIBLE | L_EDITABLE | L_BACKGROUND), Layer("Top"), Layer("Glow", blend="screen")]
    s.frame(120, [Cel(0, "zlib", 0, 0, 255, 6, 4, [0 if (x + y) % 2 else 1 for y in range(4) for x in range(6)]),
                  Cel(1, "raw", 1, 1, 255, 4, 2, [0, 2, 3, 0, 2, 2, 0, 3]),
                  Cel(2, "raw", 4, 2, 255, 2, 2, [2, 0, 0, 2])])
    return s


def sprite_oldpal():
    """3 x 2, indexed, the old palette chunk (0x0004) and the old chunk count only; transparent index 255 so every index is opaque."""
    s = Sprite(3, 2, 8, transparent=255, header_ncolors=3, old_chunk_counts=True)
    s.old_palette = True
    s.palette = {0: (255, 0, 0, 255), 1: (0, 255, 0, 255), 2: (0, 0, 255, 255)}
    s.layers = [Layer("Only")]
    s.frame(90, [Cel(0, "raw", 0, 0, 255, 3, 2, [0, 1, 2, 2, 1, 0])])
    return s


def sprite_corpus():
    """The corpus's own sprite (corpus/files/images/sprite.aseprite): 48 x 32 RGBA, a gradient behind a square that moves over four frames,
    a hidden sketch layer and a tag. Small, original, and every part of the reader is used on it."""
    s = Sprite(48, 32, 32)
    s.layers = [Layer("Sky", L_VISIBLE | L_EDITABLE | L_BACKGROUND), Layer("Square"), Layer("Shadow", blend="multiply", opacity=96), Layer("Sketch", L_EDITABLE)]
    sky = [(x * 255 // 47, y * 255 // 31, 160 if (x // 4 + y // 4) % 2 else 40, 255) for y in range(32) for x in range(48)]
    sq = solid(10, 10, (250, 220, 40, 255))
    shadow = solid(10, 4, (0, 0, 0, 255))
    sketch = solid(48, 32, (255, 0, 255, 255))
    for i, (x, y) in enumerate([(4, 4), (16, 14), (28, 20), (34, 8)]):
        cels = [Cel(0, "zlib", 0, 0, 255, 48, 32, sky) if i == 0 else Cel(0, "link", link=0),
                Cel(1, "zlib", x, y, 255, 10, 10, sq), Cel(2, "zlib", x, 28, 255, 10, 4, shadow)]
        if i == 0:
            cels.append(Cel(3, "zlib", 0, 0, 255, 48, 32, sketch))
        s.frame(120, cels)
    s.tags = [("bounce", 0, 3, 2)]
    return s


SPRITES = [("ase_rgba", "sprite.aseprite", sprite_rgba), ("ase_gray", "gray.ase", sprite_gray),
           ("ase_idx", "indexed.ase", sprite_indexed), ("ase_oldpal", "oldpal.ase", sprite_oldpal)]

# the spots the C reader is checked at: (id, frame, x, y)
PROBES = [("ase_rgba", 0, 0, 0), ("ase_rgba", 0, 7, 5), ("ase_rgba", 0, 2, 1), ("ase_rgba", 0, 3, 1), ("ase_rgba", 0, 2, 3), ("ase_rgba", 0, 4, 3),
          ("ase_rgba", 1, 0, 0), ("ase_rgba", 1, 3, 2), ("ase_rgba", 1, 6, 4), ("ase_rgba", 1, 2, 1),
          ("ase_rgba", 2, 2, 1), ("ase_rgba", 2, 4, 3), ("ase_rgba", 2, 5, 3), ("ase_rgba", 2, 7, 5),
          ("ase_gray", 0, 3, 0), ("ase_gray", 0, 1, 3), ("ase_gray", 0, 0, 0),
          ("ase_idx", 0, 0, 0), ("ase_idx", 0, 1, 0), ("ase_idx", 0, 2, 1), ("ase_idx", 0, 3, 1), ("ase_idx", 0, 2, 2), ("ase_idx", 0, 4, 2), ("ase_idx", 0, 5, 3),
          ("ase_oldpal", 0, 0, 0), ("ase_oldpal", 0, 2, 0), ("ase_oldpal", 0, 1, 1)]


def fixtures():
    """[(id, file name, bytes, Sprite)]"""
    return [(cid, fname, make().to_bytes(), make()) for cid, fname, make in SPRITES]


def write_header():
    fx = fixtures()
    by_id = {cid: sp for cid, _, _, sp in fx}
    with open(OUT, "w") as f:
        f.write("/* GENERATED by make_ase.py: do not edit. Aseprite files made in code, and the pixels a plain composite gives, for commander --selftest. */\n")
        f.write("#ifndef COMMANDER_SELFTEST_ASE_H\n#define COMMANDER_SELFTEST_ASE_H\n\n")
        for cid, fname, data, _ in fx:
            f.write("static const unsigned char FX_%s[%d] = {\n" % (cid, len(data)))
            for i in range(0, len(data), 24):
                f.write("    " + ",".join("0x%02x" % c for c in data[i:i + 24]) + ",\n")
            f.write("};\n")
        f.write("\nstatic const Fixture ASE_FIXTURES[] = {\n")
        for cid, fname, data, _ in fx:
            f.write('    {"%s", "%s", FX_%s, %d},\n' % (cid, fname, cid, len(data)))
        f.write("};\n\n/* The composite of frame `frame` at (x, y): r, g, b, a. The C reader must agree to within 1. */\n")
        f.write("typedef struct { const char *id; int frame, x, y, r, g, b, a; } AsePixel;\nstatic const AsePixel ASE_PIXELS[] = {\n")
        for cid, frame, x, y in PROBES:
            r, g, b, a = by_id[cid].render(frame)[y][x]
            f.write('    {"%s", %d, %d, %d, %d, %d, %d, %d},\n' % (cid, frame, x, y, r, g, b, a))
        f.write("};\n\n#endif\n")
    print("wrote", OUT, sum(len(d) for _, _, d, _ in fx), "bytes of fixtures")


if __name__ == "__main__":
    write_header()

"""Stdlib PNG writer and contact-sheet renderer (no Pillow), used for the specimen sheets and the BMFont atlas."""
import struct
import zlib


def png(path, w, h, rows, palette=None):
    """rows: list of h lists of palette indexes (0/1 or more). Writes an 8-bit palette PNG."""
    palette = palette or [(0, 0, 0), (255, 255, 255)]
    raw = b"".join(b"\x00" + bytes(r) for r in rows)

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    plte = b"".join(bytes(c) for c in palette)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))
    data += chunk(b"PLTE", plte) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


def sheet(path, glyphs, cols=32, scale=3, gw=10, gh=20, grid=True, labels=None):
    """glyphs: list of Bmp or None. A contact sheet, `cols` glyphs a row, each scaled and boxed. Palette: 0 bg, 1 ink, 2 grid, 3 empty."""
    n = len(glyphs)
    rws = (n + cols - 1) // cols
    cw, ch = gw * scale + 1, gh * scale + 1
    W, H = cols * cw + 1, rws * ch + 1
    img = [[2 if grid else 0] * W for _ in range(H)]
    for i, g in enumerate(glyphs):
        ox, oy = (i % cols) * cw + 1, (i // cols) * ch + 1
        for y in range(gh * scale):
            row = img[oy + y]
            for x in range(gw * scale):
                row[ox + x] = 0
        if g is None:
            for y in range(gh * scale):
                img[oy + y][ox + 0] = 3
            continue
        for y in range(min(g.h, gh)):
            for x in range(min(g.w, gw)):
                if g.p[y][x]:
                    for yy in range(scale):
                        for xx in range(scale):
                            img[oy + y * scale + yy][ox + x * scale + xx] = 1
    png(path, W, H, img, [(18, 18, 24), (240, 240, 250), (60, 60, 90), (90, 20, 20)])

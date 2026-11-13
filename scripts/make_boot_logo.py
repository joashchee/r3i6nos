#!/usr/bin/env python3
"""make_boot_logo.py: the boot logo as the BMP the bootloader shows (docs/brand/boot-logo.png -> overlay/usr/share/r3i6nos/boot-logo.bmp).

    make_boot_logo.py              write the BMP from the PNG
    make_boot_logo.py --check      fail if the committed BMP is not what the PNG gives (scripts/check.sh runs it)
    make_boot_logo.py PNG BMP      the same for other paths

The PNG is the maintainer's hand-drawn 640x480 art (docs/BRAND.md); it is never redrawn here. The BMP has the stock card's header byte
for byte (ArkOS's boot/logo.bmp: BITMAPINFOHEADER, 640x480, 24 bits, bottom-up, no alpha, 921,656 bytes: the pixel data and two
spare bytes), so the bootloader sees the same kind of file. Standard library only (zlib reads the PNG): an 8-bit RGB or RGBA,
non-interlaced PNG of exactly 640x480 (alpha is laid over black).
"""
import struct
import sys
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PNG = REPO / "docs" / "brand" / "boot-logo.png"
BMP = REPO / "overlay" / "usr" / "share" / "r3i6nos" / "boot-logo.bmp"
W, H = 640, 480


def read_png(data):
    """(width, height, rows of RGB bytes) from an 8-bit RGB/RGBA non-interlaced PNG."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, ihdr = 8, b"", None
    while pos + 8 <= len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if zlib.crc32(kind + body) != struct.unpack(">I", data[pos + 8 + n:pos + 12 + n])[0]:
            raise ValueError("PNG chunk %r has a bad CRC" % kind)
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
        if kind == b"IEND":
            break
    if not ihdr:
        raise ValueError("no IHDR")
    w, h, depth, ctype, _, _, interlace = ihdr
    if depth != 8 or ctype not in (2, 6) or interlace:
        raise ValueError("needs an 8-bit RGB or RGBA PNG that is not interlaced (got depth %d, colour type %d, interlace %d)" % (depth, ctype, interlace))
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * bpp
    if len(raw) != h * (stride + 1):
        raise ValueError("the PNG's pixel data is the wrong length")
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        cur = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = cur[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                cur[i] = (cur[i] + a) & 255
            elif f == 2:
                cur[i] = (cur[i] + b) & 255
            elif f == 3:
                cur[i] = (cur[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                cur[i] = (cur[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
            elif f != 0:
                raise ValueError("unknown PNG filter %d" % f)
        prev = cur
        if bpp == 4:     # alpha over black
            rows.append(bytes(v for x in range(w) for v in (cur[x * 4] * cur[x * 4 + 3] // 255, cur[x * 4 + 1] * cur[x * 4 + 3] // 255, cur[x * 4 + 2] * cur[x * 4 + 3] // 255)))
        else:
            rows.append(bytes(cur))
    return w, h, rows


def to_bmp(png_bytes):
    w, h, rows = read_png(png_bytes)
    if (w, h) != (W, H):
        raise ValueError("the logo must be %dx%d, this is %dx%d" % (W, H, w, h))
    pixels = b"".join(bytes(v for x in range(W) for v in (r[x * 3 + 2], r[x * 3 + 1], r[x * 3])) for r in reversed(rows))   # BGR, bottom row first
    data = pixels + b"\0\0"        # the stock file ends with two spare bytes
    head = b"BM" + struct.pack("<IHHI", 54 + len(data), 0, 0, 54) + struct.pack("<IiiHHIIiiII", 40, W, H, 1, 24, 0, len(pixels) + 2, 3779, 3779, 0, 0)
    return head + data


def main(argv):
    check = "--check" in argv
    paths = [a for a in argv if not a.startswith("--")]
    src, dst = (Path(paths[0]), Path(paths[1])) if len(paths) == 2 else (PNG, BMP)
    bmp = to_bmp(src.read_bytes())
    if check:
        if not dst.exists() or dst.read_bytes() != bmp:
            print("make_boot_logo: %s is not what %s gives (run scripts/make_boot_logo.py)" % (dst, src), file=sys.stderr)
            return 1
        print("boot logo BMP is up to date (%d bytes)" % len(bmp))
        return 0
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(bmp)
    print("wrote %s (%d bytes)" % (dst, len(bmp)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

#!/usr/bin/env python3
"""Make selftest_data.h: one small file of each format Commander's viewer reads, for `commander --selftest`.

Every file is made here in code (pictures drawn from gradients, archives of a few words, a MIDI of four notes), so none of it is
anyone else's work. Needs Python 3, Pillow (for the JPEG, GIF, TGA, PCX and ICO writers) and the `zstd` program (Python 3.9 has no zstd writer); run it
in Debian 12 under Docker:

    docker run --rm -v "$PWD":/src -w /src debian:bookworm bash -c \
        'apt-get update -qq && apt-get install -y -qq python3-pil zstd >/dev/null && python3 apps/commander-native/make_selftest_data.py'

It also writes the pixels Pillow decodes from each picture, so the C decoders are checked against an independent one.
"""
import io
import lzma
import os
import shutil
import sqlite3
import struct
import subprocess
import tarfile
import tempfile
import wave
import zipfile
import zlib

from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "selftest_data.h")
files = []      # (c name, file name, bytes)
pixels = []     # (c name, x, y, r, g, b, a)


def add(cname, fname, data):
    files.append((cname, fname, bytes(data)))


def picture(w, h):
    im = Image.new("RGBA", (w, h))
    for y in range(h):
        for x in range(w):
            im.putpixel((x, y), (x * 255 // max(w - 1, 1), y * 255 // max(h - 1, 1), 128 if (x + y) % 2 else 32, 255))
    return im


def save(im, fmt, **kw):
    b = io.BytesIO()
    im.save(b, fmt, **kw)
    return b.getvalue()


def probe(cname, data, points):
    im = Image.open(io.BytesIO(data)).convert("RGBA")
    for x, y in points:
        pixels.append((cname, x, y) + im.getpixel((x, y)))


def chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)


# ---- pictures ----
rgb = picture(24, 16).convert("RGB")
jpg = save(rgb, "JPEG", quality=92, subsampling=2)  # 4:2:0
add("jpeg_base", "photo.jpg", jpg)
probe("jpeg_base", jpg, [(0, 0), (12, 8), (23, 15)])
add("jpeg_prog", "progressive.jpg", save(rgb, "JPEG", quality=80, progressive=True))
grey = save(rgb.convert("L"), "JPEG", quality=95)
add("jpeg_grey", "grey.jpg", grey)
probe("jpeg_grey", grey, [(5, 5)])

png = save(picture(5, 3), "PNG")
add("png_rgba", "rgba.png", png)
probe("png_rgba", png, [(0, 0), (4, 2)])

# an interlaced 2-bit palette PNG, written by hand (Pillow does not write Adam7)
W, H = 9, 7
pal = [(0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255)]
idx = [[(x + 2 * y) % 4 for x in range(W)] for y in range(H)]
raw = b""
for sx, sy, dx, dy in [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]:
    for y in range(sy, H, dy):
        row = [idx[y][x] for x in range(sx, W, dx)]
        if not row:
            continue
        bits = 0
        out = bytearray()
        for i, v in enumerate(row):
            bits = bits << 2 | v
            if i % 4 == 3:
                out.append(bits)
                bits = 0
        if len(row) % 4:
            out.append(bits << (2 * (4 - len(row) % 4)))
        raw += b"\0" + bytes(out)
ipng = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 2, 3, 0, 0, 1)) + chunk(b"PLTE", bytes(sum(pal, ()))) + \
    chunk(b"tEXt", b"Title\0Adam7 test") + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
add("png_adam7", "interlaced.png", ipng)
probe("png_adam7", ipng, [(0, 0), (8, 6), (3, 1)])

gif = save(picture(10, 6).convert("RGB").quantize(16), "GIF", interlace=True)
add("gif", "pic.gif", gif)
probe("gif", gif, [(9, 5)])
bmp = save(picture(6, 3).convert("RGB").quantize(8), "BMP")
add("bmp8", "pal.bmp", bmp)
probe("bmp8", bmp, [(5, 2)])
tga = save(picture(7, 3), "TGA", compression="tga_rle")
add("tga", "rle.tga", tga)
probe("tga", tga, [(6, 0), (0, 2)])
pcx = save(picture(5, 4).convert("RGB"), "PCX")
add("pcx", "pic.pcx", pcx)
probe("pcx", pcx, [(4, 3)])
ppm = save(picture(3, 2).convert("RGB"), "PPM")
add("ppm", "pic.ppm", ppm)
probe("ppm", ppm, [(2, 1)])
ico = save(picture(16, 16), "ICO", sizes=[(16, 16)])
add("ico", "icon.ico", ico)
probe("ico", ico, [(15, 0)])

# QOI by hand: every pixel as QOI_OP_RGBA
q = b"qoif" + struct.pack(">IIBB", 2, 2, 4, 0)
qp = [(10, 20, 30, 255), (200, 100, 50, 255), (0, 0, 0, 0), (255, 255, 255, 128)]
for p in qp:
    q += b"\xff" + bytes(p)
q += b"\0" * 7 + b"\1"
add("qoi", "pic.qoi", q)

# ---- sound and music ----
b = io.BytesIO()
w = wave.open(b, "wb")
w.setnchannels(1)
w.setsampwidth(2)
w.setframerate(8000)
w.writeframes(b"\0\0" * 400)  # 0.05 s
w.close()
add("wav", "beep.wav", b.getvalue())


def vlq(n):
    out = [n & 127]
    n >>= 7
    while n:
        out.append(128 | (n & 127))
        n >>= 7
    return bytes(reversed(out))


trk = b"\0\xff\x03" + vlq(9) + b"Test song" + b"\0\xff\x51\x03\x07\xa1\x20"
for i, note in enumerate([60, 64, 67, 72]):
    trk += b"\0" + bytes([0x90, note, 100]) + vlq(480) + bytes([0x80, note, 0])
trk += b"\0\xff\x05" + vlq(5) + b"la la" + b"\0\xff\x2f\0"
add("midi", "song.mid", b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480) + b"MTrk" + struct.pack(">I", len(trk)) + trk)

mod = bytearray(1084 + 64 * 4 * 4)
mod[0:20] = b"commander test".ljust(20, b"\0")
mod[20:42] = b"bass drum".ljust(22, b"\0")
mod[950] = 1
mod[1080:1084] = b"M.K."
add("mod", "tune.mod", mod)

id3 = b"TIT2" + struct.pack(">I", 10) + b"\0\0" + b"\0Mp3 title"
mp3 = b"ID3\3\0\0" + bytes([0, 0, 0, len(id3)]) + id3 + b"\xff\xfb\x90\x64" + b"\0" * 413
add("mp3", "song.mp3", mp3)

si = struct.pack(">HH", 4096, 4096) + b"\0\0\0" * 2 + bytes([0x0A, 0xC4, 0x42, 0xF0]) + struct.pack(">I", 44100) + b"\0" * 16
vc = struct.pack("<I", 4) + b"test" + struct.pack("<I", 1) + struct.pack("<I", 12) + b"TITLE=Flacky"
add("flac", "song.flac", b"fLaC" + bytes([0]) + len(si).to_bytes(3, "big") + si + bytes([0x84]) + len(vc).to_bytes(3, "big") + vc)

# ---- archives and documents ----
b = io.BytesIO()
with zipfile.ZipFile(b, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("[Content_Types].xml", "<Types/>")
    z.writestr("docProps/core.xml", "<cp:coreProperties><dc:title>Docx title</dc:title><dc:creator>Commander</dc:creator></cp:coreProperties>")
    z.writestr("word/document.xml", "<w:document><w:body><w:p><w:r><w:t>Hello from a docx</w:t></w:r></w:p>"
               "<w:p><w:r><w:t>Second &amp; last</w:t></w:r></w:p></w:body></w:document>")
add("docx", "letter.docx", b.getvalue())

b = io.BytesIO()
with tarfile.open(fileobj=b, mode="w:gz", format=tarfile.USTAR_FORMAT) as t:
    for name, text in [("game/main.lua", b"print('hi')\n"), ("game/readme.txt", b"read me\n" * 10)]:
        ti = tarfile.TarInfo(name)
        ti.size = len(text)
        ti.mtime = 1700000000
        t.addfile(ti, io.BytesIO(text))
add("targz", "game.tar.gz", b.getvalue())

gz = io.BytesIO()
import gzip
with gzip.GzipFile(filename="notes.txt", mode="wb", fileobj=gz, mtime=1700000000) as g:
    g.write(b"gzip says hello\n" * 4)
add("gz", "notes.txt.gz", gz.getvalue())

def zstd(data):
    return subprocess.run([shutil.which("zstd") or "zstd", "-q", "-19", "-c"], input=data, stdout=subprocess.PIPE, check=True).stdout


def tar_bytes(members):
    t = io.BytesIO()
    with tarfile.open(fileobj=t, mode="w", format=tarfile.USTAR_FORMAT) as tf:
        for name, text in members:
            ti = tarfile.TarInfo(name)
            ti.size = len(text)
            ti.mtime = 1700000000
            tf.addfile(ti, io.BytesIO(text))
    return t.getvalue()


GAME = [("game/main.lua", b"print('hi')\n"), ("game/readme.txt", b"read me\n" * 10)]
# archives to pick members from: a ZIP holding a folder, a TAR and a text file; a TAR with many members and a very long name
inner = io.BytesIO()
with zipfile.ZipFile(inner, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("inner/", b"")
    z.writestr("inner/game.tar.gz", [x for x in files if x[0] == "targz"][0][2])
    z.writestr("hello.txt", b"hello from inside a zip\n")
def cpio_newc(members):
    out = b""
    def ent(name, mode, data, ino):
        nm = name.encode() + b"\0"
        h = b"070701" + b"".join(b"%08X" % v for v in (ino, mode, 0, 0, 1, 1700000000, len(data), 0, 0, 0, 0, len(nm), 0))
        r = h + nm
        r += b"\0" * (-len(r) % 4)
        r += data
        r += b"\0" * (-len(r) % 4)
        return r
    for i, (name, mode, data) in enumerate(members):
        out += ent(name, mode, data, i + 1)
    return out + ent("TRAILER!!!", 0, b"", 0)


add("cpio_newc", "initrd.cpio", cpio_newc([("bin", 0o40755, b""), ("bin/hello", 0o100755, b"#!/bin/sh\necho hello from cpio\n"), ("etc.txt", 0o100644, b"cpio etc\n" * 3)]))
add("zip_nested", "bundle.zip", inner.getvalue())
LONG = "deep/" * 12 + "this-is-a-very-long-member-name.txt"
add("tar_many", "many.tar.gz", gzip.compress(tar_bytes([("many/f%02d.txt" % i, b"file number %d\n" % i) for i in range(40)] + [(LONG, b"long name\n")]), mtime=1700000000))

add("tarxz", "game.tar.xz", lzma.compress(tar_bytes(GAME), format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC32))
add("tarzst", "game.tar.zst", zstd(tar_bytes(GAME)))
add("xz", "notes.txt.xz", lzma.compress(b"xz says hello\n" * 6, format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC64))
add("zst", "notes.txt.zst", zstd(b"zstd says hello\n" * 6))


def ar_bytes(members):
    out = b"!<arch>\n"
    for name, data in members:
        out += (name + "/").ljust(16).encode() + b"1700000000  0     0     100644  " + str(len(data)).ljust(10).encode() + b"`\n" + data
        if len(data) % 2:
            out += b"\n"
    return out


CONTROL = b"Package: hello-test\nVersion: 1.2-3\nArchitecture: arm64\nMaintainer: Nobody <no@example.com>\nDescription: a test package\n"
DATA = [("./usr/bin/hello", b"#!/bin/sh\necho hello\n"), ("./usr/share/doc/hello/README", b"hello docs\n" * 5)]
add("deb_xz", "hello-test_1.2-3_arm64.deb", ar_bytes([("debian-binary", b"2.0\n"),
    ("control.tar.xz", lzma.compress(tar_bytes([("./control", CONTROL)]), format=lzma.FORMAT_XZ)),
    ("data.tar.xz", lzma.compress(tar_bytes(DATA), format=lzma.FORMAT_XZ))]))
add("deb_zst", "hello-test_1.2-3_arm64_zst.deb", ar_bytes([("debian-binary", b"2.0\n"),
    ("control.tar.zst", zstd(tar_bytes([("./control", CONTROL)]))),
    ("data.tar.zst", zstd(tar_bytes(DATA)))]))

with tempfile.TemporaryDirectory() as d:
    db = os.path.join(d, "t.db")
    c = sqlite3.connect(db)
    c.execute("PRAGMA page_size=512")
    c.execute("CREATE TABLE songs (title TEXT, plays INT)")
    c.execute("CREATE INDEX by_plays ON songs(plays)")
    c.commit()
    c.close()
    add("sqlite", "music.db", open(db, "rb").read())

content2 = zlib.compress(b"BT /F1 12 Tf 72 700 Td (Second page) Tj ET")
pdf = (b"%PDF-1.4\n1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
       b"2 0 obj << /Type /Pages /Count 2 /Kids [3 0 R 5 0 R] >> endobj\n"
       b"3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R >> endobj\n"
       b"4 0 obj << /Length 44 >>\nstream\nBT /F1 12 Tf 72 712 Td [(Hello) -300 (PDF)] TJ ET\nendstream endobj\n"
       b"5 0 obj << /Type /Page /Parent 2 0 R /Contents 6 0 R >> endobj\n"
       b"6 0 obj << /Length " + str(len(content2)).encode() + b" /Filter /FlateDecode >>\nstream\n" + content2 + b"\nendstream endobj\n"
       b"7 0 obj << /Title (PDF title) /Author <FEFF0041006E006E> >> endobj\n"
       b"trailer << /Root 1 0 R /Info 7 0 R >>\n%%EOF\n")
add("pdf", "doc.pdf", pdf)
add("rtf", "doc.rtf", b"{\\rtf1\\ansi{\\fonttbl{\\f0 Arial;}}{\\info{\\title RTF title}}\\f0 Hello \\b RTF\\b0\\par Caf\\'e9\\par}")
add("html", "page.html", b"<!DOCTYPE html><html><head><title>Page title</title><style>p{}</style></head>"
                         b"<body><h1>Heading</h1><p>One &amp; two</p><script>x()</script></body></html>")

# ---- a disk image: MBR, one FAT32 partition ----
img = bytearray(4 * 512)
img[446:462] = bytes([0x80, 0, 0, 0, 0x0C, 0, 0, 0]) + struct.pack("<II", 1, 3)
img[510:512] = b"\x55\xaa"
bs = 512
img[bs + 82:bs + 90] = b"FAT32   "
img[bs + 71:bs + 82] = b"R36S CARD  "
img[bs + 510:bs + 512] = b"\x55\xaa"
add("mbr", "card.img", img)

# ---- text art ----
add("ansi", "logo.ans", b"\x1b[0;1;33mHI\x1b[0;44m \x1b[31mX\r\n\x1b[2CZ\x1b[0m")
xb = b"XBIN\x1a" + struct.pack("<HH", 3, 1) + bytes([16, 0]) + bytes([ord("A"), 0x1E, ord("B"), 0x2F, ord("C"), 0x4A])
add("xbin", "art.xb", xb)

with open(OUT, "w") as f:
    f.write("/* GENERATED by make_selftest_data.py: do not edit. Small files of each format, made in code, for commander --selftest. */\n")
    f.write("#ifndef COMMANDER_SELFTEST_DATA_H\n#define COMMANDER_SELFTEST_DATA_H\n\n")
    for cname, fname, data in files:
        f.write("static const unsigned char FX_%s[%d] = {\n" % (cname, len(data)))
        for i in range(0, len(data), 24):
            f.write("    " + ",".join("0x%02x" % c for c in data[i:i + 24]) + ",\n")
        f.write("};\n")
    f.write("\ntypedef struct { const char *id, *name; const unsigned char *data; unsigned len; } Fixture;\n")
    f.write("static const Fixture FIXTURES[] = {\n")
    for cname, fname, data in files:
        f.write('    {"%s", "%s", FX_%s, %d},\n' % (cname, fname, cname, len(data)))
    f.write("};\n\n/* What Pillow decodes at (x, y): the C decoders must agree (JPEG within a small rounding margin). */\n")
    f.write("typedef struct { const char *id; int x, y, r, g, b, a; } FixturePixel;\nstatic const FixturePixel FIXTURE_PIXELS[] = {\n")
    for p in pixels:
        f.write('    {"%s", %d, %d, %d, %d, %d, %d},\n' % p)
    f.write("};\n\n#endif\n")
print("wrote", OUT, sum(len(d) for _, _, d in files), "bytes of fixtures")

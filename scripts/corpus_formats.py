#!/usr/bin/env python3
"""corpus_formats.py - the files of r3i6nos's test corpus that exercise Commander's viewer (corpus/README.md, scripts/build_corpus.sh).

    corpus_formats.py <out folder>        write one small file of every format Commander identifies into <out>/<sub-folder>/

Every file is made here in code (pictures drawn from gradients, archives of a few words, a MIDI of four notes, a font of three squares), so none
of it is anyone else's work (CLAUDE.md rule 2) and a rebuild is byte for byte the same. It needs Python 3 and, for the encoders Python lacks, programs
that only exist in the Debian 12 container scripts/build_corpus.sh runs it in: Pillow (JPEG, GIF, TIFF, WebP, ICO, PCX, TGA), ffmpeg (audio and video),
p7zip-full (7z), bzip2, lzip, zstd, xz-utils, genisoimage (ISO), lua5.4 (luac) and gcc (a program). The same container then opens each file with
Commander (`commander --info`) and fails unless its Type line is what REGISTRY says.

The folders: music/ audio/ images/ art/ video/ documents/ text/ data/ archives/ disks/ programs/ fonts/ models/. Pictures drawn here are 48 x 32,
sounds are about a second, videos two seconds at 64 x 48, so the whole set stays small.
"""
import binascii
import gzip
import io
import json
import lzma
import os
import shutil
import sqlite3
import struct
import subprocess
import sys
import tarfile
import tempfile
import wave
import zipfile
import zlib
from pathlib import Path

OUT = None
REGISTRY = {}   # "folder/file": (the Type line Commander must give, what it is)
MTIME = 1794182400          # 2026-11-09 00:00 UTC: fixed, so a rebuild is identical
STAMP = (2026, 11, 9, 0, 0, 0)


def put(path, data, label, what):
    """Writes OUT/path and registers the Type line Commander must answer with."""
    p = OUT / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(bytes(data))
    REGISTRY[path] = (label, what)


def tool(name):
    t = shutil.which(name)
    if not t:
        sys.exit(f"corpus_formats: needs {name} (run scripts/build_corpus.sh, which uses the Debian 12 container)")
    return t


def run(cmd, **kw):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kw)
    if r.returncode:
        sys.exit(f"corpus_formats: {' '.join(map(str, cmd[:3]))} ... failed: {r.stderr.decode(errors='replace')[-600:]}")
    return r.stdout


def tmp():
    return tempfile.TemporaryDirectory(prefix="r3i6nos-corpus.")


# ---- pictures -------------------------------------------------------------------------------------------------------------------------

def picture(w=48, h=32):
    from PIL import Image
    im = Image.new("RGBA", (w, h))
    for y in range(h):
        for x in range(w):
            im.putpixel((x, y), (x * 255 // (w - 1), y * 255 // (h - 1), 160 if (x // 4 + y // 4) % 2 else 40, 255))
    return im


def save(im, fmt, **kw):
    b = io.BytesIO()
    im.save(b, fmt, **kw)
    return b.getvalue()


def png_chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)


def images():
    from PIL import Image
    rgba = picture()
    rgb = rgba.convert("RGB")
    put("images/photo.png", save(rgba, "PNG", optimize=True), "PNG image", "RGBA, 48 x 32")
    # an interlaced 2-bit palette PNG written by hand (Pillow does not write Adam7), with a text chunk
    W, H = 9, 7
    pal = [(0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255)]
    idx = [[(x + 2 * y) % 4 for x in range(W)] for y in range(H)]
    raw = b""
    for sx, sy, dx, dy in [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]:
        for y in range(sy, H, dy):
            row = [idx[y][x] for x in range(sx, W, dx)]
            if not row:
                continue
            bits, out = 0, bytearray()
            for i, v in enumerate(row):
                bits = bits << 2 | v
                if i % 4 == 3:
                    out.append(bits)
                    bits = 0
            if len(row) % 4:
                out.append(bits << (2 * (4 - len(row) % 4)))
            raw += b"\0" + bytes(out)
    put("images/interlaced-palette.png", b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 2, 3, 0, 0, 1)) + png_chunk(b"PLTE", bytes(sum(pal, ())))
        + png_chunk(b"tEXt", b"Title\0Adam7 test") + png_chunk(b"IDAT", zlib.compress(raw, 9)) + png_chunk(b"IEND", b""), "PNG image", "2-bit palette, Adam7")
    put("images/photo.jpg", save(rgb, "JPEG", quality=90, subsampling=2), "JPEG image", "baseline, 4:2:0")
    put("images/progressive.jpg", save(rgb, "JPEG", quality=85, progressive=True), "JPEG image", "progressive (drawn through ffmpeg)")
    put("images/grey.jpg", save(rgb.convert("L"), "JPEG", quality=90), "JPEG image", "greyscale")
    put("images/animation.gif", save(rgb.quantize(32), "GIF", interlace=True), "GIF image", "32 colours, interlaced")
    put("images/palette.bmp", save(rgb.quantize(16), "BMP"), "BMP image", "4-bit palette")
    put("images/truecolour.bmp", save(rgb, "BMP"), "BMP image", "24-bit")
    put("images/icon.ico", save(picture(32, 32), "ICO", sizes=[(32, 32), (16, 16)]), "ICO icon", "two sizes")
    put("images/rle.tga", save(rgba, "TGA", compression="tga_rle"), "Targa image", "32-bit RLE")
    put("images/photo.pcx", save(rgb, "PCX"), "PCX image", "24-bit")
    put("images/photo.ppm", save(rgb, "PPM"), "Portable pixmap", "P6")
    put("images/grey.pgm", save(rgb.convert("L"), "PPM"), "Portable greymap", "P5")
    put("images/bilevel.pbm", save(rgb.convert("1"), "PPM"), "Portable bitmap", "P4")
    put("images/ascii.pnm", b"P3\n# a pixmap in text\n4 2\n255\n" + b"255 0 0  0 255 0  0 0 255  255 255 0\n0 255 255  255 0 255  128 128 128  0 0 0\n", "Portable anymap", "P3, text")
    put("images/photo.tiff", save(rgb, "TIFF", compression="tiff_lzw"), "TIFF image", "LZW (drawn through ffmpeg)")
    put("images/photo.webp", save(rgb, "WEBP", lossless=True), "WebP image", "lossless (drawn through ffmpeg)")
    put("images/lossy.webp", save(rgb, "WEBP", quality=80), "WebP image", "lossy (drawn through ffmpeg)")
    # a QOI by hand: every pixel as QOI_OP_RGBA
    q = b"qoif" + struct.pack(">IIBB", 2, 2, 4, 0)
    for p in [(10, 20, 30, 255), (200, 100, 50, 255), (0, 0, 0, 0), (255, 255, 255, 128)]:
        q += b"\xff" + bytes(p)
    put("images/pic.qoi", q + b"\0" * 7 + b"\1", "QOI image", "2 x 2 RGBA")
    # an Aseprite sprite, written by apps/commander-native/make_ase.py (also what the viewer's tests use): four frames, four layers, a tag
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "apps" / "commander-native"))
    import make_ase
    put("images/sprite.aseprite", make_ase.sprite_corpus().to_bytes(), "Aseprite sprite", "48 x 32 RGBA, 4 frames, 4 layers, a tag")
    # a Photoshop file, raw RGB, planes of three values
    w, h = 32, 24
    psd = b"8BPS\0\1" + b"\0" * 6 + struct.pack(">HIIHH", 3, h, w, 8, 3) + b"\0" * 12 + b"\0\0"
    for v in (0x40, 0x80, 0xC0):
        psd += bytes([v]) * (w * h)
    put("images/layers.psd", psd, "Photoshop document", "raw RGB (drawn through ffmpeg)")
    # HEIC and AVIF: only the file-type box, which is all Commander reads (it describes them; it does not draw them)
    put("images/stub.heic", struct.pack(">I", 24) + b"ftypheic" + b"\0\0\0\0" + b"heicmif1", "HEIC/HEIF image", "the ftyp box only: not a picture")
    put("images/stub.avif", struct.pack(">I", 24) + b"ftypavif" + b"\0\0\0\0" + b"avifmif1", "AVIF image", "the ftyp box only: not a picture")
    put("images/vector.svg", b'<svg xmlns="http://www.w3.org/2000/svg" width="64" height="48" viewBox="0 0 64 48"><title>r3i6nos test</title>'
        b'<rect width="64" height="48" fill="#204060"/><circle cx="32" cy="24" r="14" fill="#f0c040"/></svg>\n', "SVG image", "64 x 48")


# ---- text-mode art ----------------------------------------------------------------------------------------------------------------------

def sauce(title, author, group, datatype, filetype, w, h, flags=0, size=0):
    rec = b"SAUCE00" + title.encode().ljust(35)[:35] + author.encode().ljust(20)[:20] + group.encode().ljust(20)[:20] + b"20261109"
    rec += struct.pack("<IBBHHHHB", size, datatype, filetype, w, h, 0, 0, 0) + bytes([flags]) + b"\0" * 22
    return b"\x1a" + rec[:128] if len(rec) >= 128 else b"\x1a" + rec.ljust(128, b"\0")


def vga_palette6():
    pal = []
    for i in range(16):
        r, g, b = 42 * ((i >> 2) & 1) + 21 * (i >> 3), 42 * ((i >> 1) & 1) + 21 * (i >> 3), 42 * (i & 1) + 21 * (i >> 3)
        if i == 6:
            g = 21
        pal += [r, g, b]
    return bytes(pal)


def noise_font(rows=16):
    """256 glyphs of 8 x rows, made from the character code (a font of patterns: it only has to be a font)."""
    out = bytearray()
    for ch in range(256):
        for y in range(rows):
            out.append(((ch * 37 + y * 91) ^ (ch >> 1) ^ (y * 17)) & 0xFF if 32 <= ch < 127 else 0)
    return bytes(out)


def art_cells(text_rows):
    out = bytearray()
    for r, row in enumerate(text_rows):
        for c, ch in enumerate(row):
            out += bytes([ord(ch), ((r + c) % 7 + 1) | ((c % 8) << 4)])
    return bytes(out)


def art():
    ansi = (b"\x1b[0;1;33mr3i6nos \x1b[0;44;37m art \x1b[0m\r\n\x1b[1;31m\xdb\xdb\xb2\xb1\xb0 \x1b[0;32m\xc9\xcd\xcd\xbb\x1b[0m\r\n\x1b[2C\x1b[1;36mtest corpus\x1b[0m\r\n")
    put("art/logo.ans", ansi + sauce("Test ANSI", "r3i6nos", "corpus", 1, 1, 80, 3, 0, len(ansi)), "ANSI art", "SAUCE, 80 columns")
    ice = b"\x1b[5;31;44miCE blink as colour\x1b[0m\r\n\x1b[0;1;42;30m bright backgrounds \x1b[0m\r\n"
    put("art/colours.ice", ice + sauce("iCE colours", "r3i6nos", "corpus", 1, 1, 80, 2, 1, len(ice)), "ANSI art (iCE colours)", "SAUCE flag: iCE colours")
    nfo = ("\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\xdb\r\n  r3i6nos test corpus\r\n  \xb0\xb1\xb2\xdb release info \xdb\xb2\xb1\xb0\r\n").encode("latin-1")
    put("art/release.nfo", nfo, "NFO (release info, often ANSI art)", "code page 437 blocks")
    put("art/file_id.diz", b"r3i6nos test corpus\r\nFILE_ID.DIZ description\r\n  one small file of each format\r\n", "FILE_ID.DIZ description", "text")
    put("art/screen.bin", art_cells(["BIN: character and colour pairs".ljust(80), "no escape codes, 160 bytes a row".ljust(80), "".join(chr(0xB0 + n % 4) for n in range(80)), "end".ljust(80)]), "Binary text art (BIN)", "80 columns by 4 rows")
    xb = b"XBIN\x1a" + struct.pack("<HH", 12, 2) + bytes([16, 0]) + art_cells(["XBin art 12 c", "no palette    "])[:48]
    put("art/plain.xb", xb, "XBin art", "default palette and font")
    xbp = b"XBIN\x1a" + struct.pack("<HH", 8, 2) + bytes([16, 1 | 2 | 8]) + vga_palette6() + noise_font() + art_cells(["XBin pal", "and font"])
    put("art/palette-font.xb", xbp, "XBin art", "own palette, own font, iCE colours")
    put("art/artworx.adf", b"\x01" + vga_palette6() + b"\0" * (192 - 48) + noise_font() + art_cells(["ArtWorx ADF " + "x" * 12] * 4)[:0] + b"".join(art_cells(["ADF art".ljust(80, ".")]) for _ in range(4)), "ArtWorx art (ADF)", "palette, font, 80 x 4")
    idf_data = b"".join(art_cells(["iCE Draw".ljust(40, ".")]) for _ in range(3))
    put("art/icedraw.idf", b"\x041.4" + struct.pack("<HHHH", 0, 0, 39, 2) + idf_data + noise_font() + vga_palette6(), "iCE Draw art (IDF)", "40 x 3, font, palette")
    put("art/tundra.tnd", b"\x18TUNDRA24" + b"\x01\x00\x00\x00\x00" + b"\x06\x00\x00\x00\x01" + b"T" + b"\x00\xaa\xaa\xaa\x00\x00\x00\x00\x00", "TundraDraw art", "header and a few commands")
    put("art/readme.asc", b"ASCII art in a plain text file\n   /\\_/\\\n  ( o.o )\n   > ^ <\n", "ASCII text or art", "text")


# ---- audio ------------------------------------------------------------------------------------------------------------------------------

BIT = ["-fflags", "+bitexact", "-flags:v", "+bitexact", "-flags:a", "+bitexact", "-map_metadata", "-1"]


def ffmpeg(inputs, args, ext, threads=True):
    """Runs ffmpeg into a temporary file with the options that make its output the same every time; returns the bytes."""
    with tmp() as d:
        out = os.path.join(d, "o." + ext)
        cmd = [tool("ffmpeg"), "-v", "error", "-y"] + inputs + BIT + args + (["-threads", "1"] if threads else []) + [out]
        run(cmd)
        return Path(out).read_bytes()


TONE = ["-f", "lavfi", "-i", "aevalsrc=0.45*sin(2*PI*(440+220*floor(t*4))*t)+0.2*sin(2*PI*660*t):d=1:s=22050:c=stereo"]


def audio():
    put("audio/tone.wav", ffmpeg(TONE, ["-c:a", "pcm_s16le"], "wav"), "WAV audio", "PCM 16-bit stereo, 22.05 kHz")
    put("audio/tone.aiff", ffmpeg(TONE, ["-c:a", "pcm_s16be"], "aiff"), "AIFF audio", "PCM 16-bit")
    put("audio/tone.flac", ffmpeg(TONE, ["-c:a", "flac"], "flac"), "FLAC audio", "")
    put("audio/tone.mp3", ffmpeg(TONE, ["-c:a", "libmp3lame", "-b:a", "64k", "-id3v2_version", "3", "-metadata", "title=Corpus tone"], "mp3"), "MP3 audio", "64 kbit/s")
    put("audio/vorbis.ogg", ffmpeg(TONE, ["-c:a", "libvorbis", "-q:a", "2"], "ogg"), "Ogg Vorbis audio", "")
    put("audio/opus.opus", ffmpeg(TONE, ["-c:a", "libopus", "-b:a", "48k"], "opus"), "Opus audio", "")
    put("audio/tone.wv", ffmpeg(TONE, ["-c:a", "wavpack"], "wv"), "WavPack audio", "")
    put("audio/tone.m4a", ffmpeg(TONE, ["-c:a", "aac", "-b:a", "64k"], "m4a"), "MPEG-4 audio (M4A)", "AAC")
    put("audio/tone.wma", ffmpeg(TONE, ["-c:a", "wmav2", "-b:a", "64k"], "wma"), "Windows Media (ASF)", "WMA v2")


# ---- music: the formats Neumetik reads that nothing else here writes (headers and names only: Info shows titles and instruments) ----------

def music_stubs():
    title = lambda s, n: s.encode().ljust(n, b"\0")[:n]
    # MultiTracker: 66-byte header, one sample, no tracks
    mtm = b"MTM\x10" + title("MultiTracker test", 20) + struct.pack("<HBBHBBBB", 0, 0, 0, 0, 1, 0, 64, 4) + bytes(32)
    mtm += title("bass sample", 22) + struct.pack("<IIIBBB", 0, 0, 0, 0, 64, 0) + bytes(128)
    put("music/trackers/multitracker.mtm", mtm, "MultiTracker module (MTM)", "header and one sample name")
    # Scream Tracker 2: 48-byte header, 31 sample slots
    stm = title("Scream Tracker 2 test", 20) + b"!Scream!" + b"\x1a\x02\x02\x15\x60\x01\x40" + bytes(13)
    stm += b"".join(title("inst%02d.smp" % i, 12) + bytes(20) for i in range(31))
    put("music/trackers/screamtracker2.stm", stm, "Scream Tracker 2 module (STM)", "header and 31 sample slots")
    # Composer 669: "if", a 108-byte message, counts, 128 orders, tempos and breaks
    s669 = b"if" + title("Composer 669 test message", 108) + bytes([0, 1, 0]) + bytes(128) + bytes([4] * 128) + bytes(128)
    put("music/trackers/composer.669", s669, "Composer 669 module", "header only")
    put("music/trackers/octamed.med", b"MMD0" + struct.pack(">I", 52) + bytes(48), "OctaMED module (MMD0)", "header only")
    put("music/trackers/oktalyzer.okt", b"OKTASONG" + b"CMOD" + struct.pack(">I", 8) + bytes([0, 1, 0, 1, 1, 0, 0, 1]) + b"SAMP" + struct.pack(">I", 0), "Oktalyzer module", "header and two chunks")
    put("music/trackers/hively.hvl", b"HVL\x01" + bytes(60), "HivelyTracker module", "header only")
    put("music/trackers/digibooster.dbm", b"DBM0" + bytes(60), "DigiBooster Pro module", "header only")
    put("music/trackers/polytracker.ptm", title("PolyTracker test", 28) + bytes(16) + b"PTMF" + bytes(60), "PolyTracker module (PTM)", "header only")
    put("music/trackers/farandole.far", b"FAR\xfe" + title("Farandole test", 40) + bytes(40), "Farandole module", "header only")
    put("music/trackers/ultratracker.ult", b"MAS_UTrack_V004" + title("UltraTracker test", 32) + bytes(40), "UltraTracker module", "header only")
    put("music/trackers/dsmi.amf", b"AMF\x0e" + title("DSMI test", 32) + bytes(40), "DSMI module (AMF)", "header only")
    psid = b"PSID" + struct.pack(">HHHHHHI", 2, 0x7C, 0x1000, 0x1003, 0x1006, 1, 1) + title("C64 test tune", 32) + title("r3i6nos", 32) + title("2026 corpus", 32) + bytes(4) + bytes(8)
    put("music/trackers/c64.sid", psid, "C64 SID tune", "PSID v2 header")
    nsf = b"NESM\x1a\x01\x01\x01" + struct.pack("<HHH", 0x8000, 0x8000, 0x8003) + title("NES test tune", 32) + title("r3i6nos", 32) + title("2026 corpus", 32) + bytes(20)
    put("music/trackers/nes.nsf", nsf, "NES sound file", "header only")


# ---- video: made by ffmpeg from its own test pattern ---------------------------------------------------------------------------------------

VID = ["-f", "lavfi", "-i", "testsrc2=s=64x48:r=15:d=2", "-f", "lavfi", "-i", "aevalsrc=0.4*sin(2*PI*330*t):d=2:s=22050:c=mono"]


def video():
    h264 = ["-c:v", "libx264", "-preset", "veryfast", "-crf", "30", "-pix_fmt", "yuv420p", "-x264-params", "threads=1:bframes=0", "-c:a", "aac", "-b:a", "48k", "-shortest"]
    put("video/clip.mp4", ffmpeg(VID, h264 + ["-movflags", "+faststart"], "mp4"), "MPEG-4 video", "H.264 + AAC, 64 x 48, 2 s")
    put("video/clip.mov", ffmpeg(VID, h264, "mov", ), "QuickTime video", "H.264 + AAC in a QuickTime file")
    put("video/clip.avi", ffmpeg(VID, ["-c:v", "mpeg4", "-q:v", "6", "-c:a", "libmp3lame", "-b:a", "48k", "-shortest"], "avi"), "AVI video", "MPEG-4 + MP3")
    put("video/clip.mkv", ffmpeg(VID, h264, "mkv"), "Matroska/WebM video", "H.264 + AAC in Matroska")
    put("video/clip.webm", ffmpeg(VID, ["-c:v", "libvpx", "-b:v", "100k", "-deadline", "good", "-cpu-used", "5", "-c:a", "libvorbis", "-shortest"], "webm"), "Matroska/WebM video", "VP8 + Vorbis in WebM")
    put("video/clip.flv", ffmpeg(VID, ["-c:v", "flv", "-q:v", "6", "-c:a", "libmp3lame", "-b:a", "48k", "-ar", "22050", "-shortest"], "flv"), "Flash video", "FLV1 + MP3")
    put("video/clip.wmv", ffmpeg(VID, ["-c:v", "wmv2", "-q:v", "6", "-c:a", "wmav2", "-b:a", "48k", "-shortest"], "wmv"), "Windows Media (ASF)", "WMV2 + WMA2")
    put("video/clip.mpg", ffmpeg(VID, ["-c:v", "mpeg1video", "-r", "25", "-q:v", "6", "-c:a", "mp2", "-b:a", "64k", "-ar", "22050", "-shortest", "-f", "mpeg"], "mpg"), "MPEG program stream", "MPEG-1 + MP2")
    put("video/clip.ts", ffmpeg(VID, ["-c:v", "mpeg2video", "-r", "25", "-q:v", "6", "-c:a", "mp2", "-b:a", "64k", "-ar", "22050", "-shortest", "-f", "mpegts"], "ts"), "MPEG transport stream", "MPEG-2 + MP2")
    put("video/clip.ogv", ffmpeg(VID, ["-c:v", "libtheora", "-q:v", "4", "-c:a", "libvorbis", "-shortest"], "ogv"), "Ogg Theora video", "Theora + Vorbis")
    put("video/clip.3gp", ffmpeg(VID, ["-c:v", "h263", "-s", "176x144", "-q:v", "6", "-c:a", "aac", "-b:a", "48k", "-ar", "22050", "-shortest"], "3gp"), "3GPP video", "H.263 + AAC")
    # Rec demo's own files (docs/DEMO-RECORDINGS.md): the kept frames (64 x 24 cells a screen, with how many frames it stays) and its sound
    recs = b""
    for run_frames, text in [(30, "r3i6nos"), (15, "Rec demo"), (45, "frames")]:
        ch, fg, bg = bytearray(64 * 24), bytearray(64 * 24), bytearray(64 * 24)
        for i in range(64 * 24):
            ch[i], fg[i], bg[i] = 32, 15, 1
        for i, c in enumerate(text):
            ch[2 * 64 + 4 + i], fg[2 * 64 + 4 + i] = ord(c), 14
        recs += struct.pack("<I", run_frames) + bytes(ch) + bytes(fg) + bytes(bg)
    put("video/demo-20261109-000000-frames.r3f", recs, "Rec demo frames (r3i6nos)", "3 screens, 3 s: plays in Commander without ffmpeg")
    put("video/demo-20261109-000000.wav", ffmpeg(["-f", "lavfi", "-i", "aevalsrc=0.3*sin(2*PI*392*t):d=3:s=11025:c=mono"], ["-c:a", "pcm_s16le"], "wav"), "WAV audio", "the sound beside the frames")


# ---- documents --------------------------------------------------------------------------------------------------------------------------

def zip_bytes(members, stored_first=None):
    """members: [(name, bytes)] -> a zip with fixed times and modes; `stored_first` names a member written first and stored (a mimetype)."""
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w") as z:
        for name, data in sorted(members, key=lambda m: (m[0] != stored_first, m[0] if m[0] != stored_first else "")):
            info = zipfile.ZipInfo(name, STAMP)
            info.compress_type = zipfile.ZIP_STORED if name == stored_first else zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)
    return b.getvalue()


def cfb(streams):
    """A minimal OLE compound file (512-byte sectors) holding `streams` {name: bytes} of under 4096 bytes each (the mini stream is not used: they are padded)."""
    secs = [bytearray(512)]   # sector 0: the FAT
    ents = []
    def utf16(name):
        return name.encode("utf-16-le")
    names = list(streams)
    dirsecs = (len(names) + 1 + 3) // 4
    first_data = 1 + dirsecs
    fat = [0xFFFFFFFD] + [0xFFFFFFFE] * 0  # filled below
    sector = 1
    chain = {}
    layout = []
    # directory sectors
    dir_chain = list(range(1, 1 + dirsecs))
    nxt = 1 + dirsecs
    data_chains = []
    for nme in names:
        body = streams[nme].ljust(4096, b"\0")
        n = len(body) // 512
        data_chains.append((nme, list(range(nxt, nxt + n)), body))
        nxt += n
    total = nxt
    fatv = [0xFFFFFFFF] * 128
    fatv[0] = 0xFFFFFFFD
    for i, s in enumerate(dir_chain):
        fatv[s] = dir_chain[i + 1] if i + 1 < len(dir_chain) else 0xFFFFFFFE
    for _, ch, _ in data_chains:
        for i, s in enumerate(ch):
            fatv[s] = ch[i + 1] if i + 1 < len(ch) else 0xFFFFFFFE
    assert total <= 128, "corpus_formats: the compound file outgrew one FAT sector"
    hdr = bytearray(512)
    hdr[0:8] = b"\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1"
    struct.pack_into("<HHHHHHIIIIIIII", hdr, 24, 0x3E, 3, 0xFFFE, 9, 6, 0, 0, 0, 1, 1, 0, 0x1000, 0xFFFFFFFE, 0)
    struct.pack_into("<I", hdr, 0x3C, 0xFFFFFFFE)
    hdr[0x2C:0x30] = struct.pack("<I", 1)
    hdr[0x30:0x34] = struct.pack("<I", 1)   # first directory sector
    hdr[0x4C:0x50] = struct.pack("<I", 0)   # the FAT is sector 0
    for i in range(1, 109):
        hdr[0x4C + 4 * i:0x50 + 4 * i] = b"\xff\xff\xff\xff"
    def entry(name, typ, left, right, child, start, size):
        e = bytearray(128)
        u = utf16(name)
        e[0:len(u)] = u
        struct.pack_into("<H", e, 64, len(u) + 2)
        e[66], e[67] = typ, 1
        struct.pack_into("<III", e, 68, left, right, child)
        struct.pack_into("<I", e, 116, start)
        struct.pack_into("<Q", e, 120, size)
        return bytes(e)
    entries = [entry("Root Entry", 5, 0xFFFFFFFF, 0xFFFFFFFF, 1 if names else 0xFFFFFFFF, 0xFFFFFFFE, 0)]
    for i, (nme, ch, body) in enumerate(data_chains):
        entries.append(entry(nme, 2, 0xFFFFFFFF, i + 2 if i + 1 < len(names) else 0xFFFFFFFF, 0xFFFFFFFF, ch[0], 4096))
    while len(entries) % 4:
        entries.append(bytes(128))
    out = bytes(hdr) + b"".join(struct.pack("<I", v) for v in fatv) + b"".join(entries)
    for _, _, body in data_chains:
        out += body
    return out


def docs():
    put("documents/letter.docx", zip_bytes([("[Content_Types].xml", b'<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"/>'),
        ("docProps/core.xml", b"<cp:coreProperties><dc:title>Corpus letter</dc:title><dc:creator>r3i6nos</dc:creator></cp:coreProperties>"),
        ("word/document.xml", b"<w:document><w:body><w:p><w:r><w:t>Hello from a docx</w:t></w:r></w:p><w:p><w:r><w:t>Second &amp; last</w:t></w:r></w:p></w:body></w:document>")]),
        "Word document (docx)", "")
    put("documents/sheet.xlsx", zip_bytes([("[Content_Types].xml", b"<Types/>"), ("xl/workbook.xml", b"<workbook><sheets><sheet name=\"Songs\"/></sheets></workbook>"),
        ("xl/sharedStrings.xml", b"<sst><si><t>title</t></si><si><t>plays</t></si><si><t>Corpus tune</t></si></sst>"),
        ("xl/worksheets/sheet1.xml", b"<worksheet><sheetData><row><c t=\"s\"><v>0</v></c><c t=\"s\"><v>1</v></c></row></sheetData></worksheet>")]),
        "Excel spreadsheet (xlsx)", "")
    put("documents/slides.pptx", zip_bytes([("[Content_Types].xml", b"<Types/>"), ("ppt/presentation.xml", b"<p:presentation/>"),
        ("ppt/slides/slide1.xml", b"<p:sld><p:cSld><p:spTree><p:sp><p:txBody><a:p><a:r><a:t>Corpus slide one</a:t></a:r></a:p></p:txBody></p:sp></p:spTree></p:cSld></p:sld>"),
        ("ppt/slides/slide2.xml", b"<p:sld><p:cSld><p:spTree><p:sp><p:txBody><a:p><a:r><a:t>Slide two</a:t></a:r></a:p></p:txBody></p:sp></p:spTree></p:cSld></p:sld>")]),
        "PowerPoint presentation (pptx)", "")
    odf = lambda mime, body: zip_bytes([("mimetype", mime), ("META-INF/manifest.xml", b"<manifest:manifest/>"),
        ("meta.xml", b"<office:document-meta><office:meta><dc:title>Corpus ODF</dc:title></office:meta></office:document-meta>"), ("content.xml", body)], stored_first="mimetype")
    put("documents/letter.odt", odf(b"application/vnd.oasis.opendocument.text", b"<office:document-content><office:body><office:text><text:p>Hello from an odt</text:p><text:p>Second paragraph</text:p></office:text></office:body></office:document-content>"), "OpenDocument text (odt)", "")
    put("documents/sheet.ods", odf(b"application/vnd.oasis.opendocument.spreadsheet", b"<office:document-content><office:body><office:spreadsheet><table:table><table:table-row><table:table-cell><text:p>cell</text:p></table:table-cell></table:table-row></table:table></office:spreadsheet></office:body></office:document-content>"), "OpenDocument spreadsheet (ods)", "")
    put("documents/slides.odp", odf(b"application/vnd.oasis.opendocument.presentation", b"<office:document-content><office:body><office:presentation><draw:page><text:p>Corpus slide</text:p></draw:page></office:presentation></office:body></office:document-content>"), "OpenDocument presentation (odp)", "")
    put("documents/book.epub", zip_bytes([("mimetype", b"application/epub+zip"), ("META-INF/container.xml", b'<?xml version="1.0"?><container><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>'),
        ("book.opf", b'<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Corpus book</dc:title><dc:creator>r3i6nos</dc:creator></metadata><manifest><item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="c1"/></spine></package>'),
        ("c1.xhtml", b"<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>One</title></head><body><h1>Chapter one</h1><p>Hello from an EPUB.</p></body></html>")], stored_first="mimetype"), "EPUB e-book", "")
    content2 = zlib.compress(b"BT /F1 12 Tf 72 700 Td (Second page) Tj ET")
    pdf = (b"%PDF-1.4\n1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n2 0 obj << /Type /Pages /Count 2 /Kids [3 0 R 5 0 R] >> endobj\n"
           b"3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R >> endobj\n4 0 obj << /Length 44 >>\nstream\nBT /F1 12 Tf 72 712 Td [(Hello) -300 (PDF)] TJ ET\nendstream endobj\n"
           b"5 0 obj << /Type /Page /Parent 2 0 R /Contents 6 0 R >> endobj\n6 0 obj << /Length " + str(len(content2)).encode() + b" /Filter /FlateDecode >>\nstream\n" + content2 + b"\nendstream endobj\n"
           b"7 0 obj << /Title (Corpus PDF) /Author (r3i6nos) >> endobj\ntrailer << /Root 1 0 R /Info 7 0 R >>\n%%EOF\n")
    put("documents/doc.pdf", pdf, "PDF document", "two pages")
    put("documents/doc.rtf", b"{\\rtf1\\ansi{\\fonttbl{\\f0 Arial;}}{\\info{\\title Corpus RTF}}\\f0 Hello \\b RTF\\b0\\par Caf\\'e9\\par}", "Rich Text Format document", "")
    put("documents/page.html", b"<!DOCTYPE html><html><head><title>Corpus page</title><style>p{}</style></head><body><h1>Heading</h1><p>One &amp; two</p><script>x()</script></body></html>\n", "HTML document", "")
    put("documents/page.xhtml", b'<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>XHTML</title></head><body><p>XHTML page</p></body></html>\n', "XHTML document", "")
    put("documents/legacy.doc", cfb({"WordDocument": b"\xec\xa5\xc1\x00" + bytes(60), "1Table": bytes(8)}), "Legacy MS Office document (or MSI)", "holds a Word stream")
    put("documents/legacy.xls", cfb({"Workbook": b"\x09\x08\x10\x00" + bytes(60)}), "Legacy MS Office document (or MSI)", "holds a Workbook stream")
    put("documents/legacy.ppt", cfb({"PowerPoint Document": bytes(64)}), "Legacy MS Office document (or MSI)", "holds a PowerPoint stream")
    put("documents/installer.msi", cfb({"\x05SummaryInformation": bytes(64), "Property": bytes(16)}), "Legacy MS Office document (or MSI)", "an installer's compound file")
    put("documents/mail.eml", b"From: Test Sender <sender@example.com>\r\nTo: reader@example.com\r\nSubject: Corpus mail\r\nDate: Mon, 09 Nov 2026 12:00:00 +0000\r\nMIME-Version: 1.0\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nHello from an email.\r\n", "Email message", "")
    put("documents/inbox.mbox", b"From sender@example.com Mon Nov  9 12:00:00 2026\nFrom: sender@example.com\nSubject: First\n\nOne\n\nFrom other@example.com Mon Nov  9 12:05:00 2026\nFrom: other@example.com\nSubject: Second\n\nTwo\n", "Mbox mail archive", "two messages")
    put("documents/notes.md", b"# Corpus notes\n\nA *Markdown* file with a list:\n\n- one\n- two\n\n```\ncode\n```\n", "Markdown document", "")


# ---- text, code and data ----------------------------------------------------------------------------------------------------------------

def text_files():
    files = {
        "data.xml": ("XML data", b'<?xml version="1.0"?><corpus><item id="1">one</item><item id="2">two</item></corpus>\n'),
        "data.json": ("JSON data", json.dumps({"corpus": "r3i6nos", "items": [1, 2, 3], "nested": {"ok": True}}, indent=2).encode() + b"\n"),
        "table.csv": ("CSV table", b"title,plays,year\nCorpus tune,12,2026\nSecond tune,7,2025\n"),
        "table.tsv": ("TSV table", b"title\tplays\nCorpus tune\t12\n"),
        "style.css": ("CSS stylesheet", b"body { margin: 0; color: #204060; }\n"),
        "script.js": ("JavaScript source", b"function hello(name) { return 'hello ' + name; }\nconsole.log(hello('corpus'));\n"),
        "module.mjs": ("JavaScript module", b"export const answer = 42;\n"),
        "types.ts": ("TypeScript source", b"export interface Song { title: string; plays: number }\n"),
        "script.py": ("Python source", b"def hello(name):\n    return f'hello {name}'\n\nprint(hello('corpus'))\n"),
        "main.rs": ("Rust source", b"fn main() {\n    println!(\"hello corpus\");\n}\n"),
        "main.go": ("Go source", b"package main\n\nimport \"fmt\"\n\nfunc main() { fmt.Println(\"hello corpus\") }\n"),
        "Hello.java": ("Java source", b"public class Hello { public static void main(String[] a) { System.out.println(\"hello\"); } }\n"),
        "hello.c": ("C source", b"#include <stdio.h>\nint main(void) { puts(\"hello corpus\"); return 0; }\n"),
        "hello.h": ("C header", b"#ifndef HELLO_H\n#define HELLO_H\nint hello(void);\n#endif\n"),
        "hello.cpp": ("C++ source", b"#include <iostream>\nint main() { std::cout << \"hello corpus\\n\"; }\n"),
        "hello.hpp": ("C++ header", b"#pragma once\nint hello();\n"),
        "Hello.cs": ("C# source", b"class Hello { static void Main() { System.Console.WriteLine(\"hello\"); } }\n"),
        "hello.rb": ("Ruby source", b"puts 'hello corpus'\n"),
        "hello.php": ("PHP source", b"<?php echo 'hello corpus';\n"),
        "run.sh": ("Shell script", b"#!/bin/sh\necho hello corpus\n"),
        "run.bash": ("Shell script", b"#!/bin/bash\necho hello corpus\n"),
        "game.lua": ("Lua source", b"print('hello corpus')\nfunction love.draw() end\n"),
        "config.yaml": ("YAML data", b"corpus: r3i6nos\nitems:\n  - one\n  - two\n"),
        "config.yml": ("YAML data", b"key: value\n"),
        "config.toml": ("TOML data", b"[corpus]\nname = \"r3i6nos\"\nitems = [1, 2]\n"),
        "settings.ini": ("INI settings", b"[audio]\nvolume=16\nboost=2\n"),
        "app.cfg": ("Settings file", b"mode = test\n"),
        "app.conf": ("Settings file", b"DEFAULT=1\n"),
        "query.sql": ("SQL script", b"CREATE TABLE songs (title TEXT, plays INT);\nSELECT * FROM songs;\n"),
        "boot.log": ("Log file", b"[ 0.000000] r3i6nos corpus log\n[ 1.000000] second line\n"),
        "subtitles.srt": ("SubRip subtitles", b"1\n00:00:01,000 --> 00:00:02,000\nHello corpus\n\n2\n00:00:03,000 --> 00:00:04,000\nSecond line\n"),
        "subtitles.vtt": ("WebVTT subtitles", b"WEBVTT\n\n00:01.000 --> 00:02.000\nHello corpus\n"),
        "list.m3u": ("M3U playlist", b"#EXTM3U\n#EXTINF:1,Corpus tone\n../audio/tone.mp3\n"),
        "list.m3u8": ("M3U playlist", b"#EXTM3U\n#EXTINF:1,Corpus tone\n../audio/tone.wav\n"),
        "list.pls": ("PLS playlist", b"[playlist]\nFile1=../audio/tone.mp3\nTitle1=Corpus tone\nNumberOfEntries=1\nVersion=2\n"),
        "disc.cue": ("CD cue sheet", b'FILE "tone.wav" WAVE\n  TRACK 01 AUDIO\n    TITLE "Corpus tone"\n    INDEX 01 00:00:00\n'),
        "event.ics": ("iCalendar", b"BEGIN:VCALENDAR\nVERSION:2.0\nBEGIN:VEVENT\nSUMMARY:Corpus test\nDTSTART:20261109T120000Z\nEND:VEVENT\nEND:VCALENDAR\n"),
        "contact.vcf": ("vCard contacts", b"BEGIN:VCARD\nVERSION:3.0\nFN:Corpus Person\nEMAIL:corpus@example.com\nEND:VCARD\n"),
        "paper.tex": ("TeX document", b"\\documentclass{article}\n\\begin{document}\nHello corpus.\n\\end{document}\n"),
        "refs.bib": ("BibTeX references", b"@book{corpus, title={Corpus}, author={r3i6nos}, year={2026}}\n"),
        "change.diff": ("Patch", b"--- a/x.txt\n+++ b/x.txt\n@@ -1 +1 @@\n-old\n+new\n"),
        "change.patch": ("Patch", b"--- a/y.txt\n+++ b/y.txt\n@@ -1 +1 @@\n-one\n+two\n"),
        "run.bat": ("DOS batch file", b"@echo off\r\necho hello corpus\r\n"),
        "run.ps1": ("PowerShell script", b"Write-Host 'hello corpus'\n"),
        "key.pem": ("PEM key or certificate", b"-----BEGIN PUBLIC KEY-----\nQ29ycHVzIHRlc3Qga2V5IChub3QgYSByZWFsIGtleSk=\n-----END PUBLIC KEY-----\n"),
        "app.desktop": ("Desktop entry", b"[Desktop Entry]\nName=Corpus\nExec=corpus\nType=Application\n"),
        "unit.service": ("systemd unit", b"[Unit]\nDescription=Corpus\n[Service]\nExecStart=/bin/true\n"),
        "font.bdf": ("BDF bitmap font", b"STARTFONT 2.1\nFONT -corpus-test-medium-r-normal--8-80-75-75-c-80-iso10646-1\nSIZE 8 75 75\nFONTBOUNDINGBOX 8 8 0 0\nCHARS 0\nENDFONT\n"),
    }
    for name, (label, data) in files.items():
        put("text/" + name, data, label, "")
    put("text/utf8.txt", "r3i6nos test corpus: UTF-8 text for Commander's viewer.\n\nBox drawing:  ┌──┬──┐  ╔══╦══╗\n              │  │  │  ║  ║  ║\n              └──┴──┘  ╚══╩══╝\nShades:       ░░▒▒▓▓██\nAccents:      café naïve Ångström Øre façade\nSymbols:      ♪ ♫ ☺ ☻ ♥ ♦ ♣ ♠ • ° ± ÷ ½ ¼\nNot in CP437: 中文 ✓ (shown as a stand-in)\nA long line: ".encode() + " ".join(f"word{i}" for i in range(40)).encode() + b"\n\tA tab at the start.\n", "Plain text", "")
    put("text/utf16.txt", b"\xff\xfe" + "UTF-16 text with a byte-order mark\n".encode("utf-16-le"), "Plain text", "UTF-16 with a BOM")
    put("text/dos437.txt", "caf\u00e9 \u2591\u2592\u2593\u2588 DOS text in code page 437\r\n".encode("cp437", "replace"), "Plain text", "code page 437 bytes")
    put("text/empty.txt", b"", "Empty file", "zero bytes")
    put("text/binary.bin", bytes(range(256)) * 2, "Unknown (binary data)", "every byte value twice (hex view)")
    put("text/nul-terminated.dat", b"a\0b\0c\0" * 50, "Unknown (binary data)", "NUL bytes: not text")


def bencode(o):
    if isinstance(o, int):
        return b"i%de" % o
    if isinstance(o, bytes):
        return b"%d:" % len(o) + o
    if isinstance(o, str):
        return bencode(o.encode())
    if isinstance(o, list):
        return b"l" + b"".join(bencode(x) for x in o) + b"e"
    return b"d" + b"".join(bencode(k) + bencode(o[k]) for k in sorted(o)) + b"e"


def data():
    with tmp() as d:
        db = os.path.join(d, "t.db")
        c = sqlite3.connect(db)
        c.execute("PRAGMA page_size=512")
        c.execute("CREATE TABLE songs (title TEXT, plays INT)")
        c.execute("CREATE TABLE tags (song INT, tag TEXT)")
        c.execute("CREATE INDEX by_plays ON songs(plays)")
        c.execute("INSERT INTO songs VALUES ('Corpus tune', 12)")
        c.commit()
        c.close()
        put("data/music.db", Path(db).read_bytes(), "SQLite database", "two tables, an index")
    fields = [(b"TITLE", b"C", 20, 0), (b"PLAYS", b"N", 6, 0), (b"YEAR", b"N", 4, 0)]
    rec_len = 1 + sum(f[2] for f in fields)
    hdr = bytes([3, 126, 11, 9]) + struct.pack("<IHH", 1, 32 + 32 * len(fields) + 1, rec_len) + bytes(20)
    for nme, typ, ln, dec in fields:
        hdr += nme.ljust(11, b"\0") + typ + bytes(4) + bytes([ln, dec]) + bytes(14)
    put("data/songs.dbf", hdr + b"\r" + b" " + b"Corpus tune".ljust(20) + b"    12" + b"2026" + b"\x1a", "dBase table", "3 fields, 1 record")
    # a Parquet file with no row groups: magic, a minimal footer (thrift), its length, magic
    footer = bytes([0x15, 0x02, 0x19, 0x1c, 0x00, 0x16, 0x00, 0x19, 0x0c, 0x18, 0x07]) + b"corpus" + bytes([0x00, 0x00])
    put("data/empty.parquet", b"PAR1" + footer + struct.pack("<I", len(footer)) + b"PAR1", "Parquet data file", "magic and a footer only")
    put("data/corpus.torrent", bencode({"announce": "http://tracker.invalid/announce", "created by": "r3i6nos corpus", "creation date": MTIME,
        "info": {"name": "corpus.bin", "length": 512, "piece length": 16384, "pieces": bytes(20)}}), "BitTorrent file", "one file of 512 bytes")
    lnk = b"\x4c\x00\x00\x00" + bytes.fromhex("0114020000000000c000000000000046") + struct.pack("<IIQQQIIIHHII", 0x81, 0x20, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0)
    put("data/shortcut.lnk", lnk, "Windows shortcut (.lnk)", "header only")
    put("data/system.regf", b"regf" + struct.pack("<IIQIIII", 1, 1, 0, 1, 3, 0, 1) + bytes(4096 - 32), "Windows Registry hive", "header block")


# ---- archives ---------------------------------------------------------------------------------------------------------------------------

def tar_bytes(members, fmt=tarfile.USTAR_FORMAT):
    t = io.BytesIO()
    with tarfile.open(fileobj=t, mode="w", format=fmt) as tf:
        for name, body in members:
            ti = tarfile.TarInfo(name)
            ti.size, ti.mtime, ti.uid, ti.gid, ti.uname, ti.gname, ti.mode = len(body), MTIME, 0, 0, "root", "root", 0o644
            tf.addfile(ti, io.BytesIO(body))
    return t.getvalue()


def pipe(cmd, data):
    return subprocess.run(cmd, input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout


GAME = [("game/main.lua", b"print('hello corpus')\nfunction love.draw() end\n"), ("game/readme.txt", b"read me\n" * 12), ("game/data/level1.txt", b"level one\n")]


def crc16_arc(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def dos_time(ts=MTIME):
    return (((2026 - 1980) << 9 | 11 << 5 | 9) << 16) | (12 << 11)


def rar4(name, body):
    crc = zlib.crc32(body) & 0xFFFFFFFF
    marker = b"Rar!\x1a\x07\x00"
    main = struct.pack("<BHH", 0x73, 0, 13) + bytes(6)
    mainblk = struct.pack("<H", zlib.crc32(main) & 0xFFFF) + main
    nm = name.encode()
    fh = struct.pack("<BHH", 0x74, 0x8000, 32 + len(nm)) + struct.pack("<IIBIIBBHI", len(body), len(body), 3, crc, dos_time(), 20, 0x30, len(nm), 0x81A4) + nm
    return marker + mainblk + struct.pack("<H", zlib.crc32(fh) & 0xFFFF) + fh + body + bytes.fromhex("c43d7b00400700")


def vint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def rar5(name, body):
    def block(btype, flags, fields, data=b""):
        rest = vint(btype) + vint(flags) + fields
        size = vint(len(rest))
        hdr = size + rest
        return struct.pack("<I", zlib.crc32(hdr) & 0xFFFFFFFF) + hdr + data
    nm = name.encode()
    main = block(1, 0, vint(0))
    fields = vint(len(body)) + vint(0) + vint(len(body)) + vint(0x20) if False else b""
    f = vint(0x04) + vint(len(body)) + vint(0x1A4) + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF) + vint(0) + vint(1) + vint(len(nm)) + nm
    # block type 2 (file), header flags 2 (a data area follows); the data size comes before the file fields
    rest = vint(2) + vint(2) + vint(len(body)) + f
    hdr = vint(len(rest)) + rest
    fileblk = struct.pack("<I", zlib.crc32(hdr) & 0xFFFFFFFF) + hdr + body
    return b"Rar!\x1a\x07\x01\x00" + main + fileblk + block(5, 0, vint(0))


def cab(files):
    folder_data = b"".join(b for _, b in files)
    n = len(files)
    cff = b""
    off = 0
    for nm, b in files:
        cff += struct.pack("<IIHHHH", len(b), off, 0, (2026 - 1980) << 9 | 11 << 5 | 9, 12 << 11, 0x20) + nm.encode() + b"\0"
        off += len(b)
    coff_files = 36 + 8
    coff_data = coff_files + len(cff)
    cfdata = struct.pack("<IHH", 0, len(folder_data), len(folder_data)) + folder_data
    total = coff_data + len(cfdata)
    hdr = b"MSCF" + struct.pack("<IIIIIBBHHHHH", 0, total, 0, coff_files, 0, 3, 1, 1, n, 0, 1, 0)
    return hdr + struct.pack("<IHH", coff_data, 1, 0) + cff + cfdata


def lzh(name, body):
    nm = name.encode()
    hdr = b"-lh0-" + struct.pack("<IIIBB", len(body), len(body), dos_time(), 0x20, 0) + bytes([len(nm)]) + nm + struct.pack("<H", crc16_arc(body))
    return bytes([len(hdr), sum(hdr) & 0xFF]) + hdr + body + b"\0"


def rpm():
    def header(tags):
        idx, store = b"", b""
        for tag, typ, count, val in tags:
            if typ in (6, 9):    # string, i18n string
                data = val.encode() + b"\0"
            elif typ == 4:
                data = struct.pack(">I", val)
                store += b"\0" * (-len(store) % 4)
            else:
                data = val
            idx += struct.pack(">IIII", tag, typ, len(store), count)
            store += data
        return b"\x8e\xad\xe8\x01" + bytes(4) + struct.pack(">II", len(tags), len(store)) + idx + store
    lead = b"\xed\xab\xee\xdb\x03\x00\x00\x00\x00\x01" + b"corpus-1.0-1".ljust(66, b"\0") + struct.pack(">HH", 1, 5) + bytes(16)
    sig = header([(1000, 4, 1, 0)])
    sig += b"\0" * (-len(sig) % 8)
    main = header([(1000, 6, 1, "corpus"), (1001, 6, 1, "1.0"), (1002, 6, 1, "1"), (1004, 9, 1, "r3i6nos corpus package"), (1022, 6, 1, "noarch"), (1021, 6, 1, "linux")])
    cpio = cpio_newc([("corpus.txt", 0o100644, b"rpm payload\n")])
    return lead + sig + main + gzip.compress(cpio, mtime=0)


def cpio_newc(members, magic=b"070701"):
    out = b""
    def ent(name, mode, body, ino):
        nm = name.encode() + b"\0"
        chk = sum(body) & 0xFFFFFFFF if magic == b"070702" else 0
        h = magic + b"".join(b"%08X" % v for v in (ino, mode, 0, 0, 1, MTIME, len(body), 0, 0, 0, 0, len(nm), chk))
        r = h + nm
        r += b"\0" * (-len(r) % 4)
        r += body
        r += b"\0" * (-len(r) % 4)
        return r
    for i, (name, mode, body) in enumerate(members):
        out += ent(name, mode, body, i + 1)
    return out + ent("TRAILER!!!", 0, b"", 0)


def cpio_odc(members):
    out = b""
    for i, (name, mode, body) in enumerate(members + [("TRAILER!!!", 0, b"")]):
        nm = name.encode() + b"\0"
        out += b"070707" + b"%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o" % (0, i + 1, mode, 0, 0, 1, 0, MTIME, len(nm), len(body)) + nm + body
    return out


def ar_bytes(members):
    out = b"!<arch>\n"
    for name, body in members:
        out += (name + "/").ljust(16).encode() + b"%-12d" % MTIME + b"0     0     100644  " + str(len(body)).ljust(10).encode() + b"`\n" + body
        if len(body) % 2:
            out += b"\n"
    return out


def archives():
    import bz2
    tools = {"zstd": tool("zstd"), "lzip": tool("lzip"), "7z": tool("7z")}
    put("archives/bundle.zip", zip_bytes([("hello.txt", b"hello from a zip\n"), ("inner/", b""), ("inner/game.tar", tar_bytes(GAME)), ("inner/notes.txt", b"notes\n" * 20)]), "ZIP archive", "a folder, a tar and text")
    put("archives/tool.jar", zip_bytes([("META-INF/MANIFEST.MF", b"Manifest-Version: 1.0\nMain-Class: Hello\n"), ("Hello.class", java_class())]), "Java archive (jar)", "")
    put("archives/app.msix", zip_bytes([("AppxManifest.xml", b'<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"><Identity Name="Corpus.App" Version="1.0.0.0" Publisher="CN=corpus"/></Package>'), ("[Content_Types].xml", b"<Types/>")]), "Windows app package (msix)", "")
    put("archives/game.tar", tar_bytes(GAME), "TAR archive", "ustar")
    put("archives/game.tar.gz", gzip.compress(tar_bytes(GAME), mtime=0), "TAR archive, gzip compressed", "a tar inside")
    put("archives/game.tar.xz", lzma.compress(tar_bytes(GAME), format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC32), "XZ compressed", "a tar inside")
    put("archives/game.tar.zst", pipe([tools["zstd"], "-q", "-19", "-c"], tar_bytes(GAME)), "Zstandard compressed", "a tar inside")
    put("archives/game.tar.bz2", bz2.compress(tar_bytes(GAME), 9), "Bzip2 compressed", "a tar inside (listed only with the archive options in ROADMAP section 21)")
    put("archives/game.tar.lz", pipe([tools["lzip"], "-c"], tar_bytes(GAME)), "Lzip compressed", "a tar inside")
    note = b"corpus says hello\n" * 8
    put("archives/notes.txt.gz", gzip.compress(note, mtime=0), "Gzip compressed", "")
    put("archives/notes.txt.bz2", bz2.compress(note, 9), "Bzip2 compressed", "")
    put("archives/notes.txt.xz", lzma.compress(note, format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC64), "XZ compressed", "")
    put("archives/notes.txt.zst", pipe([tools["zstd"], "-q", "-19", "-c"], note), "Zstandard compressed", "")
    put("archives/notes.txt.lz", pipe([tools["lzip"], "-c"], note), "Lzip compressed", "")
    with tmp() as d:
        for name, body in GAME:
            p = Path(d, "src", name)
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(body)
            os.utime(p, (MTIME, MTIME))
        for sub in Path(d, "src").rglob("*"):
            os.utime(sub, (MTIME, MTIME))
        out = os.path.join(d, "game.7z")
        run([tools["7z"], "a", "-t7z", "-mx=5", "-mhe=off", "-bso0", "-bsp0", out, "game"], cwd=os.path.join(d, "src"))
        put("archives/game.7z", Path(out).read_bytes(), "7-Zip archive", "LZMA2 (named only: not listed or unpacked yet)")
    put("archives/stored.rar", rar4("readme.txt", b"stored in a RAR 4 archive\n" * 4), "RAR archive", "RAR 4, one file stored")
    put("archives/stored5.rar", rar5("readme.txt", b"stored in a RAR 5 archive\n" * 4), "RAR 5 archive", "RAR 5, one file stored")
    put("archives/two.cab", cab([("hello.txt", b"hello from a cabinet\n"), ("second.txt", b"second file\n" * 5)]), "Microsoft Cabinet archive", "two files, stored")
    put("archives/old.lzh", lzh("README.TXT", b"stored in an LHA archive\n" * 3), "LZH/LHA archive", "-lh0- (stored)")
    put("archives/pkg.a", ar_bytes([("hello.o", b"not an object\n"), ("world.o", b"also not\n" * 3)]), "Unix ar archive", "")
    put("archives/initrd.cpio", cpio_newc([("bin", 0o40755, b""), ("bin/hello", 0o100755, b"#!/bin/sh\necho hello from cpio\n"), ("etc.txt", 0o100644, b"cpio etc\n" * 3)]), "cpio archive (newc)", "")
    put("archives/crc.cpio", cpio_newc([("a.txt", 0o100644, b"crc variant\n")], b"070702"), "cpio archive (newc, CRC)", "")
    put("archives/old.cpio", cpio_odc([("a.txt", 0o100644, b"portable ascii\n")]), "cpio archive (portable)", "")
    put("archives/corpus.rpm", rpm(), "RPM package", "noarch, one file")
    control = b"Package: hello-test\nVersion: 1.2-3\nArchitecture: arm64\nMaintainer: r3i6nos <noreply@example.invalid>\nDescription: a corpus package\n"
    data_ = [("./usr/bin/hello", b"#!/bin/sh\necho hello\n"), ("./usr/share/doc/hello/README", b"hello docs\n" * 5)]
    put("archives/hello-xz.deb", ar_bytes([("debian-binary", b"2.0\n"), ("control.tar.xz", lzma.compress(tar_bytes([("./control", control)]), format=lzma.FORMAT_XZ)), ("data.tar.xz", lzma.compress(tar_bytes(data_), format=lzma.FORMAT_XZ))]), "Debian package (.deb)", "xz members")
    put("archives/hello-zst.deb", ar_bytes([("debian-binary", b"2.0\n"), ("control.tar.zst", pipe([tools["zstd"], "-q", "-19", "-c"], tar_bytes([("./control", control)]))), ("data.tar.zst", pipe([tools["zstd"], "-q", "-19", "-c"], tar_bytes(data_)))]), "Debian package (.deb)", "zstd members")
    # old DOS and Amiga archives: only their signatures (Commander names them; nothing here can write them)
    put("archives/old.arc", b"\x1a\x08" + b"README.TXT\0\0\0" + struct.pack("<IHHII", 12, 0, 0, 0, 12) + b"corpus stub\n" + b"\x1a\x00", "ARC archive (SEA ARC, 1985)", "signature and one stored entry")
    put("archives/old.arj", b"\x60\xea" + struct.pack("<H", 30) + bytes(30), "ARJ archive", "signature only")
    put("archives/old.zoo", b"ZOO 2.10 Archive.\x1a" + bytes(40), "ZOO archive", "signature only")
    put("archives/old.Z", b"\x1f\x9d\x90" + bytes(20), "Unix compress (.Z)", "signature only")
    put("archives/old.ace", bytes(7) + b"**ACE**" + bytes(20), "ACE archive", "signature only")
    put("archives/amiga.dms", b"DMS!" + bytes(60), "Amiga DiskMasher disk image", "signature only")


def java_class():
    cp = struct.pack(">BH", 1, 1) + b"T" + struct.pack(">BH", 7, 1) + struct.pack(">BH", 1, 16) + b"java/lang/Object" + struct.pack(">BH", 7, 3)
    return b"\xca\xfe\xba\xbe" + struct.pack(">HHH", 0, 52, 5) + cp + struct.pack(">HHHHHHHH", 0x20, 2, 4, 0, 0, 0, 0, 0)


# ---- disks ----------------------------------------------------------------------------------------------------------------------------------

def iso9660(files, label="R3I6NOS_CORPUS"):
    """A tiny ISO 9660: the root folder holds `files` [(NAME;1, bytes)]. 2048-byte blocks."""
    def both(n, size):
        return struct.pack("<I", n) + struct.pack(">I", n) if size == 4 else struct.pack("<H", n) + struct.pack(">H", n)
    def rec(name, extent, length, flags):
        nm = name if isinstance(name, bytes) else name.encode()
        r = struct.pack("B", 0) + b"\0" + both(extent, 4) + both(length, 4) + bytes([126, 11, 9, 12, 0, 0, 0]) + bytes([flags]) + b"\0\0" + both(1, 2) + bytes([len(nm)]) + nm
        if len(r) % 2:
            r += b"\0"
        return bytes([len(r)]) + r[1:]
    blk = 2048
    first = 21
    extents, nxt = [], first
    for _, body in files:
        extents.append(nxt)
        nxt += (len(body) + blk - 1) // blk
    root = rec(b"\0", 20, blk, 2) + rec(b"\1", 20, blk, 2) + b"".join(rec(n, e, len(b), 0) for (n, b), e in zip(files, extents))
    root = root.ljust(blk, b"\0")
    pvd = bytearray(blk)
    pvd[0:7] = b"\x01CD001\x01"
    pvd[8:40] = b"R3I6NOS".ljust(32)
    pvd[40:72] = label.encode().ljust(32)
    pvd[80:88] = both(nxt, 4)
    pvd[120:124] = both(1, 2)
    pvd[124:128] = both(1, 2)
    pvd[128:132] = both(blk, 2)
    pvd[132:140] = both(10, 4)
    pvd[140:144] = struct.pack("<I", 18)
    pvd[148:152] = struct.pack(">I", 19)
    pvd[156:190] = rec(b"\0", 20, blk, 2)
    pvd[190:318] = b"r3i6nos corpus".ljust(128)
    pvd[318:446] = b"r3i6nos".ljust(128)
    pvd[446:574] = b"scripts/corpus_formats.py".ljust(128)
    for off in (813, 830, 847, 864):
        pvd[off:off + 17] = b"2026110900000000\0"
    pvd[881] = 1
    term = bytearray(blk)
    term[0:7] = b"\xffCD001\x01"
    ptl = (struct.pack("<BBIH", 1, 0, 20, 1) + b"\0").ljust(blk, b"\0")
    ptm = (struct.pack(">BBIH", 1, 0, 20, 1) + b"\0").ljust(blk, b"\0")
    out = bytearray(16 * blk) + pvd + term + ptl + ptm + root
    out = bytes(out)
    for _, body in files:
        out += body + b"\0" * (-len(body) % blk)
    return out


def gpt_image():
    img = bytearray(64 * 512)
    img[446:462] = bytes([0, 0, 2, 0, 0xEE, 0xFF, 0xFF, 0xFF]) + struct.pack("<II", 1, 63)
    img[510:512] = b"\x55\xaa"
    entries = bytearray(128 * 128)
    type_guid = bytes.fromhex("a2a0d0ebe5b9334487c068b6b72699c7")
    entries[0:16] = type_guid
    entries[16:32] = bytes.fromhex("11223344556677889900aabbccddeeff")
    struct.pack_into("<QQQ", entries, 32, 34, 60, 0)
    entries[56:56 + 22] = "corpus data".encode("utf-16-le")
    h = bytearray(92)
    h[0:8] = b"EFI PART"
    struct.pack_into("<IIIIQQQQ", h, 8, 0x10000, 92, 0, 0, 1, 63, 34, 62)
    h[56:72] = bytes.fromhex("0123456789abcdef0123456789abcdef")
    struct.pack_into("<QIII", h, 72, 2, 128, 128, zlib.crc32(bytes(entries)) & 0xFFFFFFFF)
    struct.pack_into("<I", h, 16, zlib.crc32(bytes(h)) & 0xFFFFFFFF)
    img[512:512 + 92] = h
    img[1024:1024 + len(entries[:512])] = entries[:512]
    return bytes(img)


def disks():
    put("disks/corpus.iso", iso9660([("README.TXT;1", b"an ISO 9660 image made in code\n" * 3), ("HELLO.TXT;1", b"hello from an iso\n")]), "ISO 9660 disk image", "two files")
    img = bytearray(4 * 512)
    img[446:462] = bytes([0x80, 0, 0, 0, 0x0C, 0, 0, 0]) + struct.pack("<II", 1, 3)
    img[510:512] = b"\x55\xaa"
    img[512 + 82:512 + 90] = b"FAT32   "
    img[512 + 71:512 + 82] = b"R36S CARD  "
    img[512 + 510:512 + 512] = b"\x55\xaa"
    put("disks/mbr.img", bytes(img), "Disk image (partitioned)", "MBR with one FAT32 partition labelled R36S CARD")
    put("disks/gpt.img", gpt_image(), "Disk image (partitioned)", "GPT with one partition")
    bs = bytearray(128 * 512)
    bs[0:3] = b"\xeb\x3c\x90"
    bs[3:11] = b"R3I6NOS "
    struct.pack_into("<HBHBHHBHHHII", bs, 11, 512, 1, 1, 2, 64, 128, 0xF8, 1, 32, 2, 0, 0)
    bs[36:38] = b"\x80\x00"
    bs[38] = 0x29
    bs[39:43] = b"R36S"
    bs[43:54] = b"CORPUS FAT "
    bs[54:62] = b"FAT12   "
    bs[510:512] = b"\x55\xaa"
    put("disks/floppy.img", bytes(bs), "Disk image", "a FAT12 volume labelled CORPUS FAT, no partition table")
    data_ = bytes(64 * 1024)
    ts = 2026 - 2000
    foot = bytearray(512)
    foot[0:8] = b"conectix"
    struct.pack_into(">IIQIIII", foot, 8, 2, 0x00010000, 0xFFFFFFFFFFFFFFFF, ts * 365 * 86400 + 12345, 0, 0, 0)
    foot[28:32] = b"r3i6"
    struct.pack_into(">I", foot, 32, 0x00010000)
    foot[36:40] = b"Wi2k"
    struct.pack_into(">QQ", foot, 40, len(data_), len(data_))
    struct.pack_into(">HBB", foot, 56, 8, 4, 16)
    foot[58], foot[59] = 4, 16
    struct.pack_into(">I", foot, 60, 2)
    foot[68:84] = bytes.fromhex("0123456789abcdef0123456789abcdef")
    struct.pack_into(">I", foot, 64, (~sum(foot)) & 0xFFFFFFFF)
    put("disks/fixed.vhd", data_ + bytes(foot), "Virtual hard disk (VHD)", "fixed, 64 KiB")
    koly = bytearray(512)
    koly[0:4] = b"koly"
    struct.pack_into(">IIIQQQQQIII", koly, 4, 4, 512, 1, 0, 0, 65536, 0, 0, 1, 1, 0)
    struct.pack_into(">I", koly, 216, 2)
    struct.pack_into(">Q", koly, 0xEC, 128)
    put("disks/image.dmg", bytes(65536) + bytes(koly), "Apple disk image (DMG)", "the koly trailer only")


# ---- programs ---------------------------------------------------------------------------------------------------------------------------------

def elf(machine, kind=2, entry=0x400078, interp=None):
    code = b"\xc0\x03\x5f\xd6" if machine == 183 else b"\xc3"
    eh = b"\x7fELF" + bytes([2, 1, 1, 0]) + bytes(8) + struct.pack("<HHIQQQIHHHHHH", kind, machine, 1, entry, 64, 0, 0, 64, 56, 1, 0, 0, 0)
    ph = struct.pack("<IIQQQQQQ", 1, 5, 0, 0x400000, 0x400000, 64 + 56 + len(code), 64 + 56 + len(code), 0x1000)
    return eh + ph + code


def pe():
    dos = b"MZ" + bytes(58) + struct.pack("<I", 64)
    coff = b"PE\0\0" + struct.pack("<HHIIIHH", 0x14C, 1, 0, 0, 0, 224, 0x0102)
    opt = struct.pack("<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII", 0x10B, 6, 0, 16, 0, 0, 0x1000, 0x1000, 0x2000, 0x400000, 0x1000, 0x200, 4, 0, 0, 0, 4, 0, 0, 0x2000, 0x200, 0, 3, 0, 0x100000, 0x1000, 0x100000, 0x1000, 0, 16) + bytes(128)
    sec = b".text\0\0\0" + struct.pack("<IIIIIIHHI", 16, 0x1000, 16, 0x200, 0, 0, 0, 0, 0x60000020)
    body = (dos + coff + opt[:224] + sec).ljust(0x200, b"\0") + b"\xc3" + bytes(15)
    return body


def macho(cpu, sub=0):
    return b"\xcf\xfa\xed\xfe" + struct.pack("<IIIIIII", cpu, sub, 2, 0, 0, 0, 0)


def lua_bytecode():
    with tmp() as d:
        src = os.path.join(d, "c.lua")
        Path(src).write_text("local function hello(n) return 'hello ' .. n end\nprint(hello('corpus'))\n")
        out5 = os.path.join(d, "c.luac")
        run([tool("luac5.4"), "-s", "-o", out5, src])
        outj = os.path.join(d, "c.ljbc")
        run([tool("luajit"), "-b", "-s", src, outj])
        return Path(out5).read_bytes(), Path(outj).read_bytes()


def programs():
    with tmp() as d:
        c = os.path.join(d, "h.c")
        Path(c).write_text("#include <stdio.h>\nint main(void) { puts(\"hello corpus\"); return 0; }\n")
        out = os.path.join(d, "h")
        run([tool("gcc"), "-Os", "-s", "-Wl,--build-id=none", "-o", out, c])
        put("programs/hello", Path(out).read_bytes(), "ELF program or library", "a dynamic program made by gcc for this machine (links libc)")
    put("programs/tiny-aarch64", elf(183), "ELF program or library", "hand-made, ARM64, no libraries")
    put("programs/tiny-x86_64", elf(62), "ELF program or library", "hand-made, x86-64")
    ai = bytearray(elf(183))
    ai[8:11] = b"AI\x02"
    put("programs/corpus.AppImage", bytes(ai), "AppImage (type 2)", "an ELF with the AppImage marker")
    put("programs/tiny.exe", pe(), "Windows program (PE)", "hand-made 32-bit PE")
    put("programs/thin-arm64", macho(0x0100000C), "Mach-O program (64-bit)", "hand-made header")
    a, b = macho(0x0100000C), macho(0x01000007, 3)
    put("programs/universal", b"\xca\xfe\xba\xbe" + struct.pack(">I", 2) + struct.pack(">5I", 0x0100000C, 0, 4096, len(a), 12) + struct.pack(">5I", 0x01000007, 3, 8192, len(b), 12) + bytes(4096 - 48) + a.ljust(4096, b"\0") + b, "Mach-O universal program", "arm64 and x86-64")
    put("programs/tiny.wasm", bytes.fromhex("0061736d01000000 01040160 0000 03020100 07050101660000 0a040102000b".replace(" ", "")), "WebAssembly module", "one exported function")
    l54, ljb = lua_bytecode()
    put("programs/chunk.luac", l54, "Lua bytecode", "Lua 5.4")
    put("programs/chunk.ljbc", ljb, "LuaJIT bytecode", "LuaJIT")
    put("programs/Hello.class", java_class(), "Java class", "Java 8, no methods")


# ---- fonts ------------------------------------------------------------------------------------------------------------------------------------

def table_checksum(b):
    b = b + b"\0" * (-len(b) % 4)
    return sum(struct.unpack(">%dI" % (len(b) // 4), b)) & 0xFFFFFFFF


def ttf_tables(family):
    def glyph(x0, y0, x1, y1):
        g = struct.pack(">hhhhh", 1, x0, y0, x1, y1) + struct.pack(">H", 3) + struct.pack(">H", 0) + bytes([1, 1, 1, 1])
        g += struct.pack(">hhhh", x0, x1 - x0, 0, x0 - x1) + struct.pack(">hhhh", y0, 0, y1 - y0, 0)
        return g + b"\0" * (-len(g) % 4)
    glyphs = [glyph(50, 0, 550, 700), glyph(100, 0, 500, 600), glyph(100, 0, 500, 700)]
    loca, off = b"", 0
    for g in glyphs:
        loca += struct.pack(">H", off // 2)
        off += len(g)
    loca += struct.pack(">H", off // 2)
    name_str = lambda s: s.encode("utf-16-be")
    names = [(1, family), (2, "Regular"), (4, family), (6, family.replace(" ", "") + "-Regular")]
    store = b"".join(name_str(s) for _, s in names)
    recs, pos = b"", 0
    for nid, s in names:
        recs += struct.pack(">HHHHHH", 3, 1, 0x409, nid, len(name_str(s)), pos)
        pos += len(name_str(s))
    name = struct.pack(">HHH", 0, len(names), 6 + 12 * len(names)) + recs + store
    seg = struct.pack(">HHHHHH", 4, 32, 0, 4, 4, 1) + struct.pack(">HH", 66, 0xFFFF) + struct.pack(">H", 0) + struct.pack(">HH", 65, 0xFFFF) + struct.pack(">hH", 1 - 65, 1) + struct.pack(">HH", 0, 0)
    cmap = struct.pack(">HH", 0, 1) + struct.pack(">HHI", 3, 1, 12) + seg
    head = struct.pack(">IIIIHHQQhhhhHHhhh", 0x00010000, 0x00010000, 0, 0x5F0F3CF5, 0, 1000, 0, 0, 50, 0, 550, 700, 0, 8, 2, 0, 0)
    hhea = struct.pack(">IhhhHhhhhhhhhhhhhH", 0x00010000, 800, -200, 0, 600, 0, 0, 600, 1, 0, 0, 0, 0, 0, 0, 0, 0, 3)
    maxp = struct.pack(">IHHHHHHHHHHHHHH", 0x00010000, 3, 4, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0)
    hmtx = struct.pack(">HhHhHh", 600, 0, 600, 0, 600, 0)
    post = struct.pack(">IIhhIIIII", 0x00030000, 0, -100, 50, 0, 0, 0, 0, 0)
    return {b"cmap": cmap, b"glyf": b"".join(glyphs), b"head": head, b"hhea": hhea, b"hmtx": hmtx, b"loca": loca, b"maxp": maxp, b"name": name, b"post": post}


def sfnt(tables, base=0, flavor=0x00010000):
    tags = sorted(tables)
    n = len(tags)
    dir_len = 12 + 16 * n
    out, offs, pos = b"", {}, base + dir_len
    for t in tags:
        offs[t] = pos
        data = tables[t] + b"\0" * (-len(tables[t]) % 4)
        out += data
        pos += len(data)
    es = 1
    while es * 2 <= n:
        es *= 2
    d = struct.pack(">IHHHH", flavor, n, es * 16, es.bit_length() - 1, n * 16 - es * 16)
    for t in tags:
        d += t + struct.pack(">III", table_checksum(tables[t]), offs[t], len(tables[t]))
    whole = d + out
    adj = (0xB1B0AFBA - table_checksum(whole)) & 0xFFFFFFFF
    i = offs[b"head"] - base - dir_len + dir_len + 8
    whole = whole[:i] + struct.pack(">I", adj) + whole[i + 4:]
    return whole


def fonts():
    t = ttf_tables("Corpus Test")
    put("fonts/corpus.ttf", sfnt(t), "TrueType font", "three square glyphs, family Corpus Test")
    a, b = sfnt(ttf_tables("Corpus A"), 20), None
    b = sfnt(ttf_tables("Corpus B"), 20 + len(a))
    put("fonts/corpus.ttc", b"ttcf" + struct.pack(">IIII", 0x00010000, 2, 20, 20 + len(a)) + a + b, "TrueType collection", "two fonts")
    # WOFF: the same tables, each deflated
    tags = sorted(t)
    sfnt_size = len(sfnt(t))
    comp = {k: (zlib.compress(t[k], 9) if len(zlib.compress(t[k], 9)) < len(t[k]) else t[k]) for k in tags}
    off, ents, body = 44 + 20 * len(tags), b"", b""
    for k in tags:
        ents += k + struct.pack(">IIII", off, len(comp[k]), len(t[k]), table_checksum(t[k]))
        pad = comp[k] + b"\0" * (-len(comp[k]) % 4)
        body += pad
        off += len(pad)
    put("fonts/corpus.woff", b"wOFF" + struct.pack(">IIHHIHHIIIII", 0x00010000, off, len(tags), 0, sfnt_size, 1, 0, 0, 0, 0, 0, 0) + ents + body, "WOFF font", "")
    try:
        import brotli
    except ImportError:
        sys.exit("corpus_formats: needs python3-brotli (WOFF2)")
    known = [b"cmap", b"head", b"hhea", b"hmtx", b"maxp", b"name", b"post", b"glyf", b"loca"]
    flags = {b"cmap": 0, b"head": 1, b"hhea": 2, b"hmtx": 3, b"maxp": 4, b"name": 5, b"post": 10, b"glyf": 10, b"loca": 11}
    # table-directory flag bytes: index of the tag in the standard list; glyf and loca use transform version 3 (the null transform)
    std = {b"cmap": 0, b"head": 1, b"hhea": 2, b"hmtx": 3, b"maxp": 4, b"name": 5, b"OS/2": 6, b"post": 7, b"glyf": 10, b"loca": 11}
    d2, stream = b"", b""
    for k in sorted(t, key=lambda x: std[x]):
        fb = std[k] | (0xC0 if k in (b"glyf", b"loca") else 0)
        d2 += bytes([fb]) + base128(len(t[k]))
        if k in (b"glyf", b"loca"):
            d2 += base128(len(t[k]))
        stream += t[k]
    comp2 = brotli.compress(stream, quality=11)
    total = 48 + len(d2) + len(comp2)
    hdr = b"wOF2" + struct.pack(">IIHHIIHHIIIII", 0x00010000, total + (-total % 4), len(t), 0, sfnt_size, len(comp2), 1, 0, 0, 0, 0, 0, 0)
    out = hdr + d2 + comp2
    put("fonts/corpus.woff2", out + b"\0" * (-len(out) % 4), "WOFF2 font", "brotli, null-transformed tables")
    glyphs8 = noise_font(16)
    put("fonts/console.psf", b"\x72\xb5\x4a\x86" + struct.pack("<IIIIIII", 0, 32, 0, 256, 16, 16, 8) + glyphs8, "PC Screen Font (PSF 2)", "256 glyphs, 8 x 16")
    put("fonts/console1.psf", b"\x36\x04\x00\x10" + glyphs8, "PC Screen Font (PSF 1)", "256 glyphs, 8 x 16")


def base128(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))


# ---- 3D models ----------------------------------------------------------------------------------------------------------------------------

def models():
    put("models/cube.obj", b"# a cube\no cube\nv -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\nv -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\nf 1 2 3 4\nf 5 6 7 8\nf 1 2 6 5\nf 2 3 7 6\nf 3 4 8 7\nf 4 1 5 8\n", "Wavefront OBJ 3D model", "8 vertices, 6 faces")
    put("models/triangle.stl", b"solid corpus\n facet normal 0 0 1\n  outer loop\n   vertex 0 0 0\n   vertex 1 0 0\n   vertex 0 1 0\n  endloop\n endfacet\nendsolid corpus\n", "STL 3D model (text)", "ASCII")
    put("models/triangle-binary.stl", b"r3i6nos corpus binary STL".ljust(80, b"\0") + struct.pack("<I", 1) + struct.pack("<12fH", 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0), "STL 3D model (binary)", "binary")
    pos = struct.pack("<9f", 0, 0, 0, 1, 0, 0, 0, 1, 0)
    js = json.dumps({"asset": {"version": "2.0", "generator": "r3i6nos corpus"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
                     "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}], "buffers": [{"byteLength": 36}],
                     "bufferViews": [{"buffer": 0, "byteLength": 36}], "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]}]}, separators=(",", ":")).encode()
    js += b" " * (-len(js) % 4)
    put("models/triangle.glb", b"glTF" + struct.pack("<II", 2, 12 + 8 + len(js) + 8 + 36) + struct.pack("<I", len(js)) + b"JSON" + js + struct.pack("<I", 36) + b"BIN\0" + pos, "glTF binary (GLB) 3D model", "one triangle")
    import base64
    gl = json.loads(js)
    gl["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64.b64encode(pos).decode()
    put("models/triangle.gltf", json.dumps(gl, indent=1).encode() + b"\n", "glTF JSON 3D model", "one triangle, embedded buffer")


# ---- main ---------------------------------------------------------------------------------------------------------------------------------------

def verify(commander):
    bad = 0
    for path, (label, what) in sorted(REGISTRY.items()):
        out = subprocess.run([commander, "--info", str(OUT / path)], capture_output=True, text=True).stdout.splitlines()
        got = out[0].split(None, 1)[1].strip() if out and out[0].startswith("Type") and len(out[0].split(None, 1)) > 1 else "(none)"
        if got != label:
            bad += 1
            print(f"  MISMATCH {path}: Commander says {got!r}, expected {label!r}")
    print(f"commander --info: {len(REGISTRY) - bad} of {len(REGISTRY)} files give the expected Type" + ("" if not bad else f", {bad} do not"))
    return bad


def main(argv):
    global OUT
    if len(argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    OUT = Path(argv[1])
    for fn in (images, art, audio, music_stubs, video, docs, text_files, data, archives, disks, programs, fonts, models):
        fn()
    (OUT / "_registry.json").write_text(json.dumps(REGISTRY, indent=1, sort_keys=True))
    print(f"corpus_formats: {len(REGISTRY)} files in {OUT}")
    if len(argv) > 3 and argv[2] == "--verify":
        return 1 if verify(argv[3]) else 0
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

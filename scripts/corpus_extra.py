#!/usr/bin/env python3
"""corpus_extra.py - the parts of r3i6nos's test corpus made here, and its manifest (corpus/README.md, scripts/build_corpus.sh).

    corpus_extra.py mxl <notation.musicxml> <out.mxl>    MusicXML, zipped as .mxl (with META-INF/container.xml)
    corpus_extra.py love <folder> <out.love>             a LÖVE app: the folder's files zipped
    corpus_extra.py deb <command> <out.deb>              a command-line tool package (deb_watcher's tool marker), architecture all
    corpus_extra.py text <out.txt>                       UTF-8 text with box drawing, accents and shades (Commander's text view)
    corpus_extra.py binary <out.bin>                     every byte value twice (Commander's hex view)
    corpus_extra.py manifest <files folder>              write MANIFEST.tsv; fails on a file not in FORMATS, or one in FORMATS missing

Every file is the same byte for byte each time it is made (fixed times in the zips and tars). Standard library only.
"""
import gzip
import io
import sys
import tarfile
import zipfile
from pathlib import Path

STAMP = (2026, 11, 9, 0, 0, 0)   # the zips' file times: fixed, so a rebuild is identical
MTIME = 1794182400                # the same moment for the tars (2026-11-09 00:00 UTC)

NEU, COM, PAI, ANI, KLA, CMD, MENU = "Neumetik", "Composer", "Painter", "Animator", "KlamANSI", "Commander", "the menu"

# file: (format, read by, made by). Every app also opens any of these in Commander (text or hex view).
FORMATS = {
    "midi.mid": ("Standard MIDI File (General MIDI, 7 parts)", f"{NEU}, {COM} (lint, notate)", "Neumetik's generator (r36s/lib gen::midi_song)"),
    "midi-riff.rmi": ("RIFF RMID (MIDI in a RIFF chunk)", NEU, "Neumetik's corpus example"),
    "protracker.mod": ("ProTracker MOD (M.K., 4 channels)", NEU, "Neumetik's generator (gen::module)"),
    "fasttracker-8chn.mod": ("FastTracker 8CHN MOD (8 channels)", NEU, "Neumetik's generator (gen::module)"),
    "screamtracker.s3m": ("Scream Tracker 3 S3M", NEU, "Neumetik's tracker export"),
    "adlib.s3m": ("Scream Tracker 3 S3M with AdLib (OPL2) instruments", NEU, "Neumetik's tracker tests (s3m.rs)"),
    "fasttracker2.xm": ("FastTracker 2 XM", NEU, "Neumetik's tracker export"),
    "impulsetracker.it": ("Impulse Tracker IT", NEU, "Neumetik's tracker export"),
    "neumetik.ntt": ("Neumetik tracker file (UMF .ntt)", f"{NEU}, {COM} (writes it)", "Neumetik's UMF writer"),
    "tiny.ahx": ("AHX (Abyss' Highest eXperience)", NEU, "Neumetik's tracker tests (synth.rs)"),
    "tiny.st": ("Sawteeth", NEU, "Neumetik's tracker tests (synth.rs)"),
    "tiny.kt": ("Klystrack", NEU, "Neumetik's tracker tests (synth.rs)"),
    "tiny.v2m": ("V2 (farbrausch) V2M", NEU, "Neumetik's tracker tests (synth.rs)"),
    "tiny.ml": ("Musicline Editor", NEU, "Neumetik's tracker tests (musicline.rs)"),
    "tiny.ptk": ("ProTrekkr / NoiseTrekker", NEU, "Neumetik's tracker tests (ptk.rs)"),
    "notation.musicxml": ("MusicXML", NEU, "Neumetik's text writer"),
    "notation.mxl": ("MusicXML, compressed (.mxl)", NEU, "this script, from notation.musicxml"),
    "notation.abc": ("ABC notation", NEU, "Neumetik's text writer"),
    "notation.krn": ("Humdrum **kern", NEU, "Neumetik's text writer"),
    "notation.cho": ("ChordPro", NEU, "Neumetik's text writer"),
    "notation.alda": ("Alda", NEU, "Neumetik's text writer"),
    "notation.mini": ("Strudel / Tidal mini-notation", NEU, "Neumetik's text writer"),
    "notation.rtttl": ("RTTTL (ringtone)", NEU, "Neumetik's text writer"),
    "notation.tmf": ("TMF (text MIDI)", NEU, "Neumetik's text writer"),
    "notation.flmml": ("FlMML", NEU, "hand-written in Neumetik's corpus example"),
    "notation.sion": ("SiON MML", NEU, "hand-written in Neumetik's corpus example"),
    "mml-generic.mml": ("MML, plain (Mabinogi syntax)", NEU, "Neumetik's MML writer"),
    "mml-mabinogi.mml": ("MML, Mabinogi", NEU, "Neumetik's MML writer"),
    "mml-archeage.mml": ("MML, ArcheAge", NEU, "Neumetik's MML writer"),
    "mml-maplestory2.ms2mml": ("MML, MapleStory 2 (.ms2mml)", NEU, "Neumetik's MML writer"),
    "mml-gwbasic.bas": ("MML, GW-BASIC PLAY", NEU, "Neumetik's MML writer"),
    "mml-msx.mml": ("MML, MSX BASIC", NEU, "Neumetik's MML writer"),
    "mml-msx-music.mml": ("MML, MSX-MUSIC (FM)", NEU, "Neumetik's MML writer"),
    "mml-n88.mml": ("MML, N88-BASIC", NEU, "Neumetik's MML writer"),
    "mml-c128.mml": ("MML, Commodore 128", NEU, "Neumetik's MML writer"),
    "mml-zx128.mml": ("MML, ZX Spectrum 128", NEU, "Neumetik's MML writer"),
    "mml-sakura.mml": ("MML, Sakura (TextSakura)", NEU, "Neumetik's MML writer"),
    "notation.cmp": ("Composer notation (.cmp)", COM, "Composer (compose notate)"),
    "cue-sheet.json": ("Composer cue sheet (JSON)", f"{COM} (compose cues)", "hand-written (corpus/src)"),
    "painter.png": ("PNG image (RGBA)", f"{PAI} (lint, convert, study)", "Painter (sprites draw item)"),
    "painter.ans": ("ANSI art (CP437, SAUCE)", f"{PAI} (lint)", "Painter (sprites draw item --style ansi)"),
    "animator-clip.json": ("Animator clip (animator-clip JSON)", f"{ANI} (lint)", "Animator (animate make walk)"),
    "test-corpus.kan": ("KlamANSI drawing (.kan, klam 1)", KLA, "hand-written (corpus/src)"),
    "hello.love": ("LÖVE app (.love)", f"{MENU}, LÖVE 11.5 and 12.0", "this script, from corpus/src/hello-love"),
    "r3i6nos-test-tool.deb": ("Debian package, a command-line tool (.deb)", f"{MENU} (deb_watcher, from native_deb/)", "this script, from corpus/src/tool"),
    "text-utf8.txt": ("UTF-8 text", CMD, "this script"),
    "binary.bin": ("binary (every byte value)", CMD, "this script"),
}

# Which files each app's own folder on EASYROMS gets (flash-macos.sh): the apps that only browse their own folder.
FOR_NEUMETIK = sorted(f for f, (_, by, _) in FORMATS.items() if by.startswith(NEU))
FOR_KLAMANSI = ["test-corpus.kan"]


def zip_files(out, members):
    """members: [(name in the zip, bytes, compress)]; written with fixed times."""
    with zipfile.ZipFile(out, "w") as z:
        for name, data, deflate in members:
            info = zipfile.ZipInfo(name, STAMP)
            info.compress_type = zipfile.ZIP_DEFLATED if deflate else zipfile.ZIP_STORED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)


def mxl(src, out):
    container = ('<?xml version="1.0" encoding="UTF-8"?>\n<container><rootfiles>'
                 '<rootfile full-path="score.musicxml" media-type="application/vnd.recordare.musicxml+xml"/>'
                 '</rootfiles></container>\n').encode()
    zip_files(out, [("mimetype", b"application/vnd.recordare.musicxml", False), ("META-INF/container.xml", container, True),
                    ("score.musicxml", Path(src).read_bytes(), True)])


def love(folder, out):
    zip_files(out, [(p.name, p.read_bytes(), True) for p in sorted(Path(folder).iterdir()) if p.is_file()])


def tar_gz(entries):
    """entries: [(path, bytes or None for a folder, mode)] -> a .tar.gz with fixed owners and times."""
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.GNU_FORMAT) as t:
        for path, data, mode in entries:
            info = tarfile.TarInfo(path)
            info.mtime, info.uid, info.gid, info.uname, info.gname, info.mode = MTIME, 0, 0, "root", "root", mode
            if data is None:
                info.type = tarfile.DIRTYPE
                t.addfile(info)
            else:
                info.size = len(data)
                t.addfile(info, io.BytesIO(data))
    out = io.BytesIO()
    with gzip.GzipFile(fileobj=out, mode="wb", mtime=0) as g:
        g.write(raw.getvalue())
    return out.getvalue()


def ar(members):
    """The ar archive a .deb is: [(name, bytes)]."""
    out = bytearray(b"!<arch>\n")
    for name, data in members:
        out += f"{name:<16}{MTIME:<12}{0:<6}{0:<6}{0o100644:<8o}{len(data):<10}`\n".encode()
        out += data
        if len(data) % 2:
            out += b"\n"
    return bytes(out)


def deb(command, out):
    cmd = Path(command)
    name = cmd.name
    control = (f"Package: r3i6nos-test-tool\nVersion: 1.0\nArchitecture: all\nMaintainer: r3i6nos\nSection: misc\nPriority: optional\n"
               f"Description: r3i6nos test corpus: a command-line tool package\n The {name} command, which says it works.\n").encode()
    data = tar_gz([("./", None, 0o755), ("./usr/", None, 0o755), ("./usr/bin/", None, 0o755), (f"./usr/bin/{name}", cmd.read_bytes(), 0o755),
                   ("./usr/share/", None, 0o755), ("./usr/share/r3i6nos/", None, 0o755),
                   ("./usr/share/r3i6nos/tool", b"r3i6nos test corpus\n", 0o644)])
    Path(out).write_bytes(ar([("debian-binary", b"2.0\n"), ("control.tar.gz", tar_gz([("./", None, 0o755), ("./control", control, 0o644)])),
                              ("data.tar.gz", data)]))


def text(out):
    lines = [
        "r3i6nos test corpus: UTF-8 text for Commander's viewer.",
        "",
        "Box drawing:  ┌──┬──┐  ╔══╦══╗",
        "              │  │  │  ║  ║  ║",
        "              └──┴──┘  ╚══╩══╝",
        "Shades:       ░░▒▒▓▓██",
        "Accents:      café naïve Ångström Øre façade",
        "Symbols:      ♪ ♫ ☺ ☻ ♥ ♦ ♣ ♠ • ° ± ÷ ½ ¼",
        "Not in CP437: 中文 ✓ (shown as a stand-in)",
        "A long line: " + " ".join(f"word{i}" for i in range(40)),
        "\tA tab at the start.",
    ]
    Path(out).write_text("\n".join(lines) + "\n", encoding="utf-8")


def binary(out):
    Path(out).write_bytes(bytes(range(256)) * 2)


def manifest(folder):
    folder = Path(folder)
    have = {p.name for p in folder.iterdir() if p.is_file() and p.name != "MANIFEST.tsv"}
    extra, missing = sorted(have - set(FORMATS)), sorted(set(FORMATS) - have)
    if extra or missing:
        sys.exit(f"corpus_extra: not in FORMATS: {extra}; missing from the folder: {missing}")
    rows = ["file\tbytes\tformat\tread by\tmade by"]
    for name, (fmt, by, made) in FORMATS.items():
        rows.append(f"{name}\t{(folder / name).stat().st_size}\t{fmt}\t{by}\t{made}")
    (folder / "MANIFEST.tsv").write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"MANIFEST.tsv: {len(FORMATS)} files")


def main(argv):
    cmd, args = (argv[1], argv[2:]) if len(argv) > 1 else ("", [])
    table = {"mxl": (mxl, 2), "love": (love, 2), "deb": (deb, 2), "text": (text, 1), "binary": (binary, 1), "manifest": (manifest, 1)}
    if cmd == "list":   # for flash-macos.sh: which files go into an app's own folder
        print("\n".join({"neumetik": FOR_NEUMETIK, "klamansi": FOR_KLAMANSI}[args[0]]))
        return 0
    if cmd not in table or len(args) != table[cmd][1]:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    table[cmd][0](*args)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

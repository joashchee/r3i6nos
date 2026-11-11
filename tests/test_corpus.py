"""The test corpus (corpus/README.md): every file listed and committed, sorted into sub-folders, each the format it says, small, the parts made here
rebuilt byte for byte, and every file Commander's viewer reads identified by Commander itself. Each file's opening by its own app is checked by
scripts/build_corpus.sh, which needs the apps' repositories and Docker; this needs neither (it builds Commander with cc when it can)."""
import gzip
import importlib.util
import io
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FILES = ROOT / "corpus" / "files"
spec = importlib.util.spec_from_file_location("corpus_extra", ROOT / "scripts" / "corpus_extra.py")
extra = importlib.util.module_from_spec(spec)
sys.dont_write_bytecode = True
spec.loader.exec_module(extra)

FOLDERS = {"apps", "archives", "art", "audio", "data", "disks", "documents", "fonts", "images", "models", "music", "programs", "text", "video"}


def ar_members(data):
    assert data.startswith(b"!<arch>\n")
    i, out = 8, {}
    while i < len(data):
        name, size = data[i:i + 16].decode().strip(), int(data[i + 48:i + 58])
        out[name] = data[i + 60:i + 60 + size]
        i += 60 + size + (size & 1)
    return out


def tar_names(gz):
    with tarfile.open(fileobj=io.BytesIO(gzip.decompress(gz))) as t:
        return {m.name: m for m in t.getmembers()}, t


def on_disk():
    return {p.relative_to(FILES).as_posix() for p in FILES.rglob("*") if p.is_file() and p.name not in (".DS_Store",)}


def manifest_rows():
    rows = (FILES / "MANIFEST.tsv").read_text(encoding="utf-8").splitlines()
    return rows[0], [r.split("\t") for r in rows[1:]]


class Corpus(unittest.TestCase):
    def read(self, name):
        return (FILES / name).read_bytes()

    def test_the_manifest_lists_every_file(self):
        head, rows = manifest_rows()
        self.assertEqual(head, "file\tbytes\tformat\tread by\tmade by\tcommander type")
        listed = {r[0]: int(r[1]) for r in rows}
        self.assertEqual(set(listed), on_disk() - {"MANIFEST.tsv"})
        self.assertTrue({f"{extra.WHERE[n]}/{n}" for n in extra.FORMATS} <= set(listed), "every file the apps make is listed")
        for path, size in listed.items():
            self.assertEqual((FILES / path).stat().st_size, size, f"{path}: the manifest's size is out of date (run scripts/build_corpus.sh)")
        for r in rows:
            self.assertEqual(len(r), 6, r[0])
            self.assertTrue(r[2] and r[3] and r[4], r[0])

    def test_sorted_into_sub_folders(self):
        top = {p.name for p in FILES.iterdir() if p.name != ".DS_Store"}
        self.assertEqual(top, FOLDERS | {"MANIFEST.tsv"}, "no loose files: every one is in a sub-folder")
        for folder in FOLDERS:
            self.assertTrue(any((FILES / folder).rglob("*")), f"{folder}/ is empty")
        for path in on_disk():
            if path == "MANIFEST.tsv":
                continue
            self.assertIn(path.split("/")[0], FOLDERS, path)
        by_folder = {
            "audio": (".wav", ".aiff", ".flac", ".mp3", ".ogg", ".opus", ".wv", ".m4a", ".wma"),
            "video": (".mp4", ".mov", ".avi", ".mkv", ".webm", ".flv", ".wmv", ".mpg", ".ts", ".ogv", ".3gp", ".r3f", ".wav"),
            "images": (".png", ".jpg", ".gif", ".bmp", ".ico", ".tga", ".pcx", ".ppm", ".pgm", ".pbm", ".pnm", ".tiff", ".webp", ".qoi", ".psd", ".heic", ".avif", ".svg"),
        }
        for folder, exts in by_folder.items():
            for p in (FILES / folder).rglob("*"):
                self.assertTrue(p.name.endswith(exts), f"{p.relative_to(FILES)} does not belong in {folder}/")

    def test_every_file_is_committed(self):
        # .gitignore ignores *.love, *.deb, *.img and *.zip; the corpus's own are excepted, and a file on disk but not in git passes here but not in CI
        try:
            out = subprocess.run(["git", "-C", str(ROOT), "ls-files", "corpus/files"], capture_output=True, text=True, check=True).stdout
        except (OSError, subprocess.CalledProcessError):
            self.skipTest("not a git checkout")
        tracked = {p[len("corpus/files/"):] for p in out.splitlines()}
        self.assertEqual(on_disk() - tracked, set(), "corpus files not in git (an ignore rule?): git add -f them, or fix .gitignore")

    def test_small(self):
        sizes = {p: (FILES / p).stat().st_size for p in on_disk()}
        self.assertLess(sum(sizes.values()), 4_000_000, "the whole corpus stays under 4 MB")
        for name, size in sizes.items():
            self.assertLess(size, 200_000, name)
            if not name.endswith(("empty.txt",)):
                self.assertGreater(size, 0, name)

    def test_each_app_made_file_is_its_format(self):
        r = self.read
        self.assertTrue(r("music/midi/midi.mid").startswith(b"MThd"))
        self.assertEqual((r("music/midi/midi-riff.rmi")[:4], r("music/midi/midi-riff.rmi")[8:16], r("music/midi/midi-riff.rmi")[20:24]), (b"RIFF", b"RMIDdata", b"MThd"))
        t = "music/trackers/"
        self.assertEqual(r(t + "protracker.mod")[1080:1084], b"M.K.")
        self.assertEqual(r(t + "fasttracker-8chn.mod")[1080:1084], b"8CHN")
        for s3m in ("screamtracker.s3m", "adlib.s3m"):
            self.assertEqual(r(t + s3m)[44:48], b"SCRM", s3m)
        self.assertTrue(r(t + "fasttracker2.xm").startswith(b"Extended Module: "))
        self.assertTrue(r(t + "impulsetracker.it").startswith(b"IMPM"))
        self.assertTrue(r(t + "tiny.ahx").startswith(b"THX"))
        self.assertTrue(r(t + "tiny.st").startswith(b"SWTD"))
        self.assertTrue(r(t + "tiny.kt").startswith(b"cyd!song"))
        self.assertTrue(r("images/painter.png").startswith(b"\x89PNG\r\n\x1a\n"))
        self.assertIn(b"SAUCE00", r("art/painter.ans"), "an ANSI file with its SAUCE record")
        self.assertTrue(r("art/test-corpus.kan").startswith(b"klam 1\n"))
        self.assertEqual(r("text/binary.bin"), bytes(range(256)) * 2)
        for text in ("music/notation/notation.musicxml", "music/notation/notation.abc", "music/notation/notation.cmp", "music/notation/cue-sheet.json",
                     "data/animator-clip.json", "text/utf8.txt", "music/mml/mml-sakura.mml"):
            r(text).decode("utf-8")   # text, as UTF-8
        self.assertIn(b"<score-partwise", r("music/notation/notation.musicxml"))
        self.assertIn(b"animator-clip", r("data/animator-clip.json"))
        with zipfile.ZipFile(FILES / "music/notation/notation.mxl") as z:
            self.assertEqual(z.read("mimetype"), b"application/vnd.recordare.musicxml")
            self.assertIn(b'full-path="score.musicxml"', z.read("META-INF/container.xml"))
            self.assertEqual(z.read("score.musicxml"), r("music/notation/notation.musicxml"))

    def test_the_formats_commander_reads_are_there(self):
        """The signatures, by hand, of one file in each family: a quick check that needs no build (the next test has Commander say it)."""
        r = self.read
        self.assertTrue(r("images/photo.jpg").startswith(b"\xff\xd8\xff"))
        self.assertTrue(r("images/animation.gif").startswith((b"GIF87a", b"GIF89a")))
        self.assertTrue(r("images/photo.webp")[:4] == b"RIFF" and r("images/photo.webp")[8:12] == b"WEBP")
        self.assertTrue(r("images/layers.psd").startswith(b"8BPS"))
        self.assertTrue(r("audio/tone.flac").startswith(b"fLaC"))
        self.assertTrue(r("audio/vorbis.ogg").startswith(b"OggS"))
        self.assertEqual(r("video/clip.mp4")[4:8], b"ftyp")
        self.assertTrue(r("video/clip.mkv").startswith(b"\x1a\x45\xdf\xa3"))
        self.assertTrue(r("documents/doc.pdf").startswith(b"%PDF-"))
        self.assertTrue(r("archives/stored.rar").startswith(b"Rar!\x1a\x07\x00") and r("archives/stored5.rar").startswith(b"Rar!\x1a\x07\x01\x00"))
        self.assertTrue(r("archives/game.7z").startswith(b"7z\xbc\xaf\x27\x1c"))
        self.assertEqual(r("disks/corpus.iso")[32769:32774], b"CD001")
        self.assertTrue(r("programs/hello").startswith(b"\x7fELF"))
        self.assertTrue(r("fonts/corpus.ttf").startswith(b"\x00\x01\x00\x00"))
        self.assertTrue(r("models/triangle.glb").startswith(b"glTF"))
        self.assertTrue(r("data/music.db").startswith(b"SQLite format 3\x00"))
        # the zips among them are really zips, and the OOXML and OpenDocument ones hold what their readers look for
        for z in ("archives/bundle.zip", "archives/tool.jar", "documents/letter.docx", "documents/sheet.xlsx", "documents/slides.pptx", "documents/letter.odt", "documents/book.epub"):
            self.assertTrue(zipfile.is_zipfile(FILES / z), z)
        with zipfile.ZipFile(FILES / "documents/letter.odt") as zf:
            self.assertEqual(zf.namelist()[0], "mimetype")
        with zipfile.ZipFile(FILES / "archives/tool.jar") as zf:
            self.assertIn("META-INF/MANIFEST.MF", zf.namelist())

    def test_commander_identifies_every_file_made_for_it(self):
        cc, sdl = shutil.which("cc") or shutil.which("gcc"), shutil.which("sdl2-config")
        if not cc or not sdl:
            self.skipTest("needs a C compiler and libsdl2-dev (sdl2-config) to build Commander")
        _, rows = manifest_rows()
        commander_rows = [r for r in rows if r[5]]
        self.assertGreater(len(commander_rows), 150, "the corpus covers well over 150 of Commander's formats")
        with tempfile.TemporaryDirectory() as d:
            exe = os.path.join(d, "commander")
            flags = subprocess.run([sdl, "--cflags", "--libs"], capture_output=True, text=True, check=True).stdout.split()
            subprocess.run([cc, "-std=c11", "-O1", "-w", "-I", str(ROOT / "theme"), "-o", exe, str(ROOT / "apps/commander-native/commander.c")] + flags + ["-lm"], check=True)
            bad = []
            for r in commander_rows:
                out = subprocess.run([exe, "--info", str(FILES / r[0])], capture_output=True, text=True).stdout.splitlines()
                got = out[0].split(None, 1)[1].strip() if out and out[0].startswith("Type") and len(out[0].split(None, 1)) > 1 else "(none)"
                if got != r[5]:
                    bad.append(f"{r[0]}: Commander says {got!r}, the manifest says {r[5]!r}")
            self.assertEqual(bad, [], "Commander no longer identifies these as the manifest says (or the manifest is out of date: scripts/build_corpus.sh)")

    def test_the_love_app(self):
        with zipfile.ZipFile(FILES / "apps/hello.love") as z:
            self.assertEqual(sorted(z.namelist()), ["conf.lua", "main.lua"], "main.lua at the top, as LÖVE needs")
            self.assertEqual(z.read("main.lua"), (ROOT / "corpus/src/hello-love/main.lua").read_bytes())

    def test_the_tool_package(self):
        m = ar_members(self.read("apps/r3i6nos-test-tool.deb"))
        self.assertEqual(list(m), ["debian-binary", "control.tar.gz", "data.tar.gz"])
        self.assertEqual(m["debian-binary"], b"2.0\n")
        names, t = tar_names(m["control.tar.gz"])
        self.assertIn("./control", names)
        data, t = tar_names(m["data.tar.gz"])
        self.assertIn("./usr/share/r3i6nos/tool", data, "deb_watcher's tool marker: no menu entry, a command on PATH")
        self.assertEqual(data["./usr/bin/r3i6nos-test-hello"].mode & 0o111, 0o111, "the command is executable")

    def test_the_parts_made_here_rebuild_identically(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            extra.mxl(FILES / "music/notation/notation.musicxml", d / "notation.mxl")
            extra.love(ROOT / "corpus/src/hello-love", d / "hello.love")
            extra.deb(ROOT / "corpus/src/tool/r3i6nos-test-hello", d / "r3i6nos-test-tool.deb")
            for name in ("notation.mxl", "hello.love", "r3i6nos-test-tool.deb"):
                self.assertEqual((d / name).read_bytes(), self.read(f"{extra.WHERE[name]}/{name}"), f"{name} differs from a rebuild (run scripts/build_corpus.sh)")
        for name, src in (("art/test-corpus.kan", "test-corpus.kan"), ("music/notation/cue-sheet.json", "cue-sheet.json")):
            self.assertEqual(self.read(name), (ROOT / "corpus/src" / src).read_bytes(), name)

    def test_the_lists_for_the_apps_folders(self):
        neu = extra.FOR_NEUMETIK
        self.assertIn("music/midi/midi.mid", neu)
        self.assertIn("music/trackers/tiny.ahx", neu)
        self.assertIn("music/notation/notation.mxl", neu)
        self.assertNotIn("images/painter.png", neu, "only what Neumetik plays")
        self.assertEqual(extra.FOR_KLAMANSI, ["art/test-corpus.kan"])
        for name in neu + extra.FOR_KLAMANSI:
            self.assertTrue((FILES / name).is_file(), name)

    def test_organise_moves_the_flat_files(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            for n in extra.WHERE:
                (d / n).write_bytes(b"x")
            extra.organise(d)
            self.assertEqual({p.name for p in d.iterdir()}, {"music", "images", "art", "data", "apps"})
            for n, where in extra.WHERE.items():
                self.assertTrue((d / where / n).is_file(), n)


if __name__ == "__main__":
    unittest.main()

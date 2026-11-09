"""The test corpus (corpus/README.md): every file listed, each the format it says, small, and the parts made here rebuilt byte for byte.
Each file's opening by its app is checked by scripts/build_corpus.sh, which needs the apps' repositories; this needs nothing."""
import gzip
import importlib.util
import io
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


class Corpus(unittest.TestCase):
    def read(self, name):
        return (FILES / name).read_bytes()

    def test_the_manifest_lists_every_file(self):
        rows = (FILES / "MANIFEST.tsv").read_text(encoding="utf-8").splitlines()
        self.assertEqual(rows[0], "file\tbytes\tformat\tread by\tmade by")
        listed = {r.split("\t")[0]: int(r.split("\t")[1]) for r in rows[1:]}
        on_disk = {p.name for p in FILES.iterdir() if p.name != "MANIFEST.tsv"}
        self.assertEqual(set(listed), on_disk)
        self.assertEqual(set(listed), set(extra.FORMATS))
        for name, size in listed.items():
            self.assertEqual((FILES / name).stat().st_size, size, f"{name}: the manifest's size is out of date (run scripts/build_corpus.sh)")

    def test_small(self):
        sizes = {p.name: p.stat().st_size for p in FILES.iterdir()}
        self.assertLess(sum(sizes.values()), 1_000_000, "the whole corpus stays under 1 MB")
        for name, size in sizes.items():
            self.assertLess(size, 200_000, name)
            self.assertGreater(size, 0, name)

    def test_each_file_is_its_format(self):
        r = self.read
        self.assertTrue(r("midi.mid").startswith(b"MThd"))
        self.assertEqual((r("midi-riff.rmi")[:4], r("midi-riff.rmi")[8:16], r("midi-riff.rmi")[20:24]), (b"RIFF", b"RMIDdata", b"MThd"))
        self.assertEqual(r("protracker.mod")[1080:1084], b"M.K.")
        self.assertEqual(r("fasttracker-8chn.mod")[1080:1084], b"8CHN")
        for s3m in ("screamtracker.s3m", "adlib.s3m"):
            self.assertEqual(r(s3m)[44:48], b"SCRM", s3m)
        self.assertTrue(r("fasttracker2.xm").startswith(b"Extended Module: "))
        self.assertTrue(r("impulsetracker.it").startswith(b"IMPM"))
        self.assertTrue(r("tiny.ahx").startswith(b"THX"))
        self.assertTrue(r("tiny.st").startswith(b"SWTD"))
        self.assertTrue(r("tiny.kt").startswith(b"cyd!song"))
        self.assertTrue(r("painter.png").startswith(b"\x89PNG\r\n\x1a\n"))
        self.assertIn(b"SAUCE00", r("painter.ans"), "an ANSI file with its SAUCE record")
        self.assertTrue(r("test-corpus.kan").startswith(b"klam 1\n"))
        self.assertEqual(r("binary.bin"), bytes(range(256)) * 2)
        for text in ("notation.musicxml", "notation.abc", "notation.cmp", "cue-sheet.json", "animator-clip.json", "text-utf8.txt", "mml-sakura.mml"):
            r(text).decode("utf-8")   # text, as UTF-8
        self.assertIn(b"<score-partwise", r("notation.musicxml"))
        self.assertIn(b"animator-clip", r("animator-clip.json"))
        with zipfile.ZipFile(FILES / "notation.mxl") as z:
            self.assertEqual(z.read("mimetype"), b"application/vnd.recordare.musicxml")
            self.assertIn(b'full-path="score.musicxml"', z.read("META-INF/container.xml"))
            self.assertEqual(z.read("score.musicxml"), r("notation.musicxml"))

    def test_the_love_app(self):
        with zipfile.ZipFile(FILES / "hello.love") as z:
            self.assertEqual(sorted(z.namelist()), ["conf.lua", "main.lua"], "main.lua at the top, as LÖVE needs")
            self.assertEqual(z.read("main.lua"), (ROOT / "corpus/src/hello-love/main.lua").read_bytes())

    def test_the_tool_package(self):
        m = ar_members(self.read("r3i6nos-test-tool.deb"))
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
            extra.mxl(FILES / "notation.musicxml", d / "notation.mxl")
            extra.love(ROOT / "corpus/src/hello-love", d / "hello.love")
            extra.deb(ROOT / "corpus/src/tool/r3i6nos-test-hello", d / "r3i6nos-test-tool.deb")
            extra.text(d / "text-utf8.txt")
            extra.binary(d / "binary.bin")
            for name in ("notation.mxl", "hello.love", "r3i6nos-test-tool.deb", "text-utf8.txt", "binary.bin"):
                self.assertEqual((d / name).read_bytes(), self.read(name), f"{name} differs from a rebuild (run scripts/build_corpus.sh)")
        for name in ("test-corpus.kan", "cue-sheet.json"):
            self.assertEqual(self.read(name), (ROOT / "corpus/src" / name).read_bytes(), name)

    def test_the_lists_for_the_apps_folders(self):
        neu = extra.FOR_NEUMETIK
        self.assertIn("midi.mid", neu)
        self.assertIn("tiny.ahx", neu)
        self.assertIn("notation.mxl", neu)
        self.assertNotIn("painter.png", neu, "only what Neumetik plays")
        self.assertEqual(extra.FOR_KLAMANSI, ["test-corpus.kan"])
        for name in neu + extra.FOR_KLAMANSI:
            self.assertTrue((FILES / name).is_file(), name)


if __name__ == "__main__":
    unittest.main()

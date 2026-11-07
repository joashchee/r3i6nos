import importlib.util
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("pack_love", ROOT / "scripts/pack_love.py")
pl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pl)
APP = ROOT / "apps/input-checker-love"


class Pack(unittest.TestCase):
    def test_archive_has_main_lua_at_the_top(self):
        out = Path(tempfile.mkdtemp()) / "a.love"
        n = pl.pack(str(APP), str(out))
        names = zipfile.ZipFile(out).namelist()
        self.assertEqual(n, len(names))
        for want in ("main.lua", "conf.lua", "model.lua"):
            self.assertIn(want, names)
        self.assertFalse([x for x in names if x.startswith("apps/") or "/" in x.split("main.lua")[0]])

    def test_same_source_gives_identical_bytes(self):
        d = Path(tempfile.mkdtemp())
        pl.pack(str(APP), str(d / "one.love"))
        pl.pack(str(APP), str(d / "two.love"))
        self.assertEqual((d / "one.love").read_bytes(), (d / "two.love").read_bytes())

    def test_hidden_files_are_left_out_and_main_lua_is_required(self):
        src = Path(tempfile.mkdtemp())
        (src / "main.lua").write_text("x")
        (src / ".DS_Store").write_text("junk")
        out = Path(tempfile.mkdtemp()) / "b.love"
        pl.pack(str(src), str(out))
        self.assertEqual(zipfile.ZipFile(out).namelist(), ["main.lua"])
        with self.assertRaises(SystemExit):
            pl.pack(str(Path(tempfile.mkdtemp())), str(out))


if __name__ == "__main__":
    unittest.main()

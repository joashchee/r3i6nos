"""ANSIapps Mono in the theme (theme/font/ansiapps-mono/, scripts/build_ansiapps_font.py; docs/ANSIAPPS-MONO-FONT.md).

The committed tables are compared with each other (the C header, the Lua table, the JSON, the names, code page 437 and the portrait parts), so they cannot drift,
and with the font's own repository when it sits beside this checkout (then the generator is run and its output compared byte for byte).
"""
import json
import os
import re
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
D = ROOT / "theme" / "font" / "ansiapps-mono"
sys.path.insert(0, str(ROOT / "scripts"))


def rows_of(h):
    return [int(h[i:i + 3], 16) for i in range(0, 60, 3)]


def c_header():
    t = (D / "ansiapps_mono10x20.h").read_text()
    glyphs = [[int(v, 16) for v in re.findall(r"0x([0-9a-f]{3})", line)]
              for line in t.split("r3mono_glyph[", 1)[1].split("};", 1)[0].splitlines() if line.startswith("{")]
    cps = [int(x, 16) for x in re.findall(r"0x([0-9a-f]+)", t.split("r3mono_cp[", 1)[1].split("};", 1)[0].split("{", 1)[1])]
    idx = [int(x) for x in re.findall(r"\d+", t.split("r3mono_idx[", 1)[1].split("};", 1)[0].split("{", 1)[1])]
    return t, glyphs, cps, idx


def lua_table():
    t = (D / "ansiapps_mono10x20.lua").read_text()
    glyphs = re.findall(r'^\s+"([0-9a-f]{60})",$', t, re.M)
    cmap = {int(k): int(v) - 1 for k, v in re.findall(r"\[(\d+)\]=(\d+),", t)}
    return glyphs, cmap


class Tables(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.json = json.loads((D / "ansiapps-mono.json").read_text())
        cls.header, cls.hglyphs, cls.hcps, cls.hidx = c_header()
        cls.lglyphs, cls.lcmap = lua_table()

    def test_sizes(self):
        j = self.json
        self.assertEqual((j["width"], j["height"], j["base"]), (10, 20, 15))
        self.assertEqual(len(j["cmap"]), 4583)
        self.assertEqual(len(j["glyphs"]), 4227)
        self.assertIn("#define R3MONO_COUNT 4583", self.header)
        self.assertIn("#define R3MONO_GLYPHS 4227", self.header)

    def test_c_matches_json(self):
        j = self.json
        self.assertEqual(len(self.hglyphs), len(j["glyphs"]))
        for i, g in enumerate(self.hglyphs):
            self.assertEqual(g, rows_of(j["glyphs"][i]), f"glyph {i}")
        self.assertTrue(self.hcps == sorted(int(k) for k in j["cmap"]), "the header's code points are the font's")
        for cp, i in zip(self.hcps, self.hidx):
            self.assertEqual(i, j["cmap"][str(cp)], f"U+{cp:04X}")
        self.assertTrue(self.hcps == sorted(self.hcps), "the code points are sorted for the binary search")

    def test_lua_matches_json(self):
        j = self.json
        self.assertTrue(self.lglyphs == j["glyphs"])
        self.assertTrue(self.lcmap == {int(k): v for k, v in j["cmap"].items()})

    def test_rows_are_ten_bits(self):
        for h in self.json["glyphs"]:
            self.assertEqual(len(h), 60)
            for r in rows_of(h):
                self.assertLess(r, 1024)

    def test_ascii_a_is_drawn_and_space_is_blank(self):
        j = self.json
        self.assertEqual(j["glyphs"][j["cmap"]["32"]], "0" * 60)
        a = rows_of(j["glyphs"][j["cmap"]["65"]])
        self.assertGreater(sum(bin(r).count("1") for r in a), 20)
        self.assertEqual(a[0] | a[1] | a[2], 0, "capitals start at row 3")

    def test_box_lines_join(self):
        """A box line fills its cell edge to edge, so a row of them is continuous (the tiling rule of the 10x20 size)."""
        j = self.json

        def g(cp):
            return rows_of(j["glyphs"][j["cmap"][str(cp)]])
        h, v = g(0x2500), g(0x2502)
        on = [i for i, r in enumerate(h) if r]
        self.assertTrue(on and all(h[i] == 0x3FF for i in on), "─ runs the whole cell width")
        self.assertEqual(len({r for r in v}), 1, "│ is the same on every row")
        self.assertEqual(g(0x2588), [0x3FF] * 20, "the full block fills the cell")
        self.assertEqual(g(0x2580)[:10], [0x3FF] * 10, "the upper half block is exactly ten rows")
        self.assertEqual(g(0x2584)[10:], [0x3FF] * 10, "the lower half block is exactly ten rows")
        self.assertEqual({r for r in g(0x258C)}, {0x3E0}, "the left half block is exactly five columns")


class Names(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.json = json.loads((D / "ansiapps-mono.json").read_text())
        cls.names = cls.json["names"]
        cls.cmap = {int(k) for k in cls.json["cmap"]}

    def test_every_cell_of_every_name_exists(self):
        for n, e in self.names.items():
            for k in range(e["cols"] * e["rows"]):
                self.assertIn(e["cp"] + k, self.cmap, f"{n} cell {k}")

    def test_the_old_names_all_stay(self):
        """The 884 names of R3i6nos Mono (its glyph-names.json, append only) keep their code points: apps print by name."""
        for n in ("pad.btn.A.on", "icon.app.hydrolik", "ui.wait.hourglass.0", "piano.bk.off", "key.u0041.up", "staff.clef.treble", "sys.bat.charge", "code.bp.on"):
            self.assertIn(n, self.names)
        self.assertEqual(self.names["pad.btn.A.on"]["cp"], 0x10F001)
        self.assertEqual(self.names["icon.app.hydrolik"]["cp"], 0x10FD14)
        self.assertEqual((self.names["icon.app.hydrolik"]["cols"], self.names["icon.app.hydrolik"]["rows"]), (2, 1))

    def test_wide_and_tall_counterparts(self):
        wide = [n for n in self.names if n.endswith(".wide")]
        tall = [n for n in self.names if n.endswith(".tall")]
        self.assertEqual((len(wide), len(tall)), (113, 28))
        for n in wide:
            b, e = self.names[n[:-5]], self.names[n]
            self.assertEqual((e["cols"], e["rows"]), (2 * b["cols"], b["rows"]), n)
        for n in tall:
            b, e = self.names[n[:-5]], self.names[n]
            self.assertEqual((e["cols"], e["rows"]), (2 * b["cols"], 2 * b["rows"]), n)

    def test_c_and_lua_names_agree(self):
        h = (D / "ansiapps_mono_names.h").read_text()
        entries = re.findall(r'\{"([^"]+)", R3I_\w+, 0x([0-9A-F]+)u, (\d+), (\d+)\}', h)
        base = {n: (int(cp, 16), int(c), int(r)) for n, cp, c, r in entries if not re.search(r"\.row\d$", n)}
        self.assertEqual(len(base), len(self.names))
        for n, e in self.names.items():
            self.assertEqual(base[n], (e["cp"], e["cols"], e["rows"]), n)
        self.assertEqual([x[0] for x in entries], sorted(x[0] for x in entries), "sorted by strcmp for the binary search")
        lua = (D / "ansiapps_mono_names.lua").read_text()
        self.assertEqual(len(re.findall(r'^shapes\["', lua, re.M)), len(self.names))
        self.assertEqual((D / "glyph-names.json").read_text(), json.dumps({n: self.names[n]["cp"] for n in sorted(self.names)}, indent=1) + "\n")

    def test_macro_names_do_not_collide(self):
        macros = re.findall(r"^#define (R3I_\w+) ", (D / "ansiapps_mono_names.h").read_text(), re.M)
        self.assertEqual(len(macros), len(set(macros)))


class Cp437(unittest.TestCase):
    def test_cp437_on_the_font(self):
        j = json.loads((D / "ansiapps-mono.json").read_text())
        t = (D / "ansiapps_cp437.h").read_text()
        cps = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{4})", t.split("r3i6nos_cp437[256]", 1)[1].split("};", 1)[0])]
        self.assertEqual(len(cps), 256)
        self.assertEqual(cps[0x41], 0x41)
        self.assertEqual(cps[0x01], 0x263A, "byte 1 is the smiley picture")
        self.assertEqual(cps[0xC9], 0x2554)
        self.assertEqual(cps[0xE1], 0xDF, "byte 225 is the sharp s")
        g10 = re.findall(r"\{(0x[0-9a-f]{3}(?:,0x[0-9a-f]{3}){19})\}", t.split("ansiapps_cp437_10x20[256][20]", 1)[1])
        self.assertEqual(len(g10), 256)
        h = 2166136261
        for b in range(256):
            want = "0" * 60 if b == 0 else j["glyphs"][j["cmap"][str(cps[b])]]
            got = "".join(x[2:] for x in g10[b].split(","))
            self.assertEqual(got, want, f"byte {b}")
            for r in rows_of(got):
                for x in range(10):
                    h = ((h ^ ((r >> (9 - x)) & 1)) * 16777619) & 0xFFFFFFFF
        self.assertIn(f"R3_GLYPH_HASH 0x{h:08x}u", t)
        lua = (D / "ansiapps_cp437.lua").read_text()
        self.assertIn(f"hash = 0x{h:08x}", lua)

    def test_8x16_for_ansi_art(self):
        t = (D / "ansiapps_cp437.h").read_text()
        g8 = re.findall(r"\{(0x[0-9A-Fa-f]{2}(?:,0x[0-9A-Fa-f]{2}){15})\}", t.split("ansiapps8x16[256][16]", 1)[1].split("};", 1)[0])
        self.assertEqual(len(g8), 256)
        self.assertEqual(g8[0x20].lower(), ",".join(["0x00"] * 16), "space is blank")
        self.assertEqual(g8[0xDB].lower(), ",".join(["0xff"] * 16), "the full block fills the 8x16 cell")


class Portrait(unittest.TestCase):
    def test_parts_are_in_the_font_and_mirror(self):
        j = json.loads((D / "ansiapps-mono.json").read_text())
        t = (D / "ansiapps_portrait.h").read_text()
        self.assertIn("#define AP_PARTS 692", t)
        self.assertIn("#define AP_JOINS 1283", t)
        self.assertIn("#define AP_COMPAT 82", t)
        parts = re.findall(r'\{"(portrait\.[^"]+)", 0x([0-9A-F]+)u, (\d+), (\d+), (\d)\}', t)
        self.assertEqual(len(parts), 692)
        by = {n: int(cp, 16) for n, cp, *_ in parts}
        for n, cp in by.items():
            self.assertIn(str(cp), j["cmap"], n)

        def rows(cp):
            return rows_of(j["glyphs"][j["cmap"][str(cp)]])

        def rev(r):
            return int(format(r, "010b")[::-1], 2)
        pairs = 0
        for n, cp in by.items():
            if n.endswith(".L") and n[:-2] + ".R" in by and not any(w in n for w in (".nose.", ".nosemouth.", ".mouth.", ".bridge.", ".chin.")):
                pairs += 1
                self.assertEqual([rev(r) for r in rows(cp)], rows(by[n[:-2] + ".R"]), n)
        self.assertGreater(pairs, 200)


class Licence(unittest.TestCase):
    def test_licence_files_and_credit(self):
        self.assertIn("SIL OPEN FONT LICENSE", (D / "OFL.txt").read_text())
        self.assertIn("clean-room", (D / "README.txt").read_text())
        notice = (ROOT / "NOTICE").read_text()
        self.assertIn("ANSIapps Mono", notice)
        self.assertIn("SIL Open Font License", notice)
        for f in D.glob("*.h"):
            self.assertIn("SIL OFL 1.1", f.read_text()[:800], f.name)

    def test_no_cc_by_sa_font_left_in_the_theme(self):
        for p in (ROOT / "theme").rglob("*"):
            if p.is_file() and p.name != "README.txt":
                self.assertNotIn("VileR", p.read_text(errors="ignore"), str(p))
        self.assertFalse((ROOT / "theme" / "vga8x16.lua").exists())
        self.assertFalse((ROOT / "theme" / "r3i6nos_vga8x16.h").exists())
        self.assertFalse((ROOT / "theme" / "font" / "r3i6nos-mono").exists())


class Generator(unittest.TestCase):
    @unittest.skipUnless((Path(os.environ.get("ANSIAPPS_MONO_DIR") or ROOT.parent / "ansiapps-mono-font") / "fonts" / "10x20").is_dir(),
                         "the font's repository (ansiapps-mono-font) is not beside this checkout")
    def test_committed_tables_are_what_the_generator_makes(self):
        r = subprocess.run([sys.executable, "-I", str(ROOT / "scripts" / "build_ansiapps_font.py"), "--check"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)


if __name__ == "__main__":
    unittest.main()

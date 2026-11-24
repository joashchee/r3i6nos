"""R3i6nos Mono (scripts/build_r3_font.py, scripts/r3font/): the 10x20 font of r3i6nos and the ansiapps apps. No hardware, no fontTools needed
(the TrueType files are checked in Debian by scripts/build-r3-font.sh and, in LOVE, by tests/test_r3font_love.sh)."""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

import build_r3_font  # noqa: E402
from r3font import base, boxes, emit  # noqa: E402
from r3font.registry import BLOCKS  # noqa: E402

OUT = ROOT / "theme" / "font" / "r3i6nos-mono"
R, MISS = build_r3_font.build()


class Cp437(unittest.TestCase):
    def test_every_byte_is_the_themes_glyph(self):
        glyphs, uni = base.cp437()
        for i in range(256):
            self.assertIn(uni[i], R.all_cps(), f"byte {i}")
        by = base.by_unicode()
        for cp, g in by.items():
            self.assertEqual(R.glyph(cp), g, f"U+{cp:04X} differs from the theme's 8x16 -> 10x20 mapping")

    def test_raw_bytes_mirror_the_code_page(self):
        _, uni = base.cp437()
        for i in range(256):
            self.assertEqual(R.glyph(0xF000 + i), R.glyph(uni[i] if i else 0x20), f"U+F0{i:02X}")

    def test_the_theme_geometry_the_generator_uses(self):
        g = base.by_unicode()
        self.assertEqual([x for x in range(10) if g[0x2502].p[0][x]], [4, 5, 6])      # light vertical: x 4-6
        self.assertEqual([y for y in range(20) if g[0x2500].p[y][0]], [9])             # light horizontal: y 9


class BoxDrawing(unittest.TestCase):
    def test_generator_reproduces_the_vga_light_lines(self):
        by = base.by_unicode()
        for cp in (0x250C, 0x2510, 0x2514, 0x2518, 0x251C, 0x2524, 0x252C, 0x2534, 0x253C, 0x2500, 0x2502):
            import unicodedata
            nm = unicodedata.name(chr(cp))[len("BOX DRAWINGS "):]
            self.assertEqual(boxes.arms(**boxes.parse_arms(nm)), by[cp], nm)

    def test_the_whole_block_is_there(self):
        for cp in range(0x2500, 0x2580):
            self.assertIn(cp, R.all_cps(), f"U+{cp:04X}")
        for cp in range(0x2580, 0x25A0):
            self.assertIn(cp, R.all_cps(), f"U+{cp:04X}")

    def test_strokes_meet_across_cells(self):
        """A glyph whose arm leaves its right edge meets the left edge of one whose arm leaves its left, so lines never break."""
        import unicodedata
        right, left, down, up = {}, {}, {}, {}
        for cp in range(0x2500, 0x2580):
            nm = unicodedata.name(chr(cp))
            if "DOUBLE" in nm or "SINGLE" in nm or "DASH" in nm or "DIAGONAL" in nm or "ARC" in nm:
                continue
            g = R.glyph(cp)
            col9, col0 = tuple(g.p[y][9] for y in range(20)), tuple(g.p[y][0] for y in range(20))
            row19, row0 = tuple(g.p[19]), tuple(g.p[0])
            if any(col9):
                right.setdefault(col9, cp)
            if any(col0):
                left.setdefault(col0, cp)
            if any(row19):
                down.setdefault(row19, cp)
            if any(row0):
                up.setdefault(row0, cp)
        for k in right:
            self.assertIn(k, left, "a right-going stroke has nothing to meet on the next cell")
        for k in down:
            self.assertIn(k, up, "a down-going stroke has nothing to meet on the next row")

    def test_rounded_corners_join_the_straight_lines(self):
        g = {cp: R.glyph(cp) for cp in (0x256D, 0x256E, 0x256F, 0x2570)}
        line = R.glyph(0x2500)
        stub = R.glyph(0x2502)
        self.assertEqual([g[0x256D].p[y][9] for y in range(20)], [line.p[y][0] for y in range(20)])        # ╭ right edge = ─ left edge
        self.assertEqual([g[0x256E].p[y][0] for y in range(20)], [line.p[y][9] for y in range(20)])
        self.assertEqual(g[0x256D].p[19], stub.p[19])                                                      # ╭ bottom = │ bottom
        self.assertEqual(g[0x2570].p[0], stub.p[0])

    def test_blocks_are_complementary(self):
        up, down = R.glyph(0x2580), R.glyph(0x2584)
        for y in range(20):
            for x in range(10):
                self.assertEqual(up.p[y][x] + down.p[y][x], 1)
        self.assertEqual(R.glyph(0x2588).count(), 200)


class Braille(unittest.TestCase):
    def test_all_256_with_uniform_gaps(self):
        for n in range(256):
            g = R.glyph(0x2800 + n)
            self.assertEqual(g.count(), 4 * bin(n).count("1"), n)
        full = R.glyph(0x28FF)
        cols = [x for x in range(10) if full.p[1][x]]
        rows = [y for y in range(20) if full.p[y][1]]
        self.assertEqual(cols, [1, 2, 6, 7])
        self.assertEqual(rows, [1, 2, 6, 7, 11, 12, 16, 17])
        # gaps: 3 px inside a cell, and 3 px across the cell edge (7 -> next 1 is 8, 9, 0)
        self.assertEqual((10 + cols[0]) - cols[-1] - 1, 3)
        self.assertEqual((20 + rows[0]) - rows[-1] - 1, 3)


class Names(unittest.TestCase):
    def test_all_glyphs_are_a_cell(self):
        for cp, g in R.g.items():
            self.assertEqual((g.w, g.h), (10, 20), hex(cp))

    def test_private_use_is_plane_16_blocks(self):
        for n, cps in R.names.items():
            for cp in cps:
                self.assertTrue(any(a <= cp <= z for a, z, _ in BLOCKS.values()) or cp < 0x10F000, f"{n} U+{cp:X}")

    def test_icons_are_two_consecutive_cells(self):
        for n, cps in R.names.items():
            if len(cps) == 2:
                self.assertEqual(cps[1], cps[0] + 1, n)
                self.assertEqual(cps[0] % 2, 0, n)

    def test_the_lock_file_pins_every_name(self):
        lock = json.loads((OUT / "glyph-names.json").read_text())
        self.assertEqual(R.new, [], "new names are not in glyph-names.json: run scripts/build_r3_font.py")
        for n, cps in R.names.items():
            self.assertEqual(lock[n], cps[0], n)
        self.assertEqual(len(set(lock.values())), len(lock), "two names share a code point")

    def test_the_names_the_apps_rely_on_exist(self):
        for n in ("pad.btn.A.on", "pad.btn.A.off", "pad.trig.l.4", "pad.dpad.up", "pad.stick.0.0", "key.u0041.up", "ui.switch.on",
                  "staff.note.q.4", "mus.wave.sine", "icon.app.hydrolik", "icon.app.klamansi", "sys.bat.4", "code.bp.on"):
            self.assertIn(n, R.names)


class Coverage(unittest.TestCase):
    def test_latin_one_and_the_typographic_marks(self):
        for cp in list(range(0xA0, 0x100)) + [0x2018, 0x2019, 0x201C, 0x201D, 0x2013, 0x2014, 0x2026, 0x20AC, 0x2122]:
            if cp in (0xDE, 0xF0, 0xFE):         # Þ ð þ: still to be drawn (docs/R3I6NOS-MONO-FONT.md)
                continue
            self.assertIn(cp, R.all_cps(), f"U+{cp:04X}")

    def test_the_perplexity_guide_symbols(self):
        text = "☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■░▒▓█▄▀╭╮╯╰▢□■▪▫◻◼○●◦◉◯◌◍◆◇◈△▽▷◁✓✔✗✘☐☑☒⚠⚡★☆✦✧"
        text += "▏▎▍▌▋▊▉█▁▂▃▄▅▆▇⇐⇑⇒⇓⇔⇕"
        for ch in text:
            self.assertIn(ord(ch), R.all_cps(), ch)

    def test_music_and_coding_symbols(self):
        for ch in "♩♪♫♬♭♮♯\U0001D11E\U0001D122\U0001D121\U0001D12A\U0001D12B\U0001D10B\U0001D10C\U0001D110\U0001D15D\U0001D15E⏩⏪⏭⏮⏸⏹⏺⏯":
            self.assertIn(ord(ch), R.all_cps(), ch)
        for cp in range(0xE0A0, 0xE0A3 + 1):
            self.assertIn(cp, R.all_cps())
        for cp in range(0xE0B0, 0xE0BF + 1):
            self.assertIn(cp, R.all_cps())

    def test_coding_ligature_free_ambiguities_are_distinct(self):
        # 0 O 1 l I | i j and rn m are all different pictures
        chars = "0O1lI|ij"
        seen = {R.glyph(ord(c)).hex() for c in chars}
        self.assertEqual(len(seen), len(chars))


class Outputs(unittest.TestCase):
    def test_committed_files_are_up_to_date(self):
        self.assertEqual(build_r3_font.main(["--check"]), 0)

    def test_c_header_finds_every_glyph(self):
        cc = shutil.which("cc") or shutil.which("gcc")
        if not cc:
            self.skipTest("no C compiler")
        d = json.loads((OUT / "r3i6nos-mono.json").read_text())
        probe = [0x41, 0x2588, 0x10F001, 0xE0B0, 0x28FF, 0x2502, 0x1FB00]
        with tempfile.TemporaryDirectory() as t:
            src = Path(t) / "p.c"
            body = "".join(f'  {{const unsigned short *g = r3mono_find(0x{cp:x}); printf("%x", g ? 1 : 0); if (g) {{ printf(":"); for (int i = 0; i < 20; i++) printf("%03x", g[i]); }} printf("\\n");}}\n'
                           for cp in probe)
            src.write_text('#include <stdio.h>\n#include "r3i6nos_mono10x20.h"\nint main(void) {\n' + body + '  printf("%x\\n", r3mono_find(0x10FFFF) ? 1 : 0);\n  return 0; }\n')
            exe = Path(t) / "p"
            subprocess.run([cc, "-Wall", "-Werror", "-I", str(OUT), str(src), "-o", str(exe)], check=True)
            lines = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.splitlines()
        for cp, line in zip(probe, lines):
            want = "1:" + d["glyphs"][d["cmap"][str(cp)]]
            self.assertEqual(line, want, hex(cp))
        self.assertEqual(lines[-1], "0")

    def test_json_names_point_at_real_code_points(self):
        d = json.loads((OUT / "r3i6nos-mono.json").read_text())
        for n, cps in d["names"].items():
            for cp in cps:
                self.assertIn(str(cp), d["cmap"], n)

    def test_bdf_has_every_code_point(self):
        text = (OUT / "R3i6nosMono-Regular.bdf").read_text()
        self.assertIn(f"CHARS {len(R.all_cps())}", text)
        self.assertEqual(text.count("STARTCHAR"), len(R.all_cps()))

    def test_name_tables_use_safe_escapes(self):
        h = (OUT / "r3i6nos_mono_names.h").read_text()
        self.assertIn('#define R3I_PAD_BTN_A_ON "\\364', h)         # octal, so a digit after it cannot join the escape
        lua = (OUT / "r3i6nos_mono_names.lua").read_text()
        self.assertRegex(lua, r'\["pad\.btn\.A\.on"\] = "\\244\\\d{3}\\\d{3}\\\d{3}",')   # 4 bytes, each \ddd, three digits so a digit after cannot join

    def test_ttf_if_built(self):
        ttf = OUT / "R3i6nosMono-Regular.ttf"
        try:
            from fontTools.ttLib import TTFont
        except ImportError:
            self.skipTest("fontTools is not installed here (scripts/build-r3-font.sh checks the TTF in Debian)")
        if not ttf.exists():
            self.skipTest("no TTF")
        f = TTFont(ttf)
        self.assertEqual(f["head"].unitsPerEm, 2000)
        self.assertEqual(f["hhea"].ascent, 1500)
        self.assertEqual(set(f.getBestCmap()), set(R.all_cps()))


if __name__ == "__main__":
    unittest.main()

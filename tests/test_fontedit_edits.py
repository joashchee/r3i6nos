"""The Font Editor's edits file and how the font builder applies it (scripts/r3font/edits.py, scripts/merge_font_edits.py; docs/FONT-EDITOR.md).
No hardware. The committed theme/font/r3i6nos-mono/edits.txt is never touched: every test writes its own file."""
import re
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

import build_r3_font  # noqa: E402
from r3font import edits  # noqa: E402

BASE, _ = build_r3_font.build(with_edits=False)
ICON = BASE.names["icon.app.hydrolik"]       # a two-cell icon: the editor's main case


def ink(cp, *pixels):
    rows = [list(r) for r in edits.rows_of(BASE.glyph(cp))]
    for x, y in pixels:
        rows[y][x] = "#" if rows[y][x] == "." else "."
    return ["".join(r) for r in rows]


class Edits(unittest.TestCase):
    def apply_text(self, text):
        R, _ = build_r3_font.build(with_edits=False)
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "edits.txt"
            p.write_text(text)
            return R, edits.apply(R, p)

    def test_a_missing_file_changes_nothing(self):
        R, _ = build_r3_font.build(with_edits=False)
        self.assertEqual(edits.apply(R, "/nonexistent/edits.txt"), [])

    def test_the_committed_font_is_built_with_the_committed_edits(self):
        a, _ = build_r3_font.build()
        b, _ = build_r3_font.build(with_edits=True)
        self.assertEqual(a.all_cps(), b.all_cps())
        if edits.EDITS.exists():
            for cp in edits.parse(edits.EDITS.read_text()):
                self.assertNotEqual(a.glyph(cp), BASE.glyph(cp), f"U+{cp:04X}: committed edit equals the generated cell (drop it)")

    def test_round_trip_and_a_row_that_starts_with_a_hash(self):
        rows = ink(ICON[0], (0, 0), (9, 19))
        self.assertTrue(rows[0].startswith("#"))
        text = edits.render({ICON[0]: rows, ICON[1]: ink(ICON[1], (3, 3))}, BASE)
        self.assertIn(f"cell {ICON[0]:X} icon.app.hydrolik 1/2", text)
        self.assertIn(f"cell {ICON[1]:X} icon.app.hydrolik 2/2", text)
        back = edits.parse(text)
        self.assertEqual(back[ICON[0]], ("icon.app.hydrolik", rows))
        self.assertEqual(edits.render({cp: v[1] for cp, v in back.items()}, BASE), text)

    def test_apply_changes_the_cell_and_only_the_cell(self):
        rows = ink(ICON[0], (1, 1))
        R, done = self.apply_text(edits.render({ICON[0]: rows}, BASE))
        self.assertEqual(done, [ICON[0]])
        self.assertEqual(edits.rows_of(R.glyph(ICON[0])), rows)
        self.assertEqual(R.glyph(ICON[1]), BASE.glyph(ICON[1]))
        self.assertEqual(sum(1 for cp in R.all_cps() if R.glyph(cp) != BASE.glyph(cp)), 1)

    def test_an_alias_follows_its_target(self):
        a = ord("A")
        self.assertEqual(BASE.alias.get(0xF000 + a), a)
        R, _ = self.apply_text(edits.render({a: ink(a, (0, 0))}, BASE))
        self.assertEqual(R.glyph(0xF000 + a), R.glyph(a))
        self.assertNotEqual(R.glyph(a), BASE.glyph(a))

    def test_editing_an_alias_gives_it_its_own_picture(self):
        raw = 0xF000 + ord("B")
        R, _ = self.apply_text(edits.render({raw: ink(raw, (2, 2))}, BASE))
        self.assertNotEqual(R.glyph(raw), R.glyph(ord("B")))
        self.assertEqual(R.glyph(ord("B")), BASE.glyph(ord("B")))

    def test_errors_say_what_is_wrong(self):
        good = edits.render({ICON[0]: ink(ICON[0], (1, 1))}, BASE)
        for text, word in ((good.replace("icon.app.hydrolik", "icon.app.klamansi"), "stale"),
                           (good.replace(f"cell {ICON[0]:X}", "cell 10FFFF"), "not in the font"),
                           (good.replace("10x20", "8x16"), "not"),
                           (good.replace("\n..........\n", "\n.........x\n", 1), "characters"),
                           (good + good.split("\n", 2)[2], "twice")):
            with self.assertRaises(edits.EditError, msg=word) as e:
                self.apply_text(text)
            self.assertIn(word, str(e.exception))
        with self.assertRaises(edits.EditError):
            edits.parse("# r3i6nos font edits 1\nfont R3i6nos Mono 10x20\ncell 41 - 1/1\n" + "..........\n" * 19)

    def test_merge_wins_over_the_old_and_drops_cells_equal_to_the_generated_ones(self):
        old = edits.render({ICON[0]: ink(ICON[0], (1, 1)), ICON[1]: ink(ICON[1], (2, 2))}, BASE)
        new = edits.render({ICON[0]: ink(ICON[0], (5, 5)), ICON[1]: edits.rows_of(BASE.glyph(ICON[1]))}, BASE)
        out = edits.parse(edits.merge(BASE, new, old))
        self.assertEqual(sorted(out), [ICON[0]])
        self.assertEqual(out[ICON[0]][1], ink(ICON[0], (5, 5)))
        self.assertEqual(edits.merge(BASE, new, ""), edits.merge(BASE, new, edits.merge(BASE, new, "")))


class WithTheApp(unittest.TestCase):
    """The C editor (apps/fontedit-native/fontedit.c) carries one edits file as a string and its self-test reads and writes it byte for byte; here the
    Python side reads the same text and writes the same bytes, so the two implementations of the format cannot drift apart."""

    def fixture(self):
        src = (ROOT / "apps" / "fontedit-native" / "fontedit.c").read_text()
        body = src.split("/* FIXTURE BEGIN */")[1].split("/* FIXTURE END */")[0]
        return "".join(bytes(m, "utf-8").decode("unicode_escape") for m in re.findall(r'"((?:[^"\\]|\\.)*)"', body))

    def test_python_reads_and_writes_the_c_fixture_byte_for_byte(self):
        text = self.fixture()
        cells = edits.parse(text)
        self.assertEqual(sorted(cells), [0x10FD14])
        name, rows = cells[0x10FD14]
        self.assertEqual(name, "icon.app.hydrolik")
        self.assertEqual(rows[0], "#" * 10)
        self.assertEqual(rows[19], ".........#")
        self.assertTrue(all(r == "." * 10 for r in rows[1:19]))
        self.assertEqual(edits.render({cp: v[1] for cp, v in cells.items()}, BASE), text)


if __name__ == "__main__":
    unittest.main()

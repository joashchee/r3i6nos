import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("agp", ROOT / "scripts/add_games_partition.py")
agp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(agp)

GB = 1_000_000_000


def mbr(sig=0x52333669, parts=((32768, 524288), (557056, 4194304)), extra=None):
    b = bytearray(512)
    b[440:444] = sig.to_bytes(4, "little")
    for i, (start, size) in enumerate(parts):
        o = 446 + 16 * i
        b[o + 4] = 0x0C if i == 0 else 0x83
        b[o + 8:o + 12] = start.to_bytes(4, "little")
        b[o + 12:o + 16] = size.to_bytes(4, "little")
    for n, raw in (extra or {}).items():
        b[446 + 16 * n:446 + 16 * n + 16] = raw
    b[510:512] = b"\x55\xaa"
    return bytes(b)


class Games(unittest.TestCase):
    def test_adds_exfat_partition_from_after_rootfs_to_the_end(self):
        new, start, size = agp.add_games_partition(mbr(), 128_177_930_240)
        self.assertEqual(start % 8192, 0)
        self.assertGreaterEqual(start, 557056 + 4194304)
        self.assertEqual(start + size, 128_177_930_240 // 512)       # to the last sector
        e = new[446 + 32:446 + 48]
        self.assertEqual(e[4], 0x07)
        self.assertEqual(int.from_bytes(e[8:12], "little"), start)
        self.assertEqual(int.from_bytes(e[12:16], "little"), size)

    def test_everything_else_is_untouched(self):
        old = mbr()
        new, _, _ = agp.add_games_partition(old, 64 * GB)
        self.assertEqual(new[:446 + 32], old[:446 + 32])
        self.assertEqual(new[446 + 48:], old[446 + 48:])             # partition 4, signature

    def test_refuses_foreign_disks_and_used_slots(self):
        with self.assertRaisesRegex(agp.GamesError, "not an r3i6nos"):
            agp.add_games_partition(mbr(sig=0x1234), 64 * GB)
        with self.assertRaisesRegex(agp.GamesError, "partition 3 is already"):
            agp.add_games_partition(mbr(extra={2: b"\x00" * 4 + b"\x07" + b"\x00" * 11}), 64 * GB)
        with self.assertRaisesRegex(agp.GamesError, "partition 2 is missing"):
            agp.add_games_partition(mbr(parts=((32768, 524288),)), 64 * GB)
        with self.assertRaisesRegex(agp.GamesError, "no 0x55AA"):
            agp.add_games_partition(b"\x00" * 512, 64 * GB)

    def test_refuses_when_the_card_is_nearly_full(self):
        with self.assertRaisesRegex(agp.GamesError, "not enough"):
            agp.add_games_partition(mbr(), 2_400_000_000)

    def test_cards_over_2tib_are_capped_not_wrapped(self):
        _, start, size = agp.add_games_partition(mbr(), 4 * 1024 ** 4)
        self.assertEqual(start + size, 0xFFFFFFFF)

    def test_command_line_edits_the_file(self):
        f = tempfile.NamedTemporaryFile(delete=False)
        f.write(mbr()); f.close()
        self.assertEqual(agp.main(["x", f.name, str(64 * GB)]), 0)
        self.assertEqual(Path(f.name).read_bytes()[446 + 36], 0x07)
        self.assertEqual(agp.main(["x", f.name, str(64 * GB)]), 1)      # a second run is refused


if __name__ == "__main__":
    unittest.main()

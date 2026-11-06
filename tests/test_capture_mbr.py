import importlib.util
import unittest
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "scripts/capture_mbr.py"
spec = importlib.util.spec_from_file_location("capture_mbr", SRC)
mbr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mbr)

S = 512


def card(parts=((0x0C, 32768, 100000), (0x83, 200000, 100000)), loader_at=(64, 16384), signature=True):
    raw = bytearray(32768 * S)
    for i, (ptype, start, size) in enumerate(parts):
        e = 446 + 16 * i
        raw[e + 4] = ptype
        raw[e + 8:e + 12] = start.to_bytes(4, "little")
        raw[e + 12:e + 16] = size.to_bytes(4, "little")
    if signature:
        raw[510:512] = b"\x55\xaa"
    for sector in loader_at:
        raw[sector * S: sector * S + 16] = b"LOADER-" + bytes([sector % 251]) * 9
    return bytes(raw)


class CaptureMbr(unittest.TestCase):
    def test_reads_partitions_and_loader(self):
        info = mbr.parse(card())
        self.assertEqual([p["type"] for p in info["partitions"]], [0x0C, 0x83])
        self.assertEqual(info["first_partition"], 32768)
        # Loader runs from sector 64 to the last non-zero sector (16384), whole sectors.
        self.assertEqual(len(info["loader"]), (16384 - 64 + 1) * S)
        self.assertTrue(info["loader"].startswith(b"LOADER-"))
        self.assertFalse(info["allwinner"])

    def test_loader_stops_at_the_first_partition_when_it_is_earlier(self):
        info = mbr.parse(card(parts=((0x0C, 20000, 5000),), loader_at=(64, 19999)))
        self.assertEqual(len(info["loader"]), (19999 - 64 + 1) * S)

    def test_rejects_missing_signature(self):
        with self.assertRaisesRegex(mbr.CaptureError, "signature"):
            mbr.parse(card(signature=False))

    def test_rejects_gpt(self):
        with self.assertRaisesRegex(mbr.CaptureError, "GPT"):
            mbr.parse(card(parts=((0xEE, 1, 1000),)))

    def test_rejects_partition_inside_bootloader_area(self):
        with self.assertRaisesRegex(mbr.CaptureError, "inside the bootloader"):
            mbr.parse(card(parts=((0x0C, 32, 1000),)))

    def test_rejects_empty_loader_area(self):
        with self.assertRaisesRegex(mbr.CaptureError, "all zeros"):
            mbr.parse(card(loader_at=()))

    def test_rejects_short_read(self):
        with self.assertRaisesRegex(mbr.CaptureError, "bytes read"):
            mbr.parse(card()[:1000])

    def test_flags_allwinner(self):
        raw = bytearray(card())
        raw[8192 + 4: 8192 + 12] = b"eGON.BT0"
        self.assertTrue(mbr.parse(bytes(raw))["allwinner"])


if __name__ == "__main__":
    unittest.main()

"""The boot logo: docs/brand/boot-logo.png -> the BMP the bootloader shows (scripts/make_boot_logo.py), and inject-boot.sh putting it onto BOOT.

No hardware: the converter is checked against the stock card's header layout and (when Pillow is there) against Pillow's pixels; inject-boot.sh
runs on a small fake image made here with mtools, and the logo is read back out of its BOOT partition."""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "scripts"))
import make_boot_logo as mbl  # noqa: E402

STOCK_SIZE = 921656          # ArkOS's boot/logo.bmp, measured on the stock card (docs/hardware-findings.md)
STOCK_HEAD = bytes.fromhex("424d3810" "0e000000" "00003600" "00002800" "00008002" "0000e001" "00000100" "18000000" "00000210" "0e00c30e" "0000c30e" "00000000" "00000000")


class Converter(unittest.TestCase):
    def test_the_committed_bmp_is_what_the_png_gives(self):
        self.assertEqual(mbl.BMP.read_bytes(), mbl.to_bmp(mbl.PNG.read_bytes()), "run scripts/make_boot_logo.py")

    def test_header_is_the_stock_files(self):
        d = mbl.BMP.read_bytes()
        self.assertEqual(len(d), STOCK_SIZE)
        self.assertEqual(d[:len(STOCK_HEAD)], STOCK_HEAD)
        self.assertEqual(d[-2:], b"\0\0")

    def test_pixels_match_pillow(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Pillow is not installed (the converter's pixels were not compared with an independent decoder)")
        im = Image.open(mbl.PNG).convert("RGB")
        d = mbl.BMP.read_bytes()
        for x, y in [(0, 0), (639, 479), (320, 100), (200, 140), (80, 240), (500, 240), (300, 370), (639, 0), (0, 479)]:
            at = 54 + ((479 - y) * 640 + x) * 3
            self.assertEqual((d[at + 2], d[at + 1], d[at]), im.getpixel((x, y)), (x, y))

    def test_every_png_filter_decodes(self):
        import zlib

        def png(rows, ctype=2):
            bpp = 3 if ctype == 2 else 4
            raw = b"".join(bytes([f]) + r for f, r in rows)
            chunk = lambda k, b: struct.pack(">I", len(b)) + k + b + struct.pack(">I", zlib.crc32(k + b))
            return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, len(rows), 8, ctype, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
        first = bytes([10, 20, 30, 40, 50, 60])
        # row 0 is stored plain; each next row is stored with filter f against the row above and must decode to `want`
        want = [first, bytes([11, 22, 33, 44, 55, 66]), bytes([12, 24, 36, 48, 60, 72]), bytes([13, 26, 39, 52, 65, 78]), bytes([14, 28, 42, 56, 70, 84])]

        def filtered(f, cur, prev):
            out = bytearray()
            for i in range(6):
                a = cur[i - 3] if i >= 3 else 0
                b = prev[i]
                c = prev[i - 3] if i >= 3 else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = [0, a, b, (a + b) // 2, a if pa <= pb and pa <= pc else b if pb <= pc else c][f]
                out.append((cur[i] - pred) & 255)
            return bytes(out)
        rows = [(0, first)] + [(f, filtered(f, want[f], want[f - 1])) for f in (1, 2, 3, 4)]
        w, h, got = mbl.read_png(png(rows))
        self.assertEqual((w, h), (2, 5))
        self.assertEqual(got, want)
        _, _, rgba = mbl.read_png(png([(0, bytes([200, 100, 50, 255, 200, 100, 50, 0]))], ctype=6))
        self.assertEqual(rgba[0], bytes([200, 100, 50, 0, 0, 0]), "alpha is laid over black")

    def test_wrong_files_are_refused(self):
        with self.assertRaises(ValueError):
            mbl.to_bmp(b"not a png")
        with self.assertRaises(ValueError):
            mbl.to_bmp(self.small_png())

    @staticmethod
    def small_png():
        import zlib
        chunk = lambda k, b: struct.pack(">I", len(b)) + k + b + struct.pack(">I", zlib.crc32(k + b))
        return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(b"\0\1\2\3")) + chunk(b"IEND", b"")


@unittest.skipUnless(shutil.which("mformat") and shutil.which("mcopy"), "mtools is not installed (inject-boot.sh was not run against a fake image)")
class Inject(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="r3i6nos-logotest.")
        self.addCleanup(self.tmp.cleanup)
        t = Path(self.tmp.name)
        layout = subprocess.run(["bash", "-c", f". {REPO}/scripts/image-layout.sh; echo $BOOT_START $DISK_SIGNATURE"], capture_output=True, text=True, check=True).stdout.split()
        boot_start, sig = int(layout[0]), int(layout[1], 16)
        self.img = t / "fake.img"
        size = (boot_start + 40 * 2048) * 512
        with open(self.img, "wb") as f:
            f.truncate(size)
            mbr = bytearray(512)
            mbr[440:444] = struct.pack("<I", sig)
            mbr[446:462] = bytes([0x80, 0, 0, 0, 0x0C, 0, 0, 0]) + struct.pack("<II", boot_start, 40 * 2048)
            mbr[510:512] = b"\x55\xaa"
            f.seek(0)
            f.write(mbr)
        self.fat = f"{self.img}@@{boot_start * 512}"
        subprocess.run(["mformat", "-i", self.fat, "-F", "-T", str(40 * 2048), "::"], check=True, capture_output=True)
        self.assets = t / "assets"
        (self.assets / "boot").mkdir(parents=True)
        (self.assets / "bootloader.bin").write_bytes(b"\x01" * 4096)
        (self.assets / "boot" / "boot.ini").write_text("setenv bootargs \"root=UUID='e139ce78-9841-40fe-8823-96a304a09859' rootwait rw quiet\"\n")
        self.stock = b"BM" + b"stock logo, not r3i6nos's" * 10
        (self.assets / "boot" / "logo.bmp").write_bytes(self.stock)

    def run_inject(self, **env):
        e = dict(os.environ, **env)
        return subprocess.run([str(REPO / "scripts" / "inject-boot.sh"), str(self.img), str(self.assets)], capture_output=True, text=True, env=e)

    def read_logo(self):
        return subprocess.run(["mcopy", "-n", "-i", self.fat, "::/logo.bmp", "-"], capture_output=True, check=True).stdout

    def test_the_logo_replaces_the_stock_one_on_boot_and_the_assets_are_left_alone(self):
        r = self.run_inject()
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
        self.assertEqual(self.read_logo(), mbl.BMP.read_bytes())
        self.assertEqual((self.assets / "boot" / "logo.bmp").read_bytes(), self.stock, "the file in assets-dir is never touched")

    def test_stock_logo_is_kept_on_request(self):
        r = self.run_inject(R3I6NOS_STOCK_LOGO="1")
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
        self.assertEqual(self.read_logo(), self.stock)

    def test_a_logo_that_is_not_640x480_is_refused(self):
        bad = Path(self.tmp.name) / "bad.bmp"
        bad.write_bytes(mbl.BMP.read_bytes()[:54] + b"\0" * 100)
        r = self.run_inject(R3I6NOS_BOOT_LOGO=str(bad))
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("640x480", r.stderr)
        r = self.run_inject(R3I6NOS_BOOT_LOGO=str(Path(self.tmp.name) / "missing.bmp"))
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("missing the boot logo", r.stderr)


if __name__ == "__main__":
    unittest.main()

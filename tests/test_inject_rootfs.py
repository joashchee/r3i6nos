import importlib.util
import os
import tempfile
import unittest
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "scripts/inject_rootfs.py"
spec = importlib.util.spec_from_file_location("inject_rootfs", SRC)
inj = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inj)

LISTING = """\
file libmali-bifrost-g31-rxp0-gbm.so
file libdrm.so.2.4.0
link libMali.so -> libmali-bifrost-g31-rxp0-gbm.so
link libEGL.so -> libMali.so
link libEGL.so.1 -> libEGL.so
link libGLESv2.so.2 -> /usr/lib/aarch64-linux-gnu/libMali.so
link libgbm.so.1 -> libMali.so
link libdrm.so.2 -> libdrm.so.2.4.0
link libOpenCL.so -> libnothing.so
"""


def make_assets(listing=LISTING, gpu=True):
    root = Path(tempfile.mkdtemp())
    mods = root / "rootfs/lib/modules/4.4.189/kernel"
    mods.mkdir(parents=True)
    (mods / "a.ko").write_bytes(b"A")
    (root / "rootfs/lib/modules/4.4.189/build").symlink_to("/usr/src/missing")
    fw = root / "rootfs/lib/firmware"
    fw.mkdir(parents=True)
    (fw / "x.bin").write_bytes(b"X")
    if gpu:
        g = root / "rootfs/gpu"
        g.mkdir(parents=True)
        (g / "libmali-bifrost-g31-rxp0-gbm.so").write_bytes(b"BLOB")
        (g / "libdrm.so.2.4.0").write_bytes(b"DRM")
        (root / "info").mkdir()
        (root / "info/stock-gpu-libs.txt").write_text(listing)
    return str(root)


class Plan(unittest.TestCase):
    def test_modules_and_firmware_go_to_usr_lib(self):
        ops = inj.build_plan(make_assets())
        writes = {o[2] for o in ops if o[0] == "write"}
        self.assertIn("/usr/lib/modules/4.4.189/kernel/a.ko", writes)
        self.assertIn("/usr/lib/firmware/x.bin", writes)
        self.assertIn(("symlink", "/usr/lib/modules/4.4.189/build", "/usr/src/missing"), ops)

    def test_parent_directories_come_first(self):
        ops = inj.build_plan(make_assets())
        order = [o[1] for o in ops if o[0] == "mkdir"]
        self.assertLess(order.index("/usr"), order.index("/usr/lib"))
        self.assertLess(order.index("/usr/lib"), order.index("/usr/lib/modules"))
        self.assertLess(order.index("/opt"), order.index("/opt/libmali"))

    def test_gpu_keeps_only_libmali_and_links_that_resolve_to_it(self):
        ops = inj.build_plan(make_assets())
        writes = {o[2] for o in ops if o[0] == "write"}
        links = {o[1]: o[2] for o in ops if o[0] == "symlink" and o[1].startswith("/opt/libmali")}
        self.assertEqual(writes & {"/opt/libmali/libdrm.so.2.4.0"}, set())
        self.assertIn("/opt/libmali/libmali-bifrost-g31-rxp0-gbm.so", writes)
        self.assertEqual(links["/opt/libmali/libEGL.so.1"], "libEGL.so")
        self.assertEqual(links["/opt/libmali/libGLESv2.so.2"], "libMali.so")   # absolute target rebased
        self.assertNotIn("/opt/libmali/libdrm.so.2", links)
        self.assertNotIn("/opt/libmali/libOpenCL.so", links)

    def test_missing_required_gpu_names_is_an_error(self):
        listing = "link libMali.so -> libmali-bifrost-g31-rxp0-gbm.so\n"
        with self.assertRaisesRegex(inj.PlanError, "libEGL.so.1"):
            inj.build_plan(make_assets(listing))
        # ...unless explicitly allowed
        self.assertTrue(inj.build_plan(make_assets(listing), allow_partial=True))

    def test_no_libmali_is_an_error(self):
        assets = make_assets()
        os.remove(os.path.join(assets, "rootfs/gpu/libmali-bifrost-g31-rxp0-gbm.so"))
        with self.assertRaisesRegex(inj.PlanError, "no libmali"):
            inj.build_plan(assets)

    def test_empty_assets_is_an_error(self):
        with self.assertRaisesRegex(inj.PlanError, "nothing to inject"):
            inj.build_plan(tempfile.mkdtemp())

    def test_link_loops_do_not_hang(self):
        listing = LISTING + "link loopa -> loopb\nlink loopb -> loopa\n"
        self.assertTrue(inj.build_plan(make_assets(listing)))

    def test_commands_are_quoted_and_odd_names_refused(self):
        text = inj.to_commands([("write", "/h/a b.ko", "/usr/lib/modules/a b.ko")])
        self.assertEqual(text, 'write "/h/a b.ko" "/usr/lib/modules/a b.ko"\n')
        with self.assertRaises(inj.PlanError):
            inj.to_commands([("mkdir", '/x"y')])


class RealListing(unittest.TestCase):
    """The listing captured from a real stock card (2026-11-06)."""

    FIXTURE = Path(__file__).resolve().parent / "fixtures/stock-gpu-libs.txt"

    def assets(self):
        a = make_assets(self.FIXTURE.read_text())
        gpu = Path(a) / "rootfs/gpu"
        (gpu / "libmali-bifrost-g31-rxp0-gbm.so").rename(gpu / "libmali-bifrost-g31-rxp0-wayland-gbm.so")
        return a

    def test_required_libraries_are_provided_by_the_blob(self):
        ops = inj.build_plan(self.assets())
        links = {o[1]: o[2] for o in ops if o[0] == "symlink" and o[1].startswith("/opt/libmali")}
        for name in inj.REQUIRED_GPU_NAMES:
            self.assertEqual(links["/opt/libmali/" + name], "libMali.so")
        self.assertEqual(links["/opt/libmali/libMali.so"], "libmali-bifrost-g31-rxp0-wayland-gbm.so")
        self.assertEqual(links["/opt/libmali/libmali.so.1"], "libmali.so")

    def test_mesa_and_libdrm_leftovers_are_not_injected(self):
        ops = inj.build_plan(self.assets())
        dests = {o[2] if o[0] == "write" else o[1] for o in ops if o[-1].startswith("/opt/libmali") or o[1].startswith("/opt/libmali")}
        self.assertFalse([d for d in dests if "drm" in d or "mesa" in d.lower()])


class UsrLocalListing(unittest.TestCase):
    """The blob that matches the card's own kernel (/usr/local/lib on the stock card, 2026-11-07).

    The older wayland-gbm blob in /usr/lib speaks Mali interface 10.6; the 4.4.189 kernel wants 11.x,
    and LÖVE failed with "file /dev/mali0 is not of a compatible version"."""

    FIXTURE = Path(__file__).resolve().parent / "fixtures/stock-gpu-libs-usr-local.txt"

    def test_every_required_library_links_straight_to_the_blob(self):
        a = make_assets(self.FIXTURE.read_text())
        gpu = Path(a) / "rootfs/gpu"
        (gpu / "libmali-bifrost-g31-rxp0-gbm.so").write_bytes(b"BLOB")
        (gpu / "libdrm.so.2.4.0").unlink()
        ops = inj.build_plan(a)
        links = {o[1]: o[2] for o in ops if o[0] == "symlink" and o[1].startswith("/opt/libmali")}
        for name in inj.REQUIRED_GPU_NAMES:
            self.assertEqual(links["/opt/libmali/" + name], "libmali-bifrost-g31-rxp0-gbm.so")
        self.assertEqual(links["/opt/libmali/libmali.so"], "libmali-bifrost-g31-rxp0-gbm.so")

    def test_comment_line_in_the_listing_is_ignored(self):
        self.assertTrue(self.FIXTURE.read_text().startswith("# /usr/local/lib"))
        files, links = inj._read_listing(str(self.FIXTURE))
        self.assertIn("libEGL.so.1", links)
        self.assertIn("libmali-bifrost-g31-rxp0-gbm.so", files)


class Elf(unittest.TestCase):
    @staticmethod
    def elf(names):
        strtab = b"\0" + b"\0".join(n.encode() for n in names) + b"\0"
        offsets, pos = [], 1
        for n in names:
            offsets.append(pos)
            pos += len(n) + 1
        base = 64 + 56 * 2
        dyn_off = base
        dyn = b"".join((1).to_bytes(8, "little") + o.to_bytes(8, "little") for o in offsets)
        str_off = dyn_off + len(dyn) + 16 * 2
        dyn += (5).to_bytes(8, "little") + (0x1000 + str_off).to_bytes(8, "little")   # DT_STRTAB (vaddr)
        dyn += b"\0" * 16                                                           # DT_NULL
        total = str_off + len(strtab)
        ehdr = bytearray(64)
        ehdr[:6] = b"\x7fELF\x02\x01"
        ehdr[32:40] = (64).to_bytes(8, "little")                                     # e_phoff
        ehdr[54:56] = (56).to_bytes(2, "little"); ehdr[56:58] = (2).to_bytes(2, "little")

        def ph(ptype, off, vaddr, size):
            return (ptype.to_bytes(4, "little") + bytes(4) + off.to_bytes(8, "little") + vaddr.to_bytes(8, "little")
                    + bytes(8) + size.to_bytes(8, "little") + size.to_bytes(8, "little") + bytes(8))

        body = ph(1, 0, 0x1000, total) + ph(2, dyn_off, 0x1000 + dyn_off, len(dyn))
        blob = bytearray(bytes(ehdr) + body + dyn)
        blob += b"\0" * (str_off - len(blob)) + strtab
        return bytes(blob)

    def test_reads_needed_libraries(self):
        names = ["libwayland-client.so.0", "libdrm.so.2", "libc.so.6"]
        self.assertEqual(inj.needed(self.elf(names)), names)

    def test_rejects_non_elf(self):
        with self.assertRaises(inj.PlanError):
            inj.needed(b"MZ not an elf")


class Verify(unittest.TestCase):
    def dump_of(self, assets):
        """What debugfs rdump would give back: the plan applied to a directory."""
        dump = tempfile.mkdtemp()
        for op in inj.build_plan(assets):
            if op[0] == "mkdir":
                os.makedirs(dump + op[1], exist_ok=True)
            elif op[0] == "write":
                os.makedirs(os.path.dirname(dump + op[2]), exist_ok=True)
                Path(dump + op[2]).write_bytes(Path(op[1]).read_bytes())
            else:
                os.makedirs(os.path.dirname(dump + op[1]), exist_ok=True)
                if os.path.lexists(dump + op[1]):      # a case-insensitive host: last writer wins
                    os.remove(dump + op[1])
                os.symlink(op[2], dump + op[1])
        return dump

    def test_matching_dump_verifies(self):
        assets = make_assets()
        self.assertEqual(inj.verify(inj.build_plan(assets), self.dump_of(assets)), [])

    def test_differences_are_reported(self):
        assets = make_assets()
        dump = self.dump_of(assets)
        Path(dump + "/usr/lib/firmware/x.bin").write_bytes(b"changed")
        os.remove(dump + "/opt/libmali/libMali.so")
        problems = inj.verify(inj.build_plan(assets), dump)
        self.assertTrue(any("content differs" in p for p in problems))
        self.assertTrue(any("missing symlink /opt/libmali/libMali.so" in p for p in problems))

    def test_symlink_targets_come_from_the_stat_log_not_the_dump(self):
        # A case-insensitive dump folder: libmali.so overwrote libMali.so's link.
        assets = make_assets(RealListing.FIXTURE.read_text())
        gpu = Path(assets) / "rootfs/gpu"
        (gpu / "libmali-bifrost-g31-rxp0-gbm.so").rename(gpu / "libmali-bifrost-g31-rxp0-wayland-gbm.so")
        ops = inj.build_plan(assets)
        dump = self.dump_of(assets)
        os.remove(dump + "/opt/libmali/libmali.so")
        os.symlink("libmali-bifrost-g31-rxp0-wayland-gbm.so", dump + "/opt/libmali/libmali.so")
        self.assertTrue(inj.verify(ops, dump))                       # the dump alone is fooled
        log = "".join(f'debugfs: stat "{o[1]}"\nInode: 1\nFast link dest: "{o[2]}"\n'
                      for o in ops if o[0] == "symlink")
        self.assertEqual(inj.verify(ops, dump, inj.parse_stat_log(log)), [])
        wrong = log.replace('stat "/opt/libmali/libEGL.so"\nInode: 1\nFast link dest: "libMali.so"',
                            'stat "/opt/libmali/libEGL.so"\nInode: 1\nFast link dest: "nope"')
        self.assertTrue(any("libEGL.so points to nope" in p
                            for p in inj.verify(ops, dump, inj.parse_stat_log(wrong))))


if __name__ == "__main__":
    unittest.main()

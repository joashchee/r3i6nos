"""r3i6nos-setup (docs/TWO-SLOT-SETUP.md): the console prepares its own card from the original, against fake cards.

The "disks" are sparse files, the "partitions" of the original are folders, and mount, umount, partx, mkfs.exfat, blkid, systemctl
and poweroff are fake scripts that record their calls. Nothing here touches a real card, and nothing here has run on a real R36S.
"""
import hashlib
import importlib.machinery
import importlib.util
import io
import os
import re
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "overlay/usr/local/bin/r3i6nos-setup"
sys.path.insert(0, str(REPO / "scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import add_games_partition as agp  # noqa: E402
from test_inject_rootfs import Elf  # noqa: E402

ROOT_UUID = re.search(r"^ROOT_UUID=(\S+)", (REPO / "scripts/image-layout.sh").read_text(), re.M).group(1)
STOCK_UUID = "aaaaaaaa-1111-4222-8333-444444444444"
BOOT_INI = ("odroidgoa-uboot-config\n"
            f"setenv bootargs \"root=UUID='{STOCK_UUID}' rootwait rw fsck.repair=yes console=/dev/ttyFIQ0 quiet splash\"\n"
            "load mmc 1:1 ${loadaddr} Image\n")
SIG_ORIG = 0x12345678
SIG_NEW = 0x52333669


def load(env):
    with mock.patch.dict(os.environ, env):
        loader = importlib.machinery.SourceFileLoader("r3i6nos_setup", str(SRC))
        spec = importlib.util.spec_from_loader("r3i6nos_setup", loader)
        mod = importlib.util.module_from_spec(spec)
        sys.dont_write_bytecode = True
        loader.exec_module(mod)
    return mod


def mbr(sig, entries):
    b = bytearray(512)
    b[440:444] = sig.to_bytes(4, "little")
    for i, (ptype, start, size) in enumerate(entries):
        b[446 + 16 * i + 4] = ptype
        b[446 + 16 * i + 8:446 + 16 * i + 12] = start.to_bytes(4, "little")
        b[446 + 16 * i + 12:446 + 16 * i + 16] = size.to_bytes(4, "little")
    b[510:512] = b"\x55\xaa"
    return bytes(b)


def tree_hash(top):
    h = hashlib.sha256()
    for d, dirs, files in os.walk(top):
        dirs.sort()
        for n in sorted(files + [x for x in dirs if os.path.islink(os.path.join(d, x))]):
            p = os.path.join(d, n)
            h.update(os.path.relpath(p, top).encode())
            h.update(os.readlink(p).encode() if os.path.islink(p) else Path(p).read_bytes())
    return h.hexdigest()


def file_hash(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def disk_hash(p):
    """The first 40 MiB of a (sparse, 1 GiB+) fake disk: the table, the loader, BOOT and the start of the games area."""
    with open(p, "rb") as f:
        return hashlib.sha256(f.read(40 << 20)).hexdigest()


class Fixture(unittest.TestCase):
    """The original card on mmcblk0 (sector 64 holds a loader), the new card on mmcblk1 (nothing at sector 64), root on mmcblk1p2."""

    ORIG_P1, ORIG_P2, ORIG_P3 = (0x0B, 32768, 2048), (0x83, 34816, 4096), (0x07, 38912, 1000)
    NEW_TOTAL = 2_500_000          # sectors (1.2 GiB, sparse)
    NEW_PARTS = [(0x0B, 32768, 8192), (0x83, 40960, 20000)]   # ROOTFS ends at 60960: the games area would start at 65536

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.t = Path(self.tmp.name)
        for d in ("sys/block", "sys/dev/block", "sys/devices", "dev", "proc", "bin", "run", "dev/mmcblk1p1", "boot", "root/etc", "var", "input"):
            (t / d).mkdir(parents=True)
        (t / "proc/mounts").write_text("")
        (t / "types").write_text("")
        (t / "root/etc/r3i6nos-release").write_text("VERSION=0.1.0\n")
        self.fakes()
        self.original_card()
        self.new_card()
        self.env = {
            "R3I6NOS_SYS": str(t / "sys"), "R3I6NOS_DEV": str(t / "dev"), "R3I6NOS_PROC": str(t / "proc"), "R3I6NOS_ROOT_DEV": "179:34",
            "R3I6NOS_ROOT": str(t / "root"), "R3I6NOS_BOOT": str(t / "boot"), "R3I6NOS_RUN": str(t / "run"), "R3I6NOS_VAR": str(t / "var"),
            "R3I6NOS_INPUT": str(t / "input"), "R3I6NOS_SETUP_BACKUP": "0", "R3I6NOS_SETUP_COLS": "0", "R3I6NOS_SETUP_PAUSE": "0", "R3I6NOS_SETUP_NOTE": "0",
            "PATH": f"{t / 'bin'}:{os.environ.get('PATH', '')}",
        }

    def tearDown(self):
        self.tmp.cleanup()

    # --- the fake commands ---
    def fakes(self):
        t = self.t
        scripts = {
            "blkid": f"#!/bin/sh\nfor a; do d=$a; done\ngrep \"^$d \" {t}/types | cut -d' ' -f2\n",
            "mount": f"#!/bin/sh\necho \"mount $@\" >> {t}/calls\nrmdir \"$6\" 2>/dev/null; ln -s \"$5\" \"$6\"\n",
            "umount": f"#!/bin/sh\necho \"umount $@\" >> {t}/calls\nif [ -L \"$1\" ]; then rm \"$1\"; mkdir \"$1\"; fi\n",
            "partx": f"#!/bin/sh\necho \"partx $@\" >> {t}/calls\nmkdir -p \"${{4}}p3\"\n",
            "mkfs.exfat": f"#!/bin/sh\necho \"mkfs.exfat $@\" >> {t}/calls\n",
            "systemctl": f"#!/bin/sh\necho \"systemctl $@\" >> {t}/calls\n",
            "poweroff": f"#!/bin/sh\necho \"poweroff $@\" >> {t}/calls\n",
        }
        for name, body in scripts.items():
            (t / "bin" / name).write_text(body)
            (t / "bin" / name).chmod(0o755)

    def calls(self):
        p = self.t / "calls"
        return p.read_text().splitlines() if p.exists() else []

    def sysdisk(self, name, sectors, parts, first_minor):
        t = self.t
        d = t / "sys/devices/mmc" / name
        d.mkdir(parents=True)
        (d / "size").write_text(f"{sectors}\n")
        (t / "sys/block" / name).symlink_to(d)
        (t / "sys/dev/block" / f"179:{first_minor}").symlink_to(d)
        for n in parts:
            p = d / f"{name}p{n}"
            p.mkdir()
            (p / "partition").write_text(f"{n}\n")
            (t / "sys/dev/block" / f"179:{first_minor + n}").symlink_to(p)

    def types(self, dev, fs):
        with open(self.t / "types", "a") as f:
            f.write(f"{self.t / 'dev' / dev} {fs}\n")

    # --- the original card ---
    def original_card(self, evb=False, allwinner=False, rk_dtb=True, gpu_links=True, p2_sectors=None):
        t = self.t
        p2 = self.ORIG_P2 if p2_sectors is None else (0x83, 34816, p2_sectors)
        total = 40000 if p2_sectors is None else 34816 + p2_sectors + 1000
        raw = bytearray(min(total, 40000) * 512)
        raw[:512] = mbr(SIG_ORIG, [self.ORIG_P1, p2, self.ORIG_P3])
        self.loader = bytes((i * 7 + 3) % 251 + 1 for i in range(300 * 512))        # nonzero, 300 sectors
        raw[64 * 512:64 * 512 + len(self.loader)] = self.loader
        if allwinner:
            raw[16 * 512 + 4:16 * 512 + 12] = b"eGON.BT0"
        with open(t / "dev/mmcblk0", "wb") as f:
            f.write(bytes(raw))
            f.truncate(total * 512)                      # sparse when the original is "big"
        self.sysdisk("mmcblk0", total, [1, 2, 3], 0)
        self.types("mmcblk0p1", "vfat")
        self.types("mmcblk0p2", "ext4")
        self.types("mmcblk0p3", "exfat")
        boot = t / "dev/mmcblk0p1"
        (boot / "ScreenFiles/Panel 1").mkdir(parents=True)
        (boot / "boot.ini").write_text(BOOT_INI)
        (boot / "ScreenFiles/Panel 1/boot.ini").write_text(BOOT_INI)
        (boot / "Image").write_bytes(b"kernel" * 1000)
        (boot / "uInitrd").write_bytes(b"initrd" * 500)
        if rk_dtb:
            (boot / "rk3326-r35s-linux.dtb").write_bytes(b"dtb1" * 50)
        if evb:
            (boot / "rk3326-evb-lp3-v12-linux.dtb").write_bytes(b"evb")
        (boot / ".DS_Store").write_bytes(b"finder litter")
        root = t / "dev/mmcblk0p2"
        (root / "etc").mkdir(parents=True)
        (root / "etc/os-release").write_text("NAME=Ubuntu\n")
        (root / "lib").symlink_to("/usr/lib")                       # absolute: must be read inside the card, never on this machine
        mods = root / "usr/lib/modules/4.4.189"
        mods.mkdir(parents=True)
        (mods / "modules.dep").write_text("kernel/a.ko:\n")
        (mods / "kernel").mkdir()
        (mods / "kernel/a.ko").write_bytes(b"\x7fELFmodule")
        (mods / "build").symlink_to("/usr/src/linux-headers")       # dangling and absolute: kept as a link
        (root / "usr/lib/firmware").mkdir(parents=True)
        (root / "usr/lib/firmware/x.bin").write_bytes(b"firmware")
        gpu = root / "usr/local/lib/aarch64-linux-gnu"
        gpu.mkdir(parents=True)
        (gpu / "libmali-bifrost-g31-rxp0-gbm.so").write_bytes(Elf.elf(["libdrm.so.2", "libc.so.6"]))
        (gpu / "libmali.so").symlink_to("libmali-bifrost-g31-rxp0-gbm.so")
        if gpu_links:
            for n in ("libEGL.so.1", "libGLESv2.so.2", "libgbm.so.1"):
                (gpu / n).symlink_to("libmali.so")
        (gpu / "unrelated.so").write_bytes(b"x")
        # what the new card's system already has, so the blob's needs are met
        (t / "root/usr/lib/aarch64-linux-gnu").mkdir(parents=True, exist_ok=True)
        for n in ("libdrm.so.2", "libc.so.6"):
            (t / "root/usr/lib/aarch64-linux-gnu" / n).write_bytes(b"lib")

    # --- the new card ---
    def new_card(self, sig=SIG_NEW, loader=False, p3=False):
        t = self.t
        entries = list(self.NEW_PARTS) + ([(0x07, 65536, 100000)] if p3 else [])
        raw = t / "dev/mmcblk1"
        with open(raw, "wb") as f:
            f.write(mbr(sig, entries))
            f.truncate(self.NEW_TOTAL * 512)
        if loader:
            with open(raw, "r+b") as f:
                f.seek(64 * 512)
                f.write(b"already a loader")
        self.sysdisk("mmcblk1", self.NEW_TOTAL, [1, 2], 32)

    def new_card_replace(self, **kw):
        shutil.rmtree(self.t / "sys/devices/mmc/mmcblk1")
        for p in list((self.t / "sys/dev/block").iterdir()) + [self.t / "sys/block/mmcblk1"]:
            if p.is_symlink() and "mmcblk1" in str(os.readlink(p)):
                p.unlink()
        self.new_card(**kw)

    def original_card_replace(self, **kw):
        shutil.rmtree(self.t / "dev/mmcblk0p1"); shutil.rmtree(self.t / "dev/mmcblk0p2")
        shutil.rmtree(self.t / "sys/devices/mmc/mmcblk0"); (self.t / "sys/block/mmcblk0").unlink()
        for p in (self.t / "sys/dev/block").iterdir():
            if p.is_symlink() and "mmcblk0" in str(os.readlink(p)):
                p.unlink()
        (self.t / "types").write_text("")
        self.original_card(**kw)

    def run_setup(self, **env):
        m = load(dict(self.env, **env))
        with mock.patch.dict(os.environ, dict(self.env, **env)), mock.patch("sys.stderr", io.StringIO()) as err, mock.patch("sys.stdout", io.StringIO()) as out:
            code = m.main(["r3i6nos-setup"])
        self.err, self.out = err.getvalue(), out.getvalue()
        self.frames = [f for f in self.out.split("\033[2J\033[H") if f.strip()]
        return code, m

    def report(self):
        return (self.t / "boot/r3i6nos-setup-report.txt").read_text()


class Detect(Fixture):
    def test_setup_boot_is_recognised(self):
        m = load(self.env)
        mode, info = m.detect()
        self.assertEqual((mode, info["source"], info["root"]), ("setup", "mmcblk0", "mmcblk1"))

    def test_a_card_with_its_own_bootloader_is_a_normal_boot(self):
        self.new_card_replace(loader=True)
        code, m = self.run_setup()
        self.assertEqual(code, 0)
        self.assertEqual(self.calls(), [])
        self.assertIn("mode=normal", (self.t / "run/setup-state").read_text())
        self.assertFalse((self.t / "run/setup-source").exists())

    def test_no_original_card_is_a_normal_boot(self):
        (self.t / "sys/block/mmcblk0").unlink()
        code, m = self.run_setup()
        self.assertEqual(code, 0)
        self.assertIn("no original card", (self.t / "run/setup-state").read_text())

    def test_a_card_that_is_not_an_mbr_card_is_not_the_original(self):
        raw = bytearray((self.t / "dev/mmcblk0").read_bytes())
        raw[510:512] = b"\0\0"
        (self.t / "dev/mmcblk0").write_bytes(bytes(raw))
        self.assertEqual(load(self.env).detect()[0], "normal")



class Setup(Fixture):
    def test_the_whole_setup(self):
        t = self.t
        orig_raw, orig_boot, orig_root = file_hash(t / "dev/mmcblk0"), tree_hash(t / "dev/mmcblk0p1"), tree_hash(t / "dev/mmcblk0p2")
        code, m = self.run_setup()
        self.assertEqual(code, 0, self.err)

        # the original card is untouched: the raw disk, and both folders standing for its partitions
        self.assertEqual(file_hash(t / "dev/mmcblk0"), orig_raw)
        self.assertEqual(tree_hash(t / "dev/mmcblk0p1"), orig_boot)
        self.assertEqual(tree_hash(t / "dev/mmcblk0p2"), orig_root)
        # it was mounted read-only, the root with noload, and nothing else was mounted from it
        mounts = [c for c in self.calls() if c.startswith("mount ") and "mmcblk0" in c]
        self.assertTrue(mounts)
        self.assertTrue(all(" -o ro" in c for c in mounts), mounts)
        self.assertTrue(any("ro,noload" in c and "mmcblk0p2" in c for c in mounts), mounts)

        # the bootloader is at sector 64 of the new card, read back; the rest of its first sectors are as they were
        new = (t / "dev/mmcblk1").read_bytes()[:40 * 512 * 1024]
        self.assertEqual(new[64 * 512:64 * 512 + len(self.loader)], self.loader)
        self.assertEqual(new[:446], mbr(SIG_NEW, self.NEW_PARTS)[:446])

        # the boot files: root= points at this card, junk is left behind, binary files are byte for byte
        boot = t / "boot"
        for ini in (boot / "boot.ini", boot / "ScreenFiles/Panel 1/boot.ini"):
            text = ini.read_text()
            self.assertIn(f"root=UUID='{ROOT_UUID}'", text)
            self.assertNotIn(STOCK_UUID, text)
        self.assertEqual((boot / "Image").read_bytes(), b"kernel" * 1000)
        self.assertEqual((boot / "rk3326-r35s-linux.dtb").read_bytes(), b"dtb1" * 50)
        self.assertFalse((boot / ".DS_Store").exists())
        self.assertEqual([p for p in boot.rglob("*.r3tmp")], [])

        # the drivers: modules (links kept as links), firmware, the Mali blob and the links that resolve to it
        root = t / "root"
        self.assertEqual((root / "usr/lib/modules/4.4.189/kernel/a.ko").read_bytes(), b"\x7fELFmodule")
        self.assertEqual(os.readlink(root / "usr/lib/modules/4.4.189/build"), "/usr/src/linux-headers")
        self.assertEqual((root / "usr/lib/firmware/x.bin").read_bytes(), b"firmware")
        gpu = root / "opt/libmali"
        self.assertEqual(sorted(p.name for p in gpu.iterdir()),
                         ["libEGL.so.1", "libGLESv2.so.2", "libgbm.so.1", "libmali-bifrost-g31-rxp0-gbm.so", "libmali.so"])
        self.assertEqual(os.readlink(gpu / "libEGL.so.1"), "libmali.so")
        self.assertFalse((gpu / "unrelated.so").exists())
        self.assertEqual([p for p in root.rglob("*.r3new")] + [p for p in root.rglob("*.r3old")], [])

        # the games partition: the same entry scripts/add_games_partition.py makes, formatted, with the folders
        after = (t / "dev/mmcblk1").read_bytes()[:512]
        want, start, size = agp.add_games_partition(mbr(SIG_NEW, self.NEW_PARTS), self.NEW_TOTAL * 512)
        self.assertEqual(after, want)
        self.assertIn(f"mkfs.exfat -L EASYROMS {t / 'dev/mmcblk1p3'}", self.calls())
        for f in ("Love 11.5", "Love 12.0", "native_deb"):
            self.assertTrue((t / "dev/mmcblk1p3" / f).is_dir(), f)
        self.assertFalse((t / "dev/mmcblk1p3/Backup").exists(), "the backup is off by default")

        # the original was never formatted, partitioned or written
        self.assertFalse([c for c in self.calls() if ("mkfs" in c or "partx" in c) and "mmcblk0" in c])

        # the report, the records and the power-off
        rep = self.report()
        self.assertTrue(rep.rstrip().endswith("end"))
        self.assertIn("PASS genuine R36S", rep)
        self.assertNotRegex(rep, r"(?m)^FAIL ")
        self.assertIn("SKIP backup: offered (", rep)
        self.assertIn("and not asked for", rep)
        self.assertEqual((t / "run/setup-source").read_text().strip(), "mmcblk0")
        self.assertIn("VERSION=0.1.0", (t / "var/setup-done").read_text())
        self.assertIn("systemctl --no-block poweroff", self.calls())

    def test_the_new_cards_boot_is_mounted_by_device_not_by_label(self):
        code, m = self.run_setup()
        self.assertEqual(code, 0, self.err)
        self.assertIn(f"mount -t vfat -o rw,noatime {self.t / 'dev/mmcblk1p1'} {self.t / 'boot'}", self.calls())

    def test_the_originals_boot_partition_mounted_at_boot_by_label_is_unmounted_first(self):
        """fstab mounts LABEL=BOOT at /boot; the original's boot partition may have that label and be what is there, read-write."""
        t = self.t
        (t / "proc/mounts").write_text(f"{t / 'dev/mmcblk0p1'} {t / 'boot'} vfat rw 0 0\n")
        orig_boot = tree_hash(t / "dev/mmcblk0p1")
        code, m = self.run_setup()
        self.assertEqual(code, 0, self.err)
        calls = self.calls()
        self.assertLess(calls.index(f"umount {t / 'boot'}"), calls.index(f"mount -t vfat -o rw,noatime {t / 'dev/mmcblk1p1'} {t / 'boot'}"))
        self.assertEqual(tree_hash(t / "dev/mmcblk0p1"), orig_boot, "nothing is written to the original's boot partition")
        self.assertIn(f"root=UUID='{ROOT_UUID}'", (t / "dev/mmcblk1p1/boot.ini").read_text())

    def test_when_the_originals_boot_cannot_be_unmounted_nothing_is_written(self):
        t = self.t
        (t / "proc/mounts").write_text(f"{t / 'dev/mmcblk0p1'} {t / 'boot'} vfat rw 0 0\n")
        (t / "bin/umount").write_text("#!/bin/sh\necho 'umount: target is busy' >&2\nexit 32\n")
        before = disk_hash(t / "dev/mmcblk1")
        code, m = self.run_setup()
        self.assertEqual(code, 1)
        self.assertEqual(disk_hash(t / "dev/mmcblk1"), before)
        self.assertIn("could not be unmounted", self.report())

    def test_this_cards_boot_already_mounted_is_used_as_it_is(self):
        t = self.t
        (t / "proc/mounts").write_text(f"{t / 'dev/mmcblk1p1'} {t / 'boot'} vfat rw 0 0\n")
        code, m = self.run_setup()
        self.assertEqual(code, 0, self.err)
        self.assertFalse([c for c in self.calls() if c.startswith("mount -t vfat -o rw")])
        self.assertTrue((t / "boot/boot.ini").is_file())

    def test_a_third_device_at_boot_stops_the_setup(self):
        t = self.t
        (t / "proc/mounts").write_text(f"{t / 'dev/sda1'} {t / 'boot'} vfat rw 0 0\n")
        code, m = self.run_setup()
        self.assertEqual(code, 1)
        self.assertIn("neither card in this setup", self.report())

    def test_the_report_carries_what_the_first_trial_needs_to_read(self):
        """No keyboard, no screen capture: the report is how a trial sees what the boot did (docs/TRIAL-T1.md)."""
        t = self.t
        (t / "proc/cmdline").write_text(f"root=UUID={ROOT_UUID} rootwait rw panel=4\n")
        (t / "proc/uptime").write_text("41.50 100.00\n")
        (t / "proc/meminfo").write_text("MemTotal: 919232 kB\nMemAvailable: 848420 kB\n")
        (t / "proc/mounts").write_text(f"{t / 'dev/mmcblk0p1'} {t / 'boot'} vfat rw 0 0\n")
        code, m = self.run_setup()
        self.assertEqual(code, 0, self.err)
        rep = self.report()
        for want in (f"INFO kernel command line: root=UUID={ROOT_UUID} rootwait rw panel=4", "INFO seconds since power on: 41.50",
                     "INFO memory available at start: 848420 kB", "INFO memory available after the drivers: 848420 kB",
                     f"INFO boot partition mounted at /boot before setup: {t / 'dev/mmcblk0p1'}",
                     "INFO time Check the original card:", "INFO time Install the drivers:", "INFO time Make the card bootable:"):
            self.assertIn(want, rep)

    def test_the_bootloader_is_the_last_write_so_an_interrupted_setup_is_run_again(self):
        """With a bootloader at sector 64 the next boot sees a finished card. So it goes in last: a failure before it leaves a setup card."""
        t = self.t
        (t / "bin/mkfs.exfat").write_text("#!/bin/sh\necho 'mkfs.exfat: failed' >&2\nexit 1\n")
        code, m = self.run_setup()
        self.assertEqual(code, 1)
        self.assertIn("could not format the games partition", self.report())
        head = (t / "dev/mmcblk1").read_bytes()[64 * 512:(64 + 300) * 512]
        self.assertFalse(any(head), "no bootloader was written")
        self.assertNotIn("bootloader written", self.report())
        mode, info = load(self.env).detect()
        self.assertEqual(mode, "setup", "the card is still a setup card")
        # the cause is fixed (the games partition entry is already on the card, so it is found as existing): a re-run finishes the job
        (t / "bin/mkfs.exfat").write_text(f"#!/bin/sh\necho \"mkfs.exfat $@\" >> {t}/calls\n")
        code, m = self.run_setup()
        self.assertEqual(code, 0, self.err)
        self.assertEqual((t / "dev/mmcblk1").read_bytes()[64 * 512:64 * 512 + len(self.loader)], self.loader)
        self.assertEqual(load(self.env).detect()[0], "normal", "now it is a finished card")
        self.assertTrue(self.report().rstrip().endswith("end"))

    def test_a_failure_while_installing_the_drivers_also_leaves_a_setup_card(self):
        t = self.t
        (t / "dev/mmcblk0p2/usr/lib/modules/4.4.189/kernel/a.ko").write_bytes(b"x")
        real = shutil.copy2

        def corrupt(src, dst, **kw):
            real(src, dst, **kw)
            if str(dst).endswith("a.ko"):
                Path(dst).write_bytes(b"corrupted on the way")
            return dst
        with mock.patch("shutil.copy2", side_effect=corrupt):
            code, m = self.run_setup()
        self.assertEqual(code, 1)
        self.assertIn("does not match the original", self.report())
        self.assertFalse(any((t / "dev/mmcblk1").read_bytes()[64 * 512:(64 + 300) * 512]))
        self.assertEqual(load(self.env).detect()[0], "setup")

    def test_a_setup_boot_changes_nothing_when_the_card_is_a_clone(self):
        for kw, why in ((dict(allwinner=True), "eGON.BT0"), (dict(evb=True), "rk3326-evb-lp3-v12"), (dict(rk_dtb=False), "no RK3326 board file")):
            with self.subTest(why=why):
                self.tearDown(); self.setUp()
                self.original_card_replace(**kw)
                before = disk_hash(self.t / "dev/mmcblk1")
                code, m = self.run_setup()
                self.assertEqual(code, 1)
                self.assertEqual(disk_hash(self.t / "dev/mmcblk1"), before, "the new card must not be written")
                self.assertEqual(list((self.t / "boot").iterdir()) and [p.name for p in (self.t / "boot").iterdir()], ["r3i6nos-setup-report.txt"])
                self.assertFalse((self.t / "root/opt").exists())
                self.assertIn("FAIL genuine R36S", self.report())
                self.assertIn(why, self.report())
                self.assertFalse([c for c in self.calls() if "poweroff" in c])


    def test_refuses_a_card_that_is_not_an_r3i6nos_image(self):
        self.new_card_replace(sig=0x11111111)
        before = disk_hash(self.t / "dev/mmcblk1")
        code, m = self.run_setup()
        self.assertEqual(code, 1)
        self.assertEqual(disk_hash(self.t / "dev/mmcblk1"), before)
        self.assertIn("not an r3i6nos card", self.report())


    def test_refuses_a_graphics_driver_without_the_libraries_sdl_needs(self):
        self.original_card_replace(gpu_links=False)
        before = disk_hash(self.t / "dev/mmcblk1")
        code, m = self.run_setup()
        self.assertEqual(code, 1)
        self.assertEqual(disk_hash(self.t / "dev/mmcblk1"), before)
        self.assertIn("libEGL.so.1", self.report())
        code, m = self.run_setup(R3I6NOS_GPU_ALLOW_PARTIAL="1")
        self.assertEqual(code, 0, self.err)

    def test_not_enough_room_stops_before_anything_is_written(self):
        before = disk_hash(self.t / "dev/mmcblk1")
        usage = shutil._ntuple_diskusage(10 << 20, 9 << 20, 1 << 20)
        with mock.patch("shutil.disk_usage", return_value=usage):
            code, m = self.run_setup()
        self.assertEqual(code, 1)
        self.assertEqual(disk_hash(self.t / "dev/mmcblk1"), before)
        self.assertIn("MB free but the drivers need", self.report())

    def test_an_existing_games_partition_is_kept(self):
        self.new_card_replace(p3=True)
        (self.t / "dev/mmcblk1p3").mkdir()
        mbr_before = (self.t / "dev/mmcblk1").read_bytes()[:512]
        code, m = self.run_setup()
        self.assertEqual(code, 0, self.err)
        self.assertEqual((self.t / "dev/mmcblk1").read_bytes()[:512], mbr_before)
        self.assertFalse([c for c in self.calls() if "mkfs.exfat" in c])
        self.assertIn("exists: this card already has a games partition", self.report())

    def test_a_used_space_after_rootfs_is_left_alone(self):
        with open(self.t / "dev/mmcblk1", "r+b") as f:
            f.seek(65536 * 512)
            f.write(b"somebody's data")
        mbr_before = (self.t / "dev/mmcblk1").read_bytes()[:512]
        code, m = self.run_setup()
        self.assertEqual(code, 0, self.err)
        self.assertEqual((self.t / "dev/mmcblk1").read_bytes()[:512], mbr_before)
        self.assertFalse([c for c in self.calls() if "mkfs.exfat" in c])
        self.assertIn("SKIP games partition: skipped: the space after ROOTFS is not empty", self.report())

    def test_a_small_card_gets_no_games_partition_and_says_why(self):
        shutil.rmtree(self.t / "sys/devices/mmc/mmcblk1")
        for p in list((self.t / "sys/dev/block").iterdir()) + [self.t / "sys/block/mmcblk1"]:
            if p.is_symlink() and "mmcblk1" in str(os.readlink(p)):
                p.unlink()
        self.NEW_TOTAL = 300000
        self.new_card()
        code, m = self.run_setup()
        self.assertEqual(code, 0, self.err)
        self.assertIn("not enough for a games partition", self.report())

    def test_leftovers_of_an_interrupted_run_are_replaced(self):
        root = self.t / "root"
        (root / "usr/lib/modules.r3new/junk").mkdir(parents=True)
        (root / "usr/lib/modules/old-version").mkdir(parents=True)
        (root / "opt/libmali").mkdir(parents=True)
        (root / "opt/libmali/stale.so").write_text("old")
        code, m = self.run_setup()
        self.assertEqual(code, 0, self.err)
        self.assertFalse((root / "usr/lib/modules.r3new").exists())
        self.assertFalse((root / "usr/lib/modules/old-version").exists())
        self.assertFalse((root / "opt/libmali/stale.so").exists())
        self.assertTrue((root / "usr/lib/modules/4.4.189/modules.dep").exists())

    def test_a_missing_library_the_blob_needs_is_a_failure_line_but_the_setup_finishes(self):
        (self.t / "root/usr/lib/aarch64-linux-gnu/libdrm.so.2").unlink()
        code, m = self.run_setup()
        self.assertEqual(code, 0, self.err)
        self.assertIn("FAIL graphics driver link: libmali-bifrost-g31-rxp0-gbm.so needs libdrm.so.2", self.report())
        self.assertIn("INFO libmali-bifrost-g31-rxp0-gbm.so needs: libdrm.so.2 libc.so.6", self.report())

    def test_the_original_is_not_mounted_as_a_data_card_after_a_failure(self):
        self.original_card_replace(evb=True)
        code, m = self.run_setup()
        self.assertEqual(code, 1)
        self.assertEqual((self.t / "run/setup-source").read_text().strip(), "mmcblk0")


class Feedback(Fixture):
    """Progress bars on every step, the charging tip, and the backup offered only when there is room."""

    def run_screen(self, **env):
        return self.run_setup(R3I6NOS_SETUP_COLS="64", **env)

    @staticmethod
    def pct(frame, label):
        m = re.search(rf"{label}\s+\[[#.]{{36}}\]\s+(\d+)%", frame)
        return int(m.group(1)) if m else None

    def test_every_frame_has_both_bars_and_the_tip_and_the_overall_bar_never_goes_back(self):
        code, m = self.run_screen()
        self.assertEqual(code, 0, self.err)
        working = [f for f in self.frames if "This step" in f]
        self.assertGreater(len(working), 20)
        for f in working:
            self.assertIsNotNone(self.pct(f, "This step"), f)
            self.assertIsNotNone(self.pct(f, "Overall"), f)
            self.assertIn("Keep the console charged or plugged in", f)
        overall = [self.pct(f, "Overall") for f in working]
        self.assertEqual(overall, sorted(overall), "the overall bar only moves forward when no backup is chosen")
        self.assertEqual(overall[-1], 100)
        self.assertLessEqual(max(f.count("\n") for f in working), 22, "nothing scrolls")
        self.assertIn("[x] Check the original card", working[-1])
        self.assertIn("[-] Back up the original (only if asked)", working[-1])

    def test_the_copy_reports_progress_in_bytes(self):
        big = self.t / "dev/mmcblk0p2/usr/lib/modules/4.4.189/kernel/big.ko"
        big.write_bytes(os.urandom(6 << 20))
        code, m = self.run_screen()
        self.assertEqual(code, 0, self.err)
        copying = [self.pct(f, "This step") for f in self.frames if "Copying the kernel modules" in f]
        checking = [self.pct(f, "This step") for f in self.frames if "Checking the kernel modules" in f]
        self.assertTrue(copying and checking)
        self.assertEqual(copying, sorted(copying))
        self.assertLess(max(copying), min(checking) + 1)
        self.assertTrue(any(f for f in self.frames if "Writing the boot files" in f))
        self.assertTrue(any(f for f in self.frames if "Formatting the games partition" in f))

    def test_the_wait_for_a_button_has_a_countdown_bar(self):
        (self.t / "input" / "event0").write_bytes(b"")                 # a pad that sends nothing
        code, m = self.run_screen(R3I6NOS_SETUP_BACKUP="", R3I6NOS_SETUP_WAIT="2")
        self.assertEqual(code, 0, self.err)
        offers = [f for f in self.frames if "Press any button within" in f and "[>] Back up the original" in f]
        self.assertGreaterEqual(len(offers), 3)
        self.assertEqual({re.search(r"within (\d+) s", f).group(1) for f in offers} >= {"2", "1"}, True)
        steps = [self.pct(f, "This step") for f in offers]
        self.assertEqual(steps, sorted(steps))
        self.assertGreater(steps[-1], steps[0])
        self.assertIn("SKIP backup: offered (", self.report())

    def test_no_backup_is_offered_when_the_new_card_has_no_room_for_it(self):
        self.original_card_replace(p2_sectors=30_000_000)               # a 15 GB system area: a backup needs about 5 GB, the new card has 1.2 GB
        (self.t / "input" / "event0").write_bytes(bytes(16) + (1).to_bytes(2, "little") + (304).to_bytes(2, "little") + (1).to_bytes(4, "little"))
        code, m = self.run_screen(R3I6NOS_SETUP_BACKUP="", R3I6NOS_SETUP_WAIT="5")
        self.assertEqual(code, 0, self.err)
        self.assertFalse([f for f in self.frames if "Also back up the original card" in f], "it must not even ask")
        self.assertTrue([f for f in self.frames if "No backup offered" in f])
        self.assertRegex(self.report(), r"SKIP backup: not offered: this card has \d+ MB for it and a backup needs about \d+ MB")
        self.assertFalse((self.t / "dev/mmcblk1p3/Backup").exists(), "a pressed button cannot force a backup that does not fit")

    def test_asking_for_a_backup_that_does_not_fit_is_not_honoured(self):
        self.original_card_replace(p2_sectors=30_000_000)
        code, m = self.run_setup(R3I6NOS_SETUP_BACKUP="1")
        self.assertEqual(code, 0, self.err)
        self.assertIn("SKIP backup: not offered", self.report())
        self.assertFalse((self.t / "dev/mmcblk1p3/Backup").exists())

    def test_room_is_measured_on_an_existing_games_partition_too(self):
        m = load(self.env)
        parts = [{"number": 1, "type": 0x0B, "start": 32768, "size": 2048}, {"number": 2, "type": 0x83, "start": 34816, "size": 4096}]
        self.new_card_replace(p3=True)
        (self.t / "dev/mmcblk1p3").mkdir()
        mounts = m.Mounts()
        with mock.patch.dict(os.environ, {"PATH": self.env["PATH"]}):
            enough, avail, why = m.backup_room("mmcblk1", parts, mounts)
        self.assertTrue(enough, why)
        self.assertEqual(mounts.done, [], "the probe mount is undone")
        usage = shutil._ntuple_diskusage(10 << 30, 10 << 30, 100 << 20)
        with mock.patch("shutil.disk_usage", return_value=usage), mock.patch.dict(os.environ, {"PATH": self.env["PATH"]}):
            enough, avail, why = m.backup_room("mmcblk1", parts, mounts)
        self.assertFalse(enough)
        self.assertIn("100 MB for it", why)

    def test_a_small_card_with_no_games_area_is_not_offered_a_backup(self):
        self.NEW_TOTAL = 300000
        self.new_card_replace()
        m = load(self.env)
        enough, avail, why = m.backup_room("mmcblk1", [{"number": 2, "type": 0x83, "start": 34816, "size": 4096}], m.Mounts())
        self.assertFalse(enough)
        self.assertIn("not enough for a games partition", why)

    def test_a_low_battery_is_a_loud_note_and_a_fail_line(self):
        pw = self.t / "sys/class/power_supply/battery"
        pw.mkdir(parents=True)
        (pw / "type").write_text("Battery\n")
        (pw / "capacity").write_text("18\n")
        (pw / "status").write_text("Discharging\n")
        m = load(self.env)
        self.assertEqual(m.battery_note(), "LOW BATTERY (18%): plug in the charger now.")
        code, _ = self.run_screen()
        self.assertEqual(code, 0, self.err)
        self.assertTrue(all("LOW BATTERY (18%)" in f for f in self.frames if "This step" in f))
        self.assertIn("FAIL battery: LOW BATTERY (18%)", self.report())
        (pw / "status").write_text("Charging\n")
        self.assertEqual(m.battery_note(), "Battery 18%, charging")
        (pw / "capacity").write_text("87\n")
        (pw / "status").write_text("Discharging\n")
        self.assertEqual(m.battery_note(), "Battery 87%, discharging")

    def test_no_battery_is_no_note(self):
        self.assertEqual(load(self.env).battery_note(), "")


class Backup(Fixture):
    def test_the_backup_is_made_and_checked_when_asked_for(self):
        t = self.t
        code, m = self.run_setup(R3I6NOS_SETUP_BACKUP="1")
        self.assertEqual(code, 0, self.err)
        files = sorted(p.name for p in (t / "dev/mmcblk1p3/Backup").iterdir())
        self.assertEqual(len(files), 2, files)
        gz = next(f for f in files if f.endswith(".img.gz"))
        self.assertRegex(gz, r"^stock-\d{8}-\d{6}-12345678\.img\.gz$")
        import gzip
        want = (t / "dev/mmcblk0").read_bytes()[:(34816 + 4096) * 512]          # everything up to the end of partition 2, not the games partition
        self.assertEqual(gzip.open(t / "dev/mmcblk1p3/Backup" / gz).read(), want)
        line = (t / "dev/mmcblk1p3/Backup" / (gz + ".sha256")).read_text().split()
        self.assertEqual(line[0], hashlib.sha256(want).hexdigest())
        self.assertIn("PASS backup:", self.report())

    def test_no_room_for_the_backup_is_said_and_the_setup_goes_on(self):
        usage = shutil._ntuple_diskusage(10 << 30, 10 << 30 - 1, 1 << 20)
        real = shutil.disk_usage

        def fake(path):
            return usage if str(path).endswith("setup/games") else real(path)
        with mock.patch("shutil.disk_usage", side_effect=fake):
            code, m = self.run_setup(R3I6NOS_SETUP_BACKUP="1")
        self.assertEqual(code, 0, self.err)
        self.assertIn("SKIP backup: not enough room for a backup", self.report())

    def test_a_backup_that_does_not_read_back_is_removed(self):
        m = load(dict(self.env))
        dest = self.t / "bk"
        with mock.patch.object(m.gzip, "open", side_effect=lambda *a, **k: io.BytesIO(b"wrong")):
            with self.assertRaises(m.Stop):
                m.do_backup("mmcblk0", [{"number": 1, "type": 0x0B, "start": 32768, "size": 2048}, {"number": 2, "type": 0x83, "start": 34816, "size": 4096}],
                            "12345678", dest, lambda *a, **k: None)
        self.assertEqual([p.name for p in dest.iterdir()], [], "no half-made backup is left")


class Buttons(Fixture):
    def event(self, name, typ, code, val):
        (self.t / "input" / name).write_bytes(bytes(16) + typ.to_bytes(2, "little") + code.to_bytes(2, "little") + val.to_bytes(4, "little", signed=True))

    def test_any_button_press_counts(self):
        m = load(self.env)
        self.event("event0", 1, 304, 1)
        self.assertTrue(m.button_pressed(1))

    def test_release_power_and_volume_do_not_count(self):
        m = load(self.env)
        for code, val in ((304, 0), (116, 1), (114, 1), (115, 1)):
            self.event("event0", 1, code, val)
            self.assertFalse(m.button_pressed(0.3), (code, val))

    def test_a_joydev_button_counts_but_its_initial_state_does_not(self):
        m = load(self.env)
        (self.t / "input/js0").write_bytes((0).to_bytes(4, "little") + (1).to_bytes(2, "little") + bytes([1, 1]))          # button 1 pressed
        self.assertTrue(m.button_pressed(1))
        (self.t / "input/js0").write_bytes((0).to_bytes(4, "little") + (1).to_bytes(2, "little") + bytes([0x81, 1]))      # synthetic init event
        self.assertFalse(m.button_pressed(0.3))

    def test_no_input_devices_means_no(self):
        shutil.rmtree(self.t / "input")
        self.assertFalse(load(self.env).button_pressed(0.3))

    def test_a_press_in_the_wait_turns_the_backup_on(self):
        self.event("event0", 1, 305, 1)
        env = dict(self.env)
        del env["R3I6NOS_SETUP_BACKUP"]
        env["R3I6NOS_SETUP_WAIT"] = "2"
        m = load(env)
        with mock.patch.dict(os.environ, env), mock.patch("sys.stderr", io.StringIO()):
            self.assertEqual(m.main(["r3i6nos-setup"]), 0)
        self.assertTrue(list((self.t / "dev/mmcblk1p3/Backup").glob("*.img.gz")))


class Helpers(Fixture):
    def test_the_root_rewrite_matches_the_scripts_and_the_boot_disk_guide(self):
        m = load(self.env)
        self.assertEqual(m.ROOT_UUID, ROOT_UUID)
        sh = (REPO / "scripts/inject-boot.sh").read_text()
        self.assertEqual(m.ROOT_PATTERN, re.search(r'pattern = re\.compile\(r"""(.*?)"""\)', sh, re.S).group(1))
        cases = {"root=/dev/mmcblk0p2 rw": f"root=UUID={ROOT_UUID} rw", "root=LABEL=ROOTFS rw": f"root=UUID={ROOT_UUID} rw",
                 'root=UUID="x-y" rw': f'root=UUID="{ROOT_UUID}" rw', "root=UUID='x-y' rw": f"root=UUID='{ROOT_UUID}' rw"}
        for src, want in cases.items():
            self.assertEqual(m.rewrite_root(src), (want, 1))
        self.assertEqual(m.rewrite_root("nothing here"), ("nothing here", 0))

    def test_constants_match_the_image_layout_and_the_games_script(self):
        m = load(self.env)
        layout = (REPO / "scripts/image-layout.sh").read_text()
        self.assertEqual(m.DISK_SIGNATURE, int(re.search(r"^DISK_SIGNATURE=(0x[0-9a-f]+)", layout, re.M).group(1), 16))
        self.assertEqual((m.ALIGN, m.MIN_GAMES_SECTORS, m.MAX_LBA, m.DISK_SIGNATURE), (agp.ALIGN, agp.MIN_SECTORS, agp.MAX_LBA, agp.DISK_SIGNATURE))
        mb = mbr(SIG_NEW, self.NEW_PARTS)
        for total in (2_500_000, 62_000_000, 300_000):
            try:
                want = agp.add_games_partition(mb, total * 512)
            except agp.GamesError as e:
                got, why = m.games_entry(mb, total)
                self.assertIsNone(got)
                self.assertEqual(why, str(e))
                continue
            self.assertEqual(m.games_entry(mb, total)[0], want)

    def test_the_elf_reader_matches_the_hosts(self):
        import inject_rootfs as inj
        m = load(self.env)
        names = ["libwayland-client.so.0", "libdrm.so.2", "libc.so.6"]
        self.assertEqual(m.elf_needed(Elf.elf(names)), names)
        self.assertEqual(m.elf_needed(Elf.elf(names)), inj.needed(Elf.elf(names)))
        self.assertEqual(m.elf_needed(b"MZ not an elf"), [])

    def test_an_absolute_symlink_in_the_original_cannot_leave_it(self):
        m = load(self.env)
        top = self.t / "card"
        (top / "usr/lib/modules").mkdir(parents=True)
        (top / "lib").symlink_to("/usr/lib")
        (top / "evil").symlink_to("/etc")
        (top / "up").symlink_to("../../..")
        self.assertEqual(m.resolve_in(top, "/lib/modules"), top / "usr/lib/modules")
        self.assertIsNone(m.resolve_in(top, "/evil/passwd"))          # /etc inside the card, which has none; never this machine's
        self.assertEqual(m.resolve_in(top, "/up/usr"), top / "usr")
        self.assertIsNone(m.resolve_in(top, "/nothing"))

    def test_verify_finds_a_changed_file_and_a_changed_link(self):
        m = load(self.env)
        a, b = self.t / "a", self.t / "b"
        (a / "d").mkdir(parents=True)
        (a / "d/f").write_text("one")
        (a / "l").symlink_to("x")
        m.copy_tree(a, b)
        self.assertEqual(m.verify_tree(a, b), [])
        (b / "d/f").write_text("two")
        (b / "l").unlink()
        (b / "l").symlink_to("y")
        self.assertEqual(sorted(p.split(":")[0] for p in m.verify_tree(a, b)), ["content differs", "symlink differs"])

    def test_detect_says_what_it_would_do_and_changes_nothing(self):
        before = disk_hash(self.t / "dev/mmcblk1")
        m = load(self.env)
        out = io.StringIO()
        with mock.patch("sys.stdout", out):
            self.assertEqual(m.main(["r3i6nos-setup", "detect"]), 0)
        self.assertEqual(out.getvalue().strip(), "mode=setup source=mmcblk0 root=mmcblk1")
        self.assertEqual(disk_hash(self.t / "dev/mmcblk1"), before)
        self.assertEqual(self.calls(), [])


if __name__ == "__main__":
    unittest.main()

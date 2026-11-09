"""r3i6nos-cards (docs/STORAGE.md): mounting each card by its role, against a fake /sys and fake blkid and mount. No card is touched."""
import importlib.machinery
import importlib.util
import io
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

SRC = Path(__file__).resolve().parent.parent / "overlay/usr/local/bin/r3i6nos-cards"


def load(env):
    with mock.patch.dict(os.environ, env):
        loader = importlib.machinery.SourceFileLoader("r3i6nos_cards", str(SRC))
        spec = importlib.util.spec_from_loader("r3i6nos_cards", loader)
        mod = importlib.util.module_from_spec(spec)
        sys.dont_write_bytecode = True
        loader.exec_module(mod)
    return mod


class Cards(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.t = Path(self.tmp.name)
        for d in ("sys/block", "sys/dev/block", "sys/devices", "dev", "proc", "bin", "run"):
            (t / d).mkdir(parents=True)
        (t / "proc/mounts").write_text("/dev/mmcblk0p2 / ext4 rw 0 0\n")
        (t / "types").write_text("")            # device -> filesystem, for the fake blkid
        # fake blkid: prints the type listed for the device; fake mount: records its arguments, fails for devices listed in failmount
        (t / "bin/blkid").write_text(f"#!/bin/sh\nfor a; do d=$a; done\ngrep \"^$d \" {t}/types | cut -d' ' -f2\n")
        (t / "bin/mount").write_text(f"#!/bin/sh\necho \"$@\" >> {t}/mounted\n"
                                     f"if grep -qx \"$6\" {t}/failmount 2>/dev/null; then echo 'mount: wrong fs type' >&2; exit 32; fi\n")
        for f in ("blkid", "mount"):
            (t / "bin" / f).chmod(0o755)
        self.disk("mmcblk0", 249737216, {1: None, 2: "ext4", 3: "exfat"})
        self.env = {
            "R3I6NOS_SYS": str(t / "sys"), "R3I6NOS_DEV": str(t / "dev"), "R3I6NOS_PROC": str(t / "proc"), "R3I6NOS_ROOT_DEV": "179:2",
            "R3I6NOS_ROMS": str(t / "roms"), "R3I6NOS_ROMS2": str(t / "roms2"), "R3I6NOS_CARDS_STATE": str(t / "run/r3i6nos/cards"),
            "R3I6NOS_CARD_WAIT": "0", "PATH": f"{t / 'bin'}:{os.environ.get('PATH', '')}",
        }

    def tearDown(self):
        self.tmp.cleanup()

    def disk(self, name, sectors, parts, major=179, first_minor=0):
        """A disk in /sys with numbered partitions; parts maps number -> filesystem (None: none). A disk with no parts gets its own type."""
        t = self.t
        d = t / "sys/devices/mmc" / name
        d.mkdir(parents=True)
        (d / "size").write_text(f"{sectors}\n")
        (t / "sys/block" / name).symlink_to(d)
        (t / "sys/dev/block" / f"{major}:{first_minor}").symlink_to(d)
        for n, fs in parts.items():
            p = d / f"{name}p{n}"
            p.mkdir()
            (p / "partition").write_text(f"{n}\n")
            (t / "sys/dev/block" / f"{major}:{first_minor + n}").symlink_to(p)
            if fs:
                with open(t / "types", "a") as f:
                    f.write(f"{t / 'dev' / p.name} {fs}\n")

    def whole_disk_type(self, name, fs):
        with open(self.t / "types", "a") as f:
            f.write(f"{self.t / 'dev' / name} {fs}\n")

    def run_mount(self, **env):
        m = load(dict(self.env, **env))
        with mock.patch.dict(os.environ, {"PATH": self.env["PATH"]}), mock.patch("sys.stderr", io.StringIO()) as err:
            self.assertEqual(m.main(["r3i6nos-cards", "mount"]), 0, "it never fails the boot")
        self.err = err.getvalue()
        state = {}
        for line in (self.t / "run/r3i6nos/cards").read_text().splitlines():
            k, _, v = line.partition("=")
            state[k] = v
        mounted = (self.t / "mounted").read_text().splitlines() if (self.t / "mounted").exists() else []
        return state, mounted

    def test_both_cards_by_role_not_by_label(self):
        self.disk("mmcblk1", 62521344, {1: "exfat"}, first_minor=32)
        st, mounted = self.run_mount()
        t = self.t
        self.assertEqual(st["BOOT_DISK"], "mmcblk0")
        self.assertEqual((st["SYSTEM_DEV"], st["SYSTEM_FS"], st["SYSTEM_AT"]), (str(t / "dev/mmcblk0p3"), "exfat", str(t / "roms")))
        self.assertEqual((st["DATA_DEV"], st["DATA_FS"], st["DATA_AT"]), (str(t / "dev/mmcblk1p1"), "exfat", str(t / "roms2")))
        self.assertEqual(mounted, [f"-t exfat -o rw,noatime,uid=1000,gid=1000,umask=022 {t}/dev/mmcblk0p3 {t}/roms",
                                   f"-t exfat -o rw,noatime,uid=1000,gid=1000,umask=022 {t}/dev/mmcblk1p1 {t}/roms2"])
        for m in mounted:
            self.assertNotIn("LABEL", m)

    def test_no_second_card(self):
        st, mounted = self.run_mount()
        self.assertEqual(st["SYSTEM_AT"], str(self.t / "roms"))
        self.assertEqual((st["DATA_DEV"], st["DATA_AT"]), ("none", "none"))
        self.assertEqual(len(mounted), 1)

    def test_no_games_partition_puts_the_data_card_at_roms(self):
        self.tmp.cleanup(); self.setUp()
        # rebuild the boot disk without partition 3
        import shutil
        shutil.rmtree(self.t / "sys"); (self.t / "types").write_text("")
        for d in ("sys/block", "sys/dev/block", "sys/devices"):
            (self.t / d).mkdir(parents=True)
        self.disk("mmcblk0", 249737216, {1: None, 2: "ext4"})
        self.disk("mmcblk1", 62521344, {1: "vfat"}, first_minor=32)
        st, mounted = self.run_mount()
        self.assertEqual(st["SYSTEM_DEV"], "none")
        self.assertEqual(st["DATA_AT"], str(self.t / "roms"), "the old two-card setup keeps working")
        self.assertIn(f"{self.t}/dev/mmcblk1p1 {self.t}/roms", mounted[0])

    def test_a_card_without_a_partition_table_and_an_ext4_card(self):
        self.disk("mmcblk1", 62521344, {}, first_minor=32)
        self.whole_disk_type("mmcblk1", "ext4")
        st, mounted = self.run_mount()
        self.assertEqual((st["DATA_DEV"], st["DATA_FS"]), (str(self.t / "dev/mmcblk1"), "ext4"))
        self.assertIn("-t ext4 -o rw,noatime ", mounted[1], "ext4 keeps its own owners: no uid option")

    def test_the_first_partition_with_a_filesystem(self):
        self.disk("mmcblk1", 62521344, {1: None, 2: "exfat"}, first_minor=32)
        st, _ = self.run_mount()
        self.assertEqual(st["DATA_DEV"], str(self.t / "dev/mmcblk1p2"))

    def test_an_unsupported_filesystem_is_left_alone(self):
        self.disk("mmcblk1", 62521344, {1: "ntfs"}, first_minor=32)
        st, mounted = self.run_mount()
        self.assertEqual((st["DATA_FS"], st["DATA_AT"]), ("ntfs", "none"))
        self.assertEqual(len(mounted), 1)
        self.assertIn("left alone", self.err)

    def test_a_failed_mount_is_written_down_and_the_boot_goes_on(self):
        self.disk("mmcblk1", 62521344, {1: "exfat"}, first_minor=32)
        (self.t / "failmount").write_text(f"{self.t}/roms2\n")
        st, _ = self.run_mount()
        self.assertEqual(st["DATA_AT"], "none")
        self.assertEqual(st["SYSTEM_AT"], str(self.t / "roms"))
        self.assertIn("could not mount", self.err)

    def test_already_mounted_is_not_mounted_again(self):
        (self.t / "proc/mounts").write_text(f"/dev/mmcblk0p2 / ext4 rw 0 0\n/dev/mmcblk0p3 {self.t}/roms exfat rw 0 0\n")
        st, mounted = self.run_mount()
        self.assertEqual(st["SYSTEM_AT"], str(self.t / "roms"))
        self.assertEqual(mounted, [])

    def test_empty_slots_and_emmc_areas_are_skipped(self):
        self.disk("mmcblk1", 0, {}, first_minor=32)            # a slot with no card has no size
        self.disk("mmcblk2boot0", 8192, {}, first_minor=64)    # an eMMC boot area
        st, mounted = self.run_mount()
        self.assertEqual(st["DATA_DEV"], "none")
        self.assertEqual(len(mounted), 1)

    def test_status(self):
        self.run_mount()
        m = load(self.env)
        with mock.patch("sys.stdout", io.StringIO()) as out:
            self.assertEqual(m.main(["r3i6nos-cards", "status"]), 0)
        self.assertIn("BOOT_DISK=mmcblk0", out.getvalue())
        (self.t / "run/r3i6nos/cards").unlink()
        with mock.patch("sys.stdout", io.StringIO()):
            self.assertEqual(m.main(["r3i6nos-cards", "status"]), 1)
        with mock.patch("sys.stderr", io.StringIO()):
            self.assertEqual(m.main(["r3i6nos-cards"]), 2)


if __name__ == "__main__":
    unittest.main()

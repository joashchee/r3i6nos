"""r3i6nos-format (docs/STORAGE.md, "Format the data card"), against a fake /sys, /dev and /proc, and fake mkfs, mount and cards helper.
No real card or tool is touched: run, reread and sleep are replaced, and the "cards" are sparse files."""
import importlib.machinery
import importlib.util
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

SRC = Path(__file__).resolve().parent.parent / "overlay/usr/local/bin/r3i6nos-format"
SECTORS = 131072   # a 64 MiB "card"


def load(env):
    with mock.patch.dict(os.environ, env):
        loader = importlib.machinery.SourceFileLoader("r3i6nos_format", str(SRC))
        spec = importlib.util.spec_from_loader("r3i6nos_format", loader)
        mod = importlib.util.module_from_spec(spec)
        sys.dont_write_bytecode = True
        loader.exec_module(mod)
    return mod


class Format(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.t = Path(self.tmp.name)
        for d in ("req", "out", "state", "run", "dev", "proc", "sys/dev/block", "sys/block"):
            (t / d).mkdir(parents=True)
        # the boot card mmcblk0 (root on partition 2) and the data card mmcblk1; the data card holds old data
        p2 = t / "sys/block/mmcblk0/mmcblk0p2"
        p2.mkdir(parents=True)
        (p2 / "partition").write_text("2\n")
        (t / "sys/block/mmcblk0/size").write_text(f"{SECTORS}\n")
        os.symlink(p2, t / "sys/dev/block/179:2")
        (t / "sys/block/mmcblk1").mkdir()
        (t / "sys/block/mmcblk1/size").write_text(f"{SECTORS}\n")
        for n in ("mmcblk0", "mmcblk1"):
            with open(t / "dev" / n, "wb") as f:
                f.write(b"OLD" * 100000)
                f.truncate(SECTORS * 512)
        self.boot_before = (t / "dev/mmcblk0").read_bytes()
        (t / "proc/mounts").write_text(f"{t}/dev/mmcblk0p2 / ext4 rw 0 0\n{t}/dev/mmcblk0p3 /roms exfat rw 0 0\n{t}/dev/mmcblk1p1 /roms2 exfat rw 0 0\n")
        (t / "cards").write_text("BOOT_DISK=mmcblk0\nSYSTEM_DEV=/dev/mmcblk0p3\nDATA_DEV=/dev/mmcblk1p1\nDATA_AT=/roms2\n")
        self.m = load({
            "R3I6NOS_FORMAT_DIR": str(t / "req"), "R3I6NOS_FORMAT_OUT": str(t / "out"), "R3I6NOS_STATE": str(t / "state"),
            "R3I6NOS_SYS": str(t / "sys"), "R3I6NOS_DEV": str(t / "dev"), "R3I6NOS_PROC": str(t / "proc"),
            "R3I6NOS_ROOT_DEV": "179:2", "R3I6NOS_CARDS_STATE": str(t / "cards"), "R3I6NOS_SETUP_SOURCE": str(t / "no-setup"),
            "R3I6NOS_RUN": str(t / "run"), "R3I6NOS_CARDS_BIN": "cards-bin", "R3I6NOS_ROMS2": str(t / "roms2"),
        })
        self.cmds = []
        self.failing = {}                 # program name -> stderr text: that command fails
        self.kernel_sees = True        # reread makes the new partition appear
        self.cards_mounts = True       # the cards helper opens the data card
        self.blkid = ""                # what blkid -p says about partition 1
        self.m.run = self.run_
        self.m.reread = self.reread_
        self.m.sleep = lambda s: None
        patch = mock.patch.object(os, "chown", lambda *a: None)   # not root here
        patch.start()
        self.addCleanup(patch.stop)

    def tearDown(self):
        self.tmp.cleanup()

    def run_(self, cmd, timeout=60):
        self.cmds.append(list(cmd))
        prog = cmd[0]
        if prog in self.failing:
            return subprocess.CompletedProcess(cmd, 1, "", self.failing[prog])
        t = self.t
        if prog == "umount":
            keep = [l for l in (t / "proc/mounts").read_text().splitlines() if f" {cmd[1]} " not in l]
            (t / "proc/mounts").write_text("\n".join(keep) + "\n")
        elif prog == "mount":
            (Path(cmd[-1])).mkdir(parents=True, exist_ok=True)
        elif prog == "cards-bin":
            if self.cards_mounts:
                with open(t / "proc/mounts", "a") as f:
                    f.write(f"{t}/dev/mmcblk1p1 /roms2 exfat rw 0 0\n")
                (t / "cards").write_text("BOOT_DISK=mmcblk0\nDATA_DEV=/dev/mmcblk1p1\nDATA_AT=/roms2\n")
            else:
                (t / "cards").write_text("BOOT_DISK=mmcblk0\nDATA_DEV=none\nDATA_AT=none\n")
        elif prog == "blkid" and "-p" in cmd:
            return subprocess.CompletedProcess(cmd, 0, self.blkid, "")
        return subprocess.CompletedProcess(cmd, 0, "", "")

    def show_partition(self, start=None, size=None):
        t = self.t
        mbr = (t / "dev/mmcblk1").read_bytes()[:512]
        start = start if start is not None else int.from_bytes(mbr[454:458], "little")
        size = size if size is not None else int.from_bytes(mbr[458:462], "little")
        p = t / "sys/block/mmcblk1/mmcblk1p1"
        p.mkdir(exist_ok=True)
        (p / "start").write_text(f"{start}\n")
        (p / "size").write_text(f"{size}\n")
        (t / "dev/mmcblk1p1").write_text("")

    def reread_(self, dev):
        self.cmds.append(["BLKRRPART", str(dev)])
        if self.kernel_sees:
            self.show_partition()
        return self.kernel_sees

    def ask(self, text):
        (self.t / "req/request").write_text(text)
        self.assertEqual(self.m.apply(), 0)
        res = (self.t / "out/result").read_text().splitlines()
        return dict(l.split("=", 1) for l in res if "=" in l and not l.startswith("L\t")), [l[2:] for l in res if l.startswith("L\t")]

    def names(self):
        return [c[0] for c in self.cmds]

    # ---- the format -----------------------------------------------------------------------------------------------------------

    def test_formats_the_data_card_and_opens_it(self):
        head, lines = self.ask("id=a1\nverb=format\nfs=exfat\n")
        self.assertEqual((head["id"], head["ok"], head["reboot"]), ("a1", "1", "0"), lines)
        self.assertIn("formatted (exFAT", lines[0])
        prog = dict(l.split("=", 1) for l in (self.t / "out/progress").read_text().splitlines())
        self.assertEqual((prog["id"], prog["step"], prog["steps"]), ("a1", "6", "6"), "the menu's bar reaches the last step (rule 18)")
        self.assertEqual(prog["doing"], "Opening the card")
        self.assertIn("/roms2", lines[0])
        self.assertFalse((self.t / "req/request").exists(), "the request is deleted first")
        # unmounted before it was written to, and mounted again by the cards helper at the end
        self.assertLess(self.cmds.index(["umount", "/roms2"]), self.cmds.index(["BLKRRPART", str(self.t / "dev/mmcblk1")]))
        self.assertLess(self.names().index("mkfs.exfat"), self.names().index("cards-bin"))
        mk = next(c for c in self.cmds if c[0] == "mkfs.exfat")
        self.assertEqual(mk[-1], str(self.t / "dev/mmcblk1p1"))
        self.assertIn("R3I6NOSDATA", mk)
        # the table: one partition, type 07, starting 4 MiB in and running to the end; the old data is gone
        disk = (self.t / "dev/mmcblk1").read_bytes()
        self.assertEqual(disk[510:512], b"\x55\xaa")
        e = disk[446:462]
        self.assertEqual((e[4], int.from_bytes(e[8:12], "little"), int.from_bytes(e[12:16], "little")), (0x07, 8192, SECTORS - 8192))
        self.assertEqual(disk[462:510], bytes(48))
        self.assertEqual(disk[512:8192 * 512], bytes(8192 * 512 - 512), "the old start of the card is cleared")
        self.assertEqual(disk[-1024 * 1024:], bytes(1024 * 1024), "and its end (a GPT backup)")
        self.assertEqual(disk[440:444].hex(), (self.t / "dev/mmcblk1").read_bytes()[440:444].hex())

    def test_a_new_card_gets_an_id_the_app_data_engine_reads(self):
        import json
        self.ask("id=m1\nverb=format\nfs=exfat\n")
        mark = self.t / "run/format/mnt/.r3i6nos-card"
        data = json.loads(mark.read_text())
        self.assertRegex(data["id"], r"^[0-9a-f]{16}$")
        self.assertRegex(data["made"], r"^\d{4}-\d\d-\d\d$")
        self.assertEqual(mark.stat().st_mode & 0o777, 0o644)
        # the engine takes it as the card's own id, and does not make another
        loader = importlib.machinery.SourceFileLoader("r3i6nos_appdata", str(SRC.with_name("r3i6nos-appdata")))
        spec = importlib.util.spec_from_loader("r3i6nos_appdata", loader)
        eng = importlib.util.module_from_spec(spec)
        loader.exec_module(eng)
        self.assertEqual(eng.card_id(mark.parent), data["id"])
        self.assertEqual(eng.card_id(mark.parent, create=True), data["id"])

    def test_two_formats_make_two_ids(self):
        import json
        ids = []
        for n in ("a", "b"):
            self.ask(f"id={n}\nverb=format\nfs=exfat\n")
            ids.append(json.loads((self.t / "run/format/mnt/.r3i6nos-card").read_text())["id"])
            (self.t / "run/format/mnt/.r3i6nos-card").unlink()
        self.assertNotEqual(*ids)

    def test_the_marker_is_made_by_the_restart_path_too(self):
        self.kernel_sees = False
        self.ask("id=r1\nverb=format\nfs=exfat\n")
        self.assertFalse((self.t / "run/format/mnt/.r3i6nos-card").exists(), "nothing is formatted yet, so no id yet")
        self.show_partition()                           # after the restart the kernel shows it
        self.m.finish()
        self.assertTrue((self.t / "run/format/mnt/.r3i6nos-card").is_file())

    def test_the_label_fits_every_filesystem(self):
        self.assertLessEqual(len(self.m.LABEL), 11, "exfatprogs 1.2.0 refuses a longer label: 'input string is too long' (found on the unit 2026-11-16)")

    def test_ext4_uses_its_own_tool_and_type(self):
        head, lines = self.ask("id=a2\nverb=format\nfs=ext4\n")
        self.assertEqual(head["ok"], "1", lines)
        mk = next(c for c in self.cmds if c[0] == "mkfs.ext4")
        self.assertIn("root_owner=1000:1000", mk)
        self.assertEqual((self.t / "dev/mmcblk1").read_bytes()[446 + 4], 0x83)

    def test_the_system_card_is_never_touched(self):
        self.ask("id=a3\nverb=format\nfs=exfat\n")
        self.assertEqual((self.t / "dev/mmcblk0").read_bytes(), self.boot_before)
        for c in self.cmds:
            self.assertNotIn(str(self.t / "dev/mmcblk0"), " ".join(c))
            self.assertNotIn("/roms", [a for a in c if a == "/roms"])

    def test_refuses_when_it_cannot_tell_the_boot_card(self):
        self.m.ROOT_DEV = "1:1"
        head, lines = self.ask("id=b1\nverb=format\nfs=exfat\n")
        self.assertEqual(head["ok"], "0")
        self.assertIn("system card", lines[0])
        self.assertEqual((self.t / "dev/mmcblk1").read_bytes()[:3], b"OLD")
        self.assertNotIn("umount", self.names())

    def test_refuses_with_no_second_card(self):
        (self.t / "sys/block/mmcblk1/size").write_text("0\n")
        head, lines = self.ask("id=b2\nverb=format\nfs=exfat\n")
        self.assertEqual(head["ok"], "0")
        self.assertIn("no card in the second slot", lines[0])

    def test_the_setup_source_is_not_a_data_card(self):
        (self.t / "no-setup").write_text("mmcblk1\n")
        head, lines = self.ask("id=b3\nverb=format\nfs=exfat\n")
        self.assertEqual(head["ok"], "0")
        self.assertEqual((self.t / "dev/mmcblk1").read_bytes()[:3], b"OLD")

    def test_a_busy_card_is_left_untouched(self):
        self.failing["umount"] = "target is busy"
        head, lines = self.ask("id=b4\nverb=format\nfs=exfat\n")
        self.assertEqual(head["ok"], "0")
        self.assertIn("in use", lines[0])
        self.assertIn("Nothing was changed", lines[0])
        self.assertEqual((self.t / "dev/mmcblk1").read_bytes()[:3], b"OLD", "nothing was written")
        self.assertNotIn("BLKRRPART", self.names())

    def test_unknown_filesystem_and_verb_are_refused(self):
        for text in ("id=c1\nverb=format\nfs=ntfs\n", "id=c2\nverb=format\n", "id=c3\nverb=wipe\nfs=exfat\n", "id=c4\nfs=exfat\n"):
            head, lines = self.ask(text)
            self.assertEqual(head["ok"], "0", text)
        self.assertEqual((self.t / "dev/mmcblk1").read_bytes()[:3], b"OLD")
        self.assertNotIn("umount", self.names())

    def test_a_failed_mkfs_says_so(self):
        self.failing["mkfs.exfat"] = "no space"
        head, lines = self.ask("id=d1\nverb=format\nfs=exfat\n")
        self.assertEqual(head["ok"], "0")
        self.assertIn("Could not format", lines[0])

    def test_too_small_a_card_is_refused(self):
        (self.t / "sys/block/mmcblk1/size").write_text("100\n")
        head, lines = self.ask("id=d2\nverb=format\nfs=exfat\n")
        self.assertEqual(head["ok"], "0")
        self.assertIn("too small", lines[0])

    # ---- when a restart is needed ----------------------------------------------------------------------------------------------

    def test_a_restart_is_asked_for_when_the_kernel_keeps_the_old_table(self):
        self.kernel_sees = False
        head, lines = self.ask("id=e1\nverb=format\nfs=exfat\n")
        self.assertEqual((head["ok"], head["reboot"]), ("1", "1"), lines)
        self.assertNotIn("mkfs.exfat", self.names(), "no format against a table the kernel does not have")
        marker = (self.t / "state/format-pending").read_text()
        self.assertIn("disk=mmcblk1", marker)
        self.assertIn("fs=exfat", marker)

    def test_an_old_partition_one_is_not_taken_for_the_new_one(self):
        self.kernel_sees = False
        self.show_partition(start=2048, size=1000)      # the kernel still shows the card's old partition 1
        head, lines = self.ask("id=e2\nverb=format\nfs=exfat\n")
        self.assertEqual(head["reboot"], "1")
        self.assertNotIn("mkfs.exfat", self.names())

    def test_finish_formats_the_new_partition_after_the_restart(self):
        self.kernel_sees = False
        self.ask("id=e3\nverb=format\nfs=exfat\n")
        self.cmds.clear()
        self.show_partition()                           # after the restart the kernel shows it
        self.assertEqual(self.m.finish(), 0)
        self.assertIn("mkfs.exfat", self.names())
        self.assertFalse((self.t / "state/format-pending").exists(), "one try only")

    def test_finish_does_nothing_without_a_marker(self):
        self.assertEqual(self.m.finish(), 0)
        self.assertEqual(self.cmds, [])

    def test_finish_leaves_a_card_that_has_changed(self):
        self.kernel_sees = False
        self.ask("id=e4\nverb=format\nfs=exfat\n")
        self.cmds.clear()
        self.show_partition()
        with open(self.t / "dev/mmcblk1", "r+b") as f:   # another card, or the table written over since
            f.seek(440)
            f.write(b"\xde\xad\xbe\xef")
        self.m.finish()
        self.assertNotIn("mkfs.exfat", self.names())
        self.assertFalse((self.t / "state/format-pending").exists())

    def test_finish_never_formats_a_partition_that_has_a_filesystem(self):
        self.kernel_sees = False
        self.ask("id=e5\nverb=format\nfs=exfat\n")
        self.cmds.clear()
        self.show_partition()
        self.blkid = "exfat"
        self.m.finish()
        self.assertNotIn("mkfs.exfat", self.names())

    def test_finish_refuses_a_marker_for_the_boot_card(self):
        (self.t / "state/format-pending").write_text("disk=mmcblk0\nsig=00000000\nfs=exfat\n")
        self.m.finish()
        self.assertNotIn("mkfs.exfat", self.names())

    def test_formatted_but_not_opened_asks_for_a_restart(self):
        self.cards_mounts = False
        head, lines = self.ask("id=f1\nverb=format\nfs=exfat\n")
        self.assertEqual((head["ok"], head["reboot"]), ("1", "1"), lines)
        self.assertIn("could not be opened", lines[0])

    # ---- the request is hostile ------------------------------------------------------------------------------------------------

    def test_a_linked_request_is_refused_and_removed(self):
        os.symlink("/etc/passwd", self.t / "req/request")
        self.assertEqual(self.m.read_request(), "")
        self.m.apply()
        self.assertFalse(os.path.lexists(self.t / "req/request"))
        self.assertEqual((self.t / "dev/mmcblk1").read_bytes()[:3], b"OLD")

    def test_a_big_request_is_refused(self):
        head, lines = self.ask("id=g1\nverb=format\nfs=exfat\n" + "x=" + "y" * 5000 + "\n")
        self.assertEqual(head["ok"], "0")
        self.assertEqual((self.t / "dev/mmcblk1").read_bytes()[:3], b"OLD")

    def test_the_id_is_made_safe(self):
        head, _ = self.ask("id=../../etc/x y\nverb=format\nfs=ntfs\n")
        self.assertEqual(head["id"], "....etcxy")

    def test_no_request_is_a_no_op(self):
        self.assertEqual(self.m.apply(), 0)
        self.assertFalse((self.t / "out/result").exists())


if __name__ == "__main__":
    unittest.main()

"""r3i6nos-appdata (overlay/usr/local/bin/r3i6nos-appdata): where an app's data lives, and the move and merge between the two cards.

Fake cards are temp folders and a cards file; the real copy, verify and delete code runs on them."""
import contextlib
import importlib.machinery
import importlib.util
import io
import os
import shutil
import stat
import tempfile
import unittest
from pathlib import Path
from unittest import mock

BIN = Path(__file__).resolve().parent.parent / "overlay/usr/local/bin"


def load(env):
    with mock.patch.dict(os.environ, env):
        loader = importlib.machinery.SourceFileLoader("r3i6nos_appdata", str(BIN / "r3i6nos-appdata"))
        spec = importlib.util.spec_from_loader("r3i6nos_appdata", loader)
        mod = importlib.util.module_from_spec(spec)
        loader.exec_module(mod)
    return mod


def tree(folder):
    """{relative path: content or None for a folder} of everything under folder."""
    out = {}
    for p in sorted(Path(folder).rglob("*")):
        out[str(p.relative_to(folder))] = None if p.is_dir() else p.read_bytes()
    return out


class Base(unittest.TestCase):
    APP = "Commander"

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.sys_at, self.data_at = self.root / "roms", self.root / "roms2"
        self.sys_at.mkdir()
        self.data_at.mkdir()
        self.canary = self.root / "canary"          # outside every app folder: must never change
        self.canary.mkdir()
        (self.canary / "keep.txt").write_text("not yours")
        (self.sys_at / "Love 11.5").mkdir()
        (self.sys_at / "Love 11.5" / "game.love").write_text("a game")
        (self.data_at / "native_deb").mkdir()
        (self.data_at / "native_deb" / "x.deb").write_text("a package")
        self.cards = self.root / "cards"
        self.supply = self.root / "power"
        self.supply.mkdir()
        self.env = {"R3I6NOS_CARDS_FILE": str(self.cards), "R3I6NOS_APPDATA_STATE": str(self.root / "state"),
                    "R3I6NOS_POWER_SUPPLY": str(self.supply), "R3I6NOS_APPDATA_MIN_FREE_MB": "0"}
        patcher = mock.patch.dict(os.environ, self.env)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.mod = load(self.env)
        self.mod.stop = False
        self.set_cards()

    def tearDown(self):
        self.mod.stop = False
        for p in self.root.rglob("*"):
            if p.is_dir() and not p.is_symlink():
                p.chmod(0o755)
        self.tmp.cleanup()

    def set_cards(self, system=True, data=True, data_fs="exfat", system_fs="exfat"):
        lines = [f"SYSTEM_AT={self.sys_at if system else 'none'}", f"SYSTEM_FS={system_fs if system else 'none'}",
                 f"DATA_AT={self.data_at if data else 'none'}", f"DATA_FS={data_fs if data else 'none'}"]
        self.cards.write_text("\n".join(lines) + "\n")

    @property
    def S(self):
        return self.sys_at / self.APP

    @property
    def D(self):
        return self.data_at / self.APP

    def put(self, folder, rel, text, mtime=None):
        p = folder / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
        if mtime:
            os.utime(p, (mtime, mtime))
        return p

    def run_tool(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = self.mod.main(list(args))
        return rc, out.getvalue(), err.getvalue()

    def state(self, app=None):
        rc, out, err = self.run_tool("state", app or self.APP)
        self.assertEqual(rc, 0, err)
        return dict(line.split("=", 1) for line in out.splitlines())

    def assertCanary(self):
        self.assertEqual((self.canary / "keep.txt").read_text(), "not yours")
        self.assertEqual((self.sys_at / "Love 11.5" / "game.love").read_text(), "a game", "another folder on the system card")
        self.assertEqual((self.data_at / "native_deb" / "x.deb").read_text(), "a package", "another folder on the data card")


class StateTable(Base):
    def test_no_card_at_all(self):
        self.set_cards(system=False, data=False)
        st = self.state()
        self.assertEqual((st["card"], st["action"], st["use"]), ("none", "none", ""))
        self.assertEqual(self.run_tool("path", self.APP)[0], 1)

    def test_no_valid_data_card_uses_the_system_card_without_a_question(self):
        self.set_cards(data=False)
        self.put(self.S, "save.dat", "x")
        st = self.state()
        self.assertEqual((st["card"], st["action"], st["use"]), ("none", "none", str(self.S)))

    def test_nothing_anywhere_with_a_valid_card_starts_on_the_data_card(self):
        st = self.state()
        self.assertEqual((st["card"], st["action"], st["use"]), ("valid", "none", str(self.D)))
        self.assertEqual(st["s_files"], "0")

    def test_data_only_on_the_system_card_offers_a_move_and_keeps_using_it_until_then(self):
        self.put(self.S, "a.txt", "aaa")
        self.put(self.S, "sub/b.txt", "bbbb")
        st = self.state()
        self.assertEqual(st["action"], "offer-move")
        self.assertEqual(st["use"], str(self.S))
        self.assertEqual((st["s_files"], st["s_bytes"]), ("2", "7"))

    def test_data_only_on_the_data_card_is_used_without_a_question(self):
        self.put(self.D, "a.txt", "aaa")
        st = self.state()
        self.assertEqual((st["action"], st["use"]), ("none", str(self.D)))

    def test_data_on_both_offers_a_merge_and_uses_the_data_card_meanwhile(self):
        self.put(self.S, "same.txt", "s", 1000)
        self.put(self.D, "same.txt", "d", 2000)
        self.put(self.S, "only-s.txt", "s")
        st = self.state()
        self.assertEqual((st["action"], st["use"], st["conflicts"]), ("offer-merge", str(self.D), "1"))

    def test_the_tools_own_files_are_not_data(self):
        self.put(self.D, ".r3i6nos-merge.json", "{}")
        self.put(self.D, ".merge-conflicts/20260101-000000/x", "old")
        self.put(self.D, "half.bin.r3i6nos-part", "partial")
        self.put(self.S, "a.txt", "a")
        self.assertEqual(self.state()["d_files"], "0")

    def test_a_read_only_data_card_is_not_valid(self):
        self.put(self.S, "a.txt", "a")
        self.data_at.chmod(0o555)
        if os.access(self.data_at, os.W_OK):
            self.skipTest("running as root: a folder's mode cannot make it read-only")
        st = self.state()
        self.assertEqual((st["card"], st["action"], st["use"]), ("readonly", "none", str(self.S)))
        self.assertIn("cannot be written", st["reason"])

    def test_an_unsupported_filesystem_is_not_valid(self):
        self.set_cards(data_fs="ntfs")
        self.assertEqual(self.state()["card"], "unsupported")

    def test_a_card_with_no_room_is_not_valid(self):
        with mock.patch.dict(os.environ, {"R3I6NOS_APPDATA_MIN_FREE_MB": str(10 ** 9)}):
            st = self.state()
        self.assertEqual(st["card"], "full")

    def test_a_data_card_alone_at_roms_is_the_data_card(self):
        self.cards.write_text(f"SYSTEM_AT=none\nSYSTEM_FS=none\nDATA_AT={self.sys_at}\nDATA_FS=exfat\n")
        st = self.state()
        self.assertEqual((st["card"], st["use"]), ("valid", str(self.sys_at / self.APP)))

    def test_needs_the_data_card_when_the_card_is_out_after_using_it(self):
        self.assertEqual(self.run_tool("used", self.APP, "data")[0], 0)
        self.set_cards(data=False)
        st = self.state()
        self.assertEqual((st["action"], st["use"]), ("needs-card", str(self.S)))
        self.run_tool("used", self.APP, "system")                       # the person chose the system card for now
        self.assertEqual(self.state()["action"], "none")

    def test_keep_on_the_system_card_stops_the_question_until_the_card_changes(self):
        self.put(self.S, "a.txt", "a")
        self.assertEqual(self.run_tool("choice", self.APP, "keep")[0], 0)
        first_card = (self.data_at / ".r3i6nos-card").read_text()
        st = self.state()
        self.assertEqual((st["action"], st["use"]), ("none", str(self.S)))
        (self.data_at / ".r3i6nos-card").write_text('{"id": "another card"}')    # a different card
        self.assertEqual(self.state()["action"], "offer-move")
        (self.data_at / ".r3i6nos-card").write_text(first_card)
        self.assertEqual(self.state()["action"], "none", "the first card again: still kept")
        self.run_tool("choice", self.APP, "reset")
        self.assertEqual(self.state()["action"], "offer-move")

    def test_keep_for_a_merge_asks_again_when_either_side_changes(self):
        self.put(self.S, "a.txt", "a")
        self.put(self.D, "b.txt", "b")
        self.run_tool("choice", self.APP, "keep")
        self.assertEqual(self.state()["action"], "none")
        self.put(self.S, "new.txt", "changed since")
        self.assertEqual(self.state()["action"], "offer-merge")

    def test_an_interrupted_operation_is_offered_for_resume(self):
        self.put(self.S, "a.txt", "a")
        self.put(self.D, ".r3i6nos-merge.json", '{"from": "%s", "to": "%s", "mode": "move", "started": "20260101-000000"}' % (self.S, self.D))
        self.assertEqual(self.state()["action"], "resume")

    def test_path_prints_the_folder_and_creates_it_only_when_asked(self):
        rc, out, _err = self.run_tool("path", self.APP)
        self.assertEqual((rc, out.strip()), (0, str(self.D)))
        self.assertFalse(self.D.exists())
        self.run_tool("path", self.APP, "--create")
        self.assertTrue(self.D.is_dir())

    def test_path_says_when_both_cards_have_data_and_never_moves_anything(self):
        self.put(self.S, "a.txt", "a")
        self.put(self.D, "b.txt", "b")
        rc, out, err = self.run_tool("path", self.APP)
        self.assertEqual(out.strip(), str(self.D))
        self.assertIn("both cards", err)
        self.assertTrue((self.S / "a.txt").exists())

    def test_app_names_that_could_leave_the_folder_are_refused(self):
        for bad in ("../x", ".hidden", "a/b", "", "x..y", "x/../y", "a" * 65, "trailing "):
            self.assertEqual(self.run_tool("state", bad)[0], 1, repr(bad))
        for good in ("FM1-NTT", "IO_Checker", "Rec demo", "KlamANSI", "Love 11.5"):
            self.assertEqual(self.run_tool("state", good)[0], 0, good)


class Move(Base):
    def test_move_copies_verifies_and_removes_the_original(self):
        self.put(self.S, "a.txt", "aaa", 1_700_000_000)
        self.put(self.S, "deep/er/b.bin", "bbbb", 1_600_000_000)
        (self.S / "empty").mkdir()
        before = tree(self.S)
        rc, out, err = self.run_tool("move", self.APP)
        self.assertEqual(rc, 0, out)
        self.assertIn("DONE copied=2 deleted=2 conflicts=0 skipped=0", out)
        self.assertIn("PROGRESS 2/2", out)
        self.assertEqual(tree(self.D), before, "the same tree, empty folder included")
        self.assertEqual(int(os.stat(self.D / "a.txt").st_mtime), 1_700_000_000, "modification times are kept")
        self.assertFalse(self.S.exists(), "an emptied folder is removed")
        self.assertFalse((self.D / ".r3i6nos-merge.json").exists())
        self.assertTrue((self.data_at / ".r3i6nos-card").is_file(), "the card is marked on the first move")
        self.assertCanary()
        self.assertEqual(self.state()["action"], "none")

    def test_after_a_move_the_app_counts_as_using_the_data_card(self):
        self.put(self.S, "a.txt", "a")
        self.run_tool("move", self.APP)
        self.set_cards(data=False)
        self.assertEqual(self.state()["action"], "needs-card")

    def test_move_refuses_when_the_data_card_already_has_data(self):
        self.put(self.S, "a.txt", "a")
        self.put(self.D, "b.txt", "b")
        rc, out, _err = self.run_tool("move", self.APP)
        self.assertEqual(rc, 1)
        self.assertIn("merge them instead", out)
        self.assertTrue((self.S / "a.txt").exists())

    def test_links_and_special_files_stay_and_are_counted(self):
        self.put(self.S, "a.txt", "a")
        os.symlink("a.txt", self.S / "link")
        os.mkfifo(self.S / "pipe")
        rc, out, _err = self.run_tool("move", self.APP)
        self.assertEqual(rc, 0)
        self.assertIn("skipped=2", out)
        self.assertIn("Left in place: link (a link or special file)", out)
        self.assertTrue((self.S / "link").is_symlink() and (self.S / "pipe").exists(), "never copied, never deleted")
        self.assertEqual((self.D / "a.txt").read_text(), "a")
        self.assertFalse((self.D / "link").exists())

    def test_a_name_the_target_cannot_hold_stays_where_it_is(self):
        self.put(self.S, "ok.txt", "ok")
        self.put(self.S, 'what?.txt', "q")
        self.put(self.S, 'dir"quote/inner.txt', "q")
        rc, out, _err = self.run_tool("move", self.APP)
        self.assertIn("what?.txt (a character the target filesystem cannot hold)", out)
        self.assertEqual((self.S / "what?.txt").read_text(), "q")
        self.assertEqual((self.S / 'dir"quote/inner.txt').read_text(), "q")
        self.assertEqual((self.D / "ok.txt").read_text(), "ok")

    def test_names_that_differ_only_by_case_do_not_go_to_a_case_blind_target(self):
        self.put(self.S, "Save.dat", "one")
        self.put(self.S, "save.dat", "two")
        if len(list(self.S.iterdir())) < 2:
            self.skipTest("this filesystem ignores case, so the two names cannot both exist (it runs on Linux)")
        rc, out, _err = self.run_tool("move", self.APP)
        self.assertIn("skipped=2", out)
        self.assertEqual({p.name for p in self.S.iterdir()}, {"Save.dat", "save.dat"}, "both stay: neither is overwritten or lost")
        self.set_cards(data_fs="ext4")
        self.assertIn("skipped=0", self.run_tool("move", self.APP)[1])
        self.assertEqual({p.name: p.read_text() for p in self.D.iterdir()}, {"Save.dat": "one", "save.dat": "two"})

    def test_the_same_name_goes_to_ext4_unchanged(self):
        self.set_cards(data_fs="ext4")
        self.put(self.S, "what?.txt", "q")
        rc, out, _err = self.run_tool("move", self.APP)
        self.assertIn("skipped=0", out)
        self.assertEqual((self.D / "what?.txt").read_text(), "q")

    def test_name_rules(self):
        np = self.mod.name_problem
        self.assertIsNone(np("fine.txt", "exfat", []))
        self.assertIn("255", np("x" * 256, "ext4", []))
        self.assertIn("case", np("Save.DAT", "exfat", ["save.dat"]))
        self.assertIsNone(np("Save.DAT", "ext4", ["save.dat"]))
        self.assertIn("space or a dot", np("trailing.", "vfat", []))
        self.assertIn("character", np("a:b", "exfat", []))

    def test_too_big_for_the_target_touches_nothing(self):
        self.put(self.S, "a.bin", "x" * 1000)
        before = tree(self.S)
        with mock.patch.object(self.mod.shutil, "disk_usage", return_value=mock.Mock(free=1010, total=10 ** 6)):
            rc, out, _err = self.run_tool("move", self.APP)      # 1000 bytes needs 1050 with the 5% spare
        self.assertEqual(rc, 1)
        self.assertIn("to spare", out)
        self.assertEqual(tree(self.S), before)
        self.assertFalse((self.D / "a.bin").exists())

    def test_a_low_battery_refuses_but_power_connected_is_fine(self):
        self.put(self.S, "a.txt", "a")
        bat = self.supply / "battery"
        bat.mkdir()
        (bat / "capacity").write_text("12\n")
        (bat / "status").write_text("Discharging\n")
        rc, out, _err = self.run_tool("move", self.APP)
        self.assertEqual(rc, 1)
        self.assertIn("plug in power", out)
        self.assertTrue((self.S / "a.txt").exists())
        (bat / "status").write_text("Charging\n")
        self.assertEqual(self.run_tool("move", self.APP)[0], 0)

    def test_a_failed_verification_leaves_the_original_and_says_so(self):
        self.put(self.S, "good.txt", "good")
        self.put(self.S, "bad.txt", "bad")
        real = self.mod.digest
        with mock.patch.object(self.mod, "digest", side_effect=lambda p: "wrong" if "bad.txt" in str(p) and str(p).endswith(self.mod.PART) else real(p)):
            rc, out, _err = self.run_tool("move", self.APP)
        self.assertIn("skipped=1", out)
        self.assertEqual((self.S / "bad.txt").read_text(), "bad", "the original is never deleted unverified")
        self.assertFalse((self.D / "bad.txt").exists())
        self.assertFalse(list(self.D.rglob("*" + self.mod.PART)), "no partial copy is left")
        self.assertEqual((self.D / "good.txt").read_text(), "good")

    def test_an_app_folder_that_is_a_link_is_refused_and_nothing_follows_it(self):
        self.S.symlink_to(self.canary)
        rc, out, _err = self.run_tool("move", self.APP)
        self.assertEqual(rc, 1)
        self.assertIn("not a plain folder", out)
        self.assertCanary()
        self.assertEqual(sorted(p.name for p in self.canary.iterdir()), ["keep.txt"])

    def test_without_both_cards_it_says_so(self):
        self.set_cards(data=False)
        self.assertEqual(self.run_tool("move", self.APP)[0], 1)
        self.set_cards(system=False)
        self.assertEqual(self.run_tool("move", self.APP)[0], 1)


class Merge(Base):
    def test_identical_files_are_deduplicated_and_only_in_source_ones_move(self):
        self.put(self.S, "same.txt", "same", 1000)
        self.put(self.D, "same.txt", "same", 1000)
        self.put(self.S, "only-s/x.txt", "s")
        self.put(self.D, "only-d.txt", "d")
        rc, out, _err = self.run_tool("merge", self.APP, "data")
        self.assertEqual(rc, 0, out)
        self.assertIn("conflicts=0", out)
        self.assertEqual(tree(self.D).keys() - {"only-s"}, {"same.txt", "only-s/x.txt", "only-d.txt"})
        self.assertFalse(self.S.exists())
        self.assertCanary()

    def test_the_newer_file_wins_and_the_older_is_kept_in_conflicts(self):
        self.put(self.S, "a.txt", "from system, newer", 2000)
        self.put(self.D, "a.txt", "from data, older", 1000)
        self.put(self.S, "b.txt", "from system, older", 1000)
        self.put(self.D, "b.txt", "from data, newer", 2000)
        rc, out, _err = self.run_tool("merge", self.APP, "data")
        self.assertIn("conflicts=2", out)
        self.assertEqual((self.D / "a.txt").read_text(), "from system, newer")
        self.assertEqual((self.D / "b.txt").read_text(), "from data, newer")
        kept = {p.name: p.read_text() for p in (self.D / ".merge-conflicts").rglob("*.txt")}
        self.assertEqual(kept, {"a.txt": "from data, older", "b.txt": "from system, older"}, "nothing is lost")
        self.assertFalse(self.S.exists())

    def test_equal_times_keep_the_targets_file(self):
        self.put(self.S, "a.txt", "system", 1000)
        self.put(self.D, "a.txt", "data", 1000)
        self.run_tool("merge", self.APP, "data")
        self.assertEqual((self.D / "a.txt").read_text(), "data")
        self.assertEqual(next((self.D / ".merge-conflicts").rglob("a.txt")).read_text(), "system")

    def test_merge_into_the_system_card_goes_the_other_way(self):
        self.put(self.S, "a.txt", "s", 1000)
        self.put(self.D, "a.txt", "d", 2000)
        self.put(self.D, "only-d.txt", "d")
        rc, out, _err = self.run_tool("merge", self.APP, "system")
        self.assertEqual(rc, 0, out)
        self.assertEqual((self.S / "a.txt").read_text(), "d")
        self.assertEqual((self.S / "only-d.txt").read_text(), "d")
        self.assertFalse(self.D.exists())
        self.assertEqual(next((self.S / ".merge-conflicts").rglob("a.txt")).read_text(), "s")
        self.set_cards(data=False)
        self.assertEqual(self.state()["action"], "none", "the app now lives on the system card")

    def test_merge_needs_a_target(self):
        self.assertEqual(self.run_tool("merge", self.APP)[0], 1)
        self.assertEqual(self.run_tool("merge", self.APP, "sideways")[0], 1)


class Interruption(Base):
    """Stop the operation after every possible number of files, run it again, and compare with an uninterrupted run."""

    def build(self):
        for i in range(5):
            self.put(self.S, f"d{i % 2}/f{i}.txt", f"system {i}", 1000 + i)
        self.put(self.S, "clash.txt", "system clash", 5000)
        self.put(self.D, "clash.txt", "data clash", 4000)
        self.put(self.D, "d0/f0.txt", "system 0", 1000)           # identical to a system file
        self.put(self.D, "mine.txt", "data only")

    def expected(self):
        out = {}
        for p in self.D.rglob("*"):
            if p.is_file():
                out[str(p.relative_to(self.D))] = p.read_text()
        return out

    def test_every_interruption_point_finishes_to_the_same_result(self):
        self.build()
        self.run_tool("merge", self.APP, "data")
        want = self.expected()
        self.assertEqual(len([k for k in want if k.startswith(".merge-conflicts")]), 1)
        self.tearDown()
        interrupted = 0
        for stop_after in range(0, 8):
            self.setUp()
            self.build()
            real, count = self.mod.copy_verified, [0]

            def stopper(*a, **kw):
                if count[0] >= stop_after:
                    self.mod.stop = True
                count[0] += 1
                return real(*a, **kw)
            with mock.patch.object(self.mod, "copy_verified", side_effect=stopper):
                rc, out, _err = self.run_tool("merge", self.APP, "data")
            if stop_after < 7:
                self.assertTrue((self.D / ".r3i6nos-merge.json").exists() or rc == 0, f"stop {stop_after}")
            if rc != 0:
                interrupted += 1
                self.assertEqual(self.state()["action"], "resume", f"stop {stop_after}")
                self.mod.stop = False
                rc, out, _err = self.run_tool("merge", self.APP, "data")
            self.assertEqual(rc, 0, f"stop {stop_after}: {out}")
            got = {k: v for k, v in self.expected().items() if not k.startswith(".merge-conflicts")}
            self.assertEqual(got, {k: v for k, v in want.items() if not k.startswith(".merge-conflicts")}, f"stop {stop_after}")
            lost = [v for k, v in self.expected().items() if k.startswith(".merge-conflicts")]
            self.assertEqual(lost, ["data clash"], f"stop {stop_after}: the older file is kept exactly once")
            self.assertFalse(self.S.exists(), f"stop {stop_after}")
            self.assertCanary()
            self.tearDown()
        self.setUp()
        self.assertGreaterEqual(interrupted, 4, "the test really did interrupt the operation")

    def test_a_stale_partial_copy_is_cleaned_and_never_taken_for_a_file(self):
        self.put(self.S, "a.bin", "real content")
        self.put(self.D, "a.bin" + self.mod.PART, "half of a")
        self.put(self.D, ".r3i6nos-merge.json", '{"from": "%s", "to": "%s", "mode": "move", "started": "20260101-000000"}' % (self.S, self.D))
        rc, out, _err = self.run_tool("move", self.APP)
        self.assertEqual(rc, 0, out)
        self.assertEqual((self.D / "a.bin").read_text(), "real content")
        self.assertFalse(list(self.D.rglob("*" + self.mod.PART)))

    def test_sigterm_handler_sets_the_flag(self):
        self.mod.on_signal(15, None)
        self.assertTrue(self.mod.stop)


class Cli(Base):
    def test_usage(self):
        self.assertEqual(self.run_tool()[0], 2)
        self.assertEqual(self.run_tool("bogus")[0], 2)
        self.assertEqual(self.run_tool("--help")[0], 0)
        self.assertEqual(self.run_tool("choice", self.APP, "sometimes")[0], 1)
        self.assertEqual(self.run_tool("used", self.APP, "elsewhere")[0], 1)

    def test_failures_of_move_are_in_the_fail_grammar_on_stdout(self):
        rc, out, err = self.run_tool("move", "../escape")
        self.assertEqual(rc, 1)
        self.assertTrue(out.startswith("FAIL "), out)
        self.assertEqual(err, "")


if __name__ == "__main__":
    unittest.main()

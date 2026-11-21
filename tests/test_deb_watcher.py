import importlib.util
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

SRC = Path(__file__).resolve().parent.parent / "overlay/usr/local/bin/deb_watcher.py"


def load(env):
    with mock.patch.dict(os.environ, env):
        spec = importlib.util.spec_from_file_location("deb_watcher", SRC)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
    return mod


def fake_extract_tool(deb, dest):
    """A command-line tool: two executables and the marker file."""
    (dest / "usr/bin").mkdir(parents=True)
    for n in (deb.stem, deb.stem + "-extra"):
        exe = dest / "usr/bin" / n
        exe.write_text("#!/bin/sh\n")
        exe.chmod(0o755)
    (dest / "usr/share/r3i6nos").mkdir(parents=True)
    (dest / "usr/share/r3i6nos/tool").write_text("")
    return True


def fake_extract(deb, dest):
    """Stand-in for dpkg-deb -x: drops a binary and a bundled lib."""
    (dest / "usr/bin").mkdir(parents=True)
    exe = dest / "usr/bin" / deb.stem
    exe.write_text("#!/bin/sh\n")
    exe.chmod(0o755)
    (dest / "usr/lib/aarch64-linux-gnu").mkdir(parents=True)
    return True


class DebWatcher(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.debs = root / "native_deb"
        self.debs.mkdir()
        self.mod = load({
            "R3I6NOS_DEB_DIR": str(self.debs),
            "R3I6NOS_PKG_RESERVE_MB": "0",
            "R3I6NOS_PKG_RESERVE_PCT": "0",
            "R3I6NOS_DEB_DIR2": str(root / "native_deb2"),
            "R3I6NOS_DEFAULT_DEB_DIR": str(root / "defaults"),
            "R3I6NOS_APP_ROOT": str(root / "apps"),
            "R3I6NOS_LAUNCHER_DIR": str(root / "launchers"),
            "R3I6NOS_TOOLS_DIR": str(root / "bin"),
        })
        self.mod.extract = fake_extract
        self.apps, self.launchers, self.bin = root / "apps", root / "launchers", root / "bin"

    def tearDown(self):
        self.tmp.cleanup()

    def test_installs_and_writes_launcher(self):
        (self.debs / "tuxracer.deb").write_bytes(b"x")
        self.assertEqual(self.mod.process_debs(), 1)
        script = (self.launchers / "tuxracer.sh").read_text()
        self.assertIn("usr/bin/tuxracer", script)
        self.assertIn("aarch64-linux-gnu", script)
        self.assertTrue(os.access(self.launchers / "tuxracer.sh", os.X_OK))

    def test_unchanged_deb_is_not_extracted_again(self):
        (self.debs / "a.deb").write_bytes(b"x")
        self.mod.process_debs()
        with mock.patch.object(self.mod, "extract") as ex:
            self.mod.process_debs()
        ex.assert_not_called()

    def test_replaced_deb_is_extracted_fresh(self):
        deb = self.debs / "a.deb"
        deb.write_bytes(b"x")
        self.mod.process_debs()
        (self.apps / "a/stale").write_text("old")
        deb.write_bytes(b"longer")
        self.mod.process_debs()
        self.assertFalse((self.apps / "a/stale").exists())

    def test_removed_deb_removes_app_and_launcher(self):
        deb = self.debs / "a.deb"
        deb.write_bytes(b"x")
        self.mod.process_debs()
        deb.unlink()
        self.mod.process_debs()
        self.assertFalse((self.apps / "a").exists())
        self.assertFalse((self.launchers / "a.sh").exists())

    def test_progress_one_step_for_each_package_to_extract(self):
        """Rule 18: the progress file and callback say step n of the packages to extract, reach steps, and the file is gone after."""
        pf = Path(self.tmp.name) / "progress"
        self.mod.PROGRESS_FILE = pf
        seen, files = [], []
        self.mod.PROGRESS = lambda step, steps, doing: (seen.append((step, steps, doing)), files.append(pf.read_text() if pf.exists() else ""))
        for n in ("a", "b"):
            (self.debs / f"{n}.deb").write_bytes(b"x")
        self.mod.process_debs()
        self.assertEqual([(s, t) for s, t, _ in seen], [(0, 2), (1, 2), (2, 2)])
        self.assertEqual(seen[0][2], "Installing a")
        self.assertIn("step=1\nsteps=2\ndoing=Installing b\n", files[1])
        self.assertFalse(pf.exists(), "the file goes when the work ends")
        seen.clear()
        self.mod.process_debs()                      # nothing new: nothing to say
        self.assertEqual(seen, [])
        self.assertFalse(pf.exists())

    def test_progress_survives_an_unwritable_file_and_a_refused_package(self):
        self.mod.PROGRESS_FILE = Path(self.tmp.name) / "no-such-folder" / "progress"
        (self.debs / "a.deb").write_bytes(b"x")
        self.mod.extract = lambda deb, dest: False
        self.assertEqual(self.mod.process_debs(), 0)    # the extraction failed; progress still ended cleanly and nothing raised

    def test_failed_extraction_leaves_nothing(self):
        (self.debs / "bad.deb").write_bytes(b"x")
        self.mod.extract = lambda deb, dest: False
        self.assertEqual(self.mod.process_debs(), 0)
        self.assertFalse((self.apps / "bad").exists())

    def test_missing_deb_folder_is_fine(self):
        self.debs.rmdir()
        self.assertEqual(self.mod.process_debs(), 0)

    def test_built_in_deb_installs_without_a_card(self):
        defaults = Path(self.tmp.name) / "defaults"
        defaults.mkdir()
        (defaults / "checker.deb").write_bytes(b"x")
        self.debs.rmdir()                                  # no games card at all
        self.assertEqual(self.mod.process_debs(), 1)
        self.assertTrue((self.launchers / "checker.sh").exists())

    def test_card_deb_replaces_a_built_in_of_the_same_name(self):
        defaults = Path(self.tmp.name) / "defaults"
        defaults.mkdir()
        (defaults / "checker.deb").write_bytes(b"builtin")
        (self.debs / "checker.deb").write_bytes(b"the card's, longer")
        self.assertEqual(self.mod.process_debs(), 1)
        self.assertEqual((self.apps / "checker/.r3i6nos-deb").read_text().split(":")[0], str(len(b"the card's, longer")))

    def test_unmounted_card_never_uninstalls_apps(self):
        (self.debs / "game.deb").write_bytes(b"x")
        self.mod.process_debs()
        (self.debs / "game.deb").unlink()
        self.debs.rmdir()                                  # card gone, a built-in is still there
        defaults = Path(self.tmp.name) / "defaults"
        defaults.mkdir()
        (defaults / "checker.deb").write_bytes(b"x")
        self.mod.process_debs()
        self.assertTrue((self.apps / "game").exists())
        self.assertTrue((self.launchers / "game.sh").exists())

    def test_a_tool_has_no_menu_launcher_and_its_commands_are_on_the_tools_path(self):
        self.mod.extract = fake_extract_tool
        (self.debs / "composer.deb").write_bytes(b"x")
        self.assertEqual(self.mod.process_debs(), 1)
        self.assertFalse((self.launchers / "composer.sh").exists(), "a CLI is not a menu entry")
        for name in ("composer", "composer-extra"):
            w = self.bin / name
            self.assertTrue(os.access(w, os.X_OK), name)
            self.assertIn(f'exec "{self.apps}/composer/usr/bin/{name}" "$@"', w.read_text())
            self.assertIn("# r3i6nos-tool composer", w.read_text())

    def test_a_tool_never_replaces_another_packages_command(self):
        self.bin.mkdir()
        (self.bin / "composer").write_text("#!/bin/sh\n# r3i6nos-tool other\n")
        (self.bin / "composer-extra").write_text("#!/bin/sh\necho mine\n")  # not a tool wrapper at all
        self.mod.extract = fake_extract_tool
        (self.debs / "composer.deb").write_bytes(b"x")
        self.mod.process_debs()
        self.assertIn("r3i6nos-tool other", (self.bin / "composer").read_text())
        self.assertEqual((self.bin / "composer-extra").read_text(), "#!/bin/sh\necho mine\n")

    def test_removing_a_tools_deb_removes_its_commands_only(self):
        self.mod.extract = fake_extract_tool
        (self.debs / "composer.deb").write_bytes(b"x")
        (self.debs / "painter.deb").write_bytes(b"x")
        self.mod.process_debs()
        self.assertEqual(sorted(p.name for p in self.bin.iterdir()), ["composer", "composer-extra", "painter", "painter-extra"])
        (self.debs / "composer.deb").unlink()
        self.mod.process_debs()
        self.assertEqual(sorted(p.name for p in self.bin.iterdir()), ["painter", "painter-extra"])
        self.assertFalse((self.apps / "composer").exists())

    def test_a_missing_card_uninstalls_no_tool(self):
        self.mod.extract = fake_extract_tool
        (self.debs / "composer.deb").write_bytes(b"x")
        self.mod.process_debs()
        (self.debs / "composer.deb").unlink()
        self.debs.rmdir()  # the card is not mounted
        self.mod.process_debs()
        self.assertTrue((self.bin / "composer").exists())
        self.assertTrue((self.apps / "composer").exists())

    def test_apps_and_tools_side_by_side(self):
        (self.debs / "tuxracer.deb").write_bytes(b"x")
        (self.debs / "composer.deb").write_bytes(b"x")
        self.mod.extract = lambda deb, dest: (fake_extract_tool if deb.stem == "composer" else fake_extract)(deb, dest)
        self.assertEqual(self.mod.process_debs(), 2)
        self.assertTrue((self.launchers / "tuxracer.sh").exists())
        self.assertFalse((self.launchers / "composer.sh").exists())
        self.assertTrue((self.bin / "composer").exists())
        self.assertFalse((self.bin / "tuxracer").exists())


class Versions(unittest.TestCase):
    def setUp(self):
        self.mod = load({})

    def test_debian_version_order(self):
        v = self.mod.vercmp
        for lo, hi in [("1.0", "1.1"), ("1.9", "1.10"), ("1.0~rc1", "1.0"), ("1.0", "1.0+b1"), ("1.0-1", "1.0-2"),
                       ("2.0", "1:0.9"), ("2.0", "2.0.1"), ("1.0a", "1.0b"), ("0.9.3", "0.10")]:
            self.assertEqual(v(lo, hi), -1, (lo, hi))
            self.assertEqual(v(hi, lo), 1, (lo, hi))
        self.assertEqual(v("1.2-3", "1.2-3"), 0)


class BothCards(unittest.TestCase):
    """The system card's and the data card's native_deb, the built-ins, versions, provenance and size."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.sys, self.data, self.builtin = root / "sys", root / "data", root / "defaults"
        for d in (self.sys, self.data, self.builtin):
            d.mkdir()
        self.mod = load({
            "R3I6NOS_DEB_DIR": str(self.sys), "R3I6NOS_DEB_DIR2": str(self.data), "R3I6NOS_DEFAULT_DEB_DIR": str(self.builtin),
            "R3I6NOS_APP_ROOT": str(root / "apps"), "R3I6NOS_LAUNCHER_DIR": str(root / "launchers"), "R3I6NOS_TOOLS_DIR": str(root / "bin"),
            "R3I6NOS_PKG_RESERVE_MB": "0", "R3I6NOS_PKG_RESERVE_PCT": "0",
        })
        self.mod.extract = fake_extract
        self.versions = {}
        self.mod.control = lambda deb, field: self.versions.get((deb.parent.name, deb.name), {}).get(field)
        self.apps, self.launchers = root / "apps", root / "launchers"

    def tearDown(self):
        self.tmp.cleanup()

    def put(self, folder, name, version=None, data=b"x"):
        (folder / f"{name}.deb").write_bytes(data)
        if version:
            self.versions[(folder.name, f"{name}.deb")] = {"Version": version}

    def test_a_deb_on_the_data_card_installs(self):
        self.put(self.data, "tool-a")
        self.assertEqual(self.mod.process_debs(), 1)
        self.assertTrue((self.launchers / "tool-a.sh").exists())
        self.assertEqual((self.apps / "tool-a/.r3i6nos-src").read_text(), str(self.data))

    def test_the_higher_version_wins_wherever_it_is(self):
        self.put(self.builtin, "commander", "2.0", b"newer built-in")
        self.put(self.data, "commander", "1.5", b"older on the card")
        self.mod.process_debs()
        self.assertEqual((self.apps / "commander/.r3i6nos-src").read_text(), str(self.builtin))   # a reflash is not shadowed by a stale card copy
        self.put(self.sys, "commander", "2.1", b"newest, on the system card")
        self.mod.process_debs()
        self.assertEqual((self.apps / "commander/.r3i6nos-src").read_text(), str(self.sys))

    def test_equal_versions_or_unknown_ones_prefer_the_later_folder(self):
        self.put(self.sys, "a", "1.0", b"s")
        self.put(self.data, "a", "1.0", b"d")
        self.mod.process_debs()
        self.assertEqual((self.apps / "a/.r3i6nos-src").read_text(), str(self.data))

    def test_a_package_is_removed_only_when_its_own_folder_is_there(self):
        self.put(self.data, "x")
        self.put(self.sys, "y")
        self.mod.process_debs()
        (self.data / "x.deb").unlink()
        self.data.rmdir()                       # the data card is out
        (self.sys / "y.deb").unlink()           # y was deleted from the system card, which is there
        self.mod.process_debs()
        self.assertTrue((self.apps / "x").exists(), "x came from a card that is out")
        self.assertFalse((self.apps / "y").exists())

    def test_a_package_moved_between_cards_is_not_reinstalled(self):
        self.put(self.sys, "m", "1.0", b"same")
        self.mod.process_debs()
        with mock.patch.object(self.mod, "extract") as ex:
            (self.sys / "m.deb").rename(self.data / "m.deb")
            os.utime(self.data / "m.deb", (os.stat(self.apps / "m/.r3i6nos-deb").st_mtime,) * 2)
            self.mod.process_debs()
        self.assertEqual((self.apps / "m/.r3i6nos-src").read_text(), str(self.data))

    def test_too_big_is_refused_before_it_is_extracted_and_the_old_one_stays(self):
        self.put(self.data, "big", "1.0", b"small")
        self.mod.process_debs()
        self.put(self.data, "big", "2.0", b"much bigger deb")
        self.versions[("data", "big.deb")]["Installed-Size"] = str(10 * 1024 * 1024)       # 10 GB, in KB
        free = 500 * 1024 * 1024
        with mock.patch.object(self.mod.shutil, "disk_usage", return_value=mock.Mock(free=free, total=2 * free)), \
                mock.patch.object(self.mod, "extract") as ex:
            self.assertEqual(self.mod.process_debs(), 0)
        ex.assert_not_called()
        status, why = self.mod.LAST["big"]
        self.assertEqual(status, "refused")
        self.assertIn("squashfs", why)
        self.assertTrue((self.launchers / "big.sh").exists(), "the installed version is untouched")
        self.assertFalse((self.apps / "big.new").exists())

    def test_the_reserve_is_kept_back(self):
        self.mod.MIN_RESERVE = 200 * 1024 * 1024
        self.put(self.data, "snug")
        self.versions[("data", "snug.deb")] = {"Installed-Size": str(100 * 1024)}           # 100 MB
        with mock.patch.object(self.mod.shutil, "disk_usage", return_value=mock.Mock(free=250 * 1024 * 1024, total=2300 * 1024 * 1024)):
            self.assertEqual(self.mod.process_debs(), 0)
        with mock.patch.object(self.mod.shutil, "disk_usage", return_value=mock.Mock(free=350 * 1024 * 1024, total=2300 * 1024 * 1024)):
            self.assertEqual(self.mod.process_debs(), 1)

    def test_an_interrupted_extraction_leaves_no_half_package(self):
        self.put(self.data, "p")
        (self.apps).mkdir(exist_ok=True)
        (self.apps / "p.new").mkdir()
        (self.apps / "p.new/junk").write_text("half")
        self.mod.process_debs()
        self.assertFalse((self.apps / "p.new").exists())
        self.assertTrue((self.apps / "p").is_dir())

    def test_installed_packages_lists_version_source_and_size(self):
        self.put(self.data, "lister", "3.1-2")
        self.mod.control = lambda deb, field: "3.1-2" if field == "Version" else None
        self.mod.process_debs()
        (name, version, src, size), = self.mod.installed_packages()
        self.assertEqual((name, version, src), ("lister", "3.1-2", str(self.data)))
        self.assertGreater(size, 0)


if __name__ == "__main__":
    unittest.main()

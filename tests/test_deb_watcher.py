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


if __name__ == "__main__":
    unittest.main()

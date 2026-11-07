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
        })
        self.mod.extract = fake_extract
        self.apps, self.launchers = root / "apps", root / "launchers"

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


if __name__ == "__main__":
    unittest.main()

"""r3i6nos-pkg (overlay/usr/local/bin/r3i6nos-pkg): install, list and remove .deb packages on a running unit, with fake cards.

Most tests fake dpkg-deb (the control fields and the file list); one class builds a real .deb and extracts it when dpkg-deb is installed
(CI and the Debian container have it, a Mac does not)."""
import contextlib
import importlib.machinery
import importlib.util
import io
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

BIN = Path(__file__).resolve().parent.parent / "overlay/usr/local/bin"


def load(env):
    """Load the tool under env; the caller keeps env patched for the test (the tool reads some of it when it runs)."""
    with mock.patch.dict(os.environ, env):
        loader = importlib.machinery.SourceFileLoader("r3i6nos_pkg", str(BIN / "r3i6nos-pkg"))
        spec = importlib.util.spec_from_loader("r3i6nos_pkg", loader)
        mod = importlib.util.module_from_spec(spec)
        loader.exec_module(mod)
    return mod


def fake_extract(deb, dest):
    (dest / "usr/bin").mkdir(parents=True)
    exe = dest / "usr/bin" / deb.name.split(".deb")[0]
    exe.write_text("#!/bin/sh\n")
    exe.chmod(0o755)
    return True


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.root = root
        self.sys_at, self.data_at = root / "roms", root / "roms2"
        self.sys_deb, self.data_deb = self.sys_at / "native_deb", self.data_at / "native_deb"
        self.builtin = root / "defaults"
        for d in (self.sys_at, self.data_at, self.builtin):
            d.mkdir()
        self.inbox = root / "inbox"
        self.inbox.mkdir()
        self.cards_file = root / "cards"
        self.set_cards(system=True, data=True)
        self.flag = root / "run/pkg-changed"
        env = {
            "R3I6NOS_DEB_DIR": str(self.sys_deb), "R3I6NOS_DEB_DIR2": str(self.data_deb), "R3I6NOS_DEFAULT_DEB_DIR": str(self.builtin),
            "R3I6NOS_APP_ROOT": str(root / "apps"), "R3I6NOS_LAUNCHER_DIR": str(root / "launchers"), "R3I6NOS_TOOLS_DIR": str(root / "bin"),
            "R3I6NOS_PKG_RESERVE_MB": "0", "R3I6NOS_PKG_RESERVE_PCT": "0",
            "R3I6NOS_CARDS_FILE": str(self.cards_file), "R3I6NOS_INBOX": str(self.inbox), "R3I6NOS_PKG_FLAG": str(self.flag),
        }
        patcher = mock.patch.dict(os.environ, env)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.mod = load(env)
        self.W = self.mod.W
        self.W.extract = fake_extract
        self.control = {}                      # file name -> {field: value}; a file not listed reads as a good arm64 package
        self.files = {}                        # file name -> the paths it installs
        self.W.control = self.fake_control
        self.mod.file_list = lambda deb: self.files.get(self.base(deb), ["usr/bin/app"])
        self.apps, self.launchers = root / "apps", root / "launchers"

    def tearDown(self):
        self.tmp.cleanup()

    @staticmethod
    def base(deb):
        n = Path(deb).name
        return n[:-5] if n.endswith(".part") else n

    def fake_control(self, deb, field):
        # a .part is checked under the name it will have, a built-in or card deb under its own
        fields = {"Package": Path(self.base(deb)).stem, "Version": "1.0", "Architecture": "arm64"}
        fields.update(self.control.get(self.base(deb), {}))
        return fields.get(field)

    def set_cards(self, system, data):
        lines = [f"SYSTEM_AT={self.sys_at if system else 'none'}", f"DATA_AT={self.data_at if data else 'none'}"]
        self.cards_file.write_text("\n".join(lines) + "\n")

    def make(self, name, body=b"package bytes", where=None):
        p = (where or self.inbox) / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(body)
        return p

    def run_pkg(self, *args, answer=None):
        out, err = io.StringIO(), io.StringIO()
        stdin = mock.Mock()
        stdin.isatty.return_value = answer is not None
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err), mock.patch.object(self.mod.sys, "stdin", stdin), \
                mock.patch("builtins.input", return_value=answer or ""):
            rc = self.mod.main(list(args))
        return rc, out.getvalue(), err.getvalue()


class Install(Base):
    def test_goes_to_the_data_card_when_it_is_in(self):
        src = self.make("tool.deb")
        rc, out, err = self.run_pkg("install", "--yes", str(src))
        self.assertEqual(rc, 0, err)
        self.assertTrue((self.data_deb / "tool.deb").is_file())
        self.assertFalse((self.sys_deb / "tool.deb").exists())
        self.assertIn("installed tool 1.0", out)
        self.assertIn("data card", out)
        self.assertTrue((self.launchers / "tool.sh").exists(), "applied at once")
        self.assertTrue(src.exists(), "a file given by name is not deleted")
        self.assertEqual(self.flag.read_text().strip(), "changed")

    def test_goes_to_the_system_card_when_there_is_no_data_card(self):
        self.set_cards(system=True, data=False)
        rc, out, err = self.run_pkg("install", "--yes", str(self.make("tool.deb")))
        self.assertEqual(rc, 0, err)
        self.assertTrue((self.sys_deb / "tool.deb").is_file())
        self.assertIn("system card", out)

    def test_a_card_that_is_out_is_never_written(self):
        self.set_cards(system=False, data=False)
        rc, out, err = self.run_pkg("install", "--yes", str(self.make("tool.deb")))
        self.assertEqual(rc, 1)
        self.assertIn("no card is mounted", err)
        self.assertFalse(self.data_deb.exists() or self.sys_deb.exists())

    def test_card_asked_for_is_the_one_used(self):
        rc, _out, err = self.run_pkg("install", "--yes", "--card", "system", str(self.make("tool.deb")))
        self.assertEqual(rc, 0, err)
        self.assertTrue((self.sys_deb / "tool.deb").is_file())
        rc, _out, err = self.run_pkg("install", "--yes", "--card", "bogus", str(self.make("t2.deb")))
        self.assertEqual(rc, 2)

    def test_a_package_already_on_the_other_card_is_replaced_there(self):
        self.make("tool.deb", b"old", self.sys_deb)
        rc, _out, err = self.run_pkg("install", "--yes", str(self.make("tool.deb", b"new and longer")))
        self.assertEqual(rc, 0, err)
        self.assertEqual((self.sys_deb / "tool.deb").read_bytes(), b"new and longer")
        self.assertFalse((self.data_deb / "tool.deb").exists(), "one copy")

    def test_the_old_deb_is_kept_as_prev_and_comes_back_when_the_new_one_fails(self):
        self.run_pkg("install", "--yes", str(self.make("tool.deb", b"version one")))
        self.assertFalse((self.data_deb / "tool.deb.prev").exists())
        self.run_pkg("install", "--yes", str(self.make("tool.deb", b"version two!")))
        self.assertEqual((self.data_deb / "tool.deb.prev").read_bytes(), b"version one")
        self.W.extract = lambda deb, dest: False
        rc, _out, err = self.run_pkg("install", "--yes", str(self.make("tool.deb", b"version three, broken")))
        self.assertEqual(rc, 1)
        self.assertIn("REFUSED tool.deb", err)
        self.assertEqual((self.data_deb / "tool.deb").read_bytes(), b"version two!", "the installed one is back")

    def test_refuses_what_is_not_a_package_for_this_unit(self):
        cases = {
            "junk.deb": ({"Package": ""}, "not a Debian package"),
            "x86.deb": ({"Architecture": "amd64"}, "arm64"),
            ".hidden.deb": ({}, "usable package file name"),
            "has space.deb": ({}, "usable package file name"),
        }
        for name, (fields, want) in cases.items():
            self.control[name] = fields
            rc, _out, err = self.run_pkg("install", "--yes", str(self.make(name)))
            self.assertEqual(rc, 1, name)
            self.assertIn(want, err, name)
        self.assertEqual(list(self.data_deb.glob("*")), [], "nothing was put on the card, not even a .part")

    def test_refuses_a_package_carrying_its_own_nttplay(self):
        self.files["player.deb"] = ["usr/bin/player", "usr/bin/nttplay"]
        rc, _out, err = self.run_pkg("install", "--yes", str(self.make("player.deb")))
        self.assertEqual(rc, 1)
        self.assertIn("nttplay", err)
        self.assertIn("rule 15", err)
        self.assertEqual(list(self.data_deb.glob("*")), [])

    def test_refuses_a_non_deb_and_a_link(self):
        rc, _out, err = self.run_pkg("install", "--yes", str(self.make("readme.txt")))
        self.assertEqual(rc, 1)
        real = self.make("real.deb")
        link = self.inbox / "link.deb"
        link.symlink_to(real)
        rc, _out, err = self.run_pkg("install", "--yes", str(link))
        self.assertEqual(rc, 1)
        self.assertEqual(list(self.data_deb.glob("*")), [], "a link is not followed")

    def test_refuses_a_package_too_big_for_the_root_before_putting_it_on_the_card(self):
        self.control["big.deb"] = {"Installed-Size": str(5 * 1024 * 1024)}             # 5 GB
        with mock.patch.object(self.W.shutil, "disk_usage", return_value=mock.Mock(free=400 * 1024 ** 2, total=2300 * 1024 ** 2)):
            rc, _out, err = self.run_pkg("install", "--yes", str(self.make("big.deb")))
        self.assertEqual(rc, 1)
        self.assertIn("Too big for the system card's root", err)
        self.assertIn("squashfs", err)
        self.assertEqual(list(self.data_deb.glob("*")), [])

    def test_refuses_when_the_card_has_no_room(self):
        real = shutil.disk_usage
        with mock.patch.object(self.mod.shutil, "disk_usage", side_effect=lambda p: mock.Mock(free=10, total=100) if str(self.data_at) in str(p) else real(p)):
            rc, _out, err = self.run_pkg("install", "--yes", str(self.make("tool.deb", b"x" * 2000)))
        self.assertEqual(rc, 1)
        self.assertIn("data card has", err)
        self.assertEqual(list(self.data_deb.glob("*")), [])

    def test_a_newer_version_from_elsewhere_is_not_shadowed_silently(self):
        self.make("tool.deb", b"built-in", self.builtin)
        self.control["tool.deb"] = {"Version": "2.0"}                                   # both read as 2.0 ...
        self.W.control = lambda deb, field: ({"Package": "tool", "Architecture": "arm64",
                                              "Version": "1.0" if self.base(deb) == "tool.deb" and deb.parent != self.builtin else "2.0"}).get(field)
        rc, _out, err = self.run_pkg("install", "--yes", str(self.make("tool.deb", b"older, from the inbox")))
        self.assertEqual(rc, 1)
        self.assertIn("newer tool", err)
        self.assertFalse((self.data_deb / "tool.deb").exists(), "the older one is not left on the card")

    def test_asks_first_and_no_means_no(self):
        rc, _out, err = self.run_pkg("install", str(self.make("tool.deb")), answer="n")
        self.assertEqual(rc, 1)
        self.assertIn("you said no", err)
        self.assertEqual(list(self.data_deb.glob("*")), [])
        rc, out, _err = self.run_pkg("install", str(self.make("tool.deb")), answer="y")
        self.assertEqual(rc, 0)
        self.assertTrue((self.data_deb / "tool.deb").is_file())

    def test_the_question_names_the_package_and_its_sha256(self):
        asked = []
        with mock.patch.object(self.mod, "confirm", side_effect=lambda q, yes: asked.append(q) or True):
            self.run_pkg("install", str(self.make("tool.deb", b"abc")))
        import hashlib
        self.assertIn("tool 1.0", asked[0])
        self.assertIn(hashlib.sha256(b"abc").hexdigest()[:12], asked[0])

    def test_without_a_terminal_it_will_not_guess(self):
        rc, _out, err = self.run_pkg("install", str(self.make("tool.deb")))
        self.assertEqual(rc, 1)
        self.assertIn("--yes", err)
        self.assertEqual(list(self.data_deb.glob("*")), [])


class Inbox(Base):
    def test_inbox_installs_everything_and_empties_itself(self):
        self.make("a.deb")
        self.make("b.deb")
        rc, out, err = self.run_pkg("install", "--inbox", "--yes")
        self.assertEqual(rc, 0, err)
        self.assertEqual(sorted(p.name for p in self.data_deb.glob("*.deb")), ["a.deb", "b.deb"])
        self.assertEqual(list(self.inbox.glob("*")), [])

    def test_a_refused_file_stays_in_the_inbox_and_the_rest_install(self):
        self.make("a.deb")
        self.control["bad.deb"] = {"Architecture": "amd64"}
        self.make("bad.deb")
        rc, _out, err = self.run_pkg("install", "--inbox", "--yes")
        self.assertEqual(rc, 1)
        self.assertTrue((self.data_deb / "a.deb").is_file())
        self.assertEqual([p.name for p in self.inbox.glob("*")], ["bad.deb"])

    def test_an_empty_inbox_is_a_usage_error(self):
        rc, _out, err = self.run_pkg("install", "--inbox", "--yes")
        self.assertEqual(rc, 2)
        self.assertIn("empty", err)


class RemoveAndList(Base):
    def test_remove_takes_the_app_away_and_keeps_its_data(self):
        self.run_pkg("install", "--yes", str(self.make("tool.deb", b"one")))
        self.run_pkg("install", "--yes", str(self.make("tool.deb", b"two!")))
        data = self.data_at / "tool"
        data.mkdir()
        (data / "save.dat").write_text("precious")
        rc, out, err = self.run_pkg("remove", "--yes", "tool")
        self.assertEqual(rc, 0, err)
        self.assertEqual(list(self.data_deb.glob("tool.*")), [], "the .deb and its .prev are both gone")
        self.assertFalse((self.apps / "tool").exists())
        self.assertFalse((self.launchers / "tool.sh").exists())
        self.assertEqual((data / "save.dat").read_text(), "precious")

    def test_a_built_in_cannot_be_removed(self):
        self.make("checker.deb", where=self.builtin)
        rc, _out, err = self.run_pkg("remove", "--yes", "checker")
        self.assertEqual(rc, 1)
        self.assertIn("built into the image", err)
        self.assertTrue((self.builtin / "checker.deb").exists())

    def test_removing_a_package_on_a_card_that_is_out_does_nothing(self):
        self.run_pkg("install", "--yes", str(self.make("tool.deb")))
        self.set_cards(system=True, data=False)
        rc, _out, err = self.run_pkg("remove", "--yes", "tool")
        self.assertEqual(rc, 1)
        self.assertTrue((self.data_deb / "tool.deb").exists())

    def test_remove_asks(self):
        self.run_pkg("install", "--yes", str(self.make("tool.deb")))
        rc, out, _err = self.run_pkg("remove", "tool", answer="n")
        self.assertEqual(rc, 1)
        self.assertTrue((self.data_deb / "tool.deb").exists())

    def test_list_shows_version_card_and_size(self):
        self.run_pkg("install", "--yes", str(self.make("tool.deb")))
        self.make("checker.deb", where=self.builtin)
        self.W.process_debs()
        rc, out, _err = self.run_pkg("list")
        self.assertEqual(rc, 0)
        self.assertRegex(out, r"tool\t1\.0\tdata card\t")
        self.assertRegex(out, r"checker\t.*\tbuilt in\t")

    def test_apply_reports_what_it_refused(self):
        self.control["big.deb"] = {"Installed-Size": str(5 * 1024 * 1024)}
        self.make("big.deb", where=self.data_deb)
        with mock.patch.object(self.W.shutil, "disk_usage", return_value=mock.Mock(free=400 * 1024 ** 2, total=2300 * 1024 ** 2)):
            rc, _out, err = self.run_pkg("apply")
        self.assertEqual(rc, 1)
        self.assertIn("refused big", err)


class CardsFile(Base):
    def test_a_data_card_alone_at_roms_counts_as_the_data_card(self):
        self.cards_file.write_text(f"SYSTEM_AT=none\nDATA_AT={self.sys_at}\n")
        got = self.mod.cards()
        self.assertEqual(got, {"data": self.sys_deb, "system": None})
        rc, out, err = self.run_pkg("install", "--yes", str(self.make("tool.deb")))
        self.assertIn("data card", out)

    def test_no_cards_file_goes_by_the_folders(self):
        self.cards_file.unlink()
        got = self.mod.cards()
        self.assertEqual(got["data"], self.data_deb)
        self.assertEqual(got["system"], self.sys_deb)


@unittest.skipUnless(shutil.which("dpkg-deb"), "needs dpkg-deb (Debian, Ubuntu)")
class RealDeb(unittest.TestCase):
    """A real .deb, really extracted and inspected: the fake control fields above are what dpkg-deb really says."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        r = Path(self.tmp.name)
        self.r = r
        for d in ("roms", "roms2", "inbox"):
            (r / d).mkdir()
        (r / "cards").write_text(f"SYSTEM_AT={r / 'roms'}\nDATA_AT={r / 'roms2'}\n")
        env = {
            "R3I6NOS_DEB_DIR": str(r / "roms/native_deb"), "R3I6NOS_DEB_DIR2": str(r / "roms2/native_deb"),
            "R3I6NOS_DEFAULT_DEB_DIR": str(r / "defaults"), "R3I6NOS_APP_ROOT": str(r / "apps"), "R3I6NOS_LAUNCHER_DIR": str(r / "launchers"),
            "R3I6NOS_TOOLS_DIR": str(r / "bin"), "R3I6NOS_PKG_RESERVE_MB": "0", "R3I6NOS_PKG_RESERVE_PCT": "0",
            "R3I6NOS_CARDS_FILE": str(r / "cards"), "R3I6NOS_INBOX": str(r / "inbox"), "R3I6NOS_PKG_FLAG": str(r / "flag"),
        }
        patcher = mock.patch.dict(os.environ, env)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.mod = load(env)

    def tearDown(self):
        self.tmp.cleanup()

    def build(self, name, version="1.2-3", arch="arm64", extra=()):
        pkg = self.r / f"build-{name}"
        shutil.rmtree(pkg, ignore_errors=True)
        (pkg / "DEBIAN").mkdir(parents=True)
        (pkg / "DEBIAN/control").write_text(f"Package: {name}\nVersion: {version}\nArchitecture: {arch}\nMaintainer: t <t@example.invalid>\nDescription: test\n")
        (pkg / "usr/bin").mkdir(parents=True)
        exe = pkg / "usr/bin" / name
        exe.write_text("#!/bin/sh\necho hi\n")
        exe.chmod(0o755)
        for rel in extra:
            (pkg / rel).parent.mkdir(parents=True, exist_ok=True)
            (pkg / rel).write_text("x")
        out = self.r / "inbox" / f"{name}.deb"
        subprocess.run(["dpkg-deb", "--root-owner-group", "-b", str(pkg), str(out)], check=True, capture_output=True)
        return out

    def run_pkg(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = self.mod.main(list(args))
        return rc, out.getvalue(), err.getvalue()

    def test_install_list_update_remove_with_a_real_deb(self):
        rc, out, err = self.run_pkg("install", "--yes", str(self.build("hello")))
        self.assertEqual(rc, 0, err)
        self.assertIn("installed hello 1.2-3", out)
        self.assertTrue((self.r / "apps/hello/usr/bin/hello").is_file())
        self.assertIn('exec "', (self.r / "launchers/hello.sh").read_text())
        self.assertRegex(self.run_pkg("list")[1], r"hello\t1\.2-3\tdata card")
        rc, out, err = self.run_pkg("install", "--yes", str(self.build("hello", version="1.3-1")))
        self.assertEqual(rc, 0, err)
        self.assertRegex(self.run_pkg("list")[1], r"hello\t1\.3-1\t")
        self.assertTrue((self.r / "roms2/native_deb/hello.deb.prev").is_file())
        self.assertEqual(self.run_pkg("remove", "--yes", "hello")[0], 0)
        self.assertFalse((self.r / "apps/hello").exists())

    def test_a_real_wrong_architecture_and_a_real_nttplay_are_refused(self):
        rc, _out, err = self.run_pkg("install", "--yes", str(self.build("x86tool", arch="amd64")))
        self.assertEqual(rc, 1)
        self.assertIn("amd64", err)
        rc, _out, err = self.run_pkg("install", "--yes", str(self.build("player", extra=["usr/bin/nttplay"])))
        self.assertEqual(rc, 1)
        self.assertIn("nttplay", err)

    def test_a_real_file_that_is_not_a_deb_is_refused(self):
        junk = self.r / "inbox/junk.deb"
        junk.write_bytes(b"this is not an ar archive")
        rc, _out, err = self.run_pkg("install", "--yes", str(junk))
        self.assertEqual(rc, 1)
        self.assertIn("not a Debian package", err)


if __name__ == "__main__":
    unittest.main()

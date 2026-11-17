"""r3i6nos-sysupdate (overlay/usr/local/bin/r3i6nos-sysupdate): the root helper that installs a SIGNED system package and takes it back.

Needs dpkg-deb and OpenSSL 3 (Ed25519 with -rawin): CI and the Debian 12 container have them, a Mac does not (its openssl is LibreSSL), so
there the whole file is skipped. A real key pair is made for each run, real .deb files are built, and the helper runs against a fake root
(R3I6NOS_ROOT) with fake state folders; nothing here is root."""
import hashlib
import importlib.machinery
import importlib.util
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

BIN = Path(__file__).resolve().parent.parent / "overlay/usr/local/bin"


def have_tools():
    if not shutil.which("dpkg-deb") or not shutil.which("openssl"):
        return False
    r = subprocess.run(["openssl", "version"], capture_output=True, text=True)
    return r.returncode == 0 and r.stdout.startswith("OpenSSL 3")


TOOLS = have_tools()


def sh(*cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw)


@unittest.skipUnless(TOOLS, "needs dpkg-deb and OpenSSL 3")
class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = Path(self.tmp.name)
        self.t = t
        self.root, self.state = t / "root", t / "state"
        self.req, self.out, self.pending = t / "req", t / "out", t / "pending"
        for d in (self.root, self.state, self.req, self.out):
            d.mkdir()
        self.write("opt/loveos/menu/main.lua", "old menu\n")
        self.write("opt/loveos/menu/stale.lua", "removed upstream later\n")
        self.write("usr/local/bin/r3i6nos-old", "#!/bin/sh\nold\n", 0o755)
        self.write("etc/r3i6nos-release", "VERSION=0.1.2\nCOMMIT=aaaa\n")
        self.key, self.pub = t / "key.pem", t / "packages.pub"
        sh("openssl", "genpkey", "-algorithm", "ed25519", "-out", str(self.key))
        sh("openssl", "pkey", "-in", str(self.key), "-pubout", "-out", str(self.pub))
        self.env = {
            "R3I6NOS_ROOT": str(self.root), "R3I6NOS_STATE": str(self.state), "R3I6NOS_SYSUPDATE_DIR": str(self.req),
            "R3I6NOS_SYSUPDATE_OUT": str(self.out), "R3I6NOS_PENDING_DIR": str(self.pending), "R3I6NOS_PUBKEY": str(self.pub),
            "R3I6NOS_NO_RESTART": "1", "R3I6NOS_SYSTEMCTL": "true", "R3I6NOS_SYSUPDATE_TEST": "1", "R3I6NOS_USER": "nobody-at-all",
        }
        self.patch = mock.patch.dict(os.environ, self.env)
        self.patch.start()
        loader = importlib.machinery.SourceFileLoader("r3i6nos_sysupdate", str(BIN / "r3i6nos-sysupdate"))
        spec = importlib.util.spec_from_loader("r3i6nos_sysupdate", loader)
        self.m = importlib.util.module_from_spec(spec)
        loader.exec_module(self.m)

    def tearDown(self):
        self.patch.stop()
        self.tmp.cleanup()

    def write(self, rel, text, mode=0o644):
        p = self.root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
        p.chmod(mode)

    def read(self, rel):
        p = self.root / rel
        return p.read_text() if p.is_file() else None

    def snapshot(self):
        return {str(p.relative_to(self.root)): p.read_text() for p in sorted(self.root.rglob("*")) if p.is_file()}

    # ---- making a package, a catalog, a request ----------------------------------------------------------------------------------------------

    def make_deb(self, name="r3i6nos-system", version="2026.11.18.1", files=None, kind="system", arch="all", links=None, modes=None):
        stage = self.t / f"pkg-{name}-{version}"
        shutil.rmtree(stage, ignore_errors=True)
        (stage / "DEBIAN").mkdir(parents=True)
        for rel, text in (files if files is not None else {"opt/loveos/menu/main.lua": "new menu\n", "etc/r3i6nos-release": "VERSION=0.1.3\nCOMMIT=bbbb\n"}).items():
            p = stage / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(text)
            p.chmod((modes or {}).get(rel, 0o644))
        for rel, target in (links or {}).items():
            p = stage / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            os.symlink(target, p)
        ctl = f"Package: {name}\nVersion: {version}\nArchitecture: {arch}\nMaintainer: t <t@example.invalid>\nDescription: test\n"
        if kind:
            ctl += f"X-R3i6nos-Kind: {kind}\n"
        (stage / "DEBIAN/control").write_text(ctl)
        deb = self.t / f"{name}_{version}.deb"
        sh("dpkg-deb", "--root-owner-group", "-b", str(stage), str(deb))
        return deb

    def stage(self, deb, name="r3i6nos-system", entry=None, catalog_kind="system", sign=True, tamper=False):
        data = deb.read_bytes()
        e = {"name": name, "version": sh("dpkg-deb", "-f", str(deb), "Version").stdout.strip(), "kind": catalog_kind, "size": len(data),
             "sha256": hashlib.sha256(data).hexdigest(), "min_system": "0.1.0"}
        e.update(entry or {})
        index = self.req / "index.json"
        index.write_text(json.dumps({"format": 1, "packages": [e]}))
        if sign:
            sh("openssl", "pkeyutl", "-sign", "-inkey", str(self.key), "-rawin", "-in", str(index), "-out", str(self.req / "index.json.sig"))
        else:
            (self.req / "index.json.sig").write_bytes(b"\0" * 64)
        if tamper:
            index.write_text(index.read_text().replace("2026", "2027"))
        shutil.copyfile(deb, self.req / f"{name}.deb")

    def run_apply(self, name="r3i6nos-system", rid="7"):
        (self.req / "request").write_text(f"id={rid}\nname={name}\n")
        self.m.cmd_apply()
        res = {}
        lines = []
        for line in (self.out / "result").read_text().splitlines():
            if line.startswith("L\t"):
                lines.append(line[2:])
            else:
                k, _, v = line.partition("=")
                res[k] = v
        res["lines"] = lines
        return res


class Apply(Base):
    def test_a_signed_system_package_is_installed_and_everything_it_replaced_is_kept(self):
        self.stage(self.make_deb(files={
            "opt/loveos/menu/main.lua": "new menu\n", "opt/loveos/menu/extra.lua": "added\n", "usr/local/bin/r3i6nos-new": "#!/bin/sh\nnew\n",
            "etc/r3i6nos-release": "VERSION=0.1.3\nCOMMIT=bbbb\n"}, modes={"usr/local/bin/r3i6nos-new": 0o755}))
        res = self.run_apply()
        self.assertEqual(res["ok"], "1", res["lines"])
        self.assertEqual(res["id"], "7")
        self.assertEqual(self.read("opt/loveos/menu/main.lua"), "new menu\n")
        self.assertEqual(self.read("opt/loveos/menu/extra.lua"), "added\n")
        self.assertTrue(os.access(self.root / "usr/local/bin/r3i6nos-new", os.X_OK))
        self.assertFalse(os.access(self.root / "opt/loveos/menu/main.lua", os.X_OK))
        self.assertIn("COMMIT=bbbb", self.read("etc/r3i6nos-release"))
        self.assertEqual((self.state / "system-packages").read_text(), "r3i6nos-system 2026.11.18.1\n")
        self.assertTrue((self.pending / "system-pending").is_file(), "the marker the menu removes")
        backup = self.state / "rollback/image-2026.11.18.1/files"
        self.assertEqual((backup / "opt/loveos/menu/main.lua").read_text(), "old menu\n")
        self.assertEqual((backup / "etc/r3i6nos-release").read_text(), "VERSION=0.1.2\nCOMMIT=aaaa\n")
        self.assertFalse(list(self.root.rglob("*.r3i6nos-new")), "no temporary file is left")
        self.assertFalse((self.state / "sysupdate-work").exists(), "the private copy is gone")

    def test_modes_are_cleaned_no_setuid_and_the_owner_is_the_installer(self):
        self.stage(self.make_deb(files={"usr/local/bin/r3i6nos-x": "#!/bin/sh\n", "etc/r3i6nos-release": "VERSION=0.1.3\n"}, modes={"usr/local/bin/r3i6nos-x": 0o4777}))
        self.assertEqual(self.run_apply()["ok"], "1")
        mode = (self.root / "usr/local/bin/r3i6nos-x").stat().st_mode & 0o7777
        self.assertEqual(mode, 0o755, oct(mode))

    def test_what_the_new_version_no_longer_has_is_removed_and_kept_for_the_rollback(self):
        self.stage(self.make_deb(version="2026.11.18.1", files={"opt/loveos/menu/main.lua": "v1\n", "opt/loveos/menu/old-module.lua": "x\n"}))
        self.assertEqual(self.run_apply()["ok"], "1")
        self.stage(self.make_deb(version="2026.11.18.2", files={"opt/loveos/menu/main.lua": "v2\n"}))
        self.assertEqual(self.run_apply()["ok"], "1")
        self.assertIsNone(self.read("opt/loveos/menu/old-module.lua"))
        self.assertEqual(self.read("opt/loveos/menu/stale.lua"), "removed upstream later\n", "a file the helper never installed is not its to remove")
        self.assertEqual(self.m.do_rollback()[:11], "rolled back")
        self.assertEqual(self.read("opt/loveos/menu/old-module.lua"), "x\n")
        self.assertEqual(self.read("opt/loveos/menu/main.lua"), "v1\n")

    def test_the_release_file_is_written_last(self):
        order = []
        real = self.m.put_file
        self.m.put_file = lambda src, target, exe: (order.append(str(target.relative_to(self.root))), real(src, target, exe))[1]
        self.stage(self.make_deb(files={"etc/r3i6nos-release": "VERSION=0.1.3\n", "usr/local/bin/aaa": "a\n", "opt/loveos/theme/z.lua": "z\n"}))
        self.assertEqual(self.run_apply()["ok"], "1")
        self.assertEqual(order[-1], "etc/r3i6nos-release", order)

    def test_the_same_version_again_changes_nothing(self):
        self.stage(self.make_deb())
        self.assertEqual(self.run_apply()["ok"], "1")
        before = self.snapshot()
        res = self.run_apply()
        self.assertEqual(res["ok"], "1")
        self.assertIn("already installed", res["lines"][0])
        self.assertEqual(self.snapshot(), before)


class Refusals(Base):
    def refused(self, why, **stage_args):
        before = self.snapshot()
        res = self.run_apply()
        self.assertEqual(res["ok"], "0", res["lines"])
        self.assertIn(why, " ".join(res["lines"]))
        self.assertEqual(self.snapshot(), before, "nothing was changed")
        self.assertFalse((self.pending / "system-pending").exists())
        self.assertFalse((self.state / "sysupdate-work").exists())

    def test_a_catalog_changed_after_it_was_signed(self):
        self.stage(self.make_deb(), tamper=True)
        self.refused("signature does not match")

    def test_a_catalog_with_a_garbage_signature(self):
        self.stage(self.make_deb(), sign=False)
        self.refused("signature does not match")

    def test_a_catalog_signed_by_another_key(self):
        self.stage(self.make_deb())
        other = self.t / "other.pem"
        sh("openssl", "genpkey", "-algorithm", "ed25519", "-out", str(other))
        sh("openssl", "pkeyutl", "-sign", "-inkey", str(other), "-rawin", "-in", str(self.req / "index.json"), "-out", str(self.req / "index.json.sig"))
        self.refused("signature does not match")

    def test_an_image_with_no_public_key_installs_nothing(self):
        self.stage(self.make_deb())
        self.pub.unlink()
        self.refused("no catalog public key")

    def test_a_package_swapped_after_the_catalog_was_signed(self):
        deb = self.make_deb()
        self.stage(deb)
        evil = self.make_deb(files={"opt/loveos/menu/main.lua": "evil\n"})
        shutil.copyfile(evil, self.req / "r3i6nos-system.deb")
        self.refused("not the one the signed catalog lists")

    def test_a_link_in_the_staging_folder_is_not_followed(self):
        self.stage(self.make_deb())
        (self.req / "r3i6nos-system.deb").unlink()
        os.symlink("/etc/passwd", self.req / "r3i6nos-system.deb")
        self.refused("cannot open")

    def test_an_app_in_the_catalog_is_not_installed_by_root(self):
        self.stage(self.make_deb(), catalog_kind="app")
        self.refused("not a system package")

    def test_a_package_that_does_not_say_it_is_a_system_package(self):
        self.stage(self.make_deb(kind=None))
        self.refused("does not say it is a system package")

    def test_an_unknown_name(self):
        self.stage(self.make_deb())
        (self.req / "request").write_text("id=1\nname=nothing\n")
        self.m.cmd_apply()
        self.assertIn("no package called", (self.out / "result").read_text())

    def test_names_that_could_leave_the_folder(self):
        self.stage(self.make_deb())
        for bad in ("../x", "a/b", "", ".hidden"):
            (self.req / "request").write_text(f"id=1\nname={bad}\n")
            self.m.cmd_apply()
            self.assertIn("ok=0", (self.out / "result").read_text(), bad)

    def test_a_path_outside_the_allowlist_refuses_the_whole_package(self):
        for rel in ("etc/passwd", "usr/bin/evil", "etc/sudoers.d/x", "boot/boot.ini", "opt/native_apps/x", "etc/systemd/evil.conf", "root/.ssh/authorized_keys"):
            self.stage(self.make_deb(files={"opt/loveos/menu/main.lua": "new\n", rel: "x\n"}))
            self.refused("outside what a system package may write")

    def test_dot_dot_and_absolute_names(self):
        # a tar the helper reads can hold names dpkg-deb -b would not make: judge the name function directly
        for bad in ("../etc/passwd", "opt/loveos/menu/../../../etc/passwd", "/etc/passwd", "a\\b", "opt//x"):
            with self.assertRaises(self.m.Refused, msg=bad):
                self.m.clean_name(bad)
        self.assertEqual(self.m.clean_name("./opt/loveos/menu/"), "opt/loveos/menu")

    def test_a_link_in_the_package_refuses_it(self):
        self.stage(self.make_deb(links={"opt/loveos/menu/evil": "/etc/shadow"}))
        self.refused("link or a special file")

    def test_an_older_version_never_goes_back(self):
        self.stage(self.make_deb(version="2026.11.18.5"))
        self.assertEqual(self.run_apply()["ok"], "1")
        self.stage(self.make_deb(version="2026.11.18.4"))
        res = self.run_apply()
        self.assertEqual(res["ok"], "0")
        self.assertIn("only goes forward", res["lines"][0])

    def test_a_package_that_needs_a_newer_base(self):
        self.stage(self.make_deb(), entry={"min_system": "9.0.0"})
        self.refused("needs a base of 9.0.0")

    def test_the_catalog_says_one_version_and_the_package_another(self):
        self.stage(self.make_deb(version="2026.11.18.1"), entry={"version": "2026.11.18.9"})
        self.refused("not what the signed catalog says")

    def test_a_package_over_the_size_limit(self):
        self.stage(self.make_deb(files={"opt/loveos/menu/big.bin": "x" * (3 << 20)}))
        with mock.patch.object(self.m, "MAX_MB", 1):
            self.refused("more than 1 MB")

    def test_a_failure_part_way_puts_everything_back(self):
        self.stage(self.make_deb(files={"opt/loveos/menu/main.lua": "new\n", "usr/local/bin/a": "a\n", "usr/local/bin/b": "b\n", "etc/r3i6nos-release": "VERSION=0.1.3\n"}))
        real, calls = self.m.put_file, []

        def flaky(src, target, exe):
            calls.append(target)
            if len(calls) == 3:
                raise OSError(28, "No space left on device")
            return real(src, target, exe)
        before = self.snapshot()
        with mock.patch.object(self.m, "put_file", flaky):
            res = self.run_apply()
        self.assertEqual(res["ok"], "0")
        self.assertEqual(self.snapshot(), before, "the old files are all back and the added ones gone")
        self.assertFalse((self.pending / "system-pending").exists())
        self.assertFalse((self.state / "system-packages").exists(), "the record was not written")


class BootRollback(Base):
    def update(self):
        self.stage(self.make_deb(files={"opt/loveos/menu/main.lua": "new menu\n", "opt/loveos/menu/added.lua": "a\n", "etc/r3i6nos-release": "VERSION=0.1.3\nCOMMIT=bbbb\n"}))
        self.assertEqual(self.run_apply()["ok"], "1")

    def test_a_menu_that_never_confirms_is_rolled_back_at_the_second_boot(self):
        before_update = self.snapshot()
        self.update()
        self.assertEqual(self.m.cmd_boot(), 0)
        self.assertEqual(self.read("opt/loveos/menu/main.lua"), "new menu\n", "one boot is not enough")
        self.assertTrue((self.pending / "system-pending").exists())
        self.assertEqual(self.m.cmd_boot(), 0)
        self.assertEqual(self.snapshot(), before_update, "every file is as it was before the update")
        self.assertFalse((self.pending / "system-pending").exists())
        self.assertEqual((self.state / "system-packages").read_text(), "")
        self.assertIn("rolled back", (self.out / "result").read_text())

    def test_a_menu_that_confirms_is_not_rolled_back(self):
        self.update()
        self.m.cmd_boot()
        (self.pending / "system-pending").unlink()          # the menu drew its first screen
        self.m.cmd_boot()
        self.m.cmd_boot()
        self.m.cmd_boot()
        self.assertEqual(self.read("opt/loveos/menu/main.lua"), "new menu\n")
        self.assertFalse((self.state / "rollback/boots").exists(), "the count starts again")

    def test_a_marker_with_nothing_to_roll_back_to_is_cleared(self):
        self.pending.mkdir()
        (self.pending / "system-pending").write_text("x\n")
        self.m.cmd_boot()
        self.assertEqual(self.m.cmd_boot(), 1)
        self.assertFalse((self.pending / "system-pending").exists())

    def test_the_manual_rollback(self):
        before_update = self.snapshot()
        self.update()
        self.assertEqual(self.m.cmd_rollback(), 0)
        self.assertEqual(self.snapshot(), before_update)
        self.assertEqual(self.m.cmd_rollback(), 1, "nothing is left to roll back")

    def test_an_update_after_a_rollback_is_possible(self):
        self.update()
        self.m.cmd_rollback()
        self.update()
        self.assertEqual(self.read("opt/loveos/menu/main.lua"), "new menu\n")

    def test_only_the_last_updates_backup_is_kept(self):
        self.stage(self.make_deb(version="2026.11.18.1"))
        self.run_apply()
        self.stage(self.make_deb(version="2026.11.18.2"))
        self.run_apply()
        dirs = sorted(p.name for p in (self.state / "rollback").iterdir() if p.is_dir())
        self.assertEqual(dirs, ["2026.11.18.1-2026.11.18.2"])


class Requests(Base):
    def test_no_request_is_not_an_error(self):
        self.assertEqual(self.m.cmd_apply(), 0)
        self.assertFalse((self.out / "result").exists())

    def test_a_request_that_is_a_link_is_answered_and_removed(self):
        os.symlink("/etc/passwd", self.req / "request")
        self.m.cmd_apply()
        self.assertFalse(os.path.lexists(self.req / "request"))
        self.assertIn("ok=0", (self.out / "result").read_text())

    def test_a_huge_request_is_refused(self):
        (self.req / "request").write_text("name=" + "a" * 10000)
        self.m.cmd_apply()
        self.assertIn("ok=0", (self.out / "result").read_text())

    def test_the_helper_will_not_run_as_an_ordinary_user_outside_tests(self):
        env = dict(os.environ)
        env.pop("R3I6NOS_SYSUPDATE_TEST", None)
        if os.geteuid() == 0:
            self.skipTest("running as root")
        r = subprocess.run([str(BIN / "r3i6nos-sysupdate"), "apply"], env=env, capture_output=True, text=True)
        self.assertEqual(r.returncode, 1)
        self.assertIn("runs as root", r.stderr)

    def test_status_says_what_is_installed(self):
        self.stage(self.make_deb())
        self.run_apply()
        r = subprocess.run([str(BIN / "r3i6nos-sysupdate"), "status"], capture_output=True, text=True)
        self.assertIn("r3i6nos-system 2026.11.18.1", r.stdout)
        self.assertIn("pending: yes", r.stdout)


if __name__ == "__main__":
    unittest.main()

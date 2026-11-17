"""scripts/publish-packages.sh and scripts/catalog.py: the signed catalog. Needs dpkg-deb and OpenSSL 3 (CI and Debian 12 have them; a Mac does not,
so there the file is skipped). The script runs with R3I6NOS_PUBLISH_RUNNER=local; a real key pair is made by its own `keygen`."""
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SCRIPT = REPO / "scripts/publish-packages.sh"


def have_tools():
    if not shutil.which("dpkg-deb") or not shutil.which("openssl"):
        return False
    r = subprocess.run(["openssl", "version"], capture_output=True, text=True)
    return r.returncode == 0 and r.stdout.startswith("OpenSSL 3")


@unittest.skipUnless(have_tools(), "needs dpkg-deb and OpenSSL 3")
class Publish(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = Path(self.tmp.name)
        self.t = t
        # a copy of the pieces the script needs, so keygen writes its public key into the copy and never into this checkout
        self.repo = t / "repo"
        for rel in ("scripts/publish-packages.sh", "scripts/catalog.py", "scripts/ports.conf", "scripts/build-system-package.sh",
                    "scripts/system_version.sh", "scripts/release_file.sh", "VERSION"):
            (self.repo / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(REPO / rel, self.repo / rel)
        self.keydir = t / "keys"
        self.env = dict(os.environ, R3I6NOS_PUBLISH_RUNNER="local", R3I6NOS_KEY=str(self.keydir / "packages.key"), R3I6NOS_NATIVE_BUILD=str(t / "native"),
                        R3I6NOS_PORTS_ROOT=str(t / "ports"), R3I6NOS_CATALOG_OUT=str(t / "catalog"), HOME=str(t / "home"),
                        R3I6NOS_CATALOG_URL="https://example.invalid/dl", SOURCE_DATE_EPOCH="1800000000")
        (t / "native").mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def run_script(self, *args, check=True):
        r = subprocess.run([str(self.repo / "scripts/publish-packages.sh"), *args], env=self.env, capture_output=True, text=True, cwd=self.repo)
        if check:
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        return r

    def deb(self, stem, version="1.0", package=None, arch="arm64", kind=None, extra=None, where=None):
        stage = self.t / f"stage-{stem}-{version}-{arch}"
        shutil.rmtree(stage, ignore_errors=True)
        (stage / "DEBIAN").mkdir(parents=True)
        for rel, text in (extra or {"usr/games/app": "#!/bin/sh\n"}).items():
            p = stage / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(text)
        ctl = f"Package: {package or stem.lower().replace('_', '-')}\nVersion: {version}\nArchitecture: {arch}\nMaintainer: t <t@example.invalid>\nInstalled-Size: 12\nDescription: A test app\n more text\n"
        if kind:
            ctl += f"X-R3i6nos-Kind: {kind}\n"
        (stage / "DEBIAN/control").write_text(ctl)
        out = Path(where or self.t / "native") / f"{stem}.deb"
        subprocess.run(["dpkg-deb", "--root-owner-group", "-b", str(stage), str(out)], check=True, capture_output=True)
        return out

    def keygen(self):
        return self.run_script("keygen")

    def verify(self, out):
        r = subprocess.run(["openssl", "pkeyutl", "-verify", "-pubin", "-inkey", str(self.repo / "overlay/etc/r3i6nos/packages.pub"), "-rawin",
                            "-in", str(out / "index.json"), "-sigfile", str(out / "index.json.sig")], capture_output=True, text=True)
        return r.returncode == 0

    # ---- the key ---------------------------------------------------------------------------------------------------------------------

    def test_keygen_makes_a_private_key_off_the_repo_and_a_public_key_for_the_image(self):
        r = self.keygen()
        key = self.keydir / "packages.key"
        self.assertTrue(key.is_file())
        self.assertEqual(key.stat().st_mode & 0o777, 0o600)
        self.assertTrue((self.repo / "overlay/etc/r3i6nos/packages.pub").read_text().startswith("-----BEGIN PUBLIC KEY-----"))
        self.assertNotIn("PRIVATE", (self.repo / "overlay/etc/r3i6nos/packages.pub").read_text())
        self.assertIn("never commit", r.stdout)
        self.assertFalse(list(self.repo.rglob("*.key")), "no private key in the repo")

    def test_keygen_never_overwrites_a_key(self):
        self.keygen()
        before = (self.keydir / "packages.key").read_bytes()
        r = self.run_script("keygen", check=False)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("not overwriting", r.stderr)
        self.assertEqual((self.keydir / "packages.key").read_bytes(), before)

    def test_build_without_a_key_says_to_run_keygen(self):
        self.deb("Commander")
        r = self.run_script("build", check=False)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("keygen", r.stderr)

    # ---- the catalog -------------------------------------------------------------------------------------------------------------------

    def test_a_catalog_lists_every_package_and_its_signature_verifies_with_the_images_key(self):
        self.keygen()
        cmd = self.deb("Commander", "1.2", kind="app", extra={"usr/games/commander": "x\n"})
        self.deb("IO_Checker_Native", "2.0")
        self.run_script("build")
        out = self.t / "catalog"
        self.assertTrue(self.verify(out))
        cat = json.loads((out / "index.json").read_text())
        self.assertEqual(cat["format"], 1)
        self.assertEqual(cat["generated"], "2027-01-15T08:00:00Z")
        names = [p["name"] for p in cat["packages"]]
        self.assertEqual(names, ["Commander", "IO_Checker_Native"])
        e = cat["packages"][0]
        data = cmd.read_bytes()
        self.assertEqual((e["version"], e["kind"], e["size"], e["sha256"]), ("1.2", "app", len(data), hashlib.sha256(data).hexdigest()))
        self.assertEqual(e["url"], "https://example.invalid/dl/Commander.deb")
        self.assertEqual(e["description"], "A test app")
        self.assertEqual(e["min_system"], "0.1.0")
        self.assertEqual(e["size_installed"], 12 * 1024)
        for n in names:
            self.assertEqual((out / f"{n}.deb").read_bytes(), (self.t / "native" / f"{n}.deb").read_bytes())

    def test_a_changed_index_no_longer_verifies(self):
        self.keygen()
        self.deb("Commander")
        self.run_script("build")
        out = self.t / "catalog"
        (out / "index.json").write_text((out / "index.json").read_text().replace('"1.0"', '"9.9"'))
        self.assertFalse(self.verify(out))

    def test_a_rebuild_is_identical(self):
        self.keygen()
        self.deb("Commander")
        self.run_script("build")
        first = (self.t / "catalog/index.json").read_bytes(), (self.t / "catalog/index.json.sig").read_bytes()
        self.run_script("build")
        self.assertEqual(((self.t / "catalog/index.json").read_bytes(), (self.t / "catalog/index.json.sig").read_bytes()), first)

    def test_a_key_that_is_not_the_images_is_caught_before_anything_is_ready(self):
        self.keygen()
        self.deb("Commander")
        (self.repo / "overlay/etc/r3i6nos/packages.pub").unlink()
        other = self.t / "other"
        other.mkdir()
        env = dict(self.env, R3I6NOS_KEY=str(other / "x.key"))
        subprocess.run([str(self.repo / "scripts/publish-packages.sh"), "keygen"], env=env, capture_output=True, check=True, cwd=self.repo)
        r = self.run_script("build", check=False)               # the private key is the first one, the public key now another's
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("DOES NOT VERIFY", r.stderr)

    def test_files_that_must_not_be_published(self):
        self.keygen()
        for what, kwargs, why in (
            ("armhf", {"arch": "armhf"}, "needs arm64"),
            ("Carries", {"extra": {"usr/bin/nttplay": "x\n", "usr/games/a": "y\n"}}, "nttplay"),
            ("sys", {"kind": "system", "package": "something-else"}, "Package name"),
            ("odd", {"kind": "weird"}, "kind 'weird'"),
        ):
            for f in (self.t / "native").glob("*.deb"):
                f.unlink()
            f = self.deb(what, **kwargs)
            r = self.run_script("build", str(f), check=False)
            self.assertNotEqual(r.returncode, 0, what)
            self.assertIn(why, r.stderr, what)
            self.assertFalse((self.t / "catalog/index.json").exists() and (self.t / "catalog/index.json.sig").exists() and False)

    def test_a_package_name_that_is_not_a_safe_file_name(self):
        self.keygen()
        bad = self.t / "bad name.deb"
        shutil.copy(self.deb("Commander"), bad)
        r = self.run_script("build", str(bad), check=False)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("not a usable package file name", r.stderr)

    def test_two_files_with_one_name(self):
        self.keygen()
        a = self.deb("Commander", where=self.t)
        sub = self.t / "other"
        sub.mkdir()
        b = self.deb("Commander", "2.0", where=sub)
        r = self.run_script("build", str(a), str(b), check=False)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("two files make the name Commander", r.stderr)

    def test_a_port_is_published_only_when_named(self):
        self.keygen()
        self.deb("Commander")
        ports = self.t / "ports/neumetik/r36s/build/pkg"
        ports.mkdir(parents=True)
        self.deb("Neumetik", where=ports)
        self.run_script("build")
        names = [p["name"] for p in json.loads((self.t / "catalog/index.json").read_text())["packages"]]
        self.assertEqual(names, ["Commander"], "a port is the maintainer's own, maybe proprietary: not published by default")
        self.run_script("build", "--ports", "Neumetik")
        names = [p["name"] for p in json.loads((self.t / "catalog/index.json").read_text())["packages"]]
        self.assertEqual(names, ["Commander", "Neumetik"])

    def test_an_unknown_port_is_an_error(self):
        self.keygen()
        self.deb("Commander")
        r = self.run_script("build", "--ports", "Nonesuch", check=False)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("no port called Nonesuch", r.stderr)

    def test_the_upload_command_is_printed_not_run(self):
        self.keygen()
        self.deb("Commander")
        r = self.run_script("build")
        self.assertIn("gh release upload packages", r.stdout)
        self.assertIn("--clobber", r.stdout)


if __name__ == "__main__":
    unittest.main()

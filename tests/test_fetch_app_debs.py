"""scripts/fetch_app_debs.sh and scripts/check_deb.py against a fake `gh` and hand-made .deb files."""
import hashlib
import io
import os
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FAKE_GH = """#!/bin/bash
# fake: gh release download --repo R --pattern A --pattern B --dir D   (copies $FAKE_RELEASES/<R with / as _>/<pattern>)
repo=""; dir=""; pats=()
while [ $# -gt 0 ]; do case "$1" in --repo) repo="$2"; shift 2;; --pattern) pats+=("$2"); shift 2;; --dir) dir="$2"; shift 2;; *) shift;; esac; done
src="$FAKE_RELEASES/${repo//\\//_}"; [ -d "$src" ] || { echo "release not found" >&2; exit 1; }
for p in "${pats[@]}"; do [ -f "$src/$p" ] && cp "$src/$p" "$dir/"; done; exit 0
"""


def tar_xz(files):
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:xz") as t:
        for name, text in files.items():
            b = text.encode()
            info = tarfile.TarInfo(name); info.size = len(b)
            t.addfile(info, io.BytesIO(b))
    return buf.getvalue()


def ar(members):
    out = b"!<arch>\n"
    for name, blob in members:
        out += f"{name:<16}{0:<12}{0:<6}{0:<6}{'100644':<8}{len(blob):<10}`\n".encode() + blob + (b"\n" if len(blob) % 2 else b"")
    return out


def make_deb(package="x", copyright_text="Free to use.", with_copyright=True):
    data = {"./usr/share/doc/%s/copyright" % package: copyright_text} if with_copyright else {"./usr/bin/x": "x"}
    return ar([("debian-binary", b"2.0\n"), ("control.tar.xz", tar_xz({"./control": f"Package: {package}\nVersion: 1\n"})),
               ("data.tar.xz", tar_xz(data))])


class FetchTest(unittest.TestCase):
    def setUp(self):
        self.d = Path(tempfile.mkdtemp(prefix="r3i6nos-fetch."))
        self.rel = self.d / "releases"
        (self.d / "gh").write_text(FAKE_GH); (self.d / "gh").chmod(0o755)
        (self.d / "list.txt").write_text("# comment\n\nacme/one one.deb\nacme/two two.deb\n")
        self.out = self.d / "out"

    def release(self, repo, asset, deb, sha=None):
        r = self.rel / repo.replace("/", "_"); r.mkdir(parents=True, exist_ok=True)
        (r / asset).write_bytes(deb)
        (r / (asset + ".sha256")).write_text(f"{sha or hashlib.sha256(deb).hexdigest()}  {asset}\n")

    def run_fetch(self):
        env = dict(os.environ, R3I6NOS_GH=str(self.d / "gh"), FAKE_RELEASES=str(self.rel), R3I6NOS_EXTERNAL_LIST=str(self.d / "list.txt"))
        return subprocess.run([str(ROOT / "scripts/fetch_app_debs.sh"), str(self.out)], env=env, capture_output=True, text=True)

    def both(self, **kw):
        self.release("acme/one", "one.deb", make_deb("one")); self.release("acme/two", "two.deb", make_deb("two", **kw))

    def test_fetches_both(self):
        self.both(); r = self.run_fetch()
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(sorted(p.name for p in self.out.glob("*.deb")), ["one.deb", "two.deb"])

    def test_clears_stale_debs(self):
        self.out.mkdir(); (self.out / "old.deb").write_text("x"); self.both()
        self.assertEqual(self.run_fetch().returncode, 0)
        self.assertFalse((self.out / "old.deb").exists())

    def test_missing_release_fails(self):
        self.release("acme/one", "one.deb", make_deb("one"))
        r = self.run_fetch(); self.assertNotEqual(r.returncode, 0); self.assertIn("acme/two", r.stderr)

    def test_bad_sha_fails(self):
        self.release("acme/one", "one.deb", make_deb("one"), sha="0" * 64); self.release("acme/two", "two.deb", make_deb("two"))
        r = self.run_fetch(); self.assertNotEqual(r.returncode, 0); self.assertIn("sha256", r.stderr)

    def test_draft_grant_fails(self):
        self.both(copyright_text="*** DRAFT GRANT: confirm me ***")
        r = self.run_fetch(); self.assertNotEqual(r.returncode, 0); self.assertIn("DRAFT GRANT", r.stderr)

    def test_no_copyright_fails(self):
        self.both(with_copyright=False)
        r = self.run_fetch(); self.assertNotEqual(r.returncode, 0); self.assertIn("copyright", r.stderr)

    def test_not_a_deb_fails(self):
        self.release("acme/one", "one.deb", b"not a deb"); self.release("acme/two", "two.deb", make_deb("two"))
        r = self.run_fetch(); self.assertNotEqual(r.returncode, 0)

    def test_the_real_list_names_four_apps(self):
        lines = [l.split() for l in (ROOT / "apps/external-debs.txt").read_text().splitlines() if l.strip() and not l.startswith("#")]
        self.assertEqual(sorted(a for _, a in lines), ["KlamANSI.deb", "animate.deb", "compose.deb", "sprites.deb"])


if __name__ == "__main__":
    unittest.main()

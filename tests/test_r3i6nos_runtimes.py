"""r3i6nos-runtimes, the root helper that extracts a PortMaster runtime (ROADMAP section 33), and the launcher's sudo shim, against temporary
folders. unsquashfs is replaced by a script that writes a tree (a set-uid file, a pipe, a link); nothing is mounted and nothing needs root."""
import importlib.machinery
import importlib.util
import os
import stat
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "overlay/usr/local/bin/r3i6nos-runtimes"
SHIM = ROOT / "apps/portmaster/shim/sudo"

FAKE_UNSQUASHFS = """#!/bin/sh
# a stand-in for unsquashfs: -d DEST FILE. FAKE_MODE picks what the "runtime" holds.
[ "$1" = "-version" ] && { echo "unsquashfs version 4.5.1 (fake)"; exit 0; }
while [ $# -gt 1 ]; do [ "$1" = "-d" ] && dest="$2"; shift; done
FAKE_MODE=$(cat "$(dirname "$0")/mode" 2>/dev/null)
case "$FAKE_MODE" in fail) echo "FATAL ERROR: bad superblock" >&2; exit 1 ;; esac
mkdir -p "$dest/bin" "$dest/lib"
echo "#!/bin/sh" > "$dest/bin/run"; { chmod 4755 "$dest/bin/run" 2>/dev/null || chmod 755 "$dest/bin/run"; }
echo data > "$dest/lib/data.so"; chmod 600 "$dest/lib/data.so"
ln -s data.so "$dest/lib/alias.so"
mkfifo "$dest/lib/pipe"
{ chmod 1777 "$dest/lib" 2>/dev/null || chmod 777 "$dest/lib"; }
[ "$FAKE_MODE" = "empty" ] && rm -rf "$dest/bin" "$dest/lib"
exit 0
"""


def load(env):
    with mock.patch.dict(os.environ, env):
        loader = importlib.machinery.SourceFileLoader("r3i6nos_runtimes", str(SRC))
        spec = importlib.util.spec_from_loader("r3i6nos_runtimes", loader)
        mod = importlib.util.module_from_spec(spec)
        sys.dont_write_bytecode = True
        loader.exec_module(mod)
    return mod


class Runtimes(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.t = Path(self.tmp.name).resolve()
        for d in ("req", "out", "cache", "roms/ports/PortMaster/libs", "roms2/ports", "elsewhere"):
            (t / d).mkdir(parents=True)
        fake = t / "unsquashfs"
        fake.write_text(FAKE_UNSQUASHFS)
        fake.chmod(0o755)
        self.libs = t / "roms/ports/PortMaster/libs"
        self.env = {"R3I6NOS_RUNTIMES_DIR": str(t / "req"), "R3I6NOS_RUNTIMES_OUT": str(t / "out"), "R3I6NOS_RUNTIMES_CACHE": str(t / "cache"),
                    "R3I6NOS_RUNTIMES_ROOTS": f"{t / 'roms/ports'}:{t / 'roms2/ports'}", "R3I6NOS_UNSQUASHFS": str(fake), "R3I6NOS_RUNTIMES_UID": ""}
        self.m = load(self.env)
        if os.geteuid() != 0:                    # not root: nothing can be handed to root, the rest is real
            for name in ("chown", "fchown"):
                p = mock.patch.object(os, name, lambda *a, **k: None)
                p.start()
                self.addCleanup(p.stop)

    def tearDown(self):
        self.tmp.cleanup()

    def squash(self, name="godot_3.6.squashfs", where=None, size=4096):
        f = (where or self.libs) / name
        f.write_bytes(b"hsqs" + b"\0" * (size - 4))
        return f

    def result(self):
        text = (self.t / "out/result").read_text()
        d = dict(l.split("=", 1) for l in text.splitlines() if "=" in l and not l.startswith("L\t"))
        d["lines"] = [l[2:] for l in text.splitlines() if l.startswith("L\t")]
        return d

    def ask(self, file, verb="extract", rid="7"):
        (self.t / "req/request").write_text(f"id={rid}\nverb={verb}\nfile={file}\n")
        self.m.apply()
        self.assertFalse((self.t / "req/request").exists(), "the request is deleted first")
        return self.result()

    def test_extracts_once_and_makes_it_safe(self):
        f = self.squash()
        r = self.ask(f)
        self.assertEqual(r["ok"], "1", r)
        d = Path(r["dir"])
        self.assertTrue(str(d).startswith(str(self.t / "cache")))
        self.assertEqual(r["id"], "7")
        run = d / "bin/run"
        self.assertTrue(run.is_file())
        self.assertFalse(stat.S_IMODE(run.stat().st_mode) & 0o6000, "set-uid and set-gid are cleared")
        self.assertEqual(stat.S_IMODE(run.stat().st_mode), 0o755)
        self.assertEqual(stat.S_IMODE((d / "lib/data.so").stat().st_mode), 0o644, "readable by all, not writable")
        self.assertFalse(stat.S_IMODE((d / "lib").stat().st_mode) & 0o1000, "no sticky bit")
        self.assertTrue((d / "lib/alias.so").is_symlink())
        self.assertFalse((d / "lib/pipe").exists(), "a pipe is removed")
        self.assertIn("removed_special=1", (d / self.m.DONE).read_text())
        # the second request does not extract again
        marker = d / "marker"
        marker.write_text("x")
        r2 = self.ask(f, rid="8")
        self.assertEqual((r2["ok"], r2["dir"], r2["id"]), ("1", str(d), "8"))
        self.assertTrue(marker.exists(), "an extracted runtime is reused, not made again")
        self.assertIn("already", r2["lines"][0])
        self.assertEqual([p.name for p in (self.t / "cache").iterdir()], [d.name], "no temporary folder is left")

    def test_card_two_is_allowed_and_a_changed_file_is_a_new_entry(self):
        f = self.squash(where=self.t / "roms2/ports")
        self.assertEqual(self.ask(f)["ok"], "1")
        f.write_bytes(b"hsqs" + b"\0" * 8000)
        os.utime(f, (1, 2))
        r = self.ask(f)
        self.assertEqual(r["ok"], "1")
        self.assertEqual(len(list((self.t / "cache").iterdir())), 2)

    def test_refusals(self):
        out = self.t / "elsewhere"
        cases = {
            "outside the ports folder": self.squash(where=out),
            "a relative path": "PortMaster/libs/x.squashfs",
            "not a squashfs": None,
            "bad name": self.squash(name="x y.squashfs"),
            "not a squashfs extension": self.squash(name="x.tar"),
            "too small": self.squash(name="small.squashfs", size=40),
            "a missing file": self.libs / "missing.squashfs",
        }
        (self.libs / "bad.squashfs").write_bytes(b"XXXX" + b"\0" * 400)
        cases["not a squashfs"] = self.libs / "bad.squashfs"
        for why, f in cases.items():
            r = self.ask(f)
            self.assertEqual(r["ok"], "0", why)
            self.assertNotIn("dir", r, why)
        self.assertEqual(list((self.t / "cache").iterdir()), [], "nothing was extracted")

    def test_a_link_out_of_the_ports_folder_is_refused(self):
        outside = self.squash(where=self.t / "elsewhere")
        (self.libs / "link.squashfs").symlink_to(outside)
        self.assertEqual(self.ask(self.libs / "link.squashfs")["ok"], "0")

    def test_only_extract(self):
        f = self.squash()
        self.assertEqual(self.ask(f, verb="mount")["ok"], "0")
        self.assertEqual(self.ask(f, verb="extract; rm -rf /")["ok"], "0")

    def test_a_failed_extraction_leaves_nothing_and_says_why(self):
        f = self.squash()
        (self.t / "mode").write_text("fail")
        r = self.ask(f)
        self.assertEqual(r["ok"], "0")
        self.assertIn("bad superblock", " ".join(r["lines"]))
        self.assertEqual(list((self.t / "cache").iterdir()), [])
        (self.t / "mode").write_text("empty")
        self.assertEqual(self.ask(f)["ok"], "0")
        self.assertEqual(list((self.t / "cache").iterdir()), [])

    def test_room_and_cache_limit(self):
        f = self.squash()
        with mock.patch.object(self.m.shutil, "disk_usage", return_value=mock.Mock(free=1024)):
            r = self.ask(f)
        self.assertEqual(r["ok"], "0")
        self.assertIn("room", " ".join(r["lines"]))
        self.m.MAX_BYTES = 100
        r = self.ask(f)
        self.assertEqual(r["ok"], "0")
        self.assertIn("prune", " ".join(r["lines"]))

    def test_a_request_that_is_a_link_or_too_big_is_answered_and_deleted(self):
        (self.t / "req/request").symlink_to("/etc/passwd")
        self.assertEqual(self.m.read_request(), "")
        (self.t / "req/request").unlink()
        (self.t / "req/request").write_text("x" * 9000)
        self.assertEqual(self.m.read_request(), "")
        self.m.apply()
        self.assertEqual(self.result()["ok"], "0")
        self.assertFalse((self.t / "req/request").exists())

    def test_no_request_does_nothing(self):
        self.assertEqual(self.m.apply(), 0)
        self.assertFalse((self.t / "out/result").exists())

    def test_progress_is_written_and_cleared(self):
        seen = []
        real = self.m.progress
        self.m.progress = lambda step, doing: (seen.append(step), real(step, doing))
        self.ask(self.squash())
        self.assertEqual(seen, [1, 2, 3, 4], "the bar reaches its last step (rule 18)")
        self.assertFalse((self.t / "out/progress").exists())

    def test_status_and_prune(self):
        self.ask(self.squash())
        import io
        from contextlib import redirect_stdout
        buf = io.StringIO()
        with redirect_stdout(buf):
            self.m.status()
        text = buf.getvalue()
        self.assertIn("extracted: 1", text)
        self.assertIn("runtime: godot_3.6-4096-", text)
        self.assertIn("unsquashfs: unsquashfs version", text)
        with redirect_stdout(io.StringIO()):
            self.m.prune()
        self.assertEqual(list((self.t / "cache").iterdir()), [])

    # ---- the shim -----------------------------------------------------------------------------------------------------------

    def run_shim(self, *args, serve=True, expect_ok=True):
        env = dict(os.environ, **self.env, R3I6NOS_RUNTIMES_WAIT="20")
        env["R3I6NOS_RUNTIMES_CACHE"] = str(self.t / "cache")
        stop = threading.Event()

        def helper():
            while not stop.is_set():
                if (self.t / "req/request").exists():
                    time.sleep(0.05)
                    self.m.apply()
                    return
                time.sleep(0.02)
        th = threading.Thread(target=helper)
        if serve:
            th.start()
        try:
            return subprocess.run(["sh", str(SHIM), *args], env=env, capture_output=True, text=True, timeout=60)
        finally:
            stop.set()
            if serve:
                th.join()

    def test_shim_mount_links_the_extracted_folder_and_umount_removes_it(self):
        f = self.squash()
        dest = self.t / "home/godot"
        (self.t / "home").mkdir()
        dest.mkdir()                                            # a port makes the folder first
        r = self.run_shim("-n", "mount", "-o", "loop", str(f), str(dest))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue(dest.is_symlink())
        self.assertTrue((dest / "bin/run").is_file())
        # a second mount of the same runtime elsewhere is a second link to the one extraction
        d2 = self.t / "home/other/godot"
        self.assertEqual(self.run_shim("mount", str(f), str(d2)).returncode, 0)
        self.assertEqual(os.readlink(d2), os.readlink(dest))
        self.assertEqual(len(list((self.t / "cache").iterdir())), 1)
        self.assertEqual(self.run_shim("umount", str(dest), serve=False).returncode, 0)
        self.assertFalse(os.path.lexists(dest))
        self.assertEqual(self.run_shim("umount", str(dest), serve=False).returncode, 1, "nothing to unmount")

    def test_shim_refuses_what_is_not_a_runtime_mount(self):
        f = self.squash()
        keep = self.t / "home/keep"
        keep.mkdir(parents=True)
        (keep / "mine").write_text("x")
        for args in (("mount", str(f), str(keep)),                       # a folder with the port's own files
                     ("mount", "--bind", str(keep), str(self.t / "home/b")),
                     ("mount", str(self.t / "home/keep/mine"), str(self.t / "home/c")),
                     ("chmod", "666", "/dev/uinput"), ("rm", "-rf", "/"), ("mkdir", "-p", "/x"), ()):
            r = self.run_shim(*args, serve=False)
            self.assertEqual(r.returncode, 1, args)
        self.assertEqual((keep / "mine").read_text(), "x")
        # umount never removes a link that is not ours
        (self.t / "home/foreign").symlink_to(keep)
        self.assertEqual(self.run_shim("umount", str(self.t / "home/foreign"), serve=False).returncode, 1)
        self.assertTrue((self.t / "home/foreign").is_symlink())

    def test_shim_reports_the_helpers_refusal(self):
        r = self.run_shim("mount", str(self.squash(where=self.t / "elsewhere")), str(self.t / "home/x"))
        self.assertEqual(r.returncode, 1)
        self.assertIn("ports folder", r.stderr)


if __name__ == "__main__":
    unittest.main()

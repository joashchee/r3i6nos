"""While a Rec demo records (docs/DEMO-RECORDINGS.md), r3i6nos's helpers do not start a network, an SSH server, an update check or a device report."""
import importlib.machinery
import importlib.util
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

BIN = Path(__file__).resolve().parent.parent / "overlay/usr/local/bin"


def load(name, env):
    with mock.patch.dict(os.environ, env):
        loader = importlib.machinery.SourceFileLoader(name.replace("-", "_"), str(BIN / name))
        spec = importlib.util.spec_from_loader(loader.name, loader)
        mod = importlib.util.module_from_spec(spec)
        sys.dont_write_bytecode = True
        loader.exec_module(mod)
    return mod


class DemoLock(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.run_dir = Path(self.tmp.name) / "run"
        (self.run_dir / "demo").mkdir(parents=True)
        self.env = {"R3I6NOS_RUN": str(self.run_dir)}
        self.patch = mock.patch.dict(os.environ, self.env)
        self.patch.start()

    def tearDown(self):
        self.patch.stop()
        self.tmp.cleanup()

    def lock(self, pid):
        (self.run_dir / "demo/lock").write_text(f"{pid}\n")

    def test_the_lock_is_a_live_pid(self):
        net = load("r3i6nos-net", self.env)
        self.assertFalse(net.demo_recording(), "no lock file")
        self.lock(os.getpid())
        self.assertTrue(net.demo_recording(), "a live holder")
        self.lock(999999)
        self.assertFalse(net.demo_recording(), "a lock left by a dead process blocks nothing")
        (self.run_dir / "demo/lock").write_text("not a pid")
        self.assertFalse(net.demo_recording(), "garbage blocks nothing")

    def test_network_and_ssh_do_not_start(self):
        net = load("r3i6nos-net", self.env)
        self.lock(os.getpid())
        for verb in ("scan", "wired", "wifi", "test", "ssh-enable"):
            res = net.handle({"verb": verb})
            self.assertFalse(res.ok, verb)
            self.assertIn("Rec demo is recording", res.lines[0], verb)
        self.assertNotIn("Rec demo", " ".join(net.handle({"verb": "ssh-status"}).lines), "reading the status is still allowed")

    def test_the_update_check_does_not_run(self):
        about = load("r3i6nos-about", self.env)
        self.lock(os.getpid())
        out = Path(self.tmp.name) / "check.txt"
        with mock.patch.object(about, "check", side_effect=AssertionError("the network must not be used")):
            self.assertEqual(about.main(["r3i6nos-about", "check", str(out)]), 0)
        self.assertIn("Rec demo is recording", out.read_text())

    def test_the_device_report_does_not_run(self):
        report = load("r3i6nos-report", self.env)
        self.lock(os.getpid())
        with mock.patch.object(report, "Report", side_effect=AssertionError("no report file may be started")):
            self.assertEqual(report.main(["--quiet"]), 75)


if __name__ == "__main__":
    unittest.main()

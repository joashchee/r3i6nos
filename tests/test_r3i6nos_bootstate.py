"""r3i6nos-bootstate: what systemctl and systemd-analyze say, written for the device report, against fakes of both."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "overlay/usr/local/bin/r3i6nos-bootstate"


class BootState(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.t = Path(self.tmp.name)
        (self.t / "bin").mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def fake(self, name, body):
        p = self.t / "bin" / name
        p.write_text("#!/bin/sh\n" + body)
        p.chmod(0o755)

    def run_it(self):
        out = self.t / "run/r3i6nos/boot-state"
        env = dict(os.environ, PATH=f"{self.t / 'bin'}:{os.environ['PATH']}", R3I6NOS_BOOTSTATE=str(out))
        subprocess.run(["bash", str(SRC)], env=env, check=True, timeout=30)
        return out

    def test_a_boot_with_a_failed_unit_and_a_job(self):
        self.fake("systemctl", 'case "$1" in\n'
                  "  list-units) echo 'a.service loaded failed failed A'; echo 'b.service loaded failed failed B' ;;\n"
                  "  is-system-running) echo degraded; exit 1 ;;\n"
                  "  list-jobs) echo '12 r3i6nos-net.path start waiting' ;;\n"
                  "  is-active) [ \"$2\" = loveos.service ] && { echo active; exit 0; }; echo inactive; exit 3 ;;\nesac\n")
        self.fake("systemd-analyze", "echo 'Startup finished in 3.1s (kernel) + 9.2s (userspace) = 12.3s'\n")
        out = self.run_it()
        text = out.read_text()
        for want in ("failed units: a.service b.service", "system state: degraded", "jobs waiting: r3i6nos-net.path",
                     "boot time: Startup finished in 3.1s (kernel) + 9.2s (userspace) = 12.3s",
                     "unit loveos.service: active", "unit r3i6nos-sound.service: inactive"):
            self.assertIn(want, text)
        self.assertTrue(text.startswith("written: "))
        self.assertEqual(out.stat().st_mode & 0o777, 0o644, "readable by the menu's user")
        self.assertFalse(out.with_name("boot-state.tmp").exists(), "written whole, by a rename")

    def test_a_clean_boot_and_one_not_finished(self):
        self.fake("systemctl", 'case "$1" in is-system-running) echo starting ;; is-active) echo active ;; esac\n')
        self.fake("systemd-analyze", "echo 'Bootup is not yet finished (FinishTimestampMonotonic=0).' >&2; exit 1\n")
        text = self.run_it().read_text()
        self.assertIn("failed units: none", text)
        self.assertIn("jobs waiting: none", text)
        self.assertIn("system state: starting", text)
        self.assertIn("boot time: Bootup is not yet finished", text)


if __name__ == "__main__":
    unittest.main()

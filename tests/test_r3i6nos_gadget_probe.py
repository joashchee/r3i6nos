"""r3i6nos-gadget-probe (ROADMAP section 28): builds a USB gadget in a fake /sys and configfs, and reports. No hardware, no card."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "overlay/usr/local/bin/r3i6nos-gadget-probe"


class GadgetProbe(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.t = Path(self.tmp.name)
        (t / "sys/class/udc/ff300000.usb").mkdir(parents=True)
        (t / "sys/class/udc/ff300000.usb/state").write_text("not attached\n")
        (t / "sys/class/power_supply/usb").mkdir(parents=True)
        (t / "sys/class/power_supply/usb/online").write_text("0\n")
        (t / "sys/kernel/config/usb_gadget").mkdir(parents=True)
        (t / "bin").mkdir()
        for n in ("modprobe", "mount"):       # the fakes record what they were asked
            (t / "bin" / n).write_text(f'#!/bin/sh\necho "$0 $@" >> {t}/calls\nexit ${{FAKE_{n.upper()}_RC:-0}}\n')
            (t / "bin" / n).chmod(0o755)

    def tearDown(self):
        self.tmp.cleanup()

    def run_probe(self, *args, env=None):
        e = dict(os.environ, R3I6NOS_GADGET_SYS=str(self.t / "sys"), R3I6NOS_GADGET_RUN=str(self.t / "run"),
                 R3I6NOS_GADGET_MODPROBE=str(self.t / "bin/modprobe"), R3I6NOS_GADGET_MOUNT=str(self.t / "bin/mount"),
                 R3I6NOS_GADGET_NOROOT="1", R3I6NOS_GADGET_FAKE="1")
        e.update(env or {})
        return subprocess.run(["bash", str(SRC), *args], capture_output=True, text=True, env=e, timeout=30)

    def test_no_cable_builds_binds_and_tears_down(self):
        r = self.run_probe("--wait", "1")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("PASS bind:", r.stdout)
        self.assertIn("FAIL host: the UDC never left not attached", r.stdout)
        self.assertIn("PASS teardown", r.stdout)
        self.assertEqual(r.stdout.strip().splitlines()[-1], "end")
        self.assertFalse((self.t / "sys/kernel/config/usb_gadget/r3i6nos_probe").exists())
        self.assertTrue((self.t / "run/gadget-probe.txt").read_text().endswith("end\n"))

    def test_configured_host_passes(self):
        (self.t / "sys/class/udc/ff300000.usb/state").write_text("configured\n")
        r = self.run_probe("--wait", "3")
        self.assertIn("PASS host: a computer enumerated the gadget", r.stdout)
        self.assertIn("PASS teardown", r.stdout)

    def test_attached_but_not_configured_is_info(self):
        (self.t / "sys/class/udc/ff300000.usb/state").write_text("powered\n")
        r = self.run_probe("--wait", "1")
        self.assertIn("INFO host: the UDC saw a cable", r.stdout)

    def test_no_udc_fails_without_building(self):
        (self.t / "sys/class/udc/ff300000.usb/state").unlink()
        (self.t / "sys/class/udc/ff300000.usb").rmdir()
        r = self.run_probe("--wait", "1")
        self.assertIn("FAIL udc: none", r.stdout)
        self.assertFalse((self.t / "sys/kernel/config/usb_gadget/r3i6nos_probe").exists())
        self.assertEqual(r.stdout.strip().splitlines()[-1], "end")

    def test_no_configfs_gadget_dir_is_a_fail(self):
        (self.t / "sys/kernel/config/usb_gadget").rmdir()
        r = self.run_probe("--wait", "1")
        self.assertIn("FAIL configfs:", r.stdout)
        self.assertIn(f"{self.t}/bin/mount -t configfs", (self.t / "calls").read_text())

    def test_the_legacy_gadget_module_is_one_modprobe_and_is_unloaded_after(self):
        (self.t / "sys/class/udc/ff300000.usb/state").write_text("configured\n")
        r = self.run_probe("--legacy", "ether", "--wait", "3")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("PASS legacy g_ether: loaded", r.stdout)
        self.assertIn("PASS host: a computer enumerated the gadget", r.stdout)
        self.assertIn("PASS teardown: g_ether unloaded", r.stdout)
        self.assertEqual(r.stdout.strip().splitlines()[-1], "end")
        calls = (self.t / "calls").read_text()
        self.assertIn(f"{self.t}/bin/modprobe g_ether", calls)
        self.assertIn(f"{self.t}/bin/modprobe -r g_ether", calls)
        self.assertNotIn("configfs", calls, "the legacy route needs no configfs")
        self.assertFalse((self.t / "sys/kernel/config/usb_gadget/r3i6nos_probe").exists())

    def test_without_a_configfs_gadget_the_probe_tries_g_ether_by_itself(self):
        (self.t / "sys/kernel/config/usb_gadget").rmdir()
        (self.t / "sys/class/udc/ff300000.usb/state").write_text("configured\n")
        r = self.run_probe("--wait", "3")
        self.assertIn("FAIL configfs:", r.stdout)
        self.assertIn("legacy: trying the one-shot g_ether gadget", r.stdout)
        self.assertIn("PASS legacy g_ether: loaded", r.stdout)
        self.assertIn("PASS host: a computer enumerated the gadget", r.stdout)
        self.assertEqual(r.stdout.strip().splitlines()[-1], "end")

    def test_a_legacy_module_that_will_not_load_is_a_fail(self):
        r = self.run_probe("--legacy", "ncm", "--wait", "1", env={"FAKE_MODPROBE_RC": "1"})
        self.assertIn("FAIL legacy g_ncm: modprobe failed", r.stdout)
        self.assertEqual(r.stdout.strip().splitlines()[-1], "end")

    def test_keep_leaves_the_gadget_up_and_down_takes_it_away(self):
        g = self.t / "sys/kernel/config/usb_gadget/r3i6nos_probe"
        r = self.run_probe("--keep", "--wait", "1")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("INFO kept: the gadget stays up", r.stdout)
        self.assertNotIn("teardown", r.stdout)
        self.assertTrue(g.exists(), "a kept gadget is still in configfs")
        self.assertEqual(r.stdout.strip().splitlines()[-1], "end")
        again = self.run_probe("--wait", "1")        # a second probe must say how to clear it, not stumble
        self.assertIn("run `r3i6nos-gadget-probe --down`", again.stdout)
        down = self.run_probe("--down")
        self.assertIn("PASS down: the configfs gadget is gone", down.stdout)
        self.assertFalse(g.exists())
        self.assertEqual(down.stdout.strip().splitlines()[-1], "end")

    def test_down_with_nothing_up_says_so(self):
        r = self.run_probe("--down")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("INFO down: nothing of ours is up", r.stdout)

    def test_the_wait_logs_each_udc_state_with_its_time(self):
        r = self.run_probe("--wait", "2")
        self.assertIn("INFO udc state: not attached (after 0s)", r.stdout)
        self.assertEqual(r.stdout.count("INFO udc state:"), 1, "a state that does not change is logged once")

    def test_keep_with_the_legacy_gadget_does_not_unload_it_and_down_does(self):
        (self.t / "sys/class/udc/ff300000.usb/state").write_text("configured\n")
        (self.t / "sys/module/g_ether").mkdir(parents=True)          # what a loaded module looks like
        r = self.run_probe("--legacy", "ether", "--keep", "--wait", "2")
        self.assertIn("INFO kept: g_ether stays loaded", r.stdout)
        self.assertNotIn(f"modprobe -r g_ether", (self.t / "calls").read_text())
        down = self.run_probe("--down")
        self.assertIn("INFO down: g_ether unloaded", down.stdout)
        self.assertIn(f"{self.t}/bin/modprobe -r g_ether", (self.t / "calls").read_text())

    def test_bad_arguments(self):
        self.assertEqual(self.run_probe("--legacy", "nope").returncode, 2)
        self.assertEqual(self.run_probe("--function", "nope").returncode, 2)
        self.assertEqual(self.run_probe("--wait", "x").returncode, 2)

    def test_needs_root(self):
        if os.geteuid() == 0:
            self.skipTest("running as root")
        r = self.run_probe(env={"R3I6NOS_GADGET_NOROOT": "0"})
        self.assertEqual(r.returncode, 1)
        self.assertIn("needs root", r.stderr)


if __name__ == "__main__":
    unittest.main()

"""r3i6nos-battery, the battery sampler, against a fake /sys/class/power_supply and /proc. Nothing here needs a battery."""
import importlib.machinery
import importlib.util
import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stderr
from pathlib import Path
from unittest import mock

SRC = Path(__file__).resolve().parent.parent / "overlay/usr/local/bin/r3i6nos-battery"


def load(env):
    with mock.patch.dict(os.environ, env):
        loader = importlib.machinery.SourceFileLoader("r3i6nos_battery", str(SRC))
        spec = importlib.util.spec_from_loader("r3i6nos_battery", loader)
        mod = importlib.util.module_from_spec(spec)
        sys.dont_write_bytecode = True
        loader.exec_module(mod)
    return mod


class Battery(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.t = Path(self.tmp.name)
        (t / "power").mkdir()
        (t / "proc/sys/kernel/random").mkdir(parents=True)
        (t / "proc/uptime").write_text("754.31 1200.00\n")
        (t / "proc/sys/kernel/random/boot_id").write_text("abcdef01-2345-6789-abcd-ef0123456789\n")
        self.m = load({
            "R3I6NOS_POWER_SUPPLY": str(t / "power"), "R3I6NOS_BATTERY_DIR": str(t / "state"), "R3I6NOS_PROC": str(t / "proc"),
            "R3I6NOS_BATTERY_KEEP": "5",
        })

    def tearDown(self):
        self.tmp.cleanup()

    def supply(self, name, **files):
        d = self.t / "power" / name
        d.mkdir(parents=True, exist_ok=True)
        for k, v in files.items():
            (d / k).write_text(str(v) + "\n")
        return d

    def run_main(self, *args):
        err = io.StringIO()
        with redirect_stderr(err):
            rc = self.m.main(["r3i6nos-battery", *args])
        return rc, err.getvalue()

    def lines(self):
        return (self.t / "state/history.tsv").read_text().splitlines()

    def test_records_a_full_line(self):
        self.supply("ac", type="Mains", online=1)
        self.supply("battery", type="Battery", capacity=87, status="Discharging", voltage_now=3912000, temp=315,
                    charge_full=3000000, charge_full_design=3200000, cycle_count=42)
        self.assertEqual(self.run_main("sample", "app-start", "Super", "Game")[0], 0)
        f = self.lines()[0].split("\t")
        self.assertEqual(len(f), 13)
        self.assertEqual((f[0], f[1], f[2], f[4], f[5], f[6], f[7]), ("1", "abcdef01", "754", "app-start", "Super Game", "87", "Discharging"))
        self.assertEqual((f[8], f[9], f[10], f[11], f[12]), ("3912", "3000000", "3200000", "315", "42"))
        self.assertTrue(f[3].isdigit())

    def test_only_what_the_battery_reports(self):
        self.supply("bms", type="Battery", capacity=50, status="Charging")
        self.run_main("sample", "session")
        f = self.lines()[0].split("\t")
        self.assertEqual(f[8:], ["-", "-", "-", "-", "-"])

    def test_energy_pair_when_there_is_no_charge_pair(self):
        self.supply("battery", type="Battery", capacity=10, status="Full", energy_full=9000, energy_full_design=10000)
        self.run_main("sample", "tick", "x")
        f = self.lines()[0].split("\t")
        self.assertEqual((f[9], f[10]), ("9000", "10000"))

    def test_finds_a_battery_by_name_when_type_is_missing(self):
        self.supply("rk-battery", capacity=33, status="Discharging")
        self.run_main("sample", "session")
        self.assertEqual(self.lines()[0].split("\t")[6], "33")

    def test_no_battery_records_nothing_and_does_not_fail(self):
        self.supply("ac", type="Mains", online=1)
        rc, err = self.run_main("sample", "app-start", "x")
        self.assertEqual(rc, 0)
        self.assertIn("no battery", err)
        self.assertFalse((self.t / "state/history.tsv").exists())

    def test_detail_stays_on_one_line_and_short(self):
        self.supply("battery", type="Battery", capacity=60, status="Discharging")
        self.run_main("sample", "app-start", "a\tb\nc", "x" * 200)
        line = self.lines()
        self.assertEqual(len(line), 1)
        f = line[0].split("\t")
        self.assertEqual(len(f), 13)
        self.assertLessEqual(len(f[5]), 60)

    def test_percent_is_clamped(self):
        self.supply("battery", type="Battery", capacity=140, status="Full")
        self.run_main("sample", "tick")
        self.assertEqual(self.lines()[0].split("\t")[6], "100")

    def test_usage_errors(self):
        self.assertEqual(self.run_main()[0], 2)
        self.assertEqual(self.run_main("sample", "bogus")[0], 2)
        self.assertEqual(self.run_main("clear")[0], 2)

    def test_log_is_trimmed_to_the_newest(self):
        self.supply("battery", type="Battery", capacity=60, status="Discharging")
        for i in range(40):
            self.run_main("sample", "tick", f"n{i}")
        got = self.lines()
        self.assertLessEqual(len(got), 5 + 500)                 # LIMIT = KEEP + 500
        self.assertEqual(got[-1].split("\t")[5], "n39")          # newest kept
        for i in range(600):
            self.run_main("sample", "tick", f"m{i}")
        got = self.lines()
        self.assertLessEqual(len(got), 505)
        self.assertEqual(got[-1].split("\t")[5], "m599")

    def test_unwritable_log_does_not_fail(self):
        self.supply("battery", type="Battery", capacity=60, status="Discharging")
        (self.t / "state").write_text("a file where the folder should be")
        rc, err = self.run_main("sample", "tick")
        self.assertEqual(rc, 0)
        self.assertIn("r3i6nos-battery:", err)


if __name__ == "__main__":
    unittest.main()

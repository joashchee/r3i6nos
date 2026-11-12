"""The command-line tools' device report (tools/qjs-node/report.py, docs/DEVICE-REPORTS.md): run against a fake tool, the report on
the pretend card must follow the grammar, count its statuses, time each command, skip !SLOW ones and end with "end"."""
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TEMPLATE = REPO / "tools/qjs-node/report.py"


def make(dirpath):
    """The report as package-tool.sh makes it, next to a fake `fake` tool, with its commands.txt in share/."""
    d = Path(dirpath)
    (d / "bin").mkdir()
    (d / "share/r36s-fake").mkdir(parents=True)
    text = TEMPLATE.read_text().replace("@TOOL@", "fake").replace("@APP@", "Fake App").replace("@VERSION@", "9.9").replace("@PACKAGE@", "r36s-fake")
    rep = d / "bin/fake-report"
    rep.write_text(text)
    tool = d / "bin/fake"
    tool.write_text('#!/bin/sh\ncase "$1" in\n  ok) echo fine; echo made > o/out.txt ;;\n  bad) echo "no such kit" >&2; exit 2 ;;\n'
                    '  crash) kill -9 $$ ;;\n  long) sleep 2.2 ;;\nesac\n')
    for p in (rep, tool):
        p.chmod(p.stat().st_mode | stat.S_IEXEC)
    (d / "share/r36s-fake/commands.txt").write_text("# a comment\n\nok\nbad --strict\n!SLOW ok slowly\ncrash\n")
    return rep


class ToolReport(unittest.TestCase):
    def test_a_report_on_the_card(self):
        with tempfile.TemporaryDirectory() as t:
            rep = make(t)
            roms = Path(t) / "roms"
            out = subprocess.run([sys.executable, str(rep), "--from-settings", "--quiet"], env={**os.environ, "R3I6NOS_ROMS": str(roms)},
                                 capture_output=True, text=True, timeout=60)
            self.assertEqual(out.returncode, 0, out.stderr)
            path = roms / "Fake App/r36s-report.txt"
            lines = path.read_text().splitlines()
            self.assertEqual(lines[0], "# r3i6nos device report")
            self.assertEqual(lines[1], "app: Fake App 9.9 (r36s-fake)")
            self.assertEqual(lines[3], "how: Settings > Device reports")
            self.assertTrue(lines[4].startswith("for: Claude. Copy EASYROMS/Fake App/r36s-report.txt"))
            self.assertEqual(lines[-1], "end")
            summary = lines.index("== summary ==")
            facts = [l for l in lines[5:summary] if l.startswith(("PASS ", "FAIL ", "INFO ", "SKIP "))]
            others = [l for l in lines[5:summary] if l and not l.startswith("== ") and l not in facts]
            self.assertEqual(others, [], "every line is a fact or a section")
            for l in facts:
                self.assertRegex(l, r"^(PASS|FAIL|INFO|SKIP) [^:]+: ")
            counts = {s: sum(1 for l in facts if l.startswith(s + " ")) for s in ("PASS", "FAIL", "INFO", "SKIP")}
            self.assertIn(f"PASS {counts['PASS']}  FAIL {counts['FAIL']}  INFO {counts['INFO']}  SKIP {counts['SKIP']}", lines)
            text = "\n".join(lines)
            self.assertRegex(text, r"PASS fake ok: [0-9.]+ s, peak \d+ KB, exit 0\n")
            self.assertRegex(text, r"PASS fake bad --strict: .*exit 2; stderr: no such kit")
            self.assertIn("SKIP fake ok slowly: slow under QuickJS", text)
            self.assertRegex(text, r"FAIL fake crash: killed by signal 9")
            self.assertIn("PASS rename over a file: ok", text)
            self.assertFalse((roms / "Fake App/r36s-report.txt.tmp").exists())
            self.assertFalse((roms / "Fake App/.r36s-report-test").exists())

    def test_progress_for_the_menu(self):
        """With R3I6NOS_REPORT_PROGRESS (set by the menu), it says its steps: system, card, then one a command; the file it writes."""
        with tempfile.TemporaryDirectory() as t:
            rep = make(t)
            roms, prog = Path(t) / "roms", Path(t) / "progress"
            out = subprocess.run([sys.executable, str(rep), "--from-settings", "--quiet"],
                                 env={**os.environ, "R3I6NOS_ROMS": str(roms), "R3I6NOS_REPORT_PROGRESS": str(prog)}, capture_output=True, text=True, timeout=60)
            self.assertEqual(out.returncode, 0, out.stderr)
            kv = dict(l.split("=", 1) for l in prog.read_text().splitlines())
            self.assertEqual(kv["steps"], "8", "system, card, the power baseline, the four commands and the power section")
            self.assertEqual(kv["step"], "8")
            self.assertEqual(kv["doing"], "done")
            self.assertEqual(kv["file"], str(roms / "Fake App/r36s-report.txt"))
            self.assertFalse(Path(str(prog) + ".tmp").exists())

    def run_power(self, status, commands, extra_env=None):
        """The report with a fake battery (status; 700 mA at 3.8 V = 2660 mW), and the given commands.txt; returns the report's text."""
        with tempfile.TemporaryDirectory() as t:
            rep = make(t)
            (Path(t) / "share/r36s-fake/commands.txt").write_text(commands)
            bat = Path(t) / "sys/class/power_supply/battery"
            bat.mkdir(parents=True)
            for name, text in (("type", "Battery"), ("status", status), ("capacity", "69"), ("current_now", "-700000"), ("voltage_now", "3800000"), ("charge_full", "3380000")):
                (bat / name).write_text(text + "\n")
            roms = Path(t) / "roms"
            env = {**os.environ, "R3I6NOS_ROMS": str(roms), "R3I6NOS_SYS": str(Path(t) / "sys"), "R3I6NOS_REPORT_POWER_S": "1", **(extra_env or {})}
            out = subprocess.run([sys.executable, str(rep), "--quiet"], env=env, capture_output=True, text=True, timeout=90)
            self.assertEqual(out.returncode, 0, out.stderr)
            return (roms / "Fake App/r36s-report.txt").read_text()

    def test_power_section_on_battery(self):
        """Discharging: the idle baseline, the draw during a command of a second or more, the energy over idle, the wake-up goal and what to cut first."""
        text = self.run_power("Discharging", "ok\nlong\n")
        self.assertIn("\n== power ==\n", text)
        self.assertLess(text.index("== commands =="), text.index("== power =="), "the section follows the commands it measures")
        self.assertIn("INFO battery supply: battery: 69%, Discharging", text)
        self.assertRegex(text, r"INFO working window: 2 commands, [0-9.]+ s of running")
        self.assertRegex(text, r"INFO command fake long: 2\.\d+ s, [0-9.]+ CPU s \(\d+% of a core\), \d+ wake-ups/s, peak \d+ KB, draw 2660 mW")
        self.assertNotRegex(text, r"INFO command fake ok:", "a command under a second is not listed: its start-up dominates")
        self.assertRegex(text, r"PASS working wakeups: worst [0-9.]+/s \(fake long; goal at most 200")
        self.assertRegex(text, r"INFO working draw: 2660 mW mean \(min 2660, max 2660, \d+ readings")
        self.assertRegex(text, r"INFO idle draw: 2660 mW with nothing running \(\d+ readings\): the commands cost \+0 mW over it")
        self.assertRegex(text, r"INFO working energy: about 0\.0 J over idle for the 1 command of 1 s or more")
        self.assertRegex(text, r"INFO working time left: [0-9.]+ h at this draw \(69% of 3380 mAh\)")
        self.assertIn("INFO cut first: CPU time is the energy of a tool: `fake long` took", text)
        self.assertIn("INFO reading this: one run on one battery", text)
        self.assertTrue(text.endswith("end\n"))

    def test_power_section_on_the_charger_says_to_unplug(self):
        text = self.run_power("Charging", "ok\nlong\n")
        self.assertIn("SKIP draw: the battery says Charging, not Discharging: unplug the power", text)
        self.assertNotIn("working draw", text)
        self.assertRegex(text, r"PASS working wakeups: worst", "CPU and wake-ups are measured on any power")

    def test_power_section_with_no_battery_and_only_short_commands(self):
        with tempfile.TemporaryDirectory() as t:
            rep = make(t)
            roms = Path(t) / "roms"
            sysdir = Path(t) / "emptysys"
            sysdir.mkdir()
            out = subprocess.run([sys.executable, str(rep), "--quiet"], env={**os.environ, "R3I6NOS_ROMS": str(roms), "R3I6NOS_SYS": str(sysdir)},
                                 capture_output=True, text=True, timeout=60)
            self.assertEqual(out.returncode, 0, out.stderr)
            text = (roms / "Fake App/r36s-report.txt").read_text()
            self.assertIn("SKIP battery: no battery supply found", text)
            self.assertIn("INFO idle state: a command-line tool is not running between commands", text)
            self.assertRegex(text, r"SKIP working wakeups: no command ran 1 s or more")
            self.assertRegex(text, r"INFO working cpu: [0-9.]+ CPU s in [0-9.]+ s")

    def test_no_command_list_is_a_fail_not_a_crash(self):
        with tempfile.TemporaryDirectory() as t:
            rep = make(t)
            (Path(t) / "share/r36s-fake/commands.txt").unlink()
            roms = Path(t) / "roms"
            out = subprocess.run([sys.executable, str(rep), "--quiet"], env={**os.environ, "R3I6NOS_ROMS": str(roms)}, capture_output=True, text=True, timeout=60)
            self.assertEqual(out.returncode, 0, out.stderr)
            text = (roms / "Fake App/r36s-report.txt").read_text()
            self.assertIn("FAIL commands.txt: not found", text)
            self.assertIn("how: fake-report\n", text)
            self.assertTrue(text.endswith("end\n"))


if __name__ == "__main__":
    unittest.main()

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
                    '  crash) kill -9 $$ ;;\nesac\n')
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
            self.assertEqual(kv["steps"], "6", "system, card and the four commands")
            self.assertEqual(kv["step"], "6")
            self.assertEqual(kv["doing"], "done")
            self.assertEqual(kv["file"], str(roms / "Fake App/r36s-report.txt"))
            self.assertFalse(Path(str(prog) + ".tmp").exists())

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

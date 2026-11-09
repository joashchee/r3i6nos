"""r3i6nos-report, the system's own device report (docs/STORAGE.md, docs/DEVICE-REPORTS.md), against a fake /etc, /proc, /sys and cards."""
import importlib.machinery
import importlib.util
import io
import os
import re
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

SRC = Path(__file__).resolve().parent.parent / "overlay/usr/local/bin/r3i6nos-report"
LINE = re.compile(r"^(PASS|FAIL|INFO|SKIP) [^:]+: .*$")


def load(env):
    with mock.patch.dict(os.environ, env):
        loader = importlib.machinery.SourceFileLoader("r3i6nos_report", str(SRC))
        spec = importlib.util.spec_from_loader("r3i6nos_report", loader)
        mod = importlib.util.module_from_spec(spec)
        sys.dont_write_bytecode = True
        loader.exec_module(mod)
    return mod


class SystemReport(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.t = Path(self.tmp.name)
        for d in ("etc", "proc/device-tree", "sys/block/mmcblk0", "sys/block/mmcblk1", "sys/class/mmc_host/mmc0", "sys/class/mmc_host/mmc1",
                  "dev", "run", "sys1", "sys2", "bin"):
            (t / d).mkdir(parents=True)
        (t / "etc/r3i6nos-release").write_text("VERSION=0.1.0\nCOMMIT=396fcd3" + "0" * 33 + "\nCOMMIT_DATE=2026-11-09\nBUILT=2026-11-09\nBUILT_BY=github\nMODIFIED=0\n")
        (t / "proc/device-tree/model").write_bytes(b"R36S\0")
        (t / "proc/cpuinfo").write_text("processor : 0\nprocessor : 1\n")
        (t / "proc/meminfo").write_text("MemTotal: 919232 kB\nMemAvailable: 848420 kB\n")
        (t / "proc/filesystems").write_text("nodev\tsysfs\n\text4\n\tvfat\n\texfat\n")
        (t / "proc/mounts").write_text(f"/dev/mmcblk0p3 {t / 'sys1'} exfat rw,noatime 0 0\n/dev/mmcblk1p1 {t / 'sys2'} exfat rw,noatime 0 0\n")
        (t / "sys/block/mmcblk0/size").write_text("249737216\n")
        (t / "sys/block/mmcblk1/size").write_text("62521344\n")
        (t / "sys/block/mmcblk1/removable").write_text("1\n")
        (t / "dev/loop-control").write_text("")
        self.cards(SYSTEM_DEV="/dev/mmcblk0p3", SYSTEM_FS="exfat", SYSTEM_AT=str(t / "sys1"), DATA_DEV="/dev/mmcblk1p1", DATA_FS="exfat", DATA_AT=str(t / "sys2"))
        (t / "bin/systemctl").write_text("#!/bin/sh\necho 'wpa_supplicant.service loaded failed failed WPA supplicant'\n")
        (t / "bin/systemd-analyze").write_text("#!/bin/sh\necho 'Startup finished in 3.1s (kernel) + 9.2s (userspace) = 12.3s'\n")
        for f in ("systemctl", "systemd-analyze"):
            (t / "bin" / f).chmod(0o755)
        self.env = {"R3I6NOS_ETC": str(t / "etc"), "R3I6NOS_PROC": str(t / "proc"), "R3I6NOS_SYS": str(t / "sys"), "R3I6NOS_DEV": str(t / "dev"),
                    "R3I6NOS_CARDS_STATE": str(t / "run/cards"), "R3I6NOS_REPORT_MB": "2", "PATH": f"{t / 'bin'}:{os.environ.get('PATH', '')}"}

    def tearDown(self):
        self.tmp.cleanup()

    def cards(self, **kv):
        st = {"BOOT_DISK": "mmcblk0", "SYSTEM_DEV": "none", "SYSTEM_FS": "none", "SYSTEM_AT": "none", "DATA_DEV": "none", "DATA_FS": "none", "DATA_AT": "none"}
        st.update(kv)
        (self.t / "run/cards").write_text("".join(f"{k}={v}\n" for k, v in st.items()))

    def report(self, **env):
        m = load(dict(self.env, **env))
        with mock.patch.dict(os.environ, {"PATH": self.env["PATH"]}), mock.patch("sys.stdout", io.StringIO()):
            self.assertEqual(m.main(["--from-settings", "--quiet"]), 0)
        return m

    def text(self, folder):
        path = Path(folder) / "r36s-report.txt"
        self.assertFalse(Path(str(path) + ".tmp").exists(), "renamed into place")
        text = path.read_text()
        lines = text.splitlines()
        self.assertEqual(lines[0], "# r3i6nos device report")
        self.assertEqual(lines[-1], "end")
        for ln in lines:
            if ln.startswith(("PASS ", "FAIL ", "INFO ", "SKIP ")) and not ln.startswith("PASS 0") and "  FAIL " not in ln:
                self.assertRegex(ln, LINE)
        return text

    def test_written_on_the_data_card_with_every_section(self):
        self.report()
        text = self.text(self.t / "sys2/r3i6nos")
        for want in ("app: r3i6nos 0.1.0 (r3i6nos)", "how: Settings > Device reports", "from the data card",
                     "== system ==", "INFO build: r3i6nos 0.1.0, commit 396fcd3000 (2026-11-09), built 2026-11-09 by github",
                     "INFO cpu: 2 CPUs", "INFO memory: total 919232 kB",
                     "== boot ==", "FAIL failed units: wpa_supplicant.service", "INFO boot time: 3.1s (kernel) + 9.2s (userspace) = 12.3s",
                     "== storage ==", "INFO disk mmcblk0: 127.9 GB, boot (system card)", "INFO disk mmcblk1: 32.0 GB, other (data card?), removable",
                     "PASS cards in the slots: 2", "INFO mmc hosts: mmc0, mmc1",
                     "PASS system card: /dev/mmcblk0p3 (exfat) on", "PASS data card: /dev/mmcblk1p1 (exfat) on",
                     "PASS kernel exfat: yes", "PASS kernel ext4: yes", "FAIL kernel squashfs: no", "PASS loop devices: yes",
                     "PASS system card write, rename over, read, delete: ok", "PASS data card write, rename over, read, delete: ok",
                     "INFO system card write speed:", "INFO data card read speed:", "== summary ==", "end"):
            self.assertIn(want, text)
        self.assertFalse(any(p.name.startswith(".r36s-report") for p in (self.t / "sys1/r3i6nos").iterdir()), "test files are deleted")
        self.assertFalse((self.t / "sys1/r3i6nos/r36s-report.txt").exists())

    def test_without_a_data_card_it_goes_on_the_system_card(self):
        self.cards(SYSTEM_DEV="/dev/mmcblk0p3", SYSTEM_FS="exfat", SYSTEM_AT=str(self.t / "sys1"))
        (self.t / "sys/block/mmcblk1/size").write_text("0\n")
        self.report()
        text = self.text(self.t / "sys1/r3i6nos")
        self.assertIn("from the system card", text)
        self.assertIn("INFO data card: none found", text)

    def test_no_cards_service_no_release_and_a_clean_boot(self):
        (self.t / "run/cards").unlink()
        (self.t / "etc/r3i6nos-release").unlink()
        (self.t / "bin/systemctl").write_text("#!/bin/sh\n")
        out = self.t / "out"
        self.report(R3I6NOS_REPORT_DIR=str(out))
        text = self.text(out)
        self.assertIn("FAIL r3i6nos-cards: no", text)
        self.assertIn("FAIL build: no /etc/r3i6nos-release", text)
        self.assertIn("PASS failed units: none", text)

    def test_a_card_found_but_not_mounted(self):
        self.cards(SYSTEM_DEV="/dev/mmcblk0p3", SYSTEM_FS="exfat", SYSTEM_AT=str(self.t / "sys1"), DATA_DEV="/dev/mmcblk1p1", DATA_FS="ntfs")
        self.report()
        self.assertIn("FAIL data card: /dev/mmcblk1p1 (ntfs) found but not mounted", self.text(self.t / "sys1/r3i6nos"))

    def test_progress_for_the_menu(self):
        prog = self.t / "progress"
        seen = []
        m = load(dict(self.env, R3I6NOS_REPORT_PROGRESS=str(prog)))
        real = m.Report.step
        def watch(r, doing):
            real(r, doing)
            seen.append(prog.read_text())
        with mock.patch.object(m.Report, "step", watch), mock.patch.dict(os.environ, {"PATH": self.env["PATH"]}), mock.patch("sys.stdout", io.StringIO()):
            self.assertEqual(m.main(["--from-settings", "--quiet"]), 0)
        steps = [dict(l.split("=", 1) for l in text.splitlines()) for text in seen]
        self.assertEqual([s["steps"] for s in steps], ["7"] * 8, "3 sections and 2 checks on each of the two cards, then done")
        self.assertEqual([s["step"] for s in steps], ["1", "2", "3", "4", "5", "6", "7", "7"])
        self.assertIn("speed", steps[4]["doing"])
        self.assertEqual(steps[-1]["doing"], "done")
        self.assertEqual(steps[-1]["file"], str(self.t / "sys2/r3i6nos/r36s-report.txt"))

    def test_no_progress_file_unless_asked(self):
        self.report()
        self.assertFalse((self.t / "progress").exists())

    def test_the_menu_finds_its_name(self):
        head = SRC.read_text().splitlines()[:40]
        self.assertTrue(any(re.match(r'^APP\s*=\s*"r3i6nos"', ln) for ln in head), "menu/reports.lua reads APP = \"...\" in the first 40 lines")


if __name__ == "__main__":
    unittest.main()

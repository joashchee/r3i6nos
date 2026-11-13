"""r3i6nos-report, the system's own device report (docs/STORAGE.md, docs/DEVICE-REPORTS.md), against a fake /etc, /proc, /sys and cards."""
import importlib.machinery
import importlib.util
import gzip
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


class Fake(unittest.TestCase):
    """A fake /etc, /proc, /sys and two cards, and helpers to run the report against them."""

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
        (t / "proc/uptime").write_text("754.31 1200.00\n")
        (t / "proc/filesystems").write_text("nodev\tsysfs\n\text4\n\tvfat\n\texfat\n")
        (t / "proc/mounts").write_text(f"/dev/mmcblk0p3 {t / 'sys1'} exfat rw,noatime 0 0\n/dev/mmcblk1p1 {t / 'sys2'} exfat rw,noatime 0 0\n")
        (t / "sys/block/mmcblk0/size").write_text("249737216\n")
        (t / "sys/block/mmcblk1/size").write_text("62521344\n")
        (t / "sys/block/mmcblk1/removable").write_text("1\n")
        (t / "dev/loop-control").write_text("")
        self.cards(SYSTEM_DEV="/dev/mmcblk0p3", SYSTEM_FS="exfat", SYSTEM_AT=str(t / "sys1"), DATA_DEV="/dev/mmcblk1p1", DATA_FS="exfat", DATA_AT=str(t / "sys2"))
        (t / "bin/systemctl").write_text("#!/bin/sh\ncase \"$1\" in\n"
                                         "  list-units) echo 'wpa_supplicant.service loaded failed failed WPA supplicant' ;;\n"
                                         "  is-system-running) echo degraded; exit 1 ;;\n"
                                         "  list-jobs) echo '12 r3i6nos-net.path start waiting' ;;\nesac\n")
        (t / "bin/systemd-analyze").write_text("#!/bin/sh\necho 'Startup finished in 3.1s (kernel) + 9.2s (userspace) = 12.3s'\n")
        (t / "bin/r3i6nos-sound").write_text("#!/bin/sh\necho 'cards: 1'\necho 'card 0 playback path: SPK (of OFF RCV SPK HP)'\n"
                                             "echo 'jack: rk817-sound Headphone Jack'\necho 'headphones: out'\n"
                                             "echo 'volume keys: none'\necho 'mixer Volume: 16 of 0-20, 80%, -8.0 dB'\n"
                                             "echo 'mixer Boost: 2 of 0-6, 33%, +6.0 dB'\n")
        (t / "bin/r3i6nos-clock").write_text("#!/bin/sh\necho 'now: 2026-11-09 21:30:00 +0800'\necho 'set by a person: 2026-11-09 21:29:40 +0800'\n"
                                             "echo 'rtc: none'\n")
        for f in ("systemctl", "systemd-analyze", "r3i6nos-sound", "r3i6nos-clock"):
            (t / "bin" / f).chmod(0o755)
        self.env = {"R3I6NOS_ETC": str(t / "etc"), "R3I6NOS_PROC": str(t / "proc"), "R3I6NOS_SYS": str(t / "sys"), "R3I6NOS_DEV": str(t / "dev"),
                    "R3I6NOS_CARDS_STATE": str(t / "run/cards"), "R3I6NOS_REPORT_MB": "2", "R3I6NOS_SOUND": str(t / "bin/r3i6nos-sound"), "R3I6NOS_CLOCK": str(t / "bin/r3i6nos-clock"),
                    "R3I6NOS_BOOTSTATE": str(t / "run/boot-state"), "R3I6NOS_REPORT_POWER_S": "0.3", "PATH": f"{t / 'bin'}:{os.environ.get('PATH', '')}"}

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


class SystemReport(Fake):
    def test_setup_section_says_how_the_card_was_made(self):
        t = self.t
        for d in ("var", "boot"):
            (t / d).mkdir(exist_ok=True)
        env = {"R3I6NOS_SETUP_VAR": str(t / "var"), "R3I6NOS_SETUP_RUN": str(t / "run"), "R3I6NOS_SETUP_BOOT": str(t / "boot")}
        self.report(**env)
        text = self.text(self.t / "sys2/r3i6nos")
        self.assertIn("== setup ==", text)
        self.assertIn("SKIP made by r3i6nos-setup: no record", text)
        (t / "var/setup-done").write_text("DATE=2026-11-12T09:00:00\nVERSION=0.1.0\nSOURCE_ID=12345678\nBACKUP=none\n")
        (t / "run/setup-state").write_text("mode=normal\nwhy=mmcblk0 has its own bootloader\n")
        (t / "boot/r3i6nos-setup-report.txt").write_text("PASS 12  FAIL 0  INFO 4  SKIP 1\nend\n")
        self.report(**env)
        text = self.text(self.t / "sys2/r3i6nos")
        self.assertIn("INFO made by r3i6nos-setup: 2026-11-12T09:00:00, build 0.1.0, original card id 12345678", text)
        self.assertIn("INFO backup of the original: none", text)
        self.assertIn("INFO this boot: normal, mmcblk0 has its own bootloader", text)
        self.assertIn("PASS setup report on BOOT: 12 PASS, 0 FAIL", text)

    def test_written_on_the_data_card_with_every_section(self):
        self.report()
        text = self.text(self.t / "sys2/r3i6nos")
        for want in ("app: r3i6nos 0.1.0 (r3i6nos)", "how: Settings > Device reports", "from the data card",
                     "== system ==", "INFO build: r3i6nos 0.1.0, commit 396fcd3000 (2026-11-09), built 2026-11-09 by github",
                     "INFO cpu: 2 CPUs", "INFO memory: total 919232 kB",
                     "== boot ==", "FAIL failed units: wpa_supplicant.service", "FAIL system state: degraded", "INFO jobs waiting: r3i6nos-net.path",
                     "INFO boot time: 3.1s (kernel) + 9.2s (userspace) = 12.3s", "INFO up for: 12.6 min",
                     "== sound ==", "PASS card 0 playback path: SPK (of OFF RCV SPK HP)", "INFO jack: rk817-sound Headphone Jack",
                     "INFO headphones: out", "INFO volume keys: none", "PASS mixer Volume: 16 of 0-20, 80%, -8.0 dB",
                     "PASS mixer Boost: 2 of 0-6, 33%, +6.0 dB", "INFO heard: not measured",
                     "== clock ==", "INFO now: 2026-11-09 21:30:00 +0800", "PASS set by a person: 2026-11-09 21:29:40", "INFO rtc: none",
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

    NO_BUS = "#!/bin/sh\necho 'Failed to connect to bus: No such file or directory' >&2\nexit 1\n"

    def test_no_bus_reads_the_boot_state(self):
        # From Settings the report runs as loveos, and with no D-Bus systemctl cannot reach systemd (the unit, 2026-11-09)
        (self.t / "bin/systemctl").write_text(self.NO_BUS)
        (self.t / "bin/systemd-analyze").write_text(self.NO_BUS)
        (self.t / "run/boot-state").write_text("written: 2026-11-09 22:29:00 +0800\nup: 1080.2 s\nfailed units: none\nsystem state: running\n"
                                               "jobs waiting: none\nboot time: Startup finished in 3.1s (kernel) + 9.2s (userspace) = 12.3s\n"
                                               "unit r3i6nos-sound.service: active\n")
        self.report()
        text = self.text(self.t / "sys2/r3i6nos")
        for want in ("INFO boot facts from:", "written 2026-11-09 22:29:00 +0800", "PASS failed units: none", "PASS system state: running",
                     "PASS jobs waiting: none", "INFO boot time: 3.1s (kernel) + 9.2s (userspace) = 12.3s", "INFO jack follower: active"):
            self.assertIn(want, text)

    def test_no_bus_and_a_failed_unit_in_the_boot_state(self):
        (self.t / "bin/systemctl").write_text(self.NO_BUS)
        (self.t / "run/boot-state").write_text("written: x\nfailed units: a.service b.service\nsystem state: degraded\njobs waiting: none\n"
                                               "boot time: Bootup is not yet finished\n")
        self.report()
        text = self.text(self.t / "sys2/r3i6nos")
        self.assertIn("FAIL failed units: a.service, b.service", text)
        self.assertIn("FAIL system state: degraded", text)
        self.assertIn("SKIP boot time: Bootup is not yet finished", text)

    def test_no_bus_and_no_boot_state_yet(self):
        (self.t / "bin/systemctl").write_text(self.NO_BUS)
        self.report()
        text = self.text(self.t / "sys2/r3i6nos")
        self.assertIn("SKIP boot: systemctl answers only root here (no D-Bus)", text)
        self.assertIn("not written yet", text)

    def test_no_cards_service_no_release_and_a_clean_boot(self):
        (self.t / "run/cards").unlink()
        (self.t / "etc/r3i6nos-release").unlink()
        (self.t / "bin/systemctl").write_text("#!/bin/sh\ncase \"$1\" in is-system-running) echo running ;; esac\n")
        (self.t / "bin/systemd-analyze").write_text("#!/bin/sh\necho 'Bootup is not yet finished (org.freedesktop.systemd1.Manager.FinishTimestampMonotonic=0).' >&2\nexit 1\n")
        out = self.t / "out"
        self.report(R3I6NOS_REPORT_DIR=str(out))
        text = self.text(out)
        self.assertIn("PASS system state: running", text)
        self.assertIn("PASS jobs waiting: none", text)
        self.assertIn("SKIP boot time: systemd-analyze: Bootup is not yet finished", text, "says why")
        self.assertIn("FAIL r3i6nos-cards: no", text)
        self.assertIn("FAIL build: no /etc/r3i6nos-release", text)
        self.assertIn("PASS failed units: none", text)

    def test_sound_off_is_a_fail(self):
        (self.t / "bin/r3i6nos-sound").write_text("#!/bin/sh\necho 'cards: 1'\necho 'card 0 playback path: OFF (of OFF SPK HP)'\necho 'jack: none'\n")
        self.report()
        text = self.text(self.t / "sys2/r3i6nos")
        self.assertIn("FAIL card 0 playback path: OFF (of OFF SPK HP)", text)
        self.assertIn("INFO jack: none", text)
        self.assertIn("FAIL mixer Volume: missing", text)

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
        self.assertEqual([s["steps"] for s in steps], ["15"] * 16, "5 sections, 2 checks on each of the two cards, 2 in usb and 4 in power, then done")
        self.assertEqual([s["step"] for s in steps], [str(i) for i in range(1, 16)] + ["15"])
        self.assertIn("speed", steps[6]["doing"])
        self.assertEqual(steps[-1]["doing"], "done")
        self.assertEqual(steps[-1]["file"], str(self.t / "sys2/r3i6nos/r36s-report.txt"))

    def test_no_progress_file_unless_asked(self):
        self.report()
        self.assertFalse((self.t / "progress").exists())

    def test_each_run_of_a_several_run_report_has_its_own_file(self):
        m = load(dict(self.env))
        for k in (1, 2):
            with mock.patch.dict(os.environ, {"PATH": self.env["PATH"], "R3I6NOS_REPORT_SETUP": f"{k}/3"}), mock.patch("sys.stdout", io.StringIO()):
                self.assertEqual(m.main(["--from-settings", "--quiet"]), 0)
        folder = self.t / "sys2/r3i6nos"
        for k in (1, 2):
            text = (folder / f"r36s-report-run{k}.txt").read_text()
            self.assertIn(f"run: {k} of 3 (one report for each setup: give Claude all 3 files)", text)
            self.assertIn(f"Copy r3i6nos/r36s-report-run{k}.txt from the data card", text)
            self.assertEqual(text.splitlines()[-1], "end")
        self.assertFalse((folder / "r36s-report-run3.txt").exists())
        self.assertEqual((folder / "r36s-report.txt").read_text(), (folder / "r36s-report-run2.txt").read_text(), "the plain file is a copy of the latest run")
        self.assertEqual([p.name for p in folder.iterdir() if p.name.endswith(".tmp")], [], "no temporary file is left")

    def test_a_bad_setup_variable_is_a_plain_report(self):
        m = load(dict(self.env))
        for bad in ("0/3", "4/3", "1/1", "x", "2/30"):
            with mock.patch.dict(os.environ, {"PATH": self.env["PATH"], "R3I6NOS_REPORT_SETUP": bad}), mock.patch("sys.stdout", io.StringIO()):
                self.assertEqual(m.main(["--from-settings", "--quiet"]), 0)
        self.assertEqual(sorted(p.name for p in (self.t / "sys2/r3i6nos").iterdir()), ["r36s-report.txt"])

    def test_the_menu_finds_its_name(self):
        head = SRC.read_text().splitlines()[:80]   # menu/reports.lua M.HEAD
        self.assertTrue(any(re.match(r'^APP\s*=\s*"r3i6nos"', ln) for ln in head), "menu/reports.lua reads APP = \"...\" in the first 80 lines")


class PowerSection(Fake):
    """The power section (docs/POWER.md): fake /proc processes, a fake battery, and the root-only stop-and-compare with os.kill mocked."""

    def proc(self, pid, name, cmdline, ticks=0, ctx=0, rss=1000):
        d = self.t / "proc" / str(pid)
        d.mkdir(parents=True, exist_ok=True)
        (d / "cmdline").write_bytes(cmdline.replace(" ", "\0").encode())
        fields = ["S"] + ["0"] * 10 + [str(ticks), "0"] + ["0"] * 20
        (d / "stat").write_text(f"{pid} ({name}) " + " ".join(fields) + "\n")
        (d / "status").write_text(f"Name:\t{name}\nVmRSS:\t{rss} kB\nvoluntary_ctxt_switches:\t{ctx}\nnonvoluntary_ctxt_switches:\t0\n")

    def battery(self, status="Discharging", ua=300000, uv=3900000):
        b = self.t / "sys/class/power_supply/rk817-battery"
        b.mkdir(parents=True, exist_ok=True)
        for k, v in (("type", "Battery"), ("status", status), ("capacity", "80"), ("current_now", -ua), ("voltage_now", uv), ("charge_full", 3000000)):
            (b / k).write_text(f"{v}\n")

    def power_text(self, **env):
        self.report(**env)
        text = self.text(self.t / "sys2/r3i6nos")
        return text[text.index("== power =="):]

    def test_pct_is_ticks_over_seconds(self):
        m = load(self.env)
        self.assertAlmostEqual(m.pct({"ticks": 0}, {"ticks": m.CLK}, 10.0), 10.0)
        self.assertEqual(m.pct({"ticks": 5}, {"ticks": 3}, 10.0), 0)
        self.assertEqual(m.pct({"ticks": 0}, {"ticks": 9}, 0), 0.0)

    def test_no_battery_no_daemon(self):
        p = self.power_text()
        self.assertIn("SKIP battery: no battery supply found", p)
        self.assertIn("FAIL hotplug daemon: r3i6nos-hotplug start is not running", p)
        self.assertIn("SKIP menu: no process matching /opt/loveos/menu", p)
        self.assertIn("SKIP job clock save", p)
        self.assertIn("SKIP hotplug draw: needs the battery discharging", p)

    def test_daemon_within_its_goals_and_the_menu_measured(self):
        self.proc(300, "python3", "python3 /usr/local/bin/r3i6nos-hotplug start", ticks=10, ctx=50, rss=9000)
        self.proc(301, "love", "/opt/loveos/bin/love /opt/loveos/menu", ticks=500, ctx=900, rss=40000)
        p = self.power_text()
        self.assertIn("PASS hotplug cpu: 0.00% of a core (goal at most 1.0%)", p)
        self.assertIn("PASS hotplug wakeups: 0.0/s (goal at most 4.0", p)
        self.assertIn("INFO hotplug memory: 9000 kB resident", p)
        self.assertIn("INFO menu cpu: 0.0% of a core while showing, 40000 kB resident", p)
        self.assertNotIn("FAIL", p.split("== summary ==")[0])

    def test_a_busy_daemon_fails_its_goal(self):
        self.proc(300, "python3", "python3 r3i6nos-hotplug start", ticks=0, ctx=0)
        m = load(self.env)
        real = m.proc_snap
        calls = {"n": 0}

        def busy(pid):
            s = real(pid)
            if s and pid == 300:
                calls["n"] += 1
                s["ticks"] += (calls["n"] - 1) * 100 * m.CLK     # 100 s of CPU in the window: far over any goal
                s["ctx"] += (calls["n"] - 1) * 5000
            return s

        with mock.patch.object(m, "proc_snap", busy), mock.patch.dict(os.environ, {"PATH": self.env["PATH"]}), mock.patch("sys.stdout", io.StringIO()):
            self.assertEqual(m.main(["--from-settings", "--quiet"]), 0)
        p = self.text(self.t / "sys2/r3i6nos")
        self.assertRegex(p, r"FAIL hotplug cpu: [0-9.]+% of a core \(goal at most 1.0%\)")
        self.assertRegex(p, r"FAIL hotplug wakeups: ")

    def test_charging_says_to_unplug(self):
        self.battery(status="Charging")
        p = self.power_text()
        self.assertIn("INFO battery supply: rk817-battery: 80%, Charging", p)
        self.assertIn("SKIP draw: the battery says Charging, not Discharging", p)
        self.assertNotIn("INFO idle draw", p)

    def test_discharging_gives_the_draw_in_mw(self):
        self.battery()
        p = self.power_text()
        self.assertIn("INFO current_now: 300 mA at 3.90 V", p)
        self.assertIn("INFO idle draw: 1170 mW mean", p)
        self.assertIn("INFO idle time left:", p)
        self.assertIn("SKIP hotplug draw: the daemon is not running", p)

    def test_stop_and_compare_always_continues_the_daemon(self):
        self.battery()
        self.proc(300, "python3", "python3 r3i6nos-hotplug start", ticks=0, ctx=0)
        m = load(self.env)
        sent = []
        with mock.patch.object(m.os, "kill", lambda pid, sig: sent.append((pid, sig))), mock.patch.object(m.os, "geteuid", lambda: 0), \
                mock.patch.dict(os.environ, {"PATH": self.env["PATH"]}), mock.patch("sys.stdout", io.StringIO()):
            self.assertEqual(m.main(["--from-settings", "--quiet"]), 0)
        self.assertEqual([s for _, s in sent], [m.signal.SIGSTOP, m.signal.SIGCONT])
        p = self.text(self.t / "sys2/r3i6nos")
        self.assertIn("INFO draw running / stopped / running: 1170 / 1170 / 1170 mW", p)
        self.assertIn("PASS hotplug draw: about 0 mW", p)

    def test_the_daemon_is_continued_even_if_the_measuring_crashes(self):
        self.battery()
        self.proc(300, "python3", "python3 r3i6nos-hotplug start", ticks=0, ctx=0)
        m = load(self.env)
        sent = []
        calls = {"n": 0}
        real = m.measure

        def boom(seconds, pids, b=None):
            calls["n"] += 1
            if calls["n"] == 2:
                raise RuntimeError("measuring broke")
            return real(seconds, pids, b)

        with mock.patch.object(m.os, "kill", lambda pid, sig: sent.append(sig)), mock.patch.object(m.os, "geteuid", lambda: 0), mock.patch.object(m, "measure", boom), \
                mock.patch.dict(os.environ, {"PATH": self.env["PATH"]}), mock.patch("sys.stdout", io.StringIO()):
            self.assertEqual(m.main(["--from-settings", "--quiet"]), 0)
        self.assertEqual(sent, [m.signal.SIGSTOP, m.signal.SIGCONT])
        self.assertIn("FAIL report: a check crashed: measuring broke", self.text(self.t / "sys2/r3i6nos"))

    def test_not_root_does_not_try_to_stop_it(self):
        self.battery()
        self.proc(300, "python3", "python3 r3i6nos-hotplug start", ticks=0, ctx=0)
        m = load(self.env)
        with mock.patch.object(m.os, "kill", side_effect=AssertionError("must not signal")), mock.patch.object(m.os, "geteuid", lambda: 1000), \
                mock.patch.dict(os.environ, {"PATH": self.env["PATH"]}), mock.patch("sys.stdout", io.StringIO()):
            self.assertEqual(m.main(["--from-settings", "--quiet"]), 0)
        self.assertIn("SKIP hotplug draw: stopping a root service needs root", self.text(self.t / "sys2/r3i6nos"))

    def test_periodic_jobs_are_costed_with_throwaway_state(self):
        sample = self.t / "bin/r3i6nos-battery"
        sample.write_text('#!/bin/sh\n[ -n "$R3I6NOS_BATTERY_DIR" ] && touch "$R3I6NOS_BATTERY_DIR/ran"\n')
        sample.chmod(0o755)
        boot = self.t / "bin/r3i6nos-bootstate"
        boot.write_text('#!/bin/sh\n[ -n "$R3I6NOS_BOOTSTATE" ] && echo x > "$R3I6NOS_BOOTSTATE"\n')
        boot.chmod(0o755)
        p = self.power_text(R3I6NOS_BATTERY=str(sample), R3I6NOS_BOOTSTATE_CMD=str(boot))
        self.assertRegex(p, r"INFO job battery sample tick: \d+ ms CPU, \d+ ms wall a run, 4 runs an hour = [0-9.]+% of a core")
        self.assertRegex(p, r"INFO job bootstate: ")
        self.assertRegex(p, r"PASS jobs cpu: [0-9.]+% of a core averaged, for battery sample tick, bootstate")
        self.assertFalse((self.t / "run/boot-state").exists(), "the real boot state is not touched")

    def test_the_power_section_is_in_the_progress_steps(self):
        self.report()
        self.assertIn("== power ==", self.text(self.t / "sys2/r3i6nos"))


class NeedsDialog(unittest.TestCase):
    def head(self):
        return SRC.read_text().splitlines()[:80]   # the menu reads the first 80 lines (menu/reports.lua M.HEAD)

    def test_the_report_declares_its_runs_and_setups_for_the_menu(self):
        head = self.head()
        self.assertIn("# RUNS: 3", head)
        runs = {}
        for ln in head:
            m = re.match(r"# RUN (\d): ?(.*)$", ln)
            if m:
                runs.setdefault(int(m.group(1)), []).append(m.group(2))
        self.assertEqual(sorted(runs), [1, 2, 3], "a setup for each of the three runs")
        self.assertIn("OTG", " ".join(runs[1])); self.assertIn("charger", " ".join(runs[1]))
        self.assertIn("Ethernet adapter", " ".join(runs[2]))
        self.assertIn("USB cable", " ".join(runs[3]))
        needs = [ln[len("# NEEDS:"):].lstrip(" ") for ln in head if ln.startswith("# NEEDS:")]
        every = [x for v in runs.values() for x in v] + needs
        # the dialog has 60 columns of room (menu/view.lua FULL.w - 4); a line that is too long would be cut
        self.assertTrue(all(len(x) <= 56 for x in every), [x for x in every if len(x) > 56])
        self.assertTrue(any(ln.startswith('APP = "r3i6nos"') for ln in head), "APP must stay inside the lines the menu reads")


class UsbSections(Fake):
    """== usb gadget == and == usb ethernet ==: the wired mirror's questions, asked of a fake /proc and /sys (ROADMAP section 28)."""

    def put(self, rel, text=""):
        p = self.t / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
        return p

    def link(self, rel, target):
        p = self.t / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        os.symlink(target, p)

    def config(self, *opts, where="proc/config.gz"):
        text = "".join(f"CONFIG_{o}\n" if "=" in o else f"# CONFIG_{o} is not set\n" for o in opts)
        p = self.t / where
        p.parent.mkdir(parents=True, exist_ok=True)
        if where.endswith(".gz"):
            p.write_bytes(gzip.compress(text.encode()))
        else:
            p.write_text(text)

    def run_usb(self):
        self.report()
        return self.text(self.t / "sys2/r3i6nos")

    def gadget_ready(self):
        self.put("proc/sys/kernel/osrelease", "4.4.189\n")
        self.config("USB_GADGET=y", "USB_LIBCOMPOSITE=y", "USB_CONFIGFS=y", "USB_CONFIGFS_ECM=y", "USB_CONFIGFS_NCM=y", "CONFIGFS_FS=y")
        self.put("proc/filesystems", "nodev\tsysfs\nnodev\tconfigfs\n\text4\n")
        self.put("sys/class/udc/ff300000.usb/state", "not attached\n")
        self.put("sys/class/udc/ff300000.usb/current_speed", "UNKNOWN\n")
        self.put("proc/device-tree/usb@ff300000/dr_mode", "otg\0")
        (self.t / "proc/device-tree/usb@ff300000/compatible").write_bytes(b"rockchip,rk3326-usb\0snps,dwc2\0")

    def test_nothing_readable_proves_nothing(self):
        text = self.run_usb()
        for want in ("== usb gadget ==", "== usb ethernet ==", "SKIP kernel config: none readable", "SKIP usb controller: no dwc2",
                     "SKIP device-mode controller:", "SKIP gadget ethernet: not decided", "SKIP usb ethernet adapter:", "SKIP usb devices: no USB bus",
                     "INFO wired mirror verdict: undecided"):
            self.assertIn(want, text)
        self.assertNotIn("FAIL gadget", text)
        self.assertNotIn("FAIL usb ethernet adapter", text)

    def test_an_empty_usb_bus_says_to_plug_something_in(self):
        (self.t / "sys/bus/usb/devices").mkdir(parents=True)
        self.assertIn("SKIP usb devices: none plugged in", self.run_usb())

    def test_neither_wire_works_when_the_config_rules_both_out(self):
        self.put("proc/sys/kernel/osrelease", "4.4.189\n")
        self.config("USB_DWC2=y")
        text = self.run_usb()
        self.assertIn("FAIL gadget ethernet: cannot work as the kernel is", text)
        self.assertIn("FAIL usb ethernet adapter: no adapter driver in the kernel config", text)
        self.assertIn("INFO wired mirror verdict: neither wire works", text)

    def test_a_gadget_ready_kernel_with_the_link_up(self):
        self.gadget_ready()
        self.put("sys/kernel/config/usb_gadget/r3i6nos/idVendor", "0x1d6b\n")
        real = self.t / "sys/devices/platform/ff300000.usb/gadget"
        real.mkdir(parents=True)
        self.link("sys/class/net/usb0/device", real)
        for f, v in (("operstate", "up"), ("carrier", "1"), ("speed", "480"), ("address", "02:00:00:00:00:01")):
            self.put(f"sys/class/net/usb0/{f}", v + "\n")
        text = self.run_usb()
        for want in ("INFO kernel config: ", "INFO usb controller usb@ff300000: rockchip,rk3326-usb, snps,dwc2; dr_mode otg; status okay",
                     "INFO udc ff300000.usb: state not attached", "PASS device-mode controller: registered: ff300000.usb",
                     "PASS configfs: configfs is in /proc/filesystems", "INFO gadgets set up now: r3i6nos", "PASS composite gadget core:",
                     "PASS function ECM: CONFIG_USB_CONFIGFS_ECM=y (works with macOS and Linux", "PASS function NCM:", "FAIL function RNDIS: CONFIG_USB_CONFIGFS_RNDIS is not set",
                     "PASS gadget ethernet: ready to try: a UDC, configfs and ECM, NCM",
                     "INFO network usb0: wired (this unit as a USB gadget); driver none; up; carrier 1; 480 Mb/s", "PASS wired link usb0: up",
                     "INFO wired mirror verdict: use the gadget: one cable"):
            self.assertIn(want, text)

    def test_a_host_only_device_tree_kills_the_gadget(self):
        self.put("proc/sys/kernel/osrelease", "4.4.189\n")
        self.config("USB_GADGET=y", "USB_CONFIGFS_ECM=y")
        self.put("proc/device-tree/usb@ff300000/dr_mode", "host\0")
        (self.t / "proc/device-tree/usb@ff300000/compatible").write_bytes(b"snps,dwc2\0")
        text = self.run_usb()
        self.assertIn("FAIL device-mode controller: the device tree sets the controller to dr_mode host", text)
        self.assertIn("FAIL gadget ethernet: cannot work as the kernel is: missing device-mode controller", text)
        self.assertIn("INFO wired mirror verdict: neither wire works", text)   # the readable config has no adapter driver either

    def test_a_config_without_gadget_fails_but_no_config_never_does(self):
        self.put("proc/sys/kernel/osrelease", "4.4.189\n")
        self.config("USB_DWC2=y", "CONFIGFS_FS=y", where="boot/config-4.4.189")
        self.env["R3I6NOS_SETUP_BOOT"] = str(self.t / "boot")
        text = self.run_usb()
        self.assertIn("INFO kernel config: " + str(self.t / "boot/config-4.4.189"), text)
        self.assertIn("FAIL device-mode controller: CONFIG_USB_GADGET is not set", text)
        self.assertIn("FAIL function ECM: CONFIG_USB_CONFIGFS_ECM is not set", text)
        self.assertIn("FAIL gadget ethernet: cannot work as the kernel is: missing device-mode controller", text)

    def test_a_module_config_with_no_module_file_is_a_fail_and_with_one_a_pass(self):
        self.put("proc/sys/kernel/osrelease", "4.4.189\n")
        self.config("USB_CONFIGFS_ECM=m", "USB_USBNET=m", "USB_NET_AX8817X=m")
        self.env["R3I6NOS_MODULES"] = str(self.t / "lib/modules")
        self.put("lib/modules/4.4.189/kernel/drivers/net/usb/asix.ko")
        text = self.run_usb()
        self.assertIn("FAIL function ECM: CONFIG_USB_CONFIGFS_ECM=m but no such module file", text)
        self.assertIn("PASS host driver asix: module file asix.ko", text)
        self.assertIn("INFO host driver usbnet: CONFIG_USB_USBNET=m but no such module file", text)

    def test_symbols_prove_what_is_built_in_but_never_an_absence(self):
        self.put("proc/kallsyms", "0000000000000000 t ecm_alloc_inst\n0000000000000000 T usb_composite_probe\n")
        text = self.run_usb()
        self.assertIn("PASS function ECM: kernel symbol ecm_alloc_inst", text)
        self.assertIn("PASS composite gadget core: kernel symbol usb_composite_probe", text)
        self.assertIn("SKIP function NCM: nothing readable says", text)
        self.assertNotIn("FAIL function", text)

    def test_a_usb_adapter_with_its_driver_and_the_link_up(self):
        self.put("sys/bus/usb/drivers/asix/.keep")
        self.put("sys/bus/usb/drivers/usbnet/.keep")
        self.put("sys/bus/usb/devices/1-1/idVendor", "0b95\n")
        self.put("sys/bus/usb/devices/1-1/idProduct", "772b\n")
        self.put("sys/bus/usb/devices/1-1/product", "AX88772B\n")
        self.put("sys/bus/usb/devices/1-1/manufacturer", "ASIX Elec. Corp.\n")
        self.put("sys/bus/usb/devices/1-1/speed", "480\n")
        self.put("sys/bus/usb/devices/1-1:1.0/bInterfaceClass", "ff\n")
        self.link("sys/bus/usb/devices/1-1:1.0/driver", self.t / "sys/bus/usb/drivers/asix")
        self.put("sys/bus/usb/devices/usb1/idVendor", "1d6b\n")   # the root hub is not listed
        real = self.t / "sys/devices/platform/ff580000.usb/usb1/1-1/1-1:1.0"
        real.mkdir(parents=True)
        os.symlink(self.t / "sys/bus/usb/drivers/asix", real / "driver")
        self.link("sys/class/net/eth0/device", real)
        for f, v in (("operstate", "up"), ("carrier", "1"), ("speed", "100"), ("address", "00:11:22:33:44:55")):
            self.put(f"sys/class/net/eth0/{f}", v + "\n")
        text = self.run_usb()
        for want in ("PASS host driver asix: registered USB driver", "PASS usb ethernet adapter: drivers present: asix",
                     "INFO usb device 1-1: 0b95:772b ASIX Elec. Corp. AX88772B; 480 Mb/s; driver asix",
                     "INFO network eth0: wired (a USB adapter); driver asix; up; carrier 1; 100 Mb/s; mac 00:11:22:33:44:55",
                     "PASS wired link eth0: up (wired (a USB adapter), 100 Mb/s)"):
            self.assertIn(want, text)
        self.assertNotIn("usb device usb1", text)
        self.assertIn("INFO wired mirror verdict: use a USB Ethernet adapter", text)

    def test_an_adapter_with_no_driver_is_a_fail_that_names_what_to_do(self):
        self.put("sys/bus/usb/devices/1-1/idVendor", "0bda\n")
        self.put("sys/bus/usb/devices/1-1/idProduct", "8153\n")
        self.put("sys/bus/usb/devices/1-1:1.0/bInterfaceClass", "ff\n")
        self.put("sys/bus/usb/devices/1-2/idVendor", "046d\n")      # a keyboard is not an adapter
        self.put("sys/bus/usb/devices/1-2:1.0/bInterfaceClass", "03\n")
        text = self.run_usb()
        self.assertIn("FAIL usb device 1-1: 0bda:8153", text)
        self.assertIn("interface class ff has no driver", text)
        self.assertIn("INFO usb device 1-2: 046d:", text)

    def test_wifi_is_told_apart_and_a_dead_gadget_link_is_not_a_pass(self):
        self.put("sys/class/net/wlan0/operstate", "up\n")
        self.put("sys/class/net/wlan0/phy80211/index", "0\n")
        real = self.t / "sys/devices/platform/ff300000.usb/gadget"
        real.mkdir(parents=True)
        self.link("sys/class/net/usb0/device", real)
        self.put("sys/class/net/usb0/carrier", "0\n")
        text = self.run_usb()
        self.assertIn("INFO network wlan0: wifi;", text)
        self.assertNotIn("PASS wired link", text)
        self.assertIn("INFO wired link usb0: the gadget exists but nothing is connected", text)

    def test_a_crashing_gadget_check_still_runs_the_adapter_checks(self):
        m = load(dict(self.env))
        with mock.patch.object(m, "usb_gadget", side_effect=RuntimeError("boom")), mock.patch.object(m, "usb_ethernet") as eth:
            r = mock.Mock()
            m.usb_sections(r)
        r.line.assert_called_once_with("FAIL", "usb gadget", "a check crashed: boom")
        eth.assert_called_once_with(r, "unknown")


if __name__ == "__main__":
    unittest.main()

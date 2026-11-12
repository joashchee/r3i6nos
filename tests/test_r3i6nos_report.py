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
        self.assertEqual([s["steps"] for s in steps], ["13"] * 14, "5 sections, 2 checks on each of the two cards and 4 in power, then done")
        self.assertEqual([s["step"] for s in steps], [str(i) for i in range(1, 14)] + ["13"])
        self.assertIn("speed", steps[6]["doing"])
        self.assertEqual(steps[-1]["doing"], "done")
        self.assertEqual(steps[-1]["file"], str(self.t / "sys2/r3i6nos/r36s-report.txt"))

    def test_no_progress_file_unless_asked(self):
        self.report()
        self.assertFalse((self.t / "progress").exists())

    def test_the_menu_finds_its_name(self):
        head = SRC.read_text().splitlines()[:40]
        self.assertTrue(any(re.match(r'^APP\s*=\s*"r3i6nos"', ln) for ln in head), "menu/reports.lua reads APP = \"...\" in the first 40 lines")


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


if __name__ == "__main__":
    unittest.main()

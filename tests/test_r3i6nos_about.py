"""r3i6nos-about (Settings > About) against a fake /etc, /proc, /sys and a fake GitHub on 127.0.0.1. Nothing here uses the real network."""
import http.server
import io
import importlib.machinery
import importlib.util
import json
import os
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

SRC = Path(__file__).resolve().parent.parent / "overlay/usr/local/bin/r3i6nos-about"
COMMIT = "278a84f" + "0" * 33


def load(env):
    with mock.patch.dict(os.environ, env):
        loader = importlib.machinery.SourceFileLoader("r3i6nos_about", str(SRC))
        spec = importlib.util.spec_from_loader("r3i6nos_about", loader)
        mod = importlib.util.module_from_spec(spec)
        sys.dont_write_bytecode = True
        loader.exec_module(mod)
    return mod


class FakeGitHub(http.server.BaseHTTPRequestHandler):
    routes = {}   # path -> (status, body)
    seen = []

    def do_GET(self):
        FakeGitHub.seen.append((self.path, self.headers.get("User-Agent")))
        status, body = FakeGitHub.routes.get(self.path, (404, {"message": "Not Found"}))
        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *a):
        pass


class About(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), FakeGitHub)
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        cls.api = f"http://127.0.0.1:{cls.server.server_address[1]}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.t = Path(self.tmp.name)
        for d in ("etc", "proc/device-tree", "sys/devices/system/cpu/cpu0/cpufreq", "sys/class/thermal/thermal_zone0",
                  "love/11.5/bin", "love/12.0/bin", "native/Commander", "native/Composer", "tools", "roms", "gpu"):
            (t / d).mkdir(parents=True)
        self.release(VERSION="0.1.0", COMMIT=COMMIT, COMMIT_DATE="2026-11-09", MODIFIED="0", BUILT="2026-11-09",
                     BUILT_BY="github", REPO="joashchee/r3i6nos")
        (t / "etc/debian_version").write_text("12.7\n")
        (t / "etc/hostname").write_text("r3i6nos\n")
        (t / "proc/device-tree/model").write_bytes(b"Rockchip RK3326 R36S\0")
        (t / "proc/uptime").write_text("3725.5 100.0\n")
        (t / "proc/cpuinfo").write_text("processor\t: 0\n\nprocessor\t: 1\n\nprocessor\t: 2\n\nprocessor\t: 3\n")
        (t / "proc/meminfo").write_text("MemTotal:        1009000 kB\nMemFree:  100 kB\nMemAvailable:     612000 kB\n")
        (t / "proc/mounts").write_text(f"/dev/mmcblk1p3 {t / 'roms'} exfat rw 0 0\n/dev/root / ext4 rw 0 0\n")
        (t / "sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq").write_text("1512000\n")
        (t / "sys/class/thermal/thermal_zone0/temp").write_text("48125\n")
        for v in ("11.5", "12.0"):
            (t / f"love/{v}/bin/love").write_text("")
        for n in ("compose", "compose-report", "sprites"):
            (t / "tools" / n).write_text("")
        (t / "bin").mkdir()
        # fake ip and systemctl on PATH: one address, SSH off
        (t / "bin/ip").write_text("#!/bin/sh\necho '1: lo    inet 127.0.0.1/8 scope host lo'\necho '3: wlan0    inet 192.168.1.20/24 brd x'\n")
        (t / "bin/systemctl").write_text("#!/bin/sh\necho inactive\nexit 3\n")
        for f in ("ip", "systemctl"):
            (t / "bin" / f).chmod(0o755)
        self.env = {
            "R3I6NOS_ETC": str(t / "etc"), "R3I6NOS_PROC": str(t / "proc"), "R3I6NOS_SYS": str(t / "sys"), "R3I6NOS_ROOT": str(t),
            "R3I6NOS_ROMS": str(t / "roms"), "R3I6NOS_LOVE_DIR": str(t / "love"), "R3I6NOS_NATIVE_DIR": str(t / "native"),
            "R3I6NOS_TOOLS_DIR": str(t / "tools"), "R3I6NOS_GPU_DIR": str(t / "gpu"), "R3I6NOS_UPDATE_API": self.api,
            "R3I6NOS_UPDATE_TIMEOUT": "5", "PATH": f"{t / 'bin'}:{os.environ.get('PATH', '')}",
        }
        self.m = load(self.env)
        FakeGitHub.routes, FakeGitHub.seen = {}, []

    def tearDown(self):
        self.tmp.cleanup()

    def release(self, **kv):
        (self.t / "etc/r3i6nos-release").write_text("".join(f"{k}={v}\n" for k, v in kv.items()))

    def run_check(self):
        out = self.t / "check.txt"
        with mock.patch.dict(os.environ, {"PATH": self.env["PATH"]}):
            self.assertEqual(self.m.main(["r3i6nos-about", "check", str(out)]), 0)
        text = out.read_text()
        self.assertFalse((self.t / "check.txt.tmp").exists(), "the answer is renamed into place, not left half-written")
        first, _, rest = text.partition("\n")
        for line in rest.splitlines():
            self.assertLessEqual(len(line), 60, line)
        return first == "ok=1", rest

    def routes(self, latest=None, compare=None):
        if latest is not None:
            FakeGitHub.routes["/repos/joashchee/r3i6nos/releases/latest"] = latest
        if compare is not None:
            FakeGitHub.routes[f"/repos/joashchee/r3i6nos/compare/{COMMIT}...main"] = compare

    # ---- info

    def info(self):
        with mock.patch.dict(os.environ, {"PATH": self.env["PATH"]}):
            lines = self.m.info()
        for line in lines:
            self.assertLessEqual(len(line), 60, line)
        return "\n".join(lines)

    def test_info_has_the_build_the_unit_and_its_cards(self):
        text = self.info()
        for want in ("r3i6nos 0.1.0", "278a84f000 (2026-11-09)", "2026-11-09, GitHub's build", "Rockchip RK3326 R36S", "Debian 12.7",
                     "4 cores, up to 1512 MHz", "985.4 MB, 597.7 MB free", "48 C", "Mali driver from your card", "(exfat)",
                     "11.5, 12.0", "Native apps  2", "compose, sprites", "wlan0 192.168.1.20/24", "1 h 2 min"):
            self.assertIn(want, text)
        self.assertNotIn("compose-report", text, "a tool's report is not a tool")
        self.assertIn("SSH          off", text)

    def test_info_without_a_games_card_or_driver_or_build_file(self):
        (self.t / "proc/mounts").write_text("/dev/root / ext4 rw 0 0\n")
        (self.t / "gpu").rmdir()
        (self.t / "etc/r3i6nos-release").unlink()
        text = self.info()
        self.assertIn("not found (no EASYROMS partition)", text)
        self.assertIn("software drawing", text)
        self.assertIn("/etc/r3i6nos-release is missing", text)

    def test_info_says_when_a_build_had_uncommitted_changes(self):
        self.release(VERSION="0.1.0", COMMIT=COMMIT, MODIFIED="1", BUILT_BY="local")
        text = self.info()
        self.assertIn("a local build", text)
        self.assertIn("with changes not in that commit", text)

    def test_info_never_touches_the_network(self):
        self.info()
        self.assertEqual(FakeGitHub.seen, [])

    # ---- check

    def test_up_to_date(self):
        self.routes(latest=(200, {"tag_name": "v0.1.0", "published_at": "2026-11-09T10:00:00Z", "html_url": "https://x"}),
                    compare=(200, {"status": "identical", "ahead_by": 0, "commits": []}))
        ok, text = self.run_check()
        self.assertTrue(ok)
        self.assertTrue(text.startswith("This is the latest release.\nBuilt from the newest commit on main."))
        self.assertIn("v0.1.0 (2026-11-09)", text)
        self.assertEqual({ua for _, ua in FakeGitHub.seen}, {"r3i6nos-about"}, "only the program's name is sent, nothing about the unit")

    def test_a_newer_release_and_newer_commits(self):
        url = "https://github.com/joashchee/r3i6nos/releases/tag/v0.2.0"
        self.routes(latest=(200, {"tag_name": "v0.2.0", "published_at": "2026-12-01T00:00:00Z", "html_url": url}),
                    compare=(200, {"status": "ahead", "ahead_by": 3,
                                   "commits": [{"sha": "a" * 40, "commit": {"committer": {"date": "2026-11-30T00:00:00Z"}}}]}))
        ok, text = self.run_check()
        self.assertTrue(ok)
        self.assertIn("A newer release is out.", text)
        self.assertIn("main has 3 newer commits than this build.", text)
        self.assertIn(url, text)
        self.assertIn("aaaaaaaaaa (2026-11-30)", text)
        self.assertIn("games card is not touched", text)

    def test_no_release_yet_and_a_commit_that_was_never_pushed(self):
        ok, text = self.run_check()   # every route 404s
        self.assertTrue(ok)
        self.assertIn("no release published yet", text)
        self.assertIn("This build's commit is not on GitHub", text)
        self.assertIn("never pushed", text)

    def test_a_dev_build_ahead_of_the_release(self):
        self.release(VERSION="0.2.0", COMMIT=COMMIT, REPO="joashchee/r3i6nos")
        self.routes(latest=(200, {"tag_name": "v0.1.0"}), compare=(200, {"ahead_by": 1, "commits": []}))
        ok, text = self.run_check()
        self.assertIn("This build is newer than the latest release.", text)
        self.assertIn("main has 1 newer commit than this build.", text)

    def test_rate_limited(self):
        self.routes(latest=(403, {"message": "API rate limit exceeded"}))
        ok, text = self.run_check()
        self.assertFalse(ok)
        self.assertIn("rate limit", text)

    def test_unreachable_says_to_connect_first(self):
        env = dict(self.env, R3I6NOS_UPDATE_API="http://127.0.0.1:9")   # nothing listens on the discard port
        self.m = load(env)
        ok, text = self.run_check()
        self.assertFalse(ok)
        self.assertIn("Could not reach GitHub", text)
        self.assertIn("Settings > Network", text)

    def test_a_build_without_details_or_a_bad_repo_asks_nothing(self):
        (self.t / "etc/r3i6nos-release").unlink()
        ok, text = self.run_check()
        self.assertFalse(ok)
        self.release(VERSION="0.1.0", COMMIT=COMMIT, REPO="../../evil")
        ok, text = self.run_check()
        self.assertFalse(ok)
        self.assertEqual(FakeGitHub.seen, [])

    def test_a_build_without_a_commit_only_compares_the_release(self):
        self.release(VERSION="0.1.0", COMMIT="unknown", REPO="joashchee/r3i6nos")
        self.routes(latest=(200, {"tag_name": "v0.1.0"}))
        ok, text = self.run_check()
        self.assertTrue(ok)
        self.assertIn("only the", text)
        self.assertEqual(len(FakeGitHub.seen), 1)

    def test_garbage_from_the_server_is_not_a_crash(self):
        self.routes(latest=(200, ["not", "a", "release"]), compare=(200, "nonsense"))
        ok, text = self.run_check()
        self.assertTrue(ok)
        self.assertIn("Nothing to compare with yet.", text)

    def test_usage(self):
        with mock.patch("sys.stderr", io.StringIO()) as err:
            self.assertEqual(self.m.main(["r3i6nos-about"]), 2)
        self.assertIn("r3i6nos-about info", err.getvalue())


if __name__ == "__main__":
    unittest.main()

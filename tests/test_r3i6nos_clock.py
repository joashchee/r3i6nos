"""r3i6nos-clock, the root clock helper (docs/CLOCK.md), against temporary folders. The real clock is never touched: settime is replaced."""
import calendar
import importlib.machinery
import importlib.util
import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

SRC = Path(__file__).resolve().parent.parent / "overlay/usr/local/bin/r3i6nos-clock"


def load(env):
    with mock.patch.dict(os.environ, env):
        loader = importlib.machinery.SourceFileLoader("r3i6nos_clock", str(SRC))
        spec = importlib.util.spec_from_loader("r3i6nos_clock", loader)
        mod = importlib.util.module_from_spec(spec)
        sys.dont_write_bytecode = True
        loader.exec_module(mod)
    return mod


def ts(y, mo, d, h=0, mi=0):
    return calendar.timegm((y, mo, d, h, mi, 0))


class Clock(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.t = Path(self.tmp.name)
        for d in ("req", "out", "state", "etc", "zoneinfo/Etc", "sys/class/rtc/rtc0"):
            (t / d).mkdir(parents=True)
        for z in ("UTC", "GMT-8", "GMT+5"):
            (t / "zoneinfo/Etc" / z).write_text("TZif")
        (t / "etc/r3i6nos-release").write_text("VERSION=0.1.0\nBUILT=2026-11-01\n")
        self.m = load({"R3I6NOS_CLOCK_DIR": str(t / "req"), "R3I6NOS_CLOCK_OUT": str(t / "out"), "R3I6NOS_CLOCK_STATE": str(t / "state"),
                       "R3I6NOS_ETC": str(t / "etc"), "R3I6NOS_ZONEINFO": str(t / "zoneinfo"), "R3I6NOS_SYS": str(t / "sys"),
                       "R3I6NOS_RTC_DEV": str(t / "no-rtc")})
        self.set_to = []
        self.clock = ts(1970, 1, 2)
        self.m.settime = lambda v: self.set_to.append(v)
        self.m.now = lambda: self.clock
        self.tz = os.environ.get("TZ")

    def tearDown(self):
        if self.tz is None:
            os.environ.pop("TZ", None)
        else:
            os.environ["TZ"] = self.tz
        import time
        time.tzset()
        self.tmp.cleanup()

    def request(self, text):
        (self.t / "req/request").write_text(text)
        self.assertEqual(self.m.apply(), 0)
        self.assertFalse((self.t / "req/request").exists(), "the request is deleted first")
        return (self.t / "out/result").read_text()

    # ---- restore and save

    def test_restore_lifts_an_unset_clock_to_the_build_date(self):
        self.m.restore()
        self.assertEqual(self.set_to, [ts(2026, 11, 1)])
        self.assertIn("source=the image's build date", (self.t / "out/boot").read_text())

    def test_restore_takes_the_saved_time_and_never_goes_back(self):
        (self.t / "state/clock").write_text(f"{ts(2026, 11, 9, 20)}\n")
        self.m.restore()
        self.assertEqual(self.set_to, [ts(2026, 11, 9, 20)])
        self.set_to.clear()
        self.clock = ts(2027, 1, 1)
        self.m.restore()
        self.assertEqual(self.set_to, [], "a clock already ahead of everything is left alone")

    def test_restore_prefers_a_later_rtc(self):
        (self.t / "state/clock").write_text(f"{ts(2026, 11, 9)}\n")
        (self.t / "sys/class/rtc/rtc0/since_epoch").write_text(f"{ts(2026, 11, 12)}\n")
        (self.t / "sys/class/rtc/rtc0/name").write_text("rk808-rtc\n")
        self.m.restore()
        self.assertEqual(self.set_to, [ts(2026, 11, 12)])
        self.assertIn("the RTC (rk808-rtc)", (self.t / "out/boot").read_text())

    def test_save_never_stores_an_unset_clock(self):
        self.m.save()
        self.assertFalse((self.t / "state/clock").exists())
        self.clock = ts(2026, 11, 9, 21, 30)
        self.m.save()
        self.assertEqual((self.t / "state/clock").read_text(), f"{ts(2026, 11, 9, 21, 30)}\n")

    # ---- a person's request

    def test_sets_time_zone_saves_and_marks(self):
        out = self.request("id=7-1-2\nverb=set\nset=2026-11-09 21:30\nzone=+8\n")
        self.assertIn("id=7-1-2\nok=1\n", out)
        self.assertEqual(self.set_to, [ts(2026, 11, 9, 13, 30)], "21:30 at UTC+8 is 13:30 UTC")
        self.assertEqual(os.readlink(self.t / "etc/localtime"), str(self.t / "zoneinfo/Etc/GMT-8"), "Etc/GMT-8 is UTC+8")
        self.assertEqual((self.t / "etc/timezone").read_text(), "Etc/GMT-8\n")
        self.assertTrue((self.t / "state/clock-set").exists(), "the menu stops asking")
        self.assertIn("no RTC to set", out)

    def test_utc_and_west(self):
        self.request("id=1\nset=2026-03-01 00:00\nzone=0\n")
        self.assertEqual(os.readlink(self.t / "etc/localtime"), str(self.t / "zoneinfo/Etc/UTC"))
        self.set_to.clear()
        self.request("id=2\nset=2026-03-01 00:00\nzone=-5\n")
        self.assertEqual(self.set_to, [ts(2026, 3, 1, 5)])
        self.assertEqual((self.t / "etc/timezone").read_text(), "Etc/GMT+5\n")

    def test_refusals_say_why_and_change_nothing(self):
        for text, why in (("set=2026-02-30 10:00\nzone=0", "no such day"), ("set=2026-11-09 24:00\nzone=0", "no such time"),
                          ("set=1999-01-01 00:00\nzone=0", "The year must be"), ("set=tomorrow\nzone=0", "must look like"),
                          ("set=2026-11-09 10:00\nzone=+15", "UTC-12 to UTC+14"), ("set=2026-11-09 10:00\nzone=8; rm", "UTC-12 to UTC+14"),
                          ("set=2026-11-09 10:00\nzone=+9", "No time zone file")):
            out = self.request("id=9\n" + text + "\n")
            self.assertIn("ok=0", out, text)
            self.assertIn(why, out, text)
        self.assertEqual(self.set_to, [])
        self.assertFalse((self.t / "state/clock-set").exists())

    def test_a_linked_or_huge_request_is_not_followed(self):
        secret = self.t / "secret"
        secret.write_text("id=1\nset=2026-11-09 21:30\nzone=0\n")
        os.symlink(secret, self.t / "req/request")
        self.assertEqual(self.m.apply(), 0)
        self.assertEqual(self.set_to, [], "a link planted by a game is never read")
        self.assertFalse(os.path.lexists(self.t / "req/request"), "the link is deleted, so the path unit does not keep firing")
        self.assertIn("ok=0", (self.t / "out/result").read_text())
        self.assertIn("id=1", secret.read_text(), "the link's target is untouched")
        (self.t / "req/request").write_text("x" * 5000)
        self.m.apply()
        self.assertIn("ok=0", (self.t / "out/result").read_text())

    def test_status_for_the_report(self):
        self.m.restore()
        self.clock = ts(2026, 11, 9, 21, 30)
        self.request("id=1\nset=2026-11-09 21:30\nzone=0\n")
        buf = io.StringIO()
        with redirect_stdout(buf):
            self.m.status()
        out = buf.getvalue()
        for want in ("now: 2026-11-09 21:30", "zone: Etc/UTC", "set by a person: 2026-11-09 21:30", "saved: 2026-11-09 21:30:00 UTC",
                     "rtc: none", "at boot: the image's build date, moved forward"):
            self.assertIn(want, out)


if __name__ == "__main__":
    unittest.main()

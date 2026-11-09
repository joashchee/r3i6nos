"""r3i6nos-sound, the RK817 Playback Path setter, against a fake /proc, /sys, /dev and amixer. Nothing here needs a sound card."""
import importlib.machinery
import importlib.util
import io
import os
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

SRC = Path(__file__).resolve().parent.parent / "overlay/usr/local/bin/r3i6nos-sound"

# A fake amixer: cget prints the control from $STATE (one line: the current item number), cset writes the chosen item's number
# back and logs the call. Card 1 has no Playback Path.
AMIXER = r'''#!/usr/bin/env python3
import os, sys
items = ["OFF", "RCV", "SPK", "HP", "HP_NO_MIC", "BT", "SPK_HP"]
state = os.environ["FAKE_STATE"]
args = sys.argv[1:]
with open(os.environ["FAKE_LOG"], "a") as f:
    f.write(" ".join(args) + "\n")
card = args[1]
if card != "0":
    print("amixer: Cannot find the given element from control hw:" + card, file=sys.stderr); sys.exit(1)
if args[2] == "cget":
    cur = int(open(state).read())
    print("numid=1,iface=MIXER,name='Playback Path'")
    print("  ; type=ENUMERATED,access=rw------,values=1,items=%d" % len(items))
    for i, n in enumerate(items):
        print("  ; Item #%d '%s'" % (i, n))
    print("  : values=%d" % cur)
elif args[2] == "cset":
    open(state, "w").write(str(items.index(args[4])))
'''


def load(env):
    with mock.patch.dict(os.environ, env):
        loader = importlib.machinery.SourceFileLoader("r3i6nos_sound", str(SRC))
        spec = importlib.util.spec_from_loader("r3i6nos_sound", loader)
        mod = importlib.util.module_from_spec(spec)
        sys.dont_write_bytecode = True
        loader.exec_module(mod)
    return mod


def event(etype, code, value):
    return struct.pack("llHHi", 0, 0, etype, code, value)


class Sound(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.t = Path(self.tmp.name)
        (t / "proc/asound").mkdir(parents=True)
        (t / "proc/asound/cards").write_text(" 0 [rockchiprk817c]: rockchip_rk817- - rockchip,rk817-codec\n"
                                             "                      rockchip,rk817-codec\n")
        (t / "sys/class/input").mkdir(parents=True)
        (t / "dev/input").mkdir(parents=True)
        (t / "amixer").write_text(AMIXER)
        (t / "amixer").chmod(0o755)
        (t / "state").write_text("0")
        self.env = {"R3I6NOS_PROC": str(t / "proc"), "R3I6NOS_SYS": str(t / "sys"), "R3I6NOS_DEV": str(t / "dev"),
                    "R3I6NOS_AMIXER": str(t / "amixer"), "R3I6NOS_SOUND_WAIT": "0"}
        os.environ["FAKE_STATE"] = str(t / "state")
        os.environ["FAKE_LOG"] = str(t / "log")
        self.m = load(self.env)

    def tearDown(self):
        self.tmp.cleanup()

    def current(self):
        return ["OFF", "RCV", "SPK", "HP", "HP_NO_MIC", "BT", "SPK_HP"][int((self.t / "state").read_text())]

    def input_dev(self, n, name, sw):
        d = self.t / f"sys/class/input/event{n}/device/capabilities"
        d.mkdir(parents=True)
        (d / "sw").write_text(sw + "\n")
        (d.parent / "name").write_text(name + "\n")
        (self.t / f"dev/input/event{n}").write_bytes(b"")

    def main(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            rc = self.m.main(["r3i6nos-sound", *args])
        return rc, out.getvalue(), err.getvalue()

    def test_off_becomes_speaker(self):
        rc, _, err = self.main("set")
        self.assertEqual(rc, 0)
        self.assertEqual(self.current(), "SPK")
        self.assertIn("OFF -> SPK", err)

    def test_already_set_is_left_alone(self):
        (self.t / "state").write_text("2")
        self.main("set")
        self.assertNotIn("cset", (self.t / "log").read_text())

    def test_cards_without_the_control_are_skipped(self):
        (self.t / "proc/asound/cards").write_text(" 1 [usb]: USB-Audio - Some USB DAC\n")
        rc, _, err = self.main("start")
        self.assertEqual(rc, 0)
        self.assertIn("nothing to do", err)
        self.assertNotIn("cset", (self.t / "log").read_text())

    def test_no_sound_card_is_not_an_error(self):
        (self.t / "proc/asound/cards").write_text("--- no soundcards ---\n")
        rc, _, err = self.main("start")
        self.assertEqual(rc, 0)
        self.assertIn("no sound card", err)

    def test_start_without_a_jack_sets_and_exits(self):
        self.input_dev(0, "adc-keys", "0")
        rc, _, _ = self.main("start")
        self.assertEqual(rc, 0)
        self.assertEqual(self.current(), "SPK")

    def test_finds_the_headphone_jack(self):
        self.input_dev(0, "odroidgo3-joypad", "0")
        self.input_dev(1, "rk817-sound Headphone Jack", "4")   # bit 2: SW_HEADPHONE_INSERT
        self.input_dev(2, "lid", "1")                            # bit 0: SW_LID only
        found = self.m.jack_devices()
        self.assertEqual([n for _, n in found], ["rk817-sound Headphone Jack"])
        self.assertEqual(found[0][0], self.t / "dev/input/event1")

    def test_jack_events_move_the_path(self):
        with redirect_stderr(io.StringIO()):
            self._jack_events()

    def _jack_events(self):
        self.m.handle(event(0x05, 0x02, 1) + event(0, 0, 0))
        self.assertEqual(self.current(), "HP")
        self.m.handle(event(0x01, 0x02, 0))                      # a key event, not the switch: ignored
        self.assertEqual(self.current(), "HP")
        self.m.handle(event(0x05, 0x02, 0))
        self.assertEqual(self.current(), "SPK")

    def test_choice_falls_back(self):
        self.assertEqual(self.m.choose(["OFF", "SPK_HP"], False), "SPK_HP")
        self.assertEqual(self.m.choose(["OFF", "HP_NO_MIC", "SPK"], True), "HP_NO_MIC")
        self.assertIsNone(self.m.choose(["OFF", "RCV"], False))

    def test_status_for_the_report(self):
        self.input_dev(1, "rk817-sound Headphone Jack", "4")
        rc, out, _ = self.main("status")
        self.assertEqual(rc, 0)
        self.assertIn("cards: 1", out)
        self.assertIn("card 0 playback path: OFF (of OFF RCV SPK HP HP_NO_MIC BT SPK_HP)", out)
        self.assertIn("jack: rk817-sound Headphone Jack", out)
        self.assertIn("headphones: out", out)


if __name__ == "__main__":
    unittest.main()

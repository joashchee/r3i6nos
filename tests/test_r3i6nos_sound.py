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
elif args[2] == "scontents":
    print(open(os.environ["FAKE_MIXER"]).read(), end="")
elif args[2:4] == ["-q", "sset"]:
    pass
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
                    "R3I6NOS_AMIXER": str(t / "amixer"), "R3I6NOS_SOUND_WAIT": "0",
                    "R3I6NOS_ASOUND_OUT": str(t / "run/asound-out.conf")}
        os.environ["FAKE_STATE"] = str(t / "state")
        os.environ["FAKE_LOG"] = str(t / "log")
        os.environ["FAKE_MIXER"] = str(t / "mixer")
        (t / "mixer").write_text("")
        for tool in ("aplay", "alsactl"):   # log their arguments (and aplay how many bytes it was given)
            (t / tool).write_text("#!/bin/sh\necho \"%s $* $(wc -c | tr -d ' ')\" >> \"$FAKE_LOG\"\n" % tool)
            (t / tool).chmod(0o755)
        self.env.update({"R3I6NOS_APLAY": str(t / "aplay"), "R3I6NOS_ALSACTL": str(t / "alsactl"),
                         "R3I6NOS_ALSA_STATE": str(t / "asound.state")})
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

    def start(self):
        """main("start") without its endless loop: returns (rc, the devices it would follow)."""
        with mock.patch.object(self.m, "follow", return_value=0) as follow:
            rc, _, err = self.main("start")
        follow.assert_called_once()
        return rc, [n for _, n in follow.call_args[0][0]], err

    def test_off_becomes_hp(self):
        # HP plays from the speaker, and from the jack when headphones are in; SPK was silent on the unit (2026-11-10)
        rc, _, err = self.main("set")
        self.assertEqual(rc, 0)
        self.assertEqual(self.current(), "HP")
        self.assertIn("OFF -> HP", err)

    def test_spk_becomes_hp(self):
        (self.t / "state").write_text("2")
        self.main("set")
        self.assertEqual(self.current(), "HP")

    def test_already_set_is_left_alone(self):
        (self.t / "state").write_text("3")
        self.main("set")
        self.assertNotIn("cset", (self.t / "log").read_text())

    def test_cards_without_the_control_are_skipped(self):
        (self.t / "proc/asound/cards").write_text(" 1 [usb]: USB-Audio - Some USB DAC\n")
        rc, _, err = self.start()
        self.assertEqual(rc, 0)
        self.assertIn("the output is left as it is", err)
        self.assertNotIn("cset", (self.t / "log").read_text())

    def test_no_sound_card_is_not_an_error(self):
        (self.t / "proc/asound/cards").write_text("--- no soundcards ---\n")
        rc, _, err = self.main("start")
        self.assertEqual(rc, 0)
        self.assertIn("no sound card", err)

    def test_start_without_a_jack_still_watches_for_usb(self):
        self.input_dev(0, "adc-keys", "0")
        rc, devices, _ = self.start()
        self.assertEqual(rc, 0)
        self.assertEqual(devices, [])
        self.assertEqual(self.current(), "HP")

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
        (self.t / "state").write_text("2")                       # SPK, chosen by hand in the mixer
        self.m.handle(event(0x01, 0x02, 0))                      # a key event, not the switch: ignored
        self.assertEqual(self.current(), "SPK")
        self.m.handle(event(0x05, 0x02, 1) + event(0, 0, 0))     # headphones in: HP
        self.assertEqual(self.current(), "HP")
        self.m.handle(event(0x05, 0x02, 0))                      # out: still HP (the speaker plays on HP)
        self.assertEqual(self.current(), "HP")

    def test_choice_falls_back(self):
        self.assertEqual(self.m.choose(["OFF", "SPK", "HP", "SPK_HP"]), "HP")   # this unit's four items
        self.assertEqual(self.m.choose(["OFF", "SPK_HP", "SPK"]), "SPK_HP")
        self.assertEqual(self.m.choose(["OFF", "HP_NO_MIC", "SPK"]), "HP_NO_MIC")
        self.assertIsNone(self.m.choose(["OFF", "RCV"]))

    # ---- USB sound cards ----

    USB = (" 0 [rockchiprk817c]: rockchip_rk817- - rockchip,rk817-codec\n                      rockchip,rk817-codec\n"
           " 1 [Device         ]: USB-Audio - USB Audio Device\n                      Generic USB Audio Device at usb-ff300000.usb-1\n")

    def plug_usb(self, playback=True):
        (self.t / "proc/asound/cards").write_text(self.USB)
        (self.t / "proc/asound/card1").mkdir(exist_ok=True)
        if playback:
            (self.t / "proc/asound/card1/pcm0p").mkdir(exist_ok=True)

    def unplug_usb(self):
        (self.t / "proc/asound/cards").write_text(" 0 [rockchiprk817c]: rockchip_rk817- - rockchip,rk817-codec\n")

    def out_conf(self):
        f = self.t / "run/asound-out.conf"
        return f.read_text() if f.exists() else None

    def test_no_usb_card_no_out_conf(self):
        with redirect_stderr(io.StringIO()):
            self.assertEqual(self.m.route(), 0)
        self.assertIsNone(self.out_conf())

    def test_a_usb_card_becomes_the_output(self):
        self.plug_usb()
        err = io.StringIO()
        with redirect_stderr(err):
            self.assertEqual(self.m.route(), 1)
        conf = self.out_conf()
        self.assertIn("pcm.!r3i6nos_out {", conf)
        self.assertIn("card 1", conf)
        self.assertIn("output: USB card 1 (USB-Audio - USB Audio Device)", err.getvalue())
        self.assertEqual(list((self.t / "run").iterdir()), [self.t / "run/asound-out.conf"], "no .tmp left behind")

    def test_unplugged_goes_back_to_the_codec(self):
        self.plug_usb()
        with redirect_stderr(io.StringIO()):
            self.m.route()
            self.unplug_usb()
            self.assertEqual(self.m.route(), 0)
        self.assertIsNone(self.out_conf())

    def test_unchanged_is_not_rewritten(self):
        self.plug_usb()
        err = io.StringIO()
        with redirect_stderr(err):
            self.m.route()
            self.m.route()
        self.assertEqual(err.getvalue().count("output:"), 1)

    def test_a_usb_card_that_cannot_play_is_not_used(self):
        self.plug_usb(playback=False)   # a USB microphone: capture only
        with redirect_stderr(io.StringIO()):
            self.assertEqual(self.m.route(), 0)
        self.assertIsNone(self.out_conf())

    def test_set_routes_before_making_the_volumes(self):
        self.plug_usb()
        self.main("set")
        self.assertIn("card 1", self.out_conf())

    def test_follow_looks_for_usb_cards(self):
        with mock.patch.object(self.m, "POLL", 0), redirect_stderr(io.StringIO()):
            self.plug_usb()
            self.assertEqual(self.m.follow([], rounds=1), 0)
            self.assertIn("card 1", self.out_conf())
            self.unplug_usb()
            self.m.follow([], rounds=1)
        self.assertIsNone(self.out_conf())

    def test_status_names_the_cards_and_the_output(self):
        _, out, _ = self.main("status")
        self.assertIn("card 0: rockchip_rk817- - rockchip,rk817-codec", out)
        self.assertIn("output: card 0 (no USB card)", out)
        self.plug_usb()
        _, out, _ = self.main("status")
        self.assertIn("cards: 2", out)
        self.assertIn("output: card 1, USB (USB-Audio - USB Audio Device)", out)

    def test_status_for_the_report(self):
        self.input_dev(1, "rk817-sound Headphone Jack", "4")
        rc, out, _ = self.main("status")
        self.assertEqual(rc, 0)
        self.assertIn("cards: 1", out)
        self.assertIn("card 0 playback path: OFF (of OFF RCV SPK HP HP_NO_MIC BT SPK_HP)", out)
        self.assertIn("jack: rk817-sound Headphone Jack", out)
        self.assertIn("headphones: out", out)

    # ---- the mixer: the software volumes, the volume keys, saving -------------------------------------------------------------

    MIXER = ("Simple mixer control 'Boost',0\n  Capabilities: volume\n  Limits: 0 - 7\n"
             "  Front Left: 3 [42%] [6.00dB]\n  Front Right: 3 [42%] [6.00dB]\n"
             "Simple mixer control 'Playback Path',0\n  Capabilities: enum\n  Items: 'OFF' 'SPK' 'HP'\n  Item0: 'SPK'\n"
             "Simple mixer control 'Volume',0\n  Capabilities: volume\n  Limits: 0 - 20\n"
             "  Front Left: 16 [80%] [-8.00dB]\n  Front Right: 16 [80%] [-8.00dB]\n")

    def log(self):
        return (self.t / "log").read_text() if (self.t / "log").exists() else ""

    def key_dev(self, n, name, words):
        d = self.t / f"sys/class/input/event{n}/device/capabilities"
        d.mkdir(parents=True)
        (d / "key").write_text(words + "\n")
        (d.parent / "name").write_text(name + "\n")
        (self.t / f"dev/input/event{n}").write_bytes(b"")

    def test_reads_the_mixer(self):
        (self.t / "mixer").write_text(self.MIXER)
        c = {x["name"]: x for x in self.m.mixer_controls()}
        self.assertEqual(sorted(c), ["Boost", "Playback Path", "Volume"])
        self.assertEqual((c["Volume"]["value"], c["Volume"]["min"], c["Volume"]["max"]), (16, 0, 20))
        self.assertEqual(self.m.describe(c["Volume"]), "16 of 0-20, 80%, -8.0 dB")
        self.assertEqual(self.m.describe(c["Boost"]), "3 of 0-7, 42%, +6.0 dB")
        self.assertEqual(self.m.describe(c["Playback Path"]), "SPK (of OFF SPK HP)")

    def test_first_boot_makes_the_volumes_and_sets_safe_levels(self):
        (self.t / "mixer").write_text(self.MIXER)
        rc, _, _ = self.main("set")
        self.assertEqual(rc, 0)
        log = self.log()
        self.assertIn("aplay -q -D default -t raw -f S16_LE -c 2 -r 44100 4096", log)   # opened once, with silence
        self.assertIn("-c 0 -q sset Volume 80%", log)
        self.assertIn("-c 0 -q sset Boost 3", log)
        self.assertNotIn("alsactl", log)

    def test_saved_levels_are_restored(self):
        (self.t / "mixer").write_text(self.MIXER)
        (self.t / "asound.state").write_text("state.rockchiprk817c {\n control.9 {\n  iface MIXER\n  name Boost\n }\n}\n")
        self.main("set")
        log = self.log()
        self.assertIn(f"alsactl -f {self.t / 'asound.state'} restore 0", log)
        self.assertNotIn("sset Volume", log)

    def test_a_state_from_before_the_volumes_gets_the_defaults(self):
        (self.t / "mixer").write_text(self.MIXER)
        (self.t / "asound.state").write_text("state.rockchiprk817c {\n control.1 {\n  name 'Playback Path'\n }\n}\n")
        self.main("set")
        self.assertIn("sset Boost 3", self.log())

    def test_a_restored_boost_of_silence_goes_to_0_db(self):
        # Boost's step 0 is a softvol's silence: a state saved there would leave every app mute (/etc/asound.conf)
        (self.t / "mixer").write_text(self.MIXER.replace("3 [42%] [6.00dB]", "0 [0%] [-3.00dB]"))
        (self.t / "asound.state").write_text("state.rockchiprk817c {\n control.9 {\n  iface MIXER\n  name Boost\n }\n}\n")
        with redirect_stderr(io.StringIO()) as err:
            self.m.mixer_init()
        self.assertIn("-c 0 -q sset Boost 1", self.log())
        self.assertIn("set to 1 (0 dB)", err.getvalue())

    def test_a_restored_boost_above_the_floor_is_left_alone(self):
        (self.t / "mixer").write_text(self.MIXER)
        (self.t / "asound.state").write_text("state.rockchiprk817c {\n control.9 {\n  iface MIXER\n  name Boost\n }\n}\n")
        with redirect_stderr(io.StringIO()):
            self.m.mixer_init()
        self.assertNotIn("sset Boost", self.log())

    def test_start_does_not_open_the_device_again(self):
        self.start()
        self.assertNotIn("aplay", self.log())

    def test_no_volume_control_is_said(self):
        _, _, err = self.main("set")
        self.assertIn("no Volume control", err)

    def test_save(self):
        rc, _, _ = self.main("save")
        self.assertEqual(rc, 0)
        self.assertIn(f"alsactl -f {self.t / 'asound.state'} store 0", self.log())

    def test_finds_the_volume_keys(self):
        # KEY_VOLUMEDOWN 114 and KEY_VOLUMEUP 115 are bits 50 and 51 of the second 64-bit word (the words are printed highest first)
        self.key_dev(0, "gpio-keys-vol", "c000000000000 0")
        self.key_dev(1, "odroidgo3-joypad", "ffff0000 0 0 0 0")   # BTN_ codes only: higher words
        self.key_dev(2, "power", "10000000000000 0")              # KEY_POWER 116 (bit 52): not a volume key
        self.assertEqual([n for _, n in self.m.key_devices()], ["gpio-keys-vol"])

    def test_bitmap(self):
        self.assertEqual(self.m.bitmap("1 0"), 1 << self.m.LONG_BITS)
        self.assertEqual(self.m.bitmap("4"), 4)

    def test_volume_keys_step_the_volume(self):
        with redirect_stderr(io.StringIO()):
            self.m.handle(event(0x01, 115, 1) + event(0, 0, 0))   # up, pressed
            self.m.handle(event(0x01, 114, 2))                     # down, held (repeat)
            self.m.handle(event(0x01, 114, 0))                     # released: nothing
        steps = [ln for ln in self.log().splitlines() if "sset Volume" in ln]
        self.assertEqual(steps, ["-c 0 -q sset Volume 1+", "-c 0 -q sset Volume 1-"])

    def test_start_follows_the_keys_without_a_jack(self):
        self.key_dev(0, "gpio-keys-vol", "c000000000000 0")
        _, devices, _ = self.start()
        self.assertEqual(devices, ["gpio-keys-vol"])

    def test_status_lists_the_mixer_and_keys(self):
        (self.t / "mixer").write_text(self.MIXER)
        _, out, _ = self.main("status")
        self.assertIn("volume keys: none", out)
        self.assertIn("mixer Volume: 16 of 0-20, 80%, -8.0 dB", out)
        self.assertIn("mixer Boost: 3 of 0-7, 42%, +6.0 dB", out)


if __name__ == "__main__":
    unittest.main()

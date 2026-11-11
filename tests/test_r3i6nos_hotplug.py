"""r3i6nos-hotplug (docs/HOTPLUG.md) against a fake /sys, a fake kernel log and a fake jack: what it announces, and when it does not."""
import importlib.machinery
import importlib.util
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

SRC = Path(__file__).resolve().parent.parent / "overlay/usr/local/bin/r3i6nos-hotplug"


def load(env):
    with mock.patch.dict(os.environ, env):
        loader = importlib.machinery.SourceFileLoader("r3i6nos_hotplug", str(SRC))
        spec = importlib.util.spec_from_loader("r3i6nos_hotplug", loader)
        mod = importlib.util.module_from_spec(spec)
        sys.dont_write_bytecode = True
        loader.exec_module(mod)
    return mod


class FakeSound:
    def __init__(self):
        self.jack = False
        self.card = None

    def jack_devices(self):
        return [("/dev/input/event9", "jack")]

    def headphones_in(self, devices):
        return self.jack

    def usb_card(self):
        return self.card


class Hotplug(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = Path(self.tmp.name)
        self.sys = t / "sys"
        (self.sys / "bus/usb/devices").mkdir(parents=True)
        (self.sys / "class/power_supply").mkdir(parents=True)
        self.kmsg = t / "kmsg"
        self.kmsg.write_text("")
        self.env = {"R3I6NOS_SYS": str(self.sys), "R3I6NOS_HOTPLUG_DIR": str(t / "run"), "R3I6NOS_KMSG": str(self.kmsg), "R3I6NOS_HOTPLUG_POLL": "0", "R3I6NOS_PROC": str(t / "proc"), "R3I6NOS_CARDS_STATE": str(t / "cards-state"), "R3I6NOS_VOLUME_FILE": str(t / "volume")}
        (t / "cards-state").write_text("BOOT_DISK=mmcblk0\n")
        self.mod = load(self.env)
        self.sound = FakeSound()
        self.events = self.mod.Events(t / "run/events")
        self.battery("Battery", "Discharging")
        self.supply("rk817-ac", "Mains", 0)

    def tearDown(self):
        self.tmp.cleanup()

    def battery(self, name, status):
        d = self.sys / "class/power_supply" / name
        d.mkdir(parents=True, exist_ok=True)
        (d / "type").write_text("Battery\n")
        (d / "status").write_text(status + "\n")

    def supply(self, name, kind, online):
        d = self.sys / "class/power_supply" / name
        d.mkdir(parents=True, exist_ok=True)
        (d / "type").write_text(kind + "\n")
        (d / "online").write_text(f"{online}\n")

    def usb(self, name, product, iface_class):
        d = self.sys / "bus/usb/devices" / name
        (d / f"{name}:1.0").mkdir(parents=True, exist_ok=True)
        (d / "product").write_text(product + "\n")
        (d / f"{name}:1.0/bInterfaceClass").write_text(iface_class + "\n")

    def pad(self, name):
        """The kernel's joystick handler made a jsN under the interface's input device."""
        (self.sys / "bus/usb/devices" / name / f"{name}:1.0/input/input7/js0").mkdir(parents=True, exist_ok=True)

    def card(self, disk, size="30535680"):
        d = self.sys / "block" / disk
        d.mkdir(parents=True, exist_ok=True)
        (d / "size").write_text(size + "\n")

    def mounts(self, *devs):
        proc = Path(self.tmp.name) / "proc"
        proc.mkdir(exist_ok=True)
        (proc / "mounts").write_text("".join(f"{d} /mnt ext4 rw 0 0\n" for d in devs))

    def charge(self, capacity, status="Discharging"):
        d = self.sys / "class/power_supply/Battery"
        (d / "capacity").write_text(f"{capacity}\n")
        (d / "status").write_text(status + "\n")

    def unplug_usb(self, name):
        import shutil
        shutil.rmtree(self.sys / "bus/usb/devices" / name)

    def run_script(self, script):
        """Runs the watcher one look for each step of the script: each step is a function that changes the fake unit."""
        steps = iter(script)

        def sleep(_):
            try:
                next(steps)()
            except StopIteration:
                pass
        with mock.patch.object(self.mod.time, "sleep", side_effect=sleep):
            self.mod.watch(rounds=len(script) + 1, sound=self.sound, devices=self.sound.jack_devices(), events=self.events, kmsg=self.mod.Kmsg(str(self.kmsg)))
        path = self.events.path
        return [ln.split("\t") for ln in path.read_text().splitlines()] if path.exists() else []

    def texts(self, lines):
        return [(ln[1], ln[2]) for ln in lines]

    def test_what_is_plugged_in_at_start_is_not_announced(self):
        self.usb("1-1", "Old stick", "08")
        self.sound.jack = True
        self.supply("rk817-ac", "Mains", 1)
        self.assertEqual(self.run_script([lambda: None, lambda: None, lambda: None]), [])

    def test_headphones_usb_and_dc_power_in_and_out(self):
        def jack(v):
            return lambda: setattr(self.sound, "jack", v)
        lines = self.run_script([
            jack(True), lambda: None, lambda: None,
            lambda: self.usb("1-1", "Zeta USB Headset", "01"), lambda: None, lambda: None,
            lambda: self.supply("rk817-ac", "Mains", 1), lambda: None, lambda: None,
            lambda: self.usb("1-2", "Controller X", "03"), lambda: self.pad("1-2"), lambda: None, lambda: None,
            jack(False), lambda: None, lambda: None,
            lambda: self.unplug_usb("1-1"), lambda: None, lambda: None,
            lambda: self.supply("rk817-ac", "Mains", 0), lambda: None, lambda: None,
        ])
        got = self.texts(lines)
        self.assertEqual(got, [
            ("info", "Headphones connected"),
            ("info", "USB audio connected: Zeta USB Headset: sound plays there now"),
            ("info", "DC power connected"),
            ("info", "USB gamepad connected: Controller X"),
            ("info", "Headphones removed"),
            ("info", "USB audio removed: Zeta USB Headset: sound is back on the speaker"),
            ("info", "DC power disconnected"),
        ])
        ns = [int(ln[0]) for ln in lines]
        self.assertEqual(ns, sorted(set(ns)), "strictly increasing, so a reader can keep the last it has seen")

    def test_volume_keys_toast_once_per_burst(self):
        vol = self.mod.VolumeWatch()
        self.assertEqual(vol.look(0.0), [])
        (Path(self.tmp.name) / "volume").write_text("1 70\n")
        self.assertEqual(vol.look(1.0), [])           # still pressing
        (Path(self.tmp.name) / "volume").write_text("2 72\n")
        self.assertEqual(vol.look(1.4), [])
        self.assertEqual(vol.look(1.9), [])           # 0.5 s after the last press
        self.assertEqual(vol.look(2.1), [("info", "Volume 72%")])
        self.assertEqual(vol.look(5.0), [])           # said once
        (Path(self.tmp.name) / "volume").write_text("3 72\n")   # a press at the end of the range: same level, still a toast
        vol.look(6.0)
        self.assertEqual(vol.look(6.7), [("info", "Volume 72%")])

    def test_volume_before_start_is_not_announced(self):
        (Path(self.tmp.name) / "volume").write_text("9 50\n")
        vol = self.mod.VolumeWatch()
        self.assertEqual(vol.look(0.0), [])
        self.assertEqual(vol.look(9.0), [])

    def test_a_keyboard_is_not_a_gamepad(self):
        self.usb("1-3", "Tiny Keyboard", "03")
        self.assertEqual(self.mod.usb_devices()["1-3"], ("Tiny Keyboard", "input"))
        self.pad("1-3")
        self.assertEqual(self.mod.usb_devices()["1-3"], ("Tiny Keyboard", "pad"))

    def test_tf_cards_in_and_out(self):
        self.card("mmcblk0")
        self.mounts("/dev/mmcblk0p3")
        self.card("mmcblk0boot0")
        self.card("mmcblk1rpmb")
        lines = self.run_script([
            lambda: self.card("mmcblk1"), lambda: None, lambda: None,            # the data card goes in (not mounted: read at boot)
            lambda: __import__("shutil").rmtree(self.sys / "block/mmcblk1"), lambda: None, lambda: None,   # and out again
        ])
        self.assertEqual(self.texts(lines), [("info", "TF2 card in: it is read at boot, so restart to use it"), ("info", "TF2 card removed")])

    def test_a_mounted_card_pulled_out_is_a_failure(self):
        self.card("mmcblk0")
        self.card("mmcblk1")
        self.mounts("/dev/mmcblk0p3", "/dev/mmcblk1p1")
        lines = self.run_script([lambda: __import__("shutil").rmtree(self.sys / "block/mmcblk1"), lambda: None, lambda: None])
        self.assertEqual(self.texts(lines), [("error", "TF2 card removed while in use: its last changes may be lost")])

    def test_the_system_card_is_the_boot_disk(self):
        self.card("mmcblk1")
        (Path(self.tmp.name) / "cards-state").write_text("BOOT_DISK=mmcblk1\n")
        self.assertEqual(self.mod.cards()["mmcblk1"][0], "TF1")

    def test_battery_low_critical_and_full(self):
        self.charge(60)
        lines = self.run_script([
            lambda: self.charge(30), lambda: self.charge(15), lambda: self.charge(14), lambda: self.charge(10),     # low once
            lambda: self.charge(5), lambda: self.charge(4),                                                         # critical once
            lambda: self.charge(30, "Charging"), lambda: self.charge(90, "Charging"), lambda: self.charge(100, "Full"), lambda: self.charge(100, "Full"),
            lambda: self.charge(18), lambda: self.charge(14),                                                       # it climbed above 20 on the way up: armed again
            lambda: self.charge(16), lambda: self.charge(12),                                                       # still the same dip: not again
            lambda: self.charge(40), lambda: self.charge(15),                                                       # above 20, then low once more
        ])
        self.assertEqual(self.texts(lines), [
            ("info", "Battery low: 15%"),
            ("error", "Battery critically low: 5%. Plug in power or save now"),
            ("info", "Battery full"),
            ("info", "Battery low: 14%"),
            ("info", "Battery low: 15%"),
        ])

    def test_a_battery_that_starts_low_is_not_announced(self):
        self.charge(12)
        self.assertEqual(self.run_script([lambda: self.charge(11), lambda: self.charge(10), lambda: None]), [])

    def wifi(self, state, name="wlan0"):
        d = self.sys / "class/net" / name
        (d / "wireless").mkdir(parents=True, exist_ok=True)
        (d / "operstate").write_text(state + "\n")

    def test_wifi_connect_and_disconnect_name_the_network(self):
        self.mod.WIFI_SSID.clear()
        self.wifi("down")
        asked = []
        self.mod.wifi_ssid = lambda name: asked.append(name) or "Home WiFi"
        lines = self.run_script([
            lambda: self.wifi("up"), lambda: None, lambda: None, lambda: None,
            lambda: self.wifi("down"), lambda: None, lambda: None,
        ])
        self.assertEqual(self.texts(lines), [("info", "Wi-Fi connected: Home WiFi"), ("info", "Wi-Fi disconnected: Home WiFi")])
        self.assertEqual(asked, ["wlan0"], "iw is asked once for a link, not every look")

    def test_a_wifi_blip_shorter_than_a_second_is_not_an_event(self):
        self.mod.WIFI_SSID.clear()
        self.wifi("up")
        self.mod.wifi_ssid = lambda name: "Home WiFi"
        lines = self.run_script([lambda: self.wifi("down"), lambda: self.wifi("up"), lambda: None, lambda: None])
        self.assertEqual(lines, [])

    def test_wifi_already_joined_at_start_is_not_announced(self):
        self.mod.WIFI_SSID.clear()
        self.wifi("up")
        self.mod.wifi_ssid = lambda name: "Home WiFi"
        self.assertEqual(self.run_script([lambda: None, lambda: None]), [])

    def test_ethernet_is_not_wifi(self):
        d = self.sys / "class/net/eth0"
        d.mkdir(parents=True)
        (d / "operstate").write_text("up\n")
        self.assertEqual(self.mod.wifi_up(), {})

    def test_iw_that_is_missing_gives_a_connect_without_a_name(self):
        self.mod.WIFI_SSID.clear()
        self.wifi("down")
        self.mod.wifi_ssid = lambda name: ""
        lines = self.run_script([lambda: self.wifi("up"), lambda: None, lambda: None])
        self.assertEqual(self.texts(lines), [("info", "Wi-Fi connected")])

    def test_a_bouncing_contact_is_not_an_event(self):
        lines = self.run_script([lambda: setattr(self.sound, "jack", True), lambda: setattr(self.sound, "jack", False), lambda: None, lambda: None])
        self.assertEqual(lines, [])

    def test_root_hubs_and_interfaces_are_not_devices(self):
        (self.sys / "bus/usb/devices/usb1").mkdir()
        (self.sys / "bus/usb/devices/1-1:1.0").mkdir()
        self.assertEqual(self.mod.usb_devices(), {})

    def test_the_otg_port_charging_is_usb_power(self):
        lines = self.run_script([lambda: self.supply("rk817-usb", "USB_DCP", 1), lambda: None, lambda: None])
        self.assertEqual(self.texts(lines), [("info", "USB power connected")])

    def test_a_device_that_would_not_start_is_an_error(self):
        def kernel():
            with open(self.kmsg, "a") as f:
                f.write("6,1234,5678,-;usb 1-1: device descriptor read/64, error -71\n")
                f.write("6,1235,5679,-;usb 1-1: device descriptor read/64, error -71\n")
        lines = self.run_script([kernel, lambda: None])
        self.assertEqual([t for t in self.texts(lines) if t[0] == "error"], [("error", "A USB device did not start: its cable or the device may be bad")], "one error for the retries")

    def test_old_kernel_messages_are_not_news(self):
        self.kmsg.write_text("6,1,1,-;usb 1-1: unable to enumerate USB device\n")
        self.assertEqual(self.run_script([lambda: None, lambda: None]), [])

    def test_a_usb_sound_card_that_gives_no_output_is_an_error(self):
        self.mod.AUDIO_WAIT = 0
        lines = self.run_script([lambda: self.usb("1-1", "Dead DAC", "01"), lambda: None, lambda: None, lambda: None, lambda: None])
        self.assertIn(("error", "USB audio Dead DAC gives no sound output"), self.texts(lines))
        self.assertEqual(len([t for t in self.texts(lines) if t[0] == "error"]), 1, "said once")

    def test_a_usb_sound_card_with_output_is_fine(self):
        self.mod.AUDIO_WAIT = 0
        self.sound.card = 1
        lines = self.run_script([lambda: self.usb("1-1", "Good DAC", "01"), lambda: None, lambda: None, lambda: None, lambda: None])
        self.assertEqual([t for t in self.texts(lines) if t[0] == "error"], [])

    def test_the_file_keeps_the_last_forty_and_is_whole(self):
        for i in range(50):
            self.events.add("info", f"event {i}")
        lines = self.events.path.read_text().splitlines()
        self.assertEqual(len(lines), 40)
        self.assertTrue(lines[-1].endswith("event 49"))
        self.assertFalse(self.events.path.with_suffix(".tmp").exists())
        self.events.add("info", "a\ttab\nand a newline")
        self.assertEqual(self.events.path.read_text().splitlines()[-1].split("\t")[2], "a tab and a newline")

    def test_status_names_what_is_plugged_in(self):
        self.usb("1-1", "Stick", "08")
        self.supply("rk817-ac", "Mains", 1)
        with mock.patch.object(self.mod, "load_sound", return_value=self.sound):
            import io
            import contextlib
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.mod.status()
        text = out.getvalue()
        self.assertIn("headphones: out", text)
        self.assertIn("usb 1-1: storage, Stick", text)
        self.assertIn("power rk817-ac: DC power connected", text)


if __name__ == "__main__":
    unittest.main()

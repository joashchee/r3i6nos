"""r3i6nos-persist (docs/SAVED-SETTINGS.md): the menu's saved settings kept on the data card. Run against fake /var/lib, /etc, a home, a zoneinfo folder and a fake data card,
with the real r3i6nos-appdata deciding what a valid data card is. No root, no device."""
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

BIN = Path(__file__).resolve().parent.parent / "overlay/usr/local/bin"
PERSIST = BIN / "r3i6nos-persist"


class Persist(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.t = Path(self.tmp.name)
        for d in ("var/r3i6nos", "var/alsa", "var/r3i6nos-battery", "etc", "home", "state", "run", "zone/Etc", "card", "bin"):
            (t / d).mkdir(parents=True)
        for z in ("Etc/GMT-8", "Etc/UTC", "Etc/GMT+5"):
            (t / "zone" / z).write_text("TZif2")
        self.set_cards(fs="ext4")
        (t / "bin/clock").write_text(f"#!/bin/sh\necho \"$@\" >> {t}/clock.log\n")
        (t / "bin/clock").chmod(0o755)
        self.env = dict(os.environ, R3I6NOS_PERSIST_VARLIB=str(t / "var"), R3I6NOS_PERSIST_ETC=str(t / "etc"), R3I6NOS_PERSIST_HOME=str(t / "home"),
                        R3I6NOS_PERSIST_STATE=str(t / "state"), R3I6NOS_PERSIST_OUT=str(t / "run/persist"), R3I6NOS_APPDATA_ENGINE=str(BIN / "r3i6nos-appdata"),
                        R3I6NOS_CLOCK=str(t / "bin/clock"), R3I6NOS_ZONEINFO=str(t / "zone"), R3I6NOS_CARDS_FILE=str(t / "run/cards"),
                        R3I6NOS_APPDATA_MIN_FREE_MB="1", R3I6NOS_APPDATA_STATE=str(t / "appdata-state"))

    def tearDown(self):
        self.tmp.cleanup()

    def set_cards(self, fs="ext4", at=True):
        card = str(self.t / "card") if at else "none"
        (self.t / "run/cards").write_text(f"BOOT_DISK=mmcblk0\nSYSTEM_DEV=none\nSYSTEM_FS=none\nSYSTEM_AT=none\nDATA_DEV=/dev/mmcblk1p1\nDATA_FS={fs}\nDATA_AT={card}\n")

    # ---- helpers

    def run_persist(self, *args, check=True):
        p = subprocess.run([sys.executable, str(PERSIST), *args], capture_output=True, text=True, env=self.env, timeout=60)
        if check:
            self.assertEqual(p.returncode, 0, p.stderr + p.stdout)
        return p

    def local(self, rel):
        return self.t / rel

    def card(self, name):
        return self.t / "card/r3i6nos/settings" / name

    def status(self):
        d = {}
        for ln in (self.t / "run/persist").read_text().splitlines():
            k, _, v = ln.partition("=")
            d[k] = v
        return d

    def set_settings(self, wifi="HomeNet", zone="Etc/GMT-8"):
        (self.t / "var/r3i6nos/wifi.json").write_text(json.dumps({"ssid": wifi, "psk": "hunter2hunter2"}))
        (self.t / "etc/timezone").write_text(zone + "\n")
        (self.t / "var/r3i6nos/clock").write_text("1800000000\n")
        (self.t / "var/r3i6nos/clock-set").write_text("2026-11-19 10:00\n")
        (self.t / "var/alsa/asound.state").write_text("state.rk817sound {\n  control.1 {\n  }\n}\n")
        (self.t / "var/r3i6nos-battery/history.tsv").write_text("1\t90\n2\t89\n")
        (self.t / "home/.r3i6nos-clock-asked").write_text("1\n")

    # ---- no card

    def test_no_data_card_changes_nothing_and_says_so(self):
        self.set_cards(at=False)
        self.set_settings()
        p = self.run_persist("sync")
        self.assertIn("no usable data card", p.stdout)
        self.assertEqual(self.status()["STATE"], "no-card")
        self.assertFalse((self.t / "card/r3i6nos").exists())
        self.assertEqual((self.t / "etc/timezone").read_text(), "Etc/GMT-8\n")

    def test_an_unsupported_filesystem_is_not_used(self):
        self.set_cards(fs="ntfs")
        self.set_settings()
        self.run_persist("sync")
        self.assertEqual(self.status()["STATE"], "unsupported")
        self.assertFalse((self.t / "card/r3i6nos").exists())

    def test_a_read_only_card_is_not_used(self):
        self.set_settings()
        os.chmod(self.t / "card", 0o555)
        try:
            if os.access(self.t / "card", os.W_OK):
                self.skipTest("running as a user that can write anywhere")
            self.run_persist("sync")
            self.assertEqual(self.status()["STATE"], "readonly")
        finally:
            os.chmod(self.t / "card", 0o755)

    # ---- the first sync: a unit with settings and a new card

    def test_a_first_sync_saves_every_item_to_the_card(self):
        self.set_settings()
        p = self.run_persist("sync")
        for item in ("wifi", "timezone", "clock-set", "clock", "mixer", "battery", "clock-asked"):
            self.assertIn(f"{item}: saved", p.stdout)
        self.assertEqual(json.loads(self.card("wifi.json").read_text())["ssid"], "HomeNet")
        self.assertEqual(self.card("timezone").read_text(), "Etc/GMT-8\n")
        self.assertEqual(self.card("mixer.state").read_text(), (self.t / "var/alsa/asound.state").read_text())
        self.assertEqual(self.card("battery-history.tsv").read_text(), "1\t90\n2\t89\n")
        self.assertTrue(self.card("README.txt").is_file())
        st = self.status()
        self.assertEqual((st["STATE"], st["ITEMS"], st["CONFLICTS"], st["ERRORS"]), ("synced", "7", "0", ""))
        self.assertEqual(sorted(st["SAVED"].split(",")), sorted(["wifi", "timezone", "clock-set", "clock", "mixer", "battery", "clock-asked"]))
        self.assertTrue((self.t / "card/.r3i6nos-card").is_file(), "the card gets its id, as apps give it")
        self.assertFalse(list((self.t / "card/r3i6nos/settings").glob(".*.r3i6nos-part")), "no temporary file is left")
        self.assertEqual(oct(os.stat(self.card("wifi.json")).st_mode & 0o777), "0o600", "the Wi-Fi file is root-only where the card has modes")

    def test_a_second_sync_changes_nothing(self):
        self.set_settings()
        self.run_persist("sync")
        p = self.run_persist("sync")
        self.assertNotIn("saved", p.stdout)
        self.assertNotIn("restored", p.stdout)
        self.assertEqual(p.stdout.count(": same"), 7)

    def test_a_missing_item_is_not_an_error(self):
        (self.t / "etc/timezone").write_text("Etc/UTC\n")
        p = self.run_persist("sync")
        self.assertIn("wifi: none", p.stdout)
        self.assertIn("timezone: saved", p.stdout)
        self.assertEqual(self.status()["ITEMS"], "1")

    # ---- a fresh flash: the card has the settings, the unit has none

    def test_a_fresh_system_card_gets_its_settings_back(self):
        self.set_settings(wifi="Cafe", zone="Etc/GMT+5")
        self.run_persist("sync")
        for p in ("var/r3i6nos/wifi.json", "etc/timezone", "var/r3i6nos/clock", "var/r3i6nos/clock-set", "var/alsa/asound.state", "var/r3i6nos-battery/history.tsv",
                  "home/.r3i6nos-clock-asked"):
            (self.t / p).unlink()
        (self.t / "state/synced.json").unlink()                          # a reflash forgets the last sync
        p = self.run_persist("sync")
        for item in ("wifi", "timezone", "clock", "mixer", "battery"):
            self.assertIn(f"{item}: restored", p.stdout)
        self.assertEqual(json.loads((self.t / "var/r3i6nos/wifi.json").read_text())["ssid"], "Cafe")
        self.assertEqual(oct(os.stat(self.t / "var/r3i6nos/wifi.json").st_mode & 0o777), "0o600")
        self.assertEqual((self.t / "etc/timezone").read_text(), "Etc/GMT+5\n")
        self.assertTrue((self.t / "etc/localtime").is_symlink() and os.readlink(self.t / "etc/localtime") == str(self.t / "zone/Etc/GMT+5"), "/etc/localtime follows the zone")
        self.assertIn("restore", (self.t / "clock.log").read_text(), "the clock service is asked to look at the restored time")
        self.assertEqual(sorted(self.status()["RESTORED"].split(",")), sorted(["wifi", "timezone", "clock-set", "clock", "mixer", "battery", "clock-asked"]))

    # ---- changes on either side

    def test_a_change_here_goes_to_the_card_and_a_change_there_comes_here(self):
        self.set_settings()
        self.run_persist("sync")
        (self.t / "var/r3i6nos/wifi.json").write_text(json.dumps({"ssid": "Other", "psk": "abcdefgh"}))
        p = self.run_persist("sync")
        self.assertIn("wifi: saved", p.stdout)
        self.assertEqual(json.loads(self.card("wifi.json").read_text())["ssid"], "Other")
        self.card("timezone").write_text("Etc/GMT+5\n")                   # another unit changed the zone on the card
        p = self.run_persist("sync")
        self.assertIn("timezone: restored", p.stdout)
        self.assertEqual((self.t / "etc/timezone").read_text(), "Etc/GMT+5\n")

    def test_both_changed_the_newer_wins_and_the_other_is_kept_aside(self):
        self.set_settings()
        self.run_persist("sync")
        (self.t / "var/alsa/asound.state").write_text("state.rk817sound {\n  here\n}\n")
        self.card("mixer.state").write_text("state.rk817sound {\n  there\n}\n")
        old = time.time() - 1000
        os.utime(self.t / "var/alsa/asound.state", (old, old))          # the card's copy is the newer one
        p = self.run_persist("sync")
        self.assertIn("mixer: restored+conflict", p.stdout)
        self.assertIn("there", (self.t / "var/alsa/asound.state").read_text())
        kept = list((self.t / "card/r3i6nos/settings/.merge-conflicts").glob("mixer.state-unit-*"))
        self.assertEqual(len(kept), 1)
        self.assertIn("here", kept[0].read_text())
        self.assertEqual(self.status()["CONFLICTS"], "1")

    def test_a_card_this_unit_has_not_met_wins_and_this_units_copy_is_kept(self):
        self.set_settings(zone="Etc/GMT-8")
        self.card("timezone").parent.mkdir(parents=True)
        self.card("timezone").write_text("Etc/GMT+5\n")                   # a card from another unit: no last sync to go by
        p = self.run_persist("sync")
        self.assertIn("timezone: restored+conflict", p.stdout)
        self.assertEqual((self.t / "etc/timezone").read_text(), "Etc/GMT+5\n")
        kept = list((self.t / "card/r3i6nos/settings/.merge-conflicts").glob("timezone-unit-*"))
        self.assertEqual(len(kept), 1)
        self.assertEqual(kept[0].read_text(), "Etc/GMT-8\n")

    def test_a_deletion_here_deletes_the_cards_copy_too(self):
        self.set_settings()
        self.run_persist("sync")
        (self.t / "var/r3i6nos/wifi.json").unlink()                     # Settings > Network > Forget
        (self.t / "var/r3i6nos-battery/history.tsv").unlink()           # Clear the battery history
        p = self.run_persist("sync")
        self.assertIn("wifi: deleted-card", p.stdout)
        self.assertIn("battery: deleted-card", p.stdout)
        self.assertFalse(self.card("wifi.json").exists())
        self.assertFalse(self.card("battery-history.tsv").exists())
        self.assertTrue(self.card("timezone").exists(), "only what was deleted goes")
        p = self.run_persist("sync")
        self.assertIn("wifi: none", p.stdout, "and it does not come back")

    def test_a_deletion_loses_to_a_newer_copy_on_the_card(self):
        self.set_settings()
        self.run_persist("sync")
        (self.t / "var/r3i6nos/wifi.json").unlink()
        self.card("wifi.json").write_text(json.dumps({"ssid": "NewerAtHome", "psk": "abcdefgh"}))      # changed on the card since: not a plain deletion
        p = self.run_persist("sync")
        self.assertIn("wifi: restored", p.stdout)
        self.assertEqual(json.loads((self.t / "var/r3i6nos/wifi.json").read_text())["ssid"], "NewerAtHome")

    # ---- what comes off the card is checked

    def test_nothing_unusable_is_restored(self):
        self.set_settings()
        self.run_persist("sync")
        for p in ("var/r3i6nos/wifi.json", "etc/timezone", "var/alsa/asound.state", "var/r3i6nos/clock"):
            (self.t / p).unlink()
        (self.t / "state/synced.json").unlink()
        self.card("wifi.json").write_text("not json at all")
        self.card("timezone").write_text("../../etc/passwd\n")
        self.card("mixer.state").write_text("rm -rf /\n")
        self.card("clock").write_text("yesterday\n")
        p = self.run_persist("sync")
        for item in ("wifi", "timezone", "mixer", "clock"):
            self.assertIn(f"{item}: not synced: the card's copy is not usable", p.stdout)
            self.assertFalse((self.t / {"wifi": "var/r3i6nos/wifi.json", "timezone": "etc/timezone", "mixer": "var/alsa/asound.state", "clock": "var/r3i6nos/clock"}[item]).exists())
        self.assertFalse((self.t / "etc/localtime").exists())
        self.assertIn("timezone:", self.status()["ERRORS"])

    def test_a_zone_that_is_not_installed_is_refused(self):
        (self.t / "etc/timezone").write_text("Etc/UTC\n")
        self.run_persist("sync")
        (self.t / "etc/timezone").unlink()
        (self.t / "state/synced.json").unlink()
        self.card("timezone").write_text("Europe/Nowhere\n")
        p = self.run_persist("sync")
        self.assertIn("not a zone in", p.stdout)
        self.assertFalse((self.t / "etc/timezone").exists())

    def test_a_link_in_place_of_a_file_is_never_followed(self):
        (self.t / "secret").write_text("do not read")
        os.symlink(self.t / "secret", self.t / "var/r3i6nos/wifi.json")
        p = self.run_persist("sync")
        self.assertIn("wifi: not synced: not a regular file", p.stdout)
        self.assertFalse(self.card("wifi.json").exists())

    def test_a_huge_file_is_never_read(self):
        with open(self.t / "var/r3i6nos-battery/history.tsv", "wb") as f:
            f.truncate(9 << 20)
        p = self.run_persist("sync")
        self.assertIn("battery: not synced: over 8 MiB", p.stdout)

    # ---- forgetting and the skip list

    def test_forget_takes_the_wifi_password_off_the_card_and_keeps_it_off(self):
        self.set_settings()
        self.run_persist("sync")
        self.assertTrue(self.card("wifi.json").exists())
        p = self.run_persist("forget", "wifi")
        self.assertIn("deleted from the data card", p.stdout)
        self.assertFalse(self.card("wifi.json").exists())
        p = self.run_persist("sync")
        self.assertIn("wifi: skipped", p.stdout)
        self.assertFalse(self.card("wifi.json").exists(), "the unit still has its Wi-Fi, but the card does not")
        self.assertTrue((self.t / "var/r3i6nos/wifi.json").exists())
        self.assertEqual(self.status()["SKIPPED"], "wifi")
        self.run_persist("keep", "wifi")
        p = self.run_persist("sync")
        self.assertIn("wifi: saved", p.stdout)

    def test_unknown_items_are_refused(self):
        self.assertEqual(self.run_persist("sync", "../etc/passwd", check=False).returncode, 1)
        self.assertEqual(self.run_persist("forget", "ssh", check=False).returncode, 1)
        self.assertEqual(self.run_persist("nonsense", check=False).returncode, 2)

    def test_one_item_can_be_synced_alone(self):
        self.set_settings()
        p = self.run_persist("sync", "timezone")
        self.assertIn("timezone: saved", p.stdout)
        self.assertNotIn("wifi", p.stdout)
        self.assertFalse(self.card("wifi.json").exists())

    # ---- only the list is ever touched, and status

    def test_nothing_outside_the_settings_folder_is_written_on_the_card(self):
        self.set_settings()
        self.run_persist("sync")
        made = sorted(str(p.relative_to(self.t / "card")) for p in (self.t / "card").rglob("*") if p.is_file())
        for m in made:
            self.assertTrue(m.startswith("r3i6nos/settings/") or m == ".r3i6nos-card", m)

    def test_status_with_items_compares_each_one(self):
        self.set_settings()
        self.run_persist("sync")
        self.card("timezone").write_text("Etc/GMT+5\n")
        (self.t / "var/r3i6nos/clock").unlink()
        self.card("mixer.state").write_text("garbage")
        out = self.run_persist("status", "--items").stdout
        self.assertIn("STATE=synced", out)
        self.assertIn("item=wifi state=same", out)
        self.assertIn("item=timezone state=differs", out)
        self.assertIn("item=clock state=card-only", out)
        self.assertIn("item=mixer state=card-invalid", out)

    def test_status_before_any_sync(self):
        self.assertIn("STATE=never", self.run_persist("status").stdout)

    def test_the_list_names_every_item(self):
        out = self.run_persist("list").stdout
        for item in ("wifi", "timezone", "clock-set", "clock", "mixer", "battery", "clock-asked"):
            self.assertIn(item + "\t", out)
        self.assertNotIn("ssh", out)


if __name__ == "__main__":
    unittest.main()

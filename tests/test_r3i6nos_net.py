"""r3i6nos-net, the root helper behind Settings > Network and SSH, against a fake /sys/class/net and a fake command runner.
Nothing here touches the network or needs root: `run` and `nap` are replaced."""
import importlib.machinery
import importlib.util
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

SRC = Path(__file__).resolve().parent.parent / "overlay/usr/local/bin/r3i6nos-net"


def load(env):
    with mock.patch.dict(os.environ, env):
        loader = importlib.machinery.SourceFileLoader("r3i6nos_net", str(SRC))
        spec = importlib.util.spec_from_loader("r3i6nos_net", loader)
        mod = importlib.util.module_from_spec(spec)
        sys.dont_write_bytecode = True
        loader.exec_module(mod)
    return mod


class Fake:
    """Answers commands from a table of (prefix tuple) -> (rc, output); records every call."""

    def __init__(self):
        self.calls, self.table = [], {}

    def __call__(self, cmd, timeout=15, text=None):
        self.calls.append((list(cmd), text))
        for n in range(len(cmd), 0, -1):
            hit = self.table.get(tuple(cmd[:n]))
            if hit is not None:
                return hit(cmd) if callable(hit) else hit
        return 0, ""

    def ran(self, *prefix):
        return any(tuple(c[:len(prefix)]) == prefix for c, _ in self.calls)


class Net(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = Path(self.tmp.name)
        self.t = t
        for d in ("sys", "state", "net", "ssh/sshd_config.d", "home/.ssh"):
            (t / d).mkdir(parents=True, exist_ok=True)
        self.m = load({
            "R3I6NOS_NET_DIR": str(t / "net"), "R3I6NOS_NET_OUT": str(t / "out"), "R3I6NOS_SYS_NET": str(t / "sys"), "R3I6NOS_NET_STATE": str(t / "state"),
            "R3I6NOS_RESOLV": str(t / "resolv.conf"), "R3I6NOS_SSH_ETC": str(t / "ssh"),
            "R3I6NOS_SSH_KEYS_SRC": str(t / "keys"), "R3I6NOS_SSH_HOME": str(t / "home/.ssh"),
        })
        self.f = Fake()
        self.m.run = self.f
        self.m.nap = lambda s: None
        self.m.wait_for = lambda pred, seconds, step=0.5: pred()      # no real waiting in tests
        (t / "resolv.conf").write_text("nameserver 192.168.1.1\n")

    def tearDown(self):
        self.tmp.cleanup()

    def iface(self, name, kind="wired", state="up", carrier="1"):
        d = self.t / "sys" / name
        d.mkdir(parents=True, exist_ok=True)
        (d / "operstate").write_text(state + "\n")
        (d / "carrier").write_text(carrier + "\n")
        (d / "address").write_text("aa:bb:cc:dd:ee:ff\n")
        if kind == "wifi":
            (d / "wireless").mkdir(exist_ok=True)
        else:
            (d / "device").mkdir(exist_ok=True)

    def ip_up(self, name, addr="192.168.1.20/24", gw="192.168.1.1"):
        self.f.table[("ip", "-4", "-o", "addr", "show", "dev", name)] = (0, f"2: {name}    inet {addr} brd 192.168.1.255 scope global {name}\n")
        self.f.table[("ip", "-4", "route", "show", "default", "dev", name)] = (0, f"default via {gw} dev {name}\n")

    def ask(self, **req):
        return self.m.handle(req)

    # ---- plumbing
    def test_request_and_result_round_trip(self):
        req = self.m.parse_request("id=7\nverb=wifi\nssid=My\\nNet\nnote=a\\\\b\n")
        self.assertEqual((req["id"], req["verb"], req["ssid"], req["note"]), ("7", "wifi", "My\nNet", "a\\b"))
        out = self.m.format_result("7", self.m.Result(True, ["hello\x1b[31m"], [("Home", "-40 dBm", "WPA")]))
        self.assertEqual(out.split("\n")[:2], ["id=7", "ok=1"])
        self.assertIn("L\thello?[31m", out)               # control characters never reach the screen
        self.assertIn("N\tHome\t-40 dBm\tWPA", out)

    def test_main_deletes_the_request_first_and_writes_the_result(self):
        (self.t / "net/request").write_text("id=3\nverb=nonsense\n")
        self.assertEqual(self.m.main(), 0)
        self.assertFalse((self.t / "net/request").exists())
        r = (self.t / "out/result").read_text()
        self.assertTrue(r.startswith("id=3\nok=0\n") and "Unknown request" in r)

    def test_the_answer_goes_only_into_the_root_folder(self):
        (self.t / "net/request").write_text("id=3\nverb=status\n")
        self.m.main()
        self.assertFalse((self.t / "net/result").exists() or (self.t / "net/result.tmp").exists(), "nothing is written beside the request")
        self.assertEqual(oct((self.t / "out").stat().st_mode & 0o777), "0o755")

    def test_a_planted_link_cannot_make_root_write_or_read_elsewhere(self):
        victim = self.t / "victim"
        victim.write_text("precious")
        # the request is a link: refused, never read
        (self.t / "net/request").symlink_to(victim)
        self.assertIsNone(self.m.read_request())
        (self.t / "net/request").unlink()
        # a link planted where the old design wrote its answer
        (self.t / "net/result.tmp").symlink_to(victim)
        (self.t / "net/request").write_text("id=1\nverb=status\n")
        self.m.main()
        self.assertEqual(victim.read_text(), "precious")
        self.assertEqual(victim.stat().st_uid, os.getuid())
        # a link planted at the result's temporary name in the answer folder is not followed either
        (self.t / "out" / f"result.{os.getpid()}").symlink_to(victim)
        (self.t / "net/request").write_text("id=2\nverb=status\n")
        with self.assertRaises(OSError):
            self.m.main()
        self.assertEqual(victim.read_text(), "precious")

    def test_a_huge_or_odd_request_is_not_taken(self):
        (self.t / "net/request").write_text("id=1\nverb=status\n" + "x" * 20000)
        self.assertEqual(self.m.read_request(), "")
        (self.t / "net/request").unlink()
        os.mkfifo(self.t / "net/request")
        self.assertEqual(self.m.read_request(), "", "not a regular file")

    def test_the_id_is_sanitised_and_an_unknown_verb_is_not_echoed(self):
        (self.t / "net/request").write_text("id=../../etc/x\nverb=secret stuff here\n")
        self.m.main()
        r = (self.t / "out/result").read_text()
        self.assertTrue(r.startswith("id=....etcx\nok=0\n"), r)
        self.assertNotIn("secret", r)

    def test_no_request_is_not_an_error(self):
        self.assertEqual(self.m.main(), 0)
        self.assertFalse((self.t / "out/result").exists())

    def test_a_crash_becomes_an_answer_not_silence(self):
        self.m.VERBS["status"] = lambda req: 1 / 0
        r = self.ask(verb="status")
        self.assertFalse(r.ok)
        self.assertIn("Internal error", r.lines[0])

    # ---- interfaces, status
    def test_only_real_interfaces_are_listed(self):
        for n in ("lo", "docker0", "veth12", "tun0"):
            (self.t / "sys" / n).mkdir()
        self.iface("eth0")
        self.iface("wlan0", "wifi")
        self.assertEqual(self.m.interfaces(), [("eth0", "wired"), ("wlan0", "wifi")])

    def test_status_without_any_adapter_explains_what_to_plug_in(self):
        r = self.ask(verb="status")
        self.assertTrue(r.ok)
        self.assertIn("No network adapter found.", r.lines)
        self.assertTrue(any("USB" in l for l in r.lines))

    def test_status_shows_address_gateway_dns_and_the_wifi_network(self):
        self.iface("eth0")
        self.iface("wlan0", "wifi")
        self.ip_up("eth0")
        self.f.table[("iw", "dev", "wlan0", "link")] = (0, "Connected to aa:bb\n\tSSID: Home\n\tsignal: -52 dBm\n")
        txt = "\n".join(self.ask(verb="status").lines)
        for want in ("eth0", "plugged in", "192.168.1.20/24", "192.168.1.1", "Home", "-52 dBm", "DNS: 192.168.1.1"):
            self.assertIn(want, txt)

    # ---- wired
    def test_wired_needs_a_cable_then_asks_dhcp(self):
        self.iface("eth0", carrier="0")
        r = self.ask(verb="wired")
        self.assertFalse(r.ok)
        self.assertIn("no cable link", r.lines[0])
        self.assertFalse(self.f.ran("udhcpc"))
        self.iface("eth0", carrier="1")
        self.ip_up("eth0")
        r = self.ask(verb="wired")
        self.assertTrue(r.ok, r.lines)
        self.assertTrue(self.f.ran("udhcpc", "-i", "eth0"))
        self.assertIn("Connected: eth0", r.lines[0])

    def test_wired_with_no_adapter_says_so(self):
        r = self.ask(verb="wired")
        self.assertFalse(r.ok)
        self.assertIn("No wired network adapter", r.lines[0])

    def test_dhcp_failure_is_reported_with_a_reason(self):
        self.iface("eth0")
        self.f.table[("udhcpc",)] = (1, "udhcpc: no lease, failing\n")
        r = self.ask(verb="wired")
        self.assertFalse(r.ok)
        self.assertIn("no address", r.lines[0])

    # ---- wifi
    SCAN = ("BSS aa:00:00:00:00:01(on wlan0)\n\tsignal: -70.00 dBm\n\tSSID: Cafe\n\tRSN:\t * Version: 1\n"
            "BSS aa:00:00:00:00:02(on wlan0)\n\tsignal: -40.00 dBm\n\tSSID: Home\n\tRSN:\t * Version: 1\n"
            "BSS aa:00:00:00:00:03(on wlan0)\n\tsignal: -60.00 dBm\n\tSSID: Home\n\tRSN:\t * Version: 1\n"
            "BSS aa:00:00:00:00:04(on wlan0)\n\tsignal: -80.00 dBm\n\tSSID: Guest\n"
            "BSS aa:00:00:00:00:05(on wlan0)\n\tsignal: -50.00 dBm\n\tSSID: \n")

    def test_scan_dedupes_sorts_by_signal_and_skips_hidden(self):
        self.iface("wlan0", "wifi")
        self.f.table[("iw", "dev", "wlan0", "scan")] = (0, self.SCAN)
        r = self.ask(verb="scan")
        self.assertEqual([n[0] for n in r.nets], ["Home", "Cafe", "Guest"])
        self.assertEqual(r.nets[0], ("Home", "-40 dBm", "WPA"))
        self.assertEqual(r.nets[2][2], "open")

    def test_scan_failure_is_a_reason(self):
        self.iface("wlan0", "wifi")
        self.f.table[("iw", "dev", "wlan0", "scan")] = (240, "command failed: Network is down (-100)\n")
        r = self.ask(verb="scan")
        self.assertFalse(r.ok)
        self.assertIn("scan failed", r.lines[0])

    def test_wifi_writes_a_private_hashed_config_and_saves_the_network(self):
        self.iface("wlan0", "wifi")
        self.ip_up("wlan0")
        self.f.table[("iw", "dev", "wlan0", "link")] = (0, "Connected to aa:bb\n\tSSID: Home\n\tsignal: -50 dBm\n")
        r = self.ask(verb="wifi", ssid="Home", psk="correct horse")
        self.assertTrue(r.ok, r.lines)
        conf = self.t / "out/private/wpa-wlan0.conf"
        self.assertEqual(oct(conf.stat().st_mode & 0o777), "0o600")
        body = conf.read_text()
        self.assertNotIn("correct horse", body)                          # only the derived key is written
        self.assertIn("ssid=" + "Home".encode().hex(), body)
        self.assertRegex(body, r"psk=[0-9a-f]{64}")
        self.assertTrue(self.f.ran("wpa_supplicant", "-B", "-i", "wlan0"))
        saved = (self.t / "state/wifi.json")
        self.assertEqual(oct(saved.stat().st_mode & 0o777), "0o600")
        self.assertNotIn("correct horse", saved.read_text())

    def test_known_wpa_psk_vector(self):
        # IEEE 802.11i annex H.4: passphrase "password", SSID "IEEE"
        self.assertEqual(self.m.psk_hex("IEEE", "password"), "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e")

    def test_open_network_needs_no_key(self):
        self.iface("wlan0", "wifi")
        self.ip_up("wlan0")
        self.f.table[("iw", "dev", "wlan0", "link")] = (0, "Connected to aa\n\tSSID: Guest\n")
        self.assertTrue(self.ask(verb="wifi", ssid="Guest").ok)
        self.assertIn("key_mgmt=NONE", (self.t / "out/private/wpa-wlan0.conf").read_text())

    def test_wifi_that_never_joins_is_stopped_and_explained(self):
        self.iface("wlan0", "wifi")
        self.m.wait_for = lambda pred, seconds, step=0.5: False
        r = self.ask(verb="wifi", ssid="Home", psk="wrongwrong")
        self.assertFalse(r.ok)
        self.assertIn("Could not join Home", r.lines[0])
        self.assertFalse((self.t / "state/wifi.json").exists(), "a network that did not work must not be saved")

    def test_wifi_arguments_are_checked(self):
        self.iface("wlan0", "wifi")
        for bad in (dict(ssid="x" * 33, psk="12345678"), dict(ssid="a\x01b", psk="12345678"), dict(ssid="Home", psk="short"),
                    dict(ssid="Home", psk="é" * 9)):
            r = self.ask(verb="wifi", **bad)
            self.assertFalse(r.ok, bad)
        self.assertFalse(self.f.ran("wpa_supplicant"))

    def test_wifi_with_nothing_saved_says_so_and_forget_clears_it(self):
        self.iface("wlan0", "wifi")
        self.assertIn("No saved Wi-Fi", self.ask(verb="wifi").lines[0])
        (self.t / "state/wifi.json").write_text('{"ssid": "Home", "psk": ""}')
        self.assertEqual(self.m.saved_wifi()["ssid"], "Home")
        self.ask(verb="forget")
        self.assertIsNone(self.m.saved_wifi())

    def test_disconnect_flushes_and_downs_every_interface(self):
        self.iface("eth0")
        self.iface("wlan0", "wifi")
        r = self.ask(verb="disconnect")
        self.assertTrue(r.ok)
        for n in ("eth0", "wlan0"):
            self.assertTrue(self.f.ran("ip", "addr", "flush", "dev", n))
            self.assertTrue(self.f.ran("ip", "link", "set", n, "down"))

    # ---- test
    def test_the_test_reports_each_step(self):
        self.iface("eth0")
        self.ip_up("eth0")
        self.m.socket.gethostbyname = lambda h: "151.101.0.1"
        r = self.ask(verb="test")
        txt = "\n".join(r.lines)
        self.assertTrue(r.ok, txt)
        for want in ("[ok] Link", "[ok] Address", "[ok] Gateway", "[ok] Ping the gateway", "[ok] DNS", "[ok] Internet: ping 1.1.1.1", "All tests passed."):
            self.assertIn(want, txt)

    def test_a_failing_step_says_what_to_try(self):
        self.iface("eth0")
        self.ip_up("eth0")
        def no_dns(h):
            raise OSError("no")
        self.m.socket.gethostbyname = no_dns
        self.f.table[("ping", "-c", "2", "-W", "2", "-I", "eth0", "1.1.1.1")] = (1, "")
        r = self.ask(verb="test")
        txt = "\n".join(r.lines)
        self.assertFalse(r.ok)
        self.assertIn("[FAIL] DNS", txt)
        self.assertIn("[FAIL] Internet", txt)
        self.assertIn("Some tests failed.", txt)

    def test_the_test_with_nothing_connected(self):
        self.assertIn("none found", self.ask(verb="test").lines[0])
        self.iface("eth0", state="down", carrier="0")
        self.assertIn("nothing is connected", self.ask(verb="test").lines[0])

    # ---- ssh
    def test_ssh_will_not_start_without_a_network_or_a_way_to_log_in(self):
        self.assertIn("Connect to a network", self.ask(verb="ssh-enable").lines[0])
        self.iface("eth0")
        self.ip_up("eth0")
        self.f.table[("passwd", "-S", "loveos")] = (0, "loveos L 2026-11-07 0 99999 7 -1\n")
        r = self.ask(verb="ssh-enable")
        self.assertFalse(r.ok)
        self.assertIn("Set an SSH password", r.lines[0])
        self.assertFalse(self.f.ran("systemctl", "start"))
        self.assertFalse((self.t / "ssh/sshd_config.d/r3i6nos.conf").exists())

    def test_ssh_with_a_password_writes_a_locked_down_config_and_starts(self):
        self.iface("eth0")
        self.ip_up("eth0")
        self.f.table[("passwd", "-S", "loveos")] = (0, "loveos P 2026-11-07 0 99999 7 -1\n")
        r = self.ask(verb="ssh-enable")
        self.assertTrue(r.ok, r.lines)
        conf = (self.t / "ssh/sshd_config.d/r3i6nos.conf").read_text()
        for want in ("AllowUsers loveos", "PermitRootLogin no", "PasswordAuthentication yes", "KbdInteractiveAuthentication no", "MaxAuthTries 3"):
            self.assertIn(want, conf)
        self.assertTrue(self.f.ran("usermod", "-s", "/bin/bash", "loveos"))
        self.assertTrue(self.f.ran("ssh-keygen", "-A"), "host keys are made on the first start, one set a card")
        self.assertTrue(self.f.ran("systemctl", "start", "ssh"))
        self.assertTrue(any("ssh loveos@192.168.1.20" in l for l in r.lines))

    def test_ssh_with_a_key_on_the_card_is_key_only(self):
        self.iface("eth0")
        self.ip_up("eth0")
        self.f.table[("passwd", "-S", "loveos")] = (0, "loveos L 2026-11-07 0 99999 7 -1\n")
        (self.t / "keys").write_text("ssh-ed25519 AAAAC3Nza me@laptop\n")
        r = self.ask(verb="ssh-enable")
        self.assertTrue(r.ok, r.lines)
        self.assertIn("PasswordAuthentication no", (self.t / "ssh/sshd_config.d/r3i6nos.conf").read_text())
        dest = self.t / "home/.ssh/authorized_keys"
        self.assertEqual(oct(dest.stat().st_mode & 0o777), "0o600")
        self.assertIn("key only", "\n".join(r.lines))

    def test_a_bad_key_file_is_refused(self):
        self.iface("eth0")
        self.ip_up("eth0")
        (self.t / "keys").write_text("   \n")
        self.assertFalse(self.ask(verb="ssh-enable").ok)
        (self.t / "keys").write_bytes(b"\x00\x01binary")
        self.assertFalse(self.ask(verb="ssh-enable").ok)

    def test_ssh_password_goes_to_chpasswd_on_stdin_only(self):
        r = self.ask(verb="ssh-password", password="a good one: with colon")
        self.assertTrue(r.ok)
        cmd, text = [c for c in self.f.calls if c[0][0] == "chpasswd"][0]
        self.assertEqual(cmd, ["chpasswd"], "the password must not be on a command line")
        self.assertEqual(text, "loveos:a good one: with colon\n")
        for bad in ("short", "x" * 65, "tab\there1234", "new\nline1234", "é" * 9):
            self.assertFalse(self.ask(verb="ssh-password", password=bad).ok, repr(bad))
        self.assertEqual(len([c for c in self.f.calls if c[0][0] == "chpasswd"]), 1)

    def test_ssh_stop_removes_the_config_and_the_shell(self):
        conf = self.t / "ssh/sshd_config.d/r3i6nos.conf"
        conf.write_text("x")
        self.assertTrue(self.ask(verb="ssh-disable").ok)
        self.assertFalse(conf.exists())
        self.assertTrue(self.f.ran("systemctl", "stop", "ssh"))
        self.assertTrue(self.f.ran("usermod", "-s", "/usr/sbin/nologin", "loveos"))

    def test_ssh_status_lists_the_ways_in(self):
        self.iface("eth0")
        self.ip_up("eth0")
        self.f.table[("systemctl", "is-active", "ssh")] = (0, "active\n")
        self.f.table[("passwd", "-S", "loveos")] = (0, "loveos P 2026-11-07 0 99999 7 -1\n")
        self.f.table[("ssh-keygen", "-lf")] = (0, "256 SHA256:abcd root@r3i6nos (ED25519)\n")
        txt = "\n".join(self.ask(verb="ssh-status").lines)
        for want in ("running", "Password:   set", "192.168.1.20", "SHA256:abcd"):
            self.assertIn(want, txt)


if __name__ == "__main__":
    unittest.main()

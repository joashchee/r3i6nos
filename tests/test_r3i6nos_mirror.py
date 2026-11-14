"""r3i6nos-mirror (ROADMAP section 28, docs/SCREEN-MIRROR.md): the daemon that serves the unit's cells as a web page.
A real daemon in a subprocess on loopback, a UDP socket standing in for an app, a plain socket standing in for a page. No hardware."""
import base64
import importlib.machinery
import importlib.util
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "overlay/usr/local/bin/r3i6nos-mirror"
PAGE = ROOT / "overlay/usr/share/r3i6nos/mirror/page.html"
CELLS = 64 * 24


def load():
    loader = importlib.machinery.SourceFileLoader("r3i6nos_mirror", str(SRC))
    spec = importlib.util.spec_from_loader("r3i6nos_mirror", loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


def free_udp_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def run_len(start, cells):
    """One run of a delta: u16 start, u8 count, then (character, attribute) pairs."""
    return struct.pack(">HB", start, len(cells)) + b"".join(bytes(c) for c in cells)


class Cells(unittest.TestCase):
    def setUp(self):
        self.m = load()

    def test_a_full_frame_replaces_the_screen(self):
        st = bytearray(self.m.BLANK)
        frame = bytes(range(256)) * 12
        self.assertEqual(self.m.parse_datagram(st, b"F" + frame), "full")
        self.assertEqual(bytes(st), frame)

    def test_a_short_or_long_frame_is_ignored(self):
        st = bytearray(self.m.BLANK)
        self.assertIsNone(self.m.parse_datagram(st, b"F" + bytes(100)))
        self.assertIsNone(self.m.parse_datagram(st, b"F" + bytes(self.m.FRAME + 1)))
        self.assertEqual(bytes(st), self.m.BLANK)

    def test_a_delta_changes_only_its_cells(self):
        st = bytearray(self.m.BLANK)
        d = run_len(10, [(65, 0x1F), (66, 0x2F)]) + run_len(CELLS - 1, [(90, 0x07)])
        self.assertEqual(self.m.parse_datagram(st, b"D" + d), "delta")
        self.assertEqual(bytes(st[20:24]), bytes([65, 0x1F, 66, 0x2F]))
        self.assertEqual(bytes(st[-2:]), bytes([90, 0x07]))
        self.assertEqual(bytes(st[:20]), self.m.BLANK[:20])
        self.assertEqual(bytes(st[24:-2]), self.m.BLANK[24:-2])

    def test_a_bad_delta_changes_nothing(self):
        for bad in (
            run_len(CELLS - 1, [(1, 1), (2, 2)]),                      # runs off the end of the screen
            struct.pack(">HB", 5, 0),                                  # an empty run
            struct.pack(">HB", 5, 3) + bytes(4),                       # cut short
            run_len(0, [(65, 7)]) + b"\x00",                           # a good run, then a torn one
        ):
            st = bytearray(self.m.BLANK)
            self.assertIsNone(self.m.parse_datagram(st, b"D" + bad), bad)
            self.assertEqual(bytes(st), self.m.BLANK, "a torn delta must not be half applied")

    def test_other_datagrams_are_ignored(self):
        st = bytearray(self.m.BLANK)
        for junk in (b"", b"X", b"Fx", b"D"):
            self.assertIsNone(self.m.parse_datagram(st, junk))

    def test_the_hub_gives_a_new_page_the_whole_screen_then_the_changes(self):
        hub = self.m.Hub()
        hub.feed(b"D" + run_len(0, [(72, 0x0F)]))
        q = hub.join()
        kind, data = q.get_nowait()
        self.assertEqual((kind, data[:2]), ("full", bytes([72, 0x0F])))
        hub.feed(b"D" + run_len(1, [(73, 0x0F)]))
        self.assertEqual(q.get_nowait(), ("delta", run_len(1, [(73, 0x0F)])))

    def test_a_page_too_slow_gets_the_whole_screen_not_a_gap(self):
        hub = self.m.Hub()
        q = hub.join()
        for i in range(self.m.QUEUE_DEPTH + 5):
            hub.feed(b"D" + run_len(i % CELLS, [(i % 256, 7)]))
        events = []
        while not q.empty():
            events.append(q.get_nowait())
        self.assertLess(len(events), self.m.QUEUE_DEPTH)
        self.assertEqual(events[0][0], "full", "after falling behind, the page restarts from the whole screen")
        seen = bytearray(self.m.BLANK)                    # what that page would show after replaying what it was left with
        for kind, data in events:
            self.m.parse_datagram(seen, (b"F" if kind == "full" else b"D") + data)
        self.assertEqual(bytes(seen), bytes(hub.state))

    def test_only_a_few_pages_at_once(self):
        hub = self.m.Hub()
        qs = [hub.join() for _ in range(self.m.MAX_VIEWERS)]
        self.assertTrue(all(qs))
        self.assertIsNone(hub.join())
        hub.leave(qs[0])
        self.assertIsNotNone(hub.join())


class Daemon(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name) / "mirror"
        self.ingest = free_udp_port()
        self.env = dict(os.environ, R3I6NOS_MIRROR_DIR=str(self.dir), R3I6NOS_MIRROR_PAGE=str(PAGE),
                        R3I6NOS_MIRROR_INGEST=str(self.ingest))
        self.proc = subprocess.Popen([sys.executable, "-I", str(SRC), "serve", "--bind", "127.0.0.1", "--allow-loopback", "--port", "0"],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=self.env)
        self.url = self.proc.stdout.readline().strip()
        self.assertTrue(self.url.startswith("http://127.0.0.1:"), self.url + self.proc.stderr.read() if self.proc.poll() else self.url)
        rest = self.url[len("http://"):]
        self.host, _, tail = rest.partition(":")
        port, _, path = tail.partition("/")
        self.port, self.base = int(port), "/" + path

    def tearDown(self):
        if self.proc.poll() is None:
            self.proc.terminate()
        try:
            self.proc.wait(5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        self.proc.stdout.close()
        self.proc.stderr.close()
        self.tmp.cleanup()

    def get(self, path, stream=False):
        s = socket.create_connection((self.host, self.port), timeout=5)
        s.sendall(f"GET {path} HTTP/1.1\r\nHost: x\r\n\r\n".encode())
        if stream:
            return s
        data = b""
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
        s.close()
        head, _, body = data.partition(b"\r\n\r\n")
        return int(head.split()[1]), head.decode().lower(), body

    def send(self, data):
        u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        u.sendto(data, ("127.0.0.1", self.ingest))
        u.close()

    @staticmethod
    def event(sock, buf):
        """Read one Server-Sent Event from sock (buf keeps the unread bytes): (name, bytes)."""
        while b"\n\n" not in buf[0]:
            chunk = sock.recv(65536)
            if not chunk:
                raise AssertionError("the stream closed")
            buf[0] += chunk
        block, _, buf[0] = buf[0].partition(b"\n\n")
        name = data = None
        for line in block.decode().split("\n"):
            if line.startswith("event: "):
                name = line[7:]
            if line.startswith("data: "):
                data = base64.b64decode(line[6:])
        return name, data

    def open_stream(self):
        s = self.get(self.base + "cells", stream=True)
        buf = [b""]
        while b"\r\n\r\n" not in buf[0]:
            buf[0] += s.recv(4096)
        _, _, buf[0] = buf[0].partition(b"\r\n\r\n")
        return s, buf

    def test_it_says_where_it_is_and_what_is_on(self):
        self.assertTrue((self.dir / "on").exists())
        info = dict(l.split("=", 1) for l in (self.dir / "info").read_text().split())
        self.assertEqual(info["url"], self.url)
        self.assertEqual(info["viewers"], "0")
        self.assertRegex(self.base, r"^/[0-9a-f]{8}/$")

    def test_the_page_is_served_under_the_token_only(self):
        code, head, body = self.get(self.base)
        self.assertEqual(code, 200)
        self.assertIn("text/html", head)
        self.assertIn("no-store", head)
        self.assertIn(b"<canvas", body)
        self.assertEqual(self.get("/")[0], 404)
        self.assertEqual(self.get("/00000000/")[0], 404)
        self.assertEqual(self.get("/00000000/cells")[0], 404)
        code, head, _ = self.get(self.base[:-1])
        self.assertEqual(code, 301)
        self.assertIn("location: " + self.base, head)

    def test_a_page_gets_the_screen_and_then_what_changes(self):
        s, buf = self.open_stream()
        name, data = self.event(s, buf)
        self.assertEqual((name, data), ("full", bytes([32, 0x07]) * CELLS))
        self.send(b"D" + run_len(3, [(72, 0x1F), (105, 0x1F)]))
        name, data = self.event(s, buf)
        self.assertEqual((name, data), ("delta", run_len(3, [(72, 0x1F), (105, 0x1F)])))
        frame = bytes([66, 0x4E]) * CELLS
        self.send(b"F" + frame)
        self.assertEqual(self.event(s, buf), ("full", frame))
        s.close()

    def test_a_page_that_comes_late_gets_the_current_screen(self):
        self.send(b"D" + run_len(0, [(88, 0x0A)]))
        deadline = time.time() + 5
        while time.time() < deadline:
            s, buf = self.open_stream()
            name, data = self.event(s, buf)
            s.close()
            if data[:2] == bytes([88, 0x0A]):
                break
            time.sleep(0.05)
        else:
            self.fail("the late page never saw the earlier change")

    def test_viewers_are_counted_in_the_info_file(self):
        s, buf = self.open_stream()
        self.event(s, buf)
        deadline = time.time() + 5
        while time.time() < deadline and "viewers=1" not in (self.dir / "info").read_text():
            time.sleep(0.02)
        self.assertIn("viewers=1", (self.dir / "info").read_text())
        s.close()

    def test_stop_ends_it_and_takes_the_flag_with_it(self):
        r = subprocess.run([sys.executable, "-I", str(SRC), "stop"], capture_output=True, text=True, env=self.env, timeout=10)
        self.assertIn("stopping", r.stdout)
        self.assertEqual(self.proc.wait(10), 0)
        self.assertFalse((self.dir / "on").exists())
        self.assertFalse((self.dir / "info").exists())

    def test_a_second_one_refuses(self):
        r = subprocess.run([sys.executable, "-I", str(SRC), "serve", "--bind", "127.0.0.1", "--allow-loopback", "--port", "0"],
                           capture_output=True, text=True, env=self.env, timeout=10)
        self.assertEqual(r.returncode, 3)
        self.assertIn("already running", r.stderr)

    def test_status_reports_the_address(self):
        r = subprocess.run([sys.executable, "-I", str(SRC), "status"], capture_output=True, text=True, env=self.env, timeout=10)
        self.assertEqual(r.returncode, 0)
        self.assertIn("url=" + self.url, r.stdout)


class Start(unittest.TestCase):
    """`start`: what Settings > Screen mirror runs."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name) / "mirror"
        self.env = dict(os.environ, R3I6NOS_MIRROR_DIR=str(self.dir), R3I6NOS_MIRROR_PAGE=str(PAGE),
                        R3I6NOS_MIRROR_INGEST=str(free_udp_port()))

    def tearDown(self):
        subprocess.run([sys.executable, "-I", str(SRC), "stop"], env=self.env, capture_output=True, timeout=10)
        deadline = time.time() + 5
        while (self.dir / "on").exists() and time.time() < deadline:
            time.sleep(0.05)
        self.tmp.cleanup()

    def cli(self, *args):
        return subprocess.run([sys.executable, "-I", str(SRC), *args], capture_output=True, text=True, env=self.env, timeout=20)

    def test_start_prints_the_address_and_leaves_it_running(self):
        r = self.cli("start", "--bind", "127.0.0.1", "--allow-loopback", "--port", "0")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertRegex(r.stdout.strip(), r"^http://127\.0\.0\.1:\d+/[0-9a-f]{8}/$")
        self.assertTrue((self.dir / "on").exists())
        self.assertIn("url=" + r.stdout.strip(), self.cli("status").stdout)
        again = self.cli("start", "--bind", "127.0.0.1", "--allow-loopback", "--port", "0")
        self.assertEqual(again.returncode, 0)
        self.assertIn("already running", again.stdout)
        self.assertIn(r.stdout.strip(), again.stdout, "a second start says where the first one is")

    def test_start_says_why_it_could_not(self):
        r = self.cli("start", "--iface", "nosuch0")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("no IPv4 address", r.stderr)
        self.assertFalse((self.dir / "on").exists())


class Refusals(unittest.TestCase):
    def run_cli(self, *args, env=None):
        e = dict(os.environ, **(env or {}))
        return subprocess.run([sys.executable, "-I", str(SRC), *args], capture_output=True, text=True, env=e, timeout=10)

    def test_it_never_listens_on_every_interface_or_loopback_by_default(self):
        with tempfile.TemporaryDirectory() as d:
            env = {"R3I6NOS_MIRROR_DIR": d}
            for addr in ("0.0.0.0", "127.0.0.1"):
                r = self.run_cli("serve", "--bind", addr, env=env)
                self.assertEqual(r.returncode, 2, addr)
                self.assertIn("only on a chosen interface", r.stderr)

    def test_an_interface_with_no_address_is_said_so(self):
        with tempfile.TemporaryDirectory() as d:
            r = self.run_cli("serve", "--iface", "nosuch0", env={"R3I6NOS_MIRROR_DIR": d})
            self.assertEqual(r.returncode, 2)
            self.assertIn("no IPv4 address", r.stderr)

    def test_stop_and_status_when_nothing_runs(self):
        with tempfile.TemporaryDirectory() as d:
            env = {"R3I6NOS_MIRROR_DIR": d}
            self.assertIn("not running", self.run_cli("stop", env=env).stdout)
            r = self.run_cli("status", env=env)
            self.assertEqual(r.returncode, 1)
            self.assertIn("not running", r.stdout)

    def test_a_stale_flag_is_not_a_running_daemon(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "on").write_text("999999\n")
            self.assertIn("not running", self.run_cli("stop", env={"R3I6NOS_MIRROR_DIR": d}).stdout)
            self.assertFalse((Path(d) / "on").exists())

    def test_list_names_interfaces_with_an_address_but_not_loopback(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "lo").mkdir()
            (Path(d) / "nosuch0").mkdir()
            r = self.run_cli("list", env={"R3I6NOS_SYS_NET": d})
            self.assertEqual(r.returncode, 0)
            self.assertEqual(r.stdout, "")


class Page(unittest.TestCase):
    def test_the_committed_page_is_what_the_font_and_template_give(self):
        r = subprocess.run([sys.executable, "-I", str(ROOT / "scripts/build_mirror_page.py"), "--check"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_the_page_carries_the_font_credit_and_stays_read_only(self):
        html = PAGE.read_text()
        self.assertIn("CC BY-SA 4.0", html)
        self.assertIn("VileR", html)
        for forbidden in ("XMLHttpRequest", "fetch(", "WebSocket", "sendBeacon", ".send("):
            self.assertNotIn(forbidden, html, "the page must send nothing to the unit")
        font = html.split('b64("', 1)[1].split('"', 1)[0]
        self.assertEqual(len(base64.b64decode(font)), 256 * 16)


if __name__ == "__main__":
    unittest.main()

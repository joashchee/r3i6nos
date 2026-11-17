"""r3i6nos-pkg's catalog verbs (check, available, get, update), system packages (the staging for r3i6nos-sysupdate) and LÖVE apps (install-love).

Built on test_r3i6nos_pkg's fakes (cards, the watcher's extraction and dpkg-deb's fields) and a real local HTTP server with Range support and a way to
cut a download off. The catalog's signature check is replaced by a stub in most tests; one class signs and checks with real OpenSSL 3 Ed25519, where it
exists (Debian 12, CI; not a Mac)."""
import hashlib
import http.server
import json
import os
import shutil
import subprocess
import tempfile
import threading
import time
import unittest
import zipfile
from pathlib import Path
from unittest import mock

import test_r3i6nos_pkg as base_tests

Base = base_tests.Base


class Server:
    """Serves a folder over http on a free port. cut=N closes the connection after N bytes of the body; ignore_range answers a Range with the whole file."""

    def __init__(self, root):
        self.root, self.cut, self.ignore_range, self.log = Path(root), None, False, []
        outer = self

        class H(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_GET(self):
                p = outer.root / self.path.lstrip("/")
                outer.log.append((self.path, self.headers.get("Range")))
                if not p.is_file():
                    self.send_error(404)
                    return
                data = p.read_bytes()
                rng = self.headers.get("Range")
                if rng and not outer.ignore_range:
                    start = int(rng.split("=")[1].split("-")[0])
                    if start >= len(data):
                        self.send_error(416)
                        return
                    body, code = data[start:], 206
                else:
                    body, code = data, 200
                self.send_response(code)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                if outer.cut is not None:
                    self.wfile.write(body[:outer.cut])
                    self.wfile.flush()
                    self.close_connection = True
                    return
                self.wfile.write(body)

        self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), H)
        self.url = f"http://127.0.0.1:{self.httpd.server_address[1]}"
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def close(self):
        self.httpd.shutdown()
        self.httpd.server_close()


class CatalogBase(Base):
    def setUp(self):
        super().setUp()
        t = self.root
        self.web = t / "web"
        self.web.mkdir()
        self.server = Server(self.web)
        self.addCleanup(self.server.close)
        self.cache = t / "cache"
        self.state, self.sysreq, self.sysout = t / "state", t / "run/sysupdate", t / "run/out"
        self.power, self.release = t / "power", t / "release"
        self.pub = t / "packages.pub"
        self.pub.write_text("not a real key\n")
        self.release.write_text("VERSION=0.1.2\n")
        self.state.mkdir()
        self.sysout.mkdir(parents=True)
        env = {"R3I6NOS_PKG_CACHE": str(self.cache), "R3I6NOS_PKG_ALLOW_HTTP": "1"}
        p = mock.patch.dict(os.environ, env)
        p.start()
        self.addCleanup(p.stop)
        m = self.mod
        for attr, val in (("CATALOG_URL", self.server.url), ("PUBKEY", self.pub), ("STATE", self.state), ("SYSREQ", self.sysreq), ("SYSOUT", self.sysout),
                          ("POWER", self.power), ("RELEASE", self.release), ("SYSWAIT", 1.0), ("TIMEOUT", 5.0)):
            setattr(m, attr, val)
        self.verified = []
        self.sig_ok = True

        def fake_verify(index, sig):
            self.verified.append(Path(index).name)
            if not self.sig_ok:
                raise m.Refused("the catalog's signature does not match the image's public key; it is not used")
        m.verify_catalog = fake_verify

    # a package the web serves: returns its catalog entry
    def publish(self, name, version="2.0", kind="app", body=None, min_system="0.1.0", **override):
        data = body if body is not None else (f"{name} {version} bytes " * 50).encode()
        (self.web / f"{name}.deb").write_bytes(data)
        e = {"name": name, "package": name.lower(), "version": version, "kind": kind, "arch": "arm64", "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
             "url": f"{self.server.url}/{name}.deb", "min_system": min_system, "description": f"{name} for tests", "data_dir": "", "size_installed": 1024}
        e.update(override)
        self.entries = [x for x in getattr(self, "entries", []) if x["name"] != name] + [e]
        self.write_index()
        return e

    def write_index(self):
        (self.web / "index.json").write_text(json.dumps({"format": 1, "generated": "2027-01-01T00:00:00Z", "packages": self.entries}))
        (self.web / "index.json.sig").write_bytes(b"sig")


class Check(CatalogBase):
    def test_check_lists_new_updates_and_current_and_checks_the_signature(self):
        self.make("old.deb", where=self.data_deb)
        self.mod.W.process_debs()
        self.publish("old", "2.0")
        self.publish("fresh", "1.0")
        rc, out, err = self.run_pkg("check")
        self.assertEqual(rc, 0, err)
        self.assertIn("old\tapp\t1.0\t2.0\tupdate", out)
        self.assertIn("fresh\tapp\t-\t1.0\tnew", out)
        self.assertIn("2 package(s) in the catalog; 1 update(s)", out)
        self.assertEqual(self.verified[0], "index.json.part", "checked before it replaced anything")
        self.assertTrue((self.cache / "index.json").is_file())

    def test_a_catalog_that_does_not_verify_is_not_kept_and_the_old_one_stays(self):
        self.publish("a", "1.0")
        self.assertEqual(self.run_pkg("check")[0], 0)
        old = (self.cache / "index.json").read_bytes()
        self.publish("a", "9.0")
        self.sig_ok = False
        rc, out, err = self.run_pkg("check")
        self.assertEqual(rc, 1)
        self.assertIn("signature does not match", err)
        self.assertEqual((self.cache / "index.json").read_bytes(), old)
        self.assertFalse(list(self.cache.glob("*.part")))

    def test_the_cache_is_checked_every_time_it_is_read(self):
        self.publish("a", "1.0")
        self.run_pkg("check")
        self.verified.clear()
        rc, out, err = self.run_pkg("available")
        self.assertEqual(rc, 0, err)
        self.assertEqual(self.verified, ["index.json"])
        self.sig_ok = False
        rc, out, err = self.run_pkg("available")
        self.assertEqual(rc, 1)
        self.assertIn("signature", err)

    def test_available_without_a_check_says_to_check(self):
        rc, out, err = self.run_pkg("available")
        self.assertEqual(rc, 1)
        self.assertIn("run `r3i6nos-pkg check`", err)

    def test_available_lists_only_what_is_not_installed(self):
        self.make("have.deb", where=self.data_deb)
        self.mod.W.process_debs()
        self.publish("have", "1.0")
        self.publish("fresh", "1.0")
        self.run_pkg("check")
        rc, out, err = self.run_pkg("available")
        self.assertIn("fresh\tapp", out)
        self.assertNotIn("have\t", out)

    def test_a_server_that_is_not_there(self):
        self.mod.CATALOG_URL = "http://127.0.0.1:1"
        rc, out, err = self.run_pkg("check")
        self.assertEqual(rc, 1)
        self.assertIn("could not reach", err)

    def test_only_https_is_used_unless_the_test_switch_is_on(self):
        with mock.patch.dict(os.environ, {"R3I6NOS_PKG_ALLOW_HTTP": ""}):
            rc, out, err = self.run_pkg("check")
        self.assertEqual(rc, 1)
        self.assertIn("not an https:// address", err)

    def test_entries_that_are_not_well_formed_are_ignored(self):
        self.publish("good", "1.0")
        self.entries.append({"name": "../evil", "kind": "app", "version": "1", "size": 1, "sha256": "0" * 64, "url": "x"})
        self.entries.append({"name": "short", "kind": "app", "version": "1", "size": 1, "sha256": "abc", "url": "x"})
        self.entries.append({"name": "weird", "kind": "root", "version": "1", "size": 1, "sha256": "0" * 64, "url": "x"})
        self.write_index()
        rc, out, err = self.run_pkg("check")
        self.assertIn("1 package(s) in the catalog", out)
        self.assertNotIn("evil", out)


class Get(CatalogBase):
    def test_get_downloads_checks_and_installs_an_app(self):
        self.publish("newapp", "1.5")
        self.run_pkg("check")
        rc, out, err = self.run_pkg("get", "--yes", "newapp")
        self.assertEqual(rc, 0, err)
        self.assertIn("installed newapp 1.0", out)           # the fake dpkg-deb says 1.0 for every file
        self.assertTrue((self.data_deb / "newapp.deb").is_file())
        self.assertFalse((self.data_deb / "newapp.deb.part").exists())
        self.assertTrue((self.launchers / "newapp.sh").exists())

    def test_a_download_that_is_not_the_catalogs_file_is_deleted_and_nothing_is_installed(self):
        e = self.publish("bad", "1.0")
        (self.web / "bad.deb").write_bytes(b"x" * e["size"])          # same size, different bytes
        self.run_pkg("check")
        rc, out, err = self.run_pkg("get", "--yes", "bad")
        self.assertEqual(rc, 1)
        self.assertIn("did not arrive as the catalog says", err)
        self.assertFalse((self.data_deb / "bad.deb").exists())
        self.assertFalse(list(self.data_deb.glob("*.part")))

    def test_a_cut_download_is_resumed_not_started_again(self):
        e = self.publish("big", "1.0", body=bytes(range(256)) * 400)
        self.run_pkg("check")
        self.server.cut = 30000
        rc, out, err = self.run_pkg("get", "--yes", "big")
        self.assertEqual(rc, 1)
        self.assertIn("run it again to resume", err)
        part = self.data_deb / "big.deb.part"
        self.assertEqual(part.stat().st_size, 30000, "the part is kept")
        self.server.cut = None
        self.server.log.clear()
        rc, out, err = self.run_pkg("get", "--yes", "big")
        self.assertEqual(rc, 0, err)
        self.assertEqual(self.server.log[0][1], "bytes=30000-", "asked for the rest only")
        self.assertEqual((self.data_deb / "big.deb").stat().st_size, e["size"])

    def test_a_server_that_ignores_range_still_gives_the_right_file(self):
        self.publish("big", "1.0", body=bytes(range(256)) * 400)
        self.run_pkg("check")
        (self.data_deb).mkdir(parents=True, exist_ok=True)
        (self.data_deb / "big.deb.part").write_bytes(b"\0" * 5000)      # a stale, wrong partial file
        self.server.ignore_range = True
        rc, out, err = self.run_pkg("get", "--yes", "big")
        self.assertEqual(rc, 0, err)

    def test_a_low_battery_refuses_and_a_charger_does_not(self):
        bat = self.power / "BAT0"
        bat.mkdir(parents=True)
        (bat / "type").write_text("Battery\n")
        (bat / "capacity").write_text("12\n")
        (bat / "status").write_text("Discharging\n")
        self.publish("a", "1.0")
        self.run_pkg("check")
        rc, out, err = self.run_pkg("get", "--yes", "a")
        self.assertEqual(rc, 1)
        self.assertIn("battery is at 12%", err)
        self.assertEqual(self.server.log[-1][0], "/index.json.sig", "it did not even start the download")
        (bat / "status").write_text("Charging\n")
        self.assertEqual(self.run_pkg("get", "--yes", "a")[0], 0)

    def test_a_package_that_needs_a_newer_base(self):
        self.publish("future", "1.0", min_system="9.0.0")
        self.run_pkg("check")
        rc, out, err = self.run_pkg("get", "--yes", "future")
        self.assertEqual(rc, 1)
        self.assertIn("needs a base of 9.0.0", err)

    def test_a_name_the_catalog_does_not_have(self):
        self.publish("a", "1.0")
        self.run_pkg("check")
        rc, out, err = self.run_pkg("get", "--yes", "nope")
        self.assertEqual(rc, 1)
        self.assertIn("not in the catalog", err)

    def test_it_asks_unless_told_not_to(self):
        self.publish("a", "1.0")
        self.run_pkg("check")
        rc, out, err = self.run_pkg("get", "a")
        self.assertEqual(rc, 1)
        self.assertIn("no terminal to ask on", err)
        rc, out, err = self.run_pkg("get", "a", answer="n")
        self.assertEqual(rc, 1)
        self.assertFalse((self.data_deb / "a.deb").exists())
        self.assertEqual(self.run_pkg("get", "a", answer="y")[0], 0)

    def test_a_catalog_app_that_says_it_is_a_system_package_is_refused_even_when_the_catalog_says_app(self):
        self.control["sneaky.deb"] = {"X-R3i6nos-Kind": "system"}
        self.publish("sneaky", "1.0")
        self.run_pkg("check")
        rc, out, err = self.run_pkg("get", "--yes", "sneaky")
        self.assertEqual(rc, 1)
        self.assertIn("system package", err)
        self.assertFalse((self.data_deb / "sneaky.deb").exists())

    def test_nothing_that_says_system_is_installed_by_install_or_the_watcher(self):
        self.control["sys.deb"] = {"X-R3i6nos-Kind": "system"}
        rc, out, err = self.run_pkg("install", "--yes", str(self.make("sys.deb")))
        self.assertEqual(rc, 1)
        self.assertIn("system package", err)
        self.make("sys2.deb", where=self.data_deb)
        self.control["sys2.deb"] = {"X-R3i6nos-Kind": "system"}
        self.mod.W.process_debs()
        self.assertEqual(self.mod.W.LAST["sys2"][0], "refused")
        self.assertFalse((self.launchers / "sys2.sh").exists())


class Update(CatalogBase):
    def test_update_gets_only_what_has_a_newer_version(self):
        self.make("one.deb", where=self.data_deb)
        self.make("two.deb", where=self.data_deb)
        self.mod.W.process_debs()
        self.publish("one", "1.0")                  # the same: nothing to do
        self.publish("two", "3.0")
        self.publish("fresh", "1.0")                # not installed: update never adds
        rc, out, err = self.run_pkg("update", "--yes")
        self.assertEqual(rc, 0, err)
        self.assertIn("installed two", out)
        self.assertNotIn("installed one", out)
        self.assertFalse((self.data_deb / "fresh.deb").exists())

    def test_up_to_date(self):
        self.make("one.deb", where=self.data_deb)
        self.mod.W.process_debs()
        self.publish("one", "1.0")
        rc, out, err = self.run_pkg("update", "--yes")
        self.assertIn("everything is up to date", out)


class System(CatalogBase):
    """get of a system package stages the signed catalog and the file for the root helper and waits for its answer."""

    def helper(self, ok=True, lines=("installed r3i6nos-system 2.0",), wait=0.0):
        seen = {}

        def run():
            deadline = time.time() + 5
            while time.time() < deadline:
                req = self.sysreq / "request"
                if req.is_file():
                    text = req.read_text()
                    seen["request"] = text
                    seen["files"] = sorted(p.name for p in self.sysreq.iterdir())
                    seen["deb"] = (self.sysreq / "r3i6nos-system.deb").read_bytes() if (self.sysreq / "r3i6nos-system.deb").exists() else None
                    rid = [ln for ln in text.splitlines() if ln.startswith("id=")][0][3:]
                    time.sleep(wait)
                    (self.sysout / "result").write_text(f"id={rid}\nok={1 if ok else 0}\n" + "".join(f"L\t{ln}\n" for ln in lines))
                    return
                time.sleep(0.02)
        th = threading.Thread(target=run, daemon=True)
        th.start()
        return th, seen

    def test_a_system_package_is_staged_for_the_helper_and_the_answer_is_shown(self):
        e = self.publish("r3i6nos-system", "2.0", kind="system", body=b"system bytes" * 100)
        self.run_pkg("check")
        th, seen = self.helper()
        rc, out, err = self.run_pkg("get", "--yes", "r3i6nos-system")
        th.join(3)
        self.assertEqual(rc, 0, err)
        self.assertIn("installed r3i6nos-system 2.0", out)
        self.assertIn("name=r3i6nos-system", seen["request"])
        self.assertEqual(seen["files"], ["index.json", "index.json.sig", "r3i6nos-system.deb", "request"], "everything was there when the request appeared")
        self.assertEqual(hashlib.sha256(seen["deb"]).hexdigest(), e["sha256"])
        self.assertEqual(sorted(p.name for p in self.sysreq.iterdir()), [], "the staged files are removed afterwards")
        self.assertFalse((self.data_deb / "r3i6nos-system.deb").exists(), "never put on a card as an app")
        self.assertFalse((self.cache / "r3i6nos-system.deb.part").exists())

    def test_the_helpers_refusal_is_the_answer(self):
        self.publish("r3i6nos-system", "2.0", kind="system")
        self.run_pkg("check")
        th, _ = self.helper(ok=False, lines=("the catalog's signature does not match the image's public key",))
        rc, out, err = self.run_pkg("get", "--yes", "r3i6nos-system")
        th.join(3)
        self.assertEqual(rc, 1)
        self.assertIn("signature does not match", err)

    def test_an_old_answer_is_not_taken_for_this_one(self):
        self.publish("r3i6nos-system", "2.0", kind="system")
        self.run_pkg("check")
        (self.sysout / "result").write_text("id=1\nok=1\nL\told news\n")
        rc, out, err = self.run_pkg("get", "--yes", "r3i6nos-system")
        self.assertEqual(rc, 1)
        self.assertIn("no answer from the system update helper", err)
        self.assertNotIn("old news", out)

    def test_a_system_update_needs_more_battery_than_an_app(self):
        bat = self.power / "BAT0"
        bat.mkdir(parents=True)
        (bat / "type").write_text("Battery\n")
        (bat / "capacity").write_text("25\n")
        (bat / "status").write_text("Discharging\n")
        self.publish("r3i6nos-system", "2.0", kind="system")
        self.publish("app", "1.0")
        self.run_pkg("check")
        rc, out, err = self.run_pkg("get", "--yes", "r3i6nos-system")
        self.assertEqual(rc, 1)
        self.assertIn("needs 30%", err)
        self.assertEqual(self.run_pkg("get", "--yes", "app")[0], 0, "25% is enough for an app (20%)")

    def test_the_system_package_shows_in_list_and_in_check(self):
        (self.state / "system-packages").write_text("r3i6nos-system 1.0\n")
        self.publish("r3i6nos-system", "2.0", kind="system")
        rc, out, err = self.run_pkg("check")
        self.assertIn("r3i6nos-system\tsystem\t1.0\t2.0\tupdate", out)
        rc, out, err = self.run_pkg("list")
        self.assertIn("r3i6nos-system\t1.0\tsystem", out)


class Love(Base):
    def make_love(self, name, folder="Love 11.5", files=None, where=None):
        p = (where or self.inbox) / folder / name
        p.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(p, "w") as z:
            for n, text in (files if files is not None else {"main.lua": "function love.draw() end\n"}).items():
                z.writestr(n, text)
        return p

    def test_a_love_app_goes_to_the_data_cards_folder_for_its_runtime(self):
        rc, out, err = self.run_pkg("install-love", "--yes", str(self.make_love("Game.love", "Love 12.0")))
        self.assertEqual(rc, 0, err)
        self.assertTrue((self.data_at / "Love 12.0/Game.love").is_file())
        self.assertFalse((self.sys_at / "Love 12.0/Game.love").exists())
        self.assertIn("installed Game (LÖVE 12.0", out)
        self.assertEqual(self.flag.read_text().strip(), "changed")

    def test_the_runtime_comes_from_the_option_when_the_folder_says_nothing(self):
        p = self.make_love("Game.love", "somewhere")
        self.assertEqual(self.run_pkg("install-love", "--yes", str(p))[0], 1)
        rc, out, err = self.run_pkg("install-love", "--yes", "--runtime", "11.5", str(p))
        self.assertEqual(rc, 0, err)
        self.assertTrue((self.data_at / "Love 11.5/Game.love").is_file())

    def test_the_system_card_when_there_is_no_data_card_and_the_one_copy_stays_where_it_is(self):
        self.set_cards(system=True, data=False)
        self.assertEqual(self.run_pkg("install-love", "--yes", str(self.make_love("Game.love")))[0], 0)
        self.assertTrue((self.sys_at / "Love 11.5/Game.love").is_file())
        self.set_cards(system=True, data=True)
        rc, out, err = self.run_pkg("install-love", "--yes", str(self.make_love("Game.love")))
        self.assertEqual(rc, 0, err)
        self.assertTrue((self.sys_at / "Love 11.5/Game.love.prev").is_file(), "the old copy is kept for a roll back")
        self.assertFalse((self.data_at / "Love 11.5/Game.love").exists(), "one copy: it stayed on the card that had it")

    def test_what_is_not_a_love_app_is_refused_and_nothing_is_left(self):
        junk = self.inbox / "Love 11.5" / "Junk.love"
        junk.parent.mkdir(parents=True)
        junk.write_bytes(b"not a zip")
        rc, out, err = self.run_pkg("install-love", "--yes", str(junk))
        self.assertEqual(rc, 1)
        self.assertIn("not a .love file", err)
        no_main = self.make_love("NoMain.love", files={"readme.txt": "x"})
        rc, out, err = self.run_pkg("install-love", "--yes", str(no_main))
        self.assertIn("no main.lua", err)
        self.assertFalse(list((self.data_at).rglob("*.love*")), "no .love and no .part is left on the card")

    def test_unsafe_names_and_missing_cards(self):
        p = self.make_love("bad name.love")
        self.assertIn("not a usable app file name", self.run_pkg("install-love", "--yes", str(p))[2])
        self.set_cards(system=False, data=False)
        rc, out, err = self.run_pkg("install-love", "--yes", str(self.make_love("Game.love")))
        self.assertIn("no card is mounted", err)

    def test_the_inbox_takes_love_files_from_its_runtime_folders(self):
        self.make_love("A.love", "Love 11.5")
        self.make_love("B.love", "Love 12.0")
        self.make("tool.deb")
        rc, out, err = self.run_pkg("install", "--inbox", "--yes")
        self.assertEqual(rc, 0, err)
        self.assertTrue((self.data_at / "Love 11.5/A.love").is_file())
        self.assertTrue((self.data_at / "Love 12.0/B.love").is_file())
        self.assertTrue((self.data_deb / "tool.deb").is_file())
        self.assertFalse((self.inbox / "Love 11.5/A.love").exists(), "the inbox is emptied")

    def test_it_asks_unless_told_not_to(self):
        p = self.make_love("Game.love")
        rc, out, err = self.run_pkg("install-love", str(p))
        self.assertEqual(rc, 1)
        self.assertIn("no terminal to ask on", err)
        self.assertEqual(self.run_pkg("install-love", str(p), answer="y")[0], 0)


def real_tools():
    if not shutil.which("openssl"):
        return False
    r = subprocess.run(["openssl", "version"], capture_output=True, text=True)
    return r.returncode == 0 and r.stdout.startswith("OpenSSL 3")


@unittest.skipUnless(real_tools(), "needs OpenSSL 3 (Ed25519 with -rawin)")
class RealSignature(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = Path(self.tmp.name)
        self.t = t
        subprocess.run(["openssl", "genpkey", "-algorithm", "ed25519", "-out", str(t / "k.pem")], check=True, capture_output=True)
        subprocess.run(["openssl", "pkey", "-in", str(t / "k.pem"), "-pubout", "-out", str(t / "pub.pem")], check=True, capture_output=True)
        (t / "index.json").write_text('{"format": 1, "packages": []}\n')
        subprocess.run(["openssl", "pkeyutl", "-sign", "-inkey", str(t / "k.pem"), "-rawin", "-in", str(t / "index.json"), "-out", str(t / "index.json.sig")],
                       check=True, capture_output=True)
        with mock.patch.dict(os.environ, {"R3I6NOS_WATCHER": str(base_tests.BIN / "deb_watcher.py")}):
            self.mod = base_tests.load({})
        self.mod.PUBKEY = t / "pub.pem"

    def tearDown(self):
        self.tmp.cleanup()

    def test_a_good_signature_passes_and_a_changed_catalog_does_not(self):
        self.mod.verify_catalog(self.t / "index.json", self.t / "index.json.sig")
        (self.t / "index.json").write_text('{"format": 1, "packages": [{"name": "evil"}]}\n')
        with self.assertRaises(self.mod.Refused):
            self.mod.verify_catalog(self.t / "index.json", self.t / "index.json.sig")

    def test_another_key_does_not_pass(self):
        subprocess.run(["openssl", "genpkey", "-algorithm", "ed25519", "-out", str(self.t / "o.pem")], check=True, capture_output=True)
        subprocess.run(["openssl", "pkey", "-in", str(self.t / "o.pem"), "-pubout", "-out", str(self.t / "opub.pem")], check=True, capture_output=True)
        self.mod.PUBKEY = self.t / "opub.pem"
        with self.assertRaises(self.mod.Refused):
            self.mod.verify_catalog(self.t / "index.json", self.t / "index.json.sig")

    def test_no_key_in_the_image(self):
        self.mod.PUBKEY = self.t / "none.pem"
        with self.assertRaises(self.mod.Refused) as cm:
            self.mod.verify_catalog(self.t / "index.json", self.t / "index.json.sig")
        self.assertIn("no catalog public key", str(cm.exception))


if __name__ == "__main__":
    unittest.main()

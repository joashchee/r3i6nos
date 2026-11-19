"""apps/musicue-native/musicue-sync: the sync helper's id parsing, the cue-file fetch (never over a person's own file), the yt-dlp call (Creative Commons by default) and the
progress lines the app reads. A local web server stands in for cue.chordica.com and a script for yt-dlp: nothing leaves the machine."""
import http.server
import importlib.machinery
import importlib.util
import io
import os
import stat
import sys
import tempfile
import threading
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

SRC = Path(__file__).resolve().parent.parent / "apps/musicue-native/musicue-sync"


def load():
    loader = importlib.machinery.SourceFileLoader("musicue_sync", str(SRC))
    spec = importlib.util.spec_from_loader("musicue_sync", loader)
    mod = importlib.util.module_from_spec(spec)
    sys.dont_write_bytecode = True
    loader.exec_module(mod)
    return mod


class Handler(http.server.BaseHTTPRequestHandler):
    files = {}
    hits = []

    def do_GET(self):
        Handler.hits.append(self.path)
        body = Handler.files.get(self.path.rsplit("/", 1)[-1])
        if body is None:
            self.send_response(404)
            self.end_headers()
            return
        self.send_response(200)
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


class Sync(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.srv = http.server.HTTPServer(("127.0.0.1", 0), Handler)
        cls.base = f"http://127.0.0.1:{cls.srv.server_address[1]}/data/"
        threading.Thread(target=cls.srv.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.srv.shutdown()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = Path(self.tmp.name)
        (self.d / "inbox").mkdir()
        Handler.files = {}
        Handler.hits = []
        self.m = load()
        self.m.CUES_BASE = self.base
        self.m.TIMEOUT = 5

    def tearDown(self):
        self.tmp.cleanup()

    def urls(self, text):
        (self.d / "inbox/urls.txt").write_text(text)

    def fake_ytdlp(self, body):
        p = self.d / "fake-yt-dlp"
        p.write_text("#!/bin/sh\n" + body)
        p.chmod(p.stat().st_mode | stat.S_IEXEC)
        return str(p)

    def run_sync(self, *args):
        out = io.StringIO()
        with redirect_stdout(out):
            rc = self.m.main(["--data", str(self.d), *args])
        return rc, out.getvalue().splitlines()

    # ---- ids

    def test_video_ids_from_links_and_bare_ids(self):
        v = self.m.video_id
        self.assertEqual(v("dQw4w9WgXcQ"), "dQw4w9WgXcQ")
        self.assertEqual(v("https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=42s"), "dQw4w9WgXcQ")
        self.assertEqual(v("https://youtu.be/dQw4w9WgXcQ?si=x"), "dQw4w9WgXcQ")
        self.assertEqual(v("https://www.youtube.com/shorts/dQw4w9WgXcQ"), "dQw4w9WgXcQ")
        self.assertEqual(v("https://www.youtube.com/watch?list=PL1&v=dQw4w9WgXcQ"), "dQw4w9WgXcQ")
        self.assertEqual(v("dQw4w9WgXcQ | my note"), "dQw4w9WgXcQ")
        for bad in ("", "# dQw4w9WgXcQ", "../../etc/passwd", "short", "dQw4w9WgXcQ0", "https://example.com/watch?v=dQw4w9WgXcQ", "dQw4w9Wg/cQ"):
            self.assertIsNone(v(bad), bad)

    def test_the_list_is_read_once_per_id_in_order(self):
        self.urls("# my songs\naaaaaaaaaaa\nhttps://youtu.be/bbbbbbbbbbb\naaaaaaaaaaa\nnot a link\n\nccccccccccc | note\n")
        self.assertEqual(self.m.read_ids(str(self.d / "inbox/urls.txt")), ["aaaaaaaaaaa", "bbbbbbbbbbb", "ccccccccccc"])
        self.assertIsNone(self.m.read_ids(str(self.d / "inbox/none.txt")))

    # ---- nothing to do

    def test_no_list_or_an_empty_one_says_so(self):
        rc, lines = self.run_sync()
        self.assertEqual(rc, 0)
        self.assertTrue(lines[-1].startswith("DONE nothing to do"), lines)
        self.urls("# nothing here\n")
        rc, lines = self.run_sync()
        self.assertEqual(lines[-1], "DONE urls.txt has no links or ids")

    # ---- the cue files

    def test_server_cue_files_are_saved_and_a_404_is_not_an_error(self):
        Handler.files = {"aaaaaaaaaaa.json": b'{"markers": []}', "aaaaaaaaaaa.srt": b"1\n00:00:01,000 --> 00:00:02,000\nC\n"}
        self.urls("aaaaaaaaaaa\nbbbbbbbbbbb\n")
        rc, lines = self.run_sync("--no-audio")
        self.assertEqual(rc, 0)
        self.assertEqual((self.d / "cues/aaaaaaaaaaa.json").read_bytes(), b'{"markers": []}')
        self.assertTrue((self.d / "cues/aaaaaaaaaaa.srt").is_file())
        self.assertFalse((self.d / "cues/bbbbbbbbbbb.json").exists())
        self.assertIn("PROGRESS 1/2 aaaaaaaaaaa: 2 cue files", lines)
        self.assertIn("PROGRESS 2/2 bbbbbbbbbbb: no cues on the server", lines)
        self.assertTrue(lines[-1].startswith("DONE 2 videos: 2 cue files, 0 audio saved"), lines)

    def test_a_persons_own_cue_file_is_never_overwritten(self):
        (self.d / "cues").mkdir()
        (self.d / "cues/aaaaaaaaaaa.json").write_text("MINE")
        Handler.files = {"aaaaaaaaaaa.json": b'{"markers": []}', "aaaaaaaaaaa.srt": b"x"}
        self.urls("aaaaaaaaaaa\n")
        rc, lines = self.run_sync("--no-audio")
        self.assertEqual((self.d / "cues/aaaaaaaaaaa.json").read_text(), "MINE")
        self.assertTrue((self.d / "cues/aaaaaaaaaaa.srt").is_file(), "the file that was not there is still added")
        self.assertFalse(list((self.d / "cues").glob("*.part")))

    def test_a_home_page_answered_for_every_path_is_not_a_cue_file(self):
        Handler.files = {"aaaaaaaaaaa.json": b"<!DOCTYPE html><html>app</html>", "aaaaaaaaaaa.srt": b"<html>app</html>"}
        self.urls("aaaaaaaaaaa\n")
        self.run_sync("--no-audio")
        self.assertFalse((self.d / "cues/aaaaaaaaaaa.json").exists())

    def test_a_huge_file_is_refused_and_a_dead_server_is_reported_not_raised(self):
        Handler.files = {"aaaaaaaaaaa.json": b"x" * ((1 << 20) + 10)}
        self.urls("aaaaaaaaaaa\n")
        rc, lines = self.run_sync("--no-audio")
        self.assertEqual(rc, 0)
        self.assertFalse((self.d / "cues/aaaaaaaaaaa.json").exists())
        self.assertTrue(any("too big" in ln for ln in lines), lines)
        self.m.CUES_BASE = "http://127.0.0.1:1/data/"
        rc, lines = self.run_sync("--no-audio")
        self.assertEqual(rc, 0)
        self.assertTrue(any(ln.startswith("PROGRESS") and "cue server:" in ln for ln in lines), lines)

    # ---- the audio

    def test_audio_is_fetched_with_the_creative_commons_filter_by_default(self):
        log = self.d / "args.txt"
        yt = self.fake_ytdlp(f'echo "$@" > {log}\nfor a; do last="$a"; done\nfor a; do case "$prev" in -o) out="$a" ;; esac; prev="$a"; done\n'
                             f'mkdir -p "$(dirname "$out")"; : > "$(dirname "$out")/Song [aaaaaaaaaaa].m4a"\nexit 0\n')
        self.urls("aaaaaaaaaaa\n")
        rc, lines = self.run_sync("--yt-dlp", yt)
        args = log.read_text()
        self.assertIn("--match-filter license*=Creative Commons", args)
        self.assertIn("-f ba[acodec^=mp4a]/ba/b", args)
        self.assertIn("--no-playlist", args)
        self.assertIn("https://www.youtube.com/watch?v=aaaaaaaaaaa", args)
        self.assertIn("/media/%(title).80s [%(id)s].%(ext)s", args)
        self.assertIn("PROGRESS 1/1 aaaaaaaaaaa: audio saved", lines)
        self.assertTrue(lines[-1].startswith("DONE 1 video: 0 cue files, 1 audio saved"), lines)

    def test_one_id_is_fetched_alone_and_urls_txt_is_not_read(self):
        log = self.d / "args.txt"
        yt = self.fake_ytdlp(f'echo "$@" >> {log}\nexit 0\n')
        self.urls("bbbbbbbbbbb\n")
        rc, lines = self.run_sync("--yt-dlp", yt, "--id", "aaaaaaaaaaa")
        self.assertEqual(rc, 0)
        self.assertIn("watch?v=aaaaaaaaaaa", log.read_text())
        self.assertNotIn("bbbbbbbbbbb", log.read_text())
        self.assertTrue(lines[-1].startswith("DONE 1 video"), lines)

    def test_an_id_that_is_not_eleven_characters_is_refused(self):
        rc, lines = self.run_sync("--id", "../../etc/pw")
        self.assertEqual(rc, 2)
        self.assertTrue(lines[-1].startswith("DONE that is not a YouTube id"), lines)

    def test_any_license_lifts_the_filter(self):
        log = self.d / "args.txt"
        yt = self.fake_ytdlp(f'echo "$@" > {log}\nexit 0\n')
        self.urls("aaaaaaaaaaa\n")
        self.run_sync("--yt-dlp", yt, "--any-license")
        self.assertNotIn("--match-filter", log.read_text())

    def test_a_video_that_is_not_creative_commons_is_skipped_and_said(self):
        yt = self.fake_ytdlp('echo "[download] Song does not pass filter (license*=Creative Commons), skipping .." \nexit 0\n')
        self.urls("aaaaaaaaaaa\n")
        rc, lines = self.run_sync("--yt-dlp", yt)
        self.assertTrue(any("not Creative Commons" in ln for ln in lines), lines)
        self.assertTrue(lines[-1].endswith("1 skipped, 0 failed"), lines)

    def test_audio_already_in_media_is_not_fetched_again(self):
        (self.d / "media").mkdir()
        (self.d / "media/Some title [aaaaaaaaaaa].m4a").write_text("x")
        yt = self.fake_ytdlp("echo CALLED >> " + str(self.d / "called.txt") + "\nexit 0\n")
        self.urls("aaaaaaaaaaa\n")
        rc, lines = self.run_sync("--yt-dlp", yt)
        self.assertFalse((self.d / "called.txt").exists())
        self.assertIn("PROGRESS 1/1 aaaaaaaaaaa: audio already in media/", lines)

    def test_a_failing_yt_dlp_says_its_error_and_the_sync_goes_on(self):
        yt = self.fake_ytdlp('echo "ERROR: [youtube] aaaaaaaaaaa: Video unavailable" >&2\nexit 1\n')
        self.urls("aaaaaaaaaaa\nbbbbbbbbbbb\n")
        rc, lines = self.run_sync("--yt-dlp", yt)
        self.assertEqual(rc, 0)
        self.assertTrue(any("Video unavailable" in ln for ln in lines), lines)
        self.assertTrue(lines[-1].endswith("0 skipped, 2 failed"), lines)

    def test_no_audio_never_runs_yt_dlp(self):
        yt = self.fake_ytdlp("echo CALLED >> " + str(self.d / "called.txt") + "\nexit 0\n")
        self.urls("aaaaaaaaaaa\n")
        self.run_sync("--yt-dlp", yt, "--no-audio")
        self.assertFalse((self.d / "called.txt").exists())

    def test_without_yt_dlp_only_cue_data_is_fetched_and_it_says_so(self):
        self.urls("aaaaaaaaaaa\n")
        with mock.patch.object(self.m.shutil, "which", return_value=None):
            rc, lines = self.run_sync()
        self.assertTrue(any("yt-dlp is not installed" in ln for ln in lines), lines)
        self.assertEqual(rc, 0)

    def test_it_sends_nothing_but_the_requests(self):
        Handler.files = {"aaaaaaaaaaa.json": b"{}"}
        self.urls("aaaaaaaaaaa\n")
        self.run_sync("--no-audio")
        self.assertEqual(Handler.hits, ["/data/aaaaaaaaaaa.json", "/data/aaaaaaaaaaa.srt"])


if __name__ == "__main__":
    unittest.main()

"""The command-line tools' Rec demo (tools/qjs-node/demo.py, docs/DEMO-RECORDINGS.md "Command-line tools"): run against a fake tool, the demo on a pretend
card must make numbered PNGs (read back to their pixels), the sound, the copies of the files the tour made and a log in the report grammar with a power
section, follow the lock, the checklist (--features), the cancel and the hot-plug failure, replace an earlier run's files but keep its videos, and make
the same bytes twice."""
import os
import re
import shutil
import signal
import stat
import struct
import subprocess
import sys
import tempfile
import time
import unittest
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEMO = REPO / "tools/qjs-node/demo.py"
HEADER = REPO / "theme/r3i6nos_vga8x16.h"

COMMANDS = """# a fake tool's commands, with the demo's directives in comments (report.py ignores them)
#@hero first: The first feature
#@hero second: The second feature
#@hero third: A feature left unticked in some runs
ok
#@feature first
#@say Prints two lines and makes a file.
ok
#@say Fails on purpose, with a message.
bad --strict
!SLOW ok slowly
#@feature second
#@say Makes a sound.
wav o/beep.wav
ok
#@feature third
long
"""


def tool_script(events):
    return ('#!/bin/sh\ncase "$1" in\n'
            '  ok) echo "fine, line one"; echo "line two"; echo made > o/out.txt ;;\n'
            '  bad) echo "no such kit" >&2; exit 2 ;;\n'
            f'  hotplug) printf "99999999999999999\\terror\\tUSB device failed\\n" >> "{events}" ;;\n'
            '  long) sleep 30 ;;\n'
            '  wav) python3 - "$2" <<PY\nimport sys, wave, struct\nw = wave.open(sys.argv[1], "wb")\nw.setnchannels(1); w.setsampwidth(2); w.setframerate(22050)\n'
            'w.writeframes(b"".join(struct.pack("<h", 8000 if (i // 40) % 2 else -8000) for i in range(11025)))\nw.close()\nPY\n ;;\n'
            'esac\n')


def png_info(data):
    """(width, height, depth, colour type, number of pixels decoded) of a PNG, with every chunk's CRC checked and the scanlines decompressed."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    at, idat, ihdr = 8, b"", None
    while at < len(data):
        n, tag = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + n]
        assert struct.unpack(">I", data[at + 8 + n:at + 12 + n])[0] == zlib.crc32(tag + body) & 0xFFFFFFFF, f"crc of {tag}"
        if tag == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif tag == b"IDAT":
            idat += body
        at += 12 + n
    w, h, depth, ctype = ihdr[0], ihdr[1], ihdr[2], ihdr[3]
    raw = zlib.decompress(idat)
    stride = 1 + (w * depth + 7) // 8
    assert len(raw) == stride * h, (len(raw), stride, h)
    return w, h, depth, ctype, raw, stride


def pixel(info, x, y):
    w, h, depth, ctype, raw, stride = info
    b = raw[y * stride + 1 + x // 2]
    return (b >> 4) if x % 2 == 0 else (b & 15)


REPORT = REPO / "tools/qjs-node/report.py"


class Fixture:
    def __init__(self, t, commands=COMMANDS):
        self.t = Path(t)
        # the scripts as package-tool.sh installs them, beside each other: <command>-demo (baked with the font) and <command>-report
        self.script = self.t / "fake-demo"
        subprocess.run([sys.executable, str(DEMO), "--bake", str(self.script), "fake", "Fake App", "9.9", "r36s-fake", "abc1234", "2026-11-12", "A fake tool", str(HEADER)], check=True)
        (self.t / "fake-report").write_text(REPORT.read_text().replace("@TOOL@", "fake").replace("@APP@", "Fake App").replace("@VERSION@", "9.9").replace("@PACKAGE@", "r36s-fake"))
        self.roms, self.run, self.events = self.t / "roms", self.t / "run", self.t / "events"
        self.events.write_text("1\tinfo\told\n")
        self.tool = self.t / "fake"
        self.tool.write_text(tool_script(self.events))
        self.tool.chmod(self.tool.stat().st_mode | stat.S_IEXEC)
        self.commands = self.t / "commands.txt"
        self.commands.write_text(commands)
        self.demo = self.roms / "Fake App/Demo"

    def env(self, **more):
        e = {**os.environ, "R3I6NOS_ROMS": str(self.roms), "R3I6NOS_RUN": str(self.run), "R3I6NOS_REPORT_TOOL": str(self.tool),
             "R3I6NOS_REPORT_COMMANDS": str(self.commands), "R3I6NOS_REPORT_POWER_S": "0", "R3I6NOS_JS": "/nonexistent",
             "R3I6NOS_HOTPLUG_EVENTS": str(self.events), "R3I6NOS_FFMPEG": "/nonexistent"}
        e.update(more)
        return e

    def go(self, *args, **env):
        return subprocess.run([sys.executable, str(self.script), "--quiet", *args], env=self.env(**env), capture_output=True, text=True, timeout=240)

    def log(self):
        return (self.demo / "rec-demo.txt").read_text()


class ToolDemo(unittest.TestCase):
    def test_a_whole_run(self):
        with tempfile.TemporaryDirectory() as t:
            f = Fixture(t)
            out = f.go("--no-video", "--features", "first,second")
            self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
            names = sorted(p.name for p in f.demo.iterdir())
            pngs = [n for n in names if re.match(r"\d\d-.*\.png$", n)]
            self.assertTrue(pngs[0].startswith("01-splash"), pngs)
            self.assertTrue(pngs[-1].endswith("-end.png") or "end" in pngs[-1])
            for n in pngs:
                info = png_info((f.demo / n).read_bytes())
                self.assertEqual(info[:4], (640, 480, 4, 3), n)
            # the splash: the version line is there in pixels, not only in the log (some cell of row 13 is lit)
            splash = png_info((f.demo / pngs[0]).read_bytes())
            lit = {pixel(splash, x, y) for x in range(150, 490) for y in range(13 * 20, 14 * 20)}
            self.assertGreater(len(lit), 1, "the version line is drawn on the splash")
            frames = [n for n in names if n.endswith("-frames.pngs")]
            wavs = [n for n in names if n.endswith(".wav")]
            self.assertEqual((len(frames), len(wavs)), (1, 1), "with no video the frames and the sound are kept")
            self.assertTrue((f.demo / "demo-files").is_dir())
            self.assertTrue(any(p.name.endswith("out.txt") for p in (f.demo / "demo-files").iterdir()))
            # the WAV: 16-bit stereo 44.1 kHz, as long as the video, with sound in it where the wav step shows
            import wave
            with wave.open(str(f.demo / wavs[0]), "rb") as w:
                self.assertEqual((w.getnchannels(), w.getsampwidth(), w.getframerate()), (2, 2, 44100))
                data = w.readframes(w.getnframes())
                self.assertTrue(any(data), "the tool's own .wav is mixed in")
                nframes = w.getnframes()
            text = f.log()
            lines = text.splitlines()
            self.assertEqual(lines[0], "# r3i6nos rec demo")
            self.assertEqual(lines[-1], "end")
            for sec in ("build", "runtime", "shows", "tour", "steps", "files", "timing", "video", "chapters", "unit", "power", "summary"):
                self.assertIn(f"== {sec} ==", text, sec)
            body = [l for l in lines if l and not l.startswith(("==", "#", "app:", "made:", "how:", "for:", "PASS ", "FAIL ", "INFO ", "SKIP ", "end")) and not re.match(r"PASS \d+  FAIL", l)]
            self.assertEqual(body, [], "every line is a fact, a section or the header")
            counts = {s: len(re.findall(rf"^{s} [^:\n]+: ", text, re.M)) for s in ("PASS", "FAIL", "INFO", "SKIP")}
            self.assertIn(f"PASS {counts['PASS']}  FAIL {counts['FAIL']}  INFO {counts['INFO']}  SKIP {counts['SKIP']}", text)
            self.assertIn("INFO build: v9.9 abc1234 2026-11-12", text)
            self.assertIn("INFO shows: first, second", text)
            self.assertIn("SKIP third: not chosen", text)
            self.assertRegex(text, r"PASS hero features shown: 2 of 2")
            self.assertRegex(text, r"PASS first frame: the splash, with the version line")
            self.assertRegex(text, r"PASS audio: 1 of the tool's own \.wav outputs mixed in")
            self.assertRegex(text, r"PASS video length: [0-9.]+ s \(at most 90 s\)")
            self.assertRegex(text, r"SKIP video: not made \(--no-video\)")
            self.assertRegex(text, r"INFO 2 first fake bad --strict: exit 2, ")
            self.assertRegex(text, r"INFO phase the tool's commands: ")
            self.assertRegex(text, r"INFO phase drawing the frames: ")
            self.assertRegex(text, r"INFO cut first: ")
            self.assertRegex(text, r"INFO first: [0-9.]+ s \(\d+ frames, \d+% of the video\)")
            self.assertEqual(nframes, int(re.search(r"INFO length: (\d+) frames", text).group(1)) * 1470)
            self.assertFalse((f.run / "demo/lock").exists(), "the lock is released")
            self.assertEqual((f.run / "demo/done").read_text(), "exit=0\n")
            self.assertFalse((f.run / "demo/running").exists())

    def test_the_tour_is_the_same_twice(self):
        with tempfile.TemporaryDirectory() as t:
            f = Fixture(t)
            sums = []
            for _ in range(2):
                self.assertEqual(f.go("--no-video", "--features", "first,second").returncode, 0)
                sums.append({re.sub(r"demo-\d{8}-\d{6}", "demo-X", p.name): p.read_bytes() for p in f.demo.iterdir()
                             if p.suffix in (".png", ".wav", ".pngs")})
                time.sleep(1.1)   # the stamp in the names moves on
            a, b = sums
            self.assertEqual(sorted(a), sorted(b))
            for k in a:
                self.assertEqual(a[k], b[k], f"{k} differs between two runs")

    def test_each_feature_runs_alone(self):
        with tempfile.TemporaryDirectory() as t:
            f = Fixture(t)
            for feat in ("first", "second"):
                out = f.go("--no-video", "--features", feat)
                self.assertEqual(out.returncode, 0, feat + out.stdout + out.stderr)
                self.assertIn(f"INFO shows: {feat}", f.log())
            self.assertEqual(f.go("--no-video", "--features", "nope").returncode, 2)

    def test_the_lock(self):
        with tempfile.TemporaryDirectory() as t:
            f = Fixture(t)
            (f.run / "demo").mkdir(parents=True)
            (f.run / "demo/lock").write_text(f"{os.getpid()}\n")          # a live holder: this test
            out = f.go("--no-video", "--features", "first")
            self.assertEqual(out.returncode, 75, out.stderr)
            self.assertIn("another Rec demo is recording", out.stderr)
            (f.run / "demo/lock").write_text("2147483646\n")              # a dead holder blocks nothing
            self.assertEqual(f.go("--no-video", "--features", "first").returncode, 0)

    def test_earlier_runs_go_but_their_videos_stay(self):
        with tempfile.TemporaryDirectory() as t:
            f = Fixture(t)
            f.demo.mkdir(parents=True)
            (f.demo / "demo-20200101-000000.mp4").write_bytes(b"an earlier video")
            (f.demo / "07-old-shot.png").write_bytes(b"old")
            (f.demo / "rec-demo.txt").write_text("old log")
            (f.demo / "demo-20200101-000000.wav").write_bytes(b"old")
            self.assertEqual(f.go("--no-video", "--features", "first").returncode, 0)
            self.assertEqual((f.demo / "demo-20200101-000000.mp4").read_bytes(), b"an earlier video")
            self.assertFalse((f.demo / "07-old-shot.png").exists())
            self.assertFalse((f.demo / "demo-20200101-000000.wav").exists())
            self.assertNotIn("old log", f.log())

    def test_a_missing_ffmpeg_is_a_fail_with_the_frames_kept(self):
        with tempfile.TemporaryDirectory() as t:
            f = Fixture(t)
            out = f.go("--features", "first")            # R3I6NOS_FFMPEG points at nothing
            self.assertEqual(out.returncode, 1)
            self.assertIn("FAIL video: ffmpeg is not installed here, so no mp4 was made", f.log())
            self.assertTrue(any(p.name.endswith("-frames.pngs") for p in f.demo.iterdir()))
            self.assertEqual((f.run / "demo/done").read_text(), "exit=1\n")

    @unittest.skipUnless(shutil.which("ffmpeg"), "no ffmpeg here")
    def test_the_video(self):
        with tempfile.TemporaryDirectory() as t:
            f = Fixture(t)
            out = f.go("--features", "first,second", R3I6NOS_FFMPEG=shutil.which("ffmpeg"))
            self.assertEqual(out.returncode, 0, out.stdout + out.stderr + (f.log() if (f.demo / "rec-demo.txt").exists() else ""))
            mp4 = [p for p in f.demo.iterdir() if p.suffix == ".mp4"]
            self.assertEqual(len(mp4), 1)
            self.assertGreater(mp4[0].stat().st_size, 1000)
            self.assertIn("PASS video: ", f.log())
            self.assertFalse(any(p.name.endswith("-frames.pngs") or p.suffix == ".wav" for p in f.demo.iterdir()), "made into the video: the working files go")
            if shutil.which("ffprobe"):
                p = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "stream=codec_name,width,height", "-of", "csv=p=0", str(mp4[0])], capture_output=True, text=True)
                self.assertIn("h264,640,480", p.stdout)
                self.assertIn("aac", p.stdout)

    def test_a_cancel_closes_the_run(self):
        with tempfile.TemporaryDirectory() as t:
            f = Fixture(t)
            p = subprocess.Popen([sys.executable, str(f.script), "--quiet", "--no-video", "--features", "third"], env=f.env(), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            for _ in range(100):                                   # until the tool's `sleep 30` is running
                if (f.run / "demo/lock").exists() and subprocess.run(["pgrep", "-f", f"{f.tool} long"], capture_output=True).returncode == 0:
                    break
                time.sleep(0.1)
            p.send_signal(signal.SIGTERM)
            _, err = p.communicate(timeout=60)
            self.assertEqual(p.returncode, 1, err)
            text = f.log()
            self.assertRegex(text, r"FAIL cancelled: signal \d+: the files so far are kept and no video was made")
            self.assertEqual(text.splitlines()[-1], "end")
            self.assertFalse((f.run / "demo/lock").exists())

    def test_a_hotplug_failure_stops_the_tour(self):
        with tempfile.TemporaryDirectory() as t:
            f = Fixture(t, "#@hero a: A\n#@feature a\nhotplug\nok\nok\n")
            out = f.go("--no-video")
            self.assertEqual(out.returncode, 1)
            text = f.log()
            self.assertIn("FAIL hotplug failure: USB device failed", text)
            self.assertIn("FAIL cancelled: a hot-plug failure", text)
            self.assertEqual(text.count("INFO 1 a fake hotplug: "), 1, "the first step ran")
            self.assertNotIn("INFO 2 a fake ok: ", text, "and the tour stopped before the second")
            self.assertEqual(text.splitlines()[-1], "end")

    def test_the_baked_script_stands_alone(self):
        with tempfile.TemporaryDirectory() as t:
            f = Fixture(t)
            text = f.script.read_text()
            self.assertRegex("\n".join(text.splitlines()[:40]), r'(?m)^APP\s*=\s*"Fake App"')   # menu/demos.lua reads the app's name here
            self.assertNotIn('FONT_HEX = "@', text)
            out = f.go("--no-video", "--features", "first", R3I6NOS_THEME_FONT="/nonexistent")   # no header to read: the font is in the script
            self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
            self.assertIn("INFO build: v9.9 abc1234 2026-11-12", f.log())
            os.remove(f.t / "fake-report")                              # no <command>-report beside it: the power section says so, the demo still runs
            self.assertEqual(f.go("--no-video", "--features", "first").returncode, 0)
            self.assertIn("SKIP power: the device report's readers", f.log())

if __name__ == "__main__":
    unittest.main()

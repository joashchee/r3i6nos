"""/etc/asound.conf (docs/SOUND.md): every softvol passes alsa-lib's own checks, and Boost's floor is 0 dB.

On 2026-11-10 every app on the unit was silent ("Couldn't open audio device"): Boost had min_dB 0.0, and alsa-lib refuses a softvol
whose min_dB is not negative, so the default device could not be opened at all. The rules below are alsa-lib 1.2.8's (Debian 12,
the image's), from _snd_pcm_softvol_open in src/pcm/pcm_softvol.c. Where aplay is installed (Linux) the chain is also opened for
real, with the null device in place of the codec: it must get past those checks to loading its control (no card 0 here)."""
import importlib.machinery
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONF = ROOT / "overlay/etc/asound.conf"
MAX_DB_UPPER_LIMIT = 90   # alsa-lib 1.2.8


def softvols():
    """{pcm name: {min_dB, max_dB, resolution, control}} for every `type softvol` block."""
    text = re.sub(r"#.*", "", CONF.read_text())
    out = {}
    for m in re.finditer(r"pcm\.(\w+)\s*\{(.*?)\n\}", text, re.S):
        body = m.group(2)
        if not re.search(r"type\s+softvol", body):
            continue
        num = lambda key: float(re.search(rf"\b{key}\s+(-?[\d.]+)", body).group(1))
        out[m.group(1)] = {"min_dB": num("min_dB"), "max_dB": num("max_dB"), "resolution": int(num("resolution")),
                           "control": re.search(r'name\s+"([^"]+)"', body).group(1)}
    return out


def load(path, name):
    spec = importlib.util.spec_from_loader(name, importlib.machinery.SourceFileLoader(name, str(path)))
    mod = importlib.util.module_from_spec(spec)
    sys.dont_write_bytecode = True
    spec.loader.exec_module(mod)
    return mod


def null_output(text, out_conf):
    """The conf with the null device in place of the codec, and its USB-output file at out_conf."""
    text = re.sub(r"pcm\.r3i6nos_out \{.*?\n\}", "pcm.r3i6nos_out {\n    type null\n}", text, flags=re.S)
    return text.replace('"/run/r3i6nos/asound-out.conf"', f'"{out_conf}"')


def play(dev, base, conf):
    p = subprocess.run(["aplay", "-q", "-D", dev, "-t", "raw", "-f", "S16_LE", "-c", "2", "-r", "44100"], input=bytes(4096),
                       capture_output=True, env=dict(os.environ, ALSA_CONFIG_PATH=f"{base}:{conf}"), timeout=20)
    return p.returncode, p.stderr.decode(errors="replace")


class AsoundConf(unittest.TestCase):
    def test_the_output_is_the_codec_unless_r3i6nos_sound_says(self):
        text = re.sub(r"#.*", "", CONF.read_text())
        self.assertRegex(text, r"pcm\.r3i6nos_out \{\s*type hw\s*card 0\s*device 0\s*\}")
        self.assertIn('slave.pcm "r3i6nos_out"', text)
        hook = re.search(r"@hooks \[(.*?)\n\]", text, re.S)
        self.assertTrue(hook, "the hook that loads the USB output")
        self.assertIn("errors false", hook.group(1), "a missing file must not stop sound")
        snd = load(ROOT / "overlay/usr/local/bin/r3i6nos-sound", "r3i6nos_sound_conf2")
        self.assertIn(f'"{snd.OUT_CONF}"', hook.group(1), "the file r3i6nos-sound writes")
        self.assertGreater(text.index("@hooks"), text.index("pcm.r3i6nos_out"), "loaded after the default, so it wins")

    @unittest.skipUnless(shutil.which("aplay"), "aplay (alsa-utils) not installed")
    def test_alsa_lib_takes_the_usb_output_file(self):
        base = "/usr/share/alsa/alsa.conf"
        if not os.path.exists(base):
            self.skipTest("no /usr/share/alsa/alsa.conf")
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "asound-out.conf"
            conf = Path(d) / "asound.conf"
            # the codec as card 0 (absent here: it fails to open) and the "USB" output as null (opens): which one plays tells
            conf.write_text(re.sub(r"pcm\.r3i6nos_out \{.*?\n\}", "pcm.r3i6nos_out {\n    type hw\n    card 7\n    device 0\n}",
                                   CONF.read_text(), flags=re.S).replace('"/run/r3i6nos/asound-out.conf"', f'"{out}"'))
            rc, err = play("r3i6nos_out", base, conf)
            self.assertNotEqual(rc, 0, "card 7 should not exist here")
            out.write_text("pcm.!r3i6nos_out {\n    type null\n}\n")
            rc, err = play("r3i6nos_out", base, conf)
            self.assertEqual(rc, 0, err)

    def test_the_two_volumes_are_there(self):
        sv = softvols()
        self.assertEqual(sorted(v["control"] for v in sv.values()), ["Boost", "Volume"])

    def test_every_softvol_passes_alsa_libs_checks(self):
        for name, v in softvols().items():
            self.assertLess(v["min_dB"], 0, f"{name}: alsa-lib refuses min_dB >= 0, and then nothing can open the default device")
            self.assertTrue(v["min_dB"] < v["max_dB"] <= MAX_DB_UPPER_LIMIT, name)
            self.assertTrue(1 < v["resolution"] <= 1024, name)

    def test_volume_tops_out_at_0_db(self):
        v = next(x for x in softvols().values() if x["control"] == "Volume")
        self.assertEqual(v["max_dB"], 0.0, "the volume keys never amplify; that is Boost's job")

    def test_boost_floor_is_0_db(self):
        # a softvol's step 0 is silence (pcm_softvol.c), so Boost is never set below step 1, which must be exactly 0 dB
        v = next(x for x in softvols().values() if x["control"] == "Boost")
        step = (v["max_dB"] - v["min_dB"]) / (v["resolution"] - 1)
        self.assertAlmostEqual(v["min_dB"] + step, 0.0, msg="Boost's step 1 must be 0 dB")
        snd = load(ROOT / "overlay/usr/local/bin/r3i6nos-sound", "r3i6nos_sound_conf")
        self.assertEqual(snd.BOOST_FLOOR, 1)
        default = int(dict(snd.DEFAULTS)["Boost"])
        self.assertAlmostEqual(v["min_dB"] + default * step, 6.0, msg="the first boot's Boost is +6 dB")
        self.assertIn("M.FLOOR = { Boost = 1 }", (ROOT / "menu/mixer.lua").read_text(), "the mixer stops at the same step")

    @unittest.skipUnless(shutil.which("aplay"), "aplay (alsa-utils) not installed: the static checks above still ran")
    def test_alsa_lib_opens_the_chain(self):
        with tempfile.TemporaryDirectory() as d:
            conf = Path(d) / "asound.conf"
            conf.write_text(null_output(CONF.read_text(), Path(d) / "absent.conf"))
            base = next((p for p in ("/usr/share/alsa/alsa.conf",) if os.path.exists(p)), None)
            if base is None:
                self.skipTest("no /usr/share/alsa/alsa.conf")
            for dev in ("r3i6nos_boost", "r3i6nos_volume", "default"):
                rc, err = play(dev, base, conf)
                self.assertNotIn("_snd_pcm_softvol_open", err, f"{dev}: alsa-lib refused the softvol's settings:\n{err}")
                self.assertTrue(rc == 0 or "softvol_load_control" in err or "CTL" in err, f"{dev}: {err}")


if __name__ == "__main__":
    unittest.main()

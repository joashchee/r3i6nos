#!/usr/bin/env python3
"""The boot disk guide's boot.ini patch (in the ansiapps-site repo, r3i6nos/public/bootdisk/boot-patch.js) must do what this repo's
scripts do: same root pattern as scripts/inject-boot.sh, same ROOT_UUID as scripts/image-layout.sh, same rewrite.

The web app lives in ../ansiapps-site (override with R3I6NOS_SITE_REPO); without that checkout the tests are skipped and say so.
"""
import os
import re
import shutil
import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SITE_REPO = Path(os.environ.get("R3I6NOS_SITE_REPO", REPO.parent / "ansiapps-site"))
JS = SITE_REPO / "r3i6nos/public/bootdisk/boot-patch.js"


@unittest.skipUnless(JS.is_file(), f"NOT RUN: {JS} not found (the web app lives in the ansiapps-site repo)")
class BootdiskParity(unittest.TestCase):
    def test_root_uuid_matches_image_layout(self):
        uuid = re.search(r"^ROOT_UUID=(\S+)", (REPO / "scripts/image-layout.sh").read_text(), re.M).group(1)
        self.assertIn(f"ROOT_UUID = '{uuid}'", JS.read_text())

    def test_pattern_matches_inject_boot(self):
        sh = (REPO / "scripts/inject-boot.sh").read_text()
        py = re.search(r'pattern = re\.compile\(r"""(.*?)"""\)', sh, re.S).group(1)
        js_src = re.search(r"PATTERN_SOURCE = '(.*)';", JS.read_text()).group(1).replace("\\'", "'").replace("\\\\", "\\")
        self.assertEqual(js_src, py)

    @unittest.skipUnless(shutil.which("node"), "NOT RUN: node is not installed")
    def test_browser_patch_equals_inject_boot_rewrite(self):
        uuid = re.search(r"^ROOT_UUID=(\S+)", (REPO / "scripts/image-layout.sh").read_text(), re.M).group(1)
        pattern = re.compile(r"""root=(?:UUID=)?(['"]?)[^\s'"]+\1""")
        samples = ["setenv bootargs \"root=UUID='aaaa-bbbb' rootwait rw\"\n", 'x root="/dev/mmcblk0p2" y\n',
                   "root=LABEL=ROOTFS rw\n", "root=UUID=abc rw\n"]
        # the file is a browser script that also works as CommonJS; loaded by hand so a neighbouring "type": "module" cannot matter
        loader = ("const m={exports:{}};new Function('module',require('fs').readFileSync(process.argv[1],'utf8'))(m);"
                  "process.stdout.write(m.exports.patch(process.argv[2]).text)")
        for text in samples:
            want = pattern.sub(lambda m: f"root=UUID={m.group(1)}{uuid}{m.group(1)}", text)
            got = subprocess.run(["node", "-e", loader, str(JS), text], capture_output=True, text=True, check=True).stdout
            self.assertEqual(got, want, text)


if __name__ == "__main__":
    unittest.main()

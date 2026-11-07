import importlib.util
import os
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("cpi", ROOT / "scripts/check_py_imports.py")
cpi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpi)


def fake_rootfs(*modules, dynload=()):
    root = tempfile.mkdtemp()
    lib = Path(root) / "usr/lib/python3.11"
    (lib / "lib-dynload").mkdir(parents=True)
    for m in modules:
        (lib / f"{m}.py").write_text("")
    for m in dynload:
        (lib / "lib-dynload" / f"{m}.cpython-311-aarch64-linux-gnu.so").write_text("")
    return root


def script(text):
    f = tempfile.NamedTemporaryFile("w", suffix=".py", delete=False)
    f.write(text)
    f.close()
    return f.name


class Imports(unittest.TestCase):
    def test_missing_module_is_reported(self):
        s = script("import os\nimport shutil\nfrom pathlib import Path\n")
        root = fake_rootfs("os", "pathlib")          # no shutil, like python3-minimal
        self.assertEqual(cpi.missing(root, [s]), [(s, "shutil")])

    def test_present_modules_pass_including_dynload_and_builtins(self):
        s = script("import sys, os, shutil\nimport select\nfrom subprocess import run\n")
        root = fake_rootfs("os", "shutil", "subprocess", dynload=("select",))
        self.assertEqual(cpi.missing(root, [s]), [])

    def test_packages_and_relative_imports(self):
        s = script("import json.decoder\nfrom . import sibling\n")
        root = fake_rootfs()
        os.makedirs(os.path.join(root, "usr/lib/python3.11/json"))
        self.assertEqual(cpi.missing(root, [s]), [])

    def test_the_real_boot_script_only_uses_modules_checked_here(self):
        names = cpi.imported_modules(str(ROOT / "overlay/usr/local/bin/deb_watcher.py"))
        self.assertIn("shutil", names)               # the one that was missing on the device

    def test_rootfs_without_python_is_an_error(self):
        self.assertEqual(cpi.main(["x", tempfile.mkdtemp(), script("import os\n")]), 1)


if __name__ == "__main__":
    unittest.main()

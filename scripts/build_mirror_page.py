#!/usr/bin/env python3
"""Make overlay/usr/share/r3i6nos/mirror/page.html (the page r3i6nos-mirror serves) from scripts/mirror_page.template.html and the
VGA font in theme/vga8x16.lua, with the font embedded as base64 (4096 bytes), so the unit serves one file and needs no font.

Standard library only. Usage: scripts/build_mirror_page.py [--check]  (--check writes nothing and exits 1 if the committed page differs.)
The font is CC BY-SA 4.0 (theme/font/, NOTICE): the page carries the credit and the licence note in its own comment and footer.
"""
import base64
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEMPLATE = ROOT / "scripts" / "mirror_page.template.html"
FONT = ROOT / "theme" / "vga8x16.lua"
OUT = ROOT / "overlay" / "usr" / "share" / "r3i6nos" / "mirror" / "page.html"


def font_bytes():
    glyphs = re.findall(r'^\s*"([0-9a-f]{32})",\s*$', FONT.read_text().split("  unicode = ", 1)[0], re.M)
    if len(glyphs) != 256:
        raise SystemExit(f"expected 256 glyphs in {FONT}, found {len(glyphs)}")
    return bytes.fromhex("".join(glyphs))


def build():
    return TEMPLATE.read_text().replace("@FONT@", base64.b64encode(font_bytes()).decode())


def main(argv):
    page = build()
    if "--check" in argv:
        if not OUT.exists() or OUT.read_text() != page:
            print(f"{OUT.relative_to(ROOT)} is out of date: run scripts/build_mirror_page.py", file=sys.stderr)
            return 1
        return 0
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(page)
    print(f"wrote {OUT.relative_to(ROOT)} ({len(page)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

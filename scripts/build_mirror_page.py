#!/usr/bin/env python3
"""Make overlay/usr/share/r3i6nos/mirror/page.html (the page r3i6nos-mirror serves) from scripts/mirror_page.template.html and
ANSIapps Mono (theme/font/ansiapps-mono/ansiapps-mono.json; CP437 by the table in ansiapps_cp437.lua) with the glyphs, the code point map and the
code page 437 table embedded as base64, so the unit serves one file and needs no font.

Standard library only. Usage: scripts/build_mirror_page.py [--check]  (--check writes nothing and exits 1 if the committed page differs.)
The font is SIL OFL 1.1 (theme/font/ansiapps-mono/, NOTICE): the page carries the credit and the licence note in its own comment and footer.
"""
import base64
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEMPLATE = ROOT / "scripts" / "mirror_page.template.html"
FONT = ROOT / "theme" / "font" / "ansiapps-mono" / "ansiapps_cp437.lua"          # its `unicode` list says which code point each code page 437 byte is
MONO = ROOT / "theme" / "font" / "ansiapps-mono" / "ansiapps-mono.json"
OUT = ROOT / "overlay" / "usr" / "share" / "r3i6nos" / "mirror" / "page.html"


def font_parts():
    """(glyphs, cmap, cp437) as bytes: 40 bytes a glyph (20 rows of 16 bits), 5 bytes a code point (u24 + u16 glyph), 2 bytes a CP437 byte."""
    d = json.loads(MONO.read_text())
    if d["width"] != 10 or d["height"] != 20 or any(len(g) != 60 for g in d["glyphs"]):
        raise SystemExit(f"{MONO}: not the 10x20 table this page reads")
    glyphs = b"".join(int(g[i:i + 3], 16).to_bytes(2, "big") for g in d["glyphs"] for i in range(0, 60, 3))   # 20 rows of 3 hex digits
    cmap = b"".join(int(cp).to_bytes(3, "big") + int(g).to_bytes(2, "big") for cp, g in sorted(d["cmap"].items(), key=lambda kv: int(kv[0])))
    uni = [int(n) for n in re.search(r"unicode = \{(.*?)\}", FONT.read_text(), re.S).group(1).replace("\n", " ").split(",") if n.strip()]
    if len(uni) != 256:
        raise SystemExit(f"expected 256 code points in {FONT}, found {len(uni)}")
    cp437 = b"".join((cp if i else 0x20).to_bytes(2, "big") for i, cp in enumerate(uni))   # byte 0 is shown as a blank
    return glyphs, cmap, cp437


def build():
    glyphs, cmap, cp437 = font_parts()
    enc = lambda b: base64.b64encode(b).decode()  # noqa: E731
    return TEMPLATE.read_text().replace("@GLYPHS@", enc(glyphs)).replace("@CMAP@", enc(cmap)).replace("@CP437@", enc(cp437))


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

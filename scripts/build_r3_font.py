#!/usr/bin/env python3
"""Make R3i6nos Mono, the 10x20 font of r3i6nos and the ansiapps apps (docs/R3I6NOS-MONO-FONT.md).

Writes into theme/font/r3i6nos-mono/: the glyph table (r3i6nos-mono.json, also the input of the TrueType step), BDF, BMFont, C and Lua
tables, the name tables, CSS, the coverage report, the overview sheet and the specimen page. Pure standard library. The TrueType, WOFF and WOFF2
files are made from the JSON table by scripts/build-r3-font.sh (fontTools, in Debian under Docker).

  scripts/build_r3_font.py            write everything
  scripts/build_r3_font.py --check    write nothing; exit 1 if a committed file differs (scripts/check.sh runs this)
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

from r3font import core, emit, greek, icons, latin, music, pad, ui  # noqa: E402
from r3font.registry import Registry  # noqa: E402
from r3font.render import sheet  # noqa: E402

OUT = ROOT / "theme" / "font" / "r3i6nos-mono"


def build():
    R = Registry()
    core.build(R)
    miss = greek.build(R, latin.build(R))
    pad.build(R)
    ui.build(R)
    music.build(R)
    icons.build(R)
    return R, miss


def specimen_html(R):
    sections = {}
    for cp in R.all_cps():
        sections.setdefault(R.where.get(R.alias.get(cp, cp), "?"), []).append(cp)
    parts = ['<!doctype html><meta charset="utf-8"><title>R3i6nos Mono specimen</title><link rel="stylesheet" href="r3i6nos-mono.css">',
             "<style>body{background:#101018;color:#e8e8f0;margin:24px}h2{font:16px sans-serif;margin:24px 0 6px;color:#9aa}"
             ".g{display:inline-block;width:10px;height:20px;margin:0 1px 1px 0;outline:1px solid #2a2a40;font-size:20px}"
             ".box{font-size:20px;line-height:20px;white-space:pre}</style>", "<h1 style='font:20px sans-serif'>R3i6nos Mono</h1>",
             '<div class="r3mono box">╭──────────────────────╮\n│ 0O1lI| rn m {} [] () │\n╰──────────────────────╯\n'
             '╔═══════╦═══════╗  ░▒▓█  ▁▂▃▄▅▆▇█  ⠁⠃⠇⡇⣇⣧⣷⣿\n║ CP437 ║ r3i6  ║\n╚═══════╩═══════╝</div>']
    for name, cps in sorted(sections.items(), key=lambda kv: min(kv[1])):
        parts.append(f"<h2>{name} ({len(cps)})</h2><div class='r3mono'>")
        for cp in cps:
            if cp in (0x20, 0xA0) or cp < 0x20 or 0x7F <= cp < 0xA0:
                continue
            parts.append(f"<span class='g' title='U+{cp:04X}'>&#{cp};</span>")
        parts.append("</div>")
    return "\n".join(parts) + "\n"


def outputs(R, miss):
    store, cmap = emit.glyph_store(R)
    o = {"r3i6nos-mono.json": emit.json_table(R, store, cmap), "r3i6nos_mono10x20.h": emit.c_header(R, store, cmap),
         "r3i6nos_mono10x20.lua": emit.lua_table(R, store, cmap), "r3i6nos_mono_names.h": emit.names_h(R),
         "r3i6nos_mono_names.lua": emit.names_lua(R), "R3i6nosMono-Regular.bdf": emit.bdf(R), "r3i6nos-mono.css": emit.css(),
         "glyph-coverage-report.txt": emit.coverage(R, miss), "specimen.html": specimen_html(R),
         "R3i6nosMono-10x20.psf": emit.psf(R)}
    return o


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    check = "--check" in argv
    R, miss = build()
    o = outputs(R, miss)
    OUT.mkdir(parents=True, exist_ok=True)
    stale = []
    if check:
        for n, text in o.items():
            p = OUT / n
            if not p.exists() or (p.read_bytes() != text if isinstance(text, bytes) else p.read_text() != text):
                stale.append(n)
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            emit.bmfont(R, d / "a.fnt", d / "R3i6nosMono-Regular.png")
            for n, src in (("R3i6nosMono-Regular.png", d / "R3i6nosMono-Regular.png"), ("R3i6nosMono-Regular.fnt", d / "a.fnt")):
                p = OUT / n
                if not p.exists() or p.read_bytes() != src.read_bytes():
                    if n.endswith(".fnt") and p.exists() and p.read_text() == src.read_text():
                        continue
                    stale.append(n)
        if R.new:
            stale.append("glyph-names.json (new names: " + ", ".join(R.new[:5]) + ")")
        if stale:
            print("build_r3_font: out of date: " + ", ".join(stale) + " (run scripts/build_r3_font.py)")
            return 1
        print(f"build_r3_font: up to date ({len(R.all_cps())} code points, {len(R.names)} names)")
        return 0
    for n, text in o.items():
        (OUT / n).write_bytes(text) if isinstance(text, bytes) else (OUT / n).write_text(text)
    emit.bmfont(R, OUT / "R3i6nosMono-Regular.fnt", OUT / "R3i6nosMono-Regular.png")
    R.save_lock()
    cps = R.all_cps()
    sheet(str(OUT / "overview.png"), [R.glyph(c) for c in cps], cols=64, scale=2)
    print(f"build_r3_font: {len(cps)} code points ({len(R.g)} drawn, {len(R.alias)} aliases), {len(R.names)} named, {len(R.new)} new names; "
          f"missing Latin: {len(miss)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

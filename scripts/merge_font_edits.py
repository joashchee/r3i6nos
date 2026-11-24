#!/usr/bin/env python3
"""Merge the Font Editor's file into theme/font/r3i6nos-mono/edits.txt (docs/FONT-EDITOR.md).

  scripts/merge_font_edits.py FILE [--dry-run]

FILE is what the editor wrote on the unit (FontEdit/edits-r3i6nos-mono.txt on a card). The cells in it win over the ones already in edits.txt, a cell that
equals what the generators draw is dropped, and the result is written (or printed with --dry-run). Then run scripts/build-r3-font.sh.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

from r3font import edits  # noqa: E402


def built_without_edits():
    import build_r3_font
    return build_r3_font.build(with_edits=False)[0]


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    if len(args) != 1:
        print(__doc__)
        return 2
    new = Path(args[0]).read_text()
    old = edits.EDITS.read_text() if edits.EDITS.exists() else ""
    try:
        text = edits.merge(built_without_edits(), new, old)
    except edits.EditError as e:
        print(f"merge_font_edits: {e}", file=sys.stderr)
        return 1
    n = text.count("\ncell ") + (1 if text.startswith("cell ") else 0)
    if "--dry-run" in argv:
        sys.stdout.write(text)
    else:
        edits.EDITS.write_text(text)
    print(f"merge_font_edits: {n} edited cell(s) in edits.txt" + (" (dry run)" if "--dry-run" in argv else ""), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

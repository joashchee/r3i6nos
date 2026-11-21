#!/usr/bin/env python3
"""scripts/roadmap_effort.py: every open ROADMAP.md item has an effort rating (CLAUDE.md, "ROADMAP.md").

    roadmap_effort.py            check: exit 1 and list the open items with no [S], [M], [L] or [XL] right after the checkbox
    roadmap_effort.py --list S   the open items of that rating (or several: --list S M), in roadmap order, to pick the next phase by least or most effort
    roadmap_effort.py --count    how many open items of each rating

The scale is the hands-on work left once nothing blocks it (a unit run, a decision or another item that is still open is said beside the item, not rated):
S  an hour or less: one small change, one doc, one session on the unit and reading the report
M  a few hours to a day: one feature or fix with its tests
L  several days: a subsystem part, several files and apps, or several rounds on the unit
XL a week or more: a new app or subsystem, a change across every app, or something that depends on outside work
"""
import re
import sys
from pathlib import Path

ROADMAP = Path(__file__).resolve().parent.parent / "ROADMAP.md"
OPEN = re.compile(r"^\s*- \[ \] (?:\[(S|M|L|XL)\] )?(.*)$")
ORDER = ["S", "M", "L", "XL"]


def items(text):
    for n, line in enumerate(text.splitlines(), 1):
        m = OPEN.match(line)
        if m:
            yield n, m.group(1), m.group(2)


def main(argv):
    found = list(items(ROADMAP.read_text()))
    if argv[:1] == ["--count"]:
        for r in ORDER:
            print(f"{r:2} {sum(1 for _, x, _ in found if x == r)}")
        return 0
    if argv[:1] == ["--list"]:
        want = argv[1:] or ORDER
        for n, r, text in found:
            if r in want:
                print(f"[{r}] ROADMAP.md:{n}: {re.sub(r'[*`]', '', text)[:110]}")
        return 0
    bad = [(n, t) for n, r, t in found if r is None]
    for n, t in bad:
        print(f"ROADMAP.md:{n}: open item with no effort rating ([S] [M] [L] or [XL] after the checkbox): {t[:70]}", file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

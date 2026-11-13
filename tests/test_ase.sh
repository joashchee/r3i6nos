#!/bin/bash
# test_ase.sh: Commander's Aseprite reader (apps/commander-native/ase.h) on files made in code by make_ase.py (no Aseprite-made file is
# in the repo): each good one must draw every frame, and each one cut at every length, with each byte set to 0x00/0xFF/0x7F/0x80 and with
# random damage must never crash or trip the address and undefined-behaviour sanitizers it is built with.
set -u
cd "$(dirname "$0")/.." || exit 1
if ! command -v python3 >/dev/null; then echo "NOT RUN: needs python3"; exit 0; fi
T="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-ase.XXXXXX")"
trap 'rm -rf "$T"' EXIT
cc -std=c11 -Wall -Wextra -Wno-unused-function -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -o "$T/ase" tests/test_ase.c -lm || exit 1
python3 -I - "$T" <<'P' || exit 1
import sys
sys.path.insert(0, "apps/commander-native")
import make_ase
for cid, fname, data, sp in make_ase.fixtures():
    open(sys.argv[1] + "/" + cid + ".ase", "wb").write(data)
    open(sys.argv[1] + "/" + cid + ".frames", "w").write(str(len(sp.frames)))
P
fail=0; n=0
for f in "$T"/ase_*.ase; do
    id="$(basename "$f" .ase)"
    frames="$(cat "$T/$id.frames")"
    n=$((n + 1))
    out="$("$T/ase" "$f" 2>&1)"
    if [ "$out" != "frames $frames drawn $frames" ]; then echo "FAIL: $id: expected 'frames $frames drawn $frames', got '$out'"; fail=1; fi
    n=$((n + 1))
    out="$("$T/ase" --fuzz "$f" 2>&1)"
    if ! grep -q '^tries [0-9]* opened [0-9]*$' <<<"$out"; then echo "FAIL: $id damaged and cut-short: $(head -5 <<<"$out")"; fail=1; fi
done
# a file that is not Aseprite at all
printf 'not a sprite' > "$T/junk"; n=$((n + 1))
out="$("$T/ase" "$T/junk" 2>&1)"; rc=$?
if [ $rc -ne 2 ]; then echo "FAIL: junk was read (rc $rc, '$out')"; fail=1; fi
if [ $fail -eq 0 ]; then echo "ok: $n Aseprite cases (every fixture drawn; cut and damaged files never crash)"; fi
exit $fail

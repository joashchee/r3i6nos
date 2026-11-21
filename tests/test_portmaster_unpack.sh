#!/bin/bash
# tests/test_portmaster_unpack.sh: scripts/portmaster-unpack.sh against FAKE port zips laid out as PortMaster's real ones were on 2026-11-15
# (Pyxel .pyxapp, loose main.py, a single script; Godot .pck, and a launcher that only names where one goes; Ren'Py with no game files;
# Solarus data.solarus; AGS ac2game.dat; a weston port and an unknown one skipped; a bad zip). Needs python3 and unzip. Not covered: a unit.
set -eu
cd "$(dirname "$0")/.."
T="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-pmunpack-test.XXXXXX")"; trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
mk() {  # <name> <script text> [path=content ...]: a zip like a port's: Name.sh and files
    local n="$1" s="$2"; shift 2
    python3 -I - "$T/$n.zip" "$s" "$@" <<'PY'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1], "w")
z.writestr("Port.sh", sys.argv[2])
for a in sys.argv[3:]:
    p, _, c = a.partition("=")
    z.writestr(p, c)
PY
}
mk pyapp 'runtime="pyxel_2.2.8_python_3.11"' 'pyapp/pyapp.pyxapp=x'
mk pyloose 'runtime="pyxel_2.2.8_python_3.11"' 'pyloose/gamedata/main.py=x' 'pyloose/gamedata/a.py=y'
mk pyone 'PYXEL_PKG="solo.py"
runtime="pyxel_2.2.8_python_3.11"' 'pyone/solo.py=print(1)'
mk gdok 'runtime="frt_3.5.2"' 'gdok/gdok.pck=x'
mk gdno 'runtime="frt_3.6"' 'gdno/gamedata/gdno.pck goes here=' 'gdno/patch/a.xdelta3=x'
mk rpno 'runtime="renpy_8.3.4"' 'rpno/gamefiles/.gitkeep=' 'rpno/patches/options.rpy=x'
mk rpok 'runtime="renpy_8.3.4"' 'rpok/game/script.rpy=label start:'
mk sol 'runtime="solarus-1.6.5"' 'sol/data.solarus=x'
mk agsok 'ags_3.6 ac2game' 'agsok/ac2game.dat=x'
mk wes 'runtime="weston_pkg_0.2"' 'wes/x.bin=x'
mk unk 'echo hello' 'unk/x=x'
printf 'not a zip' > "$T/bad.zip"

D="$T/out"
set +e
scripts/portmaster-unpack.sh --to "$D" "$T"/pyapp.zip "$T"/pyloose.zip "$T"/pyone.zip "$T"/gdok.zip "$T"/gdno.zip "$T"/rpno.zip "$T"/rpok.zip "$T"/sol.zip "$T"/agsok.zip "$T"/wes.zip "$T"/unk.zip "$T"/bad.zip > "$T/log" 2> "$T/err"
rc=$?
set -e
[ "$rc" = 1 ] || fail "a bad zip should end with status 1, got $rc"
grep -q 'bad: cannot read the zip' "$T/err" || fail "no message for the bad zip: $(cat "$T/err")"
[ -f "$D/Pyxel/pyapp.pyxapp" ] || fail "pyxapp not placed"
[ -f "$D/Pyxel/pyloose/main.py" ] || fail "loose Pyxel folder not placed (main.py)"
[ -f "$D/Pyxel/pyloose/a.py" ] || fail "loose Pyxel folder not placed (a.py)"
[ "$(cat "$D/Pyxel/pyone/main.py")" = "print(1)" ] || fail "single script not placed as main.py"
[ -f "$D/Godot/gdok/gdok.pck" ] || fail "pck not placed"
[ ! -e "$D/Godot/gdno" ] || fail "a placeholder was placed as a game"
grep -q 'NEEDS-DATA godot gdno' "$T/log" || fail "no NEEDS-DATA for a port with no pck"
grep -q 'NEEDS-DATA renpy rpno.*patches' "$T/log" || fail "no NEEDS-DATA (with the patch note) for Ren'Py with no game files"
[ ! -e "$D/Renpy/rpno" ] || fail "an empty Ren'Py folder was placed"
[ -f "$D/Renpy/rpok/game/script.rpy" ] || fail "Ren'Py game not placed"
[ -f "$D/Solarus/sol/data.solarus" ] || fail "Solarus quest not placed"
[ -f "$D/AGS/agsok/ac2game.dat" ] || fail "AGS game not placed"
grep -q 'SKIP wes' "$T/log" || fail "weston port not skipped"
grep -q 'SKIP unk' "$T/log" || fail "unknown port not skipped"
# the menu's own rules (menu/scan.lua) accept what was placed: Pyxel needs main.py or .pyxapp, Ren'Py game/, Solarus data.solarus, AGS ac2game.dat, Godot a .pck
# a dry run writes nothing
rm -rf "$T/dry"
scripts/portmaster-unpack.sh --dry-run --to "$T/dry" "$T"/pyapp.zip "$T"/gdok.zip > "$T/dlog"
[ ! -e "$T/dry" ] || fail "a dry run wrote files"
grep -q 'dry run' "$T/dlog" || fail "a dry run did not say so"
# no arguments, an unknown option
if scripts/portmaster-unpack.sh >/dev/null 2>&1; then fail "no zip was accepted"; fi
if scripts/portmaster-unpack.sh --nope x.zip >/dev/null 2>&1; then fail "an unknown option was accepted"; fi
echo "portmaster-unpack: ok"

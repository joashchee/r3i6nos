#!/bin/bash
# tests/test_runtimes.sh: scripts/build-runtimes.sh (the pinned hash is enforced, the trim, an aarch64 entry binary, no escaping symlink, the tool package layout,
# the notices) and the three launchers (tools/runtimes/*/run) against FAKE runtime trees; the real squashfs files are fetched only by the build.
# Not covered here: unsquashfs and the .deb pack (need squashfs-tools and dpkg-deb or Docker), and the engines running (tests/test_runtimes_docker.sh, then a unit).
set -eu
cd "$(dirname "$0")/.."
T="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-rt.XXXXXX")"; trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }

# a tiny aarch64 ELF header (e_machine 183) and an x86-64 one (62)
elf() { python3 -I -c 'import sys; sys.stdout.buffer.write(b"\x7fELF\x02\x01\x01" + bytes(9) + (2).to_bytes(2,"little") + int(sys.argv[1]).to_bytes(2,"little") + bytes(44))' "$1" > "$2"; }
mk_trees() {  # <dir> <machine>
    local d="$1" m="$2"
    mkdir -p "$d/pyxel/lib/python3.11/site-packages/pyxel" "$d/pyxel/lib/python3.11/site-packages/mypy" "$d/pyxel/lib/python3.11/site-packages/pyxel-2.2.8.dist-info" "$d/pyxel/bin"
    elf "$m" "$d/pyxel/lib/python3.11/site-packages/pyxel/pyxel_wrapper.abi3.so"; echo MIT > "$d/pyxel/LICENSE.pyxel"; echo PSF > "$d/pyxel/LICENSE.python"
    : > "$d/pyxel/lib/python3.11/site-packages/mypy/x.py"; : > "$d/pyxel/bin/maturin"
    mkdir -p "$d/renpy/lib/py3-linux-aarch64" "$d/renpy/lib/python3.9" "$d/renpy/lib/python3.12" "$d/renpy/renpy"
    elf "$m" "$d/renpy/lib/py3-linux-aarch64/python"; : > "$d/renpy/lib/python3.9/old.py"; echo MIT > "$d/renpy/renpy/LICENSE.txt"; printf '#!/bin/bash\n' > "$d/renpy/startRENPY"
    mkdir -p "$d/solarus"; elf "$m" "$d/solarus/solarus-1.6.5"; : > "$d/solarus/libsolarus.so.1"
    mkdir -p "$d/ags" "$d/godot"; elf "$m" "$d/ags/ags_3.6.aarch64"; elf "$m" "$d/godot/frt_3.6"
}
mk_trees "$T/trees" 183
export R3I6NOS_RUNTIMES_TREES="$T/trees" R3I6NOS_RUNTIMES_WORK="$T/work" R3I6NOS_RUNTIMES_OUT="$T/out"
scripts/build-runtimes.sh --stage-only >/dev/null || fail "stage failed"
S="$T/work/stage"
# pyxel: only the wheel, moved to site-packages
P="$S/pyxel/usr/lib/r3i6nos-runtimes/pyxel"
[ -f "$P/site-packages/pyxel/pyxel_wrapper.abi3.so" ] || fail "pyxel wheel lost"
[ -d "$P/site-packages/pyxel-2.2.8.dist-info" ] || fail "pyxel dist-info lost"
[ ! -e "$P/site-packages/mypy" ] || fail "pyxel not trimmed to the wheel"
[ ! -e "$P/bin" ] || fail "pyxel not trimmed to the wheel"
[ ! -e "$P/lib" ] || fail "pyxel not trimmed to the wheel"
[ -s "$P/LICENSE.pyxel" ] || fail "pyxel licence lost"
# renpy: python3.9 dropped, the rest kept
R="$S/renpy/usr/lib/r3i6nos-runtimes/renpy"
[ ! -e "$R/lib/python3.9" ] || fail "renpy lib/python3.9 not dropped"
[ -d "$R/lib/python3.12" ] || fail "renpy trimmed too much"
[ -f "$R/renpy/LICENSE.txt" ] || fail "renpy trimmed too much"
# the package layout (a tool package: deb_watcher.py wraps usr/bin/*)
for n in pyxel renpy solarus ags godot; do
    pkg="r3i6nos-runtime-$n"
    [ -x "$S/$n/usr/bin/r3i6nos-run-$n" ] || fail "$n: no command"
    [ -f "$S/$n/usr/share/r3i6nos/tool" ] || fail "$n: no tool marker"
    for f in NOTICE copyright SOURCE; do [ -s "$S/$n/usr/share/doc/$pkg/$f" ] || fail "$n: no $f"; done
    grep -q "^Package: $pkg\$" "$S/$n/DEBIAN/control" || fail "$n: control"
    grep -q "^Architecture: arm64" "$S/$n/DEBIAN/control" || fail "$n: control arch"
    bash -n "$S/$n/usr/bin/r3i6nos-run-$n" || fail "$n: launcher syntax"
done
grep -q "GPL-3.0-or-later" "$S/solarus/usr/share/doc/r3i6nos-runtime-solarus/NOTICE" || fail "solarus NOTICE does not name the licence"
# a non-aarch64 entry binary is refused
mk_trees "$T/bad" 62
if R3I6NOS_RUNTIMES_WORK="$T/w2" R3I6NOS_RUNTIMES_TREES="$T/bad" scripts/build-runtimes.sh --stage-only solarus >/dev/null 2>"$T/e1"; then fail "an x86-64 binary was accepted"; fi
grep -q "not an aarch64 ELF" "$T/e1" || fail "no aarch64 message: $(cat "$T/e1")"
# a symlink that leaves the tree is refused
mk_trees "$T/ln" 183; ln -s /etc/passwd "$T/ln/solarus/escape"
if R3I6NOS_RUNTIMES_WORK="$T/w3" R3I6NOS_RUNTIMES_TREES="$T/ln" scripts/build-runtimes.sh --stage-only solarus >/dev/null 2>"$T/e2"; then fail "an escaping symlink was accepted"; fi
grep -q "leaves the runtime" "$T/e2" || fail "no symlink message: $(cat "$T/e2")"
# an upstream layout change (a KEEP path gone) is an error, not a silent empty package
mk_trees "$T/gone" 183; rm -rf "$T/gone/pyxel/lib/python3.11/site-packages/pyxel"
if R3I6NOS_RUNTIMES_WORK="$T/w4" R3I6NOS_RUNTIMES_TREES="$T/gone" scripts/build-runtimes.sh --stage-only pyxel >/dev/null 2>&1; then fail "a missing wheel was accepted"; fi
# a wrong hash is refused (a junk file under the pinned name; fetching is switched off)
mkdir -p "$T/src"; echo junk > "$T/src/solarus-1.6.5.aarch64.squashfs"
if env -u R3I6NOS_RUNTIMES_TREES R3I6NOS_RUNTIMES_WORK="$T/w5" R3I6NOS_RUNTIMES_NO_FETCH=1 R3I6NOS_RUNTIMES_SRC_DIR="$T/src" scripts/build-runtimes.sh --stage-only solarus >/dev/null 2>"$T/e3"; then fail "a wrong sha256 was accepted"; fi
grep -q "pinned sha256" "$T/e3" || fail "no hash message: $(cat "$T/e3")"

# the launchers: argument errors, and what they run (with fakes)
for n in pyxel renpy solarus ags godot; do
    if "$S/$n/usr/bin/r3i6nos-run-$n" >/dev/null 2>&1; then fail "$n: ran with no argument"; fi
done
G="$T/g"; mkdir -p "$G/pyg"; printf 'import sys\nprint("main", sys.argv[1:])\nimport os; print(os.getcwd().endswith("pyg"))\n' > "$G/pyg/main.py"
out="$("$S/pyxel/usr/bin/r3i6nos-run-pyxel" "$G/pyg" a b)" || fail "pyxel launcher failed"
echo "$out" | grep -q "main \['a', 'b'\]" || fail "pyxel: arguments lost: $out"
echo "$out" | grep -q "^True" || fail "pyxel: did not cd into the game: $out"
printf 'import os; print("PP", os.environ["PYTHONPATH"].split(":")[0].endswith("r3i6nos-runtimes/pyxel/site-packages"))\n' > "$G/one.py"
"$S/pyxel/usr/bin/r3i6nos-run-pyxel" "$G/one.py" | grep -q "PP True" || fail "pyxel: PYTHONPATH is not the runtime's wheel"
mkdir -p "$G/nogame"; if "$S/renpy/usr/bin/r3i6nos-run-renpy" "$G/nogame" >/dev/null 2>"$T/e4"; then fail "renpy ran a folder with no game/"; fi
grep -q "no game/" "$T/e4" || fail "renpy: no message"
mkdir -p "$G/rp/game"; printf '#!/bin/bash\necho "RP $*"\n' > "$S/renpy/usr/lib/r3i6nos-runtimes/renpy/startRENPY"; chmod +x "$S/renpy/usr/lib/r3i6nos-runtimes/renpy/startRENPY"
"$S/renpy/usr/bin/r3i6nos-run-renpy" "$G/rp" compile | grep -q "RP .*rp compile" || fail "renpy: base folder and arguments not passed"
# shellcheck disable=SC2016  # the fake runs later and expands these then
printf '#!/bin/bash\necho "SOL $* LD=$LD_LIBRARY_PATH"\n' > "$S/solarus/usr/lib/r3i6nos-runtimes/solarus/solarus-1.6.5"; chmod +x "$S/solarus/usr/lib/r3i6nos-runtimes/solarus/solarus-1.6.5"
mkdir -p "$G/quest"; sol="$("$S/solarus/usr/bin/r3i6nos-run-solarus" "$G/quest" -fullscreen=yes)"
echo "$sol" | grep -q "SOL -fullscreen=yes $G/quest LD=.*r3i6nos-runtimes/solarus" || fail "solarus: options before the quest, or the library path, wrong: $sol"
# AGS: finds the game file in a folder, goes there, asks for the software renderer unless told otherwise
# shellcheck disable=SC2016  # the fake runs later and expands these then
printf '#!/bin/bash\necho "AGS $* IN=$(basename "$PWD")"\n' > "$S/ags/usr/lib/r3i6nos-runtimes/ags/ags_3.6.aarch64"; chmod +x "$S/ags/usr/lib/r3i6nos-runtimes/ags/ags_3.6.aarch64"
mkdir -p "$G/adv" "$G/emptyadv"; : > "$G/adv/ac2game.dat"
a="$("$S/ags/usr/bin/r3i6nos-run-ags" "$G/adv")"
echo "$a" | grep -q "^AGS --gfxdriver software $G/adv/ac2game.dat IN=adv" || fail "ags: folder, renderer or directory wrong: $a"
a="$("$S/ags/usr/bin/r3i6nos-run-ags" "$G/adv" --gfxdriver ogl)"
echo "$a" | grep -q "^AGS --gfxdriver ogl $G/adv/ac2game.dat" || fail "ags: the game or a chosen renderer is wrong: $a"
if echo "$a" | grep -q "software"; then fail "ags: a chosen renderer was overridden: $a"; fi
if "$S/ags/usr/bin/r3i6nos-run-ags" "$G/emptyadv" >/dev/null 2>"$T/e5"; then fail "ags ran a folder with no game"; fi
grep -q "no ac2game.dat" "$T/e5" || fail "ags: no message"
# Godot: a .pck or a folder that holds one, GLES2 unless told otherwise, in the game's folder
# shellcheck disable=SC2016  # the fake runs later and expands these then
printf '#!/bin/bash\necho "GD $* IN=$(basename "$PWD")"\n' > "$S/godot/usr/lib/r3i6nos-runtimes/godot/frt_3.6"; chmod +x "$S/godot/usr/lib/r3i6nos-runtimes/godot/frt_3.6"
mkdir -p "$G/gd" "$G/emptygd"; : > "$G/gd/game.pck"
a="$("$S/godot/usr/bin/r3i6nos-run-godot" "$G/gd")"
echo "$a" | grep -q "^GD --video-driver GLES2 --main-pack $G/gd/game.pck IN=gd" || fail "godot: folder, renderer or directory wrong: $a"
a="$("$S/godot/usr/bin/r3i6nos-run-godot" "$G/gd/game.pck" --video-driver GLES3)"
echo "$a" | grep -q "^GD --video-driver GLES3 --main-pack $G/gd/game.pck" || fail "godot: a .pck was not run or the driver is wrong: $a"
if echo "$a" | grep -q "GLES2"; then fail "godot: a chosen driver was overridden: $a"; fi
if "$S/godot/usr/bin/r3i6nos-run-godot" "$G/emptygd" >/dev/null 2>"$T/e6"; then fail "godot ran a folder with no .pck"; fi
grep -q "no .pck" "$T/e6" || fail "godot: no message"
grep -q "Artistic-2.0" "$S/ags/usr/share/doc/r3i6nos-runtime-ags/NOTICE" || fail "ags NOTICE does not name the licence"
echo "runtimes OK"

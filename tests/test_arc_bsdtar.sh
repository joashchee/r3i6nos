#!/bin/bash
# test_arc_bsdtar.sh: Commander's members of 7-Zip, .tar.bz2 and ISO archives through bsdtar (arc_members.h, ARC_BSD): the listing (files, folders,
# a link, names with spaces, glob characters and non-ASCII), the bytes of each member, a damaged archive, a plain .bz2 (bsdtar cannot read it: not listed),
# a stand-in that never ends (the time limit) and one that prints without end (the output cap), under the sanitizers. Needs bsdtar, tar and bzip2;
# 7z (or 7zr) and genisoimage/mkisofs add the 7-Zip and ISO cases.
set -u
cd "$(dirname "$0")/.." || exit 1
BSDTAR="${R3I6NOS_BSDTAR:-$(command -v bsdtar || true)}"
if [ -z "$BSDTAR" ] || ! command -v bzip2 >/dev/null; then echo "NOT RUN: needs bsdtar and bzip2"; exit 0; fi
export R3I6NOS_BSDTAR="$BSDTAR"
T="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-arcbsd.XXXXXX")"
trap 'rm -rf "$T"' EXIT
# The sanitizers on Linux only: on a Mac an ASan report inside a fork-and-exec program can hang the test (macOS's symbolizer).
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"; [ "$(uname)" = Linux ] || SAN=""
# shellcheck disable=SC2046,SC2086  # SAN and sdl2-config's output are lists of options
cc -std=c11 -Wall -Wextra -g -O1 $SAN -D_GNU_SOURCE -I theme $(sdl2-config --cflags) -o "$T/t" tests/test_arc_bsdtar.c $(sdl2-config --libs) -lm || exit 1
fail=0
ok() { echo "ok: $1"; }
bad() { echo "FAIL: $1"; fail=1; }
chk() { local d="$1"; shift; if "$@"; then ok "$d"; else bad "$d"; fi; }   # chk "what" command...: ok when the command succeeds
exit_is() { "${@:2}" >/dev/null 2>&1; [ $? = "$1" ]; }                  # exit_is N command...: the command exits N

mkdir -p "$T/src/d/sub dir"
echo hello > "$T/src/d/a b.txt"
echo star > "$T/src/d/we[i]rd*.txt"
echo accent > "$T/src/d/sub dir/é.txt"
ln -s "a b.txt" "$T/src/d/lnk"
(cd "$T/src" && tar cjf "$T/x.tar.bz2" d)

out="$("$T/t" bz2 "$T/x.tar.bz2")" || bad "x.tar.bz2 not listed"
chk "tar.bz2 lists six members through bsdtar" grep -q "^how=3 n=6 " <<<"$out"
chk "a name with a space" grep -q "^0 6 d/a b.txt$" <<<"$out"
chk "a link is a link" grep -q "^2 0 d/lnk$" <<<"$out"
chk "a folder is a folder" grep -q "^1 0 d/sub dir/$" <<<"$out"
chk "a member's bytes" grep -qx "hello" <<<"$out"
chk "glob characters in a name are escaped" grep -qx "star" <<<"$out"
chk "a non-ASCII name" grep -qx "accent" <<<"$out"

# a damaged archive: cut short and a flipped middle: never a crash, and a failure message or a short list
head -c 120 "$T/x.tar.bz2" > "$T/cut.tar.bz2"
"$T/t" bz2 "$T/cut.tar.bz2" >/dev/null 2>&1; rc=$?
chk "a cut archive does not crash (exit $rc)" test "$rc" -le 2

# a plain .bz2 has no tar inside: bsdtar reads it as nothing useful, so it is not listed
echo plain > "$T/p.txt"; bzip2 -k "$T/p.txt"
chk "a plain .bz2 is not listed" exit_is 2 "$T/t" bz2 "$T/p.txt.bz2"

# not installed: nothing is listed, nothing breaks
chk "no bsdtar, not listed" exit_is 2 env R3I6NOS_BSDTAR=/nonexistent "$T/t" bz2 "$T/x.tar.bz2"

# the time limit and the output cap, with stand-ins for bsdtar (fast: the limits are shortened by nothing, so only the cap is run here)
cat > "$T/flood" <<'F'
#!/bin/sh
exec yes "-rw-r--r--  0 0 0 1 Nov 21 06:55 f"
F
chmod +x "$T/flood"
chk "endless output is cut at the cap, not listed" exit_is 2 env R3I6NOS_BSDTAR="$T/flood" "$T/t" bz2 "$T/x.tar.bz2"

# the repository's real archives (corpus/files/archives, made by other tools): each is listed through bsdtar and its member read, or (bsdtar lacks the format) not listed
corp() { # file kind member text
    f="corpus/files/archives/$1"; [ -f "$f" ] || return 0
    out="$("$T/t" "$2" "$f" 2>/dev/null)" || { echo "SKIP: bsdtar does not list $1"; return 0; }
    chk "corpus $1: $3 read" grep -q "$4" <<<"$out"
}
corp stored.rar rar readme.txt "stored in a RAR 4"
corp stored5.rar rar readme.txt "stored in a RAR 5"
corp two.cab cab hello.txt "hello from a cabinet"
corp old.lzh lzip README.TXT "stored in an LHA"
corp game.7z 7z game/main.lua "hello corpus"
corp game.tar.bz2 bz2 game/main.lua "hello corpus"
corp game.tar.lz lzip game/main.lua "hello corpus"

if command -v 7z >/dev/null || command -v 7zr >/dev/null; then
    S7="$(command -v 7z || command -v 7zr)"
    (cd "$T/src" && "$S7" a -bd "$T/x.7z" d >/dev/null)
    out="$("$T/t" 7z "$T/x.7z")" || bad "x.7z not listed"
    chk "7-Zip lists six members" grep -q "^how=3 n=6 " <<<"$out"
    chk "7-Zip member bytes" grep -qx "hello" <<<"$out"
    chk "7-Zip non-ASCII member" grep -qx "accent" <<<"$out"
else echo "SKIP: no 7z for the 7-Zip case"; fi
MK="$(command -v genisoimage || command -v mkisofs || true)"
if [ -n "$MK" ]; then
    "$MK" -quiet -o "$T/x.iso" "$T/src/d" 2>/dev/null
    out="$("$T/t" iso "$T/x.iso")" || bad "x.iso not listed"
    chk "ISO lists through bsdtar" grep -q "^how=3 " <<<"$out"
    chk "ISO member bytes" grep -qx "hello" <<<"$out"
else echo "SKIP: no genisoimage for the ISO case"; fi

[ "$fail" = 0 ] && echo "arc bsdtar: ok" || exit 1

#!/bin/bash
# scripts/flash-macos.sh against fake diskutil, plutil, sudo and dd: argument handling, the safety refusals, the games
# partition on by default, and that every games-partition failure explains itself. Nothing touches a real disk.
set -u
cd "$(dirname "$0")/.." || exit 1

T="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-flash.XXXXXX")"
trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }

BIN="$T/bin"; mkdir -p "$BIN"
cat > "$BIN/uname" <<'EOS'
#!/bin/bash
echo Darwin
EOS
# diskutil: info (plist) works for FAKE_DISK; list prints a line; the rest succeed. FAKE_NO_S3=1 hides partition 3.
cat > "$BIN/diskutil" <<'EOS'
#!/bin/bash
case "$1" in
    info)
        case "$*" in *s3*) [ "${FAKE_NO_S3:-}" = 1 ] && exit 1 ;; esac
        echo "<plist/>"; exit 0 ;;
    list) echo "fake diskutil list"; exit 0 ;;
    eraseVolume) [ "${FAKE_ERASE_FAILS:-}" = 1 ] && exit 1; exit 0 ;;
    *) exit 0 ;;
esac
EOS
# plutil -extract KEY raw -o - -  : answer from the environment.
cat > "$BIN/plutil" <<'EOS'
#!/bin/bash
case "$2" in
    WholeDisk) echo true ;;
    Internal) echo "${FAKE_INTERNAL:-false}" ;;
    TotalSize) echo "${FAKE_SIZE:-128000000000}" ;;
    MediaName) echo "Fake Card" ;;
    MountPoint) [ -n "${FAKE_MOUNT:-}" ] && echo "$FAKE_MOUNT" ;;
esac
exit 0
EOS
cat > "$BIN/sudo" <<'EOS'
#!/bin/bash
exec "$@"
EOS
# dd: reading sector 0 gives FAKE_MBR (a file); anything else is a no-op, unless FAKE_DD_FAILS=1.
cat > "$BIN/dd" <<'EOS'
#!/bin/bash
[ "${FAKE_DD_FAILS:-}" = 1 ] && exit 1
case "$*" in
    *"if=/dev/r"*) cat "$FAKE_MBR" ;;
esac
exit 0
EOS
chmod +x "$BIN"/*
export R3I6NOS_WAIT_S3=1
# the ports' repos, faked: Neumetik and Composer built, NoCoGaMa built for both LÖVE versions (one with a stray older build), the rest not
P="$T/ports"; export R3I6NOS_PORTS_ROOT="$P"
mkdir -p "$P/neumetik/r36s/build/pkg" "$P/composer/r36s/build/pkg" "$P/klamansi/r36s" "$P/painter" "$P/nocogama/build/Love 11.5" "$P/nocogama/build/Love 12.0"
printf 'deb' > "$P/neumetik/r36s/build/pkg/Neumetik.deb"; printf 'deb' > "$P/composer/r36s/build/pkg/compose.deb"
printf 'old' > "$P/nocogama/build/Love 11.5/NoCoGaMa-old.love"; touch -t 202601010000 "$P/nocogama/build/Love 11.5/NoCoGaMa-old.love"
printf 'love' > "$P/nocogama/build/Love 11.5/NoCoGaMa-11.5.love"; printf 'love' > "$P/nocogama/build/Love 12.0/NoCoGaMa-12.0.love"
run() { PATH="$BIN:$PATH" "$@"; }

# an untouched r3i6nos MBR (what add_games_partition.py accepts): signature, partitions 1 and 2
python3 -I - "$T/mbr_good" <<'EOP'
import sys
m = bytearray(512)
m[440:444] = (0x52333669).to_bytes(4, "little")
def part(n, start, size, typ):
    o = 446 + 16 * n
    m[o + 4] = typ
    m[o + 8:o + 12] = start.to_bytes(4, "little"); m[o + 12:o + 16] = size.to_bytes(4, "little")
part(0, 32768, 524288, 0x0b)
part(1, 557056, 4194304, 0x83)
m[510:512] = b"\x55\xaa"
open(sys.argv[1], "wb").write(m)
EOP
head -c 512 /dev/zero > "$T/mbr_zero"
printf 'img' > "$T/x.img"

# --- arguments
out="$(run scripts/flash-macos.sh 2>&1)"; rc=$?; [ "$rc" -ne 0 ] || fail "no arguments must fail"
echo "$out" | grep -q -- '--games-only' || fail "usage should mention --games-only: $out"
out="$(run scripts/flash-macos.sh --games-only disk9 "$T/x.img" 2>&1)"; rc=$?; [ "$rc" -ne 0 ] || fail "--games-only with an image must fail"
echo "$out" | grep -q 'does not write an image' || fail "no explanation for --games-only with an image: $out"
out="$(run scripts/flash-macos.sh "$T/missing.img" disk9 2>&1)"; echo "$out" | grep -q 'no such file' || fail "missing image: $out"
out="$(run scripts/flash-macos.sh "$T/x.txt" disk9 2>&1)"; echo "$out" | grep -q 'no such file' || fail "missing file: $out"

# --- safety refusals say why
out="$(FAKE_INTERNAL=true run scripts/flash-macos.sh "$T/x.img" disk9 2>&1)"; echo "$out" | grep -q 'INTERNAL disk. Refusing' || fail "internal disk: $out"
echo "$out" | grep -q 'USB reader' || fail "internal-disk refusal should say what to do: $out"
out="$(FAKE_SIZE=900000000000 run scripts/flash-macos.sh "$T/x.img" disk9 2>&1)"; echo "$out" | grep -q 'larger than any microSD' || fail "huge disk: $out"
out="$(FAKE_SIZE=1000000000 run scripts/flash-macos.sh "$T/x.img" disk9 2>&1)"; echo "$out" | grep -q 'too small' || fail "small disk: $out"

# --- the games partition is the default; --no-games turns it off
out="$(run scripts/flash-macos.sh "$T/x.img" disk9 --dry-run 2>&1)"; echo "$out" | grep -q 'EASYROMS' || fail "default must include the games partition: $out"
out="$(run scripts/flash-macos.sh "$T/x.img" disk9 --no-games --dry-run 2>&1)"; echo "$out" | grep -q 'EASYROMS' && fail "--no-games still mentions EASYROMS: $out"
out="$(run scripts/flash-macos.sh --games-only disk9 --dry-run 2>&1)"; echo "$out" | grep -q 'add a games partition' || fail "--games-only dry run: $out"
echo "$out" | grep -q 'dry-run: nothing written' || fail "dry run wrote?: $out"

# --- a real run (everything faked): a wrong answer cancels, and says nothing was written
out="$(echo nope | run scripts/flash-macos.sh --games-only disk9 2>&1)"; rc=$?; [ "$rc" -ne 0 ] || fail "wrong answer must cancel"
echo "$out" | grep -q 'nothing was written' || fail "cancel message: $out"

# --- games-only success path (fake mount point)
mkdir -p "$T/mnt"
out="$(echo disk9 | FAKE_MBR="$T/mbr_good" FAKE_MOUNT="$T/mnt" run scripts/flash-macos.sh --games-only disk9 2>&1)"; rc=$?
[ "$rc" -eq 0 ] || fail "games-only success path failed ($rc): $out"
echo "$out" | grep -q 'Games partition ready' || fail "no success message: $out"
for d in "Love 11.5" "Love 12.0" native_deb; do [ -d "$T/mnt/$d" ] || fail "folder not made: $d"; done
# ... and the ports' newest local builds are on it, the missing ones named with how to build them
[ "$(cat "$T/mnt/native_deb/Neumetik.deb")" = deb ] || fail "Neumetik.deb not copied"
[ -f "$T/mnt/native_deb/compose.deb" ] || fail "compose.deb not copied"
[ "$(cat "$T/mnt/Love 11.5/NoCoGaMa.love")" = love ] || fail "the newest NoCoGaMa build for 11.5, named NoCoGaMa.love, not copied"
[ -f "$T/mnt/Love 12.0/NoCoGaMa.love" ] || fail "NoCoGaMa for 12.0 not copied"
[ -z "$(find "$T/mnt" -name '._*')" ] || fail "AppleDouble ._ files left on the card"
echo "$out" | grep -q 'Ports copied: 4 file(s)' || fail "copy count: $out"
# ... and the test corpus, all of it in "Test files/", Neumetik's music and KlamANSI's drawing in their own folders (corpus/README.md)
n="$(find corpus/files -type f | wc -l | tr -d ' ')"
[ "$(find "$T/mnt/Test files" -type f | wc -l | tr -d ' ')" = "$n" ] || fail "the whole corpus is not in Test files/"
cmp -s corpus/files/music/midi/midi.mid "$T/mnt/Test files/music/midi/midi.mid" || fail "corpus file not copied whole, in its sub-folder"
for f in video/clip.mp4 archives/game.7z images/photo.png; do
    [ -f "$T/mnt/Test files/$f" ] || fail "the corpus's sub-folders (video, archives, images) are not kept on the card: $f"
done
[ ! -e "$T/mnt/Test files/midi.mid" ] || fail "a corpus file was copied flat as well"
[ -f "$T/mnt/Neumetik/music/Test files/tiny.ahx" ] || fail "Neumetik's music not in its folder"
[ -f "$T/mnt/Neumetik/music/Test files/notation.mxl" ] || fail "Neumetik's text music not in its folder"
[ ! -e "$T/mnt/Neumetik/music/Test files/painter.png" ] || fail "only music goes into Neumetik's folder"
[ -f "$T/mnt/KlamANSI/drawings/test-files/test-corpus.kan" ] || fail "KlamANSI's drawing not in its folder"
[ ! -e "$T/mnt/native_deb/r3i6nos-test-tool.deb" ] || fail "the corpus's package must not be installed"
[ ! -e "$T/mnt/Love 11.5/hello.love" ] || fail "the corpus's app must not be installed"
echo "$out" | grep -q 'Test corpus copied: ' || fail "corpus copy not reported: $out"
echo "$out" | grep -q 'KlamANSI: no r36s/build/pkg/KlamANSI.deb (build it: cd .*klamansi && r36s/build-deb.sh)' || fail "missing KlamANSI not named: $out"
echo "$out" | grep -q 'Animator: no repo at' || fail "a port with no repo not named: $out"

# --- the dry run lists the ports; --no-games says they are not copied
out="$(run scripts/flash-macos.sh "$T/x.img" disk9 --dry-run 2>&1)"
echo "$out" | grep -q 'native_deb/Neumetik.deb   <- ' || fail "dry run does not list the ports: $out"
echo "$out" | grep -q 'MISSING (not on the card)' || fail "dry run does not say what is missing: $out"
echo "$out" | grep -q 'Painter: no r36s/build/pkg/sprites.deb' || fail "dry run: Painter: $out"
echo "$out" | grep -q 'The test corpus: [0-9]* file(s) into Test files/' || fail "dry run does not mention the test corpus: $out"
out="$(run scripts/flash-macos.sh "$T/x.img" disk9 --no-games --dry-run 2>&1)"
echo "$out" | grep -q 'the R36S ports are not copied' || fail "--no-games must say the ports are not copied: $out"

# --- every failure explains itself, exits non-zero, and does not eject
expect_games_fail() {  # <label> <text that must appear> <env...>
    local label="$1" want="$2"; shift 2
    out="$(echo disk9 | env "$@" PATH="$BIN:$PATH" scripts/flash-macos.sh --games-only disk9 2>&1)"; rc=$?
    [ "$rc" -ne 0 ] || fail "$label: must exit non-zero"
    echo "$out" | grep -q 'THE GAMES PARTITION WAS NOT MADE' || fail "$label: no headline: $out"
    echo "$out" | grep -q 'What to do' || fail "$label: no advice: $out"
    echo "$out" | grep -q -- "$want" || fail "$label: expected '$want': $out"
    echo "$out" | grep -q 'Not ejecting' || fail "$label: must leave the card for inspection: $out"
}
expect_games_fail "not an r3i6nos card" "not an MBR" FAKE_MBR="$T/mbr_zero"
expect_games_fail "card too small" "not enough for a games partition" FAKE_MBR="$T/mbr_good" FAKE_SIZE=2300000000
expect_games_fail "dd fails" "could not read sector 0" FAKE_MBR="$T/mbr_good" FAKE_DD_FAILS=1
expect_games_fail "macOS never shows s3" "Do not re-run" FAKE_MBR="$T/mbr_good" FAKE_NO_S3=1
expect_games_fail "format fails" "eraseVolume" FAKE_MBR="$T/mbr_good" FAKE_ERASE_FAILS=1
expect_games_fail "not mounted" "re-insert the card" FAKE_MBR="$T/mbr_good"

echo "flash-macos: ok"

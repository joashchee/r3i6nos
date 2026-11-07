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

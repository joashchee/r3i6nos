#!/bin/bash
# inject-rootfs.sh <image> [assets-dir]
#
# Puts the kernel modules, firmware and the Mali GPU blob from your own card
# (capture-card.sh) into the ROOTFS of an assembled image, with debugfs, so it
# works on a Mac (brew install e2fsprogs) as well as Linux. See inject_rootfs.py
# for exactly what goes where.
#
# THE GPU BLOB IS THE VENDOR'S PROPRIETARY CODE. An image with it in is for your
# own cards only: never upload it, attach it to a release, or share it.
#
# How: copy the ROOTFS partition out to a temporary file, write into that with
# debugfs, check it (fsck, then read everything back and compare), and only then
# copy it back over the image. A failed run leaves the image untouched.
# Needs about the ROOTFS size in free temporary space. Run it once per fresh image.

set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/image-layout.sh
. "$REPO/scripts/image-layout.sh"
IMG="${1:?Usage: $0 <image> [assets-dir]}"
ASSETS="${2:-$REPO/local_hardware_assets}"

fail() { echo "inject-rootfs: $*" >&2; exit 1; }

[ -f "$IMG" ] || fail "no such image: $IMG"
[ -d "$ASSETS/rootfs" ] || fail "no $ASSETS/rootfs (run capture-card.sh)"
command -v python3 >/dev/null || fail "python3 not installed"

find_tool() {  # find_tool <name>: on PATH, or in a Homebrew e2fsprogs
    local t
    t="$(command -v "$1" || true)"
    for d in /opt/homebrew/opt/e2fsprogs/sbin /usr/local/opt/e2fsprogs/sbin /sbin /usr/sbin; do
        if [ -z "$t" ] && [ -x "$d/$1" ]; then t="$d/$1"; fi
    done
    [ -n "$t" ] || fail "$1 not found (macOS: brew install e2fsprogs)"
    echo "$t"
}
DEBUGFS="$(find_tool debugfs)"
E2FSCK="$(find_tool e2fsck)"

# Where ROOTFS is (partition 2 of the MBR).
read -r root_start root_size <<<"$(python3 -I - "$IMG" <<'PY'
import sys
with open(sys.argv[1], "rb") as f:
    mbr = f.read(512)
if mbr[510:512] != b"\x55\xaa":
    sys.exit("no MBR signature")
e = mbr[446 + 16: 446 + 32]
print(int.from_bytes(e[8:12], "little"), int.from_bytes(e[12:16], "little"))
PY
)" || fail "cannot read the image's partition table"
[ "$root_size" -gt 0 ] || fail "the image has no second partition"

TMP="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-rootfs.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

echo "[*] Planning..."
python3 -I "$REPO/scripts/inject_rootfs.py" plan "$ASSETS" "$TMP/commands" || fail "no plan (see above)"

PART="$TMP/root.ext4"
echo "[*] Copying ROOTFS out ($((root_size / 2048)) MiB)..."
dd if="$IMG" of="$PART" bs=512 skip="$root_start" count="$root_size" 2>/dev/null

# debugfs exits 0 even when a command fails, so "does it exist" means "did it print an inode".
exists() { "$DEBUGFS" -R "stat $1" "$PART" 2>/dev/null | grep -q '^Inode:'; }

ver="$(find "$ASSETS/rootfs/lib/modules" -mindepth 1 -maxdepth 1 -exec basename {} \; 2>/dev/null | head -1)"
if exists /opt/libmali || { [ -n "$ver" ] && exists "/usr/lib/modules/$ver"; }; then
    fail "this image already has /opt/libmali or the kernel modules: inject into a fresh image"
fi

echo "[*] Writing..."
"$DEBUGFS" -w -f "$TMP/commands" "$PART" >"$TMP/debugfs.log" 2>&1 || true
# A directory that already exists (/usr/lib/modules may) is fine; anything else is not.
if grep -v 'already exists' "$TMP/debugfs.log" | grep -qiE 'no space|ext2fs_[a-z_]+: |could not|error'; then
    grep -v 'already exists' "$TMP/debugfs.log" | grep -iE 'no space|ext2fs_[a-z_]+: |could not|error' | sort | uniq -c | sort -rn | head -5 >&2
    echo "inject-rootfs: debugfs reported the problems above (the image is untouched)" >&2
    exit 1
fi

echo "[*] Checking the filesystem..."
"$E2FSCK" -fn "$PART" >"$TMP/fsck.log" 2>&1 || { tail -5 "$TMP/fsck.log" >&2; fail "fsck failed (the image is untouched)"; }

echo "[*] Reading everything back..."
DUMP="$TMP/dump"
mkdir -p "$DUMP/usr/lib" "$DUMP/opt"
for p in /usr/lib/modules /usr/lib/firmware /opt/libmali; do
    if exists "$p"; then
        "$DEBUGFS" -R "rdump $p $DUMP$(dirname "$p")" "$PART" >/dev/null 2>&1
    fi
done
# Symlink targets are read from the filesystem itself: a Mac's dump folder is case-insensitive
# and cannot hold both libmali.so and libMali.so.
"$DEBUGFS" -f "$TMP/commands.links" "$PART" >"$TMP/links.log" 2>&1 || true
if ! python3 -I "$REPO/scripts/inject_rootfs.py" verify "$ASSETS" "$DUMP" "$TMP/links.log"; then
    echo "debugfs said (without the routine lines):" >&2
    grep -vE '^debugfs |^Allocated inode|already exists' "$TMP/debugfs.log" | head -10 >&2
    fail "read-back differs (the image is untouched)"
fi

echo "[*] Writing ROOTFS back into the image..."
dd if="$PART" of="$IMG" bs=512 seek="$root_start" conv=notrunc 2>/dev/null

echo "[*] Done: $IMG"
if [ -d "$ASSETS/rootfs/gpu" ]; then
    echo "    This image now contains the vendor's Mali blob: keep it to your own cards."
fi

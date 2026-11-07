#!/bin/bash
# inject-boot.sh <image> [assets-dir]
#
# Puts the board's own boot files into an assembled image. They come from the
# user's working R36S card (capture-card.sh) and are never in the repo.
# Works on macOS and Linux: dd, cmp, python3 and mtools (brew install mtools).
#
# assets-dir (default local_hardware_assets/):
#   bootloader.bin   raw bytes of the card from sector 64 up to the first
#                    partition (idbloader and U-Boot)
#   boot/            the files of the card's BOOT partition: kernel, DTBs,
#                    initrd, boot.ini
#
# The stock boot.ini names the stock root (root=UUID='e139ce78-...'). Every
# *.ini and *.conf under boot/ gets that root= rewritten to this image's
# ROOTFS UUID (scripts/image-layout.sh), in a temporary copy: the files in
# assets-dir are never modified.
#
# Set R3I6NOS_BOOT_DEBUG=1 to see the boot on screen and get a boot log file on BOOT (below).
#
# The sector-64 start matches the stock card (captured 2026-11-06). The
# image's MBR (sectors 0 to 63) is never touched.

set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/image-layout.sh
. "$REPO/scripts/image-layout.sh"
IMG="${1:?Usage: $0 <image> [assets-dir]}"
ASSETS="${2:-$REPO/local_hardware_assets}"
MAX_LOADER=$(( (BOOT_START - LOADER_SECTOR) * 512 ))

fail() { echo "inject-boot: $*" >&2; exit 1; }

[ -f "$IMG" ] || fail "no such image: $IMG"
[ -f "$ASSETS/bootloader.bin" ] || fail "missing $ASSETS/bootloader.bin (run capture-card.sh)"
[ -n "$(find "$ASSETS/boot" -type f 2>/dev/null | head -1)" ] || fail "missing or empty $ASSETS/boot/"
for tool in dd cmp mcopy python3; do
    command -v "$tool" >/dev/null || fail "$tool not installed (macOS: brew install mtools)"
done

size="$(wc -c < "$ASSETS/bootloader.bin" | tr -d ' ')"
[ "$size" -gt 0 ] || fail "bootloader.bin is empty"
[ "$size" -le "$MAX_LOADER" ] || fail "bootloader.bin is $size bytes; the area before BOOT holds $MAX_LOADER"
python3 -I -c 'import sys; sys.exit(0 if any(open(sys.argv[1], "rb").read()) else 1)' "$ASSETS/bootloader.bin" \
    || fail "bootloader.bin is all zeros"

# The image must be one of ours: MBR signature and BOOT where we expect it.
layout="$(python3 -I - "$IMG" <<'PY'
import sys
with open(sys.argv[1], "rb") as f:
    mbr = f.read(512)
if mbr[510:512] != b"\x55\xaa":
    sys.exit("no MBR signature")
sig = "0x%08x" % int.from_bytes(mbr[440:444], "little")
e = mbr[446:462]
print(sig, int.from_bytes(e[8:12], "little"))
PY
)" || fail "cannot read the image's partition table"
read -r sig first_start <<<"$layout"
[ "$sig" = "$DISK_SIGNATURE" ] || fail "image has disk signature $sig, expected $DISK_SIGNATURE (not an r3i6nos image)"
[ "$first_start" -eq "$BOOT_START" ] || fail "BOOT starts at sector $first_start, expected $BOOT_START"

# --- boot files, with root= pointed at this image ---
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-boot.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
cp -R "$ASSETS/boot/." "$STAGE/"
# Finder litter is not a boot file (and a dotfile the copy glob below would skip).
find "$STAGE" \( -name .DS_Store -o -name '._*' \) -exec rm -rf {} +

echo "[*] Pointing root= at UUID $ROOT_UUID..."
python3 -I - "$STAGE" "$ROOT_UUID" <<'PY'
import os, re, sys
root, uuid = sys.argv[1], sys.argv[2]
# root=UUID='...' | root=UUID=... | root=/dev/xxx | root=LABEL=... ; keeps the quote style of UUID forms.
pattern = re.compile(r"""root=(?:UUID=)?(['"]?)[^\s'"]+\1""")
total = 0
for d, _, names in os.walk(root):
    for n in names:
        if not n.lower().endswith((".ini", ".conf")):
            continue
        path = os.path.join(d, n)
        with open(path, encoding="utf-8", errors="surrogateescape", newline="") as f:
            text = f.read()
        new, count = pattern.subn(lambda m: f"root=UUID={m.group(1)}{uuid}{m.group(1)}", text)
        if count:
            with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
                f.write(new)
            total += count
print(f"    rewrote {total} root= entries")
if total == 0:
    sys.exit("no root= found in any .ini or .conf: the image would not find its root")
PY

# R3I6NOS_BOOT_DEBUG=1: show the console on the screen (the stock line sends it to the serial port with
# `quiet splash`) and ask the image to write r3i6nos-bootlog.txt onto BOOT after booting.
if [ "${R3I6NOS_BOOT_DEBUG:-}" = 1 ]; then
    echo "[*] Debug boot: console on screen, bootlog on BOOT..."
    python3 -I - "$STAGE" <<'PY'
import os, sys
old = " console=/dev/ttyFIQ0 quiet splash plymouth.ignore-serial-consoles"
new = " console=tty1 loglevel=3 systemd.show_status=1"
total = 0
for d, _, names in os.walk(sys.argv[1]):
    for n in names:
        if n.lower().endswith((".ini", ".conf")):
            p = os.path.join(d, n)
            text = open(p, encoding="utf-8", errors="surrogateescape", newline="").read()
            if old in text:
                total += text.count(old)
                open(p, "w", encoding="utf-8", errors="surrogateescape", newline="").write(text.replace(old, new))
print(f"    changed {total} console settings")
if total == 0:
    sys.exit("the boot.ini has no stock console= line to change")
PY
    : > "$STAGE/r3i6nos-bootlog"
fi

# Nothing is written to the image until the boot files are staged and patched.
echo "[*] Bootloader: $size bytes at sector $LOADER_SECTOR..."
dd if="$ASSETS/bootloader.bin" of="$IMG" bs=512 seek="$LOADER_SECTOR" conv=notrunc 2>/dev/null
dd if="$IMG" bs=512 skip="$LOADER_SECTOR" count=$(( (size + 511) / 512 )) 2>/dev/null \
    | head -c "$size" | cmp - "$ASSETS/bootloader.bin" || fail "bootloader readback differs"

echo "[*] Boot files..."
FAT="$IMG@@$((BOOT_START * 512))"
while IFS= read -r -d '' top; do
    mcopy -i "$FAT" -s -p -Q -n -D o "$top" ::
done < <(find "$STAGE" -mindepth 1 -maxdepth 1 -print0)
# Read every file back out of the image and compare.
count=0
while IFS= read -r -d '' f; do
    rel="${f#"$STAGE"/}"
    mcopy -n -i "$FAT" "::/$rel" - | cmp - "$f" || fail "readback differs: $rel"
    count=$((count + 1))
done < <(find "$STAGE" -type f -print0)
echo "[*] $count boot files verified."

echo "[*] Done: $IMG"

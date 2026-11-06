#!/bin/bash
# inject-boot.sh <image> [assets-dir]
#
# Puts the board's own boot files into an assembled image. They come from the
# user's working R36S card (capture-card.sh) and are never in the repo.
#
# assets-dir (default local_hardware_assets/):
#   bootloader.bin   raw bytes of the card from sector 64 up to the first
#                    partition (idbloader and U-Boot), read-only copy of the card
#   boot/            the files of the card's BOOT partition: kernel, DTBs, boot config
#
# UNVERIFIED on hardware: the sector-64 start is the usual Rockchip layout
# (idbloader at 64, U-Boot at 16384). Confirm it from the card in phase 1.
# The image's MBR (sectors 0 to 63) is never touched.
#
# Needs only dd, cmp, sfdisk and mtools (no root, no mounts).

set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"
IMG="${1:?Usage: $0 <image> [assets-dir]}"
ASSETS="${2:-$REPO/local_hardware_assets}"

LOADER_SECTOR=64
BOOT_START=32768
MAX_LOADER=$(( (BOOT_START - LOADER_SECTOR) * 512 ))

fail() { echo "inject-boot: $*" >&2; exit 1; }

[ -f "$IMG" ] || fail "no such image: $IMG"
[ -f "$ASSETS/bootloader.bin" ] || fail "missing $ASSETS/bootloader.bin (see the header)"
[ -n "$(find "$ASSETS/boot" -type f 2>/dev/null | head -1)" ] || fail "missing or empty $ASSETS/boot/"
for tool in dd cmp sfdisk mcopy python3; do
    command -v "$tool" >/dev/null || fail "$tool not installed"
done

size="$(wc -c < "$ASSETS/bootloader.bin" | tr -d ' ')"
[ "$size" -gt 0 ] || fail "bootloader.bin is empty"
[ "$size" -le "$MAX_LOADER" ] || fail "bootloader.bin is $size bytes; the area before BOOT holds $MAX_LOADER"
[ -n "$(tr -d '\0' < "$ASSETS/bootloader.bin" | head -c 1)" ] || fail "bootloader.bin is all zeros"

boot_start="$(sfdisk --json "$IMG" | python3 -c '
import json, sys
print(json.load(sys.stdin)["partitiontable"]["partitions"][0]["start"])')"
[ "$boot_start" -eq "$BOOT_START" ] || fail "BOOT starts at sector $boot_start, expected $BOOT_START"

echo "[*] Bootloader: $size bytes at sector $LOADER_SECTOR..."
dd if="$ASSETS/bootloader.bin" of="$IMG" bs=512 seek="$LOADER_SECTOR" conv=notrunc status=none
dd if="$IMG" bs=512 skip="$LOADER_SECTOR" count=$(( (size + 511) / 512 )) status=none \
    | head -c "$size" | cmp - "$ASSETS/bootloader.bin" || fail "bootloader readback differs"

echo "[*] Boot files..."
FAT="$IMG@@$((BOOT_START * 512))"
mcopy -i "$FAT" -s -p -Q -n "$ASSETS"/boot/* ::
# Read every file back out of the image and compare.
count=0
while IFS= read -r -d '' f; do
    rel="${f#"$ASSETS"/boot/}"
    mcopy -n -i "$FAT" "::/$rel" - | cmp - "$f" || fail "readback differs: $rel"
    count=$((count + 1))
done < <(find "$ASSETS/boot" -type f -print0)
echo "[*] $count boot files verified."

echo "[*] Done: $IMG"

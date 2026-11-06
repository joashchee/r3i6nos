#!/bin/bash
# assemble_image.sh [rootfs-dir] [image-path]
#
# Builds the SD card image from a rootfs (build_rootfs.sh's output). Run as
# root on Linux (needs the files' real owners); no loop devices or mounts:
# the filesystems are written straight into the image file at their offsets.
#
#   sudo apt-get install dosfstools mtools e2fsprogs fdisk
#   sudo scripts/assemble_image.sh
#
# Layout (MBR, sectors of 512 bytes):
#   0 .. 32767         MBR and the raw bootloader area. Left EMPTY here:
#                      inject-boot.sh writes the board's own bootloader into it
#   p1 BOOT    FAT32   from 16 MiB, 256 MiB; the kernel, DTB and boot config
#   p2 ROOTFS  ext4    the rest
# The disk signature (0x52333669) and the ext4 UUID are fixed (scripts/image-layout.sh),
# so the board's boot.ini can say root=UUID='52333669-0000-4000-8000-000000000002',
# the same form the stock card uses; inject-boot.sh rewrites it to match.
#
# NOT here: anything from the board's own card. The image alone does not boot.

set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"
ROOTFS="${1:-$REPO/build/rootfs}"
IMG="${2:-$REPO/build/r36s-loveos.img}"

# shellcheck source=scripts/image-layout.sh
. "$REPO/scripts/image-layout.sh"
BOOT_MIB="${R3I6NOS_BOOT_MIB:-256}"
BOOT_SECT=$((BOOT_MIB * 2048))
ROOT_START=$((BOOT_START + BOOT_SECT))

if [ "$(uname -s)" != Linux ]; then
    echo "assemble_image: Linux only (build it in CI, flash it from the Mac)" >&2
    exit 1
fi
if [ "$(id -u)" -ne 0 ]; then
    echo "assemble_image: run as root, so file owners in the image are right" >&2
    exit 1
fi
for tool in sfdisk mke2fs mkfs.fat mcopy truncate du; do
    command -v "$tool" >/dev/null || { echo "assemble_image: $tool not installed" >&2; exit 1; }
done

[ -d "$ROOTFS" ] || { echo "assemble_image: no rootfs at $ROOTFS" >&2; exit 1; }
[ -e "$ROOTFS/etc/systemd/system/loveos.service" ] \
    || { echo "assemble_image: $ROOTFS is not an r3i6nos rootfs (no loveos.service)" >&2; exit 1; }
# A rootfs still holding a mounted /proc or /sys would be copied with them.
for dir in proc sys; do
    if [ -n "$(find "$ROOTFS/$dir" -mindepth 1 -maxdepth 1 2>/dev/null | head -1)" ]; then
        echo "assemble_image: $ROOTFS/$dir is not empty: is the chroot still mounted?" >&2
        exit 1
    fi
done

# ext4 size: what the tree uses, plus 30% and 512 MiB, at least 2 GiB.
used_mib="$(du -sx --block-size=1M "$ROOTFS" | cut -f1)"
ROOT_MIB="${R3I6NOS_ROOT_MIB:-$(( used_mib * 13 / 10 + 512 ))}"
if [ -z "${R3I6NOS_ROOT_MIB:-}" ] && [ "$ROOT_MIB" -lt 2048 ]; then ROOT_MIB=2048; fi
ROOT_SECT=$((ROOT_MIB * 2048))
TOTAL_BYTES=$(( (ROOT_START + ROOT_SECT) * 512 ))

mkdir -p "$(dirname "$IMG")"
rm -f "$IMG"
cleanup() {
    status=$?
    if [ "$status" -ne 0 ]; then rm -f "$IMG"; fi   # never leave a half-built image
    exit "$status"
}
trap cleanup EXIT

echo "[*] Image: $((TOTAL_BYTES / 1048576)) MiB (rootfs uses ${used_mib} MiB; ext4 ${ROOT_MIB} MiB)"
truncate -s "$TOTAL_BYTES" "$IMG"      # sparse: zeros take no space until written

echo "[*] Partition table..."
sfdisk --quiet "$IMG" <<EOF
label: dos
label-id: $DISK_SIGNATURE
unit: sectors

start=$BOOT_START, size=$BOOT_SECT, type=c
start=$ROOT_START, size=$ROOT_SECT, type=83
EOF

echo "[*] BOOT (FAT32)..."
# The block count is what keeps BOOT from growing over ROOTFS; mkfs.fat warns that the file is bigger. Expected.
mkfs.fat -F 32 -n BOOT --offset "$BOOT_START" "$IMG" "$((BOOT_SECT / 2))" >/dev/null \
    2> >(grep -v 'block count mismatch' >&2)

echo "[*] ROOTFS (ext4) from $ROOTFS..."
mke2fs -q -F -t ext4 -m 1 -L ROOTFS -U "$ROOT_UUID" -d "$ROOTFS" \
    -E "offset=$((ROOT_START * 512))" "$IMG" "$((ROOT_SECT / 2))k"

echo "[*] Image ready: $IMG"

#!/bin/bash
# check_image.sh <image>
#
# Checks an assembled image: partition table, both filesystems (fsck, read
# only), and that the files the device needs are in the root. Run as root on
# Linux (it attaches the partitions read-only through loop devices).

set -eu

IMG="${1:?Usage: $0 <image>}"
[ -f "$IMG" ] || { echo "check_image: no such file: $IMG" >&2; exit 1; }
[ "$(id -u)" -eq 0 ] || { echo "check_image: run as root" >&2; exit 1; }

LOOPS=()
MNT="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-img.XXXXXX")"
cleanup() {
    umount "$MNT" 2>/dev/null || true
    for l in "${LOOPS[@]}"; do losetup -d "$l" 2>/dev/null || true; done
    rmdir "$MNT" 2>/dev/null || true
}
trap cleanup EXIT

fail() { echo "check_image: FAIL: $*" >&2; exit 1; }

echo "[*] Partition table..."
table="$(sfdisk --json "$IMG" | python3 -c '
import json, sys
t = json.load(sys.stdin)["partitiontable"]
print(t["label"], t.get("id", ""))
for p in t["partitions"]:
    print(p["start"], p["size"], p["type"])
')"
head -1 <<<"$table" | grep -q '^dos 0x52333669$' || fail "not an MBR with signature 0x52333669: $(head -1 <<<"$table")"
[ "$(wc -l <<<"$table")" -eq 3 ] || fail "expected 2 partitions"
read -r b_start b_size b_type <<<"$(sed -n 2p <<<"$table")"
read -r r_start r_size r_type <<<"$(sed -n 3p <<<"$table")"
[ "$b_start" -eq 32768 ] || fail "BOOT should start at sector 32768, starts at $b_start"
[ "$b_type" = c ] || fail "BOOT type is $b_type, expected c (FAT32 LBA)"
[ "$r_type" = 83 ] || fail "ROOTFS type is $r_type, expected 83"
[ "$r_start" -ge $((b_start + b_size)) ] || fail "partitions overlap"

attach() {  # attach <start-sector> <sectors>
    losetup --find --show --read-only -o $(($1 * 512)) --sizelimit $(($2 * 512)) "$IMG"
}

echo "[*] BOOT..."
BOOT_DEV="$(attach "$b_start" "$b_size")"; LOOPS+=("$BOOT_DEV")
fsck.vfat -n "$BOOT_DEV" >/dev/null || fail "fsck.vfat"
[ "$(blkid -o value -s LABEL "$BOOT_DEV")" = BOOT ] || fail "BOOT label"

echo "[*] ROOTFS..."
ROOT_DEV="$(attach "$r_start" "$r_size")"; LOOPS+=("$ROOT_DEV")
e2fsck -fn "$ROOT_DEV" >/dev/null || fail "e2fsck"
[ "$(blkid -o value -s LABEL "$ROOT_DEV")" = ROOTFS ] || fail "ROOTFS label"

mount -o ro "$ROOT_DEV" "$MNT"
for f in etc/fstab etc/systemd/system/loveos.service usr/local/bin/loveos-session \
         usr/local/bin/run_app.sh usr/local/bin/deb_watcher.py opt/loveos/menu/main.lua; do
    [ -e "$MNT/$f" ] || fail "missing in rootfs: /$f"
done
for f in usr/local/bin/loveos-session usr/local/bin/run_app.sh; do
    [ -x "$MNT/$f" ] || fail "not executable: /$f"
done
[ -L "$MNT/etc/systemd/system/multi-user.target.wants/loveos.service" ] || fail "loveos.service not enabled"
grep -q '^loveos:x:1000:' "$MNT/etc/passwd" || fail "no loveos user (uid 1000)"
ls "$MNT"/opt/love/*/bin/love >/dev/null 2>&1 || fail "no LÖVE runtime in /opt/love"
[ -z "$(find "$MNT/proc" -mindepth 1 -maxdepth 1 | head -1)" ] || fail "/proc not empty in the image"
umount "$MNT"

echo "[*] Image OK: $IMG"

#!/bin/bash
# check_image.sh <image>
#
# Checks an assembled image: partition table, both filesystems (fsck, read
# only), and that the files the device needs are in the root. Run as root on
# Linux (it attaches the partitions read-only through loop devices).

set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/image-layout.sh
. "$REPO/scripts/image-layout.sh"
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
head -1 <<<"$table" | grep -q "^dos $DISK_SIGNATURE\$" || fail "not an MBR with signature $DISK_SIGNATURE: $(head -1 <<<"$table")"
[ "$(wc -l <<<"$table")" -eq 3 ] || fail "expected 2 partitions"
read -r b_start b_size b_type <<<"$(sed -n 2p <<<"$table")"
read -r r_start r_size r_type <<<"$(sed -n 3p <<<"$table")"
[ "$b_start" -eq "$BOOT_START" ] || fail "BOOT should start at sector $BOOT_START, starts at $b_start"
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
[ "$(blkid -o value -s UUID "$ROOT_DEV")" = "$ROOT_UUID" ] || fail "ROOTFS UUID is not $ROOT_UUID"

mount -o ro "$ROOT_DEV" "$MNT"
for f in etc/fstab etc/systemd/system/loveos.service usr/local/bin/loveos-session \
         usr/local/bin/run_app.sh usr/local/bin/deb_watcher.py usr/local/bin/r3i6nos-poweroff usr/local/bin/r3i6nos-about etc/r3i6nos-release usr/local/bin/r3i6nos-cards usr/local/bin/r3i6nos-format usr/local/bin/r3i6nos-runtimes etc/systemd/system/r3i6nos-runtimes.path etc/systemd/system/r3i6nos-runtimes.service usr/local/bin/r3i6nos-setup usr/local/bin/r3i6nos-report etc/systemd/system/r3i6nos-cards.service etc/systemd/system/r3i6nos-setup.service \
         opt/loveos/menu/main.lua opt/loveos/menu/settings.lua opt/loveos/menu/reports.lua opt/loveos/menu/view.lua \
         opt/loveos/theme/r3i6nos_ansiapps.lua opt/loveos/theme/font/ansiapps-mono/ansiapps_cp437.lua opt/loveos/theme/font/ansiapps-mono/OFL.txt; do
    [ -e "$MNT/$f" ] || fail "missing in rootfs: /$f"
done
for f in usr/local/bin/loveos-session usr/local/bin/run_app.sh usr/local/bin/r3i6nos-poweroff; do
    [ -x "$MNT/$f" ] || fail "not executable: /$f"
done
# nttplay is the only decoder of the ansiapps formats and is always in the image (CLAUDE.md rule 15); 0.2.0's first image went out without it.
if [ "${R3I6NOS_ALLOW_NO_NTTPLAY:-0}" != 1 ]; then
    [ -x "$MNT/usr/bin/nttplay" ] || fail "no /usr/bin/nttplay: Commander cannot play MIDI, tracker modules or music as text (R3I6NOS_ALLOW_NO_NTTPLAY=1 skips this for a test)"
    [ -s "$MNT/usr/share/nttplay/NOTICE" ] || fail "no /usr/share/nttplay/NOTICE: nttplay's third-party licences must travel with it"
    for f in usr/bin/nttplay-report usr/bin/nttplay-demo; do [ -x "$MNT/$f" ] || fail "no /$f (rules 9 and 11 for nttplay)"; done
    [ -s "$MNT/usr/share/nttplay/commands.txt" ] || fail "no /usr/share/nttplay/commands.txt: nttplay-report and nttplay-demo have nothing to run"
    [ -s "$MNT/usr/share/nttplay/NOTICE-font" ] || fail "no /usr/share/nttplay/NOTICE-font: the demo draws with ANSIapps Mono, its credit must travel"
fi
# PortMaster is in the image for testing (docs/PORTMASTER.md): its package, its licence notice, and the libraries its GUI loads.
if [ "${R3I6NOS_ALLOW_NO_PORTMASTER:-0}" != 1 ]; then
    [ -s "$MNT/opt/loveos/defaults/native_deb/PortMaster.deb" ] || fail "no PortMaster.deb in /opt/loveos/defaults/native_deb (R3I6NOS_ALLOW_NO_PORTMASTER=1 skips this for a test)"
    for lib in libSDL2_mixer-2.0.so.0 libSDL2_image-2.0.so.0 libSDL2_ttf-2.0.so.0 libSDL2_gfx-1.0.so.0; do
        [ -n "$(find "$MNT/usr/lib" -name "$lib" | head -1)" ] || fail "no $lib: PortMaster's GUI cannot start"
    done
    [ -x "$MNT/usr/bin/pgrep" ] || fail "no /usr/bin/pgrep (procps): PortMaster's ports need it"
fi
# The game-engine runtimes (docs/RUNTIMES.md) are packages, not part of the image, but they link what the image must carry: Solarus's PhysFS (the rest is SDL2, OpenAL, Vorbis, ModPlug, FreeType, which are here for LÖVE and PortMaster).
if [ "${R3I6NOS_ALLOW_NO_RUNTIME_LIBS:-0}" != 1 ]; then
    for lib in libphysfs.so.1 libopenal.so.1 libvorbisfile.so.3 libmodplug.so.1 libSDL2_image-2.0.so.0 libSDL2_ttf-2.0.so.0; do
        [ -n "$(find "$MNT/usr/lib" -name "$lib" | head -1)" ] || fail "no $lib: the Solarus, Ren'Py and Pyxel runtime packages cannot start (R3I6NOS_ALLOW_NO_RUNTIME_LIBS=1 skips this for a test)"
    done
fi
[ -L "$MNT/etc/systemd/system/multi-user.target.wants/loveos.service" ] || fail "loveos.service not enabled"
[ -L "$MNT/etc/systemd/system/multi-user.target.wants/r3i6nos-cards.service" ] || fail "r3i6nos-cards.service not enabled"
[ -L "$MNT/etc/systemd/system/multi-user.target.wants/r3i6nos-setup.service" ] || fail "r3i6nos-setup.service not enabled"
[ -L "$MNT/etc/systemd/system/multi-user.target.wants/r3i6nos-persist.service" ] || fail "r3i6nos-persist.service not enabled"
[ -x "$MNT/usr/local/bin/r3i6nos-persist" ] || fail "r3i6nos-persist not installed"
grep -q '^loveos:x:1000:' "$MNT/etc/passwd" || fail "no loveos user (uid 1000)"
ls "$MNT"/opt/love/*/bin/love >/dev/null 2>&1 || fail "no LÖVE runtime in /opt/love"
[ -z "$(find "$MNT/proc" -mindepth 1 -maxdepth 1 | head -1)" ] || fail "/proc not empty in the image"
umount "$MNT"

echo "[*] Image OK: $IMG"

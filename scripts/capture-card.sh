#!/bin/bash
# capture-card.sh <disk> [out-dir]
#
# Copies what r3i6nos needs from your own WORKING R36S card (the stock OS one,
# TF1) into local_hardware_assets/, in the layout inject-boot.sh reads:
#
#   bootloader.bin     raw bootloader sectors (from sector 64 to the first partition)
#   boot/              the files of the card's BOOT partition (kernel, DTBs, boot config)
#   rootfs/lib/modules the kernel modules (and rootfs/lib/firmware) from its root partition
#   rootfs/gpu/        the stock GPU userland (libmali*, from /usr/local/lib if the card has one there, else /usr/lib):
#                      the card's kernel is 4.x, so Mesa's panfrost cannot be used; a blob that does not match the
#                      kernel ("not of a compatible version") will not open the GPU
#   info/              partition table, stock fstab and os-release, a capture report, the raw first 16 MiB
#
# READ-ONLY on the card: it only reads the raw device, mounts BOOT read-only,
# and opens the ext4 partition with debugfs -c (catastrophic, read-only). Nothing
# ever writes to <disk>. Everything captured is the board vendor's: it stays in
# the gitignored local_hardware_assets/ and never goes in the repo or a release.
#
#   macOS:  scripts/capture-card.sh disk4          (diskutil list external physical)
#   Linux:  scripts/capture-card.sh sdb            (or mmcblk0)
#
# macOS cannot read ext4: install e2fsprogs for debugfs (brew install e2fsprogs).
# Reading the raw device needs sudo; files are handed back to you afterwards.
#
# UNTESTED on a real card. The partition parsing is unit-tested on synthetic cards.

set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"
DISK="${1:?Usage: $0 <disk> [out-dir]}"
OUT="${2:-$REPO/local_hardware_assets}"
DISK="${DISK#/dev/}"; DISK="${DISK#r}"
case "$DISK" in disk*|sd*|mmcblk*|nvme*) ;; *) echo "capture-card: odd disk name: $DISK" >&2; exit 2 ;; esac

fail() { echo "capture-card: $*" >&2; exit 1; }
warn() { echo "capture-card: WARNING: $*" >&2; INCOMPLETE=1; }
INCOMPLETE=0
SUDO=""; [ "$(id -u)" -eq 0 ] || SUDO=sudo
ME="$(id -un)"

# --- which device, and is it a card? ---
case "$(uname -s)" in
    Darwin)
        RAW="/dev/r$DISK"
        part_dev() { echo "/dev/r${DISK}s$1"; }
        info() { diskutil info -plist "/dev/$DISK" | plutil -extract "$1" raw -o - - 2>/dev/null; }
        diskutil info -plist "/dev/$DISK" >/dev/null 2>&1 || fail "diskutil cannot read /dev/$DISK"
        [ "$(info WholeDisk)" = true ] || fail "$DISK is not a whole disk"
        [ "$(info Internal)" = false ] || fail "$DISK is an internal disk; this wants the R36S card in a reader"
        echo "Card: $(info MediaName || echo unknown), $(( $(info TotalSize) / 1000000 )) MB on /dev/$DISK"
        ;;
    Linux)
        RAW="/dev/$DISK"
        part_dev() { case "$DISK" in *[0-9]) echo "/dev/${DISK}p$1" ;; *) echo "/dev/${DISK}$1" ;; esac; }
        [ -b "$RAW" ] || fail "$RAW is not a block device"
        if [ "$(cat "/sys/block/$DISK/removable" 2>/dev/null || echo 0)" != 1 ] && [ "${R3I6NOS_ALLOW_FIXED:-}" != 1 ]; then
            fail "$DISK is not removable media. If it really is the card in a reader, set R3I6NOS_ALLOW_FIXED=1"
        fi
        ;;
    *) fail "macOS or Linux only" ;;
esac

if [ -e "$OUT/bootloader.bin" ]; then
    fail "$OUT/bootloader.bin already exists: move $OUT aside first (a capture is never overwritten)"
fi
mkdir -p "$OUT/info" "$OUT/boot" "$OUT/rootfs/lib"

# --- 1. raw first 16 MiB, the partition table and the bootloader ---
echo "[*] Reading the first 16 MiB (bootloader area)..."
$SUDO dd if="$RAW" bs=1048576 count=16 2>/dev/null > "$OUT/info/first-16MiB.raw"
python3 -I "$REPO/scripts/capture_mbr.py" "$OUT/info/first-16MiB.raw" "$OUT/bootloader.bin" \
    | tee "$OUT/info/partitions.txt"
if grep -q '^allwinner=yes' "$OUT/info/partitions.txt"; then
    fail "this card carries an Allwinner boot0 (eGON.BT0): an Allwinner clone, NOT an RK3326 board. Stop here."
fi

# --- 2. BOOT partition (the first one) ---
echo "[*] Copying the BOOT partition..."
MOUNTED_BY_US=""
BOOTDIR=""
if [ "$(uname -s)" = Darwin ]; then
    mp() { diskutil info -plist "/dev/${DISK}s1" | plutil -extract MountPoint raw -o - - 2>/dev/null || true; }
    BOOTDIR="$(mp)"
    if [ -z "$BOOTDIR" ]; then
        diskutil mount readOnly "/dev/${DISK}s1" >/dev/null || fail "cannot mount /dev/${DISK}s1 read-only"
        MOUNTED_BY_US=1; BOOTDIR="$(mp)"
    fi
else
    BOOTDIR="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-boot.XXXXXX")"
    $SUDO mount -o ro "$(part_dev 1)" "$BOOTDIR" || fail "cannot mount $(part_dev 1) read-only"
    MOUNTED_BY_US=1
fi
[ -d "$BOOTDIR" ] || fail "BOOT partition is not mounted"
cleanup_boot() {
    [ -n "$MOUNTED_BY_US" ] || return 0
    if [ "$(uname -s)" = Darwin ]; then diskutil unmount "/dev/${DISK}s1" >/dev/null 2>&1 || true
    else $SUDO umount "$BOOTDIR" 2>/dev/null || true; rmdir "$BOOTDIR" 2>/dev/null || true; fi
}
trap cleanup_boot EXIT

rsync -rlt --exclude='.fseventsd' --exclude='.Spotlight-V100' --exclude='.Trashes' \
    --exclude='._*' --exclude='.DS_Store' --exclude='System Volume Information' \
    "$BOOTDIR/" "$OUT/boot/"
find "$OUT/boot" -type f | head -1 | grep -q . || fail "BOOT partition was empty"

# --- 3. root partition: kernel modules and firmware, via debugfs ---
echo "[*] Reading the root partition (modules, firmware)..."
DEBUGFS="$(command -v debugfs || true)"
for d in /opt/homebrew/opt/e2fsprogs/sbin /usr/local/opt/e2fsprogs/sbin /sbin /usr/sbin; do
    if [ -z "$DEBUGFS" ] && [ -x "$d/debugfs" ]; then DEBUGFS="$d/debugfs"; fi
done
if [ -z "$DEBUGFS" ]; then
    warn "debugfs not found (macOS: brew install e2fsprogs): kernel modules NOT captured"
else
    ROOTPART=""
    for n in 2 3 4; do
        # debugfs exits 0 even when the command fails: look for an inode in what it printed.
        if $SUDO "$DEBUGFS" -c -R "stat /etc/os-release" "$(part_dev "$n")" 2>/dev/null | grep -q '^Inode:'; then ROOTPART="$n"; break; fi
    done
    if [ -z "$ROOTPART" ]; then
        warn "no ext4 root partition with /etc/os-release found among partitions 2 to 4"
    else
        echo "    root partition: $(part_dev "$ROOTPART")"
        rdump() {  # rdump <path-on-card> <dest-dir>
            $SUDO "$DEBUGFS" -c -R "rdump $1 $2" "$(part_dev "$ROOTPART")" >/dev/null 2>&1
        }
        dump() {   # dump <path-on-card> <dest-file>
            $SUDO "$DEBUGFS" -c -R "dump $1 $2" "$(part_dev "$ROOTPART")" >/dev/null 2>&1
        }
        got_modules=""
        for src in /lib/modules /usr/lib/modules; do
            if rdump "$src" "$OUT/rootfs/lib" && [ -d "$OUT/rootfs/lib/modules" ]; then got_modules=1; break; fi
        done
        [ -n "$got_modules" ] || warn "no /lib/modules on the card's root partition"
        for src in /lib/firmware /usr/lib/firmware; do
            if rdump "$src" "$OUT/rootfs/lib" && [ -d "$OUT/rootfs/lib/firmware" ]; then break; fi
        done
        [ -d "$OUT/rootfs/lib/firmware" ] || echo "    (no /lib/firmware on the card: fine for many boards)"
        # The GPU userland the stock OS uses (Mali blob). The kernel is 4.x, so Mesa's panfrost cannot be used.
        # ArkOS keeps two blobs: an older wayland-gbm one in /usr/lib (its userspace API does not match the
        # card's own kernel) and the matching one in /usr/local/lib, which the stock ld.so.conf lists first.
        # Take the first directory that holds a real libmali file, /usr/local/lib first.
        LIBDIR=/usr/lib/aarch64-linux-gnu
        listing=""
        for cand in /usr/local/lib/aarch64-linux-gnu /usr/lib/aarch64-linux-gnu; do
            listing="$($SUDO "$DEBUGFS" -c -R "ls -p $cand" "$(part_dev "$ROOTPART")" 2>/dev/null || true)"
            if printf '%s\n' "$listing" | awk -F/ 'tolower($6) ~ /^libmali/ && $3 ~ /^100/ {found=1} END {exit !found}'; then
                LIBDIR="$cand"; break
            fi
        done
        mkdir -p "$OUT/rootfs/gpu"
        echo "# $LIBDIR" > "$OUT/info/stock-gpu-libs.txt"
        # ls -p lines: /inode/mode/uid/gid/name/size/ ; mode 100xxx is a file, 120xxx a symlink.
        printf '%s\n' "$listing" | awk -F/ 'tolower($6) ~ /mali|libgbm|libegl|libgles|libopencl/ {print $3 " " $6}' \
        | while read -r mode name; do
            case "$mode" in
                100*) dump "$LIBDIR/$name" "$OUT/rootfs/gpu/$name" || true; echo "file $name" >> "$OUT/info/stock-gpu-libs.txt" ;;
                120*) target="$($SUDO "$DEBUGFS" -c -R "stat $LIBDIR/$name" "$(part_dev "$ROOTPART")" 2>/dev/null \
                          | sed -n 's/.*Fast link dest: "\(.*\)"/\1/p')"
                      echo "link $name -> ${target:-?}" >> "$OUT/info/stock-gpu-libs.txt" ;;
            esac
        done
        dump /etc/fstab "$OUT/info/stock-fstab" || true
        dump /etc/os-release "$OUT/info/stock-os-release" || true
        # Anything debugfs wrote as root: hand it back.
        $SUDO chown -R "$ME" "$OUT" 2>/dev/null || true
    fi
fi
$SUDO chown -R "$ME" "$OUT" 2>/dev/null || true

# --- 4. the report ---
REPORT="$OUT/info/capture-report.txt"
{
    echo "r3i6nos card capture, $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "disk: $DISK"
    echo
    echo "== partitions"; cat "$OUT/info/partitions.txt"
    echo
    echo "== bootloader.bin"; wc -c < "$OUT/bootloader.bin" | tr -d ' '
    if command -v shasum >/dev/null; then shasum -a 256 "$OUT/bootloader.bin"; else sha256sum "$OUT/bootloader.bin"; fi
    echo
    echo "== BOOT files"; (cd "$OUT/boot" && find . -type f | sort)
    echo
    echo "== device trees (spec section 1: supported board vs clone)"
    (cd "$OUT/boot" && find . -name '*.dtb' | sort) || true
    echo
    echo "== root= and bootargs in the boot config (the image's root partition must match)"
    (cd "$OUT/boot" && grep -rHn -E 'root=|bootargs' --include='*.ini' --include='*.conf' --include='*.txt' --include='*.cfg' . 2>/dev/null) || echo "(none found)"
    echo
    echo "== kernel modules"; ls "$OUT/rootfs/lib/modules" 2>/dev/null || echo "(not captured)"
    echo
    echo "== stock fstab"; cat "$OUT/info/stock-fstab" 2>/dev/null || echo "(not captured)"
    echo
    echo "== GPU libraries on the stock rootfs (files copied to rootfs/gpu/)"; cat "$OUT/info/stock-gpu-libs.txt" 2>/dev/null || echo "(not captured)"
} > "$REPORT"

verdict="UNKNOWN: no board DTB recognised; read the report"
if find "$OUT/boot" -name 'rk3326-r35s-linux.dtb' -o -name 'rk3326-rg351mp-linux.dtb' | grep -q .; then
    verdict="supported (genuine RK3326 DTB found)"
fi
if find "$OUT/boot" -name 'rk3326-evb-lp3-v12-linux.dtb' -o -name 'rf3536k4ka.dtb' | grep -q .; then
    verdict="INCOMPATIBLE CLONE DTB found: do not use this card's board with the RK3326 image"
fi

echo
echo "Captured to: $OUT"
echo "Board:       $verdict"
echo "Report:      $REPORT"
echo "Next:        scripts/inject-boot.sh <image.img>   (and check root= in the report matches partition 2)"
[ "$INCOMPLETE" -eq 0 ] || { echo "INCOMPLETE: see the warnings above." >&2; exit 1; }

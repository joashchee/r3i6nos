#!/bin/bash
# flash-macos.sh <image.img | image.img.gz> [diskN] [--dry-run]
#
# Writes an r3i6nos image to a microSD card on macOS. Careful by design:
# dd on the wrong disk destroys it, so this refuses anything that is not an
# external, whole, card-sized disk, shows what it found, and makes you type
# the disk's name to go on.
#
#   scripts/flash-macos.sh build/r36s-loveos.img.gz           # lists external disks
#   scripts/flash-macos.sh build/r36s-loveos.img.gz disk4     # flashes disk4 after you confirm
#   scripts/flash-macos.sh build/r36s-loveos.img.gz disk4 --dry-run
#
# If <image>.sha256 sits beside the image it is checked first. The image alone
# does not boot: run inject-boot.sh on the .img before flashing (not on a .gz).
#
# UNTESTED on a real card. The refusal paths can be tried with --dry-run.

set -eu

IMAGE=""; DISK=""; DRY=0
for arg in "$@"; do
    case "$arg" in
        --dry-run) DRY=1 ;;
        disk[0-9]*|/dev/disk[0-9]*|/dev/rdisk[0-9]*) DISK="${arg#/dev/}"; DISK="${DISK#r}" ;;
        -*) echo "flash-macos: unknown option: $arg" >&2; exit 2 ;;
        *) IMAGE="$arg" ;;
    esac
done

fail() { echo "flash-macos: $*" >&2; exit 1; }

[ "$(uname -s)" = Darwin ] || fail "macOS only (this is $(uname -s))"
[ -n "$IMAGE" ] || fail "usage: $0 <image.img|image.img.gz> [diskN] [--dry-run]"
[ -f "$IMAGE" ] || fail "no such file: $IMAGE"

case "$IMAGE" in
    *.img)    READ=(cat "$IMAGE") ;;
    *.img.gz) READ=(gunzip -c "$IMAGE") ;;
    *) fail "expected .img or .img.gz" ;;
esac

if [ -z "$DISK" ]; then
    echo "External physical disks:"
    diskutil list external physical || true
    echo
    echo "Run again with the disk name, for example: $0 $IMAGE disk4"
    exit 0
fi

# --- is it safe? ---
info() { diskutil info -plist "/dev/$DISK" | plutil -extract "$1" raw -o - - 2>/dev/null; }

diskutil info -plist "/dev/$DISK" >/dev/null 2>&1 || fail "diskutil cannot read /dev/$DISK (no such disk, or diskutil is unavailable)"
[ "$(info WholeDisk)" = true ]  || fail "$DISK is not a whole disk (use the disk, not a partition like ${DISK}s1)"
[ "$(info Internal)" = false ]  || fail "$DISK is an INTERNAL disk. Refusing."
size="$(info TotalSize)"
[ -n "$size" ] || fail "cannot read the size of $DISK"
name="$(info MediaName || true)"

limit=$((512 * 1024 * 1024 * 1024))
if [ "$size" -gt "$limit" ] && [ "${R3I6NOS_ALLOW_LARGE:-}" != 1 ]; then
    fail "$DISK is $((size / 1000000000)) GB: larger than any microSD card. Set R3I6NOS_ALLOW_LARGE=1 only if you are sure."
fi
if [ "$size" -lt $((2 * 1024 * 1024 * 1024)) ]; then
    fail "$DISK is too small for the image ($((size / 1000000)) MB)"
fi
if [ "${IMAGE%.gz}" = "$IMAGE" ] && [ "$(wc -c < "$IMAGE")" -gt "$size" ]; then
    fail "the image is larger than $DISK"
fi

# --- checksum ---
if [ -f "$IMAGE.sha256" ]; then
    want="$(cut -d' ' -f1 "$IMAGE.sha256")"
    have="$(shasum -a 256 "$IMAGE" | cut -d' ' -f1)"
    [ "$want" = "$have" ] || fail "checksum mismatch for $IMAGE"
    echo "Checksum OK."
else
    echo "No $IMAGE.sha256 beside the image: checksum NOT verified."
fi

echo
echo "About to ERASE and overwrite:"
echo "  /dev/$DISK   ${name:-unknown}   $((size / 1000000)) MB"
echo "with $IMAGE"
echo
diskutil list "/dev/$DISK" || true

if [ "$DRY" = 1 ]; then
    echo
    echo "--dry-run: nothing written."
    exit 0
fi

echo
printf 'Type the disk name (%s) to erase it, anything else cancels: ' "$DISK"
read -r answer
[ "$answer" = "$DISK" ] || fail "cancelled"

diskutil unmountDisk "/dev/$DISK"
echo "Writing (Ctrl-T shows progress; this takes a few minutes)..."
"${READ[@]}" | sudo dd of="/dev/r$DISK" bs=4m
sync
diskutil eject "/dev/$DISK"
echo "Done. Put the card in TF1 and power on."

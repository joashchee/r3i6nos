#!/bin/bash
# flash-macos.sh <image.img | image.img.gz> [diskN] [--no-games] [--dry-run]
# flash-macos.sh --games-only <diskN> [--dry-run]
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
# By default the rest of the card, whatever its size, then becomes an exFAT partition labelled EASYROMS
# (the games partition: Love 11.5/, Love 12.0/, native_deb/), so one card holds system and games.
#   --no-games    don't: leave the space unused and use a second card labelled EASYROMS in TF2.
#   --games       the default (accepted so old command lines keep working).
#   --games-only  add the games partition to a card that is ALREADY flashed, without writing the image
#                 (existing partitions are kept; it refuses a card that is not an untouched r3i6nos card).
#
# If <image>.sha256 sits beside the image it is checked first. The image alone
# does not boot: run inject-boot.sh on the .img before flashing (not on a .gz).
#
# UNTESTED on a real card. The refusal paths can be tried with --dry-run.

set -eu

IMAGE=""; DISK=""; DRY=0; GAMES=1; ONLY=0
for arg in "$@"; do
    case "$arg" in
        --dry-run) DRY=1 ;;
        --games) GAMES=1 ;;
        --no-games) GAMES=0 ;;
        --games-only) GAMES=1; ONLY=1 ;;
        disk[0-9]*|/dev/disk[0-9]*|/dev/rdisk[0-9]*) DISK="${arg#/dev/}"; DISK="${DISK#r}" ;;
        -*) echo "flash-macos: unknown option: $arg" >&2; exit 2 ;;
        *) IMAGE="$arg" ;;
    esac
done

fail() { echo "flash-macos: $*" >&2; exit 1; }

[ "$(uname -s)" = Darwin ] || fail "macOS only (this is $(uname -s))"
if [ "$ONLY" = 1 ]; then
    [ -z "$IMAGE" ] || fail "--games-only does not write an image; drop $IMAGE (or drop --games-only to flash it)"
else
    [ -n "$IMAGE" ] || fail "usage: $0 <image.img|image.img.gz> [diskN] [--no-games] [--dry-run]   or   $0 --games-only <diskN>"
    [ -f "$IMAGE" ] || fail "no such file: $IMAGE (build or download the image first, and run inject-boot.sh on it)"
fi

READ=()
if [ "$ONLY" = 0 ]; then
    case "$IMAGE" in
        *.img)    READ=(cat "$IMAGE") ;;
        *.img.gz) READ=(gunzip -c "$IMAGE") ;;
        *) fail "expected an .img or .img.gz file, got: $IMAGE" ;;
    esac
fi

if [ -z "$DISK" ]; then
    echo "External physical disks:"
    diskutil list external physical || true
    echo
    echo "Run again with the disk name, for example: $0 ${IMAGE:---games-only} disk4"
    exit 0
fi

# --- is it safe? ---
info() { diskutil info -plist "/dev/$DISK" | plutil -extract "$1" raw -o - - 2>/dev/null; }

diskutil info -plist "/dev/$DISK" >/dev/null 2>&1 || fail "diskutil cannot read /dev/$DISK: there is no such disk (run 'diskutil list external physical' and use the name it shows, like disk4), or diskutil is not usable here"
[ "$(info WholeDisk)" = true ]  || fail "$DISK is not a whole disk: give the disk (like disk4), not a partition (like disk4s1)"
[ "$(info Internal)" = false ]  || fail "$DISK is an INTERNAL disk. Refusing: this script only writes to external cards. If your card reader shows as internal, use a USB reader."
size="$(info TotalSize)"
[ -n "$size" ] || fail "cannot read the size of $DISK from diskutil (is the card still inserted?)"
name="$(info MediaName || true)"

limit=$((512 * 1024 * 1024 * 1024))
if [ "$size" -gt "$limit" ] && [ "${R3I6NOS_ALLOW_LARGE:-}" != 1 ]; then
    fail "$DISK is $((size / 1000000000)) GB: larger than any microSD card, so it is probably not the card. Check 'diskutil list external physical'; set R3I6NOS_ALLOW_LARGE=1 only if you are sure it is."
fi
if [ "$size" -lt $((2 * 1024 * 1024 * 1024)) ]; then
    fail "$DISK is only $((size / 1000000)) MB: too small (the system alone needs 2 GB). Use a bigger card."
fi
if [ "$ONLY" = 0 ] && [ "${IMAGE%.gz}" = "$IMAGE" ] && [ "$(wc -c < "$IMAGE")" -gt "$size" ]; then
    fail "the image ($(( $(wc -c < "$IMAGE") / 1000000 )) MB) is larger than $DISK ($((size / 1000000)) MB): use a bigger card"
fi

# --- checksum ---
if [ "$ONLY" = 1 ]; then
    :
elif [ -f "$IMAGE.sha256" ]; then
    want="$(cut -d' ' -f1 "$IMAGE.sha256")"
    have="$(shasum -a 256 "$IMAGE" | cut -d' ' -f1)"
    [ "$want" = "$have" ] || fail "checksum mismatch for $IMAGE (wanted $want, got $have): the file is damaged or was changed after the checksum was made; download or build it again"
    echo "Checksum OK."
else
    echo "No $IMAGE.sha256 beside the image: checksum NOT verified."
fi

echo
if [ "$ONLY" = 1 ]; then
    echo "About to add a games partition to (existing partitions are kept):"
    echo "  /dev/$DISK   ${name:-unknown}   $((size / 1000000)) MB"
else
    echo "About to ERASE and overwrite:"
    echo "  /dev/$DISK   ${name:-unknown}   $((size / 1000000)) MB"
    echo "with $IMAGE"
fi
echo
diskutil list "/dev/$DISK" || true
if [ "$GAMES" = 1 ]; then
    echo
    echo "The space after the system partition becomes an exFAT partition named EASYROMS (use --no-games to skip this)."
fi

if [ "$DRY" = 1 ]; then
    echo
    echo "--dry-run: nothing written."
    exit 0
fi

echo
if [ "$ONLY" = 1 ]; then
    printf 'Type the disk name (%s) to change its partition table, anything else cancels: ' "$DISK"
else
    printf 'Type the disk name (%s) to erase it, anything else cancels: ' "$DISK"
fi
read -r answer
[ "$answer" = "$DISK" ] || fail "cancelled (you typed '$answer', not '$DISK'); nothing was written"

diskutil unmountDisk "/dev/$DISK" || fail "could not unmount /dev/$DISK: close any app or Finder window using the card and try again; nothing was written"
if [ "$ONLY" = 0 ]; then
    echo "Writing (Ctrl-T shows progress; this takes a few minutes)..."
    "${READ[@]}" | sudo dd of="/dev/r$DISK" bs=4m || fail "writing the image to /dev/r$DISK failed (see dd's message above). Common causes: the sudo password was wrong, the card's write-protect switch is on, the card reader is flaky, or this terminal lacks permission for removable volumes (System Settings > Privacy & Security > Full Disk Access / Removable Volumes). The card may be half-written: do not use it until you flash it again."
    sync
fi

# The games partition: partition 3 in the free space, formatted exFAT, with the folders the menu looks in.
# Every failure says what happened and what to do; the system partitions are never touched by this step.
# The one change that cannot be undone by running this again is the partition table entry, so each
# message says whether it was written yet.
games_fail() {  # games_fail <step> <what happened> <what to do>
    {
        echo
        echo "flash-macos: THE GAMES PARTITION WAS NOT MADE."
        echo "  Step:         $1"
        echo "  What happened: $2"
        echo "  What to do:    $3"
        echo "  The system partitions on $DISK are intact and the card boots; only the games space is not usable yet."
        echo "  Meanwhile you can use a second card labelled EASYROMS in TF2."
    } >&2
    return 1
}
games_partition() {
    local tmp out why start_size mnt s3="/dev/${DISK}s3" retry="scripts/flash-macos.sh --games-only $DISK"
    tmp="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-games.XXXXXX")" || { games_fail "making a temporary folder" "mktemp failed in ${TMPDIR:-/tmp}" "free some space or set TMPDIR to a writable folder, then run: $retry"; return 1; }

    # 1. Read sector 0 into a file this shell owns (python, not root, edits it next).
    # shellcheck disable=SC2024  # on purpose: this shell opens the file, so it is ours and not root's
    sudo dd if="/dev/r$DISK" bs=512 count=1 2>/dev/null > "$tmp/mbr" \
        || { games_fail "reading the card's partition table" "could not read sector 0 of /dev/r$DISK with sudo" "check the sudo password, and that this terminal may read removable volumes (System Settings > Privacy & Security > Full Disk Access). Then run: $retry"; return 1; }

    # 2. Work out partition 3 (start after ROOTFS, to the end of the card). Nothing is written yet.
    if ! out="$(python3 -I "$(dirname "$0")/add_games_partition.py" "$tmp/mbr" "$size" 2>&1)"; then
        case "$out" in
            *"disk signature differs"*) why="this card was not flashed by this script, so it is not an r3i6nos card; nothing was changed. Flash it first (without --games-only)." ;;
            *"already in use"*) why="the card already has a partition 3 or 4. If it is the games partition, you are done: see 'diskutil list $DISK'. If it exists but will not mount, format it: diskutil eraseVolume ExFAT EASYROMS $s3" ;;
            *"not enough for a games partition"*) why="the free space after the system partition is under 1 GiB: use a bigger card, or a second card labelled EASYROMS in TF2." ;;
            *"missing"*|*"not an MBR"*) why="the card's partition table is not the one this script wrote: re-flash the image, then add the games partition." ;;
            *) why="read the message above; if it is not clear, run 'diskutil list $DISK' and report it." ;;
        esac
        games_fail "working out the partition" "add_games_partition.py refused: ${out:-no message}" "$why"
        return 1
    fi
    start_size="$out"

    # 3. Write the new table entry back. This is the point of no return for the partition table.
    sudo dd if="$tmp/mbr" of="/dev/r$DISK" bs=512 count=1 conv=notrunc 2>/dev/null \
        || { games_fail "writing the partition table" "could not write sector 0 of /dev/r$DISK (nothing was changed)" "check the card's write-protect switch and the sudo password, then run: $retry"; return 1; }
    rm -rf "$tmp"
    echo "Partition 3 added: start and size in sectors: $start_size"

    # 4. macOS must notice the new partition.
    for _ in $(seq 1 "${R3I6NOS_WAIT_S3:-20}"); do
        diskutil info "$s3" >/dev/null 2>&1 && break
        sleep 1
    done
    diskutil info "$s3" >/dev/null 2>&1 \
        || { games_fail "waiting for macOS to see $s3" "the partition table is written but macOS has not re-read it in time" "eject and re-insert the card, check 'diskutil list $DISK' shows a third partition, then format it: diskutil eraseVolume ExFAT EASYROMS $s3  and make the folders (Love 11.5, Love 12.0, native_deb) on it. Do not re-run this script: partition 3 now exists."; return 1; }

    # 5. Format it exFAT, labelled EASYROMS.
    diskutil eraseVolume ExFAT EASYROMS "$s3" \
        || { games_fail "formatting $s3 as exFAT" "diskutil eraseVolume failed (its message is above)" "try again: diskutil eraseVolume ExFAT EASYROMS $s3 ; or format partition 3 in Disk Utility as ExFAT named EASYROMS. Partition 3 exists, so do not re-run this script."; return 1; }

    # 6. The folders the menu looks in.
    mnt="$(diskutil info -plist "$s3" | plutil -extract MountPoint raw -o - - 2>/dev/null || true)"
    [ -n "$mnt" ] \
        || { games_fail "finding where EASYROMS is mounted" "it was formatted, but macOS did not mount it" "eject and re-insert the card; when /Volumes/EASYROMS appears, create the folders 'Love 11.5', 'Love 12.0' and 'native_deb' on it yourself."; return 1; }
    mkdir -p "$mnt/Love 11.5" "$mnt/Love 12.0" "$mnt/native_deb" \
        || { games_fail "making the folders on $mnt" "mkdir failed (is the volume read-only?)" "create the folders 'Love 11.5', 'Love 12.0' and 'native_deb' on $mnt yourself."; return 1; }
    echo "Games partition ready: $mnt (Love 11.5, Love 12.0, native_deb)"
}
GAMES_OK=1
if [ "$GAMES" = 1 ]; then
    games_partition || GAMES_OK=0
fi

if [ "$GAMES_OK" = 0 ]; then
    echo
    echo "Not ejecting $DISK, so you can look at it ('diskutil list $DISK'). Eject it yourself when done." >&2
    exit 1
fi
diskutil eject "/dev/$DISK" || echo "flash-macos: could not eject /dev/$DISK; eject it from Finder before removing the card." >&2
echo "Done. Put the card in TF1 and power on."

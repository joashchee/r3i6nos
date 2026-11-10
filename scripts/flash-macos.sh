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
#   --no-games    don't: leave the space unused and use a second card in TF2 (exFAT or FAT32, any label).
#   --games       the default (accepted so old command lines keep working).
#   --games-only  add the games partition to a card that is ALREADY flashed, without writing the image
#                 (existing partitions are kept; it refuses a card that is not an untouched r3i6nos card).
#
# The R36S ports (KlamANSI, Neumetik, Composer, Painter, Animator, Crunchy, NoCoGaMa) are copied onto EASYROMS from their newest local
# builds, each in its own repo beside this one (R3I6NOS_PORTS_ROOT overrides where): a .deb into native_deb/ (deb_watcher installs
# it at boot), a .love into "Love 11.5/" and "Love 12.0/". A port with no local build is named, with the command that builds it;
# one built before its repo's last commit is flagged. The flash goes on either way. --dry-run shows the list.
# The test corpus (corpus/files: one file of each format the apps read, corpus/README.md) is copied too, from this repo: all of it into
# "Test files/", Neumetik's music into "Neumetik/music/Test files/" and KlamANSI's drawing into "KlamANSI/drawings/test-files/".
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

# --- the R36S ports, from their local builds ---
# name|repo folder|kind|its build in the repo ({v}: the LÖVE version)|the command that makes it, run in the repo
PORTS_ROOT="${R3I6NOS_PORTS_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"
PORTS=(
    "KlamANSI|klamansi|deb|r36s/build/pkg/KlamANSI.deb|r36s/build-deb.sh"
    "Neumetik|neumetik|deb|r36s/build/pkg/Neumetik.deb|r36s/build-deb.sh"
    "Composer|composer|deb|r36s/build/pkg/compose.deb|r36s/build-deb.sh"
    "Painter|painter|deb|r36s/build/pkg/sprites.deb|r36s/build-deb.sh"
    "Animator|animator|deb|r36s/build/pkg/animate.deb|r36s/build-deb.sh"
    "Crunchy|crunchy|deb|r36s/build/pkg/Crunchy.deb|r36s/build-deb.sh"
    "NoCoGaMa|nocogama|love|build/Love {v}/*.love|scripts/build.sh"
)
PORT_COPY=()      # "source<TAB>path on EASYROMS"
PORT_MISSING=()   # one line a port (or LÖVE version) with no local build
PORT_STALE=()     # built before the repo's last commit
CORPUS_COPY=()    # the test corpus (corpus/files), "source<TAB>path on EASYROMS"

newest() {  # the newest existing file of the arguments (a glob that matched nothing is skipped)
    local f best=""
    for f in "$@"; do
        [ -f "$f" ] || continue
        if [ -z "$best" ] || [ "$f" -nt "$best" ]; then best="$f"; fi
    done
    printf '%s' "$best"
}
note_stale() {  # <name> <repo> <file>
    local made last
    # date -r FILE and git's own date format behave the same on macOS and Linux (the tests run on both); stat -f does not
    made="$(date -r "$3" +%s 2>/dev/null || echo 0)"
    last="$(git -C "$2" log -1 --format=%ct 2>/dev/null || echo 0)"
    if [ "$last" -gt "$made" ]; then
        PORT_STALE+=("$1: $(basename "$3") was built $(date -r "$3" '+%Y-%m-%d %H:%M'), before the repo's last commit ($(git -C "$2" log -1 --format=%cd --date=format:'%Y-%m-%d %H:%M')): it may not be the latest")
    fi
}
find_ports() {
    local entry name dir kind rel how repo f v base path
    for entry in "${PORTS[@]}"; do
        IFS='|' read -r name dir kind rel how <<<"$entry"
        repo="$PORTS_ROOT/$dir"
        if [ ! -d "$repo" ]; then
            PORT_MISSING+=("$name: no repo at $repo")
            continue
        fi
        if [ "$kind" = deb ]; then
            f="$(newest "$repo/$rel")"
            if [ -n "$f" ]; then PORT_COPY+=("$f	native_deb/$(basename "$f")"); note_stale "$name" "$repo" "$f"
            else PORT_MISSING+=("$name: no $rel (build it: cd $repo && $how)"); fi
        else
            for v in 11.5 12.0; do
                path="${rel//\{v\}/$v}"
                # the folder is quoted (it has a space: "Love 11.5"), the file pattern is not, so it expands
                # shellcheck disable=SC2086
                f="$(newest "$repo/${path%/*}"/${path##*/})"
                if [ -n "$f" ]; then
                    base="$(basename "$f" .love)"; base="${base%-"$v"}"   # NoCoGaMa-11.5.love -> NoCoGaMa.love: the menu shows the file's name
                    PORT_COPY+=("$f	Love $v/$base.love"); note_stale "$name ($v)" "$repo" "$f"
                else PORT_MISSING+=("$name for LÖVE $v: no ${rel//\{v\}/$v} (build it: cd $repo && $how)"); fi
            done
        fi
    done
}
# The test corpus (corpus/README.md): all of it in "Test files/", and the files of the apps that browse only their own folder in that
# folder too (Neumetik's music, KlamANSI's drawings). Comes from this repo, so it is always there.
find_corpus() {
    local dir f
    dir="$(cd "$(dirname "$0")/.." && pwd)/corpus/files"
    [ -d "$dir" ] || return 0
    for f in "$dir"/*; do [ -f "$f" ] && CORPUS_COPY+=("$f	Test files/$(basename "$f")"); done
    while read -r f; do [ -n "$f" ] && CORPUS_COPY+=("$dir/$f	Neumetik/music/Test files/$f"); done \
        < <(python3 -I "$(dirname "$0")/corpus_extra.py" list neumetik)
    while read -r f; do [ -n "$f" ] && CORPUS_COPY+=("$dir/$f	KlamANSI/drawings/test-files/$f"); done \
        < <(python3 -I "$(dirname "$0")/corpus_extra.py" list klamansi)
    return 0
}
show_ports() {  # <heading for the list of builds>
    local line
    if [ "${#PORT_COPY[@]}" -gt 0 ]; then
        echo "$1 ($PORTS_ROOT):"
        for line in "${PORT_COPY[@]}"; do echo "  ${line#*	}   <- ${line%%	*}"; done
    fi
    if [ "${#PORT_MISSING[@]}" -gt 0 ]; then
        echo "MISSING (not on the card):"
        for line in "${PORT_MISSING[@]}"; do echo "  $line"; done
    fi
    if [ "${#PORT_STALE[@]}" -gt 0 ]; then
        echo "Possibly out of date:"
        for line in "${PORT_STALE[@]}"; do echo "  $line"; done
    fi
    [ "${#CORPUS_COPY[@]}" -gt 0 ] && echo "The test corpus: ${#CORPUS_COPY[@]} file(s) into Test files/, Neumetik/music/Test files/ and KlamANSI/drawings/test-files/"
    return 0
}
copy_ports() {  # <EASYROMS mount point>: never fails the flash; says what did not copy
    local line src dest bad=0
    for line in "${PORT_COPY[@]+"${PORT_COPY[@]}"}" "${CORPUS_COPY[@]+"${CORPUS_COPY[@]}"}"; do
        src="${line%%	*}"; dest="$1/${line#*	}"
        # -X: no extended attributes, so macOS writes no ._ file beside it (BSD cp only; elsewhere a plain copy)
        if ! { mkdir -p "$(dirname "$dest")" && { cp -X "$src" "$dest" 2>/dev/null || cp "$src" "$dest"; }; }; then
            echo "flash-macos: could not copy $src to $dest (copy it yourself)" >&2
            bad=1
        fi
    done
    # macOS writes ._ files on exFAT; the menu and deb_watcher would take ._X.love or ._X.deb for an app
    dot_clean -m "$1" 2>/dev/null || true
    find "$1" -name '._*' -delete 2>/dev/null || true
    [ "$bad" = 0 ] && [ "${#PORT_COPY[@]}" -gt 0 ] && echo "Ports copied: ${#PORT_COPY[@]} file(s)."
    [ "$bad" = 0 ] && [ "${#CORPUS_COPY[@]}" -gt 0 ] && echo "Test corpus copied: ${#CORPUS_COPY[@]} file(s)."
    if [ "${#PORT_MISSING[@]}" -gt 0 ]; then
        echo "Not copied, no local build (build them and run this again with --games-only on a fresh card, or copy them onto EASYROMS):" >&2
        for line in "${PORT_MISSING[@]}"; do echo "  $line" >&2; done
    fi
    return 0
}
find_ports
find_corpus

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
    echo
    show_ports "R36S ports copied onto it from their newest local builds"
else
    echo
    echo "--no-games: the R36S ports are not copied (no games partition)."
    show_ports "Their newest local builds, to copy onto the card in TF2 yourself (same folders)"
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
        echo "  Meanwhile you can use a second card in TF2 (exFAT or FAT32, any label)."
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
            *"not enough for a games partition"*) why="the free space after the system partition is under 1 GiB: use a bigger card, or a second card in TF2 (exFAT or FAT32, any label)." ;;
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
    copy_ports "$mnt"
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

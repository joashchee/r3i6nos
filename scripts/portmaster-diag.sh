#!/bin/bash
# scripts/portmaster-diag.sh - what stops PortMaster ports on an r3i6nos unit. Read-only, no root needed (it says what root would have shown).
#
# Run it ON the unit (SSH, as loveos):   bash portmaster-diag.sh > /roms/ports/portmaster-diag.txt
# or from the Mac, nothing copied:        ssh loveos@<unit> 'bash -s' < scripts/portmaster-diag.sh > diag.txt
# Then give diag.txt to Claude. It names no account or password and sends nothing anywhere.
set -u
PM="${R3I6NOS_PORTMASTER_DIR:-/roms/ports/PortMaster}"
PORTS="${R3I6NOS_PORTS_DIR:-/roms/ports}"

section() { printf '\n== %s ==\n' "$1"; }

section "system"
uname -a
id
date
grep -m1 PRETTY_NAME /etc/os-release 2>/dev/null
sudo -n true 2>&1 | head -1 | sed 's/^/sudo -n true: /'

section "kernel features (zcat /proc/config.gz)"
if [ -r /proc/config.gz ]; then
    zcat /proc/config.gz | grep -E '^(# )?CONFIG_(INPUT_UINPUT|SQUASHFS|SQUASHFS_ZSTD|SQUASHFS_XZ|SQUASHFS_ZLIB|SQUASHFS_LZ4|BLK_DEV_LOOP|BLK_DEV_LOOP_MIN_COUNT|FUSE_FS|OVERLAY_FS|USER_NS|ZRAM|SWAP|TMPFS)[ =]'
else
    echo "no /proc/config.gz"
fi
echo "-- filesystems"; grep -E 'squashfs|fuse|overlay' /proc/filesystems
echo "-- modules"; lsmod 2>/dev/null | grep -E 'uinput|loop|squashfs|fuse' || echo "none loaded (or lsmod missing)"
find "/lib/modules/$(uname -r)" -maxdepth 1 2>/dev/null | head -3 | sed 's/^/modules dir: /'

section "devices"
ls -l /dev/uinput /dev/input/uinput /dev/loop-control /dev/loop0 /dev/tty0 /dev/dri/* /dev/fuse 2>&1
find /dev/input -maxdepth 1 -ls 2>&1 | head -20
grep -E 'Name=|Handlers=' /proc/bus/input/devices 2>/dev/null | paste - - | head -12

section "can a login user mount a runtime? (it should say must be superuser)"
sq=""
for f in "$PM"/libs/*.squashfs; do [ -e "$f" ] && { sq="$f"; break; }; done
if [ -n "$sq" ]; then
    d="$(mktemp -d)"
    echo "file: $sq"
    mount -t squashfs -o ro,loop "$sq" "$d" 2>&1 | head -3
    mountpoint -q "$d" && { echo "MOUNTED as this user"; umount "$d"; }
    rmdir "$d"
else
    echo "no runtime in $PM/libs yet (towerjumper's download would have put frt_2.1.6 there)"
fi
for t in squashfuse fuse2fs fusermount fusermount3 unsquashfs unshare; do command -v "$t" >/dev/null 2>&1 && echo "has $t"; done

section "gptokeyb"
pgrep -a gptokeyb 2>/dev/null || echo "not running now"
if [ -x "$PM/gptokeyb" ]; then
    ldd "$PM/gptokeyb" 2>&1 | grep -E 'not found' || echo "gptokeyb: all libraries found"
    "$PM/gptokeyb" 2>&1 | head -3
fi

section "memory, space, tmp"
free -m
df -h /tmp /roms "$PM" 2>/dev/null
ls -ld /tmp

section "ports on the card"
find "$PORTS" -maxdepth 1 -ls 2>&1 | head -40
find "$PM/libs" -maxdepth 1 2>&1 | head -20

section "missing libraries in each port's binaries"
for dir in "$PORTS"/*/; do
    n="$(basename "$dir")"
    [ "$n" = PortMaster ] && continue
    out=""
    while IFS= read -r f; do
        miss="$(LD_LIBRARY_PATH="$dir/libs.aarch64:$dir/libs:$dir/gl4es.aarch64:$dir/gl4es:${LD_LIBRARY_PATH:-}" ldd "$f" 2>/dev/null | grep 'not found' | sort -u | tr '\n' ' ')"
        [ -n "$miss" ] && out="$out  $(basename "$f"): $miss"$'\n'
    done < <(find "$dir" -maxdepth 2 -type f \( -perm -u+x -o -name '*.so*' -o -name '*.aarch64' \) 2>/dev/null | head -12)
    [ -n "$out" ] && printf '%s\n%s' "$n" "$out"
done

section "port logs (last 25 lines of each)"
for log in "$PM/log.txt" "$PORTS"/*/log.txt; do
    [ -f "$log" ] || continue
    printf '\n--- %s\n' "$log"
    tail -n 25 "$log"
done

section "kernel messages (if readable)"
dmesg 2>&1 | tail -n 25

printf '\n== end ==\n'

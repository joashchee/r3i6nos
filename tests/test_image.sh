#!/bin/bash
# Builds a tiny image from a fake rootfs, checks it, injects fake boot assets
# and reads them back. Needs Linux, root, and the image tools; anywhere else it
# says NOT RUN and exits 0 (CI's build workflow runs it as root).
set -eu
cd "$(dirname "$0")/.."

missing=""
for tool in sfdisk mke2fs mkfs.fat mcopy losetup e2fsck fsck.vfat blkid python3; do
    command -v "$tool" >/dev/null || missing="$missing $tool"
done
if [ "$(uname -s)" != Linux ] || [ "$(id -u)" -ne 0 ] || [ -n "$missing" ]; then
    echo "NOT RUN: needs Linux, root and:${missing:- (all present)}"
    exit 0
fi

T="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-imgtest.XXXXXX")"
trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }

# A fake rootfs: what configure_rootfs.sh installs, plus the user and an empty /proc.
R="$T/rootfs"
mkdir -p "$R/etc" "$R/proc" "$R/sys" "$T/love/11.5/bin"
printf 'loveos:x:1000:1000::/home/loveos:/usr/sbin/nologin\n' > "$R/etc/passwd"
printf '#!/bin/sh\n' > "$T/love/11.5/bin/love"; chmod +x "$T/love/11.5/bin/love"
R3I6NOS_LOVE_BUILD="$T/love" scripts/configure_rootfs.sh "$R" >/dev/null

IMG="$T/test.img"
R3I6NOS_BOOT_MIB=64 R3I6NOS_ROOT_MIB=64 scripts/assemble_image.sh "$R" "$IMG" >/dev/null
scripts/check_image.sh "$IMG" >/dev/null || fail "check_image rejected a good image"

# A rootfs with a mounted-looking /proc must be refused.
touch "$R/proc/leftover"
if scripts/assemble_image.sh "$R" "$T/bad.img" >/dev/null 2>&1; then fail "non-empty /proc was accepted"; fi
[ ! -e "$T/bad.img" ] || fail "a refused build left an image behind"
rm "$R/proc/leftover"

# Inject fake boot assets, check they read back and the MBR survived.
A="$T/assets"
mkdir -p "$A/boot/dtb"
head -c 100000 /dev/urandom > "$A/bootloader.bin"
printf 'kernel' > "$A/boot/Image"; printf 'dtb' > "$A/boot/dtb/board.dtb"
before="$(head -c 512 "$IMG" | od -An -tx1 | tr -d ' \n')"
scripts/inject-boot.sh "$IMG" "$A" >/dev/null || fail "inject-boot failed"
after="$(head -c 512 "$IMG" | od -An -tx1 | tr -d ' \n')"
[ "$before" = "$after" ] || fail "inject-boot changed the MBR"
scripts/check_image.sh "$IMG" >/dev/null || fail "image no longer passes after injection"

# An oversize bootloader must be refused.
head -c $(( (32768 - 64) * 512 + 1 )) /dev/zero | tr '\0' 'x' > "$A/bootloader.bin"
if scripts/inject-boot.sh "$IMG" "$A" >/dev/null 2>&1; then fail "oversize bootloader accepted"; fi

echo "image build, check and boot injection: ok"

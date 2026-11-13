#!/bin/bash
# Builds a tiny image from a fake rootfs, checks it, injects fake boot assets
# and reads them back. Needs Linux, root, and the image tools; anywhere else it
# says NOT RUN and exits 0 (CI's build workflow runs it as root).
set -eu
cd "$(dirname "$0")/.."

missing=""
for tool in sfdisk mke2fs mkfs.fat mcopy losetup e2fsck fsck.vfat blkid debugfs python3; do
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
mkdir -p "$A/boot/ScreenFiles/Panel 1"
BOOTARGS='setenv bootargs "root=UUID='"'"'e139ce78-9841-40fe-8823-96a304a09859'"'"' rootwait rw quiet"'
printf '%s\n' "$BOOTARGS" > "$A/boot/boot.ini"
printf '%s\n' "$BOOTARGS" > "$A/boot/ScreenFiles/Panel 1/boot.ini"
before="$(head -c 512 "$IMG" | od -An -tx1 | tr -d ' \n')"
scripts/inject-boot.sh "$IMG" "$A" >/dev/null || fail "inject-boot failed"
after="$(head -c 512 "$IMG" | od -An -tx1 | tr -d ' \n')"
[ "$before" = "$after" ] || fail "inject-boot changed the MBR"
scripts/check_image.sh "$IMG" >/dev/null || fail "image no longer passes after injection"

# root= was pointed at this image, in both boot.ini files, and the assets were left alone.
ROOT_UUID="$(. scripts/image-layout.sh; echo "$ROOT_UUID")"
for f in boot.ini "ScreenFiles/Panel 1/boot.ini"; do
    mcopy -n -i "$IMG@@$((32768 * 512))" "::/$f" - | grep -q "root=UUID='$ROOT_UUID' rootwait" \
        || fail "$f was not rewritten to $ROOT_UUID"
done
grep -q 'e139ce78' "$A/boot/boot.ini" || fail "inject-boot modified the assets"
# the boot logo is r3i6nos's, not the stock one, and the stock one stays when asked
mcopy -n -i "$IMG@@$((32768 * 512))" "::/logo.bmp" - | cmp - overlay/usr/share/r3i6nos/boot-logo.bmp || fail "the boot logo on BOOT is not r3i6nos's"

# Kernel modules, firmware and the Mali blob go into the ROOTFS; the image stays valid; a second go is refused.
G="$T/gpuassets"
mkdir -p "$G/rootfs/lib/modules/9.9.9/kernel" "$G/rootfs/lib/firmware" "$G/rootfs/gpu" "$G/info"
printf 'module' > "$G/rootfs/lib/modules/9.9.9/kernel/a.ko"
printf 'fw' > "$G/rootfs/lib/firmware/x.bin"
printf 'blob' > "$G/rootfs/gpu/libmali-test.so"
printf 'link libMali.so -> libmali-test.so\nlink libEGL.so.1 -> libMali.so\nlink libGLESv2.so.2 -> libMali.so\nlink libgbm.so.1 -> libMali.so\n' > "$G/info/stock-gpu-libs.txt"
scripts/inject-rootfs.sh "$IMG" "$G" >/dev/null || fail "inject-rootfs failed"
scripts/check_image.sh "$IMG" >/dev/null || fail "image no longer passes after inject-rootfs"
if scripts/inject-rootfs.sh "$IMG" "$G" >/dev/null 2>&1; then fail "a second inject-rootfs was accepted"; fi
# A blob missing the required libraries must be refused before anything is written.
rm "$G/info/stock-gpu-libs.txt"; printf 'x' > "$G/info/stock-gpu-libs.txt"
if scripts/inject-rootfs.sh "$IMG" "$G" >/dev/null 2>&1; then fail "a blob without libEGL/libgbm links was accepted"; fi

# Boot files with no root= at all must be refused.
mkdir -p "$T/noroot/boot"; printf 'kernel' > "$T/noroot/boot/Image"; printf 'echo hi\n' > "$T/noroot/boot/boot.ini"
cp "$A/bootloader.bin" "$T/noroot/"
if scripts/inject-boot.sh "$IMG" "$T/noroot" >/dev/null 2>&1; then fail "boot files without root= were accepted"; fi

# An oversize bootloader must be refused.
head -c $(( (32768 - 64) * 512 + 1 )) /dev/zero | tr '\0' 'x' > "$A/bootloader.bin"
if scripts/inject-boot.sh "$IMG" "$A" >/dev/null 2>&1; then fail "oversize bootloader accepted"; fi

echo "image build, check and boot injection: ok"

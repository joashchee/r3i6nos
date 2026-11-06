#!/bin/bash
# build_rootfs.sh [rootfs-dir]
#
# Builds the Debian Bookworm arm64 rootfs for r3i6nos. Run as root on a Linux
# host (CI runs it on ubuntu-latest). Default output: build/rootfs.
#
#   sudo apt-get install debootstrap qemu-user-static binfmt-support
#   sudo scripts/build_rootfs.sh
#
# Stages: debootstrap --foreign (unpack), second stage under qemu-user,
# configure_rootfs.sh (files), then the chroot steps below (user, root lock, cleanup).
# The LÖVE runtimes are phase 3 and are not here yet.

set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"
ROOTFS="${1:-$REPO/build/rootfs}"
SUITE=bookworm
MIRROR="${R3I6NOS_MIRROR:-https://deb.debian.org/debian/}"

# Needed on the device. Comments say why, so nothing gets removed by accident.
PACKAGES=(
    systemd systemd-sysv udev kmod       # boot
    dpkg python3-minimal                 # deb_watcher.py (stdlib only)
    libsdl2-2.0-0                        # native apps and LÖVE
    libdrm2 libgbm1 libegl1 libgles2     # SDL's KMS/DRM driver
    libgl1-mesa-dri                      # Mesa panfrost for the Mali-G31
    libasound2 alsa-utils                # audio
    libopenal1 libfreetype6 libmpg123-0  # LÖVE runtime libraries (phase 3 may add more)
    libvorbisfile3 libtheora0 libmodplug1
    exfatprogs                           # a likely games-card filesystem
)

if [ "$(id -u)" -ne 0 ]; then
    echo "build_rootfs: run as root (debootstrap and chroot need it)" >&2
    exit 1
fi
for tool in debootstrap chroot; do
    command -v "$tool" >/dev/null || { echo "build_rootfs: $tool not installed" >&2; exit 1; }
done
QEMU="$(command -v qemu-aarch64-static || true)"
if [ "$(uname -m)" != aarch64 ] && [ -z "$QEMU" ]; then
    echo "build_rootfs: qemu-aarch64-static needed on a non-arm64 host" >&2
    exit 1
fi

if [ -e "$ROOTFS" ]; then
    echo "build_rootfs: $ROOTFS exists; remove it first (no overwriting a tree)" >&2
    exit 1
fi
mkdir -p "$ROOTFS"

mounted=0
unmount_chroot() {
    if [ "$mounted" = 1 ]; then
        umount "$ROOTFS/dev/pts" "$ROOTFS/dev" "$ROOTFS/sys" "$ROOTFS/proc" 2>/dev/null || true
        mounted=0
    fi
}
cleanup() {
    unmount_chroot
    if [ -n "$QEMU" ]; then rm -f "$ROOTFS/usr/bin/qemu-aarch64-static"; fi
}
trap cleanup EXIT

echo "[*] Debootstrap stage 1 ($SUITE, arm64)..."
INCLUDE="$(IFS=,; echo "${PACKAGES[*]}")"
debootstrap --foreign --arch=arm64 --variant=minbase --include="$INCLUDE" \
    "$SUITE" "$ROOTFS" "$MIRROR"

if [ -n "$QEMU" ]; then
    install -D "$QEMU" "$ROOTFS/usr/bin/qemu-aarch64-static"
fi

echo "[*] Debootstrap stage 2 (under qemu)..."
chroot "$ROOTFS" /debootstrap/debootstrap --second-stage

"$REPO/scripts/configure_rootfs.sh" "$ROOTFS"

echo "[*] Chroot steps..."
mount -t proc proc "$ROOTFS/proc"
mount -t sysfs sys "$ROOTFS/sys"
mount --bind /dev "$ROOTFS/dev"
mount --bind /dev/pts "$ROOTFS/dev/pts"
mounted=1

# uid 1000 is what the fstab's uid=1000 for the games card assumes.
chroot "$ROOTFS" useradd --uid 1000 --create-home --shell /usr/sbin/nologin \
    --groups video,audio,input,render loveos
chroot "$ROOTFS" passwd -l root
chroot "$ROOTFS" chown -R loveos:loveos /opt/native_apps /opt/launchers
chroot "$ROOTFS" apt-get clean
rm -rf "$ROOTFS"/var/lib/apt/lists/* "$ROOTFS"/debootstrap

# Unmount before anything walks the tree: /proc is empty again and du/tar see the real rootfs.
unmount_chroot
if [ -n "$QEMU" ]; then rm -f "$ROOTFS/usr/bin/qemu-aarch64-static"; fi

echo "[*] Rootfs ready: $ROOTFS"
du -sx -h "$ROOTFS"

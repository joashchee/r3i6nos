#!/bin/bash
# configure_rootfs.sh <rootfs-dir>
#
# Puts r3i6nos onto an existing Debian rootfs: overlay, menu, fstab, hostname,
# journal and unit settings. Plain file operations only (no chroot, no root
# needed), so tests/test_configure_rootfs.sh can run it on a laptop.
# build_rootfs.sh runs it after debootstrap, then does the parts that need a chroot.

set -eu

ROOTFS="${1:?Usage: $0 <rootfs-dir>}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"

[ -d "$ROOTFS" ] || { echo "configure_rootfs: no such directory: $ROOTFS" >&2; exit 1; }

echo "[*] Installing overlay and menu..."
cp -a "$REPO/overlay/." "$ROOTFS/"
mkdir -p "$ROOTFS/opt/loveos"
rm -rf "$ROOTFS/opt/loveos/menu"
cp -a "$REPO/menu" "$ROOTFS/opt/loveos/menu"
chmod 755 "$ROOTFS"/usr/local/bin/*

# Mount points and places the boot scripts write to.
mkdir -p "$ROOTFS"/{roms,boot,opt/love,opt/native_apps,opt/launchers/native}

echo "[*] System settings..."
echo "r3i6nos" > "$ROOTFS/etc/hostname"
cat > "$ROOTFS/etc/hosts" <<'EOF'
127.0.0.1 localhost
127.0.1.1 r3i6nos
EOF

# Labels match assemble_image.sh. The games card (TF2) is optional at boot.
# uid/gid make a FAT or exFAT card readable by the loveos user (uid 1000);
# an ext4 card would refuse these options (phase 1 confirms the real filesystem).
cat > "$ROOTFS/etc/fstab" <<'EOF'
LABEL=ROOTFS   /      ext4  defaults,noatime,errors=remount-ro  0 1
LABEL=BOOT     /boot  vfat  defaults,noatime,nofail             0 2
LABEL=EASYROMS /roms  auto  nofail,noatime,uid=1000,gid=1000,umask=022,x-systemd.device-timeout=5s  0 0
tmpfs          /tmp   tmpfs defaults,nosuid,nodev,size=64m      0 0
EOF

# Journal in RAM: no SD card wear, and the logs are only for debugging a boot.
mkdir -p "$ROOTFS/etc/systemd/journald.conf.d"
cat > "$ROOTFS/etc/systemd/journald.conf.d/volatile.conf" <<'EOF'
[Journal]
Storage=volatile
RuntimeMaxUse=16M
EOF

echo "[*] Enabling loveos.service..."
mkdir -p "$ROOTFS/etc/systemd/system/multi-user.target.wants"
ln -sfn /etc/systemd/system/loveos.service \
    "$ROOTFS/etc/systemd/system/multi-user.target.wants/loveos.service"
ln -sfn /lib/systemd/system/multi-user.target "$ROOTFS/etc/systemd/system/default.target"
for unit in apt-daily.timer apt-daily-upgrade.timer; do
    ln -sfn /dev/null "$ROOTFS/etc/systemd/system/$unit"
done

echo "[*] Trimming (licence texts stay: redistribution needs them)..."
rm -rf "$ROOTFS"/usr/share/{man,info,lintian}
if [ -d "$ROOTFS/usr/share/doc" ]; then
    find "$ROOTFS/usr/share/doc" -type f ! -name copyright -delete
    find "$ROOTFS/usr/share/doc" -mindepth 1 -type d -empty -delete
fi

echo "[*] Configured: $ROOTFS"

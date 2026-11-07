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
# The r3i6nos-ansiapps theme, shared by the menu and every LÖVE app (they add /opt/loveos/theme to package.path).
rm -rf "$ROOTFS/opt/loveos/theme"
cp -a "$REPO/theme" "$ROOTFS/opt/loveos/theme"
chmod 755 "$ROOTFS"/usr/local/bin/*

# LÖVE runtimes built by build_love.sh (build/love/<version>/) go to /opt/love/<version>/.
LOVE_BUILD="${R3I6NOS_LOVE_BUILD:-$REPO/build/love}"
if [ -d "$LOVE_BUILD" ]; then
    for dir in "$LOVE_BUILD"/*/; do
        [ -x "${dir}bin/love" ] || continue
        ver="$(basename "$dir")"
        echo "[*] Installing LÖVE $ver..."
        mkdir -p "$ROOTFS/opt/love"
        rm -rf "$ROOTFS/opt/love/$ver"
        cp -a "$dir" "$ROOTFS/opt/love/$ver"
    done
else
    echo "[!] No LÖVE runtimes in $LOVE_BUILD: the menu cannot start until phase 3 builds them" >&2
fi

# Default apps, built into the image so the menu is never empty (the games card's own files are listed beside
# them, and replace one of the same name): the input checker as a LÖVE 11.5 app, a LÖVE 12.0 app and a native
# ARM64 .deb (built by build_native_apps.sh; deb_watcher.py installs it at boot). None is built here: each app has its own build.
DEFAULTS="$ROOTFS/opt/loveos/defaults"
rm -rf "$DEFAULTS"
mkdir -p "$DEFAULTS/Love 11.5" "$DEFAULTS/Love 12.0" "$DEFAULTS/native_deb"
# The LÖVE default apps are built by scripts/build_love_app.sh (build/love-apps/Love <version>/<NAME>.love); this only copies them.
LOVE_APPS_BUILD="${R3I6NOS_LOVE_APPS_BUILD:-$REPO/build/love-apps}"
have_love_apps=""
for love in "$LOVE_APPS_BUILD"/Love\ */*.love; do
    if [ -f "$love" ]; then
        mkdir -p "$DEFAULTS/$(basename "$(dirname "$love")")"
        cp "$love" "$DEFAULTS/$(basename "$(dirname "$love")")/"
        have_love_apps=1
    fi
done
if [ -z "$have_love_apps" ]; then
    echo "[!] No LÖVE default app in $LOVE_APPS_BUILD: run scripts/build_love_app.sh input-checker-love (the image will have no LÖVE app)" >&2
fi
NATIVE_BUILD="${R3I6NOS_NATIVE_BUILD:-$REPO/build/native}"
have_native=""
for deb in "$NATIVE_BUILD"/*.deb; do
    if [ -f "$deb" ]; then cp "$deb" "$DEFAULTS/native_deb/"; have_native=1; fi
done
if [ -z "$have_native" ]; then
    echo "[!] No native default app in $NATIVE_BUILD: run scripts/build_native_apps.sh (the image will have no native app)" >&2
fi
find "$DEFAULTS" -type d -exec chmod 755 {} + -o -type f -exec chmod 644 {} +

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
ln -sfn /etc/systemd/system/r3i6nos-bootlog.service \
    "$ROOTFS/etc/systemd/system/multi-user.target.wants/r3i6nos-bootlog.service"
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

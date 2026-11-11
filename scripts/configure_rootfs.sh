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
# The test corpus: one file of each format the apps read (corpus/README.md), always on the boot disk; flash-macos.sh also copies it to EASYROMS.
rm -rf "$ROOTFS/opt/loveos/test-files"
cp -a "$REPO/corpus/files" "$ROOTFS/opt/loveos/test-files"
cp "$REPO/corpus/README.md" "$ROOTFS/opt/loveos/test-files/README.md"
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
    echo "[!] No LÖVE default app in $LOVE_APPS_BUILD: run scripts/build_love_app.sh io-checker-love (the image will have no LÖVE app)" >&2
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

# The minimal ffmpeg (tools/ffmpeg-min/README.md, built by scripts/build-ffmpeg-min.sh): Commander's Play tab and Picture tab (video, WebP, TIFF, PSD) and
# Rec demo's H.264 + AAC encode find it as /usr/bin/ffmpeg. It is GPL-2.0-or-later (x264), so what that licence asks of whoever ships the binary
# ships with it: the licence text, NOTICE (the source's URLs and hashes) and the build script and README (the configure flags). Under
# /usr/share/ffmpeg-min, because the trim below deletes every other file in /usr/share/doc; only `copyright` stays there, pointing at it.
FFMPEG_BUILD="${R3I6NOS_FFMPEG_BUILD:-$REPO/build/ffmpeg-min}"
if [ -f "$FFMPEG_BUILD/ffmpeg" ]; then
    mkdir -p "$ROOTFS/usr/bin" "$ROOTFS/usr/share/ffmpeg-min" "$ROOTFS/usr/share/doc/ffmpeg-min"
    FFDOC="$ROOTFS/usr/share/ffmpeg-min"
    cp "$FFMPEG_BUILD/ffmpeg" "$ROOTFS/usr/bin/ffmpeg"
    chmod 755 "$ROOTFS/usr/bin/ffmpeg"
    cp "$REPO/NOTICE" "$REPO/tools/ffmpeg-min/build.sh" "$REPO/tools/ffmpeg-min/README.md" "$FFDOC/"
    chmod 644 "$FFDOC/NOTICE" "$FFDOC/build.sh" "$FFDOC/README.md"
    for gpl in "$ROOTFS/usr/share/common-licenses/GPL-2" /usr/share/common-licenses/GPL-2; do
        if [ -f "$gpl" ]; then cp "$gpl" "$FFDOC/GPL-2"; chmod 644 "$FFDOC/GPL-2"; break; fi
    done
    [ -f "$FFDOC/GPL-2" ] || echo "[!] No GPL-2 text found (the rootfs's base-files or this machine's): /usr/share/ffmpeg-min/GPL-2 is missing, and shipping ffmpeg needs it" >&2
    cat > "$ROOTFS/usr/share/doc/ffmpeg-min/copyright" <<'EOF'
/usr/bin/ffmpeg is FFmpeg 5.1.6 built with x264 by tools/ffmpeg-min/build.sh of https://github.com/joashchee/r3i6nos.
Licence: GNU General Public License, version 2 or (at your option) any later version (the whole binary: x264 is GPL-2.0-or-later).
The licence text, the exact source (URLs and SHA-256 of both tarballs) and the build script with its configure flags are in /usr/share/ffmpeg-min/
(GPL-2, NOTICE, build.sh, README.md). No patches were applied to either source.
EOF
    chmod 644 "$ROOTFS/usr/share/doc/ffmpeg-min/copyright"
else
    echo "[!] No minimal ffmpeg in $FFMPEG_BUILD: run scripts/build-ffmpeg-min.sh (the image will have no ffmpeg: Commander shows only Info for video, WebP, TIFF and PSD, and Rec demo cannot make its .mp4)" >&2
fi

# Mount points and places the boot scripts write to.
mkdir -p "$ROOTFS"/{roms,roms2,boot,opt/love,opt/native_apps,opt/launchers/native}

echo "[*] System settings..."
# Settings > About shows which build this is, and asks GitHub whether it is the newest (only when a person chooses it).
# VERSION is the release this build belongs to; the commit says exactly what it was built from. R3I6NOS_COMMIT overrides
# it for a tree without git; safe.directory lets root (CI's sudo) read a checkout another user owns. sudo drops GITHUB_ACTIONS,
# so CI says who built it with R3I6NOS_BUILT_BY (build.yml); the first report from a unit showed a CI image as "local".
git_() { git -c safe.directory="$REPO" -C "$REPO" "$@" 2>/dev/null; }
commit="${R3I6NOS_COMMIT:-$(git_ rev-parse HEAD || echo unknown)}"
modified=0
if [ -z "${R3I6NOS_COMMIT:-}" ] && [ -n "$(git_ status --porcelain --untracked-files=no)" ]; then modified=1; fi
cat > "$ROOTFS/etc/r3i6nos-release" <<EOF
VERSION=$(tr -d '[:space:]' < "$REPO/VERSION")
COMMIT=$commit
COMMIT_DATE=$(git_ show -s --format=%cs "$commit" || echo unknown)
MODIFIED=$modified
BUILT=$(date -u +%Y-%m-%d)
BUILT_BY=${R3I6NOS_BUILT_BY:-$([ -n "${GITHUB_ACTIONS:-}" ] && echo github || echo local)}
REPO=joashchee/r3i6nos
EOF
echo "r3i6nos" > "$ROOTFS/etc/hostname"
cat > "$ROOTFS/etc/hosts" <<'EOF'
127.0.0.1 localhost
127.0.1.1 r3i6nos
EOF

# Labels match assemble_image.sh. The games cards are not here: r3i6nos-cards.service mounts them by role (docs/STORAGE.md),
# the system card's partition 3 at /roms and a card in the second slot at /roms2, because both may be labelled EASYROMS.
cat > "$ROOTFS/etc/fstab" <<'EOF'
LABEL=ROOTFS   /      ext4  defaults,noatime,errors=remount-ro  0 1
LABEL=BOOT     /boot  vfat  defaults,noatime,nofail             0 2
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
ln -sfn /etc/systemd/system/r3i6nos-cards.service \
    "$ROOTFS/etc/systemd/system/multi-user.target.wants/r3i6nos-cards.service"
# The RK817 codec's outputs stay off until its "Playback Path" is set (r3i6nos-sound): without this no app is heard.
ln -sfn /etc/systemd/system/r3i6nos-sound.service \
    "$ROOTFS/etc/systemd/system/multi-user.target.wants/r3i6nos-sound.service"
# Hot-plug toasts (docs/HOTPLUG.md): the jack, USB devices and the DC port, as lines in /run/r3i6nos-hotplug/events for the top of the display.
ln -sfn /etc/systemd/system/r3i6nos-hotplug.service \
    "$ROOTFS/etc/systemd/system/multi-user.target.wants/r3i6nos-hotplug.service"
# The system's own device report (docs/STORAGE.md), listed in Settings > Device reports with the tools' reports.
mkdir -p "$ROOTFS/opt/loveos/bin"
ln -sfn /usr/local/bin/r3i6nos-report "$ROOTFS/opt/loveos/bin/r3i6nos-report"
# The clock (docs/CLOCK.md): restored at boot and saved at shutdown and hourly; the menu's Date and time requests are set by a root helper.
mkdir -p "$ROOTFS/etc/systemd/system/sysinit.target.wants" "$ROOTFS/etc/systemd/system/timers.target.wants"
ln -sfn /etc/systemd/system/r3i6nos-clock.service \
    "$ROOTFS/etc/systemd/system/sysinit.target.wants/r3i6nos-clock.service"
ln -sfn /etc/systemd/system/r3i6nos-clock-save.timer \
    "$ROOTFS/etc/systemd/system/timers.target.wants/r3i6nos-clock-save.timer"
# how the boot went, for the device report (systemctl answers only root: no D-Bus in the image)
ln -sfn /etc/systemd/system/r3i6nos-bootstate.timer \
    "$ROOTFS/etc/systemd/system/timers.target.wants/r3i6nos-bootstate.timer"
ln -sfn /etc/systemd/system/r3i6nos-clock-set.path \
    "$ROOTFS/etc/systemd/system/multi-user.target.wants/r3i6nos-clock-set.path"
# Settings > Network and SSH: a path unit that only watches for the menu's request (the helper runs per request, never at boot).
ln -sfn /etc/systemd/system/r3i6nos-net.path \
    "$ROOTFS/etc/systemd/system/multi-user.target.wants/r3i6nos-net.path"
# SSH is off until a person starts it in Settings, and every card makes its own host keys then (not the build's, which every card would share).
rm -f "$ROOTFS"/etc/systemd/system/multi-user.target.wants/{ssh,sshd}.service "$ROOTFS"/etc/systemd/system/sshd.service \
    "$ROOTFS"/etc/systemd/system/sockets.target.wants/ssh.socket "$ROOTFS"/etc/ssh/ssh_host_*
mkdir -p "$ROOTFS/etc/ssh/sshd_config.d"
# The wpasupplicant package enables its own D-Bus wpa_supplicant.service. The image has no D-Bus daemon, so it failed at
# every boot ("Failed to start wpa_supplicant"), and Wi-Fi at boot breaks rule 6 anyway: r3i6nos-net starts
# `wpa_supplicant -B` itself, only when a person joins a network. Unwire it and mask it so nothing pulls it in.
rm -f "$ROOTFS"/etc/systemd/system/multi-user.target.wants/wpa_supplicant.service \
    "$ROOTFS"/etc/systemd/system/dbus-fi.w1.wpa_supplicant1.service
ln -sfn /dev/null "$ROOTFS/etc/systemd/system/wpa_supplicant.service"
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

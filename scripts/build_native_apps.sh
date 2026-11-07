#!/bin/bash
# build_native_apps.sh
#
# Cross-compiles the native default app (apps/input-checker-native, C and SDL2) for arm64 Debian
# Bookworm and packs it as a .deb: build/native/Input_Checker_Native.deb. configure_rootfs.sh puts
# that into the image at /opt/loveos/defaults/native_deb, where deb_watcher.py installs it at boot.
#
# Run as root in a Debian 12 container, like build_love.sh, so the binary links against
# Bookworm's SDL2 and glibc, the ones the device has:
#
#   docker run --rm -v "$PWD":/src -w /src debian:bookworm scripts/build_native_apps.sh

set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$REPO/build/native"
NAME=Input_Checker_Native            # deb_watcher names the app and its launcher after the file
BIN=input-checker-native
VERSION="${R3I6NOS_NATIVE_VERSION:-1.0}"
HOST=aarch64-linux-gnu

if [ "$(id -u)" -ne 0 ]; then
    echo "build_native_apps: run as root (it installs packages)" >&2
    exit 1
fi
if ! grep -q '^12' /etc/debian_version 2>/dev/null; then
    echo "build_native_apps: needs Debian 12 (Bookworm); see the header for a docker command" >&2
    exit 1
fi

export DEBIAN_FRONTEND=noninteractive
dpkg --add-architecture arm64
apt-get update
apt-get install -y --no-install-recommends \
    ca-certificates pkg-config dpkg-dev file crossbuild-essential-arm64 libsdl2-dev:arm64

# pkg-config must see the arm64 SDL2, not the build machine's.
export PKG_CONFIG_LIBDIR="/usr/lib/$HOST/pkgconfig:/usr/share/pkgconfig"
export PKG_CONFIG_PATH=

rm -rf "$OUT"
mkdir -p "$OUT/pkg/DEBIAN" "$OUT/pkg/usr/games" "$OUT/pkg/usr/share/doc/$BIN"

echo "[*] Compiling $BIN (arm64)..."
# shellcheck disable=SC2046  # pkg-config prints separate flags
"$HOST-gcc" -std=gnu11 -Wall -Wextra -O2 -o "$OUT/pkg/usr/games/$BIN" \
    "$REPO/apps/input-checker-native/input_checker.c" $(pkg-config --cflags --libs sdl2) -lm
"$HOST-strip" "$OUT/pkg/usr/games/$BIN"

file "$OUT/pkg/usr/games/$BIN" | grep -q 'ARM aarch64' || { echo "build_native_apps: not an arm64 binary" >&2; exit 1; }
"$HOST-readelf" -d "$OUT/pkg/usr/games/$BIN" | grep -q 'libSDL2-2.0.so.0' || { echo "build_native_apps: does not link SDL2" >&2; exit 1; }

cp "$REPO/LICENSE" "$OUT/pkg/usr/share/doc/$BIN/copyright"
size_kb="$(du -sk "$OUT/pkg/usr" | cut -f1)"
cat > "$OUT/pkg/DEBIAN/control" <<CONTROL
Package: r36s-input-checker-native
Version: $VERSION
Architecture: arm64
Maintainer: r3i6nos <noreply@users.noreply.github.com>
Installed-Size: $size_kb
Depends: libsdl2-2.0-0
Section: games
Priority: optional
Homepage: https://github.com/joashchee/r3i6nos
Description: R36S input checker (native ARM64, SDL2)
 Shows every button, axis and the D-pad of the handheld's gamepad as SDL reports them, so
 the real layout of a unit can be read off the screen. Built into r3i6nos as a default app.
CONTROL

echo "[*] Packing $NAME.deb..."
find "$OUT/pkg" -exec touch -h -d '2026-01-01 00:00:00 UTC' {} +      # same bytes for the same source
dpkg-deb --root-owner-group -Zxz --build "$OUT/pkg" "$OUT/$NAME.deb"
dpkg-deb -I "$OUT/$NAME.deb" | sed -n 1,12p
rm -rf "$OUT/pkg"
echo "[*] Done: $OUT/$NAME.deb ($(du -k "$OUT/$NAME.deb" | cut -f1) KiB)"

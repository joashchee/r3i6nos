#!/bin/bash
# build_native_app.sh <app-dir> [--smoke]
#
# Cross-compiles ONE native app (C and SDL2) from apps/<app-dir>/ for arm64 Debian Bookworm and packs
# build/native/<NAME>.deb, as described by that folder's app.conf. The firmware build does not own this:
# each app has its own workflow (.github/workflows/native-app.yml, called by app-<name>.yml) and the firmware
# build only picks up the .deb files of the apps marked DEFAULT=1 (scripts/build_native_apps.sh).
#
# --smoke then runs `--selftest` on the packed binary under qemu-user with SDL's dummy video driver, against
# this container's arm64 libraries (the same Bookworm the device has). It proves the binary runs and links; it
# does NOT prove it draws on the panel or sees the pad (needs the device).
#
# Run as root in a Debian 12 container, so the binary links against Bookworm's SDL2 and glibc:
#
#   docker run --rm -v "$PWD":/src -w /src debian:bookworm scripts/build_native_app.sh commander-native --smoke
#
# R3I6NOS_NATIVE_VERSION sets the package version (default 1.0).

set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${R3I6NOS_NATIVE_OUT:-$REPO/build/native}"
VERSION="${R3I6NOS_NATIVE_VERSION:-1.0}"
HOST=aarch64-linux-gnu

APP_DIR="${1:?Usage: $0 <app-dir under apps/> [--smoke]}"
SMOKE=""
[ "${2:-}" = "--smoke" ] && SMOKE=1
CONF="$REPO/apps/$APP_DIR/app.conf"
[ -f "$CONF" ] || { echo "build_native_app: no $CONF" >&2; exit 1; }

TYPE=native NAME="" BIN="" PKG="" SRC="" THEME=0 DESC=""
# shellcheck disable=SC1090  # the app's own manifest
. "$CONF"
[ "$TYPE" = native ] || { echo "build_native_app: $CONF is not TYPE=native (a LÖVE app: scripts/build_love_app.sh)" >&2; exit 1; }
for v in NAME BIN PKG SRC DESC; do
    [ -n "${!v}" ] || { echo "build_native_app: $CONF does not set $v" >&2; exit 1; }
done
[ -f "$REPO/apps/$APP_DIR/$SRC" ] || { echo "build_native_app: no apps/$APP_DIR/$SRC" >&2; exit 1; }

if [ "$(id -u)" -ne 0 ]; then
    echo "build_native_app: run as root (it installs packages)" >&2
    exit 1
fi
if ! grep -q '^12' /etc/debian_version 2>/dev/null; then
    echo "build_native_app: needs Debian 12 (Bookworm); see the header for a docker command" >&2
    exit 1
fi

export DEBIAN_FRONTEND=noninteractive
dpkg --add-architecture arm64
apt-get update
pkgs="ca-certificates pkg-config dpkg-dev file crossbuild-essential-arm64 libsdl2-dev:arm64"
[ -n "$SMOKE" ] && pkgs="$pkgs qemu-user-static"
# shellcheck disable=SC2086  # a list of package names
apt-get install -y --no-install-recommends $pkgs

# pkg-config must see the arm64 SDL2, not the build machine's.
export PKG_CONFIG_LIBDIR="/usr/lib/$HOST/pkgconfig:/usr/share/pkgconfig"
export PKG_CONFIG_PATH=

mkdir -p "$OUT"
rm -f "$OUT/$NAME.deb"
pkgdir="$(mktemp -d)"
mkdir -p "$pkgdir/DEBIAN" "$pkgdir/usr/games" "$pkgdir/usr/share/doc/$BIN"

extra=()
[ "$THEME" = 1 ] && extra=(-I "$REPO/theme")

echo "[*] Compiling $BIN (arm64)..."
# shellcheck disable=SC2046  # pkg-config prints separate flags
"$HOST-gcc" -std=gnu11 -Wall -Wextra -O2 ${extra[@]+"${extra[@]}"} -o "$pkgdir/usr/games/$BIN" "$REPO/apps/$APP_DIR/$SRC" $(pkg-config --cflags --libs sdl2) -lm
"$HOST-strip" "$pkgdir/usr/games/$BIN"

file "$pkgdir/usr/games/$BIN" | grep -q 'ARM aarch64' || { echo "build_native_app: $BIN is not an arm64 binary" >&2; exit 1; }
"$HOST-readelf" -d "$pkgdir/usr/games/$BIN" | grep -q 'libSDL2-2.0.so.0' || { echo "build_native_app: $BIN does not link SDL2" >&2; exit 1; }

cp "$REPO/LICENSE" "$pkgdir/usr/share/doc/$BIN/copyright"
if [ "$THEME" = 1 ]; then
    # The theme's font is CC BY-SA 4.0 and is compiled in: its credit and licence travel with the binary.
    cp "$REPO/NOTICE" "$pkgdir/usr/share/doc/$BIN/NOTICE"
    cp "$REPO/theme/font/LICENSE.TXT" "$REPO/theme/font/README.txt" "$pkgdir/usr/share/doc/$BIN/"
fi
size_kb="$(du -sk "$pkgdir/usr" | cut -f1)"
cat > "$pkgdir/DEBIAN/control" <<CONTROL
Package: $PKG
Version: $VERSION
Architecture: arm64
Maintainer: r3i6nos <noreply@users.noreply.github.com>
Installed-Size: $size_kb
Depends: libsdl2-2.0-0
Section: games
Priority: optional
Homepage: https://github.com/joashchee/r3i6nos
Description: $DESC
CONTROL

echo "[*] Packing $NAME.deb..."
find "$pkgdir" -exec touch -h -d '2026-01-01 00:00:00 UTC' {} +      # same bytes for the same source
dpkg-deb --root-owner-group -Zxz --build "$pkgdir" "$OUT/$NAME.deb"
dpkg-deb -I "$OUT/$NAME.deb" | sed -n 1,12p
rm -rf "$pkgdir"
echo "[*] Done: $OUT/$NAME.deb ($(du -k "$OUT/$NAME.deb" | cut -f1) KiB)"

if [ -n "$SMOKE" ]; then
    "$REPO/scripts/smoke_native.sh" / "$OUT/$NAME.deb" "$BIN"
fi

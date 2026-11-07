#!/bin/bash
# build_native_apps.sh
#
# Cross-compiles the native default apps (C and SDL2) for arm64 Debian Bookworm and packs each as a .deb:
#   build/native/Input_Checker_Native.deb   apps/input-checker-native
#   build/native/Commander.deb              apps/commander-native (the file manager; uses theme/)
# configure_rootfs.sh puts them into the image at /opt/loveos/defaults/native_deb, where deb_watcher.py
# installs them at boot. The deb's file name is the app's name in the menu.
#
# Run as root in a Debian 12 container, like build_love.sh, so the binary links against
# Bookworm's SDL2 and glibc, the ones the device has:
#
#   docker run --rm -v "$PWD":/src -w /src debian:bookworm scripts/build_native_apps.sh

set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$REPO/build/native"
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
mkdir -p "$OUT"

# build_app <DebName> <binary> <package> <source.c> <description> [extra cc flags...]
build_app() {
    local name="$1" bin="$2" pkg="$3" src="$4" desc="$5"
    shift 5
    local pkgdir="$OUT/pkg-$bin"
    mkdir -p "$pkgdir/DEBIAN" "$pkgdir/usr/games" "$pkgdir/usr/share/doc/$bin"

    echo "[*] Compiling $bin (arm64)..."
    # shellcheck disable=SC2046  # pkg-config prints separate flags
    "$HOST-gcc" -std=gnu11 -Wall -Wextra -O2 "$@" -o "$pkgdir/usr/games/$bin" "$REPO/$src" $(pkg-config --cflags --libs sdl2) -lm
    "$HOST-strip" "$pkgdir/usr/games/$bin"

    file "$pkgdir/usr/games/$bin" | grep -q 'ARM aarch64' || { echo "build_native_apps: $bin is not an arm64 binary" >&2; exit 1; }
    "$HOST-readelf" -d "$pkgdir/usr/games/$bin" | grep -q 'libSDL2-2.0.so.0' || { echo "build_native_apps: $bin does not link SDL2" >&2; exit 1; }

    cp "$REPO/LICENSE" "$pkgdir/usr/share/doc/$bin/copyright"
    case " $* " in
        *" -I $REPO/theme "*)
            # The theme's font is CC BY-SA 4.0 and is compiled in: its credit and licence travel with the binary.
            cp "$REPO/NOTICE" "$pkgdir/usr/share/doc/$bin/NOTICE"
            cp "$REPO/theme/font/LICENSE.TXT" "$REPO/theme/font/README.txt" "$pkgdir/usr/share/doc/$bin/"
            ;;
    esac
    local size_kb
    size_kb="$(du -sk "$pkgdir/usr" | cut -f1)"
    cat > "$pkgdir/DEBIAN/control" <<CONTROL
Package: $pkg
Version: $VERSION
Architecture: arm64
Maintainer: r3i6nos <noreply@users.noreply.github.com>
Installed-Size: $size_kb
Depends: libsdl2-2.0-0
Section: games
Priority: optional
Homepage: https://github.com/joashchee/r3i6nos
Description: $desc
CONTROL

    echo "[*] Packing $name.deb..."
    find "$pkgdir" -exec touch -h -d '2026-01-01 00:00:00 UTC' {} +      # same bytes for the same source
    dpkg-deb --root-owner-group -Zxz --build "$pkgdir" "$OUT/$name.deb"
    dpkg-deb -I "$OUT/$name.deb" | sed -n 1,12p
    rm -rf "$pkgdir"
    echo "[*] Done: $OUT/$name.deb ($(du -k "$OUT/$name.deb" | cut -f1) KiB)"
}

build_app Input_Checker_Native input-checker-native r36s-input-checker-native \
    apps/input-checker-native/input_checker.c \
    "R36S input checker (native ARM64, SDL2)
 Shows every button, axis and the D-pad of the handheld's gamepad as SDL reports them, so
 the real layout of a unit can be read off the screen. Built into r3i6nos as a default app."

build_app Commander commander r36s-commander \
    apps/commander-native/commander.c \
    "Commander: a two-panel file manager (native ARM64, SDL2)
 Browse, view, copy, move, rename, make folders and delete, in the r3i6nos-ansiapps text-mode
 theme, with the R36S's buttons. Built into r3i6nos as a default app." \
    -I "$REPO/theme"

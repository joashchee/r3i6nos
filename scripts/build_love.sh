#!/bin/bash
# build_love.sh <version>
#
# Cross-compiles a LÖVE runtime for arm64 Debian Bookworm (the rootfs's
# release), into build/love/<version>/ (bin/love, lib/...), ready to be
# copied to /opt/love/<version>/ on the device.
#
# Run as root inside a Debian 12 container or host, so the arm64 libraries
# are Bookworm's and the binary links against the same glibc and libstdc++
# the device has. (Building on Ubuntu would need a newer glibc than Bookworm's.)
#
#   docker run --rm -v "$PWD":/src -w /src debian:bookworm scripts/build_love.sh 11.5
#
# Versions: 11.5 (release tarball, pinned by sha256). 12.0 is a nightly and
# is not wired up yet: it needs a pinned commit (ROADMAP phase 3).

set -eu

VERSION="${1:?Usage: $0 <version>   (11.5)}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
HOST=aarch64-linux-gnu
OUT="$REPO/build/love/$VERSION"

case "$VERSION" in
    11.5)
        URL=https://github.com/love2d/love/releases/download/11.5/love-11.5-linux-src.tar.gz
        SHA256=066e0843f71aa9fd28b8eaf27d41abb74bfaef7556153ac2e3cf08eafc874c39
        ;;
    12.0)
        echo "build_love: 12.0 is a nightly and needs a pinned commit; not wired up yet" >&2
        exit 2
        ;;
    *)
        echo "build_love: unknown version: $VERSION" >&2
        exit 2
        ;;
esac

if [ "$(id -u)" -ne 0 ]; then
    echo "build_love: run as root (it installs packages)" >&2
    exit 1
fi
if ! grep -q '^12' /etc/debian_version 2>/dev/null; then
    echo "build_love: needs Debian 12 (Bookworm); see the header for a docker command" >&2
    exit 1
fi
if [ -e "$OUT" ]; then
    echo "build_love: $OUT exists; remove it first" >&2
    exit 1
fi

export DEBIAN_FRONTEND=noninteractive
dpkg --add-architecture arm64
apt-get update
apt-get install -y --no-install-recommends \
    ca-certificates curl make pkg-config crossbuild-essential-arm64 \
    libsdl2-dev:arm64 libopenal-dev:arm64 libluajit-5.1-dev:arm64 \
    libfreetype-dev:arm64 libmodplug-dev:arm64 libvorbis-dev:arm64 \
    libogg-dev:arm64 libtheora-dev:arm64 libmpg123-dev:arm64 zlib1g-dev:arm64

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

echo "[*] Fetching LÖVE $VERSION source..."
curl -fsSL -o src.tar.gz "$URL"
echo "$SHA256  src.tar.gz" | sha256sum -c -
tar -xzf src.tar.gz
cd "love-$VERSION"

# pkg-config must see the arm64 libraries, not the build machine's.
export PKG_CONFIG_LIBDIR="/usr/lib/$HOST/pkgconfig:/usr/share/pkgconfig"
unset PKG_CONFIG_PATH

echo "[*] Configuring (arm64, prefix /opt/love/$VERSION)..."
./configure --host="$HOST" --prefix="/opt/love/$VERSION" \
    --with-lua=luajit --disable-gme

echo "[*] Building..."
make -j"$(nproc)"

echo "[*] Installing to a staging directory..."
STAGE="$WORK/stage"
make install DESTDIR="$STAGE"

mkdir -p "$(dirname "$OUT")"
cp -a "$STAGE/opt/love/$VERSION" "$OUT"
rm -rf "$OUT/share"           # man pages, desktop files, icons: not used on the device
"$HOST-strip" "$OUT/bin/love" || true

echo "[*] Checking the result..."
test -x "$OUT/bin/love"
machine="$("$HOST-readelf" -h "$OUT/bin/love" | sed -n 's/^ *Machine: *//p')"
case "$machine" in
    *AArch64*) ;;
    *) echo "build_love: bin/love is '$machine', not AArch64" >&2; exit 1 ;;
esac
echo "bin/love: $machine; needs:"
"$HOST-readelf" -d "$OUT/bin/love" | sed -n 's/.*NEEDED.*\[\(.*\)\]/  \1/p'
du -sh "$OUT"
echo "[*] Done: $OUT"

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
# 11.5  the release tarball (autotools), against Bookworm's SDL2.
# 12.0  a pinned nightly commit (CMake). It needs SDL3, which Bookworm does not
#       ship, so SDL3 is built from a pinned release and its libraries are
#       copied into the runtime's own lib/ (found through RUNPATH $ORIGIN).
#
# Every download is checked against a sha256 below. To move a pin, change the
# URL and the hash together and say why in the CHANGELOG.

set -eu

VERSION="${1:?Usage: $0 <version>   (11.5 | 12.0)}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
HOST=aarch64-linux-gnu
OUT="$REPO/build/love/$VERSION"

# --- pins ---------------------------------------------------------------
LOVE115_URL=https://github.com/love2d/love/releases/download/11.5/love-11.5-linux-src.tar.gz
LOVE115_SHA256=066e0843f71aa9fd28b8eaf27d41abb74bfaef7556153ac2e3cf08eafc874c39

# love2d/love main on 2026-11-05 (merge of PR #2374). GitHub archive tarballs
# are generated on demand; if the hash ever stops matching, re-check the
# commit and re-pin rather than skipping the check.
LOVE120_COMMIT=def245102c67909f9902095af613b1ae05249b62
LOVE120_URL="https://github.com/love2d/love/archive/$LOVE120_COMMIT.tar.gz"
LOVE120_SHA256=26a1d9d5fa6f693c455fd0bde89ff6b6fd60f0cdb57bf7b8583d9a6f5d6140eb

SDL3_URL=https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-3.4.20.tar.gz
SDL3_SHA256=f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f
# ------------------------------------------------------------------------

case "$VERSION" in
    11.5|12.0) ;;
    *) echo "build_love: unknown version: $VERSION" >&2; exit 2 ;;
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

# Libraries both versions use (arm64 development packages).
DEV_COMMON=(
    libopenal-dev libluajit-5.1-dev libfreetype-dev libmodplug-dev
    libvorbis-dev libogg-dev libtheora-dev zlib1g-dev
)
case "$VERSION" in
    11.5) DEV=("${DEV_COMMON[@]}" libsdl2-dev libmpg123-dev); TOOLS=(make) ;;
    12.0) DEV=("${DEV_COMMON[@]}" libharfbuzz-dev
          # SDL3's Linux backends: KMS/DRM video, ALSA audio, udev input.
          libdrm-dev libgbm-dev libegl-dev libgles-dev libasound2-dev libudev-dev)
          TOOLS=(cmake ninja-build) ;;
esac

export DEBIAN_FRONTEND=noninteractive
dpkg --add-architecture arm64
apt-get update
apt-get install -y --no-install-recommends \
    ca-certificates curl pkg-config crossbuild-essential-arm64 "${TOOLS[@]}" \
    "${DEV[@]/%/:arm64}"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

fetch() {  # fetch <url> <sha256> <file>
    echo "[*] Fetching $3..."
    curl -fsSL -o "$3" "$1"
    echo "$2  $3" | sha256sum -c -
}

# pkg-config must see the arm64 libraries, not the build machine's.
export PKG_CONFIG_LIBDIR="/usr/lib/$HOST/pkgconfig:/usr/share/pkgconfig"
unset PKG_CONFIG_PATH

STAGE="$WORK/stage"

build_115() {
    fetch "$LOVE115_URL" "$LOVE115_SHA256" src.tar.gz
    tar -xzf src.tar.gz
    cd love-11.5

    echo "[*] Configuring (arm64, prefix /opt/love/11.5)..."
    ./configure --host="$HOST" --prefix=/opt/love/11.5 --with-lua=luajit --disable-gme
    echo "[*] Building..."
    make -j"$(nproc)"
    make install DESTDIR="$STAGE"
}

build_120() {
    # --- SDL3 (not in Bookworm) ---
    fetch "$SDL3_URL" "$SDL3_SHA256" sdl3.tar.gz
    tar -xzf sdl3.tar.gz
    cat > "$WORK/toolchain.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER $HOST-gcc)
set(CMAKE_CXX_COMPILER $HOST-g++)
set(CMAKE_LIBRARY_ARCHITECTURE $HOST)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
EOF

    echo "[*] Building SDL3 (KMS/DRM, ALSA; no X11, Wayland, PulseAudio)..."
    cmake -S SDL3-3.4.20 -B sdl3-build -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$WORK/toolchain.cmake" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$WORK/sdl3" \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
        -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF \
        -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DSDL_VULKAN=OFF \
        -DSDL_KMSDRM=ON -DSDL_ALSA=ON
    cmake --build sdl3-build
    cmake --install sdl3-build

    # --- LÖVE ---
    fetch "$LOVE120_URL" "$LOVE120_SHA256" love.tar.gz
    tar -xzf love.tar.gz
    echo "[*] Building LÖVE 12.0 ($LOVE120_COMMIT)..."
    cmake -S "love-$LOVE120_COMMIT" -B love-build -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$WORK/toolchain.cmake" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/love/12.0 \
        -DSDL3_DIR="$WORK/sdl3/lib/cmake/SDL3" \
        -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib:$ORIGIN' \
        -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
        -DLOVE_JIT=ON
    cmake --build love-build
    DESTDIR="$STAGE" cmake --install love-build

    # SDL3 travels with the runtime, beside liblove, where RUNPATH finds it.
    mkdir -p "$STAGE/opt/love/12.0/lib"
    cp -a "$WORK"/sdl3/lib/libSDL3.so* "$STAGE/opt/love/12.0/lib/"
}

case "$VERSION" in
    11.5) build_115 ;;
    12.0) build_120 ;;
esac

mkdir -p "$(dirname "$OUT")"
cp -a "$STAGE/opt/love/$VERSION" "$OUT"
rm -rf "$OUT/share"           # man pages, desktop files, icons: not used on the device
"$HOST-strip" "$OUT/bin/love" || true
find "$OUT/lib" -name '*.so*' -type f -exec "$HOST-strip" --strip-unneeded {} + 2>/dev/null || true

echo "[*] Checking the result..."
test -x "$OUT/bin/love"
machine="$("$HOST-readelf" -h "$OUT/bin/love" | sed -n 's/^ *Machine: *//p')"
case "$machine" in
    *AArch64*) ;;
    *) echo "build_love: bin/love is '$machine', not AArch64" >&2; exit 1 ;;
esac
echo "bin/love: $machine; needs:"
"$HOST-readelf" -d "$OUT/bin/love" | sed -n 's/.*\(NEEDED\|RUNPATH\|RPATH\).*\[\(.*\)\]/  \1 \2/p'
if [ "$VERSION" = 12.0 ]; then
    ls "$OUT/lib" | sed 's/^/  lib\//'
    test -e "$OUT"/lib/libSDL3.so.0 || { echo "build_love: SDL3 missing from lib/" >&2; exit 1; }
fi
du -sh "$OUT"
echo "[*] Done: $OUT"

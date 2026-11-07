#!/bin/bash
# build_port.sh <app-dir> [--validate | --prepare-only DIR | --smoke]
#
# Builds ONE port: an open-source program (or game engine) from upstream source, cross-compiled for the R36S
# (arm64 Debian Bookworm) and packed as a .deb in build/ports/<NAME>.deb, as described by apps/<app-dir>/app.conf
# (TYPE=port) and its build.sh. Ports are never part of the firmware image: a user drops the .deb in native_deb/
# on the games card, and deb_watcher.py extracts it at boot. That does NOT run dpkg, so a .deb's Depends are not
# installed: a port may only link libraries the rootfs already has (RUNTIME_PKGS, checked against
# build_rootfs.sh's PACKAGES), or bundle them, or link statically.
#
#   --validate         check the manifest only (no network, no root, runs anywhere; check.sh does this)
#   --prepare-only DIR download the pinned source, verify its sha256, unpack and apply patches/*.patch into DIR/src
#                      (no root, runs anywhere; the tests use it)
#   --smoke            after packing, run the binary with SMOKE_ARGS under qemu-user (dummy video/audio)
#
# A full build runs as root in a Debian 12 container, like build_native_app.sh:
#
#   docker run --rm -v "$PWD":/src -w /src debian:bookworm scripts/build_port.sh <app-dir> --smoke
#
# What a manifest must say (apps/_port-template/ has a commented copy): the pinned source URL and its sha256, the
# SPDX licence and which files carry it, what game data the port needs (none, free, or user-supplied: never shipped
# here, CLAUDE.md rule 2), the build and runtime packages. Source is never vendored in the repo, and only public
# https sources are fetched (no credentials). Not legal advice: the deb carries the upstream licence files, the exact
# source URL and hash, and the patches, which is what the common licences ask for with a binary; check each port's terms.
#
# Env: R3I6NOS_PORTS_OUT (output folder), R3I6NOS_APPS_DIR (where app folders are; tests).

set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"
APPS="${R3I6NOS_APPS_DIR:-$REPO/apps}"
OUT="${R3I6NOS_PORTS_OUT:-$REPO/build/ports}"
HOST=aarch64-linux-gnu

die() { echo "build_port: $*" >&2; exit 1; }

APP_DIR="${1:?Usage: $0 <app-dir under apps/> [--validate | --prepare-only DIR | --smoke]}"
shift
MODE=build; PREP_DIR=""; SMOKE=""
while [ $# -gt 0 ]; do
    case "$1" in
        --validate) MODE=validate ;;
        --prepare-only) MODE=prepare; PREP_DIR="${2:?--prepare-only needs a folder}"; shift ;;
        --smoke) SMOKE=1 ;;
        *) die "unknown option: $1" ;;
    esac
    shift
done

CONF="$APPS/$APP_DIR/app.conf"
[ -f "$CONF" ] || die "no $CONF"

TYPE="" NAME="" BIN="" PKG="" VERSION="" SOURCE_URL="" SOURCE_SHA256="" SOURCE_STRIP=1 LICENSE=""
LICENSE_FILES="" DATA="" BUILD_TOOLS="" BUILD_PKGS="" RUNTIME_PKGS="" SMOKE_ARGS="" DESC=""
# shellcheck disable=SC1090  # the port's own manifest
. "$CONF"

# ---- validate the manifest
[ "$TYPE" = port ] || die "$CONF is not TYPE=port"
for v in NAME BIN PKG VERSION SOURCE_URL SOURCE_SHA256 LICENSE LICENSE_FILES DATA DESC; do
    [ -n "${!v}" ] || die "$CONF does not set $v"
done
[[ "$NAME" =~ ^[A-Za-z0-9_.-]+$ ]] || die "NAME may only have letters, digits, _ . -"
[[ "$BIN" =~ ^[a-z0-9][a-z0-9.+-]*$ ]] || die "BIN must be a lower-case file name"
[[ "$PKG" =~ ^[a-z0-9][a-z0-9.+-]+$ ]] || die "PKG must be a Debian package name"
[[ "$VERSION" =~ ^[0-9][A-Za-z0-9.+~-]*$ ]] || die "VERSION must start with a digit"
[[ "$SOURCE_URL" =~ ^(https://|file://) ]] || die "SOURCE_URL must be https:// (file:// is for the tests)"
[[ "$SOURCE_SHA256" =~ ^[0-9a-f]{64}$ ]] || die "SOURCE_SHA256 must be 64 lower-case hex digits (the pin: unpinned source is never fetched)"
[[ "$SOURCE_STRIP" =~ ^[0-9]+$ ]] || die "SOURCE_STRIP must be a number"
case "$DATA" in
    none|free|user) ;;
    *) die "DATA must be none (no game data), free (freely licensed data you ship) or user (the player supplies it; nothing proprietary is shipped)" ;;
esac
# Package lists go straight onto apt's command line: names only.
for p in $BUILD_TOOLS $BUILD_PKGS $RUNTIME_PKGS; do
    [[ "$p" =~ ^[a-z0-9][a-z0-9.+-]*(:[a-z0-9]+)?$ ]] || die "not a package name: $p"
done
[ -f "$APPS/$APP_DIR/build.sh" ] || die "no apps/$APP_DIR/build.sh"
# deb_watcher runs no dpkg: every runtime package must already be in the rootfs, as a whole name in PACKAGES.
rootfs_pkgs="$(sed -n '/^PACKAGES=(/,/^)/p' "$REPO/scripts/build_rootfs.sh" | sed 's/#.*//' | tr -s ' \t' '\n')"
for p in $RUNTIME_PKGS; do
    if ! grep -qxF -- "$p" <<<"$rootfs_pkgs"; then
        die "runtime package $p is not in the rootfs (scripts/build_rootfs.sh PACKAGES): add it there (a firmware change), bundle it, or link statically"
    fi
done

if [ "$MODE" = validate ]; then
    echo "build_port: $APP_DIR manifest ok"
    exit 0
fi

# ---- fetch, verify, unpack, patch
prepare() {   # prepare <dest>: puts the patched source in <dest>/src
    local dest="$1" tarball got p
    mkdir -p "$dest/src"
    tarball="$dest/source.tar"
    echo "[*] Fetching $SOURCE_URL"
    curl -fsSL --retry 3 -o "$tarball" "$SOURCE_URL"
    if command -v sha256sum >/dev/null; then got="$(sha256sum "$tarball" | cut -d' ' -f1)"; else got="$(shasum -a 256 "$tarball" | cut -d' ' -f1)"; fi
    [ "$got" = "$SOURCE_SHA256" ] || die "source sha256 is $got, the manifest pins $SOURCE_SHA256"
    tar -xf "$tarball" -C "$dest/src" --strip-components="$SOURCE_STRIP"
    if [ -d "$APPS/$APP_DIR/patches" ]; then
        for p in "$APPS/$APP_DIR"/patches/*.patch; do
            [ -f "$p" ] || continue
            echo "[*] Patch $(basename "$p")"
            patch -p1 -d "$dest/src" < "$p" >/dev/null || die "patch $(basename "$p") does not apply"
        done
    fi
}

if [ "$MODE" = prepare ]; then
    prepare "$PREP_DIR"
    echo "[*] Source ready in $PREP_DIR/src"
    exit 0
fi

# ---- full build (root, Debian 12)
[ "$(id -u)" -eq 0 ] || die "run as root (it installs packages); see the header for a docker command"
grep -q '^12' /etc/debian_version 2>/dev/null || die "needs Debian 12 (Bookworm); see the header for a docker command"

export DEBIAN_FRONTEND=noninteractive
dpkg --add-architecture arm64
apt-get update
pkgs="ca-certificates curl patch file pkg-config dpkg-dev crossbuild-essential-arm64 libsdl2-dev:arm64 $BUILD_TOOLS $BUILD_PKGS"
if [ -n "$SMOKE" ]; then pkgs="$pkgs qemu-user-static"; fi
# shellcheck disable=SC2086  # a list of package names, checked above
apt-get install -y --no-install-recommends $pkgs

export PKG_CONFIG_LIBDIR="/usr/lib/$HOST/pkgconfig:/usr/share/pkgconfig"
export PKG_CONFIG_PATH=

work="$(mktemp -d)"
stage="$work/pkg"
mkdir -p "$stage/DEBIAN" "$stage/usr/games" "$stage/usr/share/doc/$BIN"
prepare "$work"

echo "[*] Building $NAME (arm64)..."
(
    cd "$work/src"
    JOBS="$(nproc)"
    export SRC="$work/src" STAGE="$stage" HOST BIN JOBS
    export CC="$HOST-gcc" CXX="$HOST-g++" AR="$HOST-ar" STRIP="$HOST-strip" PKG_CONFIG="pkg-config"
    bash "$APPS/$APP_DIR/build.sh"
)

[ -x "$stage/usr/games/$BIN" ] || die "build.sh did not install \$STAGE/usr/games/$BIN"
file "$stage/usr/games/$BIN" | grep -q 'ARM aarch64' || die "$BIN is not an arm64 binary"
"$HOST-strip" "$stage/usr/games/$BIN" 2>/dev/null || true

# Every shared library the binary needs must come from the rootfs (libc and friends, RUNTIME_PKGS) or be bundled.
base="libc6 libgcc-s1 libstdc++6"
for lib in $("$HOST-readelf" -d "$stage/usr/games/$BIN" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p'); do
    if [ -n "$(find "$stage" -name "$lib" 2>/dev/null)" ]; then continue; fi     # bundled
    path="$(find "/usr/lib/$HOST" "/lib/$HOST" -name "$lib" 2>/dev/null | head -1)"
    [ -n "$path" ] || die "$BIN needs $lib, which was not found"
    owner="$(dpkg -S "$(readlink -f "$path")" 2>/dev/null | head -1 | cut -d: -f1)"
    ok=""
    for p in $base $RUNTIME_PKGS; do if [ "$owner" = "$p" ]; then ok=1; fi; done
    [ -n "$ok" ] || die "$BIN needs $lib (package ${owner:-unknown}), which the rootfs does not have: list that package in RUNTIME_PKGS (and add it to build_rootfs.sh), bundle it, or link statically"
done

# Licence, source and patches travel with the binary.
doc="$stage/usr/share/doc/$BIN"
cp "$REPO/LICENSE" "$doc/copyright"          # the packaging's own licence (MIT); the program's is upstream-*
for f in $LICENSE_FILES; do
    [ -f "$work/src/$f" ] || die "LICENSE_FILES: $f is not in the source"
    cp "$work/src/$f" "$doc/upstream-$(basename "$f")"
done
{
    echo "$NAME $VERSION, built by r3i6nos from upstream source, unmodified except for the patches listed."
    echo "Licence: $LICENSE (the licence files are upstream-*)"
    echo "Source:  $SOURCE_URL"
    echo "sha256:  $SOURCE_SHA256"
    echo "Game data: $DATA"
    if [ -d "$APPS/$APP_DIR/patches" ]; then
        echo "Patches (in patches/):"
        for p in "$APPS/$APP_DIR"/patches/*.patch; do if [ -f "$p" ]; then echo "  $(basename "$p")"; fi; done
    fi
} > "$doc/SOURCE"
if [ -d "$APPS/$APP_DIR/patches" ]; then
    mkdir -p "$doc/patches"
    for p in "$APPS/$APP_DIR"/patches/*.patch; do if [ -f "$p" ]; then cp "$p" "$doc/patches/"; fi; done
fi

size_kb="$(du -sk "$stage/usr" | cut -f1)"
depends="${RUNTIME_PKGS// /, }"
{
    echo "Package: $PKG"
    echo "Version: $VERSION"
    echo "Architecture: arm64"
    echo "Maintainer: r3i6nos <noreply@users.noreply.github.com>"
    echo "Installed-Size: $size_kb"
    if [ -n "$depends" ]; then echo "Depends: $depends"; fi
    echo "Section: games"
    echo "Priority: optional"
    echo "Homepage: https://github.com/joashchee/r3i6nos"
    echo "Description: $DESC"
} > "$stage/DEBIAN/control"

mkdir -p "$OUT"
rm -f "$OUT/$NAME.deb"
find "$stage" -exec touch -h -d '2026-01-01 00:00:00 UTC' {} +
dpkg-deb --root-owner-group -Zxz --build "$stage" "$OUT/$NAME.deb"
dpkg-deb -I "$OUT/$NAME.deb" | sed -n 1,12p
echo "[*] Done: $OUT/$NAME.deb ($(du -k "$OUT/$NAME.deb" | cut -f1) KiB)"

if [ -n "$SMOKE" ]; then
    if [ -z "$SMOKE_ARGS" ]; then
        echo "[!] $NAME sets no SMOKE_ARGS: only the link check ran, the binary was not started"
    else
        tmp="$(mktemp -d)"
        dpkg-deb -x "$OUT/$NAME.deb" "$tmp"
        echo "[*] Smoke: $BIN $SMOKE_ARGS (qemu, dummy video/audio)"
        # shellcheck disable=SC2086  # SMOKE_ARGS is a list of arguments
        SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 120 qemu-aarch64-static -L / "$tmp/usr/games/$BIN" $SMOKE_ARGS \
            || die "smoke run of $BIN failed"
        rm -rf "$tmp"
    fi
fi

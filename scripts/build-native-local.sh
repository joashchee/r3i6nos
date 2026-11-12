#!/bin/bash
# scripts/build-native-local.sh <app-dir under apps/> [--no-smoke]
#
# Builds one of this repo's native apps (C and SDL2) for the R36S on this computer, the way scripts/build-commander.sh builds Commander but for any
# app with an app.conf: build/native/<NAME>.deb (arm64, Debian 12). Needs only Docker (Colima on an Apple chip). The work is
# scripts/build_native_app.sh run as root in debian:bookworm (arm64), so the binary links against Bookworm's SDL2 and glibc, the R36S rootfs's own.
#
#   scripts/build-native-local.sh io-checker-native              build, then run --selftest from the packed .deb (dummy video and sound)
#   scripts/build-native-local.sh io-checker-native --no-smoke   build only
#
# The version is the app's own (`#define VERSION "x"` in its source file, named by SRC in app.conf); R3I6NOS_NATIVE_VERSION overrides it.
set -eu

die() { echo "build-native-local: $*" >&2; exit 1; }
[ $# -ge 1 ] || die "usage: $0 <app-dir under apps/> [--no-smoke]"
APP="$1"; shift
SMOKE=--smoke
for arg in "$@"; do
    case "$arg" in
        --no-smoke) SMOKE="" ;;
        -h|--help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option: $arg" ;;
    esac
done

REPO="$(cd "$(dirname "$0")/.." && pwd)"
CONF="$REPO/apps/$APP/app.conf"
[ -f "$CONF" ] || die "no apps/$APP/app.conf under $REPO"
# shellcheck disable=SC1090  # the app's own manifest
read -r NAME SRC TYPE < <(TYPE=native NAME="" SRC=""; . "$CONF"; echo "$NAME $SRC $TYPE")
[ "$TYPE" = native ] || die "$APP is not a native app"
if [ -z "$NAME" ] || [ -z "$SRC" ]; then die "$CONF has no NAME or SRC"; fi

command -v docker >/dev/null || die "Docker is not installed (brew install colima docker, then colima start)"
# Docker Desktop's leftover config names a credential helper that is not installed without it: pulls then fail. Use an empty config.
if [ -z "${DOCKER_CONFIG:-}" ] && grep -q '"credsStore"[[:space:]]*:[[:space:]]*"desktop"' "$HOME/.docker/config.json" 2>/dev/null \
    && ! command -v docker-credential-desktop >/dev/null; then
    if [ -z "${DOCKER_HOST:-}" ]; then
        DOCKER_HOST="$(docker context inspect --format '{{.Endpoints.docker.Host}}' 2>/dev/null || true)"
        [ -n "$DOCKER_HOST" ] && export DOCKER_HOST
    fi
    DOCKER_CONFIG="$(mktemp -d "${TMPDIR:-/tmp}/native-docker.XXXXXX")"
    echo '{}' > "$DOCKER_CONFIG/config.json"
    export DOCKER_CONFIG
    trap 'rm -rf "$DOCKER_CONFIG"' EXIT
fi
docker info >/dev/null 2>&1 || die "Docker is not running (colima start)"

if [ -z "${R3I6NOS_NATIVE_VERSION:-}" ]; then
    R3I6NOS_NATIVE_VERSION="$(sed -n 's/^#define VERSION "\(.*\)"/\1/p' "$REPO/apps/$APP/$SRC" | head -n 1)"
    R3I6NOS_NATIVE_VERSION="${R3I6NOS_NATIVE_VERSION:-1.0}"
fi

DEB="$REPO/build/native/$NAME.deb"
rm -f "$DEB"
echo "[*] Building $NAME $R3I6NOS_NATIVE_VERSION in Debian 12 (arm64) under Docker ..."
docker run --rm --platform linux/arm64 -v "$REPO":/src -w /src -e R3I6NOS_NATIVE_VERSION="$R3I6NOS_NATIVE_VERSION" debian:bookworm \
    scripts/build_native_app.sh "$APP" $SMOKE
[ -s "$DEB" ] || die "the build finished but there is no build/native/$NAME.deb"
echo "[*] Built: $DEB ($(du -h "$DEB" | cut -f1), version $R3I6NOS_NATIVE_VERSION)"

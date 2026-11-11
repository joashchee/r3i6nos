#!/bin/bash
# scripts/build-commander.sh [--no-smoke]
#
# Builds Commander for the R36S on this computer, with no arguments and without the firmware workflow: build/native/Commander.deb
# (package r36s-commander, arm64, Debian 12). Run it from anywhere; it needs only Docker (Colima on an Apple chip). The work is
# scripts/build_native_app.sh, run as root in debian:bookworm (arm64), so the binary links against Bookworm's SDL2 and glibc, the R36S
# rootfs's own.
#
#   scripts/build-commander.sh              build, then run `commander --selftest` from the packed .deb (dummy video and sound)
#   scripts/build-commander.sh --no-smoke   build only
#
# Drop build/native/Commander.deb in native_deb/ on the games card (deb_watcher installs it at boot), or leave it where it is: the firmware
# image build (scripts/build_native_apps.sh) picks it up from the same folder. The version is Commander's own (VERSION in commander.c);
# R3I6NOS_NATIVE_VERSION overrides it. The smoke test proves the binary runs and links, not that it draws on the panel or plays
# sound: that needs a unit (ROADMAP).
set -eu

SMOKE=--smoke
for arg in "$@"; do
    case "$arg" in
        --no-smoke) SMOKE="" ;;
        -h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "build-commander: unknown option: $arg (try --help)" >&2; exit 2 ;;
    esac
done
die() { echo "build-commander: $*" >&2; exit 1; }

REPO="$(cd "$(dirname "$0")/.." && pwd)"
APP=commander-native
[ -f "$REPO/apps/$APP/app.conf" ] || die "no apps/$APP/app.conf under $REPO"

command -v docker >/dev/null || die "Docker is not installed (brew install colima docker, then colima start)"
# Docker Desktop's leftover config names a credential helper that is not installed without it: pulls then fail. Use an empty config.
if [ -z "${DOCKER_CONFIG:-}" ] && grep -q '"credsStore"[[:space:]]*:[[:space:]]*"desktop"' "$HOME/.docker/config.json" 2>/dev/null \
    && ! command -v docker-credential-desktop >/dev/null; then
    # the context (colima) lives beside that config, so keep its socket before leaving the config behind
    if [ -z "${DOCKER_HOST:-}" ]; then
        DOCKER_HOST="$(docker context inspect --format '{{.Endpoints.docker.Host}}' 2>/dev/null || true)"
        [ -n "$DOCKER_HOST" ] && export DOCKER_HOST
    fi
    DOCKER_CONFIG="$(mktemp -d "${TMPDIR:-/tmp}/commander-docker.XXXXXX")"
    echo '{}' > "$DOCKER_CONFIG/config.json"
    export DOCKER_CONFIG
    trap 'rm -rf "$DOCKER_CONFIG"' EXIT
fi
docker info >/dev/null 2>&1 || die "Docker is not running (colima start)"

# Commander's own version, from its source: the .deb then says what the app says
if [ -z "${R3I6NOS_NATIVE_VERSION:-}" ]; then
    R3I6NOS_NATIVE_VERSION="$(sed -n 's/^#define VERSION "\(.*\)"/\1/p' "$REPO/apps/$APP/commander.c" | head -n 1)"
    R3I6NOS_NATIVE_VERSION="${R3I6NOS_NATIVE_VERSION:-1.0}"
fi

DEB="$REPO/build/native/Commander.deb"
rm -f "$DEB"
echo "[*] Building Commander $R3I6NOS_NATIVE_VERSION in Debian 12 (arm64) under Docker ..."
docker run --rm --platform linux/arm64 -v "$REPO":/src -w /src -e R3I6NOS_NATIVE_VERSION="$R3I6NOS_NATIVE_VERSION" debian:bookworm \
    scripts/build_native_app.sh "$APP" $SMOKE
[ -s "$DEB" ] || die "the build finished but there is no build/native/Commander.deb"
echo "[*] Built: $DEB ($(du -h "$DEB" | cut -f1), version $R3I6NOS_NATIVE_VERSION)"

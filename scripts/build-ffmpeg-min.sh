#!/bin/bash
# scripts/build-ffmpeg-min.sh
#
# Builds the minimal ffmpeg for the R36S (tools/ffmpeg-min/README.md) on this computer, with no arguments: build/ffmpeg-min/ffmpeg (arm64,
# stripped, LGPL, links only libc and libm). It needs only Docker (Colima on an Apple chip). The work is tools/ffmpeg-min/build.sh, run as root
# in debian:bookworm (arm64): it downloads the pinned ffmpeg source (hash checked), builds it, and decodes files made by Debian's full ffmpeg
# with both, comparing the bytes. The firmware image does NOT include it yet (ROADMAP section 20).
set -eu
die() { echo "build-ffmpeg-min: $*" >&2; exit 1; }
REPO="$(cd "$(dirname "$0")/.." && pwd)"
[ -f "$REPO/tools/ffmpeg-min/build.sh" ] || die "no tools/ffmpeg-min/build.sh under $REPO"
command -v docker >/dev/null || die "Docker is not installed (brew install colima docker, then colima start)"
# Docker Desktop's leftover config names a credential helper that is not installed without it: pulls then fail. Use an empty config.
if [ -z "${DOCKER_CONFIG:-}" ] && grep -q '"credsStore"[[:space:]]*:[[:space:]]*"desktop"' "$HOME/.docker/config.json" 2>/dev/null \
    && ! command -v docker-credential-desktop >/dev/null; then
    if [ -z "${DOCKER_HOST:-}" ]; then
        DOCKER_HOST="$(docker context inspect --format '{{.Endpoints.docker.Host}}' 2>/dev/null || true)"
        [ -n "$DOCKER_HOST" ] && export DOCKER_HOST
    fi
    DOCKER_CONFIG="$(mktemp -d "${TMPDIR:-/tmp}/ffmpeg-min-docker.XXXXXX")"
    echo '{}' > "$DOCKER_CONFIG/config.json"
    export DOCKER_CONFIG
    trap 'rm -rf "$DOCKER_CONFIG"' EXIT
fi
docker info >/dev/null 2>&1 || die "Docker is not running (colima start)"
mkdir -p "$REPO/build/ffmpeg-min"
rm -f "$REPO/build/ffmpeg-min/ffmpeg"
echo "[*] Building the minimal ffmpeg in Debian 12 (arm64) under Docker ..."
docker run --rm --platform linux/arm64 -v "$REPO":/src -e OUT=/src/build/ffmpeg-min -w /src debian:bookworm tools/ffmpeg-min/build.sh
[ -s "$REPO/build/ffmpeg-min/ffmpeg" ] || die "the build finished but there is no build/ffmpeg-min/ffmpeg"
echo "[*] Built: $REPO/build/ffmpeg-min/ffmpeg ($(du -h "$REPO/build/ffmpeg-min/ffmpeg" | cut -f1))"

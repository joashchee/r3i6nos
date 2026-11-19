#!/bin/bash
# scripts/build-streamtools.sh [--relock | --latest] [tool ...]
#
# Makes the stream-tool packages on this computer with one command (docs/STREAM-TOOLS.md, ROADMAP section 34): build/streamtools/r3i6nos-ytdlp.deb (yt-dlp, QuickJS-ng,
# the solver scripts), r3i6nos-gallery-dl.deb and r3i6nos-streamlink.deb. Tool packages: put them in a card's native_deb/ or send them with
# scripts/push-packages.sh UNIT build/streamtools/*.deb; they are not in the image. Needs only Docker (Colima on an Apple chip): the work is tools/streamtools/build.sh,
# run as root in debian:bookworm arm64. It fetches the pinned sources (tools/streamtools/pins.conf, lock/*.txt), fails on any hash that does not match, and checks what it built.
#
#   (no option)  build every tool, or only the ones named (ytdlp, gallery-dl, streamlink)
#   --latest     print the newest upstream versions as lines for pins.conf (changes nothing): yt-dlp breaks every few weeks, this is how to find the fix
#   --relock     resolve the pinned Python packages again and rewrite tools/streamtools/lock/*.txt
# Then: tests/test_streamtools_docker.sh installs the packages in a clean Debian 12 arm64 container through the real deb_watcher.py and runs each tool.
set -eu
die() { echo "build-streamtools: $*" >&2; exit 1; }
REPO="$(cd "$(dirname "$0")/.." && pwd)"
[ -f "$REPO/tools/streamtools/build.sh" ] || die "no tools/streamtools/build.sh under $REPO"
MODE=build
case "${1:-}" in --relock) MODE=relock; shift ;; --latest) MODE=latest; shift ;; -*) die "unknown option: $1" ;; esac
command -v docker >/dev/null || die "Docker is not installed (brew install colima docker, then colima start)"
# Docker Desktop's leftover config names a credential helper that is not installed without it: pulls then fail. Use an empty config (as build-ffmpeg-min.sh does).
if [ -z "${DOCKER_CONFIG:-}" ] && grep -q '"credsStore"[[:space:]]*:[[:space:]]*"desktop"' "$HOME/.docker/config.json" 2>/dev/null \
    && ! command -v docker-credential-desktop >/dev/null; then
    if [ -z "${DOCKER_HOST:-}" ]; then
        DOCKER_HOST="$(docker context inspect --format '{{.Endpoints.docker.Host}}' 2>/dev/null || true)"
        [ -n "$DOCKER_HOST" ] && export DOCKER_HOST
    fi
    DOCKER_CONFIG="$(mktemp -d "${TMPDIR:-/tmp}/streamtools-docker.XXXXXX")"
    echo '{}' > "$DOCKER_CONFIG/config.json"
    export DOCKER_CONFIG
    trap 'rm -rf "$DOCKER_CONFIG"' EXIT
fi
docker info >/dev/null 2>&1 || die "Docker is not running (colima start)"
OUT="$REPO/build/streamtools"
mkdir -p "$OUT"
[ "$MODE" = build ] && rm -f "$OUT"/r3i6nos-*.deb
echo "[*] streamtools $MODE in Debian 12 (arm64) under Docker ..."
# the repository is mounted writable only for --relock (it rewrites lock/*.txt); a build writes only to /out
if [ "$MODE" = relock ]; then SRC_MODE=rw; else SRC_MODE=ro; fi
docker run --rm --platform linux/arm64 -v "$REPO":/src:$SRC_MODE -v "$OUT":/out -e OUT=/out -w /src debian:bookworm tools/streamtools/build.sh "$MODE" "$@"
if [ "$MODE" = build ]; then
    ls "$OUT"/r3i6nos-*.deb >/dev/null 2>&1 || die "the build finished but left no .deb in $OUT"
    ls -l "$OUT"/r3i6nos-*.deb
fi

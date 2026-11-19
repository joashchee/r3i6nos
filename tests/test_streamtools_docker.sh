#!/bin/bash
# tests/test_streamtools_docker.sh: the three stream-tool packages (build/streamtools/*.deb, from scripts/build-streamtools.sh) installed by the real deb_watcher.py in a
# CLEAN Debian 12 arm64 container (only the image's Python packages, ca-certificates and the image's ffmpeg-min) and each tool run against media served by a local web
# server (DASH and HLS made by the full ffmpeg, plain and AES-128 encrypted). Needs Docker, the debs and build/ffmpeg-min/ffmpeg; says NOT RUN otherwise. Not a test of
# the unit (CPU speed, memory, Wi-Fi) or of any real site: tests/streamtools/live.sh does one real request, by hand.
set -eu
cd "$(dirname "$0")/.."
command -v docker >/dev/null 2>&1 || { echo "NOT RUN: needs Docker"; exit 0; }
ls build/streamtools/r3i6nos-*.deb >/dev/null 2>&1 || { echo "NOT RUN: no build/streamtools/*.deb (scripts/build-streamtools.sh)"; exit 0; }
[ -x build/ffmpeg-min/ffmpeg ] || { echo "NOT RUN: no build/ffmpeg-min/ffmpeg (scripts/build-ffmpeg-min.sh)"; exit 0; }
# shellcheck disable=SC1091  # the pins
. tools/streamtools/pins.conf
FX="$PWD/build/streamtools/fixtures"
if [ ! -f "$FX/clip.mp4" ] || [ -n "${R3I6NOS_FIXTURES_REFRESH:-}" ]; then
    mkdir -p "$FX"
    docker run --rm --platform linux/arm64 -v "$PWD/tests/streamtools":/e:ro -v "$FX":/fx debian:12 sh /e/fixtures.sh >"$FX.log" 2>&1 || { tail -5 "$FX.log" >&2; echo "FAIL: could not make the fixtures" >&2; exit 1; }
fi
# R3I6NOS_BASELINE_FFMPEG=/path/to/an/older/ffmpeg-min (optional): shows what the matroska, webm and ipod muxers changed (the older one cannot join VP9 and Opus)
EXTRA=(-e BASE_NAME=)
if [ -n "${R3I6NOS_BASELINE_FFMPEG:-}" ] && [ -x "$R3I6NOS_BASELINE_FFMPEG" ]; then
    EXTRA=(-e "BASE_NAME=$(basename "$R3I6NOS_BASELINE_FFMPEG")" -v "$(cd "$(dirname "$R3I6NOS_BASELINE_FFMPEG")" && pwd)":/bs:ro)
fi
out="$(docker run --rm --platform linux/arm64 -e YTDLP_VERSION="$YTDLP_VERSION" -e E2E_LIVE="${R3I6NOS_LIVE:-}" -v "$PWD/tests/streamtools":/e:ro -v "$PWD/build/streamtools":/debs:ro \
    -v "$PWD/build/ffmpeg-min":/ff:ro -v "$FX":/fx:ro -v "$PWD/overlay/usr/local/bin":/w:ro "${EXTRA[@]}" debian:12 \
    sh -c 'mkdir -p /base; if [ -n "$BASE_NAME" ]; then cp "/bs/$BASE_NAME" /base/ffmpeg; fi; sh /e/e2e.sh' 2>&1)" || true
echo "$out" | grep -E '^E2E |^  ' || true
if echo "$out" | grep -qE '^E2E .* FAIL'; then echo "FAIL: stream tools in Docker" >&2; exit 1; fi
for n in install-yt-dlp install-gallery-dl install-streamlink ytdlp-version ytdlp-jsruntime ytdlp-dash-merge-mp4 ytdlp-dash-merge-webm ytdlp-hls-aes ytdlp-pipe gallery-dl-download streamlink-hls no-runtime-bytecode end; do
    echo "$out" | grep -qE "^E2E $n " || { echo "FAIL: no result for $n" >&2; echo "$out" | tail -15 >&2; exit 1; }
done
echo "streamtools docker OK"

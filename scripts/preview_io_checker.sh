#!/bin/bash
# preview_io_checker.sh <11.5 | 12.0 | path-to-love-binary> [page] [out.png]
#
# Draws the IO Checker once under a real LÖVE on this Mac, with a fake gamepad, and saves a screenshot
# (default build/preview/io-checker-<version>-<page>.png). page: home (default), input, video, sound, files, system.
# Get LÖVE with scripts/fetch_love_mac.sh. It opens a window for a moment, so it needs a normal desktop session.

set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"
WHICH="${1:?Usage: $0 <11.5|12.0|path-to-love> [page] [out.png]}"
case "$WHICH" in
    11.5|12.0) LOVE="$REPO/vendor/love-mac/$WHICH/love.app/Contents/MacOS/love"; TAG="$WHICH" ;;
    *) LOVE="$WHICH"; TAG="custom" ;;
esac
PAGE="${2:-home}"
OUT="${3:-$REPO/build/preview/io-checker-$TAG-$PAGE.png}"

[ -x "$LOVE" ] || { echo "preview: no LÖVE at $LOVE (run scripts/fetch_love_mac.sh)" >&2; exit 1; }
mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"

R3I6NOS_THEME="$REPO/theme" CHECKER_DIR="${CHECKER_DIR:-$REPO/apps/io-checker-love}" CHECKER_PAGE="$PAGE" CHECKER_OUT="$OUT" \
    "$LOVE" "$REPO/scripts/preview"
[ -s "$OUT" ] || { echo "preview: no screenshot was written" >&2; exit 1; }
echo "preview: $OUT"

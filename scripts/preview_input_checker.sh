#!/bin/bash
# preview_input_checker.sh <11.5 | 12.0 | path-to-love-binary> [out.png]
#
# Draws the input checker once under a real LÖVE on this Mac, with a fake gamepad, and saves a screenshot
# (default build/preview/input-checker-<version>.png). Get LÖVE with scripts/fetch_love_mac.sh.
# It opens a window for a moment, so it needs a normal desktop session.

set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"
WHICH="${1:?Usage: $0 <11.5|12.0|path-to-love> [out.png]}"
case "$WHICH" in
    11.5|12.0) LOVE="$REPO/vendor/love-mac/$WHICH/love.app/Contents/MacOS/love"; TAG="$WHICH" ;;
    *) LOVE="$WHICH"; TAG="custom" ;;
esac
OUT="${2:-$REPO/build/preview/input-checker-$TAG.png}"

[ -x "$LOVE" ] || { echo "preview: no LÖVE at $LOVE (run scripts/fetch_love_mac.sh)" >&2; exit 1; }
mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"

CHECKER_DIR="${CHECKER_DIR:-$REPO/apps/input-checker-love}" CHECKER_OUT="$OUT" "$LOVE" "$REPO/scripts/preview"
[ -s "$OUT" ] || { echo "preview: no screenshot was written" >&2; exit 1; }
echo "preview: $OUT"

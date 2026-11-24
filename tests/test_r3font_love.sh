#!/usr/bin/env bash
# R3i6nos Mono in LOVE itself: LOVE's FreeType rasteriser (love.font, no window) draws every code point of the TTF exactly as its source
# bitmap, with the bearing and advance, at each hinting mode. Needs the LOVE 11.5 desktop app (scripts/fetch_love_mac.sh puts it in
# vendor/love-mac) and the TTF (scripts/build-r3-font.sh). Skips, saying so, when either is missing.
set -u
cd "$(dirname "$0")/.." || exit 1
LOVE="${LOVE_BIN:-vendor/love-mac/11.5/love.app/Contents/MacOS/love}"
DIR=theme/font/r3i6nos-mono
if [ ! -x "$LOVE" ]; then echo "test_r3font_love: SKIPPED (no LOVE at $LOVE)"; exit 0; fi
if [ ! -f "$DIR/R3i6nosMono-Regular.ttf" ]; then echo "test_r3font_love: SKIPPED (no TTF: run scripts/build-r3-font.sh)"; exit 0; fi
R3F_DIR="$PWD/$DIR" "$LOVE" tests/love_font
rc=$?
if [ "$rc" -eq 0 ]; then echo "test_r3font_love: OK"; else echo "test_r3font_love: FAILED ($rc)"; fi
exit "$rc"

#!/bin/bash
# build_love_app.sh <app-dir>
#
# Builds ONE LÖVE app from apps/<app-dir>/ as described by its app.conf: syntax-checks every .lua file, runs the
# app's tests (TESTS), then packs build/love-apps/Love <version>/<NAME>.love for each of RUNTIMES (same bytes every
# time, scripts/pack_love.py). The firmware build owns none of this: it picks up the .love files of the DEFAULT=1 apps
# (configure_rootfs.sh), and each app has its own workflow (.github/workflows/love-app.yml, called by app-<name>.yml).
#
# Needs python3 and luajit. Works on a Mac or on Linux; no root, no container. It does NOT run the app under LÖVE
# (the runtimes are built in the firmware workflow): the app's fake-LÖVE tests are what stand in for that.
#
# R3I6NOS_LOVE_APPS_OUT overrides the output folder.

set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${R3I6NOS_LOVE_APPS_OUT:-$REPO/build/love-apps}"

APP_DIR="${1:?Usage: $0 <app-dir under apps/>}"
CONF="$REPO/apps/$APP_DIR/app.conf"
[ -f "$CONF" ] || { echo "build_love_app: no $CONF" >&2; exit 1; }

TYPE="" NAME="" RUNTIMES="" TESTS=""
# shellcheck disable=SC1090  # the app's own manifest
. "$CONF"
[ "$TYPE" = love ] || { echo "build_love_app: $CONF is not TYPE=love" >&2; exit 1; }
for v in NAME RUNTIMES; do
    [ -n "${!v}" ] || { echo "build_love_app: $CONF does not set $v" >&2; exit 1; }
done
[ -f "$REPO/apps/$APP_DIR/main.lua" ] || { echo "build_love_app: no apps/$APP_DIR/main.lua" >&2; exit 1; }
command -v luajit >/dev/null || { echo "build_love_app: luajit is not installed" >&2; exit 1; }

echo "[*] Lua syntax: apps/$APP_DIR"
find "$REPO/apps/$APP_DIR" -name '*.lua' -not -path '*/.*' | sort | while read -r f; do
    luajit -bl "$f" >/dev/null || { echo "build_love_app: syntax error in $f" >&2; exit 1; }
done

if [ -n "$TESTS" ]; then
    echo "[*] Tests: $TESTS"
    (cd "$REPO" && luajit "$TESTS")
fi

for v in $RUNTIMES; do
    rm -f "$OUT/Love $v/$NAME.love"
    python3 -I "$REPO/scripts/pack_love.py" "$REPO/apps/$APP_DIR" "$OUT/Love $v/$NAME.love"
    echo "[*] Done: $OUT/Love $v/$NAME.love"
done

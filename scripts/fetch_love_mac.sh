#!/bin/bash
# fetch_love_mac.sh [11.5 | 12.0 | all]
#
# Downloads LÖVE for macOS into vendor/love-mac/<version>/love.app (gitignored), to try apps on a Mac
# before they go on the card. Not used by the image: the device's runtimes are built by build_love.sh.
#
#   11.5  the official release; the download is checked against the sha256 pinned below.
#   12.0  there is no release: the macOS build of the same commit that build_love.sh pins, from that commit's
#         GitHub Actions run. Artifacts need a GitHub login, so this needs `gh auth login` first.
#         The artifact's sha256 is printed so it can be pinned here after the first download.
#
# Override the folder with R3I6NOS_LOVE_MAC_DIR (the tests and previews use it).

set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${R3I6NOS_LOVE_MAC_DIR:-$REPO/vendor/love-mac}"
WHICH="${1:-all}"

LOVE115_URL=https://github.com/love2d/love/releases/download/11.5/love-11.5-macos.zip
LOVE115_SHA256=6795bb3a1656af6a2fdfe741e150787b481886d3a280327a261a3fdded586913
# The CI run of love2d/love at commit def245102c67909f9902095af613b1ae05249b62 (same pin as build_love.sh).
LOVE120_RUN=41957924703
LOVE120_ARTIFACT=love-macos

fail() { echo "fetch_love_mac: $*" >&2; exit 1; }

[ "$(uname -s)" = Darwin ] || fail "macOS only (this is $(uname -s))"
case "$WHICH" in 11.5|12.0|all) ;; *) fail "usage: $0 [11.5|12.0|all]" ;; esac

TMP="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-lovemac.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

# Unzips every zip found under $1 (an artifact may hold the app or another zip), then prints the first love.app.
find_app() {
    local dir="$1" z
    for _ in 1 2 3; do
        while IFS= read -r -d '' z; do
            unzip -q -o "$z" -d "$(dirname "$z")" && rm -f "$z"
        done < <(find "$dir" -name '*.zip' -print0)
    done
    find "$dir" -name love.app -type d -print -quit
}

check_version() {  # check_version <love.app> <expected prefix, e.g. "LOVE 11.5">
    local out
    out="$("$1/Contents/MacOS/love" --version 2>/dev/null | head -n 1)"
    case "$out" in
        "$2"*) echo "    $out" ;;
        *) fail "$1 says '$out', expected '$2...'" ;;
    esac
}

fetch_115() {
    local out="$DEST/11.5"
    if [ -d "$out/love.app" ]; then echo "[*] LÖVE 11.5 already in $out"; return; fi
    echo "[*] LÖVE 11.5 (official release)..."
    mkdir -p "$TMP/115"
    curl -fSL --retry 2 -o "$TMP/115/love.zip" "$LOVE115_URL"
    have="$(shasum -a 256 "$TMP/115/love.zip" | cut -d' ' -f1)"
    [ "$have" = "$LOVE115_SHA256" ] || fail "checksum mismatch for the 11.5 download (got $have)"
    app="$(find_app "$TMP/115")"
    [ -n "$app" ] || fail "no love.app in the 11.5 download"
    check_version "$app" "LOVE 11.5"
    mkdir -p "$out"
    mv "$app" "$out/love.app"
    echo "    -> $out/love.app"
}

fetch_120() {
    local out="$DEST/12.0"
    if [ -d "$out/love.app" ]; then echo "[*] LÖVE 12.0 already in $out"; return; fi
    command -v gh >/dev/null || fail "gh is not installed (brew install gh)"
    gh auth status >/dev/null 2>&1 || fail "gh is not logged in: run 'gh auth login' (artifacts need a GitHub login)"
    echo "[*] LÖVE 12.0 (development build, CI run $LOVE120_RUN)..."
    mkdir -p "$TMP/120"
    gh run download "$LOVE120_RUN" -R love2d/love -n "$LOVE120_ARTIFACT" -D "$TMP/120" \
        || fail "download failed (artifacts expire after 90 days: re-pin LOVE120_RUN to a newer run of the pinned commit's successor)"
    find "$TMP/120" -type f -exec shasum -a 256 {} + | sed 's|  .*/|  |'
    app="$(find_app "$TMP/120")"
    [ -n "$app" ] || fail "no love.app in the 12.0 artifact"
    check_version "$app" "LOVE 12"
    mkdir -p "$out"
    mv "$app" "$out/love.app"
    echo "    -> $out/love.app"
}

case "$WHICH" in
    11.5) fetch_115 ;;
    12.0) fetch_120 ;;
    all) fetch_115; fetch_120 ;;
esac
echo "Run an app:  $DEST/11.5/love.app/Contents/MacOS/love apps/input-checker-love"

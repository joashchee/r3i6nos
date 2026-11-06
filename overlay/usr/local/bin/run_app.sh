#!/bin/bash
# run_app.sh - launch a LÖVE game or a native app on the R36S.
# Usage: run_app.sh [love115|love120|native] <path_to_game_or_binary>

set -u

TYPE="${1:-}"
TARGET="${2:-}"

if [ -z "$TYPE" ] || [ -z "$TARGET" ]; then
    echo "Usage: $0 [love115|love120|native] <path_to_target>" >&2
    exit 1
fi

if [ ! -e "$TARGET" ]; then
    echo "run_app.sh: not found: $TARGET" >&2
    exit 1
fi

# KMS/DRM, no X server, no mouse.
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-kmsdrm}"
export SDL_NOMOUSE=1

# UNVERIFIED: GUID and button order are from the spec, not tested on a unit.
# Replace with the mapping captured from a real device (see ROADMAP).
export SDL_GAMECONTROLLERCONFIG="${SDL_GAMECONTROLLERCONFIG:-03000000341200006048000000010000,R36S Handheld,a:b1,b:b0,x:b3,y:b2,leftshoulder:b4,rightshoulder:b5,back:b6,start:b7,dpup:h0.1,dpdown:h0.2,dpleft:h0.4,dpright:h0.8,platform:Linux,}"

KEYS_CFG="${R3I6NOS_KEYS_CFG:-/etc/love_keys.cfg}"
GPTOKEYB_PID=""

start_gptokeyb() {
    # Optional: only when gptokeyb is installed. LÖVE reads the gamepad itself.
    if command -v gptokeyb >/dev/null 2>&1 && [ -f "$KEYS_CFG" ]; then
        gptokeyb "$1" -c "$KEYS_CFG" &
        GPTOKEYB_PID=$!
    fi
}

cleanup() {
    [ -n "$GPTOKEYB_PID" ] && kill "$GPTOKEYB_PID" 2>/dev/null
}
trap cleanup EXIT

run_love() {
    local bin="$1"
    if [ ! -x "$bin" ]; then
        echo "run_app.sh: LÖVE runtime missing: $bin" >&2
        exit 1
    fi
    start_gptokeyb love
    "$bin" "$TARGET"
}

case "$TYPE" in
    love115) run_love "${R3I6NOS_LOVE115:-/opt/love/11.5/bin/love}" ;;
    love120) run_love "${R3I6NOS_LOVE120:-/opt/love/12.0/bin/love}" ;;
    native)
        if [ ! -x "$TARGET" ]; then
            echo "run_app.sh: not executable: $TARGET" >&2
            exit 1
        fi
        "$TARGET"
        ;;
    *)
        echo "run_app.sh: unknown application type: $TYPE" >&2
        exit 1
        ;;
esac

#!/bin/bash
# run_app.sh - launch a LÖVE game or a native app on the R36S.
# Usage: run_app.sh [love115|love120|native] <path_to_game_or_binary>

set -u

# Command-line tools installed from native_deb (deb_watcher.py, a deb with usr/share/r3i6nos/tool) are on PATH for every app, so a LÖVE game's
# os.execute and io.popen, or a native app's system(), find them by name.
export PATH="${R3I6NOS_TOOLS_DIR:-/opt/loveos/bin}:$PATH"

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

# Buttons and axes are MEASURED (input checker on a real R36S, 2026-11-07; docs/hardware-findings.md "Input layout"), 0-based as SDL
# counts: B 0, A 1, X 2, Y 3, L1-R2 4-7, D-pad 8-11 (buttons, not a hat), Select 12, Start 13, L3 14, R3 15, Fn 16; axes 0-3.
# GUID READ from the unit (KlamANSI's device report, 2026-11-09): SDL 2.26.5 calls the pad "GO-Super Gamepad",
# 1900bb3e4b4800000011000000010000 (bus 0x19 host, the name's CRC bb3e, vendor 0x484b, product 0x1100, version 0x0100), 17 buttons,
# 4 axes, 0 hats. SDL's own database maps that pad differently (Select, Start and Fn wrong), and a mapping given here replaces it.
# The second line is the same pad with the CRC zeroed, as SDL before 2.26 builds the GUID (an app bundling an older SDL) and as
# 2.26 falls back to. UNTESTED: that an app using SDL's gamepad API then sees these buttons (the checkers and Commander read raw ones).
R36S_PAD="a:b1,b:b0,x:b2,y:b3,leftshoulder:b4,rightshoulder:b5,lefttrigger:b6,righttrigger:b7,dpup:b8,dpdown:b9,dpleft:b10,dpright:b11,back:b12,start:b13,leftstick:b14,rightstick:b15,guide:b16,leftx:a0,lefty:a1,rightx:a2,righty:a3,platform:Linux,"
export SDL_GAMECONTROLLERCONFIG="${SDL_GAMECONTROLLERCONFIG:-1900bb3e4b4800000011000000010000,R36S GO-Super Gamepad,$R36S_PAD
190000004b4800000011000000010000,R36S GO-Super Gamepad,$R36S_PAD}"

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

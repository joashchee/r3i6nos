#!/bin/bash
# tests/test_love_samples.sh: the LÖVE sample games (tools/runtimes/samples/Love 11.5/Hello_Love_11.5 and Love 12.0/Hello_Love_12.0): the same main.lua in both, each conf.lua names
# its runtime, the menu's scan lists them, and (with Docker) the game runs 30 frames on Debian's LÖVE (11.4: the only one apt has) on a virtual X server and quits by its own hook.
# What it cannot say: LÖVE 12.0 itself (only 11.x is installable here), GLES on the Mali, the pad, the sound. Those are the unit's.
set -eu
cd "$(dirname "$0")/.."
S=tools/runtimes/samples
fail() { echo "FAIL: $*" >&2; exit 1; }
a="$S/Love 11.5/Hello_Love_11.5"; b="$S/Love 12.0/Hello_Love_12.0"
for d in "$a" "$b"; do
    [ -f "$d/main.lua" ] || fail "$d: no main.lua"
    [ -f "$d/conf.lua" ] || fail "$d: no conf.lua"
done
cmp -s "$a/main.lua" "$b/main.lua" || fail "the two main.lua differ: one game, two copies"
grep -q 't.version = "11.5"' "$a/conf.lua" || fail "11.5's conf.lua names another version"
grep -q 't.version = "12.0"' "$b/conf.lua" || fail "12.0's conf.lua names another version"
grep -q "width = 640" "$a/conf.lua" || fail "the window is not 640 wide"
grep -q "height = 480" "$a/conf.lua" || fail "the window is not 480 high"
if command -v luajit >/dev/null 2>&1; then luajit -bl "$a/main.lua" >/dev/null || fail "main.lua does not compile"; fi
if command -v luajit >/dev/null 2>&1; then
    T="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-lovesample.XXXXXX")"; trap 'rm -rf "$T"' EXIT
    mkdir -p "$T/card"; cp -R "$S/Love 11.5" "$S/Love 12.0" "$T/card/"
    out="$(cd menu && CARD="$T/card" NONE="$T/none" luajit -e 'package.path="./?.lua;"..package.path; local scan=require("scan"); for _,e in ipairs(scan.entries(os.getenv("CARD"), os.getenv("NONE"))) do print(e.type, e.title) end')"
    echo "$out" | grep -q "^love115	Hello Love 11.5$" || fail "the menu does not list the 11.5 sample: $out"
    echo "$out" | grep -q "^love120	Hello Love 12.0$" || fail "the menu does not list the 12.0 sample: $out"
fi
if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
    out="$(docker run --rm --platform linux/arm64 -v "$PWD/$S":/s:ro debian:12 bash -c '
        apt-get update -qq >/dev/null 2>&1; apt-get install -y -qq love xvfb xauth libgl1-mesa-dri >/dev/null 2>&1
        export LIBGL_ALWAYS_SOFTWARE=1 ALSOFT_DRIVERS=null SDL_AUDIODRIVER=dummy R3I6NOS_SAMPLE_FRAMES=30
        for v in "Love 11.5/Hello_Love_11.5" "Love 12.0/Hello_Love_12.0"; do
            rm -rf /tmp/g; cp -R "/s/$v" /tmp/g
            sed -i "s/t.version = .*/t.version = \"11.4\"/" /tmp/g/conf.lua     # Debian has 11.4; a mismatch opens a message box that waits for a click
            cat >> /tmp/g/main.lua <<EOF
function love.quit() io.stderr:write("SAMPLE QUIT "..frames.." frames\n") end
EOF
            if timeout 90 xvfb-run -a -s "-screen 0 800x600x24" love /tmp/g 2>&1 | grep -q "SAMPLE QUIT 30 frames"; then echo "E2E ok $v"; else echo "E2E FAIL $v"; fi
        done' 2>&1)" || true
    echo "$out" | grep -q "E2E FAIL" && fail "a sample did not run 30 frames and quit: $(echo "$out" | tail -3)"
    echo "$out" | grep -c "^E2E ok" | grep -q 2 || fail "the Docker run did not finish both: $(echo "$out" | tail -3)"
else
    echo "NOT RUN in Docker: no Docker"
fi
echo "love samples OK"

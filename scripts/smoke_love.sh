#!/bin/bash
# smoke_love.sh <rootfs-dir> [version]
#
# Runs the arm64 LÖVE from a built rootfs under qemu-user (no chroot, no
# root-only mounts: qemu's -L points the dynamic loader at the rootfs), with
# the dummy video and audio drivers, on tests/smoke/love. A pass proves the
# runtime links against the rootfs's libraries and its non-graphical modules
# work. It does NOT prove it draws: that needs the Mali driver (phase 5).

set -u

REPO="$(cd "$(dirname "$0")/.." && pwd)"
ROOTFS="${1:?Usage: $0 <rootfs-dir> [version]}"
VERSION="${2:-11.5}"
LOVE="$ROOTFS/opt/love/$VERSION/bin/love"

[ -x "$LOVE" ] || { echo "smoke_love: not found or not executable: $LOVE" >&2; exit 1; }

if [ "$(uname -m)" = aarch64 ]; then
    RUN=(chroot "$ROOTFS")
    LOVE_IN="/opt/love/$VERSION/bin/love"
else
    QEMU="$(command -v qemu-aarch64-static || true)"
    [ -n "$QEMU" ] || { echo "smoke_love: qemu-aarch64-static not installed" >&2; exit 1; }
    RUN=("$QEMU" -L "$ROOTFS")
    LOVE_IN="$LOVE"
fi

HOME_DIR="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-smoke.XXXXXX")"
trap 'rm -rf "$HOME_DIR"' EXIT

# On an arm64 host the game directory must be visible inside the chroot.
GAME="$REPO/tests/smoke/love"
if [ "${RUN[0]}" = chroot ]; then
    mkdir -p "$ROOTFS/tmp/r3i6nos-smoke"
    cp -a "$GAME/." "$ROOTFS/tmp/r3i6nos-smoke/"
    GAME=/tmp/r3i6nos-smoke
    trap 'rm -rf "$HOME_DIR" "$ROOTFS/tmp/r3i6nos-smoke"' EXIT
fi

echo "[*] LÖVE $VERSION smoke test..."
out="$(
    env HOME="$HOME_DIR" XDG_DATA_HOME="$HOME_DIR/data" \
        SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ALSOFT_DRIVERS=null \
        R3I6NOS_EXPECT_VERSION="$VERSION" \
        timeout 180 "${RUN[@]}" "$LOVE_IN" "$GAME" 2>&1
)"
status=$?
echo "$out"

if [ "$status" -eq 0 ] && echo "$out" | grep -q '^R3I6NOS_SMOKE ok '; then
    echo "[*] Smoke test passed."
else
    echo "smoke_love: FAILED (exit $status)" >&2
    exit 1
fi

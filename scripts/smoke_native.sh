#!/bin/bash
# smoke_native.sh <rootfs-dir> <app.deb>
#
# Runs the arm64 native app from a .deb under qemu-user, against the built rootfs's libraries
# (qemu -L), with SDL's dummy video driver: `--selftest` opens SDL, draws one frame and prints
# one line. A pass proves the binary runs and links against the rootfs's SDL2. It does NOT prove
# it draws on the panel or sees the gamepad: that needs the device (ROADMAP phase 5).

set -u

ROOTFS="${1:?Usage: $0 <rootfs-dir> <app.deb>}"
DEB="${2:?Usage: $0 <rootfs-dir> <app.deb>}"

command -v dpkg-deb >/dev/null || { echo "smoke_native: dpkg-deb not installed" >&2; exit 1; }
[ -f "$DEB" ] || { echo "smoke_native: no such file: $DEB" >&2; exit 1; }

TMP="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-native.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
dpkg-deb -x "$DEB" "$TMP"
BIN="$TMP/usr/games/input-checker-native"
[ -x "$BIN" ] || { echo "smoke_native: no usr/games/input-checker-native in $DEB" >&2; exit 1; }

if [ "$(uname -m)" = aarch64 ]; then
    RUN=()
else
    QEMU="$(command -v qemu-aarch64-static || true)"
    [ -n "$QEMU" ] || { echo "smoke_native: qemu-aarch64-static not installed" >&2; exit 1; }
    RUN=("$QEMU" -L "$ROOTFS")
fi

echo "[*] Native input checker smoke test..."
out="$(env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 120 ${RUN[@]+"${RUN[@]}"} "$BIN" --selftest 2>&1)"
status=$?
echo "$out"
if [ "$status" -ne 0 ] || ! grep -q '^R3I6NOS_SMOKE ok input-checker-native=' <<<"$out"; then
    echo "smoke_native: FAILED (exit $status)" >&2
    exit 1
fi
echo "smoke_native: ok"

#!/bin/bash
# scripts/build-nttplay.sh [/path/to/neumetik]
#
# Makes build/nttplay/ for the image (ROADMAP section 27): nttplay (Neumetik's players as a command-line tool, arm64, Debian 12), NOTICE (its third-party
# list) and SOURCE (the Neumetik commit). The build is the Neumetik repository's own (ansiapps' code, proprietary, so never copied into this repo):
# its `r36s/build-nttplay.sh` (Docker, Debian 12 arm64, with its smoke test), run on the checkout beside this one, else the argument, else NEUMETIK_DIR.
# CI does not run this: it takes the artifact `nttplay-arm64` of Neumetik's own workflow (.github/workflows/build.yml, "Fetch nttplay").
# scripts/configure_rootfs.sh puts the binary at /usr/bin/nttplay.
set -eu
die() { echo "build-nttplay: $*" >&2; exit 1; }
REPO="$(cd "$(dirname "$0")/.." && pwd)"
NT_IN="${1:-${NEUMETIK_DIR:-$REPO/../neumetik}}"
[ -x "$NT_IN/r36s/build-nttplay.sh" ] || die "no Neumetik checkout with r36s/build-nttplay.sh at $NT_IN (pass its path, or set NEUMETIK_DIR)"
NT="$(cd "$NT_IN" && pwd)"
"$NT/r36s/build-nttplay.sh" "$REPO"
for f in nttplay NOTICE SOURCE; do [ -s "$NT/r36s/build/nttplay/$f" ] || die "Neumetik's build left no r36s/build/nttplay/$f"; done
mkdir -p "$REPO/build/nttplay"
rm -f "$REPO/build/nttplay/nttplay" "$REPO/build/nttplay/NOTICE" "$REPO/build/nttplay/SOURCE"
cp "$NT/r36s/build/nttplay/nttplay" "$NT/r36s/build/nttplay/NOTICE" "$NT/r36s/build/nttplay/SOURCE" "$REPO/build/nttplay/"
chmod 755 "$REPO/build/nttplay/nttplay"
echo "[*] build/nttplay/ ready: $(cat "$REPO/build/nttplay/SOURCE"), $(du -h "$REPO/build/nttplay/nttplay" | cut -f1)"

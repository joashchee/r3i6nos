#!/bin/bash
# scripts/build-nttplay-tools.sh [out-dir]
#
# Makes build/nttplay/nttplay-report and nttplay-demo (ROADMAP section 27; rules 9, 11 and 14 for a command-line tool): the generic tool report and Rec demo
# (tools/qjs-node/report.py, demo.py) with nttplay's values and the font baked in, and commands.txt (tools/nttplay/commands.txt). nttplay itself is not
# packed here (rule 15); scripts/configure_rootfs.sh installs these beside it. Python 3 standard library only; no Docker, no network.
set -eu
die() { echo "build-nttplay-tools: $*" >&2; exit 1; }
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$REPO/build/nttplay}"
mkdir -p "$OUT"
VERSION="$(awk '{print $2}' "$OUT/SOURCE" 2>/dev/null)"; [ -n "$VERSION" ] || VERSION=unknown
DATE="$(awk '{print $3}' "$OUT/SOURCE" 2>/dev/null)"; [ -n "$DATE" ] || DATE=unknown
COMMIT="$VERSION"
APP=nttplay
sed -e "s|@TOOL@|nttplay|; s|@APP@|$APP|; s|@VERSION@|$COMMIT|; s|@PACKAGE@|nttplay|" "$REPO/tools/qjs-node/report.py" > "$OUT/nttplay-report"
python3 "$REPO/tools/qjs-node/demo.py" --bake "$OUT/nttplay-demo" nttplay "$APP" "$COMMIT" nttplay "$COMMIT" "$DATE" "Neumetik's players as a command-line tool" "$REPO/theme/font/ansiapps-mono/ansiapps_cp437.h"
chmod 755 "$OUT/nttplay-report" "$OUT/nttplay-demo"
cp "$REPO/tools/nttplay/commands.txt" "$OUT/commands.txt"
# the font is ANSIapps Mono (SIL OFL 1.1) and the demo draws with it: its credit and licence travel with nttplay's NOTICE
{ printf -- '---- ANSIapps Mono font (nttplay-demo draws with it; SIL OFL 1.1) ----\n\n'; cat "$REPO/theme/font/ansiapps-mono/README.txt"; printf '\n\n'; cat "$REPO/theme/font/ansiapps-mono/OFL.txt"; } > "$OUT/NOTICE-font"
grep -q '^#@hero ' "$OUT/commands.txt" || die "commands.txt has no #@hero lines"
echo "[*] $OUT: nttplay-report, nttplay-demo, commands.txt, NOTICE-font"

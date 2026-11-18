#!/bin/bash
# scripts/build-portmaster.sh [--stage-only]
#
# Makes build/native/PortMaster.deb, the package of PortMaster (https://github.com/PortsMaster/PortMaster-GUI, MIT), which the image carries for testing
# (docs/PORTMASTER.md). The pinned release zip is fetched (curl; cached in vendor/portmaster/), checked against the sha256 in apps/portmaster/app.conf, and
# only its arm64 files are kept (the armhf and x86_64 binaries are deleted: this image is arm64). This folder's launcher, hooks and sudo shim are
# added, and the licence, NOTICE and SOURCE go in /usr/share/doc/portmaster. Everything PortMaster later downloads (runtimes, ports) comes at run
# time, when a person chooses it, and is the card's, never the image's.
#
#   --stage-only   stop after the tree is made (build/portmaster/stage), no .deb: for machines with neither dpkg-deb nor Docker.
# Packing uses dpkg-deb, or Docker (debian:bookworm) when there is none. Overrides: R3I6NOS_PORTMASTER_ZIP (use this zip instead of fetching; its
# sha256 must still match, or R3I6NOS_PORTMASTER_SHA256 names the one to expect: the tests), R3I6NOS_NATIVE_OUT (build/native), R3I6NOS_PORTMASTER_WORK (build/portmaster).
set -eu
die() { echo "build-portmaster: $*" >&2; exit 1; }
REPO="$(cd "$(dirname "$0")/.." && pwd)"
APP="$REPO/apps/portmaster"
OUT="${R3I6NOS_NATIVE_OUT:-$REPO/build/native}"
WORK="${R3I6NOS_PORTMASTER_WORK:-$REPO/build/portmaster}"
STAGE_ONLY=0
[ "${1:-}" = "--stage-only" ] && STAGE_ONLY=1

TYPE="" NAME="" BIN="" PKG="" DESC="" PM_VERSION="" PM_URL="" PM_SHA256=""
# shellcheck disable=SC1091  # the app's own manifest
. "$APP/app.conf"
[ "$TYPE" = bundle ] || die "apps/portmaster/app.conf is not TYPE=bundle"
for v in NAME BIN PKG DESC PM_VERSION PM_URL PM_SHA256; do [ -n "${!v}" ] || die "apps/portmaster/app.conf does not set $v"; done
WANT="${R3I6NOS_PORTMASTER_SHA256:-$PM_SHA256}"

sha() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

ZIP="${R3I6NOS_PORTMASTER_ZIP:-$REPO/vendor/portmaster/PortMaster-$PM_VERSION.zip}"
if [ ! -f "$ZIP" ] || [ "$(sha "$ZIP")" != "$WANT" ]; then
    [ -z "${R3I6NOS_PORTMASTER_ZIP:-}" ] || die "$ZIP does not match sha256 $WANT"
    mkdir -p "$(dirname "$ZIP")"
    echo "[*] Fetching PortMaster $PM_VERSION..."
    curl -fsSL --retry 3 -o "$ZIP.part" "$PM_URL" || { rm -f "$ZIP.part"; die "could not fetch $PM_URL"; }
    mv "$ZIP.part" "$ZIP"
fi
[ "$(sha "$ZIP")" = "$WANT" ] || die "$ZIP does not match the pinned sha256 $WANT (fix apps/portmaster/app.conf only after reading what changed upstream)"
echo "[*] PortMaster.zip matches the pinned sha256"

STAGE="$WORK/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/usr/games" "$STAGE/usr/share/portmaster" "$STAGE/usr/share/doc/portmaster"
python3 -I - "$ZIP" "$STAGE/usr/share/portmaster" <<'PY'
import sys, zipfile, os
zf, dest = sys.argv[1], sys.argv[2]
z = zipfile.ZipFile(zf)
for i in z.infolist():
    n = i.filename
    # absolute names and .. are never extracted
    if n.startswith("/") or ".." in n.split("/") or not n.startswith("PortMaster/"):
        sys.exit("unsafe or unexpected name in the zip: " + n)
z.extractall(dest)
PY
PMD="$STAGE/usr/share/portmaster/PortMaster"
[ -f "$PMD/PortMaster.sh" ] || die "the zip is not the PortMaster release layout"
[ -f "$PMD/pugwash" ] || die "the zip is not the PortMaster release layout"
[ -f "$PMD/utils/pm_launch.txt" ] || die "the zip is not the PortMaster release layout"
[ "$(tr -d '\r\n' < "$PMD/version")" = "$PM_VERSION" ] || die "the zip's version file is not $PM_VERSION"
# arm64 only
find "$PMD" -maxdepth 1 -type f \( -name '*.x86_64' -o -name '*.armhf' -o -name '*.x86_64.so' -o -name '*.armhf.so' \) -exec rm -f {} +
rm -rf "$PMD/libs.x86_64" "$PMD/libs.armhf"
# other platforms' hook folders and files are dead weight here (ours is r3i6nos/)
rm -rf "$PMD/muos" "$PMD/miyoo" "$PMD/spruce" "$PMD/trimui" "$PMD/retrodeck" "$PMD/knulli" "$PMD/batocera"

cp "$APP/$BIN" "$STAGE/usr/games/$BIN"
mkdir -p "$STAGE/usr/share/portmaster/r3i6nos" "$STAGE/usr/share/portmaster/shim"
cp "$APP/r3i6nos/launch_hooks.txt" "$STAGE/usr/share/portmaster/r3i6nos/"
cp "$APP/shim/sudo" "$STAGE/usr/share/portmaster/shim/sudo"
chmod 755 "$STAGE/usr/games/$BIN" "$STAGE/usr/share/portmaster/shim/sudo"
find "$STAGE/usr/share/portmaster/PortMaster" -type d -exec chmod 755 {} +
find "$STAGE/usr/share/portmaster/PortMaster" -type f -exec chmod u+rw,go+r {} +

# The licence terms travel with it: PortMaster's own licenses/ folder is in the tree (its third-party notices); the MIT text, where it came from and
# which version are beside it.
{
  echo "PortMaster $PM_VERSION, the GUI and its scripts: MIT licence (SPDX-License-Identifier: MIT in its files), https://github.com/PortsMaster/PortMaster-GUI."
  echo "Its third-party components' licences are in /opt/native_apps/$PKG/usr/share/portmaster/PortMaster/licenses/."
  echo "Ports it downloads carry their own licences (a licenses/ folder in each); it ships no copyrighted game files."
  echo "r3i6nos adds usr/games/$BIN, r3i6nos/launch_hooks.txt and shim/sudo (the licence of this repository)."
} > "$STAGE/usr/share/doc/portmaster/NOTICE"
cp "$REPO/LICENSE" "$STAGE/usr/share/doc/portmaster/copyright"
printf '%s\n%s\nsha256 %s\n' "$PM_VERSION" "$PM_URL" "$PM_SHA256" > "$STAGE/usr/share/doc/portmaster/SOURCE"

deb_version="${PM_VERSION/-/.}-2"   # -2: the TMPDIR fix (2026-11-18)
size_kb="$(du -sk "$STAGE/usr" | cut -f1)"
mkdir -p "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<CONTROL
Package: $PKG
Version: $deb_version
Architecture: arm64
Maintainer: r3i6nos <noreply@users.noreply.github.com>
Installed-Size: $size_kb
Depends: python3-minimal, libpython3-stdlib, libsdl2-2.0-0
Section: games
Priority: optional
Homepage: https://portmaster.games
Description: $DESC
CONTROL
echo "[*] Staged $STAGE ($size_kb KiB)"
[ "$STAGE_ONLY" = 1 ] && exit 0

mkdir -p "$OUT"
rm -f "$OUT/$NAME.deb"
find "$STAGE" -exec touch -h -d '2026-01-01 00:00:00 UTC' {} + 2>/dev/null || find "$STAGE" -exec touch -h -t 202601010000 {} +
if command -v dpkg-deb >/dev/null 2>&1; then
    dpkg-deb --root-owner-group -Zxz --build "$STAGE" "$OUT/$NAME.deb"
else
    command -v docker >/dev/null 2>&1 || die "neither dpkg-deb nor docker here: use --stage-only, or install one"
    outdir="$(cd "$OUT" && pwd)"; stage_abs="$(cd "$STAGE" && pwd)"
    docker run --rm -v "$stage_abs":/stage:ro -v "$outdir":/out debian:bookworm \
        dpkg-deb --root-owner-group -Zxz --build /stage "/out/$NAME.deb"
fi
echo "[*] Done: $OUT/$NAME.deb ($(du -k "$OUT/$NAME.deb" | cut -f1) KiB)"

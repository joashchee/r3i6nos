#!/bin/bash
# scripts/build-system-package.sh - make the SYSTEM package: the files of the image that are not the kernel or the card's own partitions: the menu, the
# theme, the helpers in /usr/local/bin and the units (docs/PACKAGES-OTA.md, "System updates"). One command, no arguments (CLAUDE.md rule 10's spirit):
#
#   scripts/build-system-package.sh        -> build/system/r3i6nos-system.deb
#
# Its version is scripts/system_version.sh's (<VERSION>.<commit count>), which only grows, so r3i6nos-sysupdate on the unit installs it only if it is newer.
# A system package carries plain files under the paths r3i6nos-sysupdate allows (opt/loveos/menu, opt/loveos/theme, usr/local/bin, etc/systemd/system,
# usr/share/r3i6nos, etc/asound.conf, etc/r3i6nos-release) and nothing else: this script fails on a link or a file outside them. It does NOT carry the
# default apps (they update as apps, from the catalog or a card), the corpus, the kernel, libmali or the bootloader.
#
# A new unit that has to be ENABLED (a symlink in a .wants folder) cannot come this way: that is a reflash item, said so in the roadmap.
# dpkg-deb runs in Debian 12 under Docker when it is not on the machine (a Mac has none). Overrides: R3I6NOS_SYSTEM_OUT (build/system),
# R3I6NOS_MIN_SYSTEM (the VERSION file's value: the lowest base this runs on), R3I6NOS_DPKG ("docker" or "local"; default: local when dpkg-deb exists).
set -eu
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${R3I6NOS_SYSTEM_OUT:-$REPO/build/system}"
NAME=r3i6nos-system
VER="$("$REPO/scripts/system_version.sh")"
MIN="${R3I6NOS_MIN_SYSTEM:-$(tr -d '[:space:]' < "$REPO/VERSION")}"
STAGE="$OUT/stage"
die() { echo "build-system-package: $*" >&2; exit 1; }

rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN" "$STAGE/opt/loveos" "$STAGE/usr/local" "$STAGE/etc/systemd" "$STAGE/usr/share/r3i6nos"
cp -R "$REPO/menu" "$STAGE/opt/loveos/menu"
cp -R "$REPO/theme" "$STAGE/opt/loveos/theme"
cp -R "$REPO/overlay/usr/local/bin" "$STAGE/usr/local/bin"
cp -R "$REPO/overlay/etc/systemd/system" "$STAGE/etc/systemd/system"
[ -f "$REPO/overlay/etc/asound.conf" ] && cp "$REPO/overlay/etc/asound.conf" "$STAGE/etc/asound.conf"
mkdir -p "$STAGE/etc"
"$REPO/scripts/release_file.sh" > "$STAGE/etc/r3i6nos-release"
# no hidden files or caches (an editor's or a tool's folder in a source tree is not part of the system)
find "$STAGE" -mindepth 2 \( -name '.*' -o -name '__pycache__' \) -prune -exec rm -rf {} + 2>/dev/null || true
cat > "$STAGE/usr/share/r3i6nos/package.json" <<JSON
{"name": "$NAME", "kind": "system", "version": "$VER", "min_system": "$MIN"}
JSON

# a system package is plain files: no links (they could point anywhere), nothing outside the allowed roots
if [ -n "$(find "$STAGE" -type l -print -quit)" ]; then die "a link in the package: $(find "$STAGE" -type l -print -quit | sed "s|$STAGE/||")"; fi
bad="$(cd "$STAGE" && find . -mindepth 1 -maxdepth 1 ! -name DEBIAN ! -name opt ! -name usr ! -name etc)"
[ -z "$bad" ] || die "outside the allowed paths: $bad"
find "$STAGE" -type d -exec chmod 755 {} +
find "$STAGE" -type f -exec chmod 644 {} +
chmod 755 "$STAGE"/usr/local/bin/*
chmod 755 "$STAGE/DEBIAN"

SIZE_KB="$(du -sk "$STAGE" | cut -f1)"
cat > "$STAGE/DEBIAN/control" <<CTL
Package: $NAME
Version: $VER
Architecture: all
Maintainer: r3i6nos <noreply@users.noreply.github.com>
Installed-Size: $SIZE_KB
Section: admin
Priority: optional
X-R3i6nos-Kind: system
X-R3i6nos-Min-System: $MIN
Description: r3i6nos system files: the menu, the theme, the helpers and the units
 Installed only by r3i6nos-sysupdate on the unit, only when the signed catalog lists it.
CTL
chmod 644 "$STAGE/DEBIAN/control"

DEB="$OUT/$NAME.deb"
rm -f "$DEB"
if [ "${R3I6NOS_DPKG:-}" = local ] || { [ -z "${R3I6NOS_DPKG:-}" ] && command -v dpkg-deb >/dev/null 2>&1; }; then
    dpkg-deb --root-owner-group -b "$STAGE" "$DEB" >/dev/null
else
    docker run --rm -v "$OUT":/out debian:bookworm dpkg-deb --root-owner-group -b /out/stage "/out/$NAME.deb" >/dev/null \
        || die "dpkg-deb in Docker failed (is Docker running? colima start)"
fi
rm -rf "$STAGE"
echo "built $DEB ($NAME $VER, needs a base of $MIN or newer, $(wc -c < "$DEB" | tr -d ' ') bytes)"

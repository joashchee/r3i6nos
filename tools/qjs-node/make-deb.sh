#!/bin/bash
# make-deb.sh <out.deb> <package> <version> <description> <copyright-file> <notice-file> <binary>...
#
# Packs command-line tools (built by build.sh) as an arm64 .deb that r3i6nos's deb_watcher installs from native_deb: usr/bin/<binary>, the marker
# usr/share/r3i6nos/tool (a tool, not an app: no menu entry; its commands are put on PATH), and the licences in usr/share/doc/<package>. Needs dpkg-deb
# (a Debian container). The copyright file is the app's own: the grant for a proprietary app, its licence for an open one.
set -eu
DEB="${1:?usage: make-deb.sh out.deb package version description copyright notice binary...}"; PKG="$2"; VER="$3"; DESC="$4"; COPY="$5"; NOTICE="$6"; shift 6
[ $# -ge 1 ] || { echo "make-deb: no binary given" >&2; exit 1; }
command -v dpkg-deb >/dev/null || { echo "make-deb: dpkg-deb not installed (run in a Debian container)" >&2; exit 1; }
for b in "$@"; do file "$b" 2>/dev/null | grep -q 'ARM aarch64' || { echo "make-deb: $b is not an arm64 binary" >&2; exit 1; }; done
root="$(mktemp -d)"; trap 'rm -rf "$root"' EXIT
mkdir -p "$root/DEBIAN" "$root/usr/bin" "$root/usr/share/r3i6nos" "$root/usr/share/doc/$PKG"
for b in "$@"; do install -m 0755 "$b" "$root/usr/bin/$(basename "$b")"; done
: > "$root/usr/share/r3i6nos/tool"
cp "$COPY" "$root/usr/share/doc/$PKG/copyright"; cp "$NOTICE" "$root/usr/share/doc/$PKG/NOTICE"
size_kb="$(du -sk "$root/usr" | cut -f1)"
cat > "$root/DEBIAN/control" <<CONTROL
Package: $PKG
Version: $VER
Architecture: arm64
Maintainer: ansiapps <noreply@ansiapps.com>
Installed-Size: $size_kb
Section: utils
Priority: optional
Description: $DESC
CONTROL
find "$root" -exec touch -h -d '2026-01-01 00:00:00 UTC' {} +
mkdir -p "$(dirname "$DEB")"
dpkg-deb --root-owner-group -Zxz --build "$root" "$DEB" >/dev/null
echo "make-deb: $DEB ($(du -k "$DEB" | cut -f1) KiB)"

#!/bin/bash
# tests/test_portmaster.sh: scripts/build-portmaster.sh (hash check, arm64-only trim, layout) and the launcher apps/portmaster/portmaster (first install, no
# downgrade of a self-updated PortMaster, the pad mapping once, refused when the card is not writable) against a FAKE PortMaster release zip. The real
# one is fetched only by the build. Not covered: the .deb pack (needs dpkg-deb or Docker) and PortMaster itself running (needs a unit).
set -eu
cd "$(dirname "$0")/.."
T="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-pm.XXXXXX")"; trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
sha() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

mk_zip() {  # <version> <out zip>
    local d="$T/fake-$1"; rm -rf "$d"; mkdir -p "$d/PortMaster/utils" "$d/PortMaster/libs.x86_64" "$d/PortMaster/libs.aarch64" "$d/PortMaster/licenses" "$d/PortMaster/muos"
    printf '%s\n' "$1" > "$d/PortMaster/version"
    printf '#!/bin/bash\n' > "$d/PortMaster/PortMaster.sh"; printf '#!/usr/bin/env python3\n' > "$d/PortMaster/pugwash"
    printf 'echo launched\n' > "$d/PortMaster/utils/pm_launch.txt"
    : > "$d/PortMaster/7zzs.aarch64"; : > "$d/PortMaster/7zzs.x86_64"; : > "$d/PortMaster/7zzs.armhf"; : > "$d/PortMaster/libinterpose.armhf.so"; : > "$d/PortMaster/libinterpose.aarch64.so"
    : > "$d/PortMaster/libs.x86_64/.gitkeep"; echo mit > "$d/PortMaster/licenses/LICENSE-x.txt"; : > "$d/PortMaster/muos/x"
    printf '030000fd,Other pad,a:b0,\n' > "$d/PortMaster/gamecontrollerdb.txt"
    (cd "$d" && python3 -I -c 'import sys, zipfile, os
z = zipfile.ZipFile(sys.argv[1], "w")
for r, ds, fs in os.walk("PortMaster"):
    for f in fs: z.write(os.path.join(r, f))' "$2")
}
mk_zip 2026.01.01-0001 "$T/pm1.zip"
SHA1="$(sha "$T/pm1.zip")"
export R3I6NOS_PORTMASTER_ZIP="$T/pm1.zip" R3I6NOS_PORTMASTER_SHA256="$SHA1" R3I6NOS_PORTMASTER_WORK="$T/work" R3I6NOS_NATIVE_OUT="$T/out"
# the app.conf pins the real version; the fake zip says another, which the script must refuse
if scripts/build-portmaster.sh --stage-only >/dev/null 2>"$T/e1"; then fail "a zip of another version was accepted"; fi
grep -q 'version file is not' "$T/e1" || fail "no version message: $(cat "$T/e1")"
# the same fake, carrying the pinned version
# shellcheck disable=SC1091  # the app's own manifest
PINNED="$(. apps/portmaster/app.conf; echo "$PM_VERSION")"
mk_zip "$PINNED" "$T/pm2.zip"; SHA2="$(sha "$T/pm2.zip")"
export R3I6NOS_PORTMASTER_ZIP="$T/pm2.zip" R3I6NOS_PORTMASTER_SHA256="$SHA2"
scripts/build-portmaster.sh --stage-only >/dev/null || fail "stage failed"
S="$T/work/stage"; P="$S/usr/share/portmaster/PortMaster"
[ -f "$P/pugwash" ] || fail "arm64 files lost"
[ -f "$P/7zzs.aarch64" ] || fail "arm64 files lost"
[ ! -e "$P/7zzs.x86_64" ] || fail "other-platform files not trimmed"
[ ! -e "$P/7zzs.armhf" ] || fail "other-platform files not trimmed"
[ ! -e "$P/libinterpose.armhf.so" ] || fail "other-platform files not trimmed"
[ ! -e "$P/libs.x86_64" ] || fail "other-platform files not trimmed"
[ ! -e "$P/muos" ] || fail "other-platform files not trimmed"
[ -f "$P/licenses/LICENSE-x.txt" ] || fail "PortMaster's licenses folder removed"
[ -x "$S/usr/games/portmaster" ] || fail "r3i6nos pieces missing"
[ -x "$S/usr/share/portmaster/shim/sudo" ] || fail "r3i6nos pieces missing"
[ -f "$S/usr/share/portmaster/r3i6nos/launch_hooks.txt" ] || fail "r3i6nos pieces missing"
for f in NOTICE copyright SOURCE; do [ -s "$S/usr/share/doc/portmaster/$f" ] || fail "no doc $f"; done
grep -q "MIT" "$S/usr/share/doc/portmaster/NOTICE" || fail "NOTICE does not name the licence"
grep -q "^Package: r36s-portmaster" "$S/DEBIAN/control" || fail "control"
grep -q "^Architecture: arm64" "$S/DEBIAN/control" || fail "control"
# a wrong hash is refused
if R3I6NOS_PORTMASTER_SHA256=0000 scripts/build-portmaster.sh --stage-only >/dev/null 2>&1; then fail "a wrong sha256 was accepted"; fi

# the launcher
export R3I6NOS_PORTMASTER_SRC="$S/usr/share/portmaster" R3I6NOS_ROMS="$T/roms" R3I6NOS_PORTMASTER_NO_RUN=1
mkdir -p "$T/roms"
"$S/usr/games/portmaster" 2>"$T/l1" || fail "launcher failed: $(cat "$T/l1")"
D="$T/roms/ports/PortMaster"
[ -f "$D/pugwash" ] || fail "first start did not install"
[ -f "$D/r3i6nos/launch_hooks.txt" ] || fail "first start did not install"
[ "$(grep -c '^1900bb3e4b4800000011000000010000,' "$D/gamecontrollerdb.txt")" = 1 ] || fail "pad lines not added once"
[ "$(grep -c '^190000004b4800000011000000010000,' "$D/gamecontrollerdb.txt")" = 1 ] || fail "pad lines not added once"
grep -q '^030000fd,Other pad' "$D/gamecontrollerdb.txt" || fail "the shipped controller database was lost"
"$S/usr/games/portmaster" 2>/dev/null; [ "$(grep -c '^1900bb3e' "$D/gamecontrollerdb.txt")" = 1 ] || fail "pad line added twice"
# PortMaster updated itself (newer version in the card's copy, a file it made): an older package must not overwrite it
printf '2027.01.01-0001\n' > "$D/version"; echo mine > "$D/runtime-made"; echo changed > "$D/pugwash"
"$S/usr/games/portmaster" 2>/dev/null
[ "$(cat "$D/version")" = 2027.01.01-0001 ] || fail "an older package overwrote a newer PortMaster"
[ "$(cat "$D/pugwash")" = changed ] || fail "an older package overwrote a newer PortMaster"
[ -f "$D/runtime-made" ] || fail "an older package overwrote a newer PortMaster"
# a newer package does update, and keeps what PortMaster made
printf '2000.01.01-0001\n' > "$D/version"
"$S/usr/games/portmaster" 2>/dev/null
[ "$(cat "$D/version")" = "$PINNED" ] || fail "a newer package did not update, or removed a made file"
[ -f "$D/runtime-made" ] || fail "a newer package did not update, or removed a made file"
# no writable card: refused, said
chmod 555 "$T/roms"
if [ "$(id -u)" -ne 0 ] && R3I6NOS_ROMS="$T/roms" "$S/usr/games/portmaster" 2>"$T/l2"; then fail "started with a read-only card"; fi
[ "$(id -u)" -eq 0 ] || grep -q 'needs the system card writable' "$T/l2" || fail "no message for a read-only card"
chmod 755 "$T/roms"
# the sudo shim must fail (no password prompt for PortMaster)
if "$S/usr/share/portmaster/shim/sudo" true; then fail "the sudo shim succeeded"; fi
# run_app.sh runs a port script from its folder with the sudo shim first on PATH
mkdir -p "$T/ports"
cat > "$T/ports/Game2.sh" <<'IN'
pwd > "$PWD/pwd.out"
if sudo true; then echo yes > "$PWD/sudo.out"; else echo no > "$PWD/sudo.out"; fi
IN
R3I6NOS_PORTMASTER_SHIM="$S/usr/share/portmaster/shim" overlay/usr/local/bin/run_app.sh port "$T/ports/Game2.sh"
[ "$(cat "$T/ports/sudo.out")" = no ] || fail "a port saw a working sudo"
[ "$(cat "$T/ports/pwd.out")" = "$(cd "$T/ports" && pwd -P)" ] || [ "$(cat "$T/ports/pwd.out")" = "$T/ports" ] || fail "a port did not run from its folder"
# no shim found (a wrong path): a failing sudo is still put first
rm -f "$T/ports/sudo.out"
R3I6NOS_PORTMASTER_SHIM="$T/no-such-shim" overlay/usr/local/bin/run_app.sh port "$T/ports/Game2.sh"
[ "$(cat "$T/ports/sudo.out")" = no ] || fail "a port saw a working sudo when the shim was not found"
echo "portmaster: ok"

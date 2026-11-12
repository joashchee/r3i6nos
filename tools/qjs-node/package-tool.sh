#!/bin/bash
# package-tool.sh <app-dir> <tool.conf> <quickjs-2024-01-13.tar.xz> <r3i6nos-dir> [out-dir] [--smoke]
#
# Builds an app's command-line tool for the R36S and packs it as a tool .deb: out-dir/<NAME>.deb (package PKG, arm64, Debian 12). Run as root in a
# Debian 12 container, so the binary links against Bookworm's glibc (the rootfs's own); the app's folder is only read:
#
#   docker run --rm -v "$PWD":/app -v /path/to/r3i6nos:/r3i6nos:ro -w /app debian:bookworm \
#       /r3i6nos/tools/qjs-node/package-tool.sh /app r36s/tool.conf /app/r36s/build/quickjs-2024-01-13.tar.xz /r3i6nos /app/r36s/build/pkg --smoke
#
# tool.conf (sourced) sets NAME (the command), PKG, ENTRY (cli.js), DESC, COPYRIGHT (the licence or grant that ships, relative to the app folder), optionally
# NOTICE (an app NOTICE file to ship), LICENSE_FILE (a licence text appended to the copyright file, for an app that is itself under one), THIRD_PARTY (space-separated files, relative to the app folder, appended to NOTICE: the licence texts of material the app includes, which must travel with it) and RELEASE_VAR (an environment variable that, set to 1, refuses a copyright file that still has its DRAFT GRANT marker).
# --smoke installs the .deb with r3i6nos's own deb_watcher.py and runs the command from the tools folder.
set -eu
APP="$(cd "${1:?usage: package-tool.sh <app-dir> <tool.conf> <quickjs.tar.xz> <r3i6nos-dir> [out-dir] [--smoke]}" && pwd)"
CONF="${2:?usage}"; TAR="${3:?usage}"; R3="${4:?usage}"; OUT="${5:-$APP/r36s/build/pkg}"; SMOKE=""; [ "${6:-}" = "--smoke" ] && SMOKE=1
[ "$OUT" = "--smoke" ] && { OUT="$APP/r36s/build/pkg"; SMOKE=1; }
HERE="$(cd "$(dirname "$0")" && pwd)"
NAME="" PKG="" ENTRY="cli.js" DESC="" COPYRIGHT="" NOTICE="" RELEASE_VAR="" COMMANDS="" LICENSE_FILE="" THIRD_PARTY=""
# shellcheck disable=SC1090  # the app's own manifest
. "$CONF"
die() { echo "package-tool: $*" >&2; exit 1; }
for v in NAME PKG DESC COPYRIGHT; do [ -n "${!v}" ] || die "$CONF does not set $v"; done
[ -f "$APP/$COPYRIGHT" ] || die "no $APP/$COPYRIGHT"
if [ -n "$RELEASE_VAR" ] && [ "${!RELEASE_VAR:-}" = 1 ] && grep -q 'DRAFT GRANT' "$APP/$COPYRIGHT"; then die "$COPYRIGHT still has its DRAFT GRANT marker: the maintainer must confirm the grant first"; fi
[ "$(id -u)" -eq 0 ] || die "run as root (it installs packages); see the header for a docker command"
grep -q '^12' /etc/debian_version 2>/dev/null || die "needs Debian 12 (Bookworm); see the header for a docker command"

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
pkgs="ca-certificates build-essential nodejs dpkg-dev file xz-utils python3 git"
[ -n "$SMOKE" ] && [ "$(uname -m)" != aarch64 ] && pkgs="$pkgs qemu-user-static"
# shellcheck disable=SC2086  # a list of package names
apt-get install -y -qq --no-install-recommends $pkgs >/dev/null
[ "$(uname -m)" = aarch64 ] || die "this builds natively: run it on an arm64 machine or container (an x86 runner cross-builds with CC=aarch64-linux-gnu-gcc; not set up here)"

VERSION="${TOOL_VERSION:-$(node -p "require('$APP/package.json').version")}"
mkdir -p "$OUT/bin"
"$HERE/build.sh" "$APP" "$ENTRY" "$NAME" "$TAR" "$OUT/bin" >/dev/null
echo "[*] $NAME: $(du -k "$OUT/bin/$NAME" | cut -f1) KiB; needs: $(readelf -d "$OUT/bin/$NAME" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | tr '\n' ' ')"
for lib in $(readelf -d "$OUT/bin/$NAME" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
    case "$lib" in libm.so.6|libc.so.6|libdl.so.2|libpthread.so.0|ld-linux-aarch64.so.1) ;; *) die "unexpected shared library $lib" ;; esac
done
# NOTICE: the app's own, then QuickJS's licence (the interpreter is linked in)
{
    [ -n "$NOTICE" ] && [ -f "$APP/$NOTICE" ] && { cat "$APP/$NOTICE"; printf '\n'; }
    for f in $THIRD_PARTY; do [ -f "$APP/$f" ] || die "no $APP/$f (THIRD_PARTY)"; printf -- '\n---- %s ----\n\n' "$f"; cat "$APP/$f"; done
    printf -- '\n---- QuickJS 2024-01-13 (MIT) ----\n'; tar -xJf "$TAR" -O --wildcards '*/LICENSE'
} > "$OUT/NOTICE"
# the copyright file that ships: the app's, then (if it names one) the full licence text
{ cat "$APP/$COPYRIGHT"; [ -n "$LICENSE_FILE" ] && { printf '\n\n---- %s ----\n\n' "$(basename "$LICENSE_FILE")"; cat "$APP/$LICENSE_FILE"; }; } > "$OUT/copyright"
# The device report (docs/DEVICE-REPORTS.md): <command>-report runs the command list on the unit and writes /roms/<App>/r36s-report.txt.
# <App> is the app's name, DESC up to its colon ("Composer: ..."), unless tool.conf sets APP_NAME.
APP_NAME="${APP_NAME:-${DESC%%:*}}"
sed -e "s|@TOOL@|$NAME|; s|@APP@|$APP_NAME|; s|@VERSION@|$VERSION|; s|@PACKAGE@|$PKG|" "$HERE/report.py" > "$OUT/bin/$NAME-report"
chmod 0755 "$OUT/bin/$NAME-report"
share=""
if [ -n "$COMMANDS" ] && [ -f "$APP/$COMMANDS" ]; then cp "$APP/$COMMANDS" "$OUT/commands.txt"; share="$OUT/commands.txt"; fi
# The Rec demo (docs/DEMO-RECORDINGS.md): <command>-demo, with the app's values and the font baked in. Only when the command list has `#@hero` lines (the hero
# features and the captions; see the header of demo.py). The splash's version line is the build: the commit and its date (TOOL_COMMIT / TOOL_DATE override).
demo=""
if [ -n "$share" ] && grep -q '^#@hero ' "$APP/$COMMANDS"; then
    commit="${TOOL_COMMIT:-}"; cdate="${TOOL_DATE:-}"
    if [ -z "$commit" ] && command -v git >/dev/null && git -c safe.directory='*' -C "$APP" rev-parse --git-dir >/dev/null 2>&1; then
        commit="$(git -c safe.directory='*' -C "$APP" rev-parse --short HEAD)"
        [ -z "$(git -c safe.directory='*' -C "$APP" status --porcelain 2>/dev/null)" ] || commit="$commit+dirty"
        cdate="$(git -c safe.directory='*' -C "$APP" log -1 --format=%cs)"
    fi
    python3 "$HERE/demo.py" --bake "$OUT/bin/$NAME-demo" "$NAME" "$APP_NAME" "$VERSION" "$PKG" "${commit:-nogit}" "${cdate:-unknown}" "${DESC#*: }" "$R3/theme/r3i6nos_vga8x16.h"
    demo="$OUT/bin/$NAME-demo"
    # the font is CC BY-SA 4.0: its credit, changes and licence travel with the package (the demo draws with it)
    { printf -- '\n---- IBM VGA 8x16 font (the Rec demo draws with it; CC BY-SA 4.0) ----\n\n'; cat "$R3/theme/font/README.txt"; printf '\n\n'; cat "$R3/theme/font/LICENSE.TXT"; } >> "$OUT/NOTICE"
fi
# shellcheck disable=SC2086  # $demo is empty or one path
MAKE_DEB_SHARE="$share" "$HERE/make-deb.sh" "$OUT/$NAME.deb" "$PKG" "$VERSION" "$DESC" "$OUT/copyright" "$OUT/NOTICE" "$OUT/bin/$NAME" "$OUT/bin/$NAME-report" $demo

if [ -n "$SMOKE" ]; then
    T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
    mkdir -p "$T/card"; cp "$OUT/$NAME.deb" "$T/card/"
    R3I6NOS_DEB_DIR="$T/card" R3I6NOS_DEFAULT_DEB_DIR=/nonexistent R3I6NOS_APP_ROOT="$T/apps" R3I6NOS_LAUNCHER_DIR="$T/launchers" R3I6NOS_TOOLS_DIR="$T/bin" \
        python3 "$R3/overlay/usr/local/bin/deb_watcher.py"
    [ -x "$T/bin/$NAME" ] || die "deb_watcher did not install the command $NAME"
    [ -z "$(ls "$T/launchers" 2>/dev/null)" ] || die "a tool must not get a menu launcher"
    mkdir -p "$T/run/o"
    first=""
    if [ -n "$COMMANDS" ] && [ -f "$APP/$COMMANDS" ]; then first="$(grep -v '^[[:space:]]*\(#\|$\|!SLOW\)' "$APP/$COMMANDS" | head -1 || true)"; fi
    # the first command of the app's list, from a folder with an empty o/ (as the comparison runs them); it must succeed
    # shellcheck disable=SC2086  # $first is a command line's words
    out="$(cd "$T/run" && PATH="$T/bin:$PATH" "$NAME" $first 2>&1)" || die "\`$NAME $first\` failed after install: $out"
    echo "[*] installed by deb_watcher (no menu entry); \`$NAME $first\` ran: $(printf '%s' "$out" | head -1)"
    [ -x "$T/bin/$NAME-report" ] || die "deb_watcher did not install $NAME-report"
    R3I6NOS_ROMS="$T/roms" "$T/bin/$NAME-report" --quiet || die "$NAME-report failed"
    rep="$T/roms/$APP_NAME/r36s-report.txt"
    [ "$(tail -1 "$rep" 2>/dev/null)" = end ] || die "$NAME-report wrote no whole report at $rep"
    echo "[*] $NAME-report: $(grep -A1 '^== summary ==' "$rep" | tail -1) ($(grep -c '^PASS \|^FAIL ' "$rep") checks)"
    if [ -n "$demo" ]; then
        [ -x "$T/bin/$NAME-demo" ] || die "deb_watcher did not install $NAME-demo"
        R3I6NOS_ROMS="$T/roms" R3I6NOS_RUN="$T/run" R3I6NOS_REPORT_POWER_S=0 "$T/bin/$NAME-demo" --quiet --no-video || die "$NAME-demo failed"
        drec="$T/roms/$APP_NAME/Demo/rec-demo.txt"
        [ "$(tail -1 "$drec" 2>/dev/null)" = end ] || die "$NAME-demo wrote no whole log at $drec"
        ls "$T/roms/$APP_NAME/Demo"/01-*.png >/dev/null 2>&1 || die "$NAME-demo made no screenshot"
        echo "[*] $NAME-demo (no video): $(grep -A1 '^== summary ==' "$drec" | tail -1), $(find "$T/roms/$APP_NAME/Demo" -maxdepth 1 -name '*.png' | wc -l | tr -d ' ') screenshots"
    fi
    echo "package-tool: smoke ok"
fi
echo "package-tool: $OUT/$NAME.deb"

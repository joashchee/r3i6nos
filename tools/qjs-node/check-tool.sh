#!/bin/bash
# check-tool.sh <app-dir> <tool.conf> <quickjs-2024-01-13.tar.xz> [out-dir]
#
# Builds the tool natively on this machine and checks it against Node on the app's own command list: the same stdout, stderr, exit status and files
# (PNGs by pixels). This is the proof that the QuickJS build is the same program; it needs Node 22 or later (the apps need it), a C compiler and python3.
# tool.conf (sourced) sets NAME (the command), ENTRY (cli.js) and COMMANDS (a file of command lines, relative to the app folder).
set -eu
APP="$(cd "${1:?usage: check-tool.sh <app-dir> <tool.conf> <quickjs.tar.xz> [out-dir]}" && pwd)"
CONF="${2:?usage}"; TAR="${3:?usage}"; OUT="${4:-$APP/r36s/build}"
HERE="$(cd "$(dirname "$0")" && pwd)"
NAME="" ENTRY="cli.js" COMMANDS=""
# shellcheck disable=SC1090  # the app's own manifest
. "$CONF"
[ -n "$NAME" ] && [ -n "$COMMANDS" ] || { echo "check-tool: $CONF must set NAME and COMMANDS" >&2; exit 1; }
for tool in node cc python3; do command -v "$tool" >/dev/null || { echo "check-tool: needs $tool" >&2; exit 1; }; done
major="$(node -p 'process.versions.node.split(".")[0]')"
[ "$major" -ge 22 ] || { echo "check-tool: needs Node 22 or later (found $(node -v))" >&2; exit 1; }
"$HERE/build.sh" "$APP" "$ENTRY" "$NAME" "$TAR" "$OUT/native" >/dev/null
python3 "$HERE/compare.py" "$APP" "$ENTRY" "$OUT/native/$NAME" "$APP/$COMMANDS"

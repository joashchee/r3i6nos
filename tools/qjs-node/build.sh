#!/bin/bash
# build.sh <app-dir> <entry.js> <name> <quickjs-2024-01-13.tar.xz> [out-dir]
#
# Turns a Node-style command-line program (ES modules, a `cli.js`, node:fs/path/util/zlib/crypto only) into ONE native executable: the program's import
# closure is staged with its `node:` imports pointed at the shims (shim/), compiled to QuickJS BYTECODE by qjsc (so no JavaScript source ships), and
# linked with QuickJS into out-dir/<name>. Nothing of the app is copied into this repo; the app's folder is only read.
#
#   tools/qjs-node/build.sh ~/Coding/composer cli.js compose /path/to/quickjs-2024-01-13.tar.xz build/tools
#
# CC is the compiler for the target (default cc; aarch64-linux-gnu-gcc to cross-build for the R36S), HOSTCC the one that builds qjsc and runs here (default cc).
# The binary needs only libc, libm, libdl and libpthread. Needs node (for the staging script).
set -eu

APP="${1:?usage: build.sh <app-dir> <entry.js> <name> <quickjs.tar.xz> [out-dir]}"
ENTRY="${2:?usage: build.sh <app-dir> <entry.js> <name> <quickjs.tar.xz> [out-dir]}"
NAME="${3:?usage: build.sh <app-dir> <entry.js> <name> <quickjs.tar.xz> [out-dir]}"
TAR="${4:?usage: build.sh <app-dir> <entry.js> <name> <quickjs.tar.xz> [out-dir]}"
OUT="${5:-build/tools}"
HERE="$(cd "$(dirname "$0")" && pwd)"
CC="${CC:-cc}"; HOSTCC="${HOSTCC:-cc}"
QJS_SHA=3c4bf8f895bfa54beb486c8d1218112771ecfc5ac3be1036851ef41568212e03
die() { echo "qjs-node/build: $*" >&2; exit 1; }

command -v node >/dev/null || die "node is needed (it stages the program)"
if command -v sha256sum >/dev/null; then echo "$QJS_SHA  $TAR" | sha256sum -c - >/dev/null || die "QuickJS tarball hash mismatch"
else echo "$QJS_SHA  $TAR" | shasum -a 256 -c - >/dev/null || die "QuickJS tarball hash mismatch"; fi
[ -f "$APP/$ENTRY" ] || die "no $APP/$ENTRY"

mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"   # absolute: the steps below change directory
WORK="$OUT/.work-$NAME"
rm -rf "$WORK"; mkdir -p "$WORK/quickjs"
tar -xJf "$TAR" -C "$WORK/quickjs" --strip-components=1
Q="$WORK/quickjs"
DEFS=(-D_GNU_SOURCE '-DCONFIG_VERSION="2024-01-13"' -DCONFIG_BIGNUM)
SRCS=("$Q/quickjs.c" "$Q/libregexp.c" "$Q/libunicode.c" "$Q/cutils.c" "$Q/libbf.c" "$Q/quickjs-libc.c")

echo "[*] qjsc (runs here)..."
"$HOSTCC" -O2 -w "${DEFS[@]}" -DCONFIG_CC=\"cc\" -DCONFIG_PREFIX=\"/usr/local\" -I"$Q" "$Q/qjsc.c" "${SRCS[@]}" -lm -ldl -lpthread -o "$WORK/qjsc"
echo "[*] Staging $ENTRY of $APP..."
node "$HERE/stage.mjs" "$APP" "$ENTRY" "$WORK/stage"
echo "[*] Compiling to bytecode..."
(cd "$WORK/stage" && "$WORK/qjsc" -e -m -fbignum -o "$WORK/gen.c" main.js)
# qjsc's own main() exits 0 and says nothing when the program throws; ours (main_tail.c) reports it and exits 1
awk '/^int main\(/{exit} {print}' "$WORK/gen.c" > "$WORK/$NAME.c"
cat "$HERE/main_tail.c" >> "$WORK/$NAME.c"
echo "[*] Linking $NAME ($CC)..."
"$CC" -O2 -w "${DEFS[@]}" -I"$Q" "$WORK/$NAME.c" "${SRCS[@]}" -lm -ldl -lpthread -o "$OUT/$NAME"
if command -v "${CC%gcc}strip" >/dev/null 2>&1; then "${CC%gcc}strip" "$OUT/$NAME"; else strip "$OUT/$NAME" 2>/dev/null || true; fi
rm -rf "$WORK"
echo "[*] Done: $OUT/$NAME ($(du -k "$OUT/$NAME" | cut -f1) KiB)"

#!/bin/bash
# scripts/build_port.sh's offline parts: the manifest checks (--validate) and fetch, pin, unpack, patch (--prepare-only),
# against a local tarball over file://. The cross-build itself needs root and a Debian 12 container (CI).
set -eu
cd "$(dirname "$0")/.."

T="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-port.XXXXXX")"
trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
sha() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

# A tiny "upstream": one folder, one file, a licence.
mkdir -p "$T/up/hello-1.0"
printf 'int main(void){return 0;}\n' > "$T/up/hello-1.0/hello.c"
printf 'MIT, say so here\n' > "$T/up/hello-1.0/COPYING"
tar -C "$T/up" -cf "$T/hello.tar" hello-1.0
SHA="$(sha "$T/hello.tar")"

export R3I6NOS_APPS_DIR="$T/apps"
mkdir -p "$T/apps/hello-port/patches"
printf '#!/bin/bash\n' > "$T/apps/hello-port/build.sh"
cat > "$T/apps/hello-port/patches/0001-return-one.patch" <<'EOF'
--- a/hello.c
+++ b/hello.c
@@ -1 +1 @@
-int main(void){return 0;}
+int main(void){return 1;}
EOF

# conf <extra lines...>: a good manifest, with lines appended (a later assignment wins)
conf() {
    cat > "$T/apps/hello-port/app.conf" <<EOF
TYPE=port
NAME=Hello
BIN=hello
PKG=r36s-hello
VERSION=1.0
SOURCE_URL=file://$T/hello.tar
SOURCE_SHA256=$SHA
LICENSE=MIT
LICENSE_FILES="COPYING"
DATA=none
RUNTIME_PKGS="libsdl2-2.0-0"
DESC="Hello"
EOF
    for l in "$@"; do printf '%s\n' "$l" >> "$T/apps/hello-port/app.conf"; done
}
validate() { scripts/build_port.sh hello-port --validate >"$T/out.txt" 2>&1; }

conf
validate || fail "a good manifest was refused: $(cat "$T/out.txt")"

# every refusal
conf 'TYPE=native';                     ! validate || fail "TYPE other than port accepted"
conf 'SOURCE_SHA256=abc';               ! validate || fail "a short sha256 accepted"
conf 'SOURCE_SHA256=';                  ! validate || fail "no sha256 accepted"
conf 'SOURCE_URL=http://example.com/x'; ! validate || fail "plain http accepted"
conf 'LICENSE=';                        ! validate || fail "no licence accepted"
conf 'LICENSE_FILES=';                  ! validate || fail "no licence files accepted"
conf 'DATA=proprietary';                ! validate || fail "DATA=proprietary accepted"
conf 'DATA=';                           ! validate || fail "no DATA accepted"
conf 'VERSION=v1';                      ! validate || fail "VERSION not starting with a digit accepted"
conf 'BUILD_PKGS="libfoo-dev; rm -rf /"'; ! validate || fail "shell text in a package list accepted"
conf 'RUNTIME_PKGS="libnotinthererootfs0"'; ! validate || fail "a runtime package outside the rootfs accepted"
grep -q 'not in the rootfs' "$T/out.txt" || fail "the rootfs refusal does not say why"
conf 'RUNTIME_PKGS="libsdl2"';          ! validate || fail "a prefix of a rootfs package accepted (needs the whole name)"
rm "$T/apps/hello-port/build.sh"
conf;                                   ! validate || fail "a port without build.sh accepted"
printf '#!/bin/bash\n' > "$T/apps/hello-port/build.sh"
conf 'RUNTIME_PKGS=""'
validate || fail "a port with no runtime packages (static) refused: $(cat "$T/out.txt")"

# prepare: fetch, check the pin, unpack with the top folder stripped, apply the patch
conf
scripts/build_port.sh hello-port --prepare-only "$T/work" >"$T/out.txt" 2>&1 || fail "prepare failed: $(cat "$T/out.txt")"
grep -q 'return 1' "$T/work/src/hello.c" || fail "the patch was not applied"
[ -f "$T/work/src/COPYING" ] || fail "the top folder was not stripped"

conf "SOURCE_SHA256=$(printf '0%.0s' $(seq 64))"
! scripts/build_port.sh hello-port --prepare-only "$T/work2" >"$T/out.txt" 2>&1 || fail "a source that does not match its pin was accepted"
grep -q 'pins' "$T/out.txt" || fail "the pin mismatch does not say so"

conf
printf 'garbage that does not apply\n' > "$T/apps/hello-port/patches/0002-bad.patch"
! scripts/build_port.sh hello-port --prepare-only "$T/work3" >"$T/out.txt" 2>&1 || fail "a patch that does not apply was accepted"

echo "build_port: ok"

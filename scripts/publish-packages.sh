#!/bin/bash
# scripts/publish-packages.sh - make the signed package catalog on the maintainer's Mac (docs/PACKAGES-OTA.md, "The catalog"; ROADMAP section 29 phase 3).
#
#   scripts/publish-packages.sh keygen            once: make the Ed25519 signing key (kept OFF the repo, on this Mac) and put its PUBLIC half in the image
#   scripts/publish-packages.sh build [options]   make build/catalog/{index.json,index.json.sig,<name>.deb...}, sign it, verify it with the image's key
#   scripts/publish-packages.sh                   the same as build
#
# build options: --ports NAME,NAME   also publish these ports (scripts/ports.conf names; DEFAULT: none, see below)
#                --system            rebuild the system package first (scripts/build-system-package.sh)
#                FILE.deb ...        publish exactly these files (the stem of each file name is the package's name)
#
# WHAT IS IN IT by default: this repository's own builds (build/native/Commander.deb and IO_Checker_Native.deb) and the system package
# (build/system/r3i6nos-system.deb). The ports (Neumetik, Crunchy, ...) are the maintainer's own apps in their own repositories and some are
# proprietary: a public release would make their files public, so each one is published only when named with --ports.
#
# THE KEY. keygen writes the private key to ${R3I6NOS_KEY:-$HOME/.r3i6nos/packages.key} (mode 600, refuses to overwrite one) and the public key to
# overlay/etc/r3i6nos/packages.pub, which configure_rootfs.sh puts in the image as /etc/r3i6nos/packages.pub. Nothing here puts a private key in the repo,
# the environment or CI (the maintainer's rule: no credential in CI). The key is the root of trust of every system update: lose it and the units already
# flashed can take no more system packages (a reflash with a new public key fixes that); leak it and anyone can send a unit a system update.
#
# build signs with OpenSSL 3 (Ed25519: `openssl pkeyutl -sign -rawin`), which the Mac does not have (LibreSSL), so it runs in Debian 12 under Docker (colima
# start), and then VERIFIES the signature with the PUBLIC key in the repo, the one the image carries, before it says anything is ready. Uploading is by
# hand (`gh release upload`), the command is printed. The catalog is attached to the release tagged `packages` of joashchee/r3i6nos, replaced in place.
#
# Overrides: R3I6NOS_KEY, R3I6NOS_CATALOG_OUT (build/catalog), R3I6NOS_CATALOG_URL (the URL prefix the unit downloads from, default the release above),
# R3I6NOS_NATIVE_BUILD, R3I6NOS_PORTS_ROOT, R3I6NOS_PUBLISH_RUNNER=local (use this machine's openssl 3 and dpkg-deb instead of Docker: CI, the tests).
# Test: tests/test_publish_packages.py.
set -eu
REPO="$(cd "$(dirname "$0")/.." && pwd)"
KEY="${R3I6NOS_KEY:-$HOME/.r3i6nos/packages.key}"
PUB="$REPO/overlay/etc/r3i6nos/packages.pub"
OUT="${R3I6NOS_CATALOG_OUT:-$REPO/build/catalog}"
URL="${R3I6NOS_CATALOG_URL:-https://github.com/joashchee/r3i6nos/releases/download/packages}"
NATIVE_BUILD="${R3I6NOS_NATIVE_BUILD:-$REPO/build/native}"
PORTS_ROOT="${R3I6NOS_PORTS_ROOT:-$(cd "$REPO/.." && pwd)}"
IMAGE="debian:bookworm"
die() { echo "publish-packages: $*" >&2; exit 1; }

mode="${1:-build}"
case "$mode" in keygen|build) [ $# -gt 0 ] && shift ;; -*|*.deb) mode=build ;; *) die "unknown command: $mode (keygen or build)" ;; esac

if [ "$mode" = keygen ]; then
    [ ! -e "$KEY" ] || die "$KEY already exists; not overwriting a signing key (move it away on purpose if you mean to replace it: every unit flashed with the old public key then refuses your system updates)"
    [ ! -e "$PUB" ] || die "$PUB already exists (the image's public key); remove it on purpose if you are replacing the key"
    mkdir -p "$(dirname "$KEY")" "$(dirname "$PUB")"
    chmod 700 "$(dirname "$KEY")"
    OUT_ABS="$(dirname "$KEY")"
    if [ "${R3I6NOS_PUBLISH_RUNNER:-}" = local ]; then
        ( umask 077; openssl genpkey -algorithm ed25519 -out "$KEY" )
        openssl pkey -in "$KEY" -pubout -out "$PUB"
    else
        command -v docker >/dev/null 2>&1 || die "keygen needs OpenSSL 3: install Docker (colima start)"
        docker run --rm -v "$(dirname "$KEY")":/k -v "$(dirname "$PUB")":/p -e "KN=$(basename "$KEY")" "$IMAGE" bash -c \
            'apt-get update -qq >/dev/null && apt-get install -y -qq openssl >/dev/null 2>&1 && umask 077 && openssl genpkey -algorithm ed25519 -out /k/"$KN" && openssl pkey -in /k/"$KN" -pubout -out /p/packages.pub' \
            || die "key generation failed"
    fi
    chmod 600 "$KEY"; chmod 644 "$PUB"
    echo "private key: $KEY  (mode 600; back it up OFFLINE; never commit it)"
    echo "public key:  $PUB  (commit it: configure_rootfs.sh puts it in the image; units flashed before it exists accept no system update)"
    exit 0
fi

# ---- build -------------------------------------------------------------------------------------------------------------------------------
PORT_NAMES=""; SYSTEM=0; FILES=()
while [ $# -gt 0 ]; do
    case "$1" in
        --ports) [ $# -ge 2 ] || die "--ports takes a comma-separated list of port names"; PORT_NAMES="$2"; shift ;;
        --system) SYSTEM=1 ;;
        *.deb) FILES+=("$1") ;;
        *) die "unknown option: $1" ;;
    esac
    shift
done
[ -f "$KEY" ] || die "no signing key at $KEY: run scripts/publish-packages.sh keygen first"
[ -f "$PUB" ] || die "no public key at $PUB: run scripts/publish-packages.sh keygen first (the image carries it)"
[ "$SYSTEM" = 1 ] && "$REPO/scripts/build-system-package.sh"

if [ "${#FILES[@]}" -eq 0 ]; then
    for f in "$NATIVE_BUILD/Commander.deb" "$NATIVE_BUILD/IO_Checker_Native.deb" "$REPO/build/system/r3i6nos-system.deb"; do
        if [ -f "$f" ]; then FILES+=("$f"); else echo "publish-packages: not published (no build): $(basename "$f")" >&2; fi
    done
    if [ -n "$PORT_NAMES" ]; then
        # shellcheck source=/dev/null  # scripts/ports.conf: data, read here
        . "$REPO/scripts/ports.conf"
        IFS=',' read -r -a wanted <<<"$PORT_NAMES"
        for w in "${wanted[@]}"; do
            found=0
            for entry in "${PORTS[@]}"; do
                IFS='|' read -r name dir kind rel _how <<<"$entry"
                [ "$name" = "$w" ] || continue
                found=1
                [ "$kind" = deb ] || die "$w is a LÖVE port, not a .deb (the catalog lists packages; .love files are pushed with push-packages.sh)"
                f="$PORTS_ROOT/$dir/$rel"
                [ -f "$f" ] || die "$w has no build at $f (scripts/build-ports.sh)"
                FILES+=("$f")
            done
            [ "$found" = 1 ] || die "no port called $w in scripts/ports.conf"
        done
    fi
fi
[ "${#FILES[@]}" -gt 0 ] || die "nothing to publish: no builds found (scripts/build-native-apps or scripts/build-system-package.sh)"
for f in "${FILES[@]}"; do [ -f "$f" ] || die "no such file: $f"; done

rm -rf "$OUT"
mkdir -p "$OUT"
OUT_ABS="$(cd "$OUT" && pwd)"
names=()
for f in "${FILES[@]}"; do
    cp "$f" "$OUT_ABS/$(basename "$f")"
    names+=("$OUT_ABS/$(basename "$f")")
done

# build the index, sign it, and verify it with the PUBLIC key in the repo (the image's), all in one place with OpenSSL 3 and dpkg-deb
# shellcheck disable=SC2016  # single quotes on purpose: the variables expand in the shell that runs this, which has OUT, URL, KEY and PUB
steps='set -eu
command -v openssl >/dev/null 2>&1 && command -v python3 >/dev/null 2>&1 || { apt-get update -qq >/dev/null && apt-get install -y -qq python3 openssl >/dev/null 2>&1; }
python3 -I scripts/catalog.py "$OUT/index.json" "$URL" "$@"
openssl pkeyutl -sign -inkey "$KEY" -rawin -in "$OUT/index.json" -out "$OUT/index.json.sig"
openssl pkeyutl -verify -pubin -inkey "$PUB" -rawin -in "$OUT/index.json" -sigfile "$OUT/index.json.sig" >/dev/null || { echo "THE SIGNATURE DOES NOT VERIFY WITH $PUB: the private key is not the one whose public half is in the image" >&2; exit 1; }'
export OUT="$OUT_ABS" URL KEY PUB
if [ "${R3I6NOS_PUBLISH_RUNNER:-}" = local ]; then
    bash -c "$steps" _ "${names[@]}" || die "building or signing the catalog failed"
else
    command -v docker >/dev/null 2>&1 || die "this needs Docker (colima start)"
    docker run --rm -v "$REPO":"$REPO" -v "$(dirname "$KEY")":"$(dirname "$KEY")":ro -v "$OUT_ABS":"$OUT_ABS" -w "$REPO" \
        -e OUT -e URL -e KEY -e PUB "$IMAGE" bash -c "$steps" _ "${names[@]}" || die "building or signing the catalog failed"
fi

echo
echo "Ready in $OUT_ABS (signed, and verified with $PUB):"
for f in "$OUT_ABS"/index.json "$OUT_ABS"/index.json.sig "$OUT_ABS"/*.deb; do printf '  %-28s %10d bytes\n' "$(basename "$f")" "$(wc -c < "$f" | tr -d ' ')"; done
echo
echo "Upload by hand (the release is tagged \`packages\`; --clobber replaces the files in place):"
echo "  gh release view packages --repo joashchee/r3i6nos >/dev/null 2>&1 || gh release create packages --repo joashchee/r3i6nos --title 'Packages' --notes 'The signed package catalog (scripts/publish-packages.sh).'"
echo "  gh release upload packages --repo joashchee/r3i6nos --clobber '$OUT_ABS'/index.json '$OUT_ABS'/index.json.sig '$OUT_ABS'/*.deb"

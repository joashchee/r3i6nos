#!/bin/bash
# scripts/build-nttplay.sh [/path/to/neumetik]
#
# Builds nttplay (Neumetik's players as a command-line tool, ROADMAP section 27) for the image on this computer: build/nttplay/nttplay
# (arm64, stripped, links only libc, libm and libgcc_s), plus build/nttplay/SOURCE (the Neumetik commit it was built from) and build/nttplay/NOTICE (the
# third-party list Neumetik's package ships, written by its package/notices.py). The code is the
# Neumetik repository's (r36s/lib/src/bin/nttplay.rs; ansiapps' own, proprietary, so it is never copied into this repo): the checkout beside this one,
# else the argument, else NEUMETIK_DIR. It needs only Docker (Colima on an Apple chip). The work runs as root in rust:1.98-bookworm (the same
# Debian 12 arm64 as the unit's rootfs), where the binary is also run once: --version, a probe and a decode of a file it makes itself.
# scripts/configure_rootfs.sh puts it at /usr/bin/nttplay.
set -eu
die() { echo "build-nttplay: $*" >&2; exit 1; }
REPO="$(cd "$(dirname "$0")/.." && pwd)"
NT_IN="${1:-${NEUMETIK_DIR:-$REPO/../neumetik}}"
[ -f "$NT_IN/r36s/lib/src/bin/nttplay.rs" ] || die "no Neumetik checkout with r36s/lib/src/bin/nttplay.rs at $NT_IN (pass its path, or set NEUMETIK_DIR)"
NT="$(cd "$NT_IN" && pwd)"
command -v docker >/dev/null || die "Docker is not installed (brew install colima docker, then colima start)"
# Docker Desktop's leftover config names a credential helper that is not installed without it: pulls then fail. Use an empty config.
if [ -z "${DOCKER_CONFIG:-}" ] && grep -q '"credsStore"[[:space:]]*:[[:space:]]*"desktop"' "$HOME/.docker/config.json" 2>/dev/null \
    && ! command -v docker-credential-desktop >/dev/null; then
    if [ -z "${DOCKER_HOST:-}" ]; then
        DOCKER_HOST="$(docker context inspect --format '{{.Endpoints.docker.Host}}' 2>/dev/null || true)"
        [ -n "$DOCKER_HOST" ] && export DOCKER_HOST
    fi
    DOCKER_CONFIG="$(mktemp -d "${TMPDIR:-/tmp}/nttplay-docker.XXXXXX")"
    echo '{}' > "$DOCKER_CONFIG/config.json"
    export DOCKER_CONFIG
    trap 'rm -rf "$DOCKER_CONFIG"' EXIT
fi
docker info >/dev/null 2>&1 || die "Docker is not running (colima start)"

OUT="$REPO/build/nttplay"
mkdir -p "$OUT"
rm -f "$OUT/nttplay" "$OUT/SOURCE" "$OUT/NOTICE"
COMMIT="$(git -C "$NT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
[ -z "$(git -C "$NT" status --porcelain -- r36s midi bgn tracker visual 2>/dev/null)" ] || COMMIT="$COMMIT+dirty"
DATE="$(git -C "$NT" log -1 --format=%cs 2>/dev/null || echo undated)"
echo "[*] Building nttplay (Neumetik $COMMIT) in Debian 12 (arm64) under Docker ..."
# The checkout is mounted read-only; cargo's target directory is the output folder's own.
docker run --rm --platform linux/arm64 -v "$NT":/src:ro -v "$OUT":/out -w /src -e CARGO_TARGET_DIR=/out/target rust:1.98-bookworm bash -eu -c '
    cargo build -q --release --bin nttplay --manifest-path /src/r36s/lib/Cargo.toml
    install -m 755 /out/target/release/nttplay /out/nttplay
    strip /out/nttplay
    rm -rf /out/target
    # what it links: nothing but the base system
    bad="$(ldd /out/nttplay | awk "{print \$1}" | grep -Ev "^(linux-vdso|libc\.so|libm\.so|libgcc_s\.so|/lib/ld-linux|libdl\.so|libpthread\.so)" || true)"
    [ -z "$bad" ] || { echo "nttplay links more than the base system: $bad" >&2; exit 1; }
    /out/nttplay --version
    # a file it makes itself: a one-note MIDI (format 0, one track), probed and decoded
    printf "MThd\0\0\0\6\0\0\0\1\1\340MTrk\0\0\0\15\0\220\74\100\203\140\200\74\100\0\377\57\0" > /tmp/one.mid
    /out/nttplay probe /tmp/one.mid | grep -qx "kind=midi"
    n="$(/out/nttplay decode /tmp/one.mid -loop -t 1 | wc -c | tr -d " ")"
    [ "$n" = 176400 ] || { echo "decode -loop -t 1 gave $n bytes, not 176400" >&2; exit 1; }
    rc=0; /out/nttplay decode /tmp/nothing-here.mid >/dev/null 2>&1 || rc=$?
    [ "$rc" = 1 ] || { echo "a missing file gave exit $rc, not 1" >&2; exit 1; }
' || die "the build or its checks failed"
[ -s "$OUT/nttplay" ] || die "the build finished but there is no build/nttplay/nttplay"
echo "neumetik $COMMIT $DATE" > "$OUT/SOURCE"
# NOTICE: the same third-party list the Neumetik package ships (the ported code its replayers carry, with the licences), written by Neumetik's own notices.py
rm -f "$OUT/NOTICE"
command -v python3 >/dev/null || die "python3 is needed to write NOTICE"
python3 -I "$NT/r36s/package/notices.py" "$REPO" "$OUT/NOTICE" || die "could not write NOTICE"
[ -s "$OUT/NOTICE" ] || die "NOTICE is empty"
echo "[*] Built: $OUT/nttplay ($(du -h "$OUT/nttplay" | cut -f1)), from Neumetik $COMMIT"

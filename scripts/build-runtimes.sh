#!/bin/bash
# scripts/build-runtimes.sh [--stage-only] [name ...]
#
# Makes build/runtimes/<package>.deb for each game-engine runtime in tools/runtimes/<name>/runtime.conf (Pyxel, Ren'Py, Solarus; docs/RUNTIMES.md,
# ROADMAP section 33). Each is PortMaster's runtime squashfs (fetched from the pinned commit of PortMaster-New, checked against the sha256 in its manifest),
# EXTRACTED here (unsquashfs, so the kernel's squashfs options do not matter), trimmed, and packed as a "tool" package: deb_watcher.py puts the tree
# under /opt/native_apps/<package>/usr/lib/r3i6nos-runtimes/<name>/ and the command (r3i6nos-run-<name>) on the PATH. No root, no loop mount, no sudo.
#
#   --stage-only   stop after the trees are made (build/runtimes/stage/<name>), no .deb: for machines with neither dpkg-deb nor Docker.
#   name ...       only these runtimes (default: all).
# Extraction uses unsquashfs, or Docker (debian:bookworm + squashfs-tools) when there is none; packing uses dpkg-deb or Docker.
# Overrides (tests): R3I6NOS_RUNTIMES_SRC_DIR (where the squashfs files are cached; default vendor/runtimes), R3I6NOS_RUNTIMES_TREES (a folder of
# already-extracted trees, <name>/, used instead of fetching and extracting; the sha256 is not checked then), R3I6NOS_RUNTIMES_WORK (build/runtimes),
# R3I6NOS_RUNTIMES_OUT (build/runtimes), R3I6NOS_RUNTIMES_DIR (tools/runtimes), R3I6NOS_RUNTIMES_NO_FETCH (never fetch: a missing or wrong file is an error).
set -eu
die() { echo "build-runtimes: $*" >&2; exit 1; }
REPO="$(cd "$(dirname "$0")/.." && pwd)"
DEFS="${R3I6NOS_RUNTIMES_DIR:-$REPO/tools/runtimes}"
SRCDIR="${R3I6NOS_RUNTIMES_SRC_DIR:-$REPO/vendor/runtimes}"
WORK="${R3I6NOS_RUNTIMES_WORK:-$REPO/build/runtimes}"
OUT="${R3I6NOS_RUNTIMES_OUT:-$REPO/build/runtimes}"
TREES="${R3I6NOS_RUNTIMES_TREES:-}"
STAGE_ONLY=0
NAMES=()
for a in "$@"; do
    case "$a" in
        --stage-only) STAGE_ONLY=1 ;;
        -*) die "unknown option: $a" ;;
        *) NAMES+=("$a") ;;
    esac
done
if [ ${#NAMES[@]} -eq 0 ]; then
    for d in "$DEFS"/*/; do [ -f "${d}runtime.conf" ] && NAMES+=("$(basename "$d")"); done
fi
[ ${#NAMES[@]} -gt 0 ] || die "no runtime in $DEFS"

sha() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
absdir() { (cd "$1" && pwd); }

unsquash() {  # <squashfs> <dest (not existing)>
    local src="$1" dest="$2"
    if command -v unsquashfs >/dev/null 2>&1; then
        unsquashfs -q -no-xattrs -d "$dest" "$src" >/dev/null
    else
        command -v docker >/dev/null 2>&1 || die "neither unsquashfs nor docker here: install squashfs-tools (brew install squashfs) or start Docker"
        mkdir -p "$(dirname "$dest")"
        local sd dd; sd="$(absdir "$(dirname "$src")")"; dd="$(absdir "$(dirname "$dest")")"
        docker run --rm -v "$sd":/in:ro -v "$dd":/out debian:bookworm sh -c \
            "apt-get update -qq >/dev/null && apt-get install -y -qq --no-install-recommends squashfs-tools >/dev/null && unsquashfs -q -no-xattrs -d '/out/$(basename "$dest")' '/in/$(basename "$src")' >/dev/null && chmod -R a+rX '/out/$(basename "$dest")'" \
            || die "unsquashfs in Docker failed for $src"
    fi
}

is_aarch64_elf() {  # <file>: ELF, little endian, e_machine 183 (AArch64)
    python3 -I - "$1" <<'PY'
import sys
h = open(sys.argv[1], "rb").read(20)
sys.exit(0 if h[:4] == b"\x7fELF" and h[5] == 1 and int.from_bytes(h[18:20], "little") == 183 else 1)
PY
}

mkdir -p "$WORK" "$OUT"
for name in "${NAMES[@]}"; do
    conf="$DEFS/$name/runtime.conf"
    [ -f "$conf" ] || die "no $conf"
    NAME="" PKG="" VERSION="" CMD="" SRC_FILE="" SRC_URL="" SRC_SHA256="" KEEP="" DROP="" ENTRY_ELF="" LICENSE_SPDX="" LICENSE_FILES="" SOURCE_URL="" DEPENDS="" DESC=""
    # shellcheck disable=SC1090  # the runtime's own manifest
    . "$conf"
    for v in NAME PKG VERSION CMD SRC_FILE SRC_URL SRC_SHA256 ENTRY_ELF LICENSE_SPDX SOURCE_URL DEPENDS DESC; do [ -n "${!v}" ] || die "$conf does not set $v"; done
    [ "$NAME" = "$name" ] || die "$conf: NAME is $NAME, not $name"
    [ -x "$DEFS/$name/run" ] || die "$DEFS/$name/run is missing or not executable"

    X="$WORK/extract/$name"
    rm -rf "$X"
    if [ -n "$TREES" ]; then
        [ -d "$TREES/$name" ] || die "R3I6NOS_RUNTIMES_TREES has no $name/"
        mkdir -p "$(dirname "$X")"; cp -R "$TREES/$name" "$X"
    else
        f="$SRCDIR/$SRC_FILE"
        if [ ! -f "$f" ] || [ "$(sha "$f")" != "$SRC_SHA256" ]; then
            [ -z "${R3I6NOS_RUNTIMES_NO_FETCH:-}" ] || die "$f is missing or does not match the pinned sha256 $SRC_SHA256 (R3I6NOS_RUNTIMES_NO_FETCH is set: not fetching)"
            mkdir -p "$SRCDIR"
            echo "[*] Fetching $SRC_FILE..."
            curl -fsSL --retry 3 -o "$f.part" "$SRC_URL" || { rm -f "$f.part"; die "could not fetch $SRC_URL"; }
            mv "$f.part" "$f"
        fi
        [ "$(sha "$f")" = "$SRC_SHA256" ] || die "$f does not match the pinned sha256 $SRC_SHA256 (fix tools/runtimes/$name/runtime.conf only after reading what changed upstream)"
        echo "[*] $SRC_FILE matches the pinned sha256"
        mkdir -p "$WORK/extract"
        unsquash "$f" "$X"
    fi

    # ---- the runtime's tree: a whitelist (KEEP) or everything minus DROP
    STAGE="$WORK/stage/$name"
    RT="$STAGE/usr/lib/r3i6nos-runtimes/$name"
    rm -rf "$STAGE"
    mkdir -p "$RT" "$STAGE/usr/bin" "$STAGE/usr/share/r3i6nos" "$STAGE/usr/share/doc/$PKG"
    if [ -n "$KEEP" ]; then
        for pair in $KEEP; do
            from="${pair%%:*}" to="${pair#*:}"
            case "$from$to" in /*|*..*) die "unsafe path in KEEP: $pair" ;; esac
            [ -e "$X/$from" ] || die "$name: $from is not in the runtime (upstream changed?)"
            mkdir -p "$RT/$(dirname "$to")"
            cp -R "$X/$from" "$RT/$to"
        done
    else
        cp -R "$X/." "$RT/"
        for d in $DROP; do
            case "$d" in /*|*..*) die "unsafe path in DROP: $d" ;; esac
            rm -rf "${RT:?}/$d"
        done
    fi
    find "$RT" -name '__pycache__' -type d -prune -exec rm -rf {} + 2>/dev/null || true
    find "$RT" -type d -exec chmod 755 {} +
    find "$RT" -type f -exec chmod u+rw,go+r {} +
    # no symlink may leave the runtime (a squashfs from upstream is not trusted to be tidy)
    while IFS= read -r -d '' l; do
        t="$(readlink "$l")"
        case "$t" in /*|*../..*) die "$name: symlink $l -> $t leaves the runtime" ;; esac
    done < <(find "$RT" -type l -print0)
    [ -f "$RT/$ENTRY_ELF" ] || die "$name: $ENTRY_ELF is missing after the trim"
    is_aarch64_elf "$RT/$ENTRY_ELF" || die "$name: $ENTRY_ELF is not an aarch64 ELF (this image is arm64 only)"
    chmod 755 "$RT/$ENTRY_ELF" 2>/dev/null || true

    # ---- the command, the tool marker, the notices
    cp "$DEFS/$name/run" "$STAGE/usr/bin/$CMD"; chmod 755 "$STAGE/usr/bin/$CMD"
    printf '%s\n' "$PKG" > "$STAGE/usr/share/r3i6nos/tool"
    {
        echo "$NAME $VERSION: $LICENSE_SPDX. Source: $SOURCE_URL"
        echo "This package is PortMaster's runtime (https://github.com/PortsMaster/PortMaster-New, MIT) for $NAME, extracted and trimmed, with r3i6nos's command $CMD."
        echo "Fetched from: $SRC_URL (sha256 $SRC_SHA256)."
        echo "What was removed: ${KEEP:+everything but $KEEP}${DROP:+$DROP}."
        echo "The licence files of the engine are in /opt/native_apps/$PKG/usr/lib/r3i6nos-runtimes/$NAME/ (${LICENSE_FILES:-none shipped by the upstream runtime: see the source above}); the GPL's source is at the Source URL, tag $VERSION."
    } > "$STAGE/usr/share/doc/$PKG/NOTICE"
    cp "$REPO/LICENSE" "$STAGE/usr/share/doc/$PKG/copyright"
    printf '%s\n%s\nsha256 %s\n' "$VERSION" "$SRC_URL" "$SRC_SHA256" > "$STAGE/usr/share/doc/$PKG/SOURCE"
    for lf in $LICENSE_FILES; do
        [ -s "$RT/$lf" ] || die "$name: licence file $lf is missing from the tree"
    done

    size_kb="$(du -sk "$STAGE/usr" | cut -f1)"
    mkdir -p "$STAGE/DEBIAN"
    cat > "$STAGE/DEBIAN/control" <<CONTROL
Package: $PKG
Version: $VERSION-r3i6nos1
Architecture: arm64
Maintainer: r3i6nos <noreply@users.noreply.github.com>
Installed-Size: $size_kb
Depends: $DEPENDS
Section: games
Priority: optional
Homepage: $SOURCE_URL
Description: $DESC
CONTROL
    echo "[*] Staged $STAGE ($size_kb KiB)"
    [ "$STAGE_ONLY" = 1 ] && continue

    find "$STAGE" -exec touch -h -d '2026-01-01 00:00:00 UTC' {} + 2>/dev/null || find "$STAGE" -exec touch -h -t 202601010000 {} +
    rm -f "$OUT/$PKG.deb"
    if command -v dpkg-deb >/dev/null 2>&1; then
        dpkg-deb --root-owner-group -Zxz --build "$STAGE" "$OUT/$PKG.deb"
    else
        command -v docker >/dev/null 2>&1 || die "neither dpkg-deb nor docker here: use --stage-only, or install one"
        docker run --rm -v "$(absdir "$STAGE")":/stage:ro -v "$(absdir "$OUT")":/out debian:bookworm \
            dpkg-deb --root-owner-group -Zxz --build /stage "/out/$PKG.deb"
    fi
    echo "[*] Done: $OUT/$PKG.deb ($(du -k "$OUT/$PKG.deb" | cut -f1) KiB)"
done

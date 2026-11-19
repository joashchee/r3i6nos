#!/bin/bash
# tools/streamtools/build.sh MODE [tool ...]: builds the stream-tool packages (docs/STREAM-TOOLS.md, ROADMAP section 34). Runs as root INSIDE a Debian 12 arm64 container
# (the image's own distribution and architecture); scripts/build-streamtools.sh starts it. Tools: ytdlp, gallery-dl, streamlink (default: all).
#
#   build     (default) fetch the pinned sources, check every hash, build the packages into $OUT (/out): r3i6nos-ytdlp.deb, r3i6nos-gallery-dl.deb, r3i6nos-streamlink.deb
#   relock    resolve the pinned Python packages for Debian 12's Python 3.11 on arm64 and rewrite tools/streamtools/lock/<tool>.txt (every dependency, with sha256s)
#   latest    print the newest upstream versions and the yt-dlp zipapp's sha256 as lines for pins.conf; changes nothing
#
# Each package is a TOOL package (usr/share/r3i6nos/tool: deb_watcher.py puts the tree under /opt/native_apps/<package>/ and the command on the tools PATH). The Python
# code is byte-compiled at build time (unchecked-hash .pyc), because the install folder is read-only for the user who runs it and Python would otherwise recompile
# yt-dlp's 1,000 files on every start (a zip cannot keep a cache).
set -eu
die() { echo "streamtools: $*" >&2; exit 1; }
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
# shellcheck disable=SC1091  # the pins
. "$HERE/pins.conf"
MODE="${1:-build}"; [ $# -gt 0 ] && shift
OUT="${OUT:-/out}"
export DEBIAN_FRONTEND=noninteractive LC_ALL=C PIP_DISABLE_PIP_VERSION_CHECK=1 PYTHONDONTWRITEBYTECODE=1
if [ "$(id -u)" -ne 0 ] || ! grep -q '^12' /etc/debian_version 2>/dev/null; then die "run as root in Debian 12 (scripts/build-streamtools.sh does)"; fi
TOOLS=("$@"); [ ${#TOOLS[@]} -gt 0 ] || TOOLS=(ytdlp gallery-dl streamlink)
for t in "${TOOLS[@]}"; do case "$t" in ytdlp|gallery-dl|streamlink) ;; *) die "unknown tool: $t (ytdlp, gallery-dl, streamlink)" ;; esac; done
has() { for t in "${TOOLS[@]}"; do [ "$t" = "$1" ] && return 0; done; return 1; }

apt-get update -qq >/dev/null
apt-get install -y -qq --no-install-recommends python3 python3-pip ca-certificates curl binutils cmake gcc libc6-dev make dpkg >/dev/null 2>&1 || die "apt install failed"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
fetch() { # url dest sha256
    curl -fsSL --retry 3 -o "$2" "$1" || die "could not fetch $1"
    echo "$3  $2" | sha256sum -c - >/dev/null || die "$1 does not match the pinned sha256 $3 (change pins.conf only after reading what changed upstream)"
}
pybin() { python3 -I "$@"; }

pip_top() { # tool -> its top-level requirements
    case "$1" in ytdlp) echo "$YTDLP_PIP" ;; gallery-dl) echo "$GALLERYDL_PIP" ;; streamlink) echo "$STREAMLINK_PIP" ;; esac
}

# ---------------------------------------------------------------- latest
if [ "$MODE" = latest ]; then
    api() { curl -fsSL "$1"; }
    tag="$(api https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest | python3 -c 'import json,sys; print(json.load(sys.stdin)["tag_name"])')"
    sums="$(api "https://github.com/yt-dlp/yt-dlp/releases/download/$tag/SHA2-256SUMS")"
    sha="$(echo "$sums" | awk '$2=="yt-dlp"{print $1}')"
    echo "YTDLP_VERSION=$tag"
    echo "YTDLP_URL=https://github.com/yt-dlp/yt-dlp/releases/download/$tag/yt-dlp"
    echo "YTDLP_SHA256=$sha   # from the release's own SHA2-256SUMS"
    qtag="$(api https://api.github.com/repos/quickjs-ng/quickjs/releases/latest | python3 -c 'import json,sys; print(json.load(sys.stdin)["tag_name"])')"
    echo "QJS_VERSION=${qtag#v}   # then fetch $qtag's source tarball and put its sha256 in QJS_SHA256 (GitHub's archive, not a release asset)"
    for p in yt-dlp-ejs pycryptodomex gallery-dl streamlink; do
        v="$(api "https://pypi.org/pypi/$p/json" | python3 -c 'import json,sys; print(json.load(sys.stdin)["info"]["version"])')"
        echo "# $p $v"
    done
    echo "# (now: yt-dlp $YTDLP_VERSION, quickjs-ng $QJS_VERSION, $GALLERYDL_PIP, $STREAMLINK_PIP)"
    exit 0
fi

# ---------------------------------------------------------------- relock
if [ "$MODE" = relock ]; then
    mkdir -p "$HERE/lock"
    for t in "${TOOLS[@]}"; do
        wh="$W/wh-$t"; mkdir -p "$wh"
        # shellcheck disable=SC2046  # the top-level requirements are words
        python3 -m pip download -q --only-binary=:all: -d "$wh" $(pip_top "$t") >"$W/pip-$t.log" 2>&1 || { tail -20 "$W/pip-$t.log"; die "pip could not resolve $t"; }
        {
            echo "# $t: every Python package the tool needs, for Debian 12's Python 3.11 on arm64, with the sha256 of the wheel. Made by scripts/build-streamtools.sh --relock; do not edit."
            for w in "$wh"/*.whl; do
                f="$(basename "$w")"
                n="${f%%-*}"; rest="${f#*-}"; v="${rest%%-*}"
                n="$(echo "$n" | tr 'A-Z_' 'a-z-')"
                echo "$n==$v --hash=sha256:$(sha256sum "$wh/$f" | cut -d' ' -f1)"
            done
        } > "$HERE/lock/$t.txt"
        echo "[*] lock/$t.txt: $(grep -c '==' "$HERE/lock/$t.txt") packages"
    done
    exit 0
fi
[ "$MODE" = build ] || die "unknown mode: $MODE (build, relock, latest)"

# ---------------------------------------------------------------- build
mkdir -p "$OUT"
# a fixed time stamp on every file and in the archive (dpkg-deb uses SOURCE_DATE_EPOCH for the ar members and for clamping the tar's times), so two builds of the same pins are
# byte-identical (2026-01-01 00:00:00 UTC, as build-runtimes.sh uses for its files)
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-1767225600}"
BUILD_DATE="@$SOURCE_DATE_EPOCH"

pip_tree() { # tool destdir: the locked packages, installed with no dependency resolution and no network beyond the pinned hashes
    local t="$1" d="$2"
    [ -s "$HERE/lock/$t.txt" ] || die "no lock/$t.txt (scripts/build-streamtools.sh --relock)"
    mkdir -p "$d"
    python3 -m pip install -q --no-deps --require-hashes --only-binary=:all: --no-compile --break-system-packages --target "$d" -r "$HERE/lock/$t.txt" >"$W/pipi-$t.log" 2>&1 \
        || { tail -20 "$W/pipi-$t.log"; die "pip install of $t failed (a hash that does not match is an error)"; }
}
trim_tree() { # dir: what a read-only runtime copy does not need
    local d="$1"
    rm -rf "${d:?}/bin" "${d:?}/share"
    find "$d" -name __pycache__ -type d -prune -exec rm -rf {} +
    find "$d" -type d \( -name tests -o -name _tests -o -name SelfTest \) -prune -exec rm -rf {} +
    find "$d" -type f \( -name '*.pyi' -o -name 'py.typed' -o -name '*.pxd' -o -name '*.pyx' -o -name '*.pxi' -o -name '*.h' -o -name '*.c' -o -name '*.pth' \) -delete
    rm -rf "$d/lxml/includes"
    rm -rf "$d/pycountry/locales"         # 21 MiB of translations of country names; the code falls back to English (checked by the test)
}
compile_tree() { # dir destdir: unchecked-hash .pyc next to the sources (__pycache__), the build path replaced by the installed one in tracebacks
    pybin -m compileall -q -j0 --invalidation-mode unchecked-hash -d "$2" "$1" >/dev/null || die "byte-compiling $1 failed"
}
third_party() { # tree docdir: a licence line per Python package, and each package's licence files
    pybin - "$1" "$2" <<'PY'
import email, os, shutil, sys
tree, doc = sys.argv[1:3]
rows = []
os.makedirs(os.path.join(doc, "licenses"), exist_ok=True)
for d in sorted(os.listdir(tree)):
    if not d.endswith(".dist-info"):
        continue
    p = os.path.join(tree, d)
    md = email.message_from_file(open(os.path.join(p, "METADATA"), encoding="utf-8"))
    lic = md.get("License-Expression") or ""
    if not lic:
        first = (md.get("License") or "").strip().splitlines()[:1]
        lic = first[0] if first and len(first[0]) <= 60 else ""
    if not lic:
        cl = [c.split("::")[-1].strip() for c in (md.get_all("Classifier") or []) if c.startswith("License ::")]
        lic = ", ".join(cl) or "see its licence file"
    rows.append(f"{md['Name']} {md['Version']}: {lic}")
    for root, _, files in os.walk(p):
        for f in files:
            if any(k in f.upper() for k in ("LICENSE", "LICENCE", "COPYING", "NOTICE", "AUTHORS")):
                dst = os.path.join(doc, "licenses", md["Name"].replace("/", "_") + "-" + f)
                shutil.copyfile(os.path.join(root, f), dst)
open(os.path.join(doc, "THIRD-PARTY.txt"), "w").write("Python packages in this package (name version: licence), from their own metadata; their licence files are in licenses/.\n" + "\n".join(rows) + "\n")
PY
}
make_deb() { # stagedir pkg version section description
    local st="$1" pkg="$2" ver="$3" desc="$5"
    local kb; kb="$(du -sk "$st/usr" | cut -f1)"
    mkdir -p "$st/DEBIAN"
    cat > "$st/DEBIAN/control" <<CONTROL
Package: $pkg
Version: $ver-r3i6nos1
Architecture: arm64
Maintainer: r3i6nos <noreply@users.noreply.github.com>
Installed-Size: $kb
Depends: python3-minimal, libpython3-stdlib
Section: $4
Priority: optional
X-R3i6nos-Kind: tool
Description: $desc
CONTROL
    find "$st" -exec touch -h -d "$BUILD_DATE" {} +
    rm -f "$OUT/$pkg.deb"
    dpkg-deb --root-owner-group -Zxz --build "$st" "$OUT/$pkg.deb" >/dev/null
    echo "[*] $OUT/$pkg.deb: $(stat -c %s "$OUT/$pkg.deb") bytes packed, $kb KiB installed"
}
stage_base() { # stagedir pkg cmd runfile
    local st="$1" pkg="$2" cmd="$3" run="$4"
    rm -rf "$st"; mkdir -p "$st/usr/bin" "$st/usr/lib/$pkg" "$st/usr/share/r3i6nos" "$st/usr/share/doc/$pkg"
    cp "$run" "$st/usr/bin/$cmd"; chmod 755 "$st/usr/bin/$cmd"
    printf '%s\n' "$pkg" > "$st/usr/share/r3i6nos/tool"
    cp "$REPO/LICENSE" "$st/usr/share/doc/$pkg/copyright"
}
finish_tree() { # stagedir pkg
    local st="$1"
    find "$st/usr" -type d -exec chmod 755 {} +
    find "$st/usr" -type f -exec chmod u+rw,go+r {} +
    find "$st/usr" -type f \( -name '*.so' -o -name '*.so.*' \) -exec chmod 755 {} +
    while IFS= read -r -d '' l; do
        case "$(readlink "$l")" in /*|*../..*) die "a symlink leaves the package: $l" ;; esac
    done < <(find "$st/usr" -type l -print0)
}
is_aarch64_elf() { pybin - "$1" <<'PY'
import sys
h = open(sys.argv[1], "rb").read(20)
sys.exit(0 if h[:4] == b"\x7fELF" and h[5] == 1 and int.from_bytes(h[18:20], "little") == 183 else 1)
PY
}

# ---- QuickJS-ng (only the yt-dlp package has it)
if has ytdlp; then
    echo "[*] QuickJS-ng $QJS_VERSION"
    fetch "$QJS_URL" "$W/qjs.tgz" "$QJS_SHA256"
    mkdir -p "$W/qjs" && tar xzf "$W/qjs.tgz" -C "$W/qjs" --strip-components=1
    cmake -S "$W/qjs" -B "$W/qjs/b" -DCMAKE_BUILD_TYPE=Release -DQJS_BUILD_EXAMPLES=OFF >"$W/qjs-cmake.log" 2>&1 || { tail -20 "$W/qjs-cmake.log"; die "cmake failed"; }
    cmake --build "$W/qjs/b" -j"$(nproc)" >"$W/qjs-build.log" 2>&1 || { tail -20 "$W/qjs-build.log"; die "QuickJS-ng did not build"; }
    [ -x "$W/qjs/b/qjs" ] || die "the build made no qjs"
    strip -o "$W/qjs-bin" "$W/qjs/b/qjs"
    is_aarch64_elf "$W/qjs-bin" || die "qjs is not an aarch64 ELF"
    if ldd "$W/qjs-bin" | awk '{print $1}' | grep -vE '^(linux-vdso|/lib|libc\.so|libm\.so|libdl\.so|libpthread\.so)' | grep -q .; then ldd "$W/qjs-bin" >&2; die "qjs links an unexpected library (only libc and libm may appear)"; fi
    "$W/qjs-bin" --help 2>&1 | grep -q "^QuickJS-ng version $QJS_VERSION" || die "qjs --help does not say QuickJS-ng version $QJS_VERSION"
    echo 'console.log(6*7)' > "$W/t.js"; [ "$("$W/qjs-bin" --script "$W/t.js")" = 42 ] || die "qjs --script does not run a script"
fi

# ---------------------------------------------------------------- yt-dlp
if has ytdlp; then
    PKG=r3i6nos-ytdlp; ST="$W/stage/$PKG"; LIB="$ST/usr/lib/$PKG"; INST="/opt/native_apps/$PKG/usr/lib/$PKG"
    echo "[*] yt-dlp $YTDLP_VERSION"
    fetch "$YTDLP_URL" "$W/yt-dlp" "$YTDLP_SHA256"
    stage_base "$ST" "$PKG" yt-dlp "$HERE/ytdlp/run"
    mkdir -p "$LIB/bin" "$LIB/site-packages"
    cp "$W/qjs-bin" "$LIB/bin/qjs"; chmod 755 "$LIB/bin/qjs"
    pip_tree ytdlp "$LIB/site-packages"; trim_tree "$LIB/site-packages"
    # the Cryptodome package: only what yt-dlp can use (AES for encrypted HLS, and the public-key and hash code its imports pull in) is kept by trim_tree's SelfTest removal
    compile_tree "$LIB/site-packages" "$INST/site-packages"
    # the zipapp, unpacked, byte-compiled and packed again with the .pyc beside every .py (zipimport prefers the .pyc, falls back to the .py if Python's version ever changes)
    pybin - "$W/yt-dlp" "$LIB/yt-dlp" "$INST/yt-dlp" <<'PY'
import compileall, os, py_compile, sys, tempfile, zipfile
src, dst, ddir = sys.argv[1:4]
x = tempfile.mkdtemp()
with zipfile.ZipFile(src) as z:
    z.extractall(x)
if not os.path.isfile(os.path.join(x, "__main__.py")) or not os.path.isdir(os.path.join(x, "yt_dlp")):
    sys.exit("the zipapp has no __main__.py and yt_dlp/: upstream changed its layout")
ok = compileall.compile_dir(x, force=True, quiet=2, legacy=True, ddir=ddir, workers=0, invalidation_mode=py_compile.PycInvalidationMode.UNCHECKED_HASH)
if not ok:
    sys.exit("byte-compiling yt-dlp failed")
names = sorted(os.path.relpath(os.path.join(r, f), x) for r, _, fs in os.walk(x) for f in fs)
with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for n in names:
        zi = zipfile.ZipInfo(n, (2026, 1, 1, 0, 0, 0)); zi.compress_type = zipfile.ZIP_DEFLATED; zi.external_attr = 0o644 << 16
        z.writestr(zi, open(os.path.join(x, n), "rb").read(), compresslevel=9)
print(f"    yt-dlp zipapp: {os.path.getsize(src)} bytes -> {os.path.getsize(dst)} bytes with byte code ({len(names)} files)")
PY
    chmod 644 "$LIB/yt-dlp"
    DOC="$ST/usr/share/doc/$PKG"
    third_party "$LIB/site-packages" "$DOC"
    cp "$W/qjs/LICENSE" "$DOC/licenses/QuickJS-ng-LICENSE" 2>/dev/null || die "no LICENSE in the QuickJS-ng source"
    curl -fsSL --retry 3 -o "$DOC/licenses/yt-dlp-LICENSE" "https://raw.githubusercontent.com/yt-dlp/yt-dlp/$YTDLP_VERSION/LICENSE" || die "could not fetch yt-dlp's LICENSE"
    grep -q "unencumbered software released into the public domain" "$DOC/licenses/yt-dlp-LICENSE" || die "yt-dlp's LICENSE is not the Unlicense any more: read what changed"
    {
        echo "yt-dlp $YTDLP_VERSION (The Unlicense, public domain). Source: https://github.com/yt-dlp/yt-dlp/tree/$YTDLP_VERSION"
        echo "Fetched from $YTDLP_URL (sha256 $YTDLP_SHA256, the hash in the release's SHA2-256SUMS), unpacked, byte-compiled and packed again; the .py files are still inside."
        echo "QuickJS-ng $QJS_VERSION (MIT), built from $QJS_URL (sha256 $QJS_SHA256) with CMake; used only by yt-dlp, from lib/r3i6nos-ytdlp/bin/qjs."
        echo "Python packages: THIRD-PARTY.txt (yt-dlp-ejs, the solver scripts yt-dlp runs in QuickJS; pycryptodomex, native AES for encrypted HLS)."
        echo "ffmpeg is not in this package: the image's /usr/bin/ffmpeg (ffmpeg-min, GPL-2.0-or-later) joins streams."
        echo "r3i6nos's wrapper: tools/streamtools/ytdlp/run in the r3i6nos repository."
    } > "$DOC/NOTICE"
    printf '%s\n%s\nsha256 %s\n' "$YTDLP_VERSION" "$YTDLP_URL" "$YTDLP_SHA256" > "$DOC/SOURCE"
    finish_tree "$ST" "$PKG"
    make_deb "$ST" "$PKG" "$YTDLP_VERSION" net "yt-dlp $YTDLP_VERSION, a video and audio downloader for many sites (Unlicense), for r3i6nos
 Run it as yt-dlp URL. Uses the image's Python 3.11, a private QuickJS-ng to solve YouTube's challenges, and the image's ffmpeg to join streams.
 Defaults to at most 480p H.264 + AAC in an MP4; update with r3i6nos-pkg update, not -U."
fi

# ---------------------------------------------------------------- gallery-dl
if has gallery-dl; then
    PKG=r3i6nos-gallery-dl; ST="$W/stage/$PKG"; LIB="$ST/usr/lib/$PKG"; INST="/opt/native_apps/$PKG/usr/lib/$PKG"
    V="${GALLERYDL_PIP##*==}"
    echo "[*] gallery-dl $V"
    stage_base "$ST" "$PKG" gallery-dl "$HERE/gallery-dl/run"
    pip_tree gallery-dl "$LIB/site-packages"; trim_tree "$LIB/site-packages"; compile_tree "$LIB/site-packages" "$INST/site-packages"
    DOC="$ST/usr/share/doc/$PKG"; third_party "$LIB/site-packages" "$DOC"
    {
        echo "gallery-dl $V (GPL-2.0-only). Source: https://github.com/mikf/gallery-dl/tree/v$V; the package's .py files are its source."
        echo "Installed from PyPI with the sha256 hashes in tools/streamtools/lock/gallery-dl.txt, byte-compiled; THIRD-PARTY.txt lists the Python packages it needs."
        echo "r3i6nos's wrapper: tools/streamtools/gallery-dl/run in the r3i6nos repository."
    } > "$DOC/NOTICE"
    printf '%s\nhttps://pypi.org/project/gallery-dl/%s/\nhashes in tools/streamtools/lock/gallery-dl.txt\n' "$V" "$V" > "$DOC/SOURCE"
    finish_tree "$ST" "$PKG"
    make_deb "$ST" "$PKG" "$V" net "gallery-dl $V, an image gallery and collection downloader for many sites (GPL-2.0), for r3i6nos
 Run it as gallery-dl URL. Uses the image's Python 3.11."
fi

# ---------------------------------------------------------------- streamlink
if has streamlink; then
    PKG=r3i6nos-streamlink; ST="$W/stage/$PKG"; LIB="$ST/usr/lib/$PKG"; INST="/opt/native_apps/$PKG/usr/lib/$PKG"
    V="${STREAMLINK_PIP##*==}"
    echo "[*] streamlink $V"
    stage_base "$ST" "$PKG" streamlink "$HERE/streamlink/run"
    pip_tree streamlink "$LIB/site-packages"; trim_tree "$LIB/site-packages"; compile_tree "$LIB/site-packages" "$INST/site-packages"
    while IFS= read -r -d '' so; do is_aarch64_elf "$so" || die "$so is not an aarch64 ELF"; done < <(find "$LIB/site-packages" -name '*.so' -print0)
    DOC="$ST/usr/share/doc/$PKG"; third_party "$LIB/site-packages" "$DOC"
    {
        echo "streamlink $V (BSD-2-Clause). Source: https://github.com/streamlink/streamlink/tree/$V"
        echo "Installed from PyPI with the sha256 hashes in tools/streamtools/lock/streamlink.txt, byte-compiled. THIRD-PARTY.txt lists the Python packages it needs, among them"
        echo "lxml and pycryptodome (native code, aarch64 wheels), and pycountry (LGPL-2.1: its translations were removed to save 21 MiB; its source is the wheel's own .py files)."
        echo "r3i6nos's wrapper: tools/streamtools/streamlink/run in the r3i6nos repository."
    } > "$DOC/NOTICE"
    printf '%s\nhttps://pypi.org/project/streamlink/%s/\nhashes in tools/streamtools/lock/streamlink.txt\n' "$V" "$V" > "$DOC/SOURCE"
    finish_tree "$ST" "$PKG"
    make_deb "$ST" "$PKG" "$V" net "streamlink $V, a live stream and HLS/DASH player and saver for many sites (BSD-2-Clause), for r3i6nos
 Run it as streamlink URL best. Uses the image's Python 3.11 and ffmpeg."
fi
echo "[*] done"

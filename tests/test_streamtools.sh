#!/bin/bash
# tests/test_streamtools.sh: the stream-tool packages' parts that need no Docker (docs/STREAM-TOOLS.md): the pins and the lock files (every package has a sha256),
# the three wrappers' arguments and environment (with a fake Python that prints what it gets), the refusal to self-update, the build script's guards, and that
# ffmpeg-min's build has the muxers yt-dlp's merge needs. Not covered here: the build itself and the tools running (scripts/build-streamtools.sh, then
# tests/test_streamtools_docker.sh).
set -eu
cd "$(dirname "$0")/.."
T="$(cd "$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-st.XXXXXX")" && pwd -P)"; trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
has_line() { printf '%s\n' "$1" | grep -qx -- "$2"; }   # a whole line of the text
has_text() { printf '%s\n' "$1" | grep -q -- "$2"; }

# ---- pins.conf: every variable set, every hash 64 hex digits, the URL carries the version
(
    # shellcheck disable=SC1091  # the pins
    . tools/streamtools/pins.conf
    for v in YTDLP_VERSION YTDLP_URL YTDLP_SHA256 QJS_VERSION QJS_URL QJS_SHA256 YTDLP_PIP GALLERYDL_PIP STREAMLINK_PIP; do [ -n "${!v}" ] || fail "pins.conf does not set $v"; done
    for h in "$YTDLP_SHA256" "$QJS_SHA256"; do [[ "$h" =~ ^[0-9a-f]{64}$ ]] || fail "a pinned hash is not 64 hex digits: $h"; done
    case "$YTDLP_URL" in *"/$YTDLP_VERSION/yt-dlp") ;; *) fail "YTDLP_URL does not name YTDLP_VERSION" ;; esac
    case "$QJS_URL" in *"/v$QJS_VERSION.tar.gz") ;; *) fail "QJS_URL does not name QJS_VERSION" ;; esac
    case "$GALLERYDL_PIP$STREAMLINK_PIP" in *==*==*) ;; *) fail "gallery-dl and streamlink must be pinned with ==" ;; esac
) || exit 1

# ---- lock files: name==version --hash=sha256:<64 hex>, no repeats, and the packages each tool needs
for t in ytdlp gallery-dl streamlink; do
    f="tools/streamtools/lock/$t.txt"
    [ -s "$f" ] || fail "no $f (scripts/build-streamtools.sh --relock)"
    grep -v '^#' "$f" | grep -qvE '^[a-z0-9._-]+==[A-Za-z0-9._+!-]+ --hash=sha256:[0-9a-f]{64}$' && fail "$f has a line that is not name==version --hash=sha256:..."
    [ "$(grep -v '^#' "$f" | cut -d= -f1 | sort | uniq -d | wc -l | tr -d ' ')" = 0 ] || fail "$f names a package twice"
done
grep -q '^yt-dlp-ejs==' tools/streamtools/lock/ytdlp.txt || fail "lock/ytdlp.txt has no yt-dlp-ejs (the YouTube challenge solver)"
grep -q '^pycryptodomex==' tools/streamtools/lock/ytdlp.txt || fail "lock/ytdlp.txt has no pycryptodomex"
grep -q '^requests==' tools/streamtools/lock/gallery-dl.txt || fail "lock/gallery-dl.txt has no requests"
for p in lxml pycryptodome pycountry trio requests; do grep -q "^$p==" tools/streamtools/lock/streamlink.txt || fail "lock/streamlink.txt has no $p"; done
# the pinned top-level versions are the ones locked
# shellcheck disable=SC1091  # the pins
g="$(. tools/streamtools/pins.conf; echo "${GALLERYDL_PIP##*==}")"
# shellcheck disable=SC1091  # the pins
s="$(. tools/streamtools/pins.conf; echo "${STREAMLINK_PIP##*==}")"
grep -q "^gallery-dl==$g " tools/streamtools/lock/gallery-dl.txt || fail "lock/gallery-dl.txt is not gallery-dl $g (rerun --relock)"
grep -q "^streamlink==$s " tools/streamtools/lock/streamlink.txt || fail "lock/streamlink.txt is not streamlink $s (rerun --relock)"

# ---- the wrappers, with a fake Python that prints its arguments one a line and the environment it was given
mkdir -p "$T/fakebin"
cat > "$T/fakebin/fakepy" <<'PY'
#!/bin/sh
for a in "$@"; do printf 'ARG %s\n' "$a"; done
printf 'ENV PYTHONPATH=%s\nENV REQUESTS_CA_BUNDLE=%s\n' "${PYTHONPATH:-}" "${REQUESTS_CA_BUNDLE:-}"
PY
chmod 755 "$T/fakebin/fakepy"
mkpkg() { # tool pkg: <T>/<tool>/bin/<tool> (the wrapper) with its lib folder, as deb_watcher.py lays a package out
    mkdir -p "$T/$1/bin" "$T/$1/lib/$2/bin"; cp "tools/streamtools/$3/run" "$T/$1/bin/$1"; chmod 755 "$T/$1/bin/$1"
}
mkpkg yt-dlp r3i6nos-ytdlp ytdlp; mkpkg gallery-dl r3i6nos-gallery-dl gallery-dl; mkpkg streamlink r3i6nos-streamlink streamlink
export R3I6NOS_PYTHON="$T/fakebin/fakepy"
unset R3I6NOS_APP_DATA R3I6NOS_YTDLP_CACHE R3I6NOS_FFMPEG || true

out="$("$T/yt-dlp/bin/yt-dlp" --my-option value URL)"
echo "$out" | grep -qx "ARG -s" || fail "yt-dlp: python is not run with -s"
echo "$out" | grep -qx "ARG $T/yt-dlp/lib/r3i6nos-ytdlp/yt-dlp" || fail "yt-dlp: the zipapp is not what runs"
echo "$out" | grep -qx "ARG quickjs:$T/yt-dlp/lib/r3i6nos-ytdlp/bin/qjs" || fail "yt-dlp: QuickJS is not given as the JS runtime"
echo "$out" | grep -qx "ENV PYTHONPATH=$T/yt-dlp/lib/r3i6nos-ytdlp/site-packages" || fail "yt-dlp: PYTHONPATH is not the package's site-packages"
echo "$out" | grep -qx "ARG --merge-output-format" || fail "yt-dlp: no default merge format"
# the user's options come after the defaults, so they win; the URL is last
last="$(echo "$out" | grep '^ARG' | tail -3 | tr '\n' ' ')"
[ "$last" = "ARG --my-option ARG value ARG URL " ] || fail "yt-dlp: the user's arguments are not last: $last"
first_f="$(echo "$out" | grep -n '^ARG -f$' | head -1 | cut -d: -f1)"
out2="$("$T/yt-dlp/bin/yt-dlp" -f 22 URL)"
[ "$(echo "$out2" | grep -n '^ARG -f$' | tail -1 | cut -d: -f1)" -gt "$first_f" ] || fail "yt-dlp: a user's -f does not come after the default -f"
echo "$out2" | grep -A1 '^ARG -f$' | tail -1 | grep -qx "ARG 22" || fail "yt-dlp: the user's -f value is not last"
# ffmpeg only when it exists; the cache and -P follow the app's data folder when a frontend gave one
echo "$out" | grep -qx "ARG --ffmpeg-location" && [ ! -x /usr/bin/ffmpeg ] && fail "yt-dlp: --ffmpeg-location without an ffmpeg"
printf '#!/bin/sh\n' > "$T/ff"; chmod 755 "$T/ff"
R3I6NOS_FFMPEG="$T/ff" "$T/yt-dlp/bin/yt-dlp" URL | grep -qx "ARG $T/ff" || fail "yt-dlp: R3I6NOS_FFMPEG is not used"
out="$(R3I6NOS_APP_DATA=/data/App "$T/yt-dlp/bin/yt-dlp" URL)"
echo "$out" | grep -qx "ARG /data/App/.yt-dlp-cache" || fail "yt-dlp: the cache is not under R3I6NOS_APP_DATA"
echo "$out" | grep -A1 '^ARG -P$' | tail -1 | grep -qx "ARG /data/App" || fail "yt-dlp: -P is not R3I6NOS_APP_DATA"
out="$(R3I6NOS_YTDLP_CACHE=/c "$T/yt-dlp/bin/yt-dlp" URL)"; echo "$out" | grep -qx "ARG /c" || fail "yt-dlp: R3I6NOS_YTDLP_CACHE is not used"
# self-update is refused (exit 2, names the package update), and the refusal runs nothing
for u in -U --update --update-to=nightly; do
    rc=0; msg="$("$T/yt-dlp/bin/yt-dlp" "$u" 2>&1)" || rc=$?
    [ "$rc" = 2 ] || fail "yt-dlp $u: exit $rc, not 2"
    echo "$msg" | grep -q "r3i6nos-pkg update" || fail "yt-dlp $u: the refusal does not say how to update"
    echo "$msg" | grep -q '^ARG' && fail "yt-dlp $u: it ran Python"
done
# a -U after -- is not an option
"$T/yt-dlp/bin/yt-dlp" -- -U 2>&1 | grep -qx "ARG -U" || fail "yt-dlp: -U after -- was treated as an option"

out="$("$T/gallery-dl/bin/gallery-dl" URL)"
has_line "$out" "ARG -m" || fail "gallery-dl: not run as python -m"
has_line "$out" "ARG gallery_dl" || fail "gallery-dl: not run as python -m gallery_dl"
echo "$out" | grep -qx "ENV PYTHONPATH=$T/gallery-dl/lib/r3i6nos-gallery-dl/site-packages" || fail "gallery-dl: PYTHONPATH"
echo "$out" | grep -q '^ARG -d$' && fail "gallery-dl: -d without a data folder"
R3I6NOS_APP_DATA=/data/App "$T/gallery-dl/bin/gallery-dl" URL | grep -qx "ARG /data/App/gallery-dl" || fail "gallery-dl: -d is not under R3I6NOS_APP_DATA"
out="$("$T/streamlink/bin/streamlink" URL best)"
has_line "$out" "ARG streamlink" || fail "streamlink: not run as python -m streamlink"
has_line "$out" "ARG best" || fail "streamlink: the stream name was lost"
echo "$out" | grep -qx "ENV PYTHONPATH=$T/streamlink/lib/r3i6nos-streamlink/site-packages" || fail "streamlink: PYTHONPATH"
R3I6NOS_FFMPEG="$T/ff" "$T/streamlink/bin/streamlink" URL | grep -A1 '^ARG --ffmpeg-ffmpeg$' | tail -1 | grep -qx "ARG $T/ff" || fail "streamlink: R3I6NOS_FFMPEG is not used"
if [ -r /etc/ssl/certs/ca-certificates.crt ]; then
    "$T/streamlink/bin/streamlink" URL | grep -qx "ENV REQUESTS_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt" || fail "streamlink: the system CA bundle is not used"
fi

# ---- the build scripts' guards
bash -n tools/streamtools/build.sh || fail "syntax: tools/streamtools/build.sh"
bash -n scripts/build-streamtools.sh || fail "syntax: scripts/build-streamtools.sh"
rc=0; msg="$(tools/streamtools/build.sh build 2>&1)" || rc=$?
[ "$rc" != 0 ] || fail "build.sh ran outside a Debian 12 container"
has_text "$msg" "run as root in Debian 12" || fail "build.sh does not say it needs Debian 12 as root"
rc=0; msg="$(scripts/build-streamtools.sh --bogus 2>&1)" || rc=$?
[ "$rc" != 0 ] || fail "build-streamtools.sh accepts an unknown option"
has_text "$msg" "unknown option" || fail "build-streamtools.sh does not say the option is unknown"
# the packages are tool packages, built reproducibly and checked: the marker, the hashes and the aarch64 check are in the script
for needle in "usr/share/r3i6nos/tool" "--require-hashes" "is not an aarch64 ELF" "does not match the pinned sha256" "unchecked-hash"; do grep -q -- "$needle" tools/streamtools/build.sh || fail "build.sh no longer has: $needle"; done

# ---- ffmpeg-min can join the streams yt-dlp downloads separately (mp4, matroska, webm) and checks that it can
grep -q -- '--enable-muxer=rawvideo,pcm_s16le,mp4,matroska,webm,ipod' tools/ffmpeg-min/build.sh || fail "ffmpeg-min's muxers are not mp4, matroska, webm and ipod"
grep -q 'mcase vp9-opus webm' tools/ffmpeg-min/build.sh || fail "ffmpeg-min's build does not check a WebM merge"
echo "streamtools OK"

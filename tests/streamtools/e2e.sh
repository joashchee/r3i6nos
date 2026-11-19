#!/bin/sh
# Runs INSIDE a clean Debian 12 arm64 container (tests/test_streamtools_docker.sh starts it): ONLY the image's Python packages (python3-minimal, libpython3-stdlib,
# ca-certificates), the image's ffmpeg-min as /usr/bin/ffmpeg, and the three stream-tool packages installed by the REAL deb_watcher.py. A local web server serves the
# media from /fx; nothing leaves the container. Prints "E2E <name> PASS|FAIL|INFO|SKIP ...".
set -u
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq --no-install-recommends python3-minimal libpython3-stdlib ca-certificates dpkg >/dev/null 2>&1 || { echo "E2E setup FAIL apt"; exit 1; }
cp /ff/ffmpeg /usr/bin/ffmpeg; chmod 755 /usr/bin/ffmpeg
mkdir -p /t/debs /t/apps /t/launch /t/bin /o
cp /debs/*.deb /t/debs/
export R3I6NOS_DEB_DIR=/t/debs R3I6NOS_DEB_DIR2=/nonexistent R3I6NOS_DEFAULT_DEB_DIR=/nonexistent R3I6NOS_APP_ROOT=/t/apps R3I6NOS_LAUNCHER_DIR=/t/launch R3I6NOS_TOOLS_DIR=/t/bin
python3 /w/deb_watcher.py || { echo "E2E watcher FAIL"; exit 1; }
for c in yt-dlp gallery-dl streamlink; do
    if [ -x /t/bin/$c ]; then echo "E2E install-$c PASS"; else echo "E2E install-$c FAIL no command"; fi
done
export PATH="/t/bin:$PATH" HOME=/root
# the unit runs the tools as a normal user from a read-only package folder: the same here (root could write a __pycache__ and hide a missing byte code)
chmod -R a-w /t/apps
touch /tmp/stamp; sleep 1
# a helper: wall time and peak memory of a command
cat > /tmp/rss.py <<'PY'
import resource, subprocess, sys, time
t = time.time(); p = subprocess.run(sys.argv[1:], capture_output=True, text=True); dt = time.time() - t
ru = resource.getrusage(resource.RUSAGE_CHILDREN)
sys.stdout.write(f"{dt:.2f}s wall, {ru.ru_utime + ru.ru_stime:.2f}s cpu, {ru.ru_maxrss / 1024:.0f} MiB peak, rc={p.returncode}\n")
PY
( cd /fx && exec python3 -m http.server 8000 --bind 127.0.0.1 >/tmp/http.log 2>&1 ) &
sleep 2
U=http://127.0.0.1:8000
# verdict NAME "text for a pass" COMMAND...: one result line; on a failure the last lines of $SHOW (the output of the command under test) follow
SHOW=/tmp/d.out
verdict() {
    n="$1"; m="$2"; shift 2
    if "$@"; then echo "E2E $n PASS${m:+ $m}"; else echo "E2E $n FAIL"; tail -8 "$SHOW" 2>/dev/null | sed 's/^/  /'; fi
    SHOW=/tmp/d.out
}
info() { echo "E2E $1 INFO $2"; }
streams() { ffmpeg -hide_banner -i "$1" 2>&1 | grep -c '^ *Stream #'; }
# the number of error lines decoding a file all the way gives (0 = clean); ffmpeg-min has no "null" muxer, so decode to raw video and raw audio
clean() { { ffmpeg -v error -y -i "$1" -map 0:v -an -f rawvideo /dev/null; ffmpeg -v error -y -i "$1" -map 0:a -vn -f s16le /dev/null; } 2>&1 | wc -l | tr -d ' '; }
media_ok() { [ -s "$1" ] && [ "$(clean "$1")" = 0 ] && [ "$(streams "$1")" = 2 ]; }   # a decodable file with a video and an audio stream
decodes() { [ -n "$1" ] && [ -s "$1" ] && [ "$(clean "$1")" = 0 ]; }
first() { find "$1" -maxdepth 1 -name "$2" | head -n 1; }
# a pipe into ffmpeg-min must decode without a word on stderr
pipe_clean() { [ ! -s "$1" ]; }
file_ok() { [ -n "$1" ] && [ -s "$1" ]; }
refused() { [ "$1" = 2 ] && grep -q 'r3i6nos-pkg update' /tmp/u.out; }
live_ok() { [ "$1" = 0 ] && [ "$2" -gt 0 ]; }

# ------------------------------------------------------------------ yt-dlp
V="$(yt-dlp --version 2>/tmp/err.txt)"
SHOW=/tmp/err.txt; verdict ytdlp-version "$V" test "$V" = "${YTDLP_VERSION:-?}"
n="$(yt-dlp --list-extractors 2>/dev/null | wc -l | tr -d ' ')"
verdict ytdlp-extractors "$n lines" test "$n" -gt 1500
rc=0; yt-dlp -U >/tmp/u.out 2>&1 || rc=$?
SHOW=/tmp/u.out; verdict ytdlp-no-selfupdate "" refused "$rc"
# the JavaScript runtime yt-dlp found, the solver package, and the native AES
yt-dlp -v --simulate "$U/clip.mp4" >/tmp/v.out 2>&1
SHOW=/tmp/v.out; verdict ytdlp-jsruntime "$(grep -i 'JS runtimes' /tmp/v.out | head -n 1 | sed 's/^.*JS runtimes: //')" grep -qi "JS runtimes: .*quickjs" /tmp/v.out
SHOW=/tmp/v.out; verdict ytdlp-ejs "($(grep -o 'yt_dlp_ejs-[0-9.]*' /tmp/v.out | head -n 1))" grep -q "yt_dlp_ejs-" /tmp/v.out
SHOW=/tmp/v.out; verdict ytdlp-cryptodome "($(grep -o 'Cryptodome-[0-9.]*' /tmp/v.out | head -n 1))" grep -q "Cryptodome-" /tmp/v.out
rm -rf /o/y && mkdir -p /o/y
yt-dlp -o '/o/y/direct.%(ext)s' "$U/clip.mp4" >/tmp/d.out 2>&1
verdict ytdlp-direct "" media_ok /o/y/direct.mp4
yt-dlp -o '/o/y/dash.%(ext)s' "$U/dash-avc/manifest.mpd" >/tmp/d.out 2>&1
verdict ytdlp-dash-merge-mp4 "video and audio DASH joined into one MP4 by ffmpeg-min" media_ok /o/y/dash.mp4
yt-dlp -f 'bv*+ba' --merge-output-format webm -o '/o/y/vp9.%(ext)s' "$U/dash-vp9/manifest.mpd" >/tmp/d.out 2>&1
verdict ytdlp-dash-merge-webm "VP9 and Opus joined into one WebM (needs ffmpeg-min's matroska and webm muxers)" media_ok /o/y/vp9.webm
if [ -x /base/ffmpeg ]; then
    R3I6NOS_FFMPEG=/base/ffmpeg yt-dlp -f 'bv*+ba' --merge-output-format webm -o '/o/y/vp9-base.%(ext)s' "$U/dash-vp9/manifest.mpd" >/tmp/d.out 2>&1 || true
    if [ -s /o/y/vp9-base.webm ]; then info ytdlp-baseline-ffmpeg "the older ffmpeg-min (mp4 muxer only) also joined it"
    else info ytdlp-baseline-ffmpeg "the older ffmpeg-min (mp4 muxer only) cannot join VP9 and Opus: $(grep -i 'muxer\|Unable\|Invalid' /tmp/d.out | head -n 1)"; fi
fi
yt-dlp -o '/o/y/hls.%(ext)s' "$U/hls/index.m3u8" >/tmp/d.out 2>&1
verdict ytdlp-hls "" decodes "$(first /o/y 'hls.*')"
yt-dlp -o '/o/y/hlsaes.%(ext)s' "$U/hls-aes/index.m3u8" >/tmp/d.out 2>&1
verdict ytdlp-hls-aes "AES-128 decrypted natively with pycryptodomex" decodes "$(first /o/y 'hlsaes.*')"
if grep -qi "extremely slow" /tmp/d.out; then echo "E2E ytdlp-hls-aes-native FAIL yt-dlp fell back to its pure-Python AES"; fi
# streaming to a player: yt-dlp is the network layer (ffmpeg-min has no network), ffmpeg reads the pipe
yt-dlp -q -f b -o - "$U/clip.mp4" 2>/dev/null | ffmpeg -v error -y -i pipe:0 -map 0:v -an -f rawvideo /dev/null >/tmp/p.out 2>&1
SHOW=/tmp/p.out; verdict ytdlp-pipe "an MP4 with its index first plays from the pipe" pipe_clean /tmp/p.out
yt-dlp -q -f b -o - "$U/clip-nofast.mp4" 2>/dev/null | ffmpeg -v error -y -i pipe:0 -map 0:v -an -f rawvideo /dev/null >/tmp/p.out 2>&1
if [ -s /tmp/p.out ]; then info ytdlp-pipe-nofast "an MP4 with its index last does NOT play from a pipe (as expected): download it, or pick a streamable format"
else info ytdlp-pipe-nofast "it played (index last, but the pipe was readable)"; fi
# app data: a frontend's folder gets the file and the cache (rule 16)
rm -rf /o/app && mkdir -p /o/app
R3I6NOS_APP_DATA=/o/app yt-dlp -o 'viaapp.%(ext)s' "$U/clip.mp4" >/tmp/d.out 2>&1
verdict ytdlp-appdata "the file went to R3I6NOS_APP_DATA" test -s /o/app/viaapp.mp4
info ytdlp-start "--version: $(python3 -I /tmp/rss.py yt-dlp --version)"
info ytdlp-start "--version again: $(python3 -I /tmp/rss.py yt-dlp --version)"
info ytdlp-start "--list-extractors: $(python3 -I /tmp/rss.py yt-dlp --list-extractors)"
info ytdlp-start "a direct download: $(python3 -I /tmp/rss.py yt-dlp -o '/o/y/t.%(ext)s' "$U/clip.mp4")"
info ytdlp-start "a DASH join: $(python3 -I /tmp/rss.py yt-dlp -o '/o/y/t2.%(ext)s' "$U/dash-avc/manifest.mpd")"

# ------------------------------------------------------------------ gallery-dl
V="$(gallery-dl --version 2>/dev/null)"
verdict gallery-dl-version "$V" test -n "$V"
n="$(gallery-dl --list-extractors 2>/dev/null | grep -c '^[A-Za-z]')"
verdict gallery-dl-extractors "$n" test "$n" -gt 200
rm -rf /o/g && mkdir -p /o/g
gallery-dl -d /o/g "$U/pic.png" >/tmp/g.out 2>&1
png="$(find /o/g -name '*.png' | head -n 1)"
SHOW=/tmp/g.out; verdict gallery-dl-download "" file_ok "$png"
rm -rf /o/app && mkdir -p /o/app
R3I6NOS_APP_DATA=/o/app gallery-dl "$U/pic.png" >/tmp/g.out 2>&1
png="$(find /o/app/gallery-dl -name '*.png' 2>/dev/null | head -n 1)"
SHOW=/tmp/g.out; verdict gallery-dl-appdata "" file_ok "$png"
info gallery-dl-start "--version: $(python3 -I /tmp/rss.py gallery-dl --version)"
info gallery-dl-start "a download: $(python3 -I /tmp/rss.py gallery-dl -d /o/g "$U/pic.png")"

# ------------------------------------------------------------------ streamlink
V="$(streamlink --version 2>/dev/null)"
verdict streamlink-version "$V" test -n "$V"
n="$(streamlink --plugins 2>/dev/null | tr ',' '\n' | grep -c '[a-z]')"
verdict streamlink-plugins "$n" test "$n" -gt 50
rm -f /o/sl.ts
streamlink --quiet "hls://$U/hls/index.m3u8" best -o /o/sl.ts >/tmp/s.out 2>&1
SHOW=/tmp/s.out; verdict streamlink-hls "an HLS playlist saved and decoded by ffmpeg-min" decodes /o/sl.ts
rm -f /o/sl2.ts
streamlink --quiet "hls://$U/hls-aes/index.m3u8" best -o /o/sl2.ts >/tmp/s.out 2>&1
SHOW=/tmp/s.out; verdict streamlink-hls-aes "AES-128 decrypted" decodes /o/sl2.ts
SLP=/t/apps/r3i6nos-streamlink/usr/lib/r3i6nos-streamlink/site-packages
# language lookups work without the 21 MiB of translations that were removed
PYTHONPATH=$SLP python3 -s -c '
from streamlink.utils.l10n import Localization
l = Localization("en_US"); assert l.language.alpha3 == "eng", l.language
assert Localization("de_DE").country.alpha2 == "DE"
' >/tmp/l.out 2>&1
rc=$?
SHOW=/tmp/l.out; verdict streamlink-l10n "" test "$rc" = 0
PYTHONPATH=$SLP python3 -s -c 'import lxml.etree, Crypto.Cipher.AES, ssl, trio' >/tmp/l.out 2>&1
rc=$?
SHOW=/tmp/l.out; verdict streamlink-natives "lxml, pycryptodome, ssl and trio import" test "$rc" = 0
info streamlink-start "--version: $(python3 -I /tmp/rss.py streamlink --version)"
info streamlink-start "--version again: $(python3 -I /tmp/rss.py streamlink --version)"
info streamlink-start "an HLS save: $(python3 -I /tmp/rss.py streamlink --quiet "hls://$U/hls/index.m3u8" best -o /o/sl3.ts)"

# ------------------------------------------------------------------ the installed folders were only read
# the byte code made at build time is what ran: a root user here could write a missing one, so look for any new .pyc under the packages
nn="$(find /t/apps -newer /tmp/stamp -name '*.pyc' | wc -l | tr -d ' ')"
verdict no-runtime-bytecode "nothing was compiled at run time" test "$nn" = 0

# ------------------------------------------------------------------ one real request (only with E2E_LIVE=1, by hand: R3I6NOS_LIVE=1 tests/test_streamtools_docker.sh)
if [ -n "${E2E_LIVE:-}" ]; then
    LIVE_URL="https://www.youtube.com/watch?v=jNQXAC9IVRw"
    info live "one metadata request to YouTube for a public video, nothing downloaded: $LIVE_URL"
    # the Docker VM's resolver never answers IPv6 (AAAA) lookups, so glibc waits 4 s on every new host; glibc 2.36's no-aaaa option skips them (a test-container fix only)
    grep -q no-aaaa /etc/resolv.conf 2>/dev/null || echo 'options no-aaaa' >> /etc/resolv.conf
    rc=0; yt-dlp -v --simulate -F "$LIVE_URL" >/tmp/live.out 2>&1 || rc=$?
    grep -i "jsc\|quickjs\|challenge\|ejs\|Sign in\|bot" /tmp/live.out | head -n 12 | sed 's/^/  /'
    nf="$(grep -cE '^[0-9a-z_-]+ +(mp4|webm|m4a|3gp)' /tmp/live.out || true)"
    SHOW=/tmp/live.out verdict live "$nf formats listed, the YouTube challenge solved by QuickJS-ng" test "$rc" = 0 -a "$nf" -gt 0
    yt-dlp --simulate -O '%(format_id)s %(resolution)s %(vcodec)s %(acodec)s' "$LIVE_URL" 2>/dev/null | sed 's/^/E2E live INFO the default selection: /'
    rm -rf /root/.cache/yt-dlp
    info live "extraction, cold cache: $(python3 -I /tmp/rss.py yt-dlp --simulate "$LIVE_URL")"
    info live "extraction again (the solved challenge is not cached between runs): $(python3 -I /tmp/rss.py yt-dlp --simulate "$LIVE_URL")"
    # the streaming chain a frontend would use: yt-dlp downloads format 18 (progressive MP4, H.264 + AAC) into a pipe, ffmpeg-min decodes it
    t0="$(date +%s.%N)"
    yt-dlp -q -f 18 -o - "$LIVE_URL" 2>/tmp/live-pipe.err | ffmpeg -v error -y -i pipe:0 -map 0:v -an -f rawvideo /dev/null >/tmp/live-dec.out 2>&1
    t1="$(date +%s.%N)"
    SHOW=/tmp/live-dec.out verdict live-pipe "format 18 streamed through a pipe into ffmpeg-min and decoded ($(python3 -I -c "print(round($t1-$t0,2))") s)" pipe_clean /tmp/live-dec.out
    # audio only (the guide's "--no-video"): the AAC stream (format 140) saved, then decoded alone by ffmpeg-min to 16-bit stereo PCM, as the player does
    yt-dlp -q -f 140 -o /tmp/a140.m4a "$LIVE_URL" >/dev/null 2>&1
    info live-audio "format 140 (AAC) decoding alone: $(python3 -I /tmp/rss.py ffmpeg -v error -y -i /tmp/a140.m4a -vn -f s16le -ar 44100 -ac 2 /dev/null) for $(ffmpeg -hide_banner -i /tmp/a140.m4a 2>&1 | grep -o 'Duration: [0-9:.]*')"
    yt-dlp -q -f 18 -o /tmp/v18.mp4 "$LIVE_URL" >/dev/null 2>&1
    info live-video "format 18 (320x240 H.264) decoding alone, scaled to 640x400 BGRA at 30 fps as Commander does: $(python3 -I /tmp/rss.py ffmpeg -v error -y -i /tmp/v18.mp4 -an -vf scale=640:400,fps=30 -f rawvideo -pix_fmt bgra /dev/null) for $(ffmpeg -hide_banner -i /tmp/v18.mp4 2>&1 | grep -o 'Duration: [0-9:.]*')"
fi
echo "E2E end PASS"

#!/bin/bash
# tools/ffmpeg-min/build.sh: builds the minimal ffmpeg (tools/ffmpeg-min/README.md) and checks it. Runs as root INSIDE a Debian 12 container
# (arm64 on the R36S's own architecture); scripts/build-ffmpeg-min.sh starts it. Output: $OUT/ffmpeg (default /out), stripped.
#
# Pinned sources, each checked by hash: ffmpeg-5.1.6.tar.xz and x264 (stable, commit baee400, build 164: the one Debian 12 ships). x264 and zlib are
# linked statically, so nothing but libc and libm is linked. **GPL-2.0-or-later** (--enable-gpl --enable-libx264; the maintainer accepted it on
# 2026-11-11); no --enable-nonfree. ROADMAP section 20: Tier A (Commander's Play tab and Diskette's audio and video lists), Tier B (Rec demo's H.264 + AAC
# encode on the unit) and WebP, TIFF and PSD stills (Commander's Picture tab).
set -eu

VER=5.1.6
SHA256=f4fa066278f7a47feab316fef905f4db0d5e9b589451949740f83972b30901bd
URL="https://ffmpeg.org/releases/ffmpeg-$VER.tar.xz"
X264_COMMIT=baee400fa9ced6f5481a728138fed6e867b0ff7f
X264_SHA256=ce6623b8b289765daee04a297c2fd1a293cb2565a1749c76d66c8d72c7ddc1ab
X264_URL="https://code.videolan.org/videolan/x264/-/archive/$X264_COMMIT/x264-$X264_COMMIT.tar.bz2"
OUT="${OUT:-/out}"
JOBS="${JOBS:-$(nproc)}"
export DEBIAN_FRONTEND=noninteractive LC_ALL=C
REPO="$(cd "$(dirname "$0")/../.." && pwd)"

if [ "$(id -u)" -ne 0 ] || ! grep -q '^12' /etc/debian_version 2>/dev/null; then echo "ffmpeg-min: run as root in Debian 12 (scripts/build-ffmpeg-min.sh does)" >&2; exit 1; fi
apt-get update -qq >/dev/null
# the full Debian ffmpeg is only the reference the minimal one is compared with; it is not part of the result
apt-get install -y -qq --no-install-recommends build-essential ca-certificates curl xz-utils bzip2 pkg-config zlib1g-dev libsdl2-dev libjpeg-turbo-progs ffmpeg >/dev/null

W="$(mktemp -d)"
cd "$W"
curl -fsSL -o ff.tar.xz "$URL"
echo "$SHA256  ff.tar.xz" | sha256sum -c - >/dev/null || { echo "ffmpeg-min: ff.tar.xz does not match the pinned hash" >&2; exit 1; }
tar xf ff.tar.xz
curl -fsSL -o x264.tar.bz2 "$X264_URL"
echo "$X264_SHA256  x264.tar.bz2" | sha256sum -c - >/dev/null || { echo "ffmpeg-min: x264.tar.bz2 does not match the pinned hash" >&2; exit 1; }
tar xjf x264.tar.bz2

# x264, static, 8-bit 4:2:0 only (all Rec demo makes), no command line
( cd "x264-$X264_COMMIT"
  ./configure --prefix="$W/x264" --enable-static --enable-strip --disable-cli --disable-opencl --disable-avs --disable-swscale --disable-lavf \
      --disable-ffms --disable-gpac --disable-lsmash --bit-depth=8 --chroma-format=420 >"$W/x264-configure.log" 2>&1 || { tail -20 "$W/x264-configure.log"; exit 1; }
  if ! { make -j"$JOBS" && make install-lib-static install-lib-dev; } >"$W/x264-make.log" 2>&1; then tail -20 "$W/x264-make.log"; exit 1; fi )
# zlib (TIFF's deflate), static too: a directory with only libz.a, so the linker cannot pick the shared one
mkdir "$W/zs"; ln -s "$(dpkg -L zlib1g-dev | grep 'libz\.a$' | head -n 1)" "$W/zs/libz.a"

export PKG_CONFIG_PATH="$W/x264/lib/pkgconfig"
cd "ffmpeg-$VER"

DEMUXERS=mov,matroska,avi,asf,flv,mpegps,mpegts,mpegvideo,wav,mp3,ogg,flac,aiff,aac,wv,ac3,eac3,dts,rawvideo,image2,image2pipe,image_jpeg_pipe,image_webp_pipe,image_tiff_pipe,image_psd_pipe,image_png_pipe   # mpegvideo: a program stream identifies its video by probing it
PARSERS=png,webp,h264,hevc,aac,ac3,dca,mpegaudio,mpeg4video,mpegvideo,h263,mjpeg,vc1,vorbis,flac,opus,vp8,vp9
VIDEO=png,webp,tiff,psd,rawvideo,h264,hevc,mpeg4,mpeg2video,mpeg1video,vp8,vp9,wmv1,wmv2,wmv3,vc1,mjpeg,msmpeg4v1,msmpeg4v2,msmpeg4v3,h263,h263p,flv,vp6,vp6f,vp6a,cinepak,msvideo1,qtrle,svq1,svq3,prores
AUDIO=aac,aac_latm,mp1,mp1float,mp2,mp2float,mp3,mp3float,ac3,eac3,dca,flac,vorbis,opus,wavpack,alac,wmav1,wmav2,wmapro,pcm_s16le,pcm_s16be,pcm_u8,pcm_s24le,pcm_s32le,pcm_f32le,adpcm_ms,adpcm_ima_wav
./configure --prefix=/opt/min --disable-everything --disable-autodetect --disable-doc --disable-network --disable-debug --enable-small \
    --disable-ffplay --disable-ffprobe --enable-ffmpeg \
    --enable-protocol=file,pipe --enable-demuxer="$DEMUXERS" --enable-parser="$PARSERS" \
    --enable-decoder="$VIDEO,$AUDIO" \
    --enable-gpl --enable-libx264 --enable-zlib --pkg-config-flags=--static \
    --extra-cflags="-I$W/x264/include" --extra-ldflags="-L$W/x264/lib -L$W/zs" \
    --enable-encoder=rawvideo,pcm_s16le,libx264,aac --enable-muxer=rawvideo,pcm_s16le,mp4 \
    --enable-filter=scale,fps,pad,format,aresample,aformat,null,anull,setpts \
    --enable-swscale --enable-swresample >"$W/configure.log" 2>&1 || { tail -25 "$W/configure.log"; exit 1; }
make -j"$JOBS" >"$W/make.log" 2>&1 || { tail -25 "$W/make.log"; exit 1; }
strip -o "$W/ffmpeg-min" ffmpeg
MIN="$W/ffmpeg-min"

echo "[*] linked libraries (only libc and libm may appear):"
ldd "$MIN" | awk '{print "    " $1}'
ldd "$MIN" | grep -E 'libav|libsw|libx26|libz|libpulse|libX' && { echo "ffmpeg-min: an unexpected library is linked" >&2; exit 1; } || true

# every decoder asked for must be in the binary (a misspelt name is silently left out by configure)
fail=0
for d in $(echo "$VIDEO,$AUDIO" | tr ',' ' ' | sed 's/msmpeg4v3/msmpeg4/'); do "$MIN" -hide_banner -decoders 2>/dev/null | awk '{print $2}' | grep -qx "$d" || { echo "    MISSING decoder: $d"; fail=1; }; done

# the same for demuxers, muxers and encoders (their names in `-demuxers` are not always the configure names: image_psd_pipe is psd_pipe)
for d in mov matroska avi asf flv mpeg mpegts mpegvideo wav mp3 ogg flac aiff aac wv ac3 eac3 dts rawvideo image2 image2pipe jpeg_pipe webp_pipe tiff_pipe psd_pipe png_pipe; do
    "$MIN" -hide_banner -demuxers 2>/dev/null | awk '{print $2}' | tr ',' '\n' | grep -qx "$d" || { echo "    MISSING demuxer: $d"; fail=1; }
done
for m in mp4 rawvideo s16le; do "$MIN" -hide_banner -muxers 2>/dev/null | awk '{print $2}' | grep -qx "$m" || { echo "    MISSING muxer: $m"; fail=1; }; done
for e in libx264 aac rawvideo pcm_s16le; do "$MIN" -hide_banner -encoders 2>/dev/null | awk '{print $2}' | grep -qx "$e" || { echo "    MISSING encoder: $e"; fail=1; }; done

# decode what the full ffmpeg encodes, the way Commander's player does (player.h), and compare the bytes
T="$W/t"; mkdir -p "$T"
V='scale=640:400:force_original_aspect_ratio=decrease:flags=neighbor,pad=640:400:(ow-iw)/2:(oh-ih)/2,fps=30'
vcase() { # name container extra-encoder-args...
    local name="$1" ext="$2"; shift 2
    local f="$T/v-$name.$ext"
    if ! ffmpeg -v error -f lavfi -i testsrc2=s=352x288:r=25:d=2 -an "$@" -y "$f" 2>/dev/null; then echo "    skip  video $name (the reference ffmpeg cannot make one)"; return; fi
    local a b
    a="$(ffmpeg -v error -i "$f" -an -vf "$V" -f rawvideo -pix_fmt bgra pipe:1 2>&1 | md5sum | cut -c1-12)"
    b="$("$MIN" -v error -i "$f" -an -vf "$V" -f rawvideo -pix_fmt bgra pipe:1 2>&1 | md5sum | cut -c1-12)"
    if [ "$a" = "$b" ] && [ "$(ffmpeg -v error -i "$f" -an -vf "$V" -f rawvideo -pix_fmt bgra pipe:1 | wc -c)" -gt 1000000 ]; then echo "    ok    video $name in .$ext"; else echo "    FAIL  video $name in .$ext (full $a, minimal $b)"; "$MIN" -v error -i "$f" -an -vf "$V" -f null - 2>&1 | head -4 | sed 's/^/          /'; fail=1; fi
}
acase() { # name container extra-encoder-args...
    local name="$1" ext="$2"; shift 2
    local f="$T/a-$name.$ext"
    if ! ffmpeg -v error -f lavfi -i "sine=f=440:d=2" -vn "$@" -y "$f" 2>/dev/null; then echo "    skip  audio $name (the reference ffmpeg cannot make one)"; return; fi
    local a b
    a="$(ffmpeg -v error -i "$f" -vn -f s16le -ar 44100 -ac 2 pipe:1 2>&1 | md5sum | cut -c1-12)"
    b="$("$MIN" -v error -i "$f" -vn -f s16le -ar 44100 -ac 2 pipe:1 2>&1 | md5sum | cut -c1-12)"
    if [ "$a" = "$b" ] && [ "$(ffmpeg -v error -i "$f" -vn -f s16le -ar 44100 -ac 2 pipe:1 | wc -c)" -gt 100000 ]; then echo "    ok    audio $name in .$ext"; else echo "    FAIL  audio $name in .$ext (full $a, minimal $b)"; fail=1; fi
}
echo "[*] decoded the same as the full ffmpeg (video 640x400 BGRA, audio 44.1 kHz s16le stereo):"
vcase h264 mp4 -c:v libx264 -pix_fmt yuv420p
vcase mpeg4 avi -c:v mpeg4
vcase msmpeg4v2 avi -c:v msmpeg4v2
vcase msmpeg4v3 avi -c:v msmpeg4
vcase mjpeg avi -c:v mjpeg
vcase mjpeg mov -c:v mjpeg
vcase h263 mov -c:v h263
vcase flv1 flv -c:v flv
vcase cinepak avi -c:v cinepak
vcase msvideo1 avi -c:v msvideo1
vcase qtrle mov -c:v qtrle
vcase svq1 mov -c:v svq1
vcase prores mov -c:v prores_ks
vcase wmv1 asf -c:v wmv1
vcase wmv2 asf -c:v wmv2
vcase mpeg2 ts -c:v mpeg2video
vcase mpeg1 mpg -c:v mpeg1video
vcase vp8 webm -c:v libvpx
vcase vp9 webm -c:v libvpx-vp9
vcase hevc mkv -c:v libx265 -pix_fmt yuv420p
acase mp2 mp2 -c:a mp2
acase mp2 ts -c:a mp2 -f mpegts
acase ac3 ac3 -c:a ac3
acase ac3 mkv -c:a ac3
acase eac3 eac3 -c:a eac3
acase dts mkv -c:a dca -strict -2
acase wmav1 asf -c:a wmav1
acase wmav2 asf -c:a wmav2
acase aac m4a -c:a aac
acase mp3 mp3 -c:a libmp3lame
acase opus ogg -c:a libopus
acase vorbis ogg -c:a libvorbis
acase flac flac -c:a flac
acase wav wav -c:a pcm_s16le
acase aiff aiff -c:a pcm_s16be
acase wavpack wv -c:a wavpack
echo "[*] no encoder exists in the reference ffmpeg for wmv3, vc1, svq3, vp6, wmapro: their decoders are in the binary but have not decoded a file"

# stills, the way Commander's Picture tab asks (apps/commander-native/player.h pl_still): the first frame, native size, BGRA
scase() { # name file
    local a b
    a="$(ffmpeg -v error -i "$2" -frames:v 1 -f rawvideo -pix_fmt bgra pipe:1 2>&1 | md5sum | cut -c1-12)"
    b="$("$MIN" -v error -i "$2" -frames:v 1 -f rawvideo -pix_fmt bgra pipe:1 2>&1 | md5sum | cut -c1-12)"
    if [ "$a" = "$b" ] && [ "$("$MIN" -v error -i "$2" -frames:v 1 -f rawvideo -pix_fmt bgra pipe:1 | wc -c)" = "$((${3:-320} * ${4:-240} * 4))" ]; then echo "    ok    still $1"; else echo "    FAIL  still $1 (full $a, minimal $b)"; "$MIN" -v warning -i "$2" -frames:v 1 -f null - 2>&1 | head -5 | sed 's/^/          /'; fail=1; fi
}
echo "[*] stills decode to the same pixels as the full ffmpeg:"
ffmpeg -v error -f lavfi -i testsrc2=s=320x240 -frames:v 1 -y "$T/s.png"
ffmpeg -v error -i "$T/s.png" -c:v libwebp -lossless 0 -q:v 80 -y "$T/lossy.webp"; scase "webp lossy" "$T/lossy.webp"
ffmpeg -v error -i "$T/s.png" -c:v libwebp -lossless 1 -y "$T/lossless.webp"; scase "webp lossless" "$T/lossless.webp"
ffmpeg -v error -f lavfi -i "color=c=red@0.5:s=320x240,format=rgba" -frames:v 1 -c:v libwebp -lossless 1 -y "$T/alpha.webp"; scase "webp with alpha" "$T/alpha.webp"
ffmpeg -v error -i "$T/s.png" -c:v tiff -compression_algo raw -y "$T/raw.tiff"; scase "tiff uncompressed" "$T/raw.tiff"
ffmpeg -v error -i "$T/s.png" -c:v tiff -compression_algo lzw -y "$T/lzw.tiff"; scase "tiff lzw" "$T/lzw.tiff"
ffmpeg -v error -i "$T/s.png" -c:v tiff -compression_algo deflate -y "$T/deflate.tiff"; scase "tiff deflate" "$T/deflate.tiff"
ffmpeg -v error -i "$T/s.png" -c:v tiff -compression_algo packbits -y "$T/pack.tiff"; scase "tiff packbits" "$T/pack.tiff"
# PNG (Commander and the demo tools hand it PNGs; zlib is already linked in for TIFF), by extension and by content alone
scase "png" "$T/s.png"
cp "$T/s.png" "$T/png-no-extension"; scase "png, found by its content (no extension)" "$T/png-no-extension"
ffmpeg -v error -f lavfi -i "color=c=red@0.5:s=320x240,format=rgba" -frames:v 1 -y "$T/alpha.png"; scase "png with alpha" "$T/alpha.png"
# JPEG, baseline and progressive (Commander draws a progressive one through ffmpeg: images.h does not), by extension and by content alone
ffmpeg -v error -f lavfi -i gradients=s=320x240:n=4:seed=7 -frames:v 1 -y "$T/g.ppm"
cjpeg -quality 90 "$T/g.ppm" > "$T/base.jpg"; scase "jpeg baseline" "$T/base.jpg"
cjpeg -quality 90 -progressive "$T/g.ppm" > "$T/prog.jpg"; scase "jpeg progressive" "$T/prog.jpg"
cp "$T/prog.jpg" "$T/prog-no-extension"; scase "jpeg progressive, found by its content (no extension)" "$T/prog-no-extension"
a="$("$MIN" -v error -i "$T/base.jpg" -frames:v 1 -f rawvideo -pix_fmt bgra pipe:1 | md5sum | cut -c1-12)"; b="$("$MIN" -v error -i "$T/prog.jpg" -frames:v 1 -f rawvideo -pix_fmt bgra pipe:1 | md5sum | cut -c1-12)"
if [ "$a" = "$b" ]; then echo "    ok    a progressive JPEG decodes to the same pixels as the baseline one"; else echo "    FAIL  progressive and baseline decode differently"; fail=1; fi
# a hand-made Photoshop file (no ffmpeg encoder exists): 320x240, 8-bit RGB, raw, planes of 0x40, 0x80 and 0xC0
{ printf '8BPS\000\001\000\000\000\000\000\000\000\003\000\000\000\360\000\000\001\100\000\010\000\003'; head -c 12 /dev/zero; printf '\000\000'
  for v in '\100' '\200' '\300'; do head -c $((320 * 240)) /dev/zero | tr '\000' "$v"; done; } > "$T/hand.psd"
scase "psd (hand-made, raw RGB)" "$T/hand.psd"
"$MIN" -v error -i "$T/hand.psd" -frames:v 1 -f rawvideo -pix_fmt bgra pipe:1 | head -c 4 | od -An -tx1 | grep -q '^ c0 80 40 ff' || { echo "    FAIL  psd first pixel is not B=c0 G=80 R=40 A=ff"; fail=1; }

# Rec demo's encode (theme/r3i6nos_demo.h r3_demo_encode_p), with the real command: raw BGRA frames on a pipe plus a WAV in, H.264 + AAC MP4 out
echo "[*] Rec demo's encode:"
ffmpeg -v error -f lavfi -i testsrc2=s=640x480:r=30:d=3 -pix_fmt bgra -f rawvideo -y "$T/frames.bgra"
ffmpeg -v error -f lavfi -i "sine=f=440:d=3:r=44100" -ac 2 -c:a pcm_s16le -y "$T/demo.wav"
"$MIN" -y -loglevel error -f rawvideo -pixel_format bgra -video_size 640x480 -framerate 30 -i pipe:0 -i "$T/demo.wav" -c:v libx264 -preset veryfast -crf 18 \
    -tune animation -pix_fmt yuv420p -c:a aac -b:a 128k -shortest -movflags +faststart "$T/demo.mp4" < "$T/frames.bgra"
vp="$(ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,pix_fmt,width,height,r_frame_rate,nb_frames -of default=nw=1 "$T/demo.mp4" | tr '\n' ' ')"
ap="$(ffprobe -v error -select_streams a:0 -show_entries stream=codec_name,channels,sample_rate -of default=nw=1 "$T/demo.mp4" | tr '\n' ' ')"
echo "    probed: $vp| $ap"
if echo "$vp" | grep -q 'codec_name=h264' && echo "$vp" | grep -q 'pix_fmt=yuv420p' && echo "$vp" | grep -q 'width=640 ' && echo "$vp" | grep -q 'height=480 ' && echo "$vp" | grep -q 'r_frame_rate=30/1'; then echo "    ok    H.264 yuv420p 640x480 30 fps"; else echo "    FAIL  video stream is not H.264 yuv420p 640x480 30 fps"; fail=1; fi
case "$vp" in *nb_frames=90*) echo "    ok    90 frames";; *) echo "    FAIL  not 90 frames"; fail=1;; esac
if echo "$ap" | grep -q 'codec_name=aac' && echo "$ap" | grep -q 'channels=2' && echo "$ap" | grep -q 'sample_rate=44100'; then echo "    ok    AAC stereo 44.1 kHz"; else echo "    FAIL  audio is not AAC stereo 44.1 kHz"; fail=1; fi
if [ "$(ffprobe -v error -show_entries format=format_name -of default=nw=1:nk=1 "$T/demo.mp4")" = "mov,mp4,m4a,3gp,3g2,mj2" ]; then echo "    ok    an MP4"; else echo "    FAIL  not an MP4"; fail=1; fi
# the full ffmpeg and the minimal one decode that MP4 to the same bytes, and its frames are close to what went in (lossy: crf 18)
a="$(ffmpeg -v error -i "$T/demo.mp4" -an -f rawvideo -pix_fmt bgra pipe:1 | md5sum | cut -c1-12)"; b="$("$MIN" -v error -i "$T/demo.mp4" -an -f rawvideo -pix_fmt bgra pipe:1 | md5sum | cut -c1-12)"
if [ "$a" = "$b" ]; then echo "    ok    decoded the same by both"; else echo "    FAIL  decoded differently (full $a, minimal $b)"; fail=1; fi
psnr="$(ffmpeg -v info -i "$T/demo.mp4" -f rawvideo -pix_fmt bgra -s 640x480 -framerate 30 -i "$T/frames.bgra" -lavfi "[0:v]format=bgra[a];[1:v]format=bgra[b];[a][b]psnr" -f null - 2>&1 | sed -n 's/.*average:\([0-9.]*\).*/\1/p')"
echo "    PSNR against the source frames: ${psnr:-unknown} dB"
# the recorder's own test, with this ffmpeg as the encoder
if [ -f "$REPO/tests/test_demo.c" ]; then
    # shellcheck disable=SC2046  # sdl2-config prints separate flags
    cc -std=c11 -Wall -Wextra -O2 -I "$REPO/theme" $(sdl2-config --cflags) -o "$T/test_demo" "$REPO/tests/test_demo.c" $(sdl2-config --libs) -lm
    if R3I6NOS_FFMPEG="$MIN" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$T/test_demo" 2>&1 | tail -3 | tee "$T/td.out" | grep -q 'test_demo: ok'; then echo "    ok    tests/test_demo.c with this ffmpeg as the encoder"; else cat "$T/td.out"; echo "    FAIL  tests/test_demo.c"; fail=1; fi
fi

[ "$fail" = 0 ] || { echo "ffmpeg-min: checks failed" >&2; exit 1; }
mkdir -p "$OUT"
install -m 0755 "$MIN" "$OUT/ffmpeg"
echo "[*] Built: $OUT/ffmpeg ($(stat -c %s "$OUT/ffmpeg") bytes)"

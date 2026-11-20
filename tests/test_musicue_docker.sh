#!/bin/bash
# tests/test_musicue_docker.sh: the MusiCue package (build/native/MusiCue.deb, from scripts/build-native-local.sh musicue-native) unpacked in a clean Debian 12 arm64
# container with the image's own libraries: the program finds the sync helper and the renderer packed beside it, the packed musicue-render makes a 20 s tone 1.25 times faster and 2 semitones up (its length is checked), makes and reads its demo song, draws a screenshot, and passes its own
# self-test. Needs Docker and the deb; says NOT RUN otherwise. Not a test of the unit (the sound device, the pad, the speed).
set -eu
cd "$(dirname "$0")/.."
command -v docker >/dev/null 2>&1 || { echo "NOT RUN: needs Docker"; exit 0; }
[ -f build/native/MusiCue.deb ] || { echo "NOT RUN: no build/native/MusiCue.deb (scripts/build-native-local.sh musicue-native)"; exit 0; }
out="$(docker run --rm --platform linux/arm64 -v "$PWD/build/native":/d:ro debian:12 sh -c '
set -eu
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq --no-install-recommends libsdl2-2.0-0 python3-minimal libpython3-stdlib dpkg >/dev/null 2>&1
mkdir /p && dpkg-deb -x /d/MusiCue.deb /p
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
echo "SYNC $(/p/usr/games/musicue --sync-path)"
test -x "$(/p/usr/games/musicue --sync-path)" && echo "SYNC-EXEC ok"
/p/usr/lib/musicue/musicue-sync --help >/dev/null && echo "HELPER-RUNS ok"
/p/usr/games/musicue --make-demo /t/MusiCue && echo "DEMO $(ls /t/MusiCue/media /t/MusiCue/cues | tr "\n" " ")"
/p/usr/games/musicue --screen player --theme Nord --screenshot /t/shot.bmp && echo "SHOT $(wc -c < /t/shot.bmp)"
echo "RENDER $(/p/usr/games/musicue --render-path)"
test -x "$(/p/usr/games/musicue --render-path)" && echo "RENDER-EXEC ok"
python3 -c "
import math, struct, wave
w = wave.open(\"/t/tone.wav\", \"wb\"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
w.writeframes(b\"\".join(struct.pack(\"<hh\", int(12000 * math.sin(2 * math.pi * 440 * i / 44100)), int(12000 * math.sin(2 * math.pi * 440 * i / 44100))) for i in range(44100 * 20)))
w.close()"
R=$(/p/usr/games/musicue --render-path)
"$R" --in /t/tone.wav --out /t/tone-copy.wav --ratio 1.25 --semitones 2 --workers 2 | sed "s/^/RENDERRUN /"
python3 -c "
import wave
w = wave.open(\"/t/tone-copy.wav\"); print(\"RENDERED\", w.getnframes(), w.getframerate(), w.getnchannels())"
ls /t | grep -q "tone-copy.wav\.\(part\|journal\)" && echo "RENDER-LEFTOVER bad"
/p/usr/games/musicue --selftest | sed "s/^/SELFTEST /"
' 2>&1)" || true
printf '  %s\n' "${out//$'\n'/$'\n  '}"
echo "$out" | grep -q "^SYNC .*/usr/games/../lib/musicue/musicue-sync" || { echo "FAIL: the helper is not found beside the program" >&2; exit 1; }
echo "$out" | grep -q "^SYNC-EXEC ok" || { echo "FAIL: the helper is not executable" >&2; exit 1; }
echo "$out" | grep -q "^HELPER-RUNS ok" || { echo "FAIL: the helper does not run on the image's Python" >&2; exit 1; }
echo "$out" | grep -q "^DEMO .*demoSong001.json" || { echo "FAIL: --make-demo made no cue file" >&2; exit 1; }
echo "$out" | grep -q "^DEMO .*MusiCue demo \[demoSong001\].wav" || { echo "FAIL: --make-demo made no song" >&2; exit 1; }
shot="$(printf '%s\n' "$out" | sed -n 's/^SHOT //p')"      # 640 x 480 x 4 bytes of pixels, and a header whose size depends on SDL's version
{ [ "${shot:-0}" -ge 1228800 ] && [ "${shot:-0}" -le 1229100 ]; } || { echo "FAIL: the screenshot is not a 640x480 32-bit BMP ($shot bytes)" >&2; exit 1; }
echo "$out" | grep -q "^RENDER .*/usr/games/../lib/musicue/musicue-render" || { echo "FAIL: the render helper is not found beside the program" >&2; exit 1; }
echo "$out" | grep -q "^RENDER-EXEC ok" || { echo "FAIL: the render helper is not executable" >&2; exit 1; }
echo "$out" | grep -q "^RENDERRUN DONE " || { echo "FAIL: the packed musicue-render did not finish" >&2; exit 1; }
echo "$out" | grep -q "^RENDERED 705600 44100 2$" || { echo "FAIL: the render's length is not 20 s / 1.25 (705600 frames)" >&2; exit 1; }
echo "$out" | grep -q "^RENDER-LEFTOVER" && { echo "FAIL: the render left its part or journal behind" >&2; exit 1; }
echo "$out" | grep -q "^SELFTEST R3I6NOS_SMOKE ok musicue=" || { echo "FAIL: the self-test" >&2; exit 1; }
echo "musicue docker OK"

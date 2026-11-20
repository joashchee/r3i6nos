#!/bin/bash
# tools/stretch-proto/test-render.sh BINDIR: the checks of musicue-render, run by build-proto.sh inside Debian 12 arm64 (BINDIR has stretch-proto and
# musicue-render). A 60 s synthetic song and a 40 s sine through the real program:
#   1. the length is exactly what the ratio says, the pitch is the key shift, the progress lines and DONE are printed, nothing is left behind;
#   2. one worker and four workers give the same bytes (the plan depends on the song, not the number of workers);
#   3. a run that dies after a checkpoint (a power cut), run again, gives the same bytes as a run that was not interrupted, and says it resumed;
#   4. a different ratio on the same part and journal starts again instead of continuing;
#   5. a SIGTERM leaves the part and the journal and exits 130, and the next run finishes the copy;
#   6. a song that is too short, one that is not audio and a ratio out of range are refused.
set -u
B="$1"; T="$(mktemp -d)"; fail=0
ok() { echo "PASS $1"; }
bad() { echo "FAIL $1"; fail=1; }
R="$B/musicue-render"; S="$B/stretch-proto"
if ! "$S" make-song "$T/song.wav" 60; then bad "make-song"; exit 1; fi
if ! "$S" make-sine "$T/sine.wav" 40 440; then bad "make-sine"; exit 1; fi

out="$("$R" --in "$T/sine.wav" --out "$T/a.wav" --ratio 0.8 --semitones 3 --workers 4)"; rc=$?
if [ "$rc" = 0 ] && grep -q '^DONE ' <<<"$out"; then ok "renders and says DONE"; else bad "renders and says DONE ($rc): $out"; fi
if grep -q '^PROGRESS [0-9]*/[0-9]* ' <<<"$out"; then ok "prints PROGRESS lines"; else bad "prints PROGRESS lines"; fi
if [ -e "$T/a.wav.part" ] || [ -e "$T/a.wav.journal" ]; then bad "no part or journal left behind"; else ok "no part or journal left behind"; fi
info="$("$S" info "$T/a.wav")"
want=$(( 40 * 44100 * 10 / 8 ))                  # 40 s at ratio 0.8: 40 * 44100 / 0.8 frames
frames="${info#frames=}"; frames="${frames%% *}"; hz="${info##*hz=}"
if [ "$frames" = "$want" ]; then ok "length $frames = $want frames"; else bad "length $frames, wanted $want"; fi
if awk "BEGIN{d=$hz-523.25; if (d<0) d=-d; exit !(d/523.25 < 0.004)}"; then ok "pitch $hz Hz (440 Hz up 3 semitones: 523.25)"; else bad "pitch $hz Hz, wanted 523.25"; fi

"$R" --in "$T/song.wav" --out "$T/w1.wav" --ratio 1.25 --semitones -2 --workers 1 > /dev/null || bad "1 worker"
"$R" --in "$T/song.wav" --out "$T/w4.wav" --ratio 1.25 --semitones -2 --workers 4 > /dev/null || bad "4 workers"
if cmp -s "$T/w1.wav" "$T/w4.wav"; then ok "1 worker and 4 workers give the same bytes"; else bad "1 worker and 4 workers differ"; fi

R3I6NOS_RENDER_DIE_AFTER=1 "$R" --in "$T/song.wav" --out "$T/cut.wav" --ratio 1.25 --semitones -2 --workers 4 > /dev/null; rc=$?
if [ "$rc" = 9 ] && [ -e "$T/cut.wav.part" ] && [ -e "$T/cut.wav.journal" ]; then ok "a power cut leaves the part and the journal"; else bad "a power cut leaves the part and the journal (exit $rc)"; fi
out="$("$R" --in "$T/song.wav" --out "$T/cut.wav" --ratio 1.25 --semitones -2 --workers 2)"
if grep -q 'Resumed from segment' <<<"$out"; then ok "the next run says it resumed"; else bad "the next run says it resumed: $out"; fi
if cmp -s "$T/cut.wav" "$T/w1.wav"; then ok "the resumed copy is the same bytes as an uninterrupted one"; else bad "the resumed copy differs from an uninterrupted one"; fi
R3I6NOS_RENDER_DIE_AFTER=2 "$R" --in "$T/song.wav" --out "$T/cut2.wav" --ratio 1.25 --semitones -2 --workers 3 > /dev/null
"$R" --in "$T/song.wav" --out "$T/cut2.wav" --ratio 1.25 --semitones -2 --workers 1 > /dev/null
if cmp -s "$T/cut2.wav" "$T/w1.wav"; then ok "a cut after the second checkpoint, resumed with another worker count: the same bytes"; else bad "a cut after the second checkpoint differs"; fi

R3I6NOS_RENDER_DIE_AFTER=1 "$R" --in "$T/song.wav" --out "$T/chg.wav" --ratio 1.25 --semitones -2 --workers 4 > /dev/null
out="$("$R" --in "$T/song.wav" --out "$T/chg.wav" --ratio 0.9 --semitones -2 --workers 4)"
"$R" --in "$T/song.wav" --out "$T/ref09.wav" --ratio 0.9 --semitones -2 --workers 4 > /dev/null
if grep -q 'Resumed' <<<"$out"; then bad "another ratio over an old journal starts again (it resumed)"
elif cmp -s "$T/chg.wav" "$T/ref09.wav"; then ok "another ratio over an old journal starts again"
else bad "another ratio over an old journal starts again (bytes differ)"; fi

"$S" make-song "$T/long.wav" 400
"$R" --in "$T/long.wav" --out "$T/term.wav" --ratio 1.25 --semitones -2 --workers 1 > /dev/null &
pid=$!; sleep 1.5; kill -TERM "$pid" 2>/dev/null; wait "$pid"; rc=$?
if [ "$rc" = 130 ] && [ -e "$T/term.wav.part" ] && [ -e "$T/term.wav.journal" ]; then
    ok "SIGTERM exits 130 and leaves the part and the journal"
    "$R" --in "$T/long.wav" --out "$T/term.wav" --ratio 1.25 --semitones -2 --workers 4 > /dev/null
    out="$("$R" --in "$T/long.wav" --out "$T/longref.wav" --ratio 1.25 --semitones -2 --workers 4)"
    echo "INFO a 400 s song (70 MiB of 16-bit audio), 4 workers: $(grep '^DONE' <<<"$out")"
    if cmp -s "$T/term.wav" "$T/longref.wav"; then ok "the run after a SIGTERM finishes the same bytes as an uninterrupted one"; else bad "the run after a SIGTERM differs"; fi
else bad "SIGTERM exits 130 and leaves the part and the journal (exit $rc)"; fi

"$S" make-sine "$T/short.wav" 5 440
out="$("$R" --in "$T/short.wav" --out "$T/short.out.wav" --ratio 1.1 --semitones 0)"; rc=$?
if [ "$rc" = 1 ] && grep -q '^ERROR ' <<<"$out"; then ok "a song under 8 s is refused"; else bad "a song under 8 s is refused ($rc): $out"; fi
echo "not audio" > "$T/junk.wav"
out="$(R3I6NOS_FFMPEG=/nonexistent "$R" --in "$T/junk.wav" --out "$T/junk.out.wav" --ratio 1.1 --semitones 0)"; rc=$?
if [ "$rc" = 1 ] && grep -q '^ERROR ' <<<"$out"; then ok "a file that is not audio is refused"; else bad "a file that is not audio is refused ($rc): $out"; fi
"$R" --in "$T/song.wav" --out "$T/x.wav" --ratio 9 --semitones 0 2>/dev/null; rc=$?
if [ "$rc" = 2 ]; then ok "a ratio out of range is a usage error"; else bad "a ratio out of range is a usage error ($rc)"; fi

rm -rf "$T"
if [ "$fail" = 0 ]; then echo "musicue-render: all passed"; else echo "musicue-render: FAILED"; fi
exit "$fail"

#!/bin/bash
# build_corpus.sh - rebuild r3i6nos's test corpus (corpus/files): one small file of each format the apps on r3i6nos read, every one
# made in code by the apps' own generators and writers or written by hand here, none of it anyone else's (CLAUDE.md rule 2).
# Then each file is opened by the app that reads it, and only if all of them open is corpus/files replaced. See corpus/README.md.
#
#   scripts/build_corpus.sh
#
# Needs the app repositories beside this one (R3I6NOS_PORTS_ROOT overrides where): neumetik (cargo), composer, painter, animator and
# klamansi (node 22+), and python3. The corpus is committed, so the image build (CI) never needs them; run this when an app's
# formats change, then commit corpus/files.

set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
PORTS="${R3I6NOS_PORTS_ROOT:-$(dirname "$REPO")}"
SRC="$REPO/corpus/src"
EXTRA=(python3 -I "$REPO/scripts/corpus_extra.py")
die() { echo "build_corpus: $*" >&2; exit 1; }
step() { printf '\n== %s\n' "$*"; }

for r in neumetik composer painter animator klamansi; do [ -d "$PORTS/$r" ] || die "no $r repository at $PORTS/$r (set R3I6NOS_PORTS_ROOT)"; done
command -v cargo >/dev/null || die "needs cargo (Neumetik's generators are Rust)"
command -v node >/dev/null || die "needs node 22+ (Composer, Painter, Animator, KlamANSI)"

work="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-corpus.XXXXXX")"
trap 'rm -rf "$work"' EXIT
out="$work/files"
mkdir -p "$out"

step "Neumetik: MIDI, tracker modules, music as text"
(cd "$PORTS/neumetik/r36s/lib" && cargo run --quiet --release --example corpus write "$out")
(cd "$PORTS/neumetik/tracker" && NEUMETIK_CORPUS_DIR="$out" cargo test --quiet --release corpus_file >/dev/null)
"${EXTRA[@]}" mxl "$out/notation.musicxml" "$out/notation.mxl"

step "Composer: notation and a cue sheet"
node "$PORTS/composer/cli.js" notate melody seed=corpus --out "$out/notation.cmp" >/dev/null
cp "$SRC/cue-sheet.json" "$out/cue-sheet.json"

step "Painter: a PNG and an ANSI file"
node "$PORTS/painter/cli.js" draw item --out "$out/painter.png" >/dev/null
node "$PORTS/painter/cli.js" draw item --style ansi --out "$out/painter.ans" >/dev/null

step "Animator: a clip"
node "$PORTS/animator/cli.js" make walk --out "$out/animator-clip.json" >/dev/null

step "KlamANSI, the menu, Commander"
cp "$SRC/test-corpus.kan" "$out/test-corpus.kan"
"${EXTRA[@]}" love "$SRC/hello-love" "$out/hello.love"
"${EXTRA[@]}" deb "$SRC/tool/r3i6nos-test-hello" "$out/r3i6nos-test-tool.deb"
"${EXTRA[@]}" text "$out/text-utf8.txt"
"${EXTRA[@]}" binary "$out/binary.bin"

step "Each file opened by the app that reads it"
mkdir "$work/neumetik"
while read -r f; do cp "$out/$f" "$work/neumetik/"; done < <("${EXTRA[@]}" list neumetik)
(cd "$PORTS/neumetik/r36s/lib" && cargo run --quiet --release --example corpus check "$work/neumetik") | tail -n 1
node "$PORTS/composer/cli.js" lint "$out/midi.mid" >/dev/null 2>&1 || [ $? -eq 1 ] || die "Composer could not read midi.mid"   # 1: findings, still read
node "$PORTS/composer/cli.js" notate "$out/notation.cmp" >/dev/null || die "Composer could not read notation.cmp"
node "$PORTS/composer/cli.js" cues "$out/cue-sheet.json" --dir "$work/cues" >/dev/null || die "Composer could not build cue-sheet.json"
for f in painter.png painter.ans; do
    node "$PORTS/painter/cli.js" lint "$out/$f" >/dev/null 2>&1 || [ $? -eq 1 ] || die "Painter could not read $f"
done
node "$PORTS/painter/cli.js" convert "$out/painter.png" --out "$work/conv.ans" >/dev/null 2>&1 || die "Painter could not convert painter.png"
node "$PORTS/animator/cli.js" lint "$out/animator-clip.json" >/dev/null 2>&1 || [ $? -eq 1 ] || die "Animator could not read animator-clip.json"
(cd "$PORTS/klamansi" && node --input-type=module -e "
import { readFileSync } from 'node:fs';
import { load } from './public/klam.js';
const { error } = load(readFileSync(process.argv[1], 'utf8'), 'test-corpus');
if (error) { console.error('KlamANSI: ' + error); process.exit(1); }" "$out/test-corpus.kan") || die "KlamANSI could not load test-corpus.kan"
echo "Composer, Painter, Animator and KlamANSI read theirs"

step "The manifest, and corpus/files replaced"
"${EXTRA[@]}" manifest "$out"
rm -rf "$REPO/corpus/files"
cp -R "$out" "$REPO/corpus/files"
du -sh "$REPO/corpus/files" | cut -f1 | sed 's/^/corpus\/files: /'

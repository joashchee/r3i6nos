#!/bin/bash
# scripts/corpus_formats_in_docker.sh <out folder>
#
# Runs INSIDE the Debian 12 container scripts/build_corpus.sh starts (as root): installs what scripts/corpus_formats.py needs, builds Commander,
# makes one file of every format Commander reads into <out folder>/<sub-folder>/ and fails unless `commander --info` gives each the Type line it
# is registered with. The container is arm64 on an Apple-silicon Mac (Colima), so the one program made with gcc is an ARM64 Linux binary, which is what
# the R36S runs. A rebuild is byte for byte the same.
set -eu
OUT="${1:?usage: $0 <out folder>}"
export DEBIAN_FRONTEND=noninteractive LC_ALL=C
cd "$(dirname "$0")/.."
apt-get update -qq >/dev/null
apt-get install -y -qq --no-install-recommends python3 python3-pil python3-brotli ffmpeg zstd lzip p7zip-full lua5.4 luajit gcc libc6-dev libsdl2-dev >/dev/null
# shellcheck disable=SC2046  # sdl2-config prints separate flags
gcc -std=c11 -O1 -Wno-unused-function -I theme $(sdl2-config --cflags) apps/commander-native/commander.c -o /tmp/commander $(sdl2-config --libs) -lm
python3 -I scripts/corpus_formats.py "$OUT" --verify /tmp/commander

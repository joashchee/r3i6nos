#!/bin/bash
# tests/test_runtimes_docker.sh: the five runtime packages (build/runtimes/*.deb, from scripts/build-runtimes.sh) installed by the real deb_watcher.py in a
# Debian 12 arm64 container with the image's libraries, and each engine run (Pyxel and Solarus on a virtual X server with Mesa's software GL; Ren'Py
# compiling a script). Needs Docker and the debs; says NOT RUN otherwise. Not a test of the unit: GLES on the Mali, the pad, the sound and the memory are
# for the device report (docs/RUNTIMES.md).
set -eu
cd "$(dirname "$0")/.."
command -v docker >/dev/null 2>&1 || { echo "NOT RUN: needs Docker"; exit 0; }
ls build/runtimes/*.deb >/dev/null 2>&1 || { echo "NOT RUN: no build/runtimes/*.deb (scripts/build-runtimes.sh)"; exit 0; }
out="$(docker run --rm --platform linux/arm64 -v "$PWD/tests/runtimes":/e:ro -v "$PWD/build/runtimes":/debs:ro -v "$PWD/overlay/usr/local/bin":/w:ro -v "$PWD/tools/runtimes/samples":/s:ro -v "$PWD/theme/font/ansiapps-mono":/f:ro debian:12 sh /e/e2e.sh 2>&1)" || true
echo "$out" | grep -E '^E2E |^  ' || true
if echo "$out" | grep -q '^E2E .* FAIL'; then echo "FAIL: runtimes in Docker" >&2; echo "$out" | tail -20 >&2; exit 1; fi
for n in install-pyxel install-renpy install-solarus install-ags install-godot ags godot pyxel renpy solarus sample-pyxel sample-renpy sample-solarus font-pyxel; do echo "$out" | grep -q "^E2E $n " || { echo "FAIL: no result for $n" >&2; exit 1; }; done
echo "runtimes docker OK"

#!/usr/bin/env bash
# Build R3i6nos Mono: the tables (stdlib Python, on this machine), then TTF, WOFF and WOFF2 with fontTools in Debian 12 under Docker, then
# check every glyph of the TTF and the WOFF2 renders pixel for pixel as its source bitmap (FreeType, 20 px, no anti-aliasing).
# One command, no arguments (rule 10's spirit for the font). Needs Docker (Colima up) for the TrueType step.
set -euo pipefail
cd "$(dirname "$0")/.." || exit 1
python3 scripts/build_r3_font.py
if ! docker info >/dev/null 2>&1; then
    echo "build-r3-font: Docker is not running: the tables are made, the TrueType files are not (start Colima and run this again)" >&2
    exit 2
fi
IMG=r3i6nos-fontbuild
if ! docker image inspect "$IMG" >/dev/null 2>&1; then
    docker build -q -t "$IMG" - <<'DOCKER'
FROM debian:12
RUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends python3 python3-fonttools python3-brotli python3-pil >/dev/null && rm -rf /var/lib/apt/lists/*
DOCKER
fi
docker run --rm -v "$PWD":/work -w /work "$IMG" bash -c '
    set -e
    python3 scripts/r3font/ttf.py theme/font/r3i6nos-mono
    python3 scripts/r3font/verify_ttf.py theme/font/r3i6nos-mono'
ls -l theme/font/r3i6nos-mono/R3i6nosMono-Regular.{ttf,woff,woff2}

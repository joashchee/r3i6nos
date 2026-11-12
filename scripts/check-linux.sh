#!/bin/bash
# Runs scripts/check.sh inside Debian 12 (what CI runs) so that nothing is skipped for being on a Mac: GCC, shellcheck, luajit, Node, alsa-utils
# (the ALSA tests), Pillow (the font check), and, as root in a privileged container, the image test that needs loop devices.
# Needs Docker (on this Mac: Colima, `colima start`, CLAUDE.md memory "Local tools"). The first run builds the image (a few minutes); later runs reuse it.
#
#   scripts/check-linux.sh            the whole check
#   scripts/check-linux.sh --rebuild  rebuild the image first (after changing the package list below)
#
# The repo is copied into the container (without build/, local_hardware_assets/ and vendor/love-mac): nothing the check writes reaches this checkout.
set -eu
cd "$(dirname "$0")/.."

IMAGE="${R3I6NOS_CHECK_IMAGE:-r3i6nos-check:bookworm-2}"      # bump the tag when the package list below changes
command -v docker >/dev/null || { echo "check-linux: needs docker (colima start)" >&2; exit 2; }
docker info >/dev/null 2>&1 || { echo "check-linux: docker is not running (colima start)" >&2; exit 2; }

if [ "${1:-}" = "--rebuild" ] || ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "[*] Building $IMAGE (Debian 12 with the tools CI uses)"
    docker build -t "$IMAGE" - <<'DOCKERFILE'
FROM debian:bookworm
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates git gcc make pkg-config libc6-dev libsdl2-dev python3 python3-pil \
        shellcheck luajit lua5.4 alsa-utils xz-utils zstd patch ffmpeg \
        dosfstools mtools e2fsprogs fdisk util-linux squashfs-tools dpkg curl file \
    && rm -rf /var/lib/apt/lists/*
# Node 22 (Debian 12's is 18, which cannot import the QuickJS shims' CommonJS files by name; the Mac's and CI's are newer)
RUN arch="$(dpkg --print-architecture)"; case "$arch" in amd64) arch=x64 ;; esac; \
    curl -fsSL "https://nodejs.org/dist/v22.11.0/node-v22.11.0-linux-$arch.tar.xz" | tar -xJ -C /usr/local --strip-components=1 \
    && node --version
DOCKERFILE
fi

# --privileged: tests/test_image.sh makes loop devices. The copy keeps .git (check.sh asks git what is tracked). The web app that the boot disk
# parity tests compare against is in ../ansiapps-site; when it is there it is mounted (without it those tests say NOT RUN).
SITE=()
if [ -d "$PWD/../ansiapps-site/r3i6nos/public/bootdisk" ]; then
    SITE=(-v "$(cd .. && pwd)/ansiapps-site:/ansiapps-site:ro" -e R3I6NOS_SITE_REPO=/ansiapps-site)
fi
exec docker run --rm --privileged -v "$PWD":/src:ro ${SITE[@]+"${SITE[@]}"} -w /work "$IMAGE" bash -c '
set -e
mkdir -p /work
tar -C /src --exclude=./build --exclude=./local_hardware_assets --exclude=./vendor/love-mac -cf - . | tar -C /work -xf -
cd /work
git config --global --add safe.directory /work
set +e
scripts/check.sh
'

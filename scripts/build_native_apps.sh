#!/bin/bash
# build_native_apps.sh
#
# Builds every native app marked DEFAULT=1 in its apps/*/app.conf, with build_native_app.sh, into build/native/.
# configure_rootfs.sh puts those .deb files into the image at /opt/loveos/defaults/native_deb, where
# deb_watcher.py installs them at boot. Apps that are not defaults (ports, extras) have their own workflow
# and are not part of the firmware.
#
# Run as root in a Debian 12 container, like build_love.sh:
#
#   docker run --rm -v "$PWD":/src -w /src debian:bookworm scripts/build_native_apps.sh

set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"
rm -rf "${R3I6NOS_NATIVE_OUT:-$REPO/build/native}"

built=0
for conf in "$REPO"/apps/*/app.conf; do
    [ -f "$conf" ] || continue
    # shellcheck disable=SC1090  # the app's own manifest
    if (TYPE=native DEFAULT=0; . "$conf"; [ "$TYPE" = native ] && [ "$DEFAULT" = 1 ]); then
        "$REPO/scripts/build_native_app.sh" "$(basename "$(dirname "$conf")")"
        built=$((built + 1))
    fi
done
[ "$built" -gt 0 ] || { echo "build_native_apps: no app has DEFAULT=1" >&2; exit 1; }

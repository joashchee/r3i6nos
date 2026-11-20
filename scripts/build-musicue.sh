#!/bin/bash
# scripts/build-musicue.sh [--no-smoke]
#
# Builds MusiCue for the R36S on this computer, with no arguments: build/native/MusiCue.deb (package r36s-musicue, arm64, Debian 12, with
# musicue-sync packed beside the program). Needs only Docker (Colima on an Apple chip). It is scripts/build-native-local.sh musicue-native,
# so the binary links against Bookworm's SDL2 and glibc, the R36S rootfs's own, and the packed .deb's `musicue --selftest` runs (dummy video
# and sound) unless --no-smoke. The smoke test proves the binary runs and links, not that it draws or plays on the panel: that needs a unit.
#
# MusiCue is a user-installed package (DEFAULT=0), not in the image: copy build/native/MusiCue.deb to native_deb/ on the games card (the boot
# watcher installs it) or push it with scripts/push-packages.sh. The git commit shown in About comes from R3I6NOS_BUILD_COMMIT (default: HEAD).
set -eu
case "${1:-}" in
    -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    ""|--no-smoke) ;;
    *) echo "build-musicue: unknown option: $1 (try --help)" >&2; exit 2 ;;
esac
exec "$(dirname "$0")/build-native-local.sh" musicue-native "$@"

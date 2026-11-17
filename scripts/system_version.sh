#!/bin/bash
# scripts/system_version.sh - the version of the system package for the current checkout: <VERSION>.<number of commits>, e.g. 0.1.2.183.
# It only ever grows with the history, so the Debian version comparison (r3i6nos-sysupdate "only goes forward") orders builds the way git does.
# configure_rootfs.sh records it as the image's baseline; build-system-package.sh stamps it on the package. R3I6NOS_SYSTEM_VERSION overrides
# it (a tree without git, a test).
set -eu
REPO="$(cd "$(dirname "$0")/.." && pwd)"
if [ -n "${R3I6NOS_SYSTEM_VERSION:-}" ]; then printf '%s\n' "$R3I6NOS_SYSTEM_VERSION"; exit 0; fi
n="$(git -c safe.directory="$REPO" -C "$REPO" rev-list --count HEAD 2>/dev/null || echo 0)"
printf '%s.%s\n' "$(tr -d '[:space:]' < "$REPO/VERSION")" "$n"

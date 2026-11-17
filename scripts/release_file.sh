#!/bin/bash
# scripts/release_file.sh - print the contents of /etc/r3i6nos-release for the current checkout (About shows it; r3i6nos-sysupdate reads VERSION).
# One place for both scripts/configure_rootfs.sh (the image's file) and scripts/build-system-package.sh (a system package carries the new one, which
# r3i6nos-sysupdate writes last). R3I6NOS_COMMIT, R3I6NOS_BUILT_BY and R3I6NOS_SYSTEM_VERSION override, as they always did.
set -eu
REPO="$(cd "$(dirname "$0")/.." && pwd)"
git_() { git -c safe.directory="$REPO" -C "$REPO" "$@" 2>/dev/null; }
commit="${R3I6NOS_COMMIT:-$(git_ rev-parse HEAD || echo unknown)}"
modified=0
if [ -z "${R3I6NOS_COMMIT:-}" ] && [ -n "$(git_ status --porcelain --untracked-files=no)" ]; then modified=1; fi
cat <<EOT
VERSION=$(tr -d '[:space:]' < "$REPO/VERSION")
COMMIT=$commit
COMMIT_DATE=$(git_ show -s --format=%cs "$commit" || echo unknown)
MODIFIED=$modified
BUILT=$(date -u +%Y-%m-%d)
BUILT_BY=${R3I6NOS_BUILT_BY:-$([ -n "${GITHUB_ACTIONS:-}" ] && echo github || echo local)}
REPO=joashchee/r3i6nos
SYSTEM_VERSION=$("$REPO/scripts/system_version.sh")
EOT

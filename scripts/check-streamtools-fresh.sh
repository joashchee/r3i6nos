#!/bin/bash
# scripts/check-streamtools-fresh.sh: is the yt-dlp pinned in tools/streamtools/pins.conf the newest release? (docs/STREAM-TOOLS.md, "Updating")
#
# yt-dlp breaks every few weeks when a site changes, and a frozen package stops working, so a newer release is a reason to rebuild. This asks GitHub for the
# latest release and compares it with YTDLP_VERSION:
#   exit 0  the pin is the latest (or newer: a nightly, say)
#   exit 1  a newer release is out: prints the lines to put in pins.conf (the version, the URL and the zipapp's sha256 from the release's own SHA2-256SUMS)
#           and the commands to rebuild; it changes nothing
#   exit 3  GitHub could not be asked (no answer, or an answer that is not a release)
# Run weekly by .github/workflows/streamtools-fresh.yml; by hand: scripts/check-streamtools-fresh.sh. GITHUB_TOKEN, when set, raises the API's rate limit.
# Overrides, for tests: R3I6NOS_GITHUB_API (https://api.github.com), R3I6NOS_GITHUB_DL (https://github.com), R3I6NOS_PINS (tools/streamtools/pins.conf).
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
PINS="${R3I6NOS_PINS:-$REPO/tools/streamtools/pins.conf}"
API="${R3I6NOS_GITHUB_API:-https://api.github.com}"
DL="${R3I6NOS_GITHUB_DL:-https://github.com}"
# shellcheck disable=SC1090  # the pins
. "$PINS" || { echo "check-streamtools-fresh: cannot read $PINS" >&2; exit 3; }
[ -n "${YTDLP_VERSION:-}" ] || { echo "check-streamtools-fresh: $PINS does not set YTDLP_VERSION" >&2; exit 3; }

auth=()
[ -n "${GITHUB_TOKEN:-}" ] && auth=(-H "Authorization: Bearer $GITHUB_TOKEN")
body="$(curl -fsSL --max-time 30 ${auth[@]+"${auth[@]}"} -H "Accept: application/vnd.github+json" "$API/repos/yt-dlp/yt-dlp/releases/latest" 2>/dev/null)" \
    || { echo "check-streamtools-fresh: could not ask GitHub for yt-dlp's latest release" >&2; exit 3; }
tag="$(printf '%s' "$body" | python3 -I -c 'import json,sys; print(json.load(sys.stdin)["tag_name"])' 2>/dev/null)" \
    || { echo "check-streamtools-fresh: GitHub's answer has no tag_name" >&2; exit 3; }
case "$tag" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) echo "check-streamtools-fresh: '$tag' is not a yt-dlp version" >&2; exit 3 ;;
esac

newer="$(python3 -I -c '
import sys
a = tuple(int(x) for x in sys.argv[1].split("."))
b = tuple(int(x) for x in sys.argv[2].split("."))
print("yes" if a > b else "no")' "$tag" "$YTDLP_VERSION" 2>/dev/null)" || { echo "check-streamtools-fresh: cannot compare $tag with $YTDLP_VERSION" >&2; exit 3; }
if [ "$newer" != yes ]; then
    echo "yt-dlp $YTDLP_VERSION is the latest ($tag is the newest release)"
    exit 0
fi

sums="$(curl -fsSL --max-time 30 "$DL/yt-dlp/yt-dlp/releases/download/$tag/SHA2-256SUMS" 2>/dev/null)" || sums=""
sha="$(printf '%s\n' "$sums" | awk '$2 == "yt-dlp" { print $1 }' | head -n 1)"
echo "A newer yt-dlp is out: $tag (the package has $YTDLP_VERSION)."
echo "Edit tools/streamtools/pins.conf:"
echo "  YTDLP_VERSION=$tag"
echo "  YTDLP_URL=https://github.com/yt-dlp/yt-dlp/releases/download/$tag/yt-dlp"
echo "  YTDLP_SHA256=${sha:-<the yt-dlp line of the release SHA2-256SUMS>}"
echo "Then: scripts/build-streamtools.sh --relock && scripts/build-streamtools.sh && tests/test_streamtools_docker.sh"
echo "(read what changed upstream first; docs/STREAM-TOOLS.md, \"Updating\")"
exit 1

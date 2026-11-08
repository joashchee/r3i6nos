#!/bin/bash
# fetch_app_debs.sh [out-dir]: downloads the latest release of each app in apps/external-debs.txt (the .deb and its .sha256) into build/external_deb/,
# checks the sha256 and, with scripts/check_deb.py, that each is a real package with its licence and no DRAFT GRANT marker. configure_rootfs.sh puts
# them in /opt/loveos/defaults/native_deb, so every image carries the latest of each (deb_watcher.py installs them at boot).
#
# Needs `gh` logged in with read access to those repos (in CI, GH_TOKEN from the APP_DEBS_TOKEN secret: a fine-grained token with Contents: read on them).
# Any app that cannot be fetched fails the run: an image must not silently lack one. R3I6NOS_GH, R3I6NOS_EXTERNAL_LIST and R3I6NOS_EXTERNAL_DEBS override
# the gh command, the list and the output folder (tests use fakes).
set -eu
REPO="$(cd "$(dirname "$0")/.." && pwd)"
LIST="${R3I6NOS_EXTERNAL_LIST:-$REPO/apps/external-debs.txt}"
OUT="${1:-${R3I6NOS_EXTERNAL_DEBS:-$REPO/build/external_deb}}"
GH="${R3I6NOS_GH:-gh}"
die() { echo "fetch_app_debs: $*" >&2; exit 1; }
command -v "$GH" >/dev/null || die "$GH not found (install the GitHub CLI and run gh auth login)"
if command -v sha256sum >/dev/null; then SUM=(sha256sum -c); else SUM=(shasum -a 256 -c); fi
T="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-appdebs.XXXXXX")"; trap 'rm -rf "$T"' EXIT
mkdir -p "$OUT"; find "$OUT" -maxdepth 1 -name '*.deb' -delete
n=0
while read -r repo asset rest; do
    case "$repo" in ''|'#'*) continue ;; esac
    [ -n "$asset" ] && [ -z "$rest" ] || die "bad line in $LIST: $repo $asset $rest"
    case "$asset" in */*|.*) die "bad asset name: $asset" ;; esac
    d="$T/$n"; mkdir -p "$d"
    "$GH" release download --repo "$repo" --pattern "$asset" --pattern "$asset.sha256" --dir "$d" >/dev/null \
        || die "no release of $repo with $asset and $asset.sha256 (publish one: tag r36s-v* in that repo)"
    [ -f "$d/$asset" ] && [ -f "$d/$asset.sha256" ] || die "$repo's latest release lacks $asset or $asset.sha256"
    (cd "$d" && "${SUM[@]}" "$asset.sha256" >/dev/null) || die "$repo: $asset does not match its sha256"
    pkg="$(python3 -I "$REPO/scripts/check_deb.py" "$d/$asset")" || exit 1
    cp "$d/$asset" "$OUT/$asset"
    echo "fetch_app_debs: $asset ($pkg) from $repo"
    n=$((n + 1))
done < "$LIST"
[ "$n" -gt 0 ] || die "$LIST lists nothing"
echo "fetch_app_debs: $n packages in $OUT"

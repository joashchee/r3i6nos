#!/bin/bash
# Runs every check the CI runs. Tools that are not installed are reported, not skipped silently.
set -u
cd "$(dirname "$0")/.." || exit 1
fail=0

step() { echo "== $1"; }

step "no vendor or huge files tracked"
if git rev-parse --git-dir >/dev/null 2>&1; then
    bad="$(git ls-files | grep -E '^local_hardware_assets|\.(img|zip)(\.gz)?$' || true)"
    big="$(git ls-files -z | xargs -0 find -maxdepth 0 -size +20M 2>/dev/null || true)"
    if [ -n "$bad$big" ]; then
        echo "FAIL: tracked files that must not be in the repo:" >&2
        printf '%s\n%s\n' "$bad" "$big" >&2
        fail=1
    fi
else echo "NOT RUN: not a git checkout"; fi

step "python tests"
python3 -I -m unittest discover -s tests -v || fail=1

step "lua tests"
if command -v luajit >/dev/null; then luajit tests/test_scan.lua || fail=1
elif command -v lua >/dev/null; then lua tests/test_scan.lua || fail=1
else echo "NOT RUN: no lua/luajit"; fi

step "lua syntax"
if command -v luajit >/dev/null; then
    for f in menu/*.lua tests/smoke/love/*.lua; do
        luajit -bl "$f" >/dev/null || fail=1
    done
else echo "NOT RUN: no luajit"; fi

step "shell tests"
bash tests/test_configure_rootfs.sh || fail=1
bash tests/test_image.sh || fail=1

step "shell syntax"
for f in overlay/usr/local/bin/run_app.sh overlay/usr/local/bin/loveos-session scripts/*.sh tests/*.sh; do
    bash -n "$f" || fail=1
done

step "shellcheck"
if command -v shellcheck >/dev/null; then
    shellcheck overlay/usr/local/bin/run_app.sh overlay/usr/local/bin/loveos-session scripts/*.sh tests/*.sh || fail=1
else echo "NOT RUN: shellcheck not installed"; fi

exit $fail

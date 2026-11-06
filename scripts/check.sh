#!/bin/bash
# Runs every check the CI runs. Tools that are not installed are reported, not skipped silently.
set -u
cd "$(dirname "$0")/.." || exit 1
fail=0

step() { echo "== $1"; }

step "python tests"
python3 -I -m unittest discover -s tests -v || fail=1

step "lua tests"
if command -v luajit >/dev/null; then luajit tests/test_scan.lua || fail=1
elif command -v lua >/dev/null; then lua tests/test_scan.lua || fail=1
else echo "NOT RUN: no lua/luajit"; fi

step "shell syntax"
for f in overlay/usr/local/bin/run_app.sh overlay/etc/init.d/S99loveos scripts/*.sh; do
    bash -n "$f" || fail=1
done

step "shellcheck"
if command -v shellcheck >/dev/null; then
    shellcheck overlay/usr/local/bin/run_app.sh overlay/etc/init.d/S99loveos scripts/*.sh || fail=1
else echo "NOT RUN: shellcheck not installed"; fi

exit $fail

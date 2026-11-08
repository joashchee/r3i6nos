#!/bin/bash
# test_qjs_node.sh - builds tests/qjs_node_sample/cli.js into a QuickJS binary with tools/qjs-node/build.sh and checks it does what Node does with it:
# the same stdout, stderr, exit code and files for several command lines. Needs node, a C compiler and the pinned QuickJS tarball
# (R3I6NOS_QUICKJS_TAR, or vendor/quickjs-2024-01-13.tar.xz); without them it says NOT RUN and exits 0, as check.sh does for other missing tools.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
TAR="${R3I6NOS_QUICKJS_TAR:-$REPO/vendor/quickjs-2024-01-13.tar.xz}"
for tool in node cc; do command -v "$tool" >/dev/null || { echo "NOT RUN: test_qjs_node needs $tool"; exit 0; }; done
[ -f "$TAR" ] || { echo "NOT RUN: test_qjs_node needs the QuickJS tarball (R3I6NOS_QUICKJS_TAR or vendor/quickjs-2024-01-13.tar.xz)"; exit 0; }

TMP="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-qjs-node.XXXXXX")"; trap 'rm -rf "$TMP"' EXIT
"$REPO/tools/qjs-node/build.sh" "$REPO/tests/qjs_node_sample" cli.js sample "$TAR" "$TMP/bin" >/dev/null || { echo "test_qjs_node: build failed" >&2; exit 1; }

fail=0
TMPDIR_N="$TMP/n"; TMPDIR_Q="$TMP/q"
while IFS= read -r args; do
    rm -rf "$TMP/n" "$TMP/q"; mkdir -p "$TMP/n" "$TMP/q"
    # shellcheck disable=SC2086  # the case's words
    (cd "$TMP/n" && node "$REPO/tests/qjs_node_sample/cli.js" $args >.stdout 2>.stderr; echo $? >.rc)
    # shellcheck disable=SC2086
    (cd "$TMP/q" && "$TMP/bin/sample" $args >.stdout 2>.stderr; echo $? >.rc)
    if [ "${args%% *}" = throws ] || [ "${args%% *}" = reject ]; then
        # an uncaught error: Node prints its own stack format, so compare what matters: a non-zero status and the message on stderr
        if [ "$(cat "$TMPDIR_Q/.rc" 2>/dev/null)" != 0 ] && grep -q "from the sample" "$TMPDIR_Q/.stderr" && [ "$(cat "$TMPDIR_N/.rc")" = "$(cat "$TMPDIR_Q/.rc")" ]; then echo "  ok: $args (status $(cat "$TMPDIR_Q/.rc"), message on stderr)"; else echo "test_qjs_node: an uncaught error is not reported like Node's: $args" >&2; fail=1; fi
    elif diff -r "$TMP/n" "$TMP/q" >/dev/null; then echo "  ok: $args"; else echo "test_qjs_node: DIFFERENT for: $args" >&2; diff -r "$TMP/n" "$TMP/q" | head -8 >&2; fail=1; fi
done <<'CASES'
echo hello world
echo --loud hi there
files --dir out
files
misc
misc --n 21
exit 0
exit 3
nonsense
throws
reject
CASES
if [ "$fail" = 0 ]; then echo "test_qjs_node: ok"; else exit 1; fi

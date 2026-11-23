#!/bin/bash
# tests/test_run_app_runtime.sh: run_app.sh's pyxel, renpy, solarus, ags and godot types (docs/RUNTIMES.md): the runtime's command is found on the tools PATH and gets the
# target, its exit code comes back, a missing runtime says which package to install, and Select+Start (r3i6nos-portquit, faked here) ends a game that hangs.
set -eu
cd "$(dirname "$0")/.."
T="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-ra.XXXXXX")"; trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
mkdir -p "$T/bin" "$T/game"
export R3I6NOS_TOOLS_DIR="$T/bin" R3I6NOS_PORTQUIT="$T/nope"
for e in pyxel renpy solarus ags godot; do
    # shellcheck disable=SC2016  # $1 is for the fake, when it runs
    printf '#!/bin/bash\necho "RAN %s $1"\nexit "${FAKE_RC:-0}"\n' "$e" > "$T/bin/r3i6nos-run-$e"; chmod +x "$T/bin/r3i6nos-run-$e"
    out="$(overlay/usr/local/bin/run_app.sh "$e" "$T/game")" || fail "$e: run_app.sh failed"
    [ "$out" = "RAN $e $T/game" ] || fail "$e: got '$out'"
    FAKE_RC=3 overlay/usr/local/bin/run_app.sh "$e" "$T/game" >/dev/null && fail "$e: exit code lost"
done
# the exit code also comes back through the quit guard
printf '#!/bin/bash\nsleep 30 & wait\n' > "$T/guard"; chmod +x "$T/guard"
# shellcheck disable=SC2016  # $1 is for the fake, when it runs
printf '#!/bin/bash\nsleep 0.3\nkill -TERM -- -"$1"\n' > "$T/quit"; chmod +x "$T/quit"
printf '#!/bin/bash\nsleep 30\n' > "$T/bin/r3i6nos-run-pyxel"
if R3I6NOS_PORTQUIT="$T/quit" timeout 20 overlay/usr/local/bin/run_app.sh pyxel "$T/game" >/dev/null 2>&1; then fail "a hanging game was not ended"; fi
# a missing runtime names its package
rm "$T/bin/r3i6nos-run-solarus"
if err="$(overlay/usr/local/bin/run_app.sh solarus "$T/game" 2>&1)"; then fail "ran without the runtime"; fi
echo "$err" | grep -q "r3i6nos-runtime-solarus" || fail "no package named: $err"
if command -v setsid >/dev/null 2>&1; then   # the guarded launch (and so the log) needs setsid, which macOS has not; CI and the unit do
# the log: the engine's output and its exit are written to r3i6nos/runtime-<type>.log, and the state lines come while it runs
mkdir -p "$T/log"
printf '#!/bin/bash\necho "ENGINE SAYS HI"\necho "ENGINE ERR" >&2\nexit 4\n' > "$T/bin/r3i6nos-run-godot"
printf '#!/bin/bash\nsleep 0.2\n' > "$T/quit-idle"; chmod +x "$T/quit-idle"
R3I6NOS_LOG_DIR="$T/log" R3I6NOS_PORTQUIT="$T/quit-idle" overlay/usr/local/bin/run_app.sh godot "$T/game" >/dev/null 2>&1 && fail "godot exit 4 lost"
grep -q "ENGINE SAYS HI" "$T/log/runtime-godot.log" || fail "the engine's output is not in the log"
grep -q "ENGINE ERR" "$T/log/runtime-godot.log" || fail "the engine's errors are not in the log"
grep -q "== exit 4 after" "$T/log/runtime-godot.log" || fail "the exit is not in the log"
head -1 "$T/log/runtime-godot.log" | grep -q "godot $T/game" || fail "the log has no header"
# a game that runs longer than 5 s gets a state line (stop it with the fake quit)
printf '#!/bin/bash\nsleep 7\n' > "$T/bin/r3i6nos-run-godot"
R3I6NOS_LOG_DIR="$T/log" R3I6NOS_PORTQUIT="$T/quit-idle" overlay/usr/local/bin/run_app.sh godot "$T/game" >/dev/null 2>&1 || fail "a 7 s game failed"
grep -q "^t=5s state=" "$T/log/runtime-godot.log" || fail "no state line after 5 s: $(cat "$T/log/runtime-godot.log")"
fi
echo "run_app runtime types OK"

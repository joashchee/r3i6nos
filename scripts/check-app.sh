#!/bin/bash
# scripts/check-app.sh APP [--theme]      (also: scripts/check.sh --app APP [--theme])
#
# The checks for one native app, without the rest of the repository's: what `scripts/check.sh` does for that app and nothing else, so a
# change to MusiCue is checked in a minute, not in the ten the whole run takes. APP is the folder under apps/ (musicue-native).
#   1. the app compiles (-Werror under clang) and its --selftest prints the R3I6NOS_SMOKE line (the same command as check.sh's native step);
#   2. the Python tests named for it (tests/test_<app>*.py, without "-native"), run as `python3 -I`;
#   3. bash -n and shellcheck on the scripts named for it (scripts/*<app>*.sh);
#   --theme also builds and runs the tests of the theme headers the app compiles against (input and main menu, themes, report, toast, demo, appdata).
# Not a substitute for scripts/check.sh before a commit that touches anything else. Tools that are missing are reported, never skipped silently.
set -u
cd "$(dirname "$0")/.." || exit 1
case "${1:-}" in -h|--help|"") sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;; esac
app="$1"; shift
theme=0
for a in "$@"; do
    case "$a" in --theme) theme=1 ;; *) echo "check-app: unknown option: $a (try --help)" >&2; exit 2 ;; esac
done
conf="apps/$app/app.conf"
[ -f "$conf" ] || { echo "check-app: no $conf" >&2; exit 2; }
fail=0
step() { echo "== $1"; }

# shellcheck disable=SC1090  # the app's own manifest
read -r name src < <(TYPE=native; . "$conf"; [ "$TYPE" = native ] && echo "$BIN $(dirname "$conf")/$SRC")
[ -n "${name:-}" ] || { echo "check-app: $app is not a native app (a LÖVE app is checked by scripts/build_love_app.sh $app)" >&2; exit 2; }

step "$app: compile and --selftest"
if command -v cc >/dev/null && command -v sdl2-config >/dev/null; then
    werror=""; cc --version 2>/dev/null | grep -qi clang && werror="-Werror"
    bin="$(mktemp "${TMPDIR:-/tmp}/r3i6nos-app.XXXXXX")"
    # shellcheck disable=SC2046,SC2086  # sdl2-config prints separate flags; $werror is empty or one flag
    if cc -std=c11 -Wall -Wextra $werror -O2 -I theme $(sdl2-config --cflags) -o "$bin" "$src" $(sdl2-config --libs) -lm; then
        out="$(SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$bin" --selftest 2>&1)"
        echo "$out"
        grep -q "^R3I6NOS_SMOKE ok $name=" <<<"$out" || { echo "FAIL: $name selftest"; fail=1; }
    else echo "FAIL: $name does not compile"; fail=1; fi
    rm -f "$bin"
else echo "NOT RUN: needs a C compiler and libsdl2-dev (sdl2-config)"; fail=1; fi

base="${app%-native}"; pat="${base//-/_}"
step "$app: python tests (tests/test_${pat}*.py)"
found=0
for t in tests/test_"$pat"*.py; do
    [ -f "$t" ] || continue
    found=1
    python3 -I -m unittest discover -s tests -p "$(basename "$t")" -v || fail=1
done
[ "$found" = 1 ] || echo "none"

step "$app: shell scripts (scripts/*${base}*.sh)"
found=0
for f in scripts/*"$base"*.sh; do
    [ -f "$f" ] || continue
    found=1
    bash -n "$f" || fail=1
    if command -v shellcheck >/dev/null; then shellcheck "$f" || fail=1; else echo "NOT RUN: shellcheck not installed ($f)"; fi
done
[ "$found" = 1 ] || echo "none"

if [ "$theme" = 1 ]; then
    step "theme headers the app compiles against"
    if command -v cc >/dev/null && command -v sdl2-config >/dev/null; then
        for t in input themes report toast demo appdata; do
            tb="$(mktemp "${TMPDIR:-/tmp}/r3i6nos-theme.XXXXXX")"
            # shellcheck disable=SC2046,SC2086  # sdl2-config prints separate flags; $werror is empty or one flag
            if cc -std=c11 -Wall -Wextra $werror -O2 -I theme $(sdl2-config --cflags) -o "$tb" "tests/test_$t.c" $(sdl2-config --libs) -lm; then
                SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$tb" || fail=1
            else echo "FAIL: tests/test_$t.c does not compile"; fail=1; fi
            rm -f "$tb"
        done
    else echo "NOT RUN: needs a C compiler and libsdl2-dev (sdl2-config)"; fail=1; fi
fi

echo "== check-app $app: $([ "$fail" = 0 ] && echo passed || echo FAILED)"
exit $fail

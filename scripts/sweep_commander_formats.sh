#!/bin/bash
# sweep_commander_formats.sh: build Commander with GCC in Debian 12, run its self-test, then open a sample of the container's own files
# with an AddressSanitizer/UBSan build (`--info` and `--content`) to catch crashes and undefined behaviour in the format readers.
#
#   docker run --rm -v "$PWD":/src -w /src debian:bookworm scripts/sweep_commander_formats.sh [files-per-extension]
#
# Samples up to N files (default 5) of each extension under /usr, /etc, /var/cache and corpus/, so every format on the system is tried
# without opening thousands of the same kind (the sanitizer build takes about a second to start, twice per file). Prints as it goes;
# exits 1 if GCC warns, the self-test fails or any file trips the sanitizers.

set -u
PER="${1:-5}"
cd "$(dirname "$0")/.." || exit 1
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq gcc libsdl2-dev pkg-config fonts-dejavu-core >/dev/null 2>&1 || { echo "apt-get failed"; exit 1; }
fail=0
SRC=apps/commander-native/commander.c
FLAGS=(-std=c11 -Wall -Wextra -I theme)
read -ra SDL_CFLAGS <<<"$(sdl2-config --cflags)"
read -ra SDL_LIBS <<<"$(sdl2-config --libs)"

echo "== GCC warnings"
w="$(gcc "${FLAGS[@]}" -O2 "${SDL_CFLAGS[@]}" -o /tmp/commander "$SRC" "${SDL_LIBS[@]}" -lm 2>&1)"
if [ -n "$w" ]; then echo "$w" | head -40; fail=1; else echo "none"; fi

echo "== self-test"
SDL_VIDEODRIVER=dummy /tmp/commander --selftest 2>&1 | tail -5
SDL_VIDEODRIVER=dummy /tmp/commander --selftest >/dev/null 2>&1 || fail=1

echo "== sanitizer build"
gcc "${FLAGS[@]}" -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer "${SDL_CFLAGS[@]}" -o /tmp/commander-asan "$SRC" \
    "${SDL_LIBS[@]}" -lm || exit 1

echo "== sweep ($PER files per extension)"
find /usr /etc /var/cache corpus -xdev -type f -size -20M 2>/dev/null |
    awk -v per="$PER" '{ n = split($0, p, "/"); b = p[n]; e = (index(b, ".") ? tolower(substr(b, match(b, /\.[^.]*$/) + 1)) : "(none)");
                         if (c[e]++ < per) print }' > /tmp/list
total="$(wc -l < /tmp/list)"
n=0; bad=0
while read -r f; do
    n=$((n + 1))
    [ $((n % 100)) -eq 0 ] && echo "... $n of $total"
    : > /tmp/err
    /tmp/commander-asan --info "$f" >/dev/null 2>>/tmp/err
    /tmp/commander-asan --content "$f" >/dev/null 2>>/tmp/err
    if grep -q "ERROR: AddressSanitizer\|runtime error" /tmp/err; then
        bad=$((bad + 1))
        echo "!! $f"
        grep -m4 "ERROR\|runtime error\|#[0-3] " /tmp/err
    fi
done < /tmp/list
echo "swept $n files, $bad with problems"
[ "$bad" -eq 0 ] || fail=1
exit "$fail"

#!/bin/bash
# tools/stretch-proto/build-proto.sh [--bench] [--sanitize]
#
# Builds the Key-and-Tempo render prototype (ROADMAP section 40, step 1) in Debian 12 arm64 under Docker, with no arguments, and runs its
# self-test. It fetches Signalsmith Stretch and Signalsmith Linear (both MIT, pinned by commit below, checked after the clone) into
# build/stretchproto/ (gitignored). With --bench it also times 1 to 4 workers. A Docker core on a Mac is not a Cortex-A35: the timings
# here are for the shape (how it scales), not for the unit. With --sanitize the self-test also runs under AddressSanitizer and UBSan.
set -eu
STRETCH_COMMIT=88061fd4eda56a2353c5d65c74aa3f4ccf76f9da   # Signalsmith-Audio/signalsmith-stretch 1.3.2, 2026-10-22, MIT
LINEAR_COMMIT=7bb1c8f36794d6980080ce817c91f8da8fe56a66    # Signalsmith-Audio/linear, 2026-10-21, MIT
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out="$root/build/stretchproto"
mkdir -p "$out"
case "${1:-}" in
    -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    ""|--bench|--sanitize) ;;
    *) echo "build-proto: unknown option: $1 (try --help)" >&2; exit 2 ;;
esac
docker run --rm --platform linux/arm64 -e STRETCH_COMMIT="$STRETCH_COMMIT" -e LINEAR_COMMIT="$LINEAR_COMMIT" -e MODE="${1:-}" \
    -v "$root/build/stretchproto":/o -v "$here":/src:ro debian:12 bash -c '
set -eu
echo "options no-aaaa" >> /etc/resolv.conf
apt-get update -qq
apt-get install -y -qq g++ git ca-certificates >/dev/null
cd /o
fetch() {   # name url commit
    if [ ! -d "$1/.git" ] || [ "$(git -C "$1" rev-parse HEAD)" != "$3" ]; then
        rm -rf "$1"; git clone -q "$2" "$1"; git -C "$1" checkout -q "$3"
    fi
    [ "$(git -C "$1" rev-parse HEAD)" = "$3" ] || { echo "$1: wrong commit"; exit 1; }
}
fetch ss https://github.com/Signalsmith-Audio/signalsmith-stretch "$STRETCH_COMMIT"
fetch linear https://github.com/Signalsmith-Audio/linear "$LINEAR_COMMIT"
g++ -std=c++17 -O3 -mcpu=native -Wall -Wextra -Werror -I ss -I ss/include/signalsmith-stretch -I linear/include -pthread -o stretch-proto /src/proto.cpp
./stretch-proto selftest
if [ "$MODE" = "--bench" ]; then ./stretch-proto bench 60; fi
if [ "$MODE" = "--sanitize" ]; then
    g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -I ss -I ss/include/signalsmith-stretch -I linear/include -pthread -o stretch-proto-asan /src/proto.cpp
    ./stretch-proto-asan selftest
fi
'

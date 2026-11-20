#!/bin/bash
# tools/stretch-proto/build-proto.sh [--bench] [--sanitize] [--package]
#
# Builds the Key-and-Tempo render prototype (ROADMAP section 40) in Debian 12 arm64 under Docker, with no arguments, and runs its self-test, for
# two builds of the FFT: Signalsmith Linear's own, and PFFFT (BSD-like, with NEON code). It fetches Signalsmith Stretch, Signalsmith Linear
# and PFFFT (pinned by commit below, checked after the clone) into build/stretchproto/ (gitignored). Everything is built with -O3
# -mcpu=cortex-a35, so what is tested here is the program the unit runs.
#   --bench      also times 1 to 4 workers (a Docker core on a Mac is not a Cortex-A35: the shape of the scaling, not the unit's speed)
#   --sanitize   also runs the self-test under AddressSanitizer and UBSan
#   --package    also makes build/stretchproto/r3i6nos-stretch-proto.deb (a tool package: stretch-proto, stretch-proto-pffft and
#                stretch-proto-report, for step 2 on the unit: scripts/push-packages.sh UNIT build/stretchproto/r3i6nos-stretch-proto.deb),
#                then installs it into an empty folder and runs the report there (a short song) to check the package works
set -eu
# shellcheck disable=SC1091  # the pins
. "$(dirname "$0")/pins.conf"
VERSION=0.1
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out="$root/build/stretchproto"
mkdir -p "$out"
MODE=""
for a in "$@"; do
    case "$a" in
        -h|--help) sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        --bench|--sanitize|--package) MODE="$MODE $a" ;;
        *) echo "build-proto: unknown option: $a (try --help)" >&2; exit 2 ;;
    esac
done
docker run --rm --platform linux/arm64 -e STRETCH_COMMIT="$STRETCH_COMMIT" -e LINEAR_COMMIT="$LINEAR_COMMIT" -e PFFFT_COMMIT="$PFFFT_COMMIT" \
    -e MODE="$MODE" -e VERSION="$VERSION" -v "$root/build/stretchproto":/o -v "$here":/src:ro -v "$root/LICENSE":/LICENSE:ro debian:12 bash -c '
set -eu
echo "options no-aaaa" >> /etc/resolv.conf
apt-get update -qq
apt-get install -y -qq g++ gcc git ca-certificates >/dev/null
cd /o
fetch() {   # name url commit
    if [ ! -d "$1/.git" ] || [ "$(git -C "$1" rev-parse HEAD)" != "$3" ]; then
        rm -rf "$1"; git clone -q "$2" "$1"; git -C "$1" checkout -q "$3"
    fi
    [ "$(git -C "$1" rev-parse HEAD)" = "$3" ] || { echo "$1: wrong commit"; exit 1; }
}
fetch ss https://github.com/Signalsmith-Audio/signalsmith-stretch "$STRETCH_COMMIT"
fetch linear https://github.com/Signalsmith-Audio/linear "$LINEAR_COMMIT"
fetch pffft https://github.com/marton78/pffft "$PFFFT_COMMIT"
FLAGS="-std=c++17 -O3 -mcpu=cortex-a35 -Wall -Wextra -Werror -I ss -I ss/include/signalsmith-stretch -I linear/include -pthread"
# PFFFT is C: built once with the same CPU flags, then linked into the second program
PF="-I pffft/include -I pffft/include/pffft -I pffft/src"
for f in pffft pffft_common fftpack; do gcc -O3 -mcpu=cortex-a35 -DPFFFT_ENABLE_NEON -w $PF -c pffft/src/$f.c -o pf_$f.o; done
STATIC="-static-libstdc++ -static-libgcc"
g++ $FLAGS $STATIC -o stretch-proto /src/proto.cpp
g++ $FLAGS $STATIC -DSIGNALSMITH_USE_PFFFT $PF -o stretch-proto-pffft /src/proto.cpp pf_pffft.o pf_pffft_common.o pf_fftpack.o
g++ $FLAGS $STATIC -o musicue-render /src/musicue_render.cpp
echo "--- default FFT"; ./stretch-proto selftest
echo "--- PFFFT"; ./stretch-proto-pffft selftest
echo "--- musicue-render"; bash /src/test-render.sh /o
case "$MODE" in *--bench*)
    ./stretch-proto bench 60; ./stretch-proto-pffft bench 60; ./stretch-proto-pffft bench 60 --cheaper ;;
esac
case "$MODE" in *--sanitize*)
    g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -I ss -I ss/include/signalsmith-stretch -I linear/include -pthread -o stretch-proto-asan /src/proto.cpp
    ./stretch-proto-asan selftest ;;
esac
case "$MODE" in *--package*)
    apt-get install -y -qq binutils >/dev/null
    S=/tmp/stage; rm -rf $S; mkdir -p $S/usr/bin $S/usr/share/r3i6nos $S/usr/share/doc/r3i6nos-stretch-proto $S/DEBIAN
    install -m 755 stretch-proto stretch-proto-pffft $S/usr/bin/
    strip $S/usr/bin/stretch-proto $S/usr/bin/stretch-proto-pffft
    sed "s/@VERSION@/$VERSION/" /src/stretch-proto-report > $S/usr/bin/stretch-proto-report; chmod 755 $S/usr/bin/stretch-proto-report
    echo r3i6nos-stretch-proto > $S/usr/share/r3i6nos/tool
    D=$S/usr/share/doc/r3i6nos-stretch-proto
    cp ss/LICENSE.txt $D/LICENSE.signalsmith-stretch; cp linear/LICENSE.txt $D/LICENSE.signalsmith-linear
    sed -n "1,/\\*\\//p" pffft/include/pffft/pffft.h > $D/LICENSE.pffft
    { echo "stretch-proto $VERSION: the Key-and-Tempo render prototype of r3i6nos (ROADMAP section 40). MIT, this repository."
      echo "Built with Signalsmith Stretch $STRETCH_COMMIT (MIT, https://github.com/Signalsmith-Audio/signalsmith-stretch),"
      echo "Signalsmith Linear $LINEAR_COMMIT (MIT, https://github.com/Signalsmith-Audio/linear) and, in stretch-proto-pffft,"
      echo "PFFFT $PFFFT_COMMIT (BSD-like, https://github.com/marton78/pffft). The three licence texts are in this folder."; } > $D/NOTICE
    cp /src/README.md $D/README.md
    cp /LICENSE $D/copyright
    SIZE=$(du -sk $S/usr | cut -f1)
    printf "Package: r3i6nos-stretch-proto\nVersion: %s\nArchitecture: arm64\nMaintainer: r3i6nos <noreply@users.noreply.github.com>\nInstalled-Size: %s\nDepends: libc6\nSection: sound\nPriority: optional\nDescription: Key-and-Tempo render prototype (a measuring tool)\n Times a multi-core pitch and tempo render on the unit; stretch-proto-report writes the device report.\n" "$VERSION" "$SIZE" > $S/DEBIAN/control
    find $S -exec touch -h -d "2026-01-01 00:00:00 UTC" {} +
    rm -f /o/r3i6nos-stretch-proto.deb
    dpkg-deb --root-owner-group -Zxz --build $S /o/r3i6nos-stretch-proto.deb
    # the package in an empty folder, running its own report on a short song, with fake /sys and /proc, as the unit would
    T=/tmp/inst; rm -rf $T; mkdir -p $T/data; dpkg -x /o/r3i6nos-stretch-proto.deb $T
    # a fake battery (discharging, the current negative as the unit reports it, 1.5 A at 4 V is 6 W) so the power section runs too
    B=$T/sys/class/power_supply/battery; mkdir -p $B
    echo Discharging > $B/status; echo 90 > $B/capacity; echo -300000 > $B/current_now; echo 4000000 > $B/voltage_now
    R3I6NOS_SYS=$T/sys R3I6NOS_APP_DATA=$T/data R3I6NOS_STRETCH_SECS=8 TMPDIR=$T $T/usr/bin/stretch-proto-report > /tmp/report.out || { cat /tmp/report.out; echo "REPORT FAILED"; exit 1; }
    tail -n 22 $T/data/r36s-report.txt
    grep -q "draw 4 workers, default preset: 1200 mW" $T/data/r36s-report.txt || { echo "the power section did not read 1200 mW (0.3 A x 4 V, sign dropped)"; exit 1; }
    [ "$(tail -n 1 $T/data/r36s-report.txt)" = end ] || { echo "report does not end with end"; exit 1; }
    ;;
esac
'
if [ -f "$out/r3i6nos-stretch-proto.deb" ]; then ls -l "$out/r3i6nos-stretch-proto.deb"; fi
exit 0

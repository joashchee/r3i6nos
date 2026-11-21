#!/usr/bin/env bash
# r3i6nos-runtimes as root in Debian 12 with the real squashfs-tools: a squashfs made with a set-uid file, a device node and a pipe is extracted
# by the real helper (privileges dropped to nobody), made safe, and used through the launcher's sudo shim. Needs Docker; the Mac cannot run it.
set -euo pipefail
cd "$(dirname "$0")/.."
command -v docker >/dev/null || { echo "NOT RUN: needs Docker"; exit 0; }
docker info >/dev/null 2>&1 || { echo "NOT RUN: Docker is not running"; exit 0; }
docker run --rm --platform linux/arm64 -v "$PWD:/src:ro" debian:12-slim bash -euo pipefail -c '
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq >/dev/null && apt-get install -y -qq --no-install-recommends squashfs-tools python3-minimal libpython3-stdlib >/dev/null
  fail() { echo "FAIL: $*" >&2; exit 1; }
  W=$(mktemp -d); mkdir -p $W/roms/ports/PortMaster/libs $W/t/bin $W/t/lib $W/home $W/cache
  echo "#!/bin/sh" > $W/t/bin/run; chmod 4755 $W/t/bin/run
  echo data > $W/t/lib/d.so; chmod 600 $W/t/lib/d.so
  ln -s d.so $W/t/lib/alias.so; ln -s /etc/shadow $W/t/lib/abs
  mkfifo $W/t/lib/pipe
  cp -a $W/t $W/bad; mknod $W/bad/lib/null c 1 3
  mksquashfs $W/bad $W/roms/ports/PortMaster/libs/bad_1.squashfs -comp zstd -quiet -no-progress >/dev/null
  mksquashfs $W/t $W/roms/ports/PortMaster/libs/test_1.squashfs -comp zstd -quiet -no-progress >/dev/null
  cp /src/overlay/usr/local/bin/r3i6nos-runtimes $W/h; chmod +x $W/h
  export R3I6NOS_RUNTIMES_DIR=$W/req R3I6NOS_RUNTIMES_OUT=$W/out R3I6NOS_RUNTIMES_CACHE=$W/cache R3I6NOS_RUNTIMES_ROOTS=$W/roms/ports
  chmod 755 $W; mkdir -p $W/req
  # the shim and the helper, as the path unit would run it
  # a runtime with a device node is refused whole (the unprivileged extractor cannot make one) and leaves nothing behind
  ( while [ ! -e $W/req/request ]; do sleep 0.1; done; $W/h apply ) &
  if sh /src/apps/portmaster/shim/sudo mount $W/roms/ports/PortMaster/libs/bad_1.squashfs $W/home/bad 2>$W/bad.err; then fail "a device node was accepted"; fi
  wait
  grep -q "character device" $W/bad.err || fail "the refusal does not say why: $(cat $W/bad.err)"
  [ ! -e $W/home/bad ] && [ -z "$(ls -A $W/cache)" ] || fail "a refused runtime left something"
  ( while [ ! -e $W/req/request ]; do sleep 0.1; done; $W/h apply ) &
  sh /src/apps/portmaster/shim/sudo mount $W/roms/ports/PortMaster/libs/test_1.squashfs $W/home/test || fail "shim mount"
  wait
  D=$(readlink $W/home/test); [ -d "$D" ] || fail "no link"
  [ "$(stat -c %a $D/bin/run)" = 755 ] || fail "run mode $(stat -c %a $D/bin/run)"
  [ "$(stat -c %U $D/bin/run)" = root ] || fail "owner"
  [ "$(stat -c %a $D/lib/d.so)" = 644 ] || fail "d.so mode"
  [ ! -e $D/lib/pipe ] || fail "a pipe was kept"
  [ -L $D/lib/alias.so ] || fail "link lost"
  find $D -perm /7000 | grep -q . && fail "set-uid or sticky left"
  grep -q "^files=" $D/.r3i6nos-complete || fail "no marker"
  $W/h status | grep -q "extracted: 1" || fail "status"
  sh /src/apps/portmaster/shim/sudo umount $W/home/test || fail "umount"
  [ ! -e $W/home/test ] || fail "link left"
  $W/h prune | grep -q "removed 1" || fail "prune"
  echo "runtimes docker test: ok"
'

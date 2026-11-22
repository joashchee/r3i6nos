#!/bin/bash
# scripts/pocketide-spike.sh: the PocketIDE spike (ROADMAP section 42, docs/POCKETIDE.md). Clones AGS at a pinned tag, builds its standalone script compiler
# (agscc) and tools in Debian 12 under Docker, and compiles a tiny script (and a wrong one, to see the messages). Everything goes to build/ags-spike/
# (gitignored). Needs Docker and the network; prints NOT RUN without Docker. Artistic-2.0 source, nothing of it is kept in the repo.
set -euo pipefail
cd "$(dirname "$0")/.."
TAG=v3.6.3.2
COMMIT=56fad9d501aafaf365a486d9406202dff3e4778f
command -v docker >/dev/null || { echo "NOT RUN: needs Docker"; exit 0; }
docker info >/dev/null 2>&1 || { echo "NOT RUN: Docker is not running"; exit 0; }
W="$PWD/build/ags-spike"
mkdir -p "$W/t"
printf '// a tiny room script\nfunction room_Load()\n{\n  Display("Hello from PocketIDE");\n}\n' > "$W/t/hello.asc"
printf 'function f() { Dispay(1); }\n' > "$W/t/bad.asc"
docker run --rm -v "$W":/w debian:12 bash -euo pipefail -c '
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq >/dev/null
  apt-get install -y -qq --no-install-recommends git cmake g++ make ca-certificates >/dev/null
  cd /w
  if [ ! -d ags ]; then git clone -q --depth 1 --branch '"$TAG"' https://github.com/adventuregamestudio/ags.git; fi
  [ "$(git -C ags rev-parse HEAD)" = '"$COMMIT"' ] || { echo "FAIL: ags is not '"$TAG"'"; exit 1; }
  mkdir -p bld; cd bld
  cmake ../ags -DAGS_BUILD_COMPILER=ON -DAGS_BUILD_TOOLS=ON -DAGS_BUILD_ENGINE=OFF -DCMAKE_BUILD_TYPE=Release >cmake.log 2>&1
  make -j4 agscc agspak crmpak >make.log 2>&1
  cd /w/t
  H=../ags/Editor/AGS.Editor/Resources/agsdefns.sh
  # agscc reads its input as the last argument and writes INPUT.o beside it (the -o option with a separate value mis-parses in this version)
  ../bld/Compiler/agscc -H $H hello.asc >/dev/null
  [ "$(head -c 4 hello.o)" = SCOM ] || { echo "FAIL: hello.o is not a compiled script"; exit 1; }
  if ../bld/Compiler/agscc -H $H bad.asc >bad.out 2>&1; then echo "FAIL: a wrong script compiled"; exit 1; fi
  grep -q "Undefined token" bad.out || { echo "FAIL: no message for the wrong script"; exit 1; }
  echo "pocketide spike: ok (agscc, agspak and crmpak built; hello.asc compiled to a SCOM object; bad.asc refused with its line)"
'

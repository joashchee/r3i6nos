#!/bin/bash
# scripts/build-ports.sh against fake port repos (no Docker, no real builds): what it builds, skips and reports.
set -eu
cd "$(dirname "$0")/.."
T="$(mktemp -d "${TMPDIR:-/tmp}/r3-buildports.XXXXXX")"
trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
export R3I6NOS_PORTS_ROOT="$T" R3I6NOS_BUILD_PORTS_NO_DOCKER=1
run() { scripts/build-ports.sh "$@" 2>&1; }
has() { grep -qF -- "$2" <<<"$1" || fail "$3: wanted \"$2\" in: $1"; }
hasnt() { if grep -qF -- "$2" <<<"$1"; then fail "$3: did not want \"$2\" in: $1"; fi; }
runs() {
    if [ -f "$T/runs.$1" ]; then wc -l < "$T/runs.$1" | tr -d ' '; else echo 0; fi
}

# a fake .deb port: its build script logs a run and makes (or does not make) the package
fake_deb() {  # <dir> <package> <mode: ok|fail|nothing>
    mkdir -p "$T/$1/r36s"
    printf 'x\n' > "$T/$1/source.c"
    cat > "$T/$1/r36s/build-deb.sh" <<SH
#!/bin/bash
echo run >> "$T/runs.$1"
case "$3" in
    ok) mkdir -p r36s/build/pkg; echo package > "r36s/build/pkg/$2" ;;
    fail) echo "the compiler said no" >&2; exit 1 ;;
    nothing) : ;;
esac
SH
    chmod +x "$T/$1/r36s/build-deb.sh"
}
fake_deb klamansi KlamANSI.deb ok
fake_deb neumetik Neumetik.deb fail
fake_deb composer compose.deb ok
fake_deb painter sprites.deb nothing
# a fake LÖVE port that only makes the 11.5 file at first
mkdir -p "$T/nocogama/scripts"
printf 'x\n' > "$T/nocogama/main.lua"
cat > "$T/nocogama/scripts/build.sh" <<SH
#!/bin/bash
echo run >> "$T/runs.nocogama"
mkdir -p "build/Love 11.5" "build/Love 12.0"
echo love > "build/Love 11.5/NoCoGaMa-11.5.love"
[ -f "$T/both" ] && echo love > "build/Love 12.0/NoCoGaMa-12.0.love"
exit 0
SH
chmod +x "$T/nocogama/scripts/build.sh"

# --list: names the state, builds nothing
out="$(run --list klamansi)"
has "$out" "KlamANSI" "list names the port"
has "$out" "missing" "a port with no build is missing"
[ "$(runs klamansi)" = 0 ] || fail "--list built something"

# a first build, by id and by name (any case)
out="$(run klamansi)"
has "$out" "built    KlamANSI" "built"
[ "$(runs klamansi)" = 1 ] || fail "the build did not run once"
[ -s "$T/klamansi/r36s/build/pkg/KlamANSI.deb" ] || fail "no package"
[ -f build/ports-logs/klamansi.log ] || fail "no log"

# fresh: nothing to do
out="$(run KlamANSI)"
has "$out" "Nothing to build" "fresh is left alone"
[ "$(runs klamansi)" = 1 ] || fail "a fresh port was built again"

# a source file changed after the build: out of date, rebuilt
sleep 1
touch "$T/klamansi/source.c"
out="$(run --list klamansi)"
has "$out" "stale: " "a newer source file is stale"
out="$(run klamansi)"
has "$out" "built    KlamANSI" "stale is rebuilt"
[ "$(runs klamansi)" = 2 ] || fail "the stale port was not rebuilt"

# docs, tests and CI files are not part of a build
sleep 1
mkdir -p "$T/klamansi/docs" "$T/klamansi/tests"
echo x > "$T/klamansi/CHANGELOG.md"; echo x > "$T/klamansi/docs/a.txt"; echo x > "$T/klamansi/tests/t.sh"
out="$(run --list klamansi)"
has "$out" "fresh" "a doc or test change does not make a port stale"

# --all builds what is fresh too
out="$(run --all klamansi)"
has "$out" "built    KlamANSI" "--all rebuilds"
[ "$(runs klamansi)" = 3 ] || fail "--all did not rebuild"

# a failure does not stop the others, is reported with its log, and sets the status
set +e
out="$(run neumetik composer)"; rc=$?
set -e
[ "$rc" = 1 ] || fail "a failed build should exit 1, not $rc"
has "$out" "FAILED   Neumetik" "the failure is in the summary"
has "$out" "the compiler said no" "the log's tail is shown"
has "$out" "built    Composer" "the next port was still built"

# a build that says it worked and made nothing is a failure
set +e
out="$(run painter)"; rc=$?
set -e
[ "$rc" = 1 ] || fail "a build with no package should exit 1"
has "$out" "FAILED   Painter" "no package is a failure"

# a LÖVE port needs both runtimes' files
set +e
out="$(run nocogama)"; rc=$?
set -e
[ "$rc" = 1 ] || fail "a LÖVE port with one runtime's file should fail"
has "$out" "FAILED   NoCoGaMa" "the 12.0 file is missing"
touch "$T/both"
out="$(run nocogama)"
has "$out" "built    NoCoGaMa" "both files made"
out="$(run --list nocogama)"
has "$out" "fresh" "both files: fresh"

# a port whose repo is not there is skipped, and said
out="$(run crunchy)"
has "$out" "no repo at" "a missing repo is said"
hasnt "$out" "FAILED" "a missing repo is not a failure"

# an unknown name is an error that lists the ids
set +e
out="$(run nosuchport)"; rc=$?
set -e
[ "$rc" = 2 ] || fail "an unknown name should exit 2, not $rc"
has "$out" "no port called 'nosuchport'" "unknown name"
has "$out" "io-checker-native" "the ids are listed"

echo "build-ports: ok"

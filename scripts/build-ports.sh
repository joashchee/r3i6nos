#!/bin/bash
# scripts/build-ports.sh [--list] [--all] [-v] [NAME...]
#
# Refreshes the local builds of every R36S port, one command, on this computer (CLAUDE.md rule 10 gives each port a build script that takes no
# arguments; this runs them all, so the next flash-macos.sh copies fresh files):
#
#   the ports beside this repo (scripts/ports.conf: KlamANSI, Neumetik, Composer, Painter, Animator, Crunchy, Chordica, TinyNTT, NoCoGaMa),
#   each with its own `r36s/build-deb.sh` (or `scripts/build.sh`) run in its repo; and this repo's own apps:
#   Commander and the IO Checker (native, `scripts/build-native-local.sh`; LÖVE, `scripts/build_love_app.sh`).
#
#   scripts/build-ports.sh                  build what is missing or out of date, skip what is fresh
#   scripts/build-ports.sh --all            build everything, fresh or not
#   scripts/build-ports.sh --list           say what is fresh, out of date or missing; build nothing
#   scripts/build-ports.sh tinyntt commander   only these (an id from --list; a name works too)
#   scripts/build-ports.sh -v               show each build's output as it runs (default: a log in build/ports-logs/<id>.log, its tail on a failure)
#
# "Out of date": a file of the port (what git tracks or would track in its repo) is newer than its build, or, for a .deb, a file in this repo's
# theme/ or tools/qjs-node/ is (the ports compile against them). A build in another place, such as CI's, is not looked at.
# A failed build does not stop the others; the summary lists them and the exit status is 1. A port whose repo is not beside this one is
# skipped, and said. Needs Docker for everything but the LÖVE IO Checker (Colima: `colima start`), and luajit for that one.
# R3I6NOS_PORTS_ROOT says where the port repos are (default: the folder this repo is in). Test: tests/test_build_ports.sh.
set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"
PORTS_ROOT="${R3I6NOS_PORTS_ROOT:-$(cd "$REPO/.." && pwd)}"
LOGS="$REPO/build/ports-logs"
# shellcheck source=/dev/null  # scripts/ports.conf: data, read here
. "$REPO/scripts/ports.conf"

LIST=0; ALL=0; VERBOSE=0
WANT=()
for arg in "$@"; do
    case "$arg" in
        --list) LIST=1 ;;
        --all) ALL=1 ;;
        -v|--verbose) VERBOSE=1 ;;
        -h|--help) sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "build-ports: unknown option: $arg (try --help)" >&2; exit 2 ;;
        *) WANT+=("$(printf '%s' "$arg" | tr '[:upper:]' '[:lower:]')") ;;
    esac
done

# One line an item: id|label|work dir|command|needs|artifacts|watch
#   artifacts: ";"-separated paths, globs allowed; every one must exist (a LÖVE port has one a runtime)
#   watch:     ";"-separated: git:<repo> (what git tracks or would track there) or dir:<folder> (everything in it)
ITEMS=()
add_item() { ITEMS+=("$1|$2|$3|$4|$5|$6|$7"); }

add_item commander "Commander (native)" "$REPO" "scripts/build-native-local.sh commander-native" docker \
    "$REPO/build/native/Commander.deb" "dir:$REPO/apps/commander-native;dir:$REPO/theme;dir:$REPO/scripts/build_native_app.sh"
add_item io-checker-native "IO Checker (native)" "$REPO" "scripts/build-native-local.sh io-checker-native" docker \
    "$REPO/build/native/IO_Checker_Native.deb" "dir:$REPO/apps/io-checker-native;dir:$REPO/theme;dir:$REPO/scripts/build_native_app.sh"
add_item io-checker-love "IO Checker (LÖVE 11.5 and 12.0)" "$REPO" "scripts/build_love_app.sh io-checker-love" luajit \
    "$REPO/build/love-apps/Love 11.5/IO_Checker.love;$REPO/build/love-apps/Love 12.0/IO_Checker.love" "dir:$REPO/apps/io-checker-love;dir:$REPO/theme;dir:$REPO/scripts/build_love_app.sh"

for entry in "${PORTS[@]}"; do
    IFS='|' read -r pname pdir pkind prel phow <<<"$entry"
    repo="$PORTS_ROOT/$pdir"
    if [ "$pkind" = deb ]; then
        add_item "$pdir" "$pname" "$repo" "$phow" docker "$repo/$prel" "git:$repo;dir:$REPO/theme;dir:$REPO/tools/qjs-node"
    else
        add_item "$pdir" "$pname" "$repo" "$phow" docker "$repo/${prel//\{v\}/11.5};$repo/${prel//\{v\}/12.0}" "git:$repo"
    fi
done

# Prints "fresh", "missing: <artifact>" or "stale: <a file newer than the build>". Python 3 standard library only.
status_of() {  # <artifacts> <watch>
    python3 -I - "$1" "$2" <<'PY'
import glob, os, subprocess, sys
arts = [a for a in sys.argv[1].split(';') if a]
oldest = None
for spec in arts:
    found = glob.glob(spec)
    if not found:
        print("missing: " + spec)
        sys.exit(0)
    newest = max(os.path.getmtime(f) for f in found)
    oldest = newest if oldest is None else min(oldest, newest)
SKIP_DIRS = ('docs/', 'tests/', '.github/')          # not part of a build: a change there does not make it out of date
def relevant(rel):
    return not rel.endswith('.md') and not rel.startswith(SKIP_DIRS) and rel != '.gitignore'
def files(w):
    kind, _, path = w.partition(':')
    if kind == 'git':
        res = subprocess.run(['git', '-C', path, 'ls-files', '-co', '--exclude-standard', '-z'], capture_output=True)
        if res.returncode == 0:
            for rel in res.stdout.split(b'\0'):
                r = rel.decode(errors='replace')
                if rel and relevant(r):
                    yield os.path.join(path, r)
            return
        # not a git checkout (or git failed): look at every file instead
    if os.path.isfile(path):
        yield path
    else:
        for root, dirs, names in os.walk(path):
            dirs[:] = [d for d in dirs if d not in ('.git', 'build', 'node_modules', '__pycache__')]
            for n in names:
                full = os.path.join(root, n)
                if n != '.DS_Store' and relevant(os.path.relpath(full, path)):
                    yield full
for w in [w for w in sys.argv[2].split(';') if w]:
    for f in files(w):
        try:
            if os.path.getmtime(f) > oldest:
                print("stale: " + os.path.relpath(f, os.getcwd()) + " changed after the build")
                sys.exit(0)
        except OSError:
            pass
print("fresh")
PY
}

selected() {  # <id> <label> [<one name>]: named by the arguments (or by that one name); with no arguments, everything is
    local w label
    label="$(printf '%s' "$2" | tr '[:upper:]' '[:lower:]')"
    if [ -n "${3:-}" ]; then
        if [ "$3" = "$1" ] || [ "$3" = "$label" ]; then return 0; fi
        return 1
    fi
    [ "${#WANT[@]}" -gt 0 ] || return 0
    for w in "${WANT[@]}"; do
        if [ "$w" = "$1" ] || [ "$w" = "$label" ]; then return 0; fi
    done
    return 1
}

# every argument must name something
if [ "${#WANT[@]}" -gt 0 ]; then
    for w in "${WANT[@]}"; do
        ok=0
        for item in "${ITEMS[@]}"; do
            IFS='|' read -r id label _ <<<"$item"
            if selected "$id" "$label" "$w"; then ok=1; fi
        done
        if [ "$ok" = 0 ]; then
            echo "build-ports: no port called '$w'. Ids:" >&2
            for item in "${ITEMS[@]}"; do IFS='|' read -r id label _ <<<"$item"; echo "  $id   ($label)" >&2; done
            exit 2
        fi
    done
fi

# What is to be built (and the state of each, for --list)
TODO=()           # indexes into ITEMS
SKIPPED=()        # lines
FRESH=()
echo "R36S ports in $PORTS_ROOT, this repo's apps in $REPO"
i=-1
for item in "${ITEMS[@]}"; do
    i=$((i + 1))
    IFS='|' read -r id label workdir how needs artifacts watch <<<"$item"
    selected "$id" "$label" || continue
    if [ ! -d "$workdir" ]; then
        SKIPPED+=("$label: no repo at $workdir")
        printf '  %-34s %s\n' "$label" "no repo at $workdir (skipped)"
        continue
    fi
    st="$(status_of "$artifacts" "$watch")"
    if [ "$ALL" = 1 ] && [ "$st" = fresh ]; then st="fresh (rebuilt: --all)"; fi
    printf '  %-34s %s\n' "$label" "$st"
    case "$st" in
        fresh) FRESH+=("$label") ;;
        *) TODO+=("$i") ;;
    esac
done
[ "$LIST" = 0 ] || exit 0

if [ "${#TODO[@]}" = 0 ]; then
    echo "Nothing to build: everything is fresh${SKIPPED[0]+ (but see the skipped above)}."
    exit 0
fi

# Docker (Colima) when any build needs it; luajit for the LÖVE one
need_docker=0
for idx in "${TODO[@]}"; do
    IFS='|' read -r _ _ _ _ needs _ <<<"${ITEMS[$idx]}"
    if [ "$needs" = docker ]; then need_docker=1; fi
done
if [ "$need_docker" = 1 ] && [ -z "${R3I6NOS_BUILD_PORTS_NO_DOCKER:-}" ]; then      # the variable is for tests/test_build_ports.sh, whose builds are fakes
    command -v docker >/dev/null || { echo "build-ports: Docker is not installed (brew install colima docker, then colima start)" >&2; exit 2; }
    if ! docker info >/dev/null 2>&1; then
        if [ -z "${DOCKER_HOST:-}" ] && [ -S "$HOME/.colima/default/docker.sock" ]; then
            DOCKER_HOST="unix://$HOME/.colima/default/docker.sock"
            export DOCKER_HOST
        fi
        docker info >/dev/null 2>&1 || { echo "build-ports: Docker is not running (colima start)" >&2; exit 2; }
    fi
fi

mkdir -p "$LOGS"
OK=(); FAILED=(); NOTOOL=()
n=0
total="${#TODO[@]}"
for idx in "${TODO[@]}"; do
    n=$((n + 1))
    IFS='|' read -r id label workdir how needs _ <<<"${ITEMS[$idx]}"
    if [ "$needs" = luajit ] && ! command -v luajit >/dev/null; then
        NOTOOL+=("$label: needs luajit (brew install luajit)")
        echo "[$n/$total] $label: skipped, needs luajit"
        continue
    fi
    log="$LOGS/$id.log"
    echo "[$n/$total] $label: $how  (in $workdir)"
    start="$(date +%s)"
    if [ "$VERBOSE" = 1 ]; then
        # tee would hide the build's exit status: take it from PIPESTATUS
        if { (cd "$workdir" && eval "$how") 2>&1 | tee "$log"; [ "${PIPESTATUS[0]}" = 0 ]; }; then rc=0; else rc=1; fi
    else
        if (cd "$workdir" && eval "$how") >"$log" 2>&1; then rc=0; else rc=1; fi
    fi
    took=$(( $(date +%s) - start ))
    if [ "$rc" = 0 ]; then
        IFS='|' read -r _ _ _ _ _ artifacts _ <<<"${ITEMS[$idx]}"
        st="$(status_of "$artifacts" "")"
        case "$st" in
            missing*) rc=1; echo "    the build finished but $st" ;;
        esac
    fi
    if [ "$rc" = 0 ]; then
        OK+=("$label (${took}s)")
        echo "    done in ${took}s"
    else
        FAILED+=("$label (log: $log)")
        echo "    FAILED after ${took}s; the end of its log:"
        tail -n 15 "$log" | sed 's/^/      /'
    fi
done

echo
echo "== build-ports summary"
for line in ${OK[@]+"${OK[@]}"}; do echo "  built    $line"; done
for line in ${FRESH[@]+"${FRESH[@]}"}; do echo "  fresh    $line"; done
for line in ${SKIPPED[@]+"${SKIPPED[@]}"}; do echo "  skipped  $line"; done
for line in ${NOTOOL[@]+"${NOTOOL[@]}"}; do echo "  skipped  $line"; done
for line in ${FAILED[@]+"${FAILED[@]}"}; do echo "  FAILED   $line"; done
[ "${#FAILED[@]}" = 0 ]

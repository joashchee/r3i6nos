#!/bin/bash
# scripts/push-packages.sh [options] UNIT [FILE.deb ...]
#
# Puts native .deb packages on a running R36S over SSH, so no card has to come out (docs/PACKAGES-OTA.md, method A).
#
#   scripts/push-packages.sh 192.168.1.50                 the newest local build of every port (scripts/ports.conf) and of this repo's
#                                                         Commander and IO Checker (build/native), after one question
#   scripts/push-packages.sh 192.168.1.50 build/native/Commander.deb      only these files
#   scripts/push-packages.sh 192.168.1.50 "build/love-apps/Love 12.0/IO_Checker.love"   a LÖVE app: the runtime is the name of the folder it
#                                                         is in ("Love 11.5" or "Love 12.0"), or --runtime 11.5|12.0; it goes to the card's
#                                                         "Love <runtime>" folder, where the menu lists LÖVE games
#   scripts/push-packages.sh --yes ...                    no question here (r3i6nos-pkg on the unit is told --yes too)
#   scripts/push-packages.sh --runtime 11.5|12.0 ...      the LÖVE version of the .love files named (when their folder does not say)
#   scripts/push-packages.sh --card system|data ...       the card to put them on (default: the data card when one is in, else the system card)
#   scripts/push-packages.sh -n ...                       say what would be sent; connect to nothing
#   scripts/push-packages.sh UNIT --list                  what is installed (version, card, size)
#   scripts/push-packages.sh UNIT --remove NAME           take a package away (its data folder stays)
#
# UNIT is the address Settings > Network shows, or user@address (the user is `loveos`); R3I6NOS_UNIT is used when it is left out. SSH must be
# started on the unit first (Settings > SSH, which also sets the password this asks for). Nothing needs root on the unit: the login user owns
# the apps folder. The files go to ~/inbox on the unit, `r3i6nos-pkg install --inbox` checks each one (a Debian archive, arm64, a safe name,
# no nttplay, room for it, not too big for the root filesystem) and puts it on a card, applies it at once, and the menu lists it within a second.
# A .love file goes to ~/inbox/love-<runtime>/ and `r3i6nos-pkg install-love` checks it (a zip with main.lua at its top, a safe name, room) and puts it in
# the card's "Love <runtime>" folder; with no files named, the newest build of every LÖVE port and of this repo's IO Checker (build/love-apps) is sent too.
# Whatever it refuses stays in ~/inbox and is said. Exit status: 0 all installed, 1 something refused or failed, 2 a usage error.
#
# R3I6NOS_PORTS_ROOT says where the port repos are (default: the folder this repo is in). R3I6NOS_SSH, R3I6NOS_SCP, R3I6NOS_NATIVE_BUILD and
# R3I6NOS_LOVE_BUILD (build/love-apps) are for the tests.
# Test: tests/test_push_packages.sh.
set -u

REPO="$(cd "$(dirname "$0")/.." && pwd)"
PORTS_ROOT="${R3I6NOS_PORTS_ROOT:-$(cd "$REPO/.." && pwd)}"
SSH="${R3I6NOS_SSH:-ssh}"
SCP="${R3I6NOS_SCP:-scp}"
# shellcheck source=/dev/null  # scripts/ports.conf: data, read here
. "$REPO/scripts/ports.conf"

LOVE_ARGS=()
YES=0; DRY=0; CARD=""; MODE=push; REMOVE=""; UNIT="${R3I6NOS_UNIT:-}"; UNIT_GIVEN=0; FILES=(); LOVES=(); LOVE_RT=(); RUNTIME=""
LOVE_BUILD="${R3I6NOS_LOVE_BUILD:-$REPO/build/love-apps}"   # this repo's LÖVE apps: <build>/Love 11.5/*.love and Love 12.0
NATIVE_BUILD="${R3I6NOS_NATIVE_BUILD:-$REPO/build/native}"   # this repo's own Commander and IO Checker builds
usage() { sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; }
die() { echo "push-packages: $*" >&2; exit 2; }
while [ $# -gt 0 ]; do
    case "$1" in
        --yes|-y) YES=1 ;;
        -n|--dry-run) DRY=1 ;;
        --card) [ $# -ge 2 ] || die "--card takes data or system"; CARD="$2"; shift
                case "$CARD" in data|system) ;; *) die "--card takes data or system" ;; esac ;;
        --runtime) [ $# -ge 2 ] || die "--runtime takes 11.5 or 12.0"; RUNTIME="$2"; shift
                   case "$RUNTIME" in 11.5|12.0) ;; *) die "--runtime takes 11.5 or 12.0" ;; esac ;;
        --list) MODE=list ;;
        --remove) [ $# -ge 2 ] || die "--remove takes a package name"; MODE=remove; REMOVE="$2"; shift ;;
        -h|--help) usage; exit 0 ;;
        -*) die "unknown option: $1 (try --help)" ;;
        *.deb) FILES+=("$1") ;;
        *.love) LOVE_ARGS+=("$1") ;;
        *) if [ "$UNIT_GIVEN" = 0 ]; then UNIT="$1"; UNIT_GIVEN=1; else die "what is '$1'? the unit is already $UNIT (files end in .deb)"; fi ;;
    esac
    shift
done
for f in "${LOVE_ARGS[@]+"${LOVE_ARGS[@]}"}"; do
    rt="$RUNTIME"
    if [ -z "$rt" ]; then
        case "$(basename "$(dirname "$f")")" in "Love 11.5") rt=11.5 ;; "Love 12.0") rt=12.0 ;; esac
    fi
    [ -n "$rt" ] || die "which LÖVE version is $f for? put it in a folder called \"Love 11.5\" or \"Love 12.0\", or pass --runtime 11.5|12.0"
    LOVES+=("$f"); LOVE_RT+=("$rt")
done
[ -n "$UNIT" ] || die "which unit? give its address (Settings > Network shows it) or set R3I6NOS_UNIT"
case "$UNIT" in *@*) ;; *) UNIT="loveos@$UNIT" ;; esac
SSH_OPTS=(-o ConnectTimeout=10)

newest() {  # the newest existing file of the arguments (a glob that matched nothing is skipped)
    local f best=""
    for f in "$@"; do
        [ -f "$f" ] || continue
        if [ -z "$best" ] || [ "$f" -nt "$best" ]; then best="$f"; fi
    done
    printf '%s' "$best"
}

remote() { "$SSH" "${SSH_OPTS[@]}" "$@"; }

if [ "$MODE" = list ]; then
    exec "$SSH" "${SSH_OPTS[@]}" "$UNIT" r3i6nos-pkg list
fi
if [ "$MODE" = remove ]; then
    case "$REMOVE" in ''|*[!A-Za-z0-9._+-]*|.*) die "not a package name: $REMOVE" ;; esac
    flags=""; [ "$YES" = 1 ] && flags=" --yes"
    exec "$SSH" "${SSH_OPTS[@]}" -t "$UNIT" "r3i6nos-pkg remove$flags $REMOVE"
fi

# which files: those named, else the newest local build of every port and of this repo's own native apps
MISSING=()
if [ "${#FILES[@]}" -eq 0 ] && [ "${#LOVES[@]}" -eq 0 ]; then
    for entry in "${PORTS[@]}"; do
        IFS='|' read -r name dir kind rel _how <<<"$entry"
        [ "$kind" = deb ] || continue
        f="$(newest "$PORTS_ROOT/$dir/$rel")"
        if [ -n "$f" ]; then FILES+=("$f"); else MISSING+=("$name (no $PORTS_ROOT/$dir/$rel)"); fi
    done
    for f in "$NATIVE_BUILD/Commander.deb" "$NATIVE_BUILD/IO_Checker_Native.deb"; do
        if [ -f "$f" ]; then FILES+=("$f"); else MISSING+=("$(basename "$f" .deb) (not built: scripts/build-ports.sh)"); fi
    done
    # the LÖVE apps: every runtime's newest build of each name, from this repo's builds and from the LÖVE ports
    for rt in 11.5 12.0; do
        for f in "$LOVE_BUILD/Love $rt"/*.love; do
            [ -f "$f" ] && { LOVES+=("$f"); LOVE_RT+=("$rt"); }
        done
        for entry in "${PORTS[@]}"; do
            IFS='|' read -r name dir kind rel _how <<<"$entry"
            [ "$kind" = love ] || continue
            pattern="$PORTS_ROOT/$dir/${rel//\{v\}/$rt}"
            # the folder (it has a space in it: "Love 11.5") quoted, the file pattern after it a glob on purpose
            cands=()
            for c in "${pattern%/*}"/${pattern##*/}; do cands+=("$c"); done
            f="$(newest "${cands[@]}")"
            if [ -n "$f" ]; then LOVES+=("$f"); LOVE_RT+=("$rt"); fi
        done
    done
fi
[ "${#FILES[@]}" -gt 0 ] || [ "${#LOVES[@]}" -gt 0 ] || die "nothing to push: no .deb or .love builds found (scripts/build-ports.sh makes them)"
for f in "${FILES[@]+"${FILES[@]}"}" "${LOVES[@]+"${LOVES[@]}"}"; do [ -f "$f" ] || die "no such file: $f"; done

echo "To $UNIT:"
total=0
for f in "${FILES[@]+"${FILES[@]}"}"; do
    size="$(wc -c < "$f" | tr -d ' ')"
    total=$((total + size))
    printf '  %-34s %8s KB   built %s\n' "$(basename "$f")" "$((size / 1024))" "$(date -r "$f" '+%Y-%m-%d %H:%M' 2>/dev/null || echo '?')"
done
i=0
for f in "${LOVES[@]+"${LOVES[@]}"}"; do
    size="$(wc -c < "$f" | tr -d ' ')"
    total=$((total + size))
    printf '  %-34s %8s KB   built %s   (LÖVE %s)\n' "$(basename "$f")" "$((size / 1024))" "$(date -r "$f" '+%Y-%m-%d %H:%M' 2>/dev/null || echo '?')" "${LOVE_RT[$i]}"
    i=$((i + 1))
done
printf '  %d file(s), %d KB\n' "$((${#FILES[@]} + ${#LOVES[@]}))" "$((total / 1024))"
if [ "${#MISSING[@]}" -gt 0 ]; then
    echo "Not pushed (no local build):"
    printf '  %s\n' "${MISSING[@]}"
fi
if [ "$DRY" = 1 ]; then echo "(dry run: nothing sent)"; exit 0; fi

if [ "$YES" != 1 ]; then
    printf 'Push these? [y/N] '
    read -r answer
    case "$answer" in y|Y|yes|YES) ;; *) echo "not pushed"; exit 1 ;; esac
fi

# the inbox is a staging folder: what an earlier push left (a refused file) is cleared so it is not tried again
remote "$UNIT" 'mkdir -p ~/inbox/love-11.5 ~/inbox/love-12.0 && rm -f ~/inbox/*.deb ~/inbox/love-11.5/*.love ~/inbox/love-12.0/*.love' || { echo "push-packages: could not reach $UNIT over SSH (is SSH started in Settings > SSH, and the address right?)" >&2; exit 1; }
for f in "${FILES[@]+"${FILES[@]}"}"; do
    echo "sending $(basename "$f")"
    "$SCP" "${SSH_OPTS[@]}" -q "$f" "$UNIT:inbox/$(basename "$f")" || { echo "push-packages: copying $f failed" >&2; exit 1; }
done
i=0
for f in "${LOVES[@]+"${LOVES[@]}"}"; do
    echo "sending $(basename "$f") (LÖVE ${LOVE_RT[$i]})"
    "$SCP" "${SSH_OPTS[@]}" -q "$f" "$UNIT:inbox/love-${LOVE_RT[$i]}/$(basename "$f")" || { echo "push-packages: copying $f failed" >&2; exit 1; }
    i=$((i + 1))
done
flags="--yes"
[ -n "$CARD" ] && flags="$flags --card $CARD"
rc=0
if [ "${#FILES[@]}" -gt 0 ]; then
    "$SSH" "${SSH_OPTS[@]}" "$UNIT" "r3i6nos-pkg install --inbox $flags" || rc=1
fi
for rt in 11.5 12.0; do
    n=0; i=0
    for f in "${LOVES[@]+"${LOVES[@]}"}"; do
        [ "${LOVE_RT[$i]}" = "$rt" ] && n=$((n + 1))
        i=$((i + 1))
    done
    [ "$n" -gt 0 ] || continue
    # install-love takes the files by their folder's runtime; a refused file stays in the inbox, a good one is removed here
    "$SSH" "${SSH_OPTS[@]}" "$UNIT" "r3i6nos-pkg install-love --runtime $rt $flags ~/inbox/love-$rt/*.love && rm -f ~/inbox/love-$rt/*.love" || rc=1
done
exit "$rc"

#!/bin/bash
# scripts/push-packages.sh against a fake ssh and scp (no unit, no network): what it sends, asks, refuses and runs on the unit.
set -eu
cd "$(dirname "$0")/.."
T="$(mktemp -d "${TMPDIR:-/tmp}/r3-push.XXXXXX")"
trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
has() { grep -qF -- "$2" <<<"$1" || fail "$3: wanted \"$2\" in: $1"; }
hasnt() { if grep -qF -- "$2" <<<"$1"; then fail "$3: did not want \"$2\" in: $1"; fi; }

# fakes: each logs its arguments, one line a call; a file named fail-ssh or fail-scp in $T makes it fail
cat > "$T/ssh" <<SH
#!/bin/bash
echo "ssh \$*" >> "$T/log"
[ -f "$T/fail-ssh" ] && exit 255
exit 0
SH
cat > "$T/scp" <<SH
#!/bin/bash
echo "scp \$*" >> "$T/log"
[ -f "$T/fail-scp" ] && exit 1
exit 0
SH
chmod +x "$T/ssh" "$T/scp"
export R3I6NOS_SSH="$T/ssh" R3I6NOS_SCP="$T/scp" R3I6NOS_PORTS_ROOT="$T/ports" R3I6NOS_NATIVE_BUILD="$T/native" R3I6NOS_LOVE_BUILD="$T/love"
unset R3I6NOS_UNIT
mkdir -p "$T/ports/klamansi/r36s/build/pkg" "$T/ports/tinyntt/r36s/build/pkg" "$T/native"
printf 'a' > "$T/ports/klamansi/r36s/build/pkg/KlamANSI.deb"
printf 'bb' > "$T/ports/tinyntt/r36s/build/pkg/TinyNTT.deb"
printf 'ccc' > "$T/native/Commander.deb"
run() { : > "$T/log"; scripts/push-packages.sh "$@" 2>&1 < /dev/null || true; }
status() { scripts/push-packages.sh "$@" > /dev/null 2>&1 < /dev/null; echo $?; }
log() { cat "$T/log"; }

# a dry run connects to nothing and lists what it found and what it did not
out="$(run -n 192.168.1.50)"
has "$out" "To loveos@192.168.1.50:" "default user"
has "$out" "KlamANSI.deb" "port build found"
has "$out" "TinyNTT.deb" "second port found"
has "$out" "Commander.deb" "this repo's native build"
has "$out" "Not pushed (no local build)" "missing ones are said"
has "$out" "Neumetik" "a port with no build is named"
has "$out" "(dry run: nothing sent)" "dry run"
[ ! -s "$T/log" ] || fail "a dry run ran ssh or scp: $(log)"

# a question first; no answer, no push
out="$(run 192.168.1.50)"
has "$out" "not pushed" "no answer means no"
[ ! -s "$T/log" ] || fail "asked nothing but connected anyway: $(log)"

# --yes: clear the inbox, copy every file, then install from the inbox
out="$(run --yes me@10.0.0.7 "$T/ports/tinyntt/r36s/build/pkg/TinyNTT.deb")"
has "$out" "To me@10.0.0.7:" "a given user is kept"
L="$(log)"
has "$L" "rm -f ~/inbox/*.deb ~/inbox/love-11.5/*.love ~/inbox/love-12.0/*.love" "the inbox is cleared first"
has "$L" "scp -o ConnectTimeout=10 -q $T/ports/tinyntt/r36s/build/pkg/TinyNTT.deb me@10.0.0.7:inbox/TinyNTT.deb" "the file is copied"
has "$L" "r3i6nos-pkg install --inbox --yes" "the unit installs from the inbox"
hasnt "$L" "--card" "no card unless asked"
[ "$(grep -c '^scp' <<<"$L")" = 1 ] || fail "one file, one scp: $L"
[ "$(sed -n 1p "$T/log" | cut -c1-3)" = ssh ] || fail "the inbox is cleared before any copy"

# all the builds, a card asked for, the unit from the environment
out="$(R3I6NOS_UNIT=10.1.1.1 run --yes --card system)"
L="$(log)"
[ "$(grep -c '^scp' <<<"$L")" = 3 ] || fail "three builds, three copies: $L"
has "$L" "loveos@10.1.1.1:inbox/Commander.deb" "the unit from R3I6NOS_UNIT"
has "$L" "r3i6nos-pkg install --inbox --yes --card system" "the card is passed on"

# LÖVE apps: the runtime from the folder, sent to its own inbox folder, installed with install-love per runtime
mkdir -p "$T/love/Love 11.5" "$T/love/Love 12.0" "$T/ports/nocogama/build/Love 11.5" "$T/ports/nocogama/build/Love 12.0"
printf 'l1' > "$T/love/Love 11.5/IO_Checker.love"
printf 'l2' > "$T/love/Love 12.0/IO_Checker.love"
printf 'n1' > "$T/ports/nocogama/build/Love 11.5/NoCoGaMa.love"
out="$(run -n 10.1.1.1)"
has "$out" "IO_Checker.love" "this repo's LÖVE build is found"
has "$out" "NoCoGaMa.love" "a LÖVE port's build is found"
has "$out" "(LÖVE 12.0)" "the runtime is shown"
out="$(R3I6NOS_UNIT=10.1.1.1 run --yes)"
L="$(log)"
has "$L" "loveos@10.1.1.1:inbox/love-11.5/IO_Checker.love" "a 11.5 app goes to the 11.5 inbox"
has "$L" "loveos@10.1.1.1:inbox/love-12.0/IO_Checker.love" "a 12.0 app goes to the 12.0 inbox"
has "$L" "r3i6nos-pkg install-love --runtime 11.5 --yes ~/inbox/love-11.5/*.love" "installed per runtime"
has "$L" "r3i6nos-pkg install-love --runtime 12.0 --yes ~/inbox/love-12.0/*.love" "installed for 12.0 too"
[ "$(grep -c 'install-love' <<<"$L")" = 2 ] || fail "one install-love per runtime that has files: $L"
# only the files named; the runtime from the folder, or from --runtime
run --yes --card data 10.1.1.1 "$T/love/Love 12.0/IO_Checker.love" > /dev/null
L="$(log)"
[ "$(grep -c '^scp' <<<"$L")" = 1 ] || fail "one LÖVE file, one scp: $L"
has "$L" "install-love --runtime 12.0 --yes --card data" "the card is passed on"
hasnt "$L" "install --inbox" "no .deb, no .deb install"
printf 'x' > "$T/Game.love"
[ "$(status --yes 10.1.1.1 "$T/Game.love")" = 2 ] || fail "a .love with no runtime is a usage error"
run --yes --runtime 11.5 10.1.1.1 "$T/Game.love" > /dev/null
has "$(log)" "inbox/love-11.5/Game.love" "--runtime names it"
[ "$(status 10.1.1.1 --runtime 13 "$T/Game.love")" = 2 ] || fail "a bad runtime is a usage error"
# mixed: both kinds, both installs
run --yes 10.1.1.1 "$T/native/Commander.deb" "$T/love/Love 11.5/IO_Checker.love" > /dev/null
L="$(log)"
has "$L" "r3i6nos-pkg install --inbox --yes" "the .deb install"
has "$L" "install-love --runtime 11.5" "and the LÖVE install"

# list and remove
run 10.1.1.1 --list > /dev/null
has "$(log)" "loveos@10.1.1.1 r3i6nos-pkg list" "list"
run 10.1.1.1 --remove TinyNTT > /dev/null
has "$(log)" "-t loveos@10.1.1.1 r3i6nos-pkg remove TinyNTT" "remove asks on the unit's terminal"
run --yes 10.1.1.1 --remove TinyNTT > /dev/null
has "$(log)" "r3i6nos-pkg remove --yes TinyNTT" "--yes goes on"

# refusals and failures
[ "$(status)" = 2 ] || fail "no unit is a usage error"
[ "$(status 10.1.1.1 --card floppy)" = 2 ] || fail "a bad card is a usage error"
[ "$(status 10.1.1.1 --remove '../etc')" = 2 ] || fail "a package name cannot be a path"
[ "$(status 10.1.1.1 extra --yes)" = 2 ] || fail "a second unit is refused"
[ "$(status --yes 10.1.1.1 "$T/nothing.deb")" = 2 ] || fail "a missing file is refused"
touch "$T/fail-ssh"
out="$(run --yes 10.1.1.1 "$T/native/Commander.deb")"
has "$out" "could not reach" "an unreachable unit is said, with what to check"
rm "$T/fail-ssh"; touch "$T/fail-scp"
out="$(run --yes 10.1.1.1 "$T/native/Commander.deb")"
has "$out" "copying" "a failed copy stops before anything installs"
hasnt "$(log)" "install --inbox" "nothing is installed after a failed copy"
rm "$T/fail-scp"
rm -rf "$T/ports" "$T/native" "$T/love"
out="$(run --yes 10.1.1.1)"
has "$out" "nothing to push" "no builds at all"

echo "push-packages: ok"

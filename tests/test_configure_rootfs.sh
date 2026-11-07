#!/bin/bash
# configure_rootfs.sh against a fake rootfs, and the session loop against fakes.
set -eu
cd "$(dirname "$0")/.."

T="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos.XXXXXX")"
trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }

# --- configure_rootfs.sh
R="$T/rootfs"
mkdir -p "$R/usr/share/doc/foo" "$R/usr/share/man/man1"
touch "$R/usr/share/doc/foo/copyright" "$R/usr/share/doc/foo/changelog.gz" "$R/usr/share/man/man1/x.1"
mkdir -p "$T/love/11.5/bin" "$T/love/notaruntime"
printf '#!/bin/sh\n' > "$T/love/11.5/bin/love"; chmod +x "$T/love/11.5/bin/love"
R3I6NOS_LOVE_BUILD="$T/love" scripts/configure_rootfs.sh "$R" >/dev/null

[ -x "$R/opt/love/11.5/bin/love" ]              || fail "LÖVE 11.5 not installed"
[ ! -e "$R/opt/love/notaruntime" ]              || fail "a folder without bin/love was installed"
[ -x "$R/usr/local/bin/run_app.sh" ]            || fail "run_app.sh not installed executable"
[ -x "$R/usr/local/bin/loveos-session" ]        || fail "loveos-session not installed executable"
[ -f "$R/opt/loveos/menu/main.lua" ]            || fail "menu not installed"
[ "$(cat "$R/etc/hostname")" = r3i6nos ]        || fail "hostname"
grep -q 'LABEL=EASYROMS /roms' "$R/etc/fstab"   || fail "fstab games card"
grep -q 'nofail' "$R/etc/fstab"                 || fail "fstab nofail"
[ -L "$R/etc/systemd/system/multi-user.target.wants/loveos.service" ] || fail "unit not enabled"
[ -x "$R/usr/local/bin/r3i6nos-bootlog" ]        || fail "r3i6nos-bootlog not installed executable"
[ -L "$R/etc/systemd/system/multi-user.target.wants/r3i6nos-bootlog.service" ] || fail "bootlog unit not enabled"
grep -q '^ConditionPathExists=/boot/r3i6nos-bootlog' overlay/etc/systemd/system/r3i6nos-bootlog.service \
    || fail "bootlog must stay opt-in"

# bootlog writes its file (delays 0 so the test is instant); nothing else on the card
b="$(mktemp -d "${TMPDIR:-/tmp}/bootlog.XXXXXX")"
R3I6NOS_BOOTLOG_OUT="$b/log.txt" R3I6NOS_BOOTLOG_DELAYS="0 0" overlay/usr/local/bin/r3i6nos-bootlog \
    || fail "bootlog exited non-zero"
grep -q '===== cmdline =====' "$b/log.txt" || fail "bootlog wrote no cmdline section"
[ ! -e "$b/log.txt.tmp" ] || fail "bootlog left its temp file"
rm -rf "$b"
[ -f "$R/usr/share/doc/foo/copyright" ]         || fail "copyright was trimmed"
[ ! -e "$R/usr/share/doc/foo/changelog.gz" ]    || fail "doc not trimmed"
[ ! -e "$R/usr/share/man" ]                     || fail "man not trimmed"

# Running it twice must give the same tree (no nested menu/menu or love/11.5/11.5).
R3I6NOS_LOVE_BUILD="$T/love" scripts/configure_rootfs.sh "$R" >/dev/null
[ ! -e "$R/opt/love/11.5/11.5" ] || fail "second run nested the runtime"
[ ! -e "$R/opt/loveos/menu/menu" ] || fail "second run nested the menu"

# --- unit file names things that exist in the overlay
exec_start="$(sed -n 's/^ExecStart=//p' overlay/etc/systemd/system/loveos.service)"
[ -x "overlay$exec_start" ] || fail "ExecStart $exec_start is not in the overlay"

# --- loveos-session: menu picks, run_app gets the type and target, then exits (ONCE)
cat > "$T/menu" <<EOF
#!/bin/bash
printf 'love115\t/roms/Love 11.5/a game.love\n' > "\$R3I6NOS_NEXT"
printf '%s|%s\n' "\${LD_LIBRARY_PATH:-}" "\${LOVE_GRAPHICS_USE_OPENGLES:-}" > "$T/env"
EOF
cat > "$T/run_app" <<EOF
#!/bin/bash
printf '%s|%s\n' "\$1" "\$2" > "$T/got"
EOF
printf '#!/bin/bash\nexit 0\n' > "$T/watcher"
chmod +x "$T/menu" "$T/run_app" "$T/watcher"

R3I6NOS_ONCE=1 R3I6NOS_NEXT_DIR="$T/run" R3I6NOS_MENU_LOVE="$T/menu" \
R3I6NOS_RUN_APP="$T/run_app" R3I6NOS_WATCHER="$T/watcher" \
    overlay/usr/local/bin/loveos-session
[ "$(cat "$T/got")" = 'love115|/roms/Love 11.5/a game.love' ] || fail "session passed: $(cat "$T/got")"
[ "$(cat "$T/env")" = '|' ] || fail "no GPU dir, yet the session set GPU env: $(cat "$T/env")"

# With the Mali blob dir present: it goes first on LD_LIBRARY_PATH and LÖVE is told to use GLES.
mkdir -p "$T/libmali"
LD_LIBRARY_PATH=/other R3I6NOS_ONCE=1 R3I6NOS_NEXT_DIR="$T/run" R3I6NOS_MENU_LOVE="$T/menu" \
R3I6NOS_RUN_APP="$T/run_app" R3I6NOS_WATCHER="$T/watcher" R3I6NOS_GPU_DIR="$T/libmali" \
    overlay/usr/local/bin/loveos-session
[ "$(cat "$T/env")" = "$T/libmali:/other|1" ] || fail "GPU env wrong: $(cat "$T/env")"

echo "configure_rootfs and session: ok"

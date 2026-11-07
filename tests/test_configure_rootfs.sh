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
mkdir -p "$T/native"; printf 'not a real deb' > "$T/native/Input_Checker_Native.deb"
for v in 11.5 12.0; do mkdir -p "$T/loveapps/Love $v"; python3 -I scripts/pack_love.py apps/input-checker-love "$T/loveapps/Love $v/Input_Checker.love" >/dev/null; done
R3I6NOS_LOVE_BUILD="$T/love" R3I6NOS_NATIVE_BUILD="$T/native" R3I6NOS_LOVE_APPS_BUILD="$T/loveapps" scripts/configure_rootfs.sh "$R" >/dev/null

[ -x "$R/opt/love/11.5/bin/love" ]              || fail "LÖVE 11.5 not installed"
[ ! -e "$R/opt/love/notaruntime" ]              || fail "a folder without bin/love was installed"
[ -x "$R/usr/local/bin/run_app.sh" ]            || fail "run_app.sh not installed executable"
[ -x "$R/usr/local/bin/loveos-session" ]        || fail "loveos-session not installed executable"
[ -f "$R/opt/loveos/menu/main.lua" ]            || fail "menu not installed"
[ -f "$R/opt/loveos/theme/r3i6nos_ansiapps.lua" ] || fail "theme not installed"
[ -f "$R/opt/loveos/theme/vga8x16.lua" ]        || fail "theme font data not installed"
[ -f "$R/opt/loveos/theme/font/LICENSE.TXT" ]    || fail "the font's licence must ship with the theme"
[ "$(cat "$R/etc/hostname")" = r3i6nos ]        || fail "hostname"
grep -q 'LABEL=EASYROMS /roms' "$R/etc/fstab"   || fail "fstab games card"
grep -q 'nofail' "$R/etc/fstab"                 || fail "fstab nofail"
[ -L "$R/etc/systemd/system/multi-user.target.wants/loveos.service" ] || fail "unit not enabled"
# run_app.sh's default pad mapping carries the measured layout (docs/hardware-findings.md): buttons, not a hat
M="$(grep '^export SDL_GAMECONTROLLERCONFIG' overlay/usr/local/bin/run_app.sh)"
for tok in a:b1 b:b0 x:b2 y:b3 leftshoulder:b4 rightshoulder:b5 lefttrigger:b6 righttrigger:b7 dpup:b8 dpdown:b9 dpleft:b10 dpright:b11 back:b12 start:b13 leftstick:b14 rightstick:b15 leftx:a0 lefty:a1 rightx:a2 righty:a3; do
    case ",$M" in *",$tok,"*) ;; *) fail "run_app.sh mapping lacks $tok" ;; esac
done
case "$M" in *h0.*) fail "run_app.sh mapping still has a hat D-pad" ;; esac

# default apps: the checker in both LÖVE folders (a real zip with main.lua at the top) and the native deb
for v in 11.5 12.0; do
    f="$R/opt/loveos/defaults/Love $v/Input_Checker.love"
    [ -f "$f" ] || fail "default LÖVE $v app missing"
    python3 -I -c 'import sys, zipfile; z = zipfile.ZipFile(sys.argv[1]); n = z.namelist(); sys.exit(0 if "main.lua" in n and "conf.lua" in n and "model.lua" in n else 1)' "$f" \
        || fail "default LÖVE $v app is not a love archive with main.lua, conf.lua, model.lua"
done
[ -f "$R/opt/loveos/defaults/native_deb/Input_Checker_Native.deb" ] || fail "default native deb not installed"
[ "$(find "$R/opt/loveos/defaults" -type f ! -perm -444 | wc -l | tr -d ' ')" = 0 ] || fail "defaults must be world-readable"
R2="$T/rootfs-no-native"; mkdir -p "$R2"
R3I6NOS_LOVE_BUILD="$T/love" R3I6NOS_NATIVE_BUILD="$T/nothing-here" R3I6NOS_LOVE_APPS_BUILD="$T/nothing-here" scripts/configure_rootfs.sh "$R2" >/dev/null 2>"$T/warn.txt" \
    || fail "a missing native app must be a warning, not an error"
grep -q 'No native default app' "$T/warn.txt" || fail "no warning when the native app is missing"
grep -q 'No LÖVE default app' "$T/warn.txt" || fail "no warning when the LÖVE app is missing"
[ ! -e "$R2/opt/loveos/defaults/native_deb/Input_Checker_Native.deb" ] || fail "phantom native deb"
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

# Settings > Shutdown: the menu asks for "system<TAB>poweroff"; the session leaves the flag, exits
# (even without ONCE), and runs no app; r3i6nos-poweroff acts on the flag once, and only on it.
cat > "$T/menu_off" <<EOF2
#!/bin/bash
printf 'system\tpoweroff\n' > "\$R3I6NOS_NEXT"
EOF2
chmod +x "$T/menu_off"
rm -f "$T/got" "$T/run/poweroff"
env R3I6NOS_NEXT_DIR="$T/run" R3I6NOS_MENU_LOVE="$T/menu_off" \
R3I6NOS_RUN_APP="$T/run_app" R3I6NOS_WATCHER="$T/watcher" overlay/usr/local/bin/loveos-session &
sess=$!
( sleep 10; kill "$sess" 2>/dev/null ) &
wd=$!
wait "$sess" || fail "session did not exit 0 on a poweroff request"
kill "$wd" 2>/dev/null
[ -e "$T/run/poweroff" ] || fail "no poweroff flag left"
[ ! -e "$T/got" ] || fail "a poweroff request ran an app"
printf '#!/bin/bash\necho "$*" > "%s/ctl"\n' "$T" > "$T/systemctl"; chmod +x "$T/systemctl"
R3I6NOS_NEXT_DIR="$T/run" R3I6NOS_SYSTEMCTL="$T/systemctl" overlay/usr/local/bin/r3i6nos-poweroff
[ "$(cat "$T/ctl")" = "--no-block poweroff" ] || fail "poweroff ran: $(cat "$T/ctl" 2>/dev/null)"
[ ! -e "$T/run/poweroff" ] || fail "flag not consumed"
rm -f "$T/ctl"
R3I6NOS_NEXT_DIR="$T/run" R3I6NOS_SYSTEMCTL="$T/systemctl" overlay/usr/local/bin/r3i6nos-poweroff
[ ! -e "$T/ctl" ] || fail "powered off without the flag"
grep -q '^ExecStopPost=+/usr/local/bin/r3i6nos-poweroff' overlay/etc/systemd/system/loveos.service || fail "unit lacks ExecStopPost"
grep -q '^Restart=on-failure' overlay/etc/systemd/system/loveos.service || fail "unit would restart after a clean shutdown exit"

echo "configure_rootfs and session: ok"

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
# What the openssh-server package leaves in a fresh rootfs: an enabled service and the build's own host keys.
mkdir -p "$R/etc/systemd/system/multi-user.target.wants" "$R/etc/systemd/system/sockets.target.wants" "$R/etc/ssh"
ln -s /lib/systemd/system/ssh.service "$R/etc/systemd/system/multi-user.target.wants/ssh.service"
ln -s /lib/systemd/system/ssh.service "$R/etc/systemd/system/sshd.service"
ln -s /lib/systemd/system/ssh.socket "$R/etc/systemd/system/sockets.target.wants/ssh.socket"
# And what wpasupplicant leaves: its D-Bus service enabled (it failed at every boot: no D-Bus daemon in the image).
ln -s /lib/systemd/system/wpa_supplicant.service "$R/etc/systemd/system/multi-user.target.wants/wpa_supplicant.service"
ln -s /lib/systemd/system/wpa_supplicant.service "$R/etc/systemd/system/dbus-fi.w1.wpa_supplicant1.service"
touch "$R/etc/ssh/ssh_host_ed25519_key" "$R/etc/ssh/ssh_host_ed25519_key.pub" "$R/etc/ssh/ssh_host_rsa_key" "$R/etc/ssh/sshd_config"
for v in 11.5 12.0; do mkdir -p "$T/loveapps/Love $v"; python3 -I scripts/pack_love.py apps/input-checker-love "$T/loveapps/Love $v/Input_Checker.love" >/dev/null; done
R3I6NOS_LOVE_BUILD="$T/love" R3I6NOS_NATIVE_BUILD="$T/native" R3I6NOS_LOVE_APPS_BUILD="$T/loveapps" scripts/configure_rootfs.sh "$R" >/dev/null

[ -x "$R/opt/love/11.5/bin/love" ]              || fail "LÖVE 11.5 not installed"
[ ! -e "$R/opt/love/notaruntime" ]              || fail "a folder without bin/love was installed"
[ -x "$R/usr/local/bin/run_app.sh" ]            || fail "run_app.sh not installed executable"
[ -x "$R/usr/local/bin/loveos-session" ]        || fail "loveos-session not installed executable"
[ -x "$R/usr/local/bin/r3i6nos-battery" ]       || fail "r3i6nos-battery not installed executable"
[ -f "$R/opt/loveos/menu/battery.lua" ]         || fail "the menu's battery module not installed"
[ -f "$R/opt/loveos/menu/main.lua" ]            || fail "menu not installed"
[ -f "$R/opt/loveos/theme/r3i6nos_ansiapps.lua" ] || fail "theme not installed"
[ -f "$R/opt/loveos/theme/vga8x16.lua" ]        || fail "theme font data not installed"
[ -f "$R/opt/loveos/theme/font/LICENSE.TXT" ]    || fail "the font's licence must ship with the theme"
[ "$(cat "$R/etc/hostname")" = r3i6nos ]        || fail "hostname"
# the games cards are mounted by role, not by label (docs/STORAGE.md): no fstab line, the service enabled, the session waits for it
! grep -q 'EASYROMS' "$R/etc/fstab"             || fail "fstab still mounts a card by the EASYROMS label (two cards race for /roms)"
grep -q 'LABEL=BOOT .*nofail' "$R/etc/fstab"    || fail "fstab BOOT nofail"
[ -x "$R/usr/local/bin/r3i6nos-cards" ]         || fail "r3i6nos-cards not installed executable"
[ -L "$R/etc/systemd/system/multi-user.target.wants/r3i6nos-cards.service" ] || fail "r3i6nos-cards.service not enabled"
grep -q '^ExecStart=/usr/local/bin/r3i6nos-cards mount$' "$R/etc/systemd/system/r3i6nos-cards.service" || fail "the cards unit runs the wrong command"
grep -q '^After=.*r3i6nos-cards.service' "$R/etc/systemd/system/loveos.service" || fail "the session does not wait for the cards"
[ -x "$R/usr/local/bin/r3i6nos-sound" ]         || fail "r3i6nos-sound not installed executable"
[ -L "$R/etc/systemd/system/multi-user.target.wants/r3i6nos-sound.service" ] || fail "r3i6nos-sound.service not enabled (no app would be heard)"
grep -q '^Before=loveos.service' "$R/etc/systemd/system/r3i6nos-sound.service" || fail "the speaker is not turned on before the menu"
! grep -q 'roms.mount' "$R/etc/systemd/system/loveos.service" || fail "the session still waits for a roms.mount that no longer exists"
[ -d "$R/roms" ]                                || fail "mount point /roms missing"
[ -d "$R/roms2" ]                               || fail "mount point /roms2 missing"
[ "$(readlink "$R/opt/loveos/bin/r3i6nos-report")" = /usr/local/bin/r3i6nos-report ] || fail "the system report is not linked into the reports folder"
[ -x "$R/usr/local/bin/r3i6nos-report" ]        || fail "r3i6nos-report not installed executable"
[ -L "$R/etc/systemd/system/multi-user.target.wants/loveos.service" ] || fail "unit not enabled"
# run_app.sh's default pad mapping carries the measured layout (docs/hardware-findings.md): buttons, not a hat
# ... under the GUID read from the unit (KlamANSI's device report, 2026-11-10), with and without the name's CRC; and it reaches SDL as two lines
C="$(bash -c 'unset SDL_GAMECONTROLLERCONFIG; eval "$(sed -n "/^R36S_PAD=/,/}\"\$/p" overlay/usr/local/bin/run_app.sh)"; printf %s "$SDL_GAMECONTROLLERCONFIG"')"
M="$C"
[ "$(printf '%s\n' "$C" | grep -c '^1900bb3e4b4800000011000000010000,R36S GO-Super Gamepad,a:b1,')" = 1 ] || fail "run_app.sh mapping lacks the unit's GUID"
[ "$(printf '%s\n' "$C" | grep -c '^190000004b4800000011000000010000,R36S GO-Super Gamepad,a:b1,')" = 1 ] || fail "run_app.sh mapping lacks the CRC-less GUID"
case "$C" in *03000000341200006048*) fail "run_app.sh still has the spec's guessed GUID" ;; esac
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

# Settings > Network and SSH: the helper and its two units are installed, only the watcher is enabled, and SSH is off with no shared host keys
[ -x "$R/usr/local/bin/r3i6nos-net" ]                || fail "r3i6nos-net not installed executable"
[ -L "$R/etc/systemd/system/multi-user.target.wants/r3i6nos-net.path" ] || fail "the net path unit is not enabled"
[ ! -e "$R/etc/systemd/system/multi-user.target.wants/r3i6nos-net.service" ] || fail "the helper service must not be wanted by a target (rule 6: nothing at boot)"
grep -q '^PathExists=/run/loveos/net/request' "$R/etc/systemd/system/r3i6nos-net.path" || fail "the path unit watches the wrong file"
! grep -q '^\[Install\]' "$R/etc/systemd/system/r3i6nos-net.service" || fail "the helper service must have no [Install]"
[ ! -e "$R/etc/systemd/system/multi-user.target.wants/ssh.service" ] || fail "ssh.service is enabled at boot"
[ ! -e "$R/etc/systemd/system/sshd.service" ] || fail "the sshd alias is still there"
[ ! -e "$R/etc/systemd/system/sockets.target.wants/ssh.socket" ] || fail "ssh.socket is still wired to start"
[ -z "$(ls "$R"/etc/ssh/ssh_host_* 2>/dev/null)" ] || fail "host keys from the build are in the image: every card would share them"
[ -f "$R/etc/ssh/sshd_config" ] || fail "sshd_config was removed along with the host keys"
[ -d "$R/etc/ssh/sshd_config.d" ] || fail "the sshd drop-in folder is missing"
[ ! -e "$R/etc/systemd/system/multi-user.target.wants/wpa_supplicant.service" ] || fail "wpa_supplicant.service is enabled at boot"
if [ -e "$R/etc/systemd/system/dbus-fi.w1.wpa_supplicant1.service" ] || [ -L "$R/etc/systemd/system/dbus-fi.w1.wpa_supplicant1.service" ]; then
    fail "the wpa_supplicant D-Bus alias is still there"
fi
[ "$(readlink "$R/etc/systemd/system/wpa_supplicant.service")" = /dev/null ] || fail "wpa_supplicant.service is not masked"
# Settings > About: the build says what it is, and the helper that shows it is installed
grep -qx "VERSION=$(tr -d '[:space:]' < VERSION)" "$R/etc/r3i6nos-release" || fail "r3i6nos-release has the wrong VERSION"
grep -Eq '^COMMIT=([0-9a-f]{40}|unknown)$' "$R/etc/r3i6nos-release" || fail "r3i6nos-release has no COMMIT"
grep -qx 'REPO=joashchee/r3i6nos' "$R/etc/r3i6nos-release" || fail "r3i6nos-release names no repository"
grep -Eq '^MODIFIED=[01]$' "$R/etc/r3i6nos-release" || fail "r3i6nos-release has no MODIFIED"
[ -x "$R/usr/local/bin/r3i6nos-about" ] || fail "r3i6nos-about not installed executable"
R3I6NOS_ETC="$R/etc" python3 -I "$R/usr/local/bin/r3i6nos-about" info | grep -q "r3i6nos $(tr -d '[:space:]' < VERSION)" || fail "r3i6nos-about info does not read the build's version"

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
[ -d "$T/run/net" ] || fail "the session did not make the folder Settings > Network leaves its requests in"
[ "$(cat "$T/env")" = '|' ] || fail "no GPU dir, yet the session set GPU env: $(cat "$T/env")"

# Battery history: a sample at the session start, around each app (with the app's name), every interval while it runs, and at shutdown.
printf '#!/bin/bash\necho "$*" >> "%s/bat"\n' "$T" > "$T/battery"; chmod +x "$T/battery"
cat > "$T/run_slow" <<EOF
#!/bin/bash
printf '%s|%s\n' "\$1" "\$2" > "$T/got"
sleep 2.5
EOF
chmod +x "$T/run_slow"
rm -f "$T/bat"
R3I6NOS_ONCE=1 R3I6NOS_NEXT_DIR="$T/run" R3I6NOS_MENU_LOVE="$T/menu" R3I6NOS_RUN_APP="$T/run_slow" R3I6NOS_WATCHER="$T/watcher" \
R3I6NOS_BATTERY="$T/battery" R3I6NOS_BATTERY_INTERVAL=1 overlay/usr/local/bin/loveos-session
[ "$(sed -n 1p "$T/bat")" = "sample session" ]                    || fail "no session sample first: $(cat "$T/bat")"
[ "$(sed -n 2p "$T/bat")" = "sample app-start a game" ]           || fail "no app-start sample with the app's name: $(cat "$T/bat")"
[ "$(grep -c '^sample tick a game$' "$T/bat")" -ge 1 ]            || fail "no tick while the app ran: $(cat "$T/bat")"
[ "$(tail -n 1 "$T/bat")" = "sample app-end a game" ]             || fail "the last sample is not the app-end: $(cat "$T/bat")"
sleep 1.2
[ "$(tail -n 1 "$T/bat")" = "sample app-end a game" ]             || fail "the ticker kept sampling after the app ended: $(cat "$T/bat")"
# a missing or failing sampler never stops a game
printf '#!/bin/bash\nexit 1\n' > "$T/battery_bad"; chmod +x "$T/battery_bad"
rm -f "$T/got"
R3I6NOS_ONCE=1 R3I6NOS_NEXT_DIR="$T/run" R3I6NOS_MENU_LOVE="$T/menu" R3I6NOS_RUN_APP="$T/run_app" R3I6NOS_WATCHER="$T/watcher" \
R3I6NOS_BATTERY="$T/battery_bad" overlay/usr/local/bin/loveos-session
[ -e "$T/got" ] || fail "a failing battery sampler stopped the app from starting"
rm -f "$T/got"
R3I6NOS_ONCE=1 R3I6NOS_NEXT_DIR="$T/run" R3I6NOS_MENU_LOVE="$T/menu" R3I6NOS_RUN_APP="$T/run_app" R3I6NOS_WATCHER="$T/watcher" \
R3I6NOS_BATTERY="$T/nonexistent" overlay/usr/local/bin/loveos-session
[ -e "$T/got" ] || fail "a missing battery sampler stopped the app from starting"

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
rm -f "$T/got" "$T/run/poweroff" "$T/bat"
env R3I6NOS_NEXT_DIR="$T/run" R3I6NOS_MENU_LOVE="$T/menu_off" R3I6NOS_BATTERY="$T/battery" \
R3I6NOS_RUN_APP="$T/run_app" R3I6NOS_WATCHER="$T/watcher" overlay/usr/local/bin/loveos-session &
sess=$!
( sleep 10; kill "$sess" 2>/dev/null ) &
wd=$!
wait "$sess" || fail "session did not exit 0 on a poweroff request"
kill "$wd" 2>/dev/null
[ -e "$T/run/poweroff" ] || fail "no poweroff flag left"
[ "$(tail -n 1 "$T/bat")" = "sample shutdown" ] || fail "no shutdown sample before the power-off: $(cat "$T/bat")"
[ ! -e "$T/got" ] || fail "a poweroff request ran an app"
printf '#!/bin/bash\necho "$*" > "%s/ctl"\n' "$T" > "$T/systemctl"; chmod +x "$T/systemctl"
R3I6NOS_NEXT_DIR="$T/run" R3I6NOS_SYSTEMCTL="$T/systemctl" overlay/usr/local/bin/r3i6nos-poweroff
[ "$(cat "$T/ctl")" = "--no-block poweroff" ] || fail "poweroff ran: $(cat "$T/ctl" 2>/dev/null)"
[ ! -e "$T/run/poweroff" ] || fail "flag not consumed"
rm -f "$T/ctl"
R3I6NOS_NEXT_DIR="$T/run" R3I6NOS_SYSTEMCTL="$T/systemctl" overlay/usr/local/bin/r3i6nos-poweroff
[ ! -e "$T/ctl" ] || fail "powered off without the flag"
grep -q '^ExecStopPost=+/usr/local/bin/r3i6nos-poweroff' overlay/etc/systemd/system/loveos.service || fail "unit lacks ExecStopPost"
grep -q '^StateDirectory=r3i6nos-battery' overlay/etc/systemd/system/loveos.service || fail "unit has no state folder for the battery history"
grep -q '^Restart=on-failure' overlay/etc/systemd/system/loveos.service || fail "unit would restart after a clean shutdown exit"

echo "configure_rootfs and session: ok"

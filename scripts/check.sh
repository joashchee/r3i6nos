#!/bin/bash
# Runs every check the CI runs. Tools that are not installed are reported, not skipped silently.
set -u
cd "$(dirname "$0")/.." || exit 1
fail=0

step() { echo "== $1"; }

step "no vendor or huge files tracked"
if git rev-parse --git-dir >/dev/null 2>&1; then
    bad="$(git ls-files | grep -E '^local_hardware_assets|\.(img|zip)(\.gz)?$' || true)"
    big="$(git ls-files -z | xargs -0 find -maxdepth 0 -size +20M 2>/dev/null || true)"
    if [ -n "$bad$big" ]; then
        echo "FAIL: tracked files that must not be in the repo:" >&2
        printf '%s\n%s\n' "$bad" "$big" >&2
        fail=1
    fi
else echo "NOT RUN: not a git checkout"; fi

step "theme font up to date"
if python3 -c "import PIL" 2>/dev/null; then python3 scripts/build_vga_font.py --check || fail=1
else echo "NOT RUN: Pillow is not installed (the generated font files were not compared with the font)"; fi

step "python tests"
python3 -I -m unittest discover -s tests -v || fail=1

step "lua tests"
if command -v luajit >/dev/null; then
    luajit tests/test_scan.lua || fail=1
    luajit tests/test_battery.lua || fail=1
    luajit tests/test_settings.lua || fail=1
    luajit tests/test_clock.lua || fail=1
    luajit tests/test_mixer.lua || fail=1
    luajit tests/test_reports.lua || fail=1
    luajit tests/test_demos.lua || fail=1
    luajit tests/test_toast.lua || fail=1
    luajit tests/test_about.lua || fail=1
    luajit tests/test_net_menu.lua || fail=1
    luajit tests/test_theme.lua || fail=1
    luajit tests/test_menu_view.lua || fail=1
    luajit tests/test_io_checker.lua || fail=1
elif command -v lua >/dev/null; then
    lua tests/test_scan.lua || fail=1
    lua tests/test_battery.lua || fail=1
    lua tests/test_settings.lua || fail=1
    lua tests/test_clock.lua || fail=1
    lua tests/test_reports.lua || fail=1
    lua tests/test_demos.lua || fail=1
    lua tests/test_toast.lua || fail=1
    lua tests/test_net_menu.lua || fail=1
    lua tests/test_theme.lua || fail=1
    lua tests/test_menu_view.lua || fail=1
    lua tests/test_io_checker.lua || fail=1
else echo "NOT RUN: no lua/luajit"; fi

step "qjs-node shims (Node-style CLIs on QuickJS)"
if command -v node >/dev/null; then
    node tests/test_qjs_node.mjs || fail=1
    scripts/test_qjs_node.sh || fail=1
else echo "NOT RUN: no node (the QuickJS shims were not compared with Node's)"; fi

step "lua syntax"
if command -v luajit >/dev/null; then
    for f in menu/*.lua theme/*.lua apps/*/*.lua scripts/preview/*.lua tests/smoke/love/*.lua; do
        luajit -bl "$f" >/dev/null || fail=1
    done
else echo "NOT RUN: no luajit"; fi

step "native apps (C, SDL2)"
if command -v cc >/dev/null && command -v sdl2-config >/dev/null; then
    # Warnings are errors under clang (what these were written with); another compiler's new warnings are shown, not fatal.
    werror=""; cc --version 2>/dev/null | grep -qi clang && werror="-Werror"
    # every native app is listed by its app.conf (scripts/build_native_app.sh builds from the same file)
    for conf in apps/*/app.conf; do
        [ -f "$conf" ] || continue
        # shellcheck disable=SC1090  # the app's own manifest
        read -r name src < <(TYPE=native; . "$conf"; [ "$TYPE" = native ] && echo "$BIN $(dirname "$conf")/$SRC")
        [ -n "$name" ] || continue          # a LÖVE app (checked below)
        native_bin="$(mktemp "${TMPDIR:-/tmp}/r3i6nos-native.XXXXXX")"
        # shellcheck disable=SC2046  # sdl2-config prints separate flags
        if cc -std=c11 -Wall -Wextra $werror -O2 -I theme $(sdl2-config --cflags) -o "$native_bin" "$src" $(sdl2-config --libs) -lm; then
            out="$(SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$native_bin" --selftest 2>&1)"
            echo "$out"
            grep -q "^R3I6NOS_SMOKE ok $name=" <<<"$out" || { echo "FAIL: $name selftest"; fail=1; }
        else
            echo "FAIL: $name does not compile"; fail=1
        fi
        rm -f "$native_bin"
    done
else echo "NOT RUN: needs a C compiler and libsdl2-dev (sdl2-config)"; fi

step "xz and zstd decoders (apps/commander-native/lzma.h, zstd.h)"
if command -v cc >/dev/null; then tests/test_unpack.sh || fail=1
else echo "NOT RUN: needs a C compiler"; fi

step "theme video: presenting (theme/r3i6nos_ansiapps.h)"
if command -v cc >/dev/null && command -v sdl2-config >/dev/null; then
    tv_bin="$(mktemp "${TMPDIR:-/tmp}/r3i6nos-tv.XXXXXX")"
    # shellcheck disable=SC2046,SC2086  # sdl2-config prints separate flags; $werror is empty or one flag
    if cc -std=c11 -Wall -Wextra $werror -O2 -I theme $(sdl2-config --cflags) -o "$tv_bin" tests/test_theme_video.c $(sdl2-config --libs) -lm; then
        SDL_VIDEODRIVER=dummy "$tv_bin" || fail=1
    else echo "FAIL: tests/test_theme_video.c does not compile"; fail=1; fi
    rm -f "$tv_bin"
else echo "NOT RUN: needs a C compiler and libsdl2-dev (sdl2-config)"; fi

step "device report helper (theme/r3i6nos_report.h)"
if command -v cc >/dev/null && command -v sdl2-config >/dev/null; then
    report_bin="$(mktemp "${TMPDIR:-/tmp}/r3i6nos-report.XXXXXX")"
    # shellcheck disable=SC2046,SC2086  # sdl2-config prints separate flags; $werror is empty or one flag
    if cc -std=c11 -Wall -Wextra $werror -O2 -I theme $(sdl2-config --cflags) -o "$report_bin" tests/test_report.c $(sdl2-config --libs) -lm; then
        SDL_AUDIODRIVER=dummy "$report_bin" || fail=1
    else echo "FAIL: tests/test_report.c does not compile"; fail=1; fi
    rm -f "$report_bin"
else echo "NOT RUN: needs a C compiler and libsdl2-dev (sdl2-config)"; fi

step "Hot-plug toast (theme/r3i6nos_toast.h)"
if command -v cc >/dev/null && command -v sdl2-config >/dev/null; then
    toast_bin="$(mktemp "${TMPDIR:-/tmp}/r3i6nos-toast.XXXXXX")"
    # shellcheck disable=SC2046,SC2086  # sdl2-config prints separate flags; $werror is empty or one flag
    if cc -std=c11 -Wall -Wextra $werror -O2 -I theme $(sdl2-config --cflags) -o "$toast_bin" tests/test_toast.c $(sdl2-config --libs) -lm; then
        "$toast_bin" || fail=1
    else echo "FAIL: tests/test_toast.c does not compile"; fail=1; fi
    rm -f "$toast_bin"
else echo "NOT RUN: needs a C compiler and libsdl2-dev (sdl2-config)"; fi

step "Rec demo recorder (theme/r3i6nos_demo.h)"
if command -v cc >/dev/null && command -v sdl2-config >/dev/null; then
    demo_bin="$(mktemp "${TMPDIR:-/tmp}/r3i6nos-demo.XXXXXX")"
    # shellcheck disable=SC2046,SC2086  # sdl2-config prints separate flags; $werror is empty or one flag
    if cc -std=c11 -Wall -Wextra $werror -O2 -I theme $(sdl2-config --cflags) -o "$demo_bin" tests/test_demo.c $(sdl2-config --libs) -lm; then
        SDL_AUDIODRIVER=dummy "$demo_bin" || fail=1
    else echo "FAIL: tests/test_demo.c does not compile"; fail=1; fi
    rm -f "$demo_bin"
else echo "NOT RUN: needs a C compiler and libsdl2-dev (sdl2-config)"; fi

step "LÖVE apps (check, test, pack)"
if command -v luajit >/dev/null; then
    love_out="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-loveapps.XXXXXX")"
    for conf in apps/*/app.conf; do
        [ -f "$conf" ] || continue
        # shellcheck disable=SC1090  # the app's own manifest
        if (TYPE=native; . "$conf"; [ "$TYPE" = love ]); then
            R3I6NOS_LOVE_APPS_OUT="$love_out" scripts/build_love_app.sh "$(basename "$(dirname "$conf")")" || fail=1
        fi
    done
    rm -rf "$love_out"
else echo "NOT RUN: no luajit"; fi

step "ports (manifests)"
for conf in apps/*/app.conf; do
    [ -f "$conf" ] || continue
    # shellcheck disable=SC1090  # the app's own manifest
    if (TYPE=native; . "$conf"; [ "$TYPE" = port ]); then
        scripts/build_port.sh "$(basename "$(dirname "$conf")")" --validate || fail=1
    fi
done

step "shell tests"
bash tests/test_configure_rootfs.sh || fail=1
bash tests/test_flash_macos.sh || fail=1
bash tests/test_build_port.sh || fail=1
bash tests/test_image.sh || fail=1

step "shell syntax"
for f in overlay/usr/local/bin/run_app.sh overlay/usr/local/bin/loveos-session scripts/*.sh tests/*.sh tools/qjs-node/*.sh; do
    bash -n "$f" || fail=1
done

step "shellcheck"
if command -v shellcheck >/dev/null; then
    shellcheck overlay/usr/local/bin/run_app.sh overlay/usr/local/bin/loveos-session overlay/etc/profile.d/*.sh scripts/*.sh tests/*.sh tools/qjs-node/*.sh || fail=1
else echo "NOT RUN: shellcheck not installed"; fi

exit $fail

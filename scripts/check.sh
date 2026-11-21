#!/bin/bash
# Runs every check the CI runs. Tools that are not installed are reported, not skipped silently.
set -u
cd "$(dirname "$0")/.." || exit 1
# scripts/check.sh --app APP [--theme]: only that native app's checks (scripts/check-app.sh), in a minute instead of the whole run
if [ "${1:-}" = "--app" ]; then shift; exec scripts/check-app.sh "$@"; fi
fail=0

step() { echo "== $1"; }

step "no vendor or huge files tracked"
if git rev-parse --git-dir >/dev/null 2>&1; then
    # the test corpus's own tiny .img and .zip files (corpus/files, 64 KiB at most) are the one exception: they are test data, not a card image
    bad="$(git ls-files | grep -E '^local_hardware_assets|\.(img|zip)(\.gz)?$' | grep -v '^corpus/files/' || true)"
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

step "screen mirror page up to date (scripts/mirror_page.template.html and the font)"
python3 -I scripts/build_mirror_page.py --check || fail=1

step "boot logo BMP up to date (docs/brand/boot-logo.png)"
python3 -I scripts/make_boot_logo.py --check || fail=1

step "every open ROADMAP item has an effort rating (CLAUDE.md)"
python3 -I scripts/roadmap_effort.py || fail=1

step "python tests"
python3 -I -m unittest discover -s tests -v || fail=1

step "lua tests"
if command -v luajit >/dev/null; then
    luajit tests/test_scan.lua || fail=1
    luajit tests/test_battery.lua || fail=1
    luajit tests/test_netstatus.lua || fail=1
    luajit tests/test_settings.lua || fail=1
    luajit tests/test_clock.lua || fail=1
    luajit tests/test_datacard.lua || fail=1
    luajit tests/test_mixer.lua || fail=1
    luajit tests/test_reports.lua || fail=1
    luajit tests/test_demos.lua || fail=1
    luajit tests/test_toast.lua || fail=1
    luajit tests/test_sysconfirm.lua || fail=1
    luajit tests/test_appdata_lua.lua || fail=1   # needs python3 (the real engine) and luajit's ffi
    luajit tests/test_mirror.lua || fail=1
    luajit tests/test_mirror_menu.lua || fail=1
    luajit tests/test_pkgflag.lua || fail=1
    luajit tests/test_redraw.lua || fail=1
    luajit tests/test_menu_conf.lua || fail=1
    luajit tests/test_about.lua || fail=1
    luajit tests/test_net_menu.lua || fail=1
    luajit tests/test_theme.lua || fail=1
    luajit tests/test_menu_view.lua || fail=1
    luajit tests/test_portmaster_notice.lua || fail=1
    luajit tests/test_io_checker.lua || fail=1
elif command -v lua >/dev/null; then
    lua tests/test_scan.lua || fail=1
    lua tests/test_battery.lua || fail=1
    lua tests/test_netstatus.lua || fail=1
    lua tests/test_settings.lua || fail=1
    lua tests/test_clock.lua || fail=1
    lua tests/test_datacard.lua || fail=1
    lua tests/test_reports.lua || fail=1
    lua tests/test_demos.lua || fail=1
    lua tests/test_toast.lua || fail=1
    lua tests/test_sysconfirm.lua || fail=1
    lua tests/test_mirror.lua || fail=1
    lua tests/test_mirror_menu.lua || fail=1
    lua tests/test_redraw.lua || fail=1
    lua tests/test_menu_conf.lua || fail=1
    lua tests/test_net_menu.lua || fail=1
    lua tests/test_theme.lua || fail=1
    lua tests/test_menu_view.lua || fail=1
    lua tests/test_portmaster_notice.lua || fail=1
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

step "Aseprite reader (apps/commander-native/ase.h), cut and damaged files under the sanitizers"
if command -v cc >/dev/null; then tests/test_ase.sh || fail=1
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

step "true-colour themes (theme/r3i6nos_themes.h): every pair passes 4.5:1"
if command -v cc >/dev/null && command -v sdl2-config >/dev/null; then
    th_bin="$(mktemp "${TMPDIR:-/tmp}/r3i6nos-th.XXXXXX")"
    # shellcheck disable=SC2046,SC2086  # sdl2-config prints separate flags; $werror is empty or one flag
    if cc -std=c11 -Wall -Wextra $werror -O2 -I theme $(sdl2-config --cflags) -o "$th_bin" tests/test_themes.c $(sdl2-config --libs) -lm; then
        SDL_VIDEODRIVER=dummy "$th_bin" || fail=1
    else echo "FAIL: tests/test_themes.c does not compile"; fail=1; fi
    rm -f "$th_bin"
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

step "App data frontend (theme/r3i6nos_appdata.h), against the real r3i6nos-appdata"
if command -v cc >/dev/null && command -v sdl2-config >/dev/null && command -v python3 >/dev/null; then
    ad_bin="$(mktemp "${TMPDIR:-/tmp}/r3i6nos-appdata.XXXXXX")"
    # shellcheck disable=SC2046,SC2086  # sdl2-config prints separate flags; $werror is empty or one flag
    if cc -std=c11 -Wall -Wextra $werror -O2 -I theme $(sdl2-config --cflags) -o "$ad_bin" tests/test_appdata.c $(sdl2-config --libs) -lm; then
        SDL_VIDEODRIVER=dummy "$ad_bin" || fail=1
    else echo "FAIL: tests/test_appdata.c does not compile"; fail=1; fi
    rm -f "$ad_bin"
else echo "NOT RUN: needs a C compiler, libsdl2-dev (sdl2-config) and python3"; fi

step "Hot-plug toast (theme/r3i6nos_toast.h)"
if command -v cc >/dev/null && command -v sdl2-config >/dev/null; then
    toast_bin="$(mktemp "${TMPDIR:-/tmp}/r3i6nos-toast.XXXXXX")"
    # shellcheck disable=SC2046,SC2086  # sdl2-config prints separate flags; $werror is empty or one flag
    if cc -std=c11 -Wall -Wextra $werror -O2 -I theme $(sdl2-config --cflags) -o "$toast_bin" tests/test_toast.c $(sdl2-config --libs) -lm; then
        "$toast_bin" || fail=1
    else echo "FAIL: tests/test_toast.c does not compile"; fail=1; fi
    rm -f "$toast_bin"
else echo "NOT RUN: needs a C compiler and libsdl2-dev (sdl2-config)"; fi

step "input mapping reader and main menu (theme/r3i6nos_input.h, theme/r3i6nos_mainmenu.h)"
if command -v cc >/dev/null && command -v sdl2-config >/dev/null; then
    input_bin="$(mktemp "${TMPDIR:-/tmp}/r3i6nos-input.XXXXXX")"
    # shellcheck disable=SC2046,SC2086  # sdl2-config prints separate flags; $werror is empty or one flag
    if cc -std=c11 -Wall -Wextra $werror -O2 -I theme $(sdl2-config --cflags) -o "$input_bin" tests/test_input.c $(sdl2-config --libs) -lm; then
        "$input_bin" || fail=1
    else echo "FAIL: tests/test_input.c does not compile"; fail=1; fi
    rm -f "$input_bin"
else echo "NOT RUN: needs a C compiler and libsdl2-dev (sdl2-config)"; fi

step "screen mirror tap (theme/r3i6nos_mirror.h)"
if command -v cc >/dev/null && command -v sdl2-config >/dev/null; then
    mirror_bin="$(mktemp "${TMPDIR:-/tmp}/r3i6nos-mirror.XXXXXX")"
    # shellcheck disable=SC2046,SC2086  # sdl2-config prints separate flags; $werror is empty or one flag
    if cc -std=c11 -Wall -Wextra $werror -O2 -I theme $(sdl2-config --cflags) -o "$mirror_bin" tests/test_mirror.c $(sdl2-config --libs) -lm; then
        "$mirror_bin" || fail=1
    else echo "FAIL: tests/test_mirror.c does not compile"; fail=1; fi
    rm -f "$mirror_bin"
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
bash tests/test_build_ports.sh || fail=1
bash tests/test_portmaster.sh || fail=1
bash tests/test_push_packages.sh || fail=1
bash tests/test_runtimes.sh || fail=1
bash tests/test_streamtools.sh || fail=1
bash tests/test_run_app_runtime.sh || fail=1
bash tests/test_image.sh || fail=1

step "shell syntax"
for f in overlay/usr/local/bin/run_app.sh overlay/usr/local/bin/loveos-session apps/portmaster/portmaster apps/portmaster/shim/sudo scripts/*.sh tests/*.sh tests/runtimes/*.sh tests/streamtools/*.sh tools/runtimes/*/run tools/streamtools/build.sh tools/streamtools/*/run tools/qjs-node/*.sh tools/stretch-proto/*.sh tools/stretch-proto/stretch-proto-report; do
    bash -n "$f" || fail=1
done

step "shellcheck"
if command -v shellcheck >/dev/null; then
    shellcheck overlay/usr/local/bin/run_app.sh overlay/usr/local/bin/loveos-session apps/portmaster/portmaster apps/portmaster/shim/sudo overlay/etc/profile.d/*.sh scripts/*.sh tests/*.sh tests/runtimes/*.sh tests/streamtools/*.sh tools/runtimes/*/run tools/streamtools/build.sh tools/streamtools/*/run tools/qjs-node/*.sh tools/stretch-proto/*.sh tools/stretch-proto/stretch-proto-report || fail=1
else echo "NOT RUN: shellcheck not installed"; fi

exit $fail

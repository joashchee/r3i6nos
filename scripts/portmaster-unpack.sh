#!/bin/bash
# scripts/portmaster-unpack.sh [--to DIR] [--dry-run] PORT.zip [PORT.zip ...]
#
# Takes the GAME DATA out of PortMaster port zips and puts it where r3i6nos's menu looks for a game of an engine the image ships a runtime for
# (docs/PORTMASTER-TESTS.md, "Second set"; docs/RUNTIMES.md):  DIR/Pyxel/, DIR/Godot/, DIR/Renpy/, DIR/Solarus/, DIR/AGS/.  DIR defaults to
# build/port-games; copy its folders to a card's games partition (or point --to at the card, e.g. /Volumes/EASYROMS).
#
# PortMaster's own launch scripts mount the engine's squashfs with `$ESUDO mount`, which needs root (docs/PORTMASTER.md, item 1), so they are not
# used: only the data is. The script reads the zip, never runs anything in it, and writes only under DIR. Per zip it prints one line:
#   OK <engine> <name> -> <path>        the data was in the zip and is in place
#   NEEDS-DATA <engine> <name>: <why>   the port ships only a launcher and patches; the game itself is the person's own copy (a free itch.io
#                                       download, or bought), put it where the line says
#   SKIP <name>: <why>                  not an engine r3i6nos ships (weston, Mono, Java, Qt, libretro ...), or nothing recognisable
# Exit status 0 when every zip was read (NEEDS-DATA and SKIP are answers, not errors), 1 when a zip could not be read, 2 for a bad call.
#
# Untested on a unit. The layouts were read from PortMaster-New's zips of 2026-11-15 (Pyxel .pyxapp, loose main.py, a single .py; Godot .pck;
# Ren'Py game/; Solarus data.solarus or data/quest.dat; AGS ac2game.dat); a port that lays its data out otherwise says SKIP, not a guess.
set -eu
REPO="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$REPO/build/port-games"
DRY=0
ZIPS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --to) [ $# -ge 2 ] || { echo "portmaster-unpack: --to needs a folder" >&2; exit 2; }; DEST="$2"; shift 2 ;;
        --dry-run) DRY=1; shift ;;
        -h|--help) sed -n 2,20p "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "portmaster-unpack: unknown option $1" >&2; exit 2 ;;
        *) ZIPS+=("$1"); shift ;;
    esac
done
[ "${#ZIPS[@]}" -gt 0 ] || { echo "usage: scripts/portmaster-unpack.sh [--to DIR] [--dry-run] PORT.zip ..." >&2; exit 2; }
command -v unzip >/dev/null 2>&1 || { echo "portmaster-unpack: unzip not installed" >&2; exit 2; }

T="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-pmunpack.XXXXXX")"
trap 'rm -rf "$T"' EXIT
status=0

# first file under $1 matching the find expression given in the rest, else nothing
first() { local d="$1"; shift; find "$d" -type f \( "$@" \) 2>/dev/null | LC_ALL=C sort | head -n 1; }
put() {  # <engine> <name> <what> <from> <dest path>: copy, or say what would be
    if [ "$DRY" = 1 ]; then echo "OK $1 $2 -> $5 (dry run)"; return 0; fi
    mkdir -p "$(dirname "$5")"
    rm -rf "$5"
    cp -R "$4" "$5"
    echo "OK $1 $2 -> $5"
}

for zip in "${ZIPS[@]}"; do
    base="$(basename "$zip" .zip)"
    x="$T/$base"
    rm -rf "$x"; mkdir -p "$x"
    if ! unzip -q -o "$zip" -d "$x" 2>"$T/err"; then
        echo "FAIL $base: cannot read the zip: $(head -n 1 "$T/err")" >&2
        status=1
        continue
    fi
    # the launch script (top level *.sh) tells the engine; the files tell where the data is
    script="$(find "$x" -maxdepth 1 -type f -name '*.sh' | LC_ALL=C sort | head -n 1)"
    text=""
    if [ -n "$script" ]; then text="$(cat "$script")"; fi
    name="$(printf '%s' "$base" | tr -c 'A-Za-z0-9._-' '_')"

    case "$text" in
        *weston_pkg*|*mono-6*|*dotnet-*|*gmtoolkit*|*zulu*|*qt6-eglfs*|*pmjs*|*retroarch*|*godot_4*|*frt_4*|*rlvm*)
            echo "SKIP $name: needs a runtime r3i6nos does not ship (weston, Mono, .NET, Java, Qt, Godot 4, rlvm) or an emulator core"; continue ;;
    esac

    engine=""
    case "$text" in
        *pyxel_*|*PYXEL_PKG*) engine=pyxel ;;
        *renpy_*|*startRENPY*) engine=renpy ;;
        *solarus-*) engine=solarus ;;
        *frt_3*|*godot-3*) engine=godot ;;
        *ags_*|*ac2game*|*"/ags"*) engine=ags ;;
    esac
    if [ -z "$engine" ]; then
        echo "SKIP $name: no engine r3i6nos ships is named in its launch script"
        continue
    fi

    case "$engine" in
    pyxel)
        app="$(first "$x" -name '*.pyxapp')"
        if [ -n "$app" ]; then
            put pyxel "$name" app "$app" "$DEST/Pyxel/$name.pyxapp"
            continue
        fi
        main="$(first "$x" -name main.py)"
        if [ -n "$main" ]; then
            put pyxel "$name" folder "$(dirname "$main")" "$DEST/Pyxel/$name"
            continue
        fi
        # a single script that the launcher names (PYXEL_PKG="name.py"): the menu lists a folder with main.py, so it goes in as main.py
        pkg="$(printf '%s\n' "$text" | sed -n 's/^PYXEL_PKG="\{0,1\}\([^" ]*\)"\{0,1\}.*/\1/p' | head -n 1)"
        one=""
        if [ -n "$pkg" ]; then one="$(first "$x" -name "$pkg")"; fi
        if [ -z "$one" ]; then one="$(first "$x" -name '*.py')"; fi
        if [ -n "$one" ]; then
            if [ "$DRY" = 1 ]; then
                echo "OK pyxel $name -> $DEST/Pyxel/$name/main.py (dry run)"
            else
                mkdir -p "$DEST/Pyxel/$name"
                cp "$one" "$DEST/Pyxel/$name/main.py"
                echo "OK pyxel $name -> $DEST/Pyxel/$name/main.py"
            fi
            continue
        fi
        echo "NEEDS-DATA pyxel $name: no .pyxapp, main.py or script in the zip; put the game in $DEST/Pyxel/$name/ (main.py) or as a .pyxapp"
        ;;
    godot)
        pck="$(first "$x" -iname '*.pck')"
        if [ -n "$pck" ]; then
            put godot "$name" pck "$pck" "$DEST/Godot/$name/$(basename "$pck")"
            continue
        fi
        echo "NEEDS-DATA godot $name: the zip has no .pck (the launcher only says where one goes); put the game's .pck in $DEST/Godot/$name/ (Godot 3.x export; some ports also ship an xdelta3 patch to apply to the original first)"
        ;;
    renpy)
        game="$(find "$x" -type d -name game 2>/dev/null | LC_ALL=C sort | head -n 1)"
        if [ -n "$game" ] && [ -n "$(find "$game" -type f ! -name '.gitkeep' 2>/dev/null | head -n 1)" ]; then
            put renpy "$name" game "$(dirname "$game")" "$DEST/Renpy/$name"
            continue
        fi
        extra=""
        if [ -n "$(find "$x" -type f -path '*patches*' -name '*.rpy' 2>/dev/null | head -n 1)" ]; then
            extra=" (the port also patches the game's .rpy files for a small screen: that patch is not applied here)"
        fi
        echo "NEEDS-DATA renpy $name: the zip has no game/ folder with files; put the game's game/ folder in $DEST/Renpy/$name/game$extra"
        ;;
    solarus)
        sol="$(first "$x" -name 'data.solarus' -o -name 'data.solarus.zip' -o -name 'quest.dat')"
        if [ -n "$sol" ]; then
            case "$sol" in
                */data/quest.dat) put solarus "$name" quest "$(dirname "$(dirname "$sol")")" "$DEST/Solarus/$name" ;;
                *) put solarus "$name" quest "$(dirname "$sol")" "$DEST/Solarus/$name" ;;
            esac
            continue
        fi
        echo "NEEDS-DATA solarus $name: the zip has no data.solarus or data/quest.dat; put the quest in $DEST/Solarus/$name/"
        ;;
    ags)
        ags="$(first "$x" -name 'ac2game.dat' -o -iname '*.ags')"
        if [ -n "$ags" ]; then
            put ags "$name" game "$(dirname "$ags")" "$DEST/AGS/$name"
            continue
        fi
        echo "NEEDS-DATA ags $name: the zip has no ac2game.dat or .ags; put the game in $DEST/AGS/$name/"
        ;;
    esac
done
exit "$status"

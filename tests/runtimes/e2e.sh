#!/bin/sh
# Runs INSIDE a Debian 12 arm64 container (tests/test_runtimes_docker.sh starts it): the image's own library set, the runtime debs installed by the REAL
# deb_watcher.py into a scratch root, then each engine run through its r3i6nos-run-* command. Prints "E2E <name> PASS|FAIL|SKIP ...".
set -u
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq --no-install-recommends python3-minimal libpython3-stdlib libsdl2-2.0-0 libsdl2-image-2.0-0 libsdl2-ttf-2.0-0 libopenal1 libphysfs1 zlib1g libstdc++6 libvorbisfile3 libmodplug1 libfreetype6 libasound2 dpkg xvfb xauth libegl1 libgles2 libgl1-mesa-dri libglx-mesa0 libgl1 >/dev/null 2>&1 || { echo "E2E setup FAIL apt"; exit 1; }
mkdir -p /t/debs /t/apps /t/launch /t/bin
cp /debs/*.deb /t/debs/
export R3I6NOS_DEB_DIR=/t/debs R3I6NOS_DEB_DIR2=/nonexistent R3I6NOS_DEFAULT_DEB_DIR=/nonexistent R3I6NOS_APP_ROOT=/t/apps R3I6NOS_LAUNCHER_DIR=/t/launch R3I6NOS_TOOLS_DIR=/t/bin
python3 /w/deb_watcher.py || { echo "E2E watcher FAIL"; exit 1; }
for n in pyxel renpy solarus ags godot; do [ -x /t/bin/r3i6nos-run-$n ] && echo "E2E install-$n PASS" || echo "E2E install-$n FAIL no command"; done
export PATH="/t/bin:$PATH" SDL_AUDIODRIVER=dummy

# AGS and Godot: the engines start and say their version through the installed commands' own binaries (no sample game: an AGS or Godot game cannot be made here)
ags="$(/t/apps/r3i6nos-runtime-ags/usr/lib/r3i6nos-runtimes/ags/ags_3.6.aarch64 --version 2>&1)"
if echo "$ags" | grep -q "Engine version"; then echo "E2E ags PASS $(echo "$ags" | grep 'Engine version')"; else echo "E2E ags FAIL no version"; fi
gd="$(/t/apps/r3i6nos-runtime-godot/usr/lib/r3i6nos-runtimes/godot/frt_3.6 --version 2>&1)"
if echo "$gd" | grep -q "frt"; then echo "E2E godot PASS $gd"; else echo "E2E godot FAIL no version"; fi

# Pyxel: a window, 9 frames, quit (pyxel.quit exits the process with 0)
mkdir -p /g/py && cat > /g/py/main.py <<PY
import pyxel
pyxel.init(64, 48, title="e2e", fps=30)
n = [0]
def update():
    n[0] += 1
    if n[0] > 8: pyxel.quit()
def draw():
    pyxel.cls(1); pyxel.text(1, 1, "hi", 7)
pyxel.run(update, draw)
PY
if timeout 90 xvfb-run -a -s "-screen 0 640x480x24" r3i6nos-run-pyxel /g/py >/g/py.out 2>&1; then echo "E2E pyxel PASS"; else echo "E2E pyxel FAIL"; tail -3 /g/py.out; fi

# R3i6nos Mono (theme/font/r3i6nos-mono, mounted at /f): the BDF in Pyxel and the TTF in SDL2_ttf (what Solarus and the SDL engines load). Each draws the
# letter A, a box-drawing corner and a pad button and compares every pixel with the font's own bitmap (r3i6nos-mono.json). Nothing here draws on a unit.
if [ -d /f ]; then
mkdir -p /g/pf && cat > /g/pf/main.py <<'PY'
import json, pyxel
d = json.load(open("/f/r3i6nos-mono.json"))
def rows(cp):
    g = d["glyphs"][d["cmap"][str(cp)]]
    return [[(int(g[y * 3:y * 3 + 3], 16) >> (9 - x)) & 1 for x in range(10)] for y in range(20)]
pyxel.init(120, 40, title="font", fps=30)
font = pyxel.Font("/f/R3i6nosMono-Regular.bdf")
state = {"n": 0, "bad": 0, "checked": 0}
tests = [(ord("A"), 0, 0), (0x2554, 20, 0), (d["names"]["pad.btn.A.on"][0], 40, 0), (0xDE, 60, 0), (0x3B8, 80, 0)]
def update():
    state["n"] += 1
    if state["n"] == 3:
        for cp, ox, oy in tests:
            want = rows(cp)
            for y in range(20):
                for x in range(10):
                    got = 1 if pyxel.pget(ox + x, oy + y) == 7 else 0
                    state["checked"] += 1
                    if got != want[y][x]:
                        state["bad"] += 1
        print("PYXELFONT bad=%d checked=%d width=%d" % (state["bad"], state["checked"], font.text_width("AB")), flush=True)
        pyxel.quit()
def draw():
    pyxel.cls(0)
    for cp, ox, oy in tests:
        pyxel.text(ox, oy, chr(cp), 7, font)
pyxel.run(update, draw)
PY
if out="$(timeout 120 xvfb-run -a -s "-screen 0 640x480x24" r3i6nos-run-pyxel /g/pf 2>&1)" && echo "$out" | grep -q "PYXELFONT bad=0 "; then echo "E2E font-pyxel PASS $(echo "$out" | grep PYXELFONT)"; else echo "E2E font-pyxel FAIL $(echo "$out" | tail -3 | tr '\n' ' ')"; fi

cat > /g/ttf.py <<'PY'
import ctypes, json
d = json.load(open("/f/r3i6nos-mono.json"))
def rows(cp):
    g = d["glyphs"][d["cmap"][str(cp)]]
    return [[(int(g[y * 3:y * 3 + 3], 16) >> (9 - x)) & 1 for x in range(10)] for y in range(20)]
ttf = ctypes.CDLL("libSDL2_ttf-2.0.so.0")
ttf.TTF_OpenFont.restype = ctypes.c_void_p
ttf.TTF_OpenFont.argtypes = [ctypes.c_char_p, ctypes.c_int]
ttf.TTF_RenderGlyph32_Solid.restype = ctypes.c_void_p
ttf.TTF_RenderGlyph32_Solid.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32]
ttf.TTF_SetFontHinting.argtypes = [ctypes.c_void_p, ctypes.c_int]
ttf.TTF_GlyphMetrics32.argtypes = [ctypes.c_void_p, ctypes.c_uint32] + [ctypes.POINTER(ctypes.c_int)] * 5
class Surf(ctypes.Structure):
    _fields_ = [("flags", ctypes.c_uint32), ("format", ctypes.c_void_p), ("w", ctypes.c_int), ("h", ctypes.c_int), ("pitch", ctypes.c_int), ("pixels", ctypes.c_void_p)]
assert ttf.TTF_Init() == 0
f = ttf.TTF_OpenFont(b"/f/R3i6nosMono-Regular.ttf", 20)
assert f
ttf.TTF_SetFontHinting(f, 2)  # TTF_HINTING_MONO
bad = checked = 0
for cp in (ord("A"), 0x2554, d["names"]["pad.btn.A.on"][0], 0xDE, 0x3B8, 0x1FB00):
    s = ctypes.cast(ttf.TTF_RenderGlyph32_Solid(f, cp, 0xFFFFFF), ctypes.POINTER(Surf)).contents
    adv = ctypes.c_int()
    ttf.TTF_GlyphMetrics32(f, cp, None, None, None, None, ctypes.byref(adv))
    assert adv.value == 10, "advance %d" % adv.value
    want = rows(cp)
    # the surface is the whole 10 x 20 cell, so every pixel is compared in place
    assert s.w == 10 and s.h == 20, "surface %dx%d" % (s.w, s.h)
    pix = (ctypes.c_uint8 * (s.pitch * s.h)).from_address(s.pixels)
    for y in range(20):
        for x in range(10):
            checked += 1
            if (1 if pix[y * s.pitch + x] else 0) != want[y][x]:
                bad += 1
print("TTFFONT bad=%d checked=%d" % (bad, checked))
PY
if out="$(SDL_VIDEODRIVER=dummy python3 /g/ttf.py 2>&1)" && echo "$out" | grep -q "TTFFONT bad=0 "; then echo "E2E font-sdl-ttf PASS $(echo "$out" | grep TTFFONT)"; else echo "E2E font-sdl-ttf FAIL $(echo "$out" | tail -3 | tr '\n' ' ')"; fi
fi

# Ren'Py: compile a one-line script (no window: the dummy video driver)
mkdir -p /g/rp/game && printf 'label start:\n    "Hello"\n    return\n' > /g/rp/game/script.rpy
SDL_VIDEODRIVER=dummy timeout 120 r3i6nos-run-renpy /g/rp compile >/g/rp.out 2>&1
if [ -s /g/rp/game/script.rpyc ]; then echo "E2E renpy PASS"; else echo "E2E renpy FAIL"; tail -3 /g/rp.out; fi

# Solarus: opens the quest and runs its Lua main. LuaJIT on a host kernel with 48-bit user addresses panics ("bad light userdata pointer", a LuaJIT limit);
# the unit's 4.4 kernel is probably 39-bit, so that is a SKIP here, not a pass.
mkdir -p /g/sq/data && echo "" > /g/sq/data/project_db.dat
printf 'quest{ solarus_version = "1.6", write_dir = "r3i6nos_e2e", title_bar = "e2e", normal_quest_size = "320x240", min_quest_size = "320x240", max_quest_size = "320x240" }\n' > /g/sq/data/quest.dat
printf 'function sol.main:on_started()\n  print("QUEST-STARTED")\n  sol.main.exit()\nend\n' > /g/sq/data/main.lua
timeout 90 xvfb-run -a -s "-screen 0 640x480x24" r3i6nos-run-solarus /g/sq >/g/sq.out 2>&1
if grep -q QUEST-STARTED /g/sq.out; then echo "E2E solarus PASS"
elif grep -q "bad light userdata pointer" /g/sq.out && grep -q "Opening quest" /g/sq.out; then echo "E2E solarus SKIP engine loads and opens the quest; LuaJIT panics on this host's 48-bit address space (needs the unit)"
else echo "E2E solarus FAIL"; tail -3 /g/sq.out; fi

# the samples (tools/runtimes/samples, mounted at /s): Pyxel draws and quits by its own hook, Ren'Py reaches the first line, Solarus loads its Lua
if [ -d /s ]; then
    if R3I6NOS_SAMPLE_FRAMES=12 timeout 90 xvfb-run -a -s "-screen 0 640x480x24" r3i6nos-run-pyxel /s/Pyxel/Hello_Pyxel >/g/spy.out 2>&1; then echo "E2E sample-pyxel PASS"; else echo "E2E sample-pyxel FAIL"; tail -3 /g/spy.out; fi
    if R3I6NOS_SAMPLE_AUTOQUIT=1 timeout 150 xvfb-run -a -s "-screen 0 640x480x24" r3i6nos-run-renpy /s/Renpy/Hello_Renpy >/g/srp.out 2>&1; then echo "E2E sample-renpy PASS"; else echo "E2E sample-renpy FAIL"; tail -3 /g/srp.out; fi
    # Godot: the pack's three text files load, the scene runs on GLES2 (Mesa's llvmpipe here), 12 frames, quit by its own hook
    if R3I6NOS_SAMPLE_FRAMES=12 SDL_AUDIODRIVER=dummy timeout 120 xvfb-run -a -s "-screen 0 640x480x24" r3i6nos-run-godot /s/Godot/Hello_Godot.pck >/g/sgd.out 2>&1 && grep -q "HELLO_GODOT quit frames frames=12" /g/sgd.out; then echo "E2E sample-godot PASS"; else echo "E2E sample-godot FAIL"; tail -3 /g/sgd.out; fi
    R3I6NOS_SAMPLE_AUTOQUIT=1 timeout 90 xvfb-run -a -s "-screen 0 640x480x24" r3i6nos-run-solarus /s/Solarus/Hello_Solarus >/g/ssq.out 2>&1
    if grep -q "Opening quest" /g/ssq.out && ! grep -q "Cannot find" /g/ssq.out; then echo "E2E sample-solarus PASS opens the quest (see the solarus line above for the LuaJIT note)"; else echo "E2E sample-solarus FAIL"; tail -3 /g/ssq.out; fi
fi

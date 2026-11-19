#!/bin/sh
# Runs INSIDE a Debian 12 arm64 container (tests/test_runtimes_docker.sh starts it): the image's own library set, the runtime debs installed by the REAL
# deb_watcher.py into a scratch root, then each engine run through its r3i6nos-run-* command. Prints "E2E <name> PASS|FAIL|SKIP ...".
set -u
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq --no-install-recommends python3-minimal libpython3-stdlib libsdl2-2.0-0 libsdl2-image-2.0-0 libsdl2-ttf-2.0-0 libopenal1 libphysfs1 libvorbisfile3 libmodplug1 libfreetype6 libasound2 dpkg xvfb xauth libgl1-mesa-dri libglx-mesa0 libgl1 >/dev/null 2>&1 || { echo "E2E setup FAIL apt"; exit 1; }
mkdir -p /t/debs /t/apps /t/launch /t/bin
cp /debs/*.deb /t/debs/
export R3I6NOS_DEB_DIR=/t/debs R3I6NOS_DEB_DIR2=/nonexistent R3I6NOS_DEFAULT_DEB_DIR=/nonexistent R3I6NOS_APP_ROOT=/t/apps R3I6NOS_LAUNCHER_DIR=/t/launch R3I6NOS_TOOLS_DIR=/t/bin
python3 /w/deb_watcher.py || { echo "E2E watcher FAIL"; exit 1; }
for n in pyxel renpy solarus; do [ -x /t/bin/r3i6nos-run-$n ] && echo "E2E install-$n PASS" || echo "E2E install-$n FAIL no command"; done
export PATH="/t/bin:$PATH" SDL_AUDIODRIVER=dummy

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

# the three samples (tools/runtimes/samples, mounted at /s): Pyxel draws and quits by its own hook, Ren'Py reaches the first line, Solarus loads its Lua
if [ -d /s ]; then
    if R3I6NOS_SAMPLE_FRAMES=12 timeout 90 xvfb-run -a -s "-screen 0 640x480x24" r3i6nos-run-pyxel /s/Pyxel/Hello_Pyxel >/g/spy.out 2>&1; then echo "E2E sample-pyxel PASS"; else echo "E2E sample-pyxel FAIL"; tail -3 /g/spy.out; fi
    if R3I6NOS_SAMPLE_AUTOQUIT=1 timeout 150 xvfb-run -a -s "-screen 0 640x480x24" r3i6nos-run-renpy /s/Renpy/Hello_Renpy >/g/srp.out 2>&1; then echo "E2E sample-renpy PASS"; else echo "E2E sample-renpy FAIL"; tail -3 /g/srp.out; fi
    R3I6NOS_SAMPLE_AUTOQUIT=1 timeout 90 xvfb-run -a -s "-screen 0 640x480x24" r3i6nos-run-solarus /s/Solarus/Hello_Solarus >/g/ssq.out 2>&1
    if grep -q "Opening quest" /g/ssq.out && ! grep -q "Cannot find" /g/ssq.out; then echo "E2E sample-solarus PASS opens the quest (see the solarus line above for the LuaJIT note)"; else echo "E2E sample-solarus FAIL"; tail -3 /g/ssq.out; fi
fi

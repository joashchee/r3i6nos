# PortMaster on r3i6nos: the cheapest set of ports that tests every part

Made 2026-11-18 from PortMaster's own catalogue (`ports.json` of the PortMaster-New release 2026-11-15_2009: 1,506 ports, 1,302 that can run on aarch64)
and a read of the launch scripts of 234 of the small ports. **Nothing below has been run on a unit except VVVVVV** (it passed, ROADMAP section 31).
Sizes are the downloads from PortMaster (the port's zip, plus a runtime the first time it is needed; a runtime is shared by every port that names it).

## What the catalogue says about what matters

- **948 of the 1,302 ports need no runtime**; **354 need one** (squashfs images of Godot, Python, Java, Qt, .NET, Mono, Ren'Py and others), and **84 of those also need `weston_pkg`**
  (an X11 / Wayland layer). So the question of whether a login user can mount a runtime (`$ESUDO mount`, see `docs/PORTMASTER.md`, "the root model") decides about a quarter of the library;
  the weston ones need a display server on the DRM device, a second question.
- **Almost every script calls `$GPTOKEYB`** (keyboard-from-pad through `/dev/uinput`), most also `$ESUDO chmod 666 /dev/uinput`, and many `$ESUDO systemctl restart oga_events` (harmless if it fails).
  VVVVVV passing therefore says the pad reaches SDL; it does not yet show `gptokeyb` works. A game that can only be played by key presses (a typing game) shows it at once.
- **Ports are not all free to try:** `rtr` ("ready to run") ports carry their data; the others (paid, or free from an itch page) need the person's own copy. The cheapest test sets below use `rtr` ports only, except where a missing-data message is the point.
- **Filters:** 203 ports are armhf-only (r3i6nos has no armhf), 4 want 2 GB (`2gb`), 56 want a larger screen (`!lowres`), 2 want 4:3 (the R36S is 640x480, 4:3: fine), 33 want a second stick (`analog_2`: the unit has two).

## The set, cheapest first (stop at the first group that fails: later groups depend on it)

| # | Port (zip) | Download | What it tests |
|---|---|---|---|
| 1 | **lilt** (rtr, SDL, no runtime) | 21 KB | The smallest install, run and return: control.txt, `get_controls`, `pm_platform_helper`, `pm_finish`, the sudo shim, no `.gptk` file |
| 2 | ~~wordlesdl~~ | | **Dropped after the first run:** it draws on the Linux console (`/dev/tty0`) and is played by typing into it, so it tests the console as much as `gptokeyb`. Use 3 for `gptokeyb` |
| 3 | **sdlsopwith** (rtr) | 60 KB | gptokeyb with an arcade mapping, sound, and a run of a few minutes (stability, no leftover `gptokeyb` after exit) |
| 4 | ~~2048~~ | | **Dropped after the first run:** it is a RetroArch libretro core and runs `/usr/bin/retroarch`, which r3i6nos does not have and will not have (no emulators). Every libretro-based port is out of scope here |
| 5 | **penguneverleft** (paid, bring your own copy, a LÖVE game) | 10 KB | The missing-data path: a clear message and a clean return to the menu, not a hang; and a LÖVE port beside r3i6nos's own LÖVE |
| 6 | **azimuth** (rtr, ships gl4es) | 1.8 MB | OpenGL on a GLES-only GPU: gl4es over the Mali blob (`libgl_default.txt`, `LIBGL_*`); the cheapest ready-to-run game that does this (found by reading the zips' central directories; `stuntcarracer` 1.9 MB and `rawgl` 2.1 MB are the next) |
| 7 | **towerjumper** (rtr, `frt_2.1.6`) | 1.3 MB + runtime 7.2 MB = **8.5 MB** | **The first runtime: the squashfs mount without root** (and `umount` at exit). If this fails, every runtime port (354) is out until the root model is decided |
| 8 | **momo** (rtr, `python_3.11`) | 0.04 MB + 33.3 MB = 33.4 MB | A Python runtime: pysdl2 inside a runtime, beside the system's Python |
| 9 | **dustracing2d** (rtr, `qt6-eglfs-6.6.3`) | 5.6 MB + 40.5 MB = 46.1 MB | Qt on EGLFS, straight on the Mali EGL with no X server |
| 10 | **xskat** (rtr, `weston_pkg_0.2`) | 0.26 MB + 55.4 MB = 55.7 MB | **weston**: an X11 app through a Wayland compositor on the DRM device, as a login user (a seat, `video`/`input` groups). The second root question |
| 11 | **mrplatformer** (rtr, `godot_4.2.2` + weston) | 1.3 MB + 22.1 MB = 23.4 MB more | Godot 4 (Vulkan-less GLES) over weston; shares the weston image from 10 |
| 12 | **voidbeat** (rtr, same runtimes as 11) | 1.6 MB | **A runtime already on the card is reused**: no second runtime download; mounts and unmounts again |

Total for all twelve: **about 171 MB**, of which 141 MB is four runtimes. Groups 1-6 alone are **2 MB**.

## First run on the unit (2026-11-18, maintainer's report) and what was done about it

| Port | Result |
|---|---|
| lilt (1) | ran; left with Select+Start |
| wordlesdl, sdlsopwith (2, 3) | ran; sopwith had sound; **the pad did nothing and neither could be left** |
| 2048 (4) | did not run: it needs RetroArch (above) |
| penguneverleft (5) | LÖVE error "cannot load game at path ..." (no game data: expected), **and the menu did not come back** |
| azimuth (6), towerjumper (7), momo (8), dustracing2d (9), xskat (10), mrplatformer (11), voidbeat (12) | **did not run** (reasons not read yet) |

Read together: a port that runs but ignores the pad and cannot be quit is a `gptokeyb` that is not working (it makes the keys from the pad through `/dev/uinput`, and
holds the Select+Start exit); every runtime port waits on the squashfs mount without root, which was always the open question. **Done:** `r3i6nos-portquit` (a port in its own process group is
ended by holding Select+Start for 2 seconds, with or without `gptokeyb`; `run_app.sh port`), an udev rule and a module list that give the `input` group `/dev/uinput`
(`overlay/etc/udev/rules.d/60-r3i6nos-uinput.rules`, `overlay/etc/modules-load.d/r3i6nos-uinput.conf`: untested, and a no-op if the kernel has no uinput), and
**`scripts/portmaster-diag.sh`**, a read-only report to run on the unit (kernel options, devices, whether a user can mount, each port's missing libraries and last log lines).
**Next:** run the diagnostic and read why azimuth (no runtime, so no mount) failed; it should answer most of this.

## Second set: games for the engines r3i6nos ships (made 2026-11-21; **nothing here has been run on a unit**)

r3i6nos ships five runtimes pre-extracted (`docs/RUNTIMES.md`): Pyxel 2.2.8, Ren'Py 8.3.4, Solarus 1.6.5, Godot/FRT 3.6 and AGS 3.6. **They do not make PortMaster's own ports start from PortMaster:**
those launch scripts mount the squashfs with `$ESUDO mount`, which needs root (`docs/PORTMASTER.md`, item 1). What works is taking the port's **game data out of its zip** and putting it in the
folder the menu scans on a card (`Pyxel/`, `Renpy/`, `Godot/`, `Solarus/`, `AGS/`), where the menu runs it with `r3i6nos-run-<engine>`: no root, no mount, no PortMaster. The ports below were picked from
the catalogue (`ports.json` of 2026-11-15, 1,506 ports) because their data is in the zip (`rtr`) or is a free download, they are small, and their runtime is the version we ship or the nearest to it.
**Run our own samples first** (`tools/runtimes/samples/{Pyxel,Renpy,Solarus}`): if a sample fails, every port of that engine will, and the cause is the engine, not the port.

| # | Engine | Port | Size | Data | What it tests |
|---|---|---|---|---|---|
| 1 | Pyxel 2.2.8 | **entropipes** | 0.1 MB | `rtr`, one `.pyxapp` | The simplest case: a packed game, window, pad, quit |
| 2 | Pyxel | **ticoban** | 0.1 MB | `rtr` | Pad-driven puzzle: d-pad and A/B through SDL |
| 3 | Pyxel | **vortexion** | 0.1 MB | `rtr` | An arcade game with sound |
| 4 | Pyxel | **megaball** | 0.04 MB | `rtr`, loose `.py` and `.pyxres` | The unpacked form: a folder with `main.py` |
| 5 | Pyxel | **pyxeljack** | 0.03 MB | `rtr` | A second small game, for frame pacing |
| 6 | Godot 3.6 | **yolkheroes** | 0.3 MB | `.pck` | The one port built for the Godot 3.6 we ship: GLES2 on the Mali |
| 7 | Godot 3.6 | **cairn** | 0.1 MB | free download | A second 3.6 port |
| 8 | Godot 3.5 data | **turbotabletennis** | 0.1 MB | `rtr` | 3.5 data on the 3.6 engine: **the first that may fail on a version mismatch** |
| 9 | Godot 3.5 | **starcatcher**, **hyper.jump** | 1.1 / 1.5 MB | `rtr` | A platformer and an action game: pad feel, frame rate |
| 10 | Godot 3.5 | **pachycefalosaurus** | 0.8 MB | `rtr`, full game | A strategy game on the pad |
| 11 | Godot 3.5 | **yomihd** | 10 MB | `rtr`, demo | Bigger assets, memory on 1 GB |
| 12 | Ren'Py 8.3.4 | **justkisshimalready** | 33 MB | `rtr`, full game | A full game: start-up time, peak memory, text input |
| 13 | Ren'Py 8.3.4 | **milkinside**, **onenighthotsprings** | 0.6 MB | free itch download | The missing-data message, then a small game |
| 14 | Solarus 1.6.5 | **yarntown_pm** | 0.3 MB | free itch download | Whether Solarus's Lua (LuaJIT) works on the 4.4 kernel's address space |
| 15 | AGS 3.6 | **kenslabyrinth** | 9.8 MB | `rtr` (zip not read: check it holds an `ac2game.dat`) | The only AGS candidate with data; the software renderer |
| 16 | Godot 3.6 | **chasmal** | 0.1 MB | free download | Two sticks (`analog_2`; the unit has two) |

Total about 20 MB without Ren'Py's full game, about 55 MB with it.

- **Version risk:** a `.pck` pack is not guaranteed to load across Godot minor versions, and a Pyxel or Ren'Py game built for a newer or older runtime may not load. Left out for that reason: Godot 4.x ports, Pyxel ports on 2.3 to 2.9, Ren'Py 8.1.3 ports.
- **`gptokeyb`:** many Godot ports expect the keyboard from `gptokeyb` (`turbotabletennis.gptk` is one). The menu's runner does not start it, so a game with no joypad action may ignore the pad: that is a finding about the game, not a failure of the runtime.
- **Left out because it is not cheap:** anything that needs weston, Mono, .NET, Java or Qt; rlvm (an interpreter of a commercial engine: ask first, `docs/RUNTIMES.md`).
- **How to try one:** unzip the port, copy its data (`<port>/<name>.pyxapp`, the `.pck`, the `game/` folder, the quest) into `<card>/Pyxel/<Name>/`, `Godot/`, `Renpy/<Name>/` or `Solarus/<Name>/`, install the engine's package (`scripts/push-packages.sh UNIT build/runtimes/*.deb`), and start it from the menu. Bring back, as in "What to bring back": did it draw, take the pad, make sound, return with Select+Start held 2 s; and the `== runtimes ==` section of `r3i6nos-report`.

## Not a download: the lists and the lifecycle (free)

- **Hidden or flagged (checked on the unit, 2026-11-18, all three as they should be):** `toziuha2` (needs 2 GB), `shovel.knight` (armhf only) and **`nanosaur`** (needs `opengl`) are not in PortMaster's list; "Shovel Knight Pocket Dungeon" is a different port and is listed. **`opengl` is real desktop GL**: `device_info.txt` adds it only when Mesa's DRI drivers and a `libGL` that is not gl4es or the Mali blob are installed. The R36S has only the Mali GLES blob, so every port that needs `opengl` (2 in the catalogue: `nanosaur`, `bugdom`) is correctly hidden; ports that carry gl4es are the ones to test.
- **Offline:** switch Wi-Fi off, open PortMaster (offline mode) and run an installed port.
- **Cancel** a download part-way (start `dustracing2d`, press B): no half port left, `.tmp` cleared at the next start.
- **Uninstall** a port from PortMaster, and check it leaves the menu and the card.
- **Memory and heat:** run `azimuth` (group 6) for ten minutes and read `r3i6nos-report` afterwards.

## Left out because it costs more than it adds (try only after groups 7-11 pass)

- Java: `unciv` (51 MB + JDK 8 55 MB + weston 55 MB), or the free `sandtrix` (74 KB, but the game's jar is the person's own download).
- .NET and GameMaker toolkit (`dotnet-8.0.12` 30 MB + `gmtoolkit` 121 MB), Mono (262 MB), Ren'Py, Solarus, rlvm: every one of their cheap ports needs the person's own data (free itch downloads for Ren'Py and Solarus: `onenighthotsprings`, `yarntown_pm`, would work).
- Native ports with `gl4es` and a data file of the person's own (the Sonic ports).

## What to bring back

For each port: whether it installed, started, drew, took the pad (and the keyboard mapping, for 2 and 3), made sound, and returned to the menu; `/roms/ports/<port>/log.txt`
(the scripts write one) and, for a failure, the last lines. For group 7, `dmesg | tail` and the error from the mount (`mount: ... must be superuser`, `unknown filesystem type 'squashfs'`).
Over SSH: `ls -l /dev/uinput`, `grep -E "SQUASHFS|UINPUT|LOOP" /proc/config.gz` (zcat).

(The catalogue changes weekly: the sizes and names above are from 2026-11-15_2009.)

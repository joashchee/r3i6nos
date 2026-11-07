# Default apps

Built into every image (`/opt/loveos/defaults`), so the menu is never empty. Anything on the games card
(`EASYROMS`) is listed beside them, and a file of the same name there replaces the built-in one.

All three are the **R36S Input Checker** from `docs/R36S_Input_Checker_Specification.md`: a 640×480
diagnostic that shows every button, axis and the D-pad as the pad reports them, raw and as SDL's gamepad
mapping, with a log of the last events. It exists to find out a unit's real input layout (ROADMAP: the
gamepad mapping in `run_app.sh` is unverified).

| App | Where it is built in | Source |
|---|---|---|
| LÖVE 11.5 | `Love 11.5/Input_Checker.love` | `input-checker-love/` |
| LÖVE 12.0 | `Love 12.0/Input_Checker.love` | `input-checker-love/` (same code, packed twice; it prints the runtime it runs on) |
| Native ARM64 | `native_deb/Input_Checker_Native.deb` | `input-checker-native/` (C and SDL2, no other libraries) |

- LÖVE: `scripts/configure_rootfs.sh` packs the folder with `scripts/pack_love.py` (same bytes every time).
  The logic is in `model.lua` and tested by `tests/test_input_checker.lua`, which also runs `main.lua` against a
  fake LÖVE and a fake gamepad.
- Native: `scripts/build_native_apps.sh` cross-compiles it in a Debian 12 container and packs the `.deb`
  (the `native` job in `.github/workflows/build.yml`); `scripts/smoke_native.sh` runs it under qemu with the
  dummy video driver. At boot `deb_watcher.py` installs it like any `.deb` on the card.
  `input-checker-native --screenshot out.bmp` draws a demo frame to a file, for looking at the layout.

**Leaving:** hold Select+Start (raw buttons 14 and 17) for a second, or Esc on a keyboard. Every version also
leaves by itself after **15 seconds** without input, with a countdown in the last 10, so nobody gets stuck in it.

**Button numbers** on screen count from 1, as LÖVE does. The layout is from a real unit (`docs/hardware-findings.md`):
the D-pad is buttons 9-12, not a hat, and SDL has no matching gamepad mapping, so the checker reads raw buttons.
L1/R1/L2/R2 (5-8) are a guess until measured. Power and the volume keys are not joystick buttons: the VOL-, VOL+ and
PWR pills light from keyboard key events, if the system delivers them (not yet seen). Event lines are numbered
(`#27 btn 11 DOWN (held)`) and say whether the button is actually held right then.

**Run on a real R36S:** the first LÖVE 11.5 version (2026-11-07), which found its layout bugs. This revision has not been.
Tested here: the logic, the LÖVE code against a fake LÖVE, the native app's draw and self-test on a Mac (dummy video).
The LÖVE layout itself has only been drawn by LÖVE on the device; the native one is checked by screenshot here.

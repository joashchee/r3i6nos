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

**Leaving:** hold Select+Start for a second (needs SDL's gamepad mapping for the pad), or Esc on a keyboard.
Because that mapping is unverified, every version also leaves by itself after 2 minutes without input, with a
countdown in the last 15 seconds, so an unmapped unit cannot get stuck in it.

**Not yet run on a real R36S:** none of the three has been seen on the panel or with the pad. Tested here:
the logic, the LÖVE code against a fake LÖVE, the native app's draw and self-test on a Mac (dummy video).

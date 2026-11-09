# Sound

How r3i6nos makes sound work on the R36S: the output path, the volume, the volume keys, and Settings > Sound mixer.
**Nothing here has been heard on a real R36S yet** (CLAUDE.md rule 1); the open items are in `ROADMAP.md` section 6.

## The parts

| Part | Where | What it does |
|---|---|---|
| `r3i6nos-sound` | `overlay/usr/local/bin/` | root helper: sets the output, makes and restores the volumes, follows the jack and the volume keys, saves the levels |
| `r3i6nos-sound.service` | `overlay/etc/systemd/system/` | runs `set` before the menu (`ExecStartPre`), then `start`; `save` when stopped (shutdown) |
| `/etc/asound.conf` | `overlay/etc/` | puts two software volumes, **Volume** and **Boost**, in front of card 0 for every app |
| Settings > Sound mixer | `menu/mixer.lua`, `menu/settings.lua`, `menu/view.lua`, `menu/main.lua` | every mixer control of card 0, changed while a test sound loops |

## The output: Playback Path

The stock 4.4 kernel's RK817 codec driver (Rockchip BSP) powers its outputs only through the ALSA control **Playback Path** (`OFF`,
`RCV`, `SPK`, `HP`, `HP_NO_MIC`, `BT`, `SPK_HP`, ...), which starts at `OFF`. On Android the audio HAL sets it; on Debian nothing did,
so no app was heard (2026-11-09). `r3i6nos-sound` sets it at boot: `SPK`, or `HP` while headphones are in. The device tree's
simple-audio-card has a headphone-detect GPIO ("Headphone Jack", `SW_HEADPHONE_INSERT` on an input device); the codec does not move
the path itself, so `r3i6nos-sound start` reads that switch and moves it.

## The volume: Volume and Boost

The codec driver has no volume control: its DAC level comes from the device tree (`spk-volume`, `hp-volume`; both are in the captured
DTBs), near full. So `/etc/asound.conf` sends the `default` device, which LÖVE (OpenAL Soft) and SDL2 apps open, through two ALSA
`softvol` plugins on card 0:

| Control | Range | Steps | First boot |
|---|---|---|---|
| **Volume** | -40 to 0 dB | 2 dB (21 levels) | 80% (-8 dB) |
| **Boost** | 0 to +18 dB | 3 dB (7 levels) | +6 dB |

Boost makes quiet sound louder and clips sound that is already loud; it is never left at its maximum by default. A `plug` in front
converts any app's rate and format. There is no `dmix`: one app plays at a time (the menu exits before an app starts).

ALSA makes a softvol's control only when a program first opens the device, so `r3i6nos-sound set` opens `default` for 23 ms of
silence (`aplay`) at boot, then restores the levels saved in `/var/lib/alsa/asound.state` (`alsactl restore 0`). On a first boot, or
when the saved state predates the volumes (it has no `Boost`), it sets the levels above instead.

**Saving:** `r3i6nos-sound save` (`alsactl store 0`) runs when the service stops, so at a clean shutdown. A level changed and then
lost to a forced power-off is back to the last saved one at the next boot. The menu runs as `loveos` and cannot write the state itself.

## The volume keys

The volume keys are keyboard keys, not pad buttons (`docs/hardware-findings.md`, the input table). `r3i6nos-sound start` finds every
input device whose `capabilities/key` has `KEY_VOLUMEUP` or `KEY_VOLUMEDOWN` and steps **Volume** one level (2 dB) per press and per
auto-repeat. It reads the devices without grabbing them, so apps still see the keys. **Untested on the unit:** which device sends them.

## Settings > Sound mixer

Every control `amixer -c 0 scontents` lists that has something to change (a level or a choice), Volume and Boost first, then the
codec's own (Playback Path). Up/Down choose, Left/Right change one step, L1/R1 five, B goes back. The last row is the **test sound**:
Off, a 441 Hz tone on the left only, the right only, both, and a short loop of music (four chords, an arpeggio and a bass), made in
code by `mixer.sample` so no recording is in the image. It loops while the screen is up and stops when you leave it. The Playback
Path can be changed here to try the other outputs; the jack sets it again when headphones go in or out.

## The device report

r3i6nos's report (`r3i6nos-report`, `sound` section) gets `r3i6nos-sound status`: each card's Playback Path (FAIL at `OFF`), the
jack, headphones in or out, the volume-key devices, and every mixer control. **Volume and Boost missing is a FAIL**: the software
volumes were not made, so the keys and the mixer do nothing. Whether sound is heard is not measured; the IO Checker asks a person.

## Tests

`tests/test_r3i6nos_sound.py` (fake `/proc`, `/sys`, `/dev`, `amixer`, `aplay`, `alsactl`), `tests/test_mixer.lua` (parsing, steps,
the dialog through Settings, its screen, the test sounds), `tests/test_r3i6nos_report.py` (the sound section).

**Untested on the unit:** that `/etc/asound.conf` loads and the controls appear at boot; the levels heard; Boost's clipping; the volume
keys' device; the test sound under the menu's LÖVE; the levels kept across a restart.

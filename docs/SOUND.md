# Sound

How r3i6nos makes sound work on the R36S: the output path, the volume, the volume keys, and Settings > Sound mixer.
**Nothing here has been heard on a real R36S yet** (CLAUDE.md rule 1); the open items are in `ROADMAP.md` section 6.

## The parts

| Part | Where | What it does |
|---|---|---|
| `r3i6nos-sound` | `overlay/usr/local/bin/` | root helper: sets the output (the codec's path, or a USB card), makes and restores the volumes, follows the volume keys and USB cards, saves the levels |
| `r3i6nos-sound.service` | `overlay/etc/systemd/system/` | runs `set` before the menu (`ExecStartPre`), then `start`; `save` when stopped (shutdown) |
| `/etc/asound.conf` | `overlay/etc/` | puts two software volumes, **Volume** and **Boost**, in front of the output (card 0, or a USB card) for every app |
| Settings > Sound mixer | `menu/mixer.lua`, `menu/settings.lua`, `menu/view.lua`, `menu/main.lua` | every mixer control of card 0, changed while a test sound loops |

## The output: Playback Path

The stock 4.4 kernel's RK817 codec driver (Rockchip BSP) powers its outputs only through the ALSA control **Playback Path** (`OFF`,
`RCV`, `SPK`, `HP`, `HP_NO_MIC`, `BT`, `SPK_HP`, ...), which starts at `OFF`. On Android the audio HAL sets it; on Debian nothing did,
so no app was heard (2026-11-09). This unit's codec lists four: `OFF SPK HP SPK_HP`.

**`r3i6nos-sound` sets `HP`, always**, whatever the jack. Heard on the unit (2026-11-10, the maintainer, Settings > Sound mixer's test
music, Volume 30%, Boost 14%):

| Path | Nothing in the jack | Headphones in the 3.5 mm jack |
|---|---|---|
| `OFF` | silent | silent |
| `SPK` (chosen after `OFF`, while playing) | silent | silent |
| `HP` | **the speaker** | **the headphones** |
| `SPK_HP` | the speaker (only tried after `HP`) | the headphones |

The speaker was never heard with headphones in, on any path: the jack cuts the speaker off by itself. And once `HP` had played, going
back to `SPK` kept the speaker playing, so `SPK`'s silence is about how the driver powers up from `OFF` while a stream runs. (An earlier
image set `SPK` at boot, before any program played, and the IO Checker's sweep was heard: so `SPK` can work from a cold start, but `HP`
works in every case tried.) The jack's switch ("rockchip,rk817-codec Headphones", `SW_HEADPHONE_INSERT`) is still read, for the report;
when it changes, `r3i6nos-sound` sets `HP` again, which also undoes a path chosen by hand in the mixer.

## USB headphones and USB sound cards

**On the unit (2026-11-10), USB headphones were silent on every path**: `/etc/asound.conf` played only on card 0, the codec. The stock
kernel has `snd-usb-audio` built in, so a USB sound device should appear as card 1 (`/proc/asound/cards`, driver `USB-Audio`), if the
port works as a USB host for it. Now:

- `/etc/asound.conf`'s volumes play on **`r3i6nos_out`**: card 0, unless `/run/r3i6nos/asound-out.conf` redefines it. That file is loaded
  by an alsa-lib hook with `errors false`, so a missing file is fine; a broken one would stop all sound, so it is only ever written whole
  (a rename) by `r3i6nos-sound`. Checked in Debian 12's alsa-lib (`tests/test_asound_conf.py`).
- `r3i6nos-sound` writes it at boot when a USB card that can play (`pcm0p`) is there, and looks again every 2 seconds: plugged in, the USB
  card is the output; pulled out, the file is removed and card 0 is back. Volume and Boost stay on card 0 either way, so the volume keys
  and the mixer work the same.
- **A program that already has sound open keeps its device.** The next one to open sound gets the new output. The menu opens sound when it
  starts, so its own sounds and the mixer's test sound go to the USB headphones only if they were plugged in before the menu started (at
  boot, or after an app was run and closed).
- The USB card's own mixer (often a `PCM` or `Headphone` level) is left as the device starts it; the mixer screen shows card 0 only.

## The volume: Volume and Boost

The codec driver has no volume control: its DAC level comes from the device tree (`spk-volume`, `hp-volume`; both are in the captured
DTBs), near full. So `/etc/asound.conf` sends the `default` device, which LÖVE (OpenAL Soft) and SDL2 apps open, through two ALSA
`softvol` plugins on card 0:

| Control | Range | Steps | First boot |
|---|---|---|---|
| **Volume** | -40 to 0 dB | 2 dB (21 levels) | 80% (-8 dB) |
| **Boost** | 0 to +18 dB (step 1 to 7; step 0 is silence and never used) | 3 dB | +6 dB (step 3) |

Boost makes quiet sound louder and clips sound that is already loud; it is never left at its maximum by default.

**Two alsa-lib rules shape these** (alsa-lib 1.2.8, `src/pcm/pcm_softvol.c`; `tests/test_asound_conf.py` checks both). A softvol's
`min_dB` must be negative, or alsa-lib refuses it and **no program can open the default device**: the first image with Boost had
`min_dB 0.0`, and every app on the unit failed with "Couldn't open audio device" (2026-11-10). And a softvol's step 0 is silence, whatever
its `min_dB`. So Boost is set from -3 dB, its step 0 (silence) is never used, and its step 1 is exactly 0 dB, which alsa-lib passes
through untouched. `r3i6nos-sound` and the mixer stop at step 1, and a saved Boost of 0 is put back to 1 at boot. Volume's step 0 is
silence too: the mixer shows it as "off". A `plug` in front
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
Off, a 1 kHz tone on the left only, the right only, both, and a short loop of music (four chords, an arpeggio and a bass, all above
340 Hz: the speaker gave nothing heard at 440 Hz in the IO Checker, 2026-11-09), made in
code by `mixer.sample` so no recording is in the image. It loops while the screen is up and stops when you leave it. The Playback
Path can be changed here to try the other outputs; the jack sets it again when headphones go in or out.

## The device report

r3i6nos's report (`r3i6nos-report`, `sound` section) gets `r3i6nos-sound status`: every card by name, the output (card 0 or a USB
card), each card's Playback Path (FAIL at `OFF`), the
jack, headphones in or out, the volume-key devices, and every mixer control. **Volume and Boost missing is a FAIL**: the software
volumes were not made, so the keys and the mixer do nothing. Whether sound is heard is not measured; the IO Checker asks a person.

## Tests

`tests/test_r3i6nos_sound.py` (fake `/proc`, `/sys`, `/dev`, `amixer`, `aplay`, `alsactl`), `tests/test_mixer.lua` (parsing, steps,
the dialog through Settings, its screen, the test sounds), `tests/test_r3i6nos_report.py` (the sound section).

**Untested on the unit:** `HP` set from a cold boot (it was heard only when chosen in the mixer); a USB card appearing at all (the
port as a USB host) and heard; the switch to and from it while plugged and pulled; that `/etc/asound.conf` loads and the controls appear at boot; the levels heard; Boost's clipping; the volume
keys' device; the test sound under the menu's LÖVE; the levels kept across a restart.

# Hot-plug toast: what was plugged in or out, at the top of the display

When something is plugged in or out while the unit runs, the screen says so at once: a **toast over the top bar (row 0)**. A change flashes for
**3 seconds**. A **failure stays until a key is pressed**, and that key is used up (it clears the toast and does nothing else). CLAUDE.md rule 13
makes it a rule for every R36S port.

Status: **specified and built 2026-11-11, tested on a Mac with fake sysfs and a fake kernel log only. Nothing here has run on a real R36S**: the
sysfs names of the unit's USB, jack and power supplies have never been read on it (ROADMAP section 19).

## What is watched

`overlay/usr/local/bin/r3i6nos-hotplug` is a root service (`r3i6nos-hotplug.service`, started before the menu). It looks every second (every 0.25 s while a change is being confirmed) and announces a
change once it shows in **two looks in a row** (a bouncing jack contact is one event, not three). What is plugged in at start is not announced.

| What | How it is read | Toast (info, 3 s) |
|---|---|---|
| 3.5 mm jack | the codec's headphone switch (`SW_HEADPHONE_INSERT`), read through `r3i6nos-sound`'s own code | `Headphones connected` / `Headphones removed` |
| USB devices (the OTG port) | `/sys/bus/usb/devices`: each device (not its interfaces, not the root hubs), named by its product string, kinded by its interface classes | `USB audio connected: <name>: sound plays there now`, `USB audio removed: <name>: sound is back on the speaker`; likewise `USB controller or keyboard`, `USB storage`, `USB wireless`, `USB hub`, `USB device` |
| Wi-Fi | `/sys/class/net/<wireless interface>/operstate` is `up` when joined to a network; the SSID comes from `iw dev <if> link`, asked once when the link comes up and remembered to name it when it goes | `Wi-Fi connected: <network>` / `Wi-Fi disconnected: <network>` (a dropped link, going out of range, Settings > Network > Disconnect, or a join from the menu alike). A blip shorter than a second is not an event; a link already up at start is not announced |
| USB gamepad | a USB HID device with a `jsN` under its input device (the kernel's joystick handler): a gamepad; other HID is a keyboard or mouse | `USB gamepad connected: <name>` / `removed`, `USB keyboard or mouse ...` |
| Battery | `/sys/class/power_supply/*` of type Battery: `capacity`, `status` | `Battery low: 15%` (at 15% or less, not charging; armed again above 20%), `Battery full` (status turns Full). Not announced if it starts low |
| TF cards (microSD) | `/sys/block/mmcblk*` (not boot or rpmb areas); TF1 is the boot disk (`BOOT_DISK=` in `/run/r3i6nos/cards`), TF2 the other; mounted from `/proc/mounts` | `TF2 card in: it is read at boot, so restart to use it` (`docs/STORAGE.md`: cards are mounted at boot only), `TF2 card removed` |
| Volume keys | `r3i6nos-sound` writes `<count> <percent>` to `/run/r3i6nos/volume` after each press; the daemon waits until the file has rested 0.6 s, so a held key is one toast | `Volume 72%` (also at the end of the range) |
| DC power port | `/sys/class/power_supply/*` that is not the battery and has `online`: type `Mains` is the DC port, `USB*` the USB port | `DC power connected` / `DC power disconnected`, `USB power connected` / `USB power disconnected` |

**Failures** (an `error` event; the toast is white on red, says `Press a key`, and stays):

- a USB device that would not enumerate: the kernel log (`/dev/kmsg`) says so (`unable to enumerate USB device`, `device descriptor read ... error`,
  `device not accepting address`, `Cannot enable. Maybe the USB cable is bad`, `over-current condition`): `A USB device did not start: its cable or the device may be bad`
  (one toast for the kernel's retries);
- a USB sound card that is plugged in but opened no playback device after 5 seconds: `USB audio <name> gives no sound output`;
- a TF card pulled out while it was mounted (no Eject first): `TF2 card removed while in use: its last changes may be lost` (after Settings > Storage > Eject it is only a change);
- the battery at 5% or less, not charging (armed again above 8%): `Battery critically low: 5%. Plug in power or save now`.

A failure does not wait for a flash to end (it replaces it) and goes first in the queue; several changes at once show one after another, 3 seconds each; a full
queue (8) drops the oldest change, never a failure.

## The file

The daemon is the only writer of `/run/r3i6nos-hotplug/events` (root-owned, world-readable; `/run/loveos` belongs to `loveos.service`): one line per event,
`<nanoseconds>\t<info|error>\t<text>`, the last 40 kept, written whole (a temporary file renamed over it). The numbers only rise, so a reader keeps
the last it has seen. A reader that starts **does not replay** what is in the file: it is history.

## Showing it (every port)

- **Native (C, SDL2):** `theme/r3i6nos_toast.h` (`r3_toast_init`, `r3_toast_poll(ms)` every frame, `r3_toast_key` first on every key or button, `r3_toast_draw` after the
  screen is composed, `r3_toast_active` to keep the loop awake). Test: `tests/test_toast.c`.
- **LÖVE (and the menu):** `theme/r3i6nos_toast.lua` (`toast.new()`, `toast.poll(t, seconds)`, `toast.key(t)`, `toast.draw(t, screen)`). Test: `tests/test_toast.lua`.
- The colours are black on light green (a change) and white on red (a failure): both pass 4.5:1.
- The key that clears a failure is **consumed**: it must not also press a button in the app (so an error never makes a game jump). A change toast never
  consumes a key. In the IO Checkers the consumed key is not counted as a press in the input test.
- **Where it is wired now:** the menu, the IO Checker (LÖVE and native), Commander, TinyNTT. The other ports (Neumetik, KlamANSI, Crunchy, Chordica, Composer,
  Painter, Animator, NoCoGaMa) are to do (ROADMAP section 19). A third-party LÖVE game from the card has no toast: it is not an r3i6nos app.
- **Rec demo shows no toast, and a failure stops it** (`docs/DEMO-RECORDINGS.md`): the recording is the app's own frames, so no toast is in it and none is drawn
  while it records (the toast is *held*: `r3_toast_hold`, `toast.hold`; changes are dropped). A **failure** during a recording stops the tour at once and **closes the
  recording** (the files so far are kept, the log has a `FAIL hotplug failure: <text>` line for each and `end`, no video is made); the app then shows the failure(s)
  as error toasts, one at a time, each until a key, and says it in its reply line (`Rec demo failed. Stopped by a failure: ...`; over SSH, on stderr). The recorder
  does this itself (`r3_demo_failed` in `theme/r3i6nos_demo.h`, its own held toast), so a port needs only to call it each frame and stop. A critical battery or a
  card pulled out in use stops a recording the same way.

## The device report

`r3i6nos-report`'s sound section has `plugged headphones`, `plugged usb ...`, `plugged wifi ...`, `plugged power ...` lines and `hotplug watcher` (is the service active), so a report from the
unit tells whether the daemon's names match the unit. `r3i6nos-hotplug status` prints the same.

## Untested on the unit

The headphone switch (r3i6nos-sound already follows it), the USB interface classes and product strings, the power supplies' names and `online`, `/dev/kmsg`'s
wording on the 4.4.189 kernel, whether the codec's jack and a USB card announce themselves the way the tests assume, and how it looks on the panel.

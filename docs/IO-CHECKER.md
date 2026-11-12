# IO Checker

The R36S's hardware check, built into every image three ways: **LÖVE 11.5** and **LÖVE 12.0** (`apps/io-checker-love/`, one app packed
twice) and **native ARM64** (`apps/io-checker-native/io_checker.c`, C and SDL2). It checks what goes in (every button, both sticks, the D-pad,
the power and volume keys) and what comes out (the screen, the sound, the cards), **what the OTG port can bring (USB devices, video out, MIDI,
storage, Ethernet, a keyboard, a mouse, game controllers: the native build only so far)**, shows the system, and writes a device report for Claude.
It replaced the R36S Input Checker on 2026-11-09; its first version (2026-11-07) is what measured the pad's layout (`docs/hardware-findings.md`).

Three builds, because each answers for its own stack: LÖVE draws with OpenGL ES and plays through OpenAL; the native build draws with SDL's
renderer and plays through SDL's audio (ALSA underneath). A tone heard in one and not the other points at the stack, not the speaker.

## The screen

The r3i6nos-ansiapps theme (`docs/R3I6NOS-ANSIAPPS-THEME.md`): 64x24 cells, the top bar (`IO Checker`, and the runtime at the right), the
display view, the three-row command line (the reply, what A does, where you are). Every list follows the theme's keys: D-pad up and down
move, A opens, B goes back, L1/R1 turn a page, L2/R2 jump to the top or end. **Hold Select+Start for a second** to go back to the list from
any page, or to leave from the list. A keyboard works too (arrows, Enter, Esc; H and N for heard and not heard).

**Two departures from the theme, and why:**

- **The video patterns are pixels, not cells.** A test of the panel has to light every pixel in a known colour; the text grid can't. They
  fill the whole screen with no top bar or command line, and B (or A) comes back to the themed list.
- **The motion pattern changes every frame**, against the theme's "nothing changes more than twice a second". Measuring the frame rate is
  its job, and it only runs while chosen.

## The pages

| Page | What it does |
|---|---|
| **Buttons and sticks** | Every raw button (numbered from 1, as LÖVE counts), both sticks as a dot in a box (the box lights while the stick is pressed in), the D-pad (four buttons on the R36S, or a hat on a pad that has one), and the power and volume keys, which arrive as keyboard keys. The **Raw input** box: buttons held now, the hat, each axis now with the lowest and highest it has reached, how many buttons have been pressed and **which never have**, the keys seen, chatter, whether SDL has a gamepad mapping, and the last six events (each says whether the pad reports the button held at that moment, so an event stream that disagrees with the state shows). No button navigates on this page; Select+Start held, or 15 s with no input (counted down in the last 10), goes back. |
| **Video** | Nine full-screen patterns, chosen in a list (left/right step through them while one shows): colour bars and the 16 VGA colours; white, red, green, blue and black (stuck pixels, tint, backlight bleed); grey, red, green and blue ramps (steps mean fewer colour bits); a 1-pixel checker, a 10-pixel grid, the edge lines and red corners (scaling, overscan); a moving bar with a row of boxes lit one a frame (**measures the frame rate**: frames, mean and worst frame time, frames over 1.5 times the mean). The list's lower box shows the mode, the renderer, the backlight, the measured rate and what to look for in the selected pattern. |
| **Sound** | Six tones, each faded in and out: 1 kHz on the left channel, the right, both; 440 Hz on both (the low one); a sweep from 100 Hz to 8 kHz; 1 kHz at 10%. They were 440 Hz until the first native report from the unit (2026-11-09) had every 440 Hz tone silent from the speaker and the sweep heard (`docs/hardware-findings.md`). A plays the selected one, **X marks it heard, Y silent**; the answers go in the report. The lower box shows the card, the RK817's speaker path from `r3i6nos-sound status` (in red if `OFF`), the headphone jack and the output device. |
| **Files** | On the games card (`/roms`) and, when one is mounted, the data card (`/roms2`), in their `IO_Checker` folder: write a file, write a second and rename it over the first, read it back, delete it; then write 8 MB with sync and read it back byte for byte, and the free space. The read speed is of a file just written, so it may come from memory; it says so. |
| **USB and OTG** (native build) | What the OTG port can bring, from `/sys`, `/proc` and `/dev` (`apps/io-checker-native/usb_io.h`; nothing needs root). A list of eight checks, each with what it found beside it; the page behind each rescans every 2 s, so a device shows up as it is plugged in. **Devices:** every USB device (a hub or dock lists the things behind it), what it is (keyboard, mouse, pad, storage, MIDI, audio, network, Wi-Fi, camera, serial, Bluetooth, hub: from its interface classes and the driver bound to each), its vendor:product, speed, drivers; one with **no driver** says `NO DRIVER` (a FAIL in the report: the kernel cannot use it). **Video out:** every video connector the kernel has (`/sys/class/drm`): this unit's own screen, an HDMI or DisplayLink dock when connected, with its first mode, and how many displays SDL sees. **MIDI:** the ALSA raw MIDI ports; the selected one is opened, what comes in is shown in words (`ch1 note on C4 vel 100`), and **A sends middle C** (note on, then off 300 ms later): a cable from the out to the in loops it back. **Storage:** USB drives (model, size, whether mounted); **A tests a mounted one like a card** (write, rename, read, delete, 8 MB speed; its rows also appear on Files and in the report). **Network:** Ethernet and Wi-Fi adapters, link, speed, address, bytes. **Keyboard:** each different key counts once, the last six are shown, modifiers live; every key is a test here, so **Esc, or B on the pad, goes back**. **Mouse:** movement, which buttons were pressed, the wheel. **Game controllers:** each pad other than the built-in one, opened while the page is up: shape, buttons held now, how many of them have been pressed, the axes; its events never reach the built-in pad's record on Buttons and sticks. |
| **System** | The r3i6nos build, kernel, board, CPU, memory, battery, backlight, the runtime, screen and renderer, the pad's name, GUID and shape, whether SDL maps it, the sound card and speaker path. |
| **Device report** | Runs the file checks again and writes everything, below. |

## The device report

`EASYROMS/IO_Checker/r36s-report-<build>.txt`: `r36s-report-11.5.txt`, `r36s-report-12.0.txt` or `r36s-report-native.txt`, one a build
so they never overwrite each other. The grammar is `docs/DEVICE-REPORTS.md`'s. The native build also writes it without a window,
`io-checker-native --report`, for SSH (nothing pressed, shown or played, so those lines are SKIP).

| Section | Checks (PASS or FAIL) | Facts (INFO) and SKIPs |
|---|---|---|
| system | | the System page's rows; the native build adds SDL's drivers and output devices |
| input | the pad's shape (17 buttons, 4 axes); **every button pressed** (or which never were); **no chatter** (a press within 30 ms of the same button's release); **each axis's range** past -0.90 and +0.90; **each axis at rest** within 0.10 when the report runs (hands off); the power and volume keys arriving | presses a button, keys seen, hat directions; SKIP for what was never tried |
| video | the window is 640x480; **the motion test** at 55 fps or more with under 1% late frames | mode, renderer, the patterns shown, the backlight; how they looked is for the person to say in words |
| sound | the speaker path is not `OFF`; the output opened; **each tone heard** (FAIL if marked silent) | the card, the jack; SKIP for a tone not played or not answered |
| files | write, rename over, read, delete; read back every byte; the same for each **mounted USB drive** ("USB sda") | where, write and read speed, free space |
| usb devices | **a device with no driver** (FAIL: the kernel cannot use it; a hub is not one) | every device: product, vendor:product, speed, what it is, drivers; SKIP when nothing is plugged in |
| usb video out | **an external display connected** (PASS, with its first mode) | every connector (this unit's screen, HDMI, a USB adapter) and SDL's display count; SKIP when none is connected |
| usb midi | **each port opens** (FAIL with the reason, e.g. permission denied); **messages** when the MIDI page has been used | the ports and their cards; SKIP when none |
| usb storage | **a USB drive is mounted** (FAIL when it is plugged in and not: nothing mounts USB storage for apps yet, ROADMAP section 24) | model and size; SKIP when none |
| usb network | **an Ethernet adapter has a link** (PASS) | Wi-Fi adapters, no link, traffic and address |
| usb keyboard, mouse, pads | **keys, mouse events, a controller's buttons arrived** (PASS) when the page has been used | the devices; SKIP for what was not tried, and when nothing is plugged in |

## Power (CLAUDE.md rule 14; `docs/POWER.md`)

The native build's Device report has the `== power ==` section: **idle** (the list, as the main loop draws it, nothing pressed) and **working** (the
Motion pattern, a frame every vsync). The main loop used to compose a frame and sleep 16 ms, 60 times a second whatever changed; measured on a
laptop (Debian 12, dummy video driver, not a unit) that was **4.7% of a core and 104 wake-ups a second** idle. It now sleeps **100 ms** a pass on a
quiet page and keeps the 16 ms frame only while something is changing (a screen still being presented, a toast, the exit chord, a job about to
run) or on a page where a person is pressing things (the button test, the live USB checks): **0.00% and 18 wake-ups a second**, six runs out of six.
Not `SDL_WaitEventTimeout`: on a video driver with no native wait SDL turns it into a 1 ms polling loop (`docs/POWER.md`). A press on a quiet page
waits at most 100 ms. The sound output is also **closed when a tone has played out** (it stayed open, and SDL's audio thread woke about 40 times a
second feeding it silence, until the app quit). Not yet measured on a unit.

## Leaving

From the list: B, Quit, or Select+Start held for a second. Esc on a keyboard. Nobody can be trapped: the input page goes back by itself
after 15 s with no input, and the list needs only B.

## Tested here, and not

- **Tested on a Mac:** the logic, every page's screen as text, the probes and the file checks against temporary folders, and the whole LÖVE
  app driven through every page under a fake LÖVE and a fake pad, down to the report's file (`tests/test_io_checker.lua`); the native
  build's `--selftest` (the same logic, every page, the files check and report in a temporary folder, the navigation through its real
  event handler), its `--report` and its page screenshots.
- **The USB and OTG checks** are tested against fake `/sys`, `/proc` and `/dev` trees (seven devices of every kind, three video connectors,
  a mounted stick, two adapters, a MIDI node that loops back, three input devices): every parser, every page's rows, the live keyboard, mouse and
  pad events, the MIDI send and read, the report's sections, and nothing plugged in (everything SKIPs, nothing FAILs). Also run against a
  Debian 12 container's real `/sys` and `/proc` (it found the VM's virtual USB keyboard and digitizer and classified them), under GCC with `-Werror`,
  AddressSanitizer and UBSan. **No real USB device has been plugged into a unit**: whether the kernel on the unit has the drivers (USB MIDI,
  Ethernet, storage, a DisplayLink adapter), whether the OTG port can host and power a device, what `/sys/class/drm` shows, and whether the
  app's user may open the MIDI nodes are all untested. **The two LÖVE builds have no USB pages yet.**
- **Not run on a real R36S:** none of it. Not under a real LÖVE either (`scripts/preview_io_checker.sh` draws it on a Mac with a desktop;
  the session that wrote it had none). Untested: that the tones are heard, the patterns' look on the panel, the measured frame rate, the
  backlight reading, the jack reading, and whether the power and volume keys reach apps at all.

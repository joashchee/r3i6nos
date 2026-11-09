# IO Checker

The R36S's hardware check, built into every image three ways: **LÖVE 11.5** and **LÖVE 12.0** (`apps/io-checker-love/`, one app packed
twice) and **native ARM64** (`apps/io-checker-native/io_checker.c`, C and SDL2). It checks what goes in (every button, both sticks, the D-pad,
the power and volume keys) and what comes out (the screen, the sound, the cards), shows the system, and writes a device report for Claude.
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
| **Sound** | Five tones, each faded in and out: 440 Hz on the left channel, the right, both; a sweep from 100 Hz to 8 kHz; 440 Hz at 10%. A plays the selected one, **X marks it heard, Y silent**; the answers go in the report. The lower box shows the card, the RK817's speaker path from `r3i6nos-sound status` (in red if `OFF`), the headphone jack and the output device. |
| **Files** | On the games card (`/roms`) and, when one is mounted, the data card (`/roms2`), in their `IO_Checker` folder: write a file, write a second and rename it over the first, read it back, delete it; then write 8 MB with sync and read it back byte for byte, and the free space. The read speed is of a file just written, so it may come from memory; it says so. |
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
| files | write, rename over, read, delete; read back every byte | where, write and read speed, free space |

## Leaving

From the list: B, Quit, or Select+Start held for a second. Esc on a keyboard. Nobody can be trapped: the input page goes back by itself
after 15 s with no input, and the list needs only B.

## Tested here, and not

- **Tested on a Mac:** the logic, every page's screen as text, the probes and the file checks against temporary folders, and the whole LÖVE
  app driven through every page under a fake LÖVE and a fake pad, down to the report's file (`tests/test_io_checker.lua`); the native
  build's `--selftest` (the same logic, every page, the files check and report in a temporary folder, the navigation through its real
  event handler), its `--report` and its page screenshots.
- **Not run on a real R36S:** none of it. Not under a real LÖVE either (`scripts/preview_io_checker.sh` draws it on a Mac with a desktop;
  the session that wrote it had none). Untested: that the tones are heard, the patterns' look on the panel, the measured frame rate, the
  backlight reading, the jack reading, and whether the power and volume keys reach apps at all.

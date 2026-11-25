# IO Checker

The R36S's hardware check, built three ways (**only the native build is in the image since 2026-11-18**; the LÖVE builds are for a card or a Mac): **LÖVE 11.5** and **LÖVE 12.0** (`apps/io-checker-love/`, one app packed
twice) and **native ARM64** (`apps/io-checker-native/io_checker.c`, C and SDL2). It checks what goes in (every button, both sticks, the D-pad,
the power and volume keys) and what comes out (the screen, the sound, the cards), **what the OTG port can bring (USB devices, video out, MIDI,
storage, Ethernet, a keyboard, a mouse, game controllers: the native build only so far)**, shows the system, and writes a device report for Claude.
It replaced the R36S Input Checker on 2026-11-09; its first version (2026-11-07) is what measured the pad's layout (`docs/hardware-findings.md`).

Three builds, because each answers for its own stack: LÖVE draws with OpenGL ES and plays through OpenAL; the native build draws with SDL's
renderer and plays through SDL's audio (ALSA underneath). A tone heard in one and not the other points at the stack, not the speaker.

## The screen

The r3i6nos-ansiapps theme (`docs/R3I6NOS-ANSIAPPS-THEME.md`): 64x24 cells, the top bar (`IO Checker`, and the runtime at the right), the
display view, the three-row command line (the reply, what A does, where you are). **The native build follows the input mapping**
(`docs/r3i6nos-mapping.md`, rule 17; since 2026-11-20): every list uses the mapping's keys, D-pad or right stick up and down move,
A opens, B goes back, **R1 held** with up/down pages and with left/right goes to the top or end (L1, R1, L2 and R2 alone no longer page or jump).
**L1 + R1 opens the main menu** (below) on every list page. **On the two test pages (Buttons and sticks, R3i6nos Mapping) every input is a test,
so nothing navigates and no chord opens a menu: hold Select+Start for a second to go back to the list.** Select+Start no longer leaves the
app: Quit in the main menu does (B on the list still quits too). A keyboard works too (arrows, Enter, Esc; H and N for heard and not heard; **F10 is the main menu**).

**What the app does not have, and says so on screen when asked** (rule 17 allows an app to differ for its function): no virtual mouse (L3) and no
virtual keyboard (R3); no contextual menu (L2 + R2); **no panels except on R3i6nos Mapping** (Fn there turns its two panels: Fn the next, L1 + Fn
the first, R1 + Fn the one before). Sound's **X (heard) and Y (silent)** are the app's own one-touch buttons, as the mapping allows. The LÖVE
builds are **not** moved onto the mapping yet (the Lua theme has no reader or main menu).

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
| **R3i6nos Mapping** (native build, 2026-11-20) | Tests the input mapping on the unit (`docs/r3i6nos-mapping.md`). **Panel 1, inputs:** all 17 default inputs (B, A, X, Y, L1, R1, L2, R2, the D-pad, Select, Start, L3, R3, Fn) with **the action each would make right now** (hold R1 and Up reads "Page up"; hold L1 and R1 reads "Main menu"), a count of presses, and the row lit while held; below, the **system chords** (L1+R1 main menu, L2+R2 context menu, Fn next panel, L1+Fn first panel, R1+Fn previous panel) and **R1 as "increase"** (R1+Up Page up, R1+Down Page down, R1+Left Home, R1+Right End), each marked "seen" once made, and the last action. The chords are recorded here, not obeyed. **Panel 2, sticks and speed (the speed of the R1 modifier):** R1 held or not; the **right stick** (selection): its push, the steps a second it asks for against the 6 at full, the steps it really made in the last second and the most in one, and the counts of Up, Down, Page up/down, Home, End it made (with R1 held it pages and Home/End fire once a push); the **left stick** (pointer): a pointer in a 380x100 px test area, its speed now in px/s smoothed over 0.4 s, the highest **plain** and the highest **with R1** and their ratio (the mapping says 120 px/s at full and 3 times that with R1: 360), and how long the area takes to cross at each speed. Fn turns the two panels (L1+Fn the first, R1+Fn the one before). Select+Start held a second, or 15 s with no input, goes back. |
| **ANSIapps Mono** (native build, 2026-11-24; twelve panels since 2026-11-25) | A screen test of the 10x20 ANSIapps Mono font (`docs/ANSIAPPS-MONO-FONT.md`), between System and Device report. Twelve panels, Fn next and R1+Fn previous: text mode (CP437, lines, blocks, Braille, octants, accents, symbols, Powerline, and what the font does not have yet); the gamepad drawn live in the state the pad is in, then every state; the on-screen keyboard (A types); a piano (A turns a note on or off, ring = off, hole in the filled key = on, black keys as pairs above); UI widgets (bar, slider, spinner, ring, switches, knobs); music notation; two panels of two-cell icons, with a right and a wrong way to print one; then **Wide and tall** (every glyph that has a `.wide` and a `.tall` counterpart, the three sizes side by side, in pages), **Named glyphs** (all 2,219 in code point order, three size sets and 17 groups, a cursor shows the name, code point and size), **Code points** (all 4,583, six pages) and the **Portrait maker** (a portrait of 8 x 4 cells built from the font's 692 parts, A a new one, Y the other layout, beside the checks of the parts). The checks (`apps/io-checker-native/font_check.h`) also write a `== font ==` section of the Device report and run in `--selftest`: every glyph and cell has a bitmap; all 113 wide and 28 tall are the size of their base; all 692 portrait parts are in the font; 259 right parts mirror their left; all 1,283 listed joins hold on the 10x20 bitmaps; no pair of neighbouring kinds lost a join; every seam of 84 built portraits joins. Untested on a unit (the checks read tables, not the screen). |
| **System** | The r3i6nos build, kernel, board, CPU, memory, battery, backlight, the runtime, screen and renderer, the pad's name, GUID and shape, whether SDL maps it, the sound card and speaker path. |
| **Device report** | Runs the file checks again and writes everything, below. |

## The main menu (rule 17; native build, 2026-11-20)

**L1 + R1** (keyboard: F10) on any list page opens the main menu, from `theme/r3i6nos_mainmenu.h`, in the rule's order:

| Item | Does here |
|---|---|
| **About** | Name, `v<version> <commit> <date>` (the commit comes from `scripts/build_native_app.sh`; `dev` when built by hand), the font credit (ANSIapps Mono, SIL OFL 1.1), MIT, no network or telemetry. Any key closes. |
| **Colour theme** | The theme picker (VGA Classic by default, the 13 bundled themes; moving shows the theme at once, A keeps it, B goes back). **The choice is saved** in `<app data folder>/settings.conf` (`theme=<name>`) and read at the next start. The video test patterns are pixels and do not change. |
| **Device report** | The device report below. |
| **Rec demo** | **Not built yet:** the item is there (rule 17: never missing), and answers "Rec demo is not built for IO Checker yet." (rule 11 is open for this app; ROADMAP section 39.) |
| **Benchmark** | The Motion pattern for 4 s (the frame rate against the 55 fps goal, under 1% late frames), then the card tests (write, read and rename speeds); B cancels. Writes **`<app data folder>/benchmark-native.txt`** in the report grammar and shows the summary on the command line. |
| **Quit** | Leaves the app. |

## The device report

`<app data folder>/r36s-report-<build>.txt` (rule 16, `docs/APP-DATA.md`: `IO_Checker/` on the data card when a valid one is in, else `EASYROMS/IO_Checker/`; the **native** build asks to move its data at the first start after a data card is put in; the LÖVE builds still write to the games card until they call `appdata.init`): `r36s-report-11.5.txt`, `r36s-report-12.0.txt` or `r36s-report-native.txt`, one a build
so they never overwrite each other. The grammar is `docs/DEVICE-REPORTS.md`'s. The native build also writes it without a window,
`io-checker-native --report`, for SSH (nothing pressed, shown or played, so those lines are SKIP, and no question is ever asked). **App data** (rule 16) is only the report; the Files checks make their own `IO_Checker` folder on each card they test and leave nothing.

| Section | Checks (PASS or FAIL) | Facts (INFO) and SKIPs |
|---|---|---|
| system | | the System page's rows; the native build adds SDL's drivers and output devices |
| mapping (native) | **each chord and R1 move made** on R3i6nos Mapping (PASS), **the right stick's steps** at a full push (PASS at 4-7 in the busiest second, 6 expected) | the highest step rate asked for, the highest pointer speed plain and with R1, and R1's gain (INFO); **SKIP for what was not tried**, with how to try it |
| input | the pad's shape (17 buttons, 4 axes); **every button pressed** (or which never were); **no chatter** (a press within 30 ms of the same button's release); **each axis's range** past -0.90 and +0.90; **each axis at rest** within 0.10 when the report runs (hands off); the power and volume keys arriving | presses a button, keys seen, hat directions; SKIP for what was never tried |
| video | the window is 640x480; **the motion test** at 55 fps or more with under 1% late frames | mode, renderer, the patterns shown, the backlight; how they looked is for the person to say in words |
| sound | the speaker path is not `OFF`; the output opened; **each tone heard** (FAIL if marked silent) | the card, the jack; SKIP for a tone not played or not answered |
| files | write, rename over, read, delete; read back every byte; the same for each **mounted USB drive** ("USB sda") | where, write and read speed, free space |
| usb devices | **a device with no driver** (FAIL: the kernel cannot use it; a hub is not one) | every device: product, vendor:product, speed, what it is, drivers; SKIP when nothing is plugged in |
| usb video out | **an external display connected** (PASS, with its first mode) | every connector (this unit's screen, HDMI, a USB adapter) and SDL's display count; SKIP when none is connected |
| usb displaylink | the four things USB video (a DisplayLink adapter) needs, in order: **udl loaded** (a module in `/proc/modules`, built in, or its USB driver registered); **udl module file** (`udl.ko` under `/lib/modules/<release>`, or listed in `modules.builtin`); **kernel config DRM_UDL** (`CONFIG_DRM_UDL=y` or `m`, read from `/proc/config.gz`, `/boot/config-<release>` or the build tree's `.config`; `FB_UDL`, the older driver, as INFO); **drm device** (a DRM card behind USB, bound to `udl`, which plugging the adapter in should create). With no adapter plugged in (USB vendor `17e9`) a missing piece is INFO, never FAIL, and "loaded" and "drm device" are SKIP; with one plugged in a missing piece is a FAIL. `udl verdict` says what to do next (`docs/DISPLAYLINK-ADAPTERS.md`) |
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

From the list: B, or Quit in the main menu (the native build; **Select+Start held no longer leaves the app**, it only goes back from the two test pages). Esc on a keyboard.
Nobody can be trapped: the two test pages go back by themselves after 15 s with no input, and the list needs only B.

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
- **The input mapping and the main menu** (native build): `tests/test_input.c` (the reader: every plain input, the modifiers, each chord in both orders, R1 held on the D-pad and the right stick, the dead zone, 6 steps a second at full, the pointer's 120 and 360 px/s, a stalled frame not bursting, the list rule, the menu's order with and without Benchmark, every theme's contrast on the menu, About and picker) and the app's `--selftest` (the home list on the new order, every one of the 17 buttons on the mapping page, every chord and R1 move through the real event handler, the panels and their history, the stick's measured step rate and the pointer's measured speeds against 120 and 360, the report's mapping section, the menu's items and the theme saved and read back, the benchmark's start and cancel, Select+Start not leaving). **Not run on a unit:** the real pad's buttons 14-16 (L3, R3, Fn) and axes 2-3 are from the measured mapping and have not been checked in this app; the 6 steps a second, 120 px/s and the factor 3 are guesses to try (`R3_STICK_RATE`, `R3_POINTER_PPS`, `R3_R1_POINTER_X` in `theme/r3i6nos_input.h`); the benchmark's whole run (it needs the window's vsync) and the saved theme on a card are untried.
- **Not run on a real R36S:** none of it. Not under a real LÖVE either (`scripts/preview_io_checker.sh` draws it on a Mac with a desktop;
  the session that wrote it had none). Untested: that the tones are heard, the patterns' look on the panel, the measured frame rate, the
  backlight reading, the jack reading, and whether the power and volume keys reach apps at all.

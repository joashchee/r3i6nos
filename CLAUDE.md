# r3i6nos

Dedicated firmware for the R36S handheld (Rockchip RK3326): it boots to a
menu and runs only LÖVE games (11.5 stable, 12.0 nightly) and native
ARM64 Debian apps. No emulators. The spec is
**`docs/r36s_love_only_firmware_specification.md`**; read it before any
architectural change. This file is the condensed operating rules only.

**`CHANGELOG.md`**: add a bullet whenever a change lands.
**`ROADMAP.md`**: tick items in the same commit as the work, and say what
is blocked and on what.

## Layout

- `overlay/`: files copied onto the image's root filesystem as-is
  (`usr/local/bin/`, `etc/systemd/system/`). Paths mirror the device.
- `menu/`: the boot menu, a LÖVE 11.5 app installed at `/opt/loveos/menu`; its Settings has Network and SSH (a root helper, `overlay/usr/local/bin/r3i6nos-net`, does the work) Battery (`docs/BATTERY.md`; its top bar shows the charge) Sound mixer (`docs/SOUND.md`: Volume and Boost from `/etc/asound.conf`, the codec, a test sound) Data card (`docs/STORAGE.md`: format the second-slot card as exFAT or ext4 and open it; the root helper `r3i6nos-format`) Date and time (`docs/CLOCK.md`; asked on a card's first boot, set by the root helper `r3i6nos-clock`, kept across restarts) Device reports (`menu/reports.lua`, rule 9) and About (`docs/ABOUT.md`: the build, the unit, Check for updates).
- `apps/`: the apps (each has an `app.conf`, native or LÖVE, and its own workflow, `.github/workflows/app-<name>.yml`; only `DEFAULT=1` apps enter the image). The default apps built into the image (`/opt/loveos/defaults`): the native ARM64 IO Checker (`docs/IO-CHECKER.md`: input, video, sound, files, USB and OTG, a device report; its LÖVE 11.5 and 12.0 builds are no longer in the image, since 2026-11-18, only native `.deb` apps are), PortMaster (for testing, below), and Commander (a native ARM64 file manager whose viewer identifies and opens about 150 formats: pictures, ANSI art, archives, packages, disk images, documents, and plays Rec demo recordings, WAV and, with ffmpeg, video; `apps/README.md`). See `apps/README.md`.
- `theme/`: the r3i6nos-ansiapps text-mode theme for LÖVE (`.lua`) and C/SDL2 (`.h`): font, screen, list dialogs; installed at `/opt/loveos/theme`. See `docs/R3I6NOS-ANSIAPPS-THEME.md`.
- `tools/ffmpeg-min/`: the minimal GPL ffmpeg (one 5.3 MiB binary: the decoders Commander's Play tab and Diskette's audio and video lists need, WebP/TIFF/PSD stills, and Rec demo's H.264 + AAC encode; `scripts/build-ffmpeg-min.sh` builds and checks it; `configure_rootfs.sh` installs it at `/usr/bin/ffmpeg` with its GPL terms; ROADMAP section 20).
- `/usr/bin/nttplay` (built by `scripts/build-nttplay.sh` from the Neumetik repository, installed by `configure_rootfs.sh`, always in the image; ROADMAP section 27): Neumetik's players as a command-line tool, `nttplay probe FILE` and `nttplay decode FILE` (16-bit stereo PCM on stdout, like ffmpeg's pipe), for Commander and Hydrolik. **nttplay is the only decoder of the ansiapps formats, and there is one copy of it (rule 15).**
- `tools/runtimes/` (`docs/RUNTIMES.md`, ROADMAP section 33): the game-engine runtimes Pyxel, Ren'Py and Solarus (PortMaster's, pre-extracted, no mount, no root), each a manifest and a command; `scripts/build-runtimes.sh` makes `build/runtimes/r3i6nos-runtime-<name>.deb` (tool packages for `native_deb/` or `scripts/push-packages.sh`, not in the image), the menu lists games in a card's `Pyxel/`, `Renpy/` and `Solarus/` folders, `samples/` has one game each. **Built and tested in Docker; untested on a unit.** Third-party engines: rules 8-14 and 16 are for the makers that will be built on them.
- `apps/musicue-native/` (`docs/MUSICUE.md`, ROADMAP section 36): **MusiCue**, the R36S port of the maintainer's web app Cue, a native C and SDL2 app that is a user-installed package (`DEFAULT=0`): the whole song as one waveform with a moving playhead (pixels, rows 1-6), the rest in the text-mode theme in true colour; Cue's own files, instant jumps and gapless A/B loops from a song held in RAM, and `musicue-sync` (Python, fetches Cue's server cue files and, with the yt-dlp package, Creative Commons audio). **Built and tested in Docker and on a Mac; untested on a unit; its device report, Rec demo and power section are still open.**
- `tools/streamtools/` (`docs/STREAM-TOOLS.md`, ROADMAP section 34): yt-dlp, gallery-dl and streamlink as tool packages, the backends a native or LÖVE app runs for web video, pictures and live streams (yt-dlp with a private QuickJS-ng for YouTube's challenges; the image's ffmpeg-min joins streams); `scripts/build-streamtools.sh` makes `build/streamtools/r3i6nos-{ytdlp,gallery-dl,streamlink}.deb` (reproducible, every source hash-pinned, not in the image), updated through the catalog since yt-dlp goes stale in weeks. **Built and tested in Docker plus one real YouTube request; untested on a unit.** Rule 15's one-copy idea applies: apps are frontends, none bundles its own.
- `tools/qjs-node/`: the shims and builder that turn a Node-style CLI into one QuickJS executable (`docs/QJS-CLI-PORTS.md`); an app's folder is only ever read, never copied in.
- `corpus/`: the test corpus, one file of each format the apps read (Commander's ~130 and the other apps'), sorted into sub-folders (music, audio, images, art, video, documents, text, data, archives, disks, programs, fonts, models, apps; `corpus/README.md`), installed at `/opt/loveos/test-files` and copied to EASYROMS by `flash-macos.sh`; `scripts/build_corpus.sh` remakes it.
- `scripts/`: host-side tools. `check.sh` runs everything CI runs.
- `tests/`: Python (`unittest`) and Lua tests; no hardware needed.
- `vendor/`, `build/`, `local_hardware_assets/`: fetched, built or captured, gitignored.
- `docs/DEVICE-REPORTS.md`: the device report every R36S port has (rule 9) and its file format.
- `docs/POWER.md`: the power section every device report has (rule 14), and what the background work (the hot-plug daemon, the toast poll, the periodic jobs) costs the battery, and the `== power ==` section of `r3i6nos-report` that measures it on the unit.
- `docs/SCREEN-MIRROR.md`: the unit's screen on a computer or TV over a wired link (changed text cells to a web page, not VNC); the `== usb gadget ==` and `== usb ethernet ==` sections of `r3i6nos-report` answer what the OTG port can do (ROADMAP section 28).
- `docs/HOTPLUG.md`: the toast over the top bar when headphones, a USB device or DC power is plugged in or out (rule 13); `r3i6nos-hotplug` writes the events, `theme/r3i6nos_toast.{h,lua}` show them.
- `docs/DEMO-RECORDINGS.md`: Rec demo, the screenshots and video every R36S port records to a `Demo` folder (rule 11).
- `docs/TWO-SLOT-SETUP.md`: the console prepares its own card from the original (`overlay/usr/local/bin/r3i6nos-setup`, a no-op on every normal boot; the web guide at r3i6nos.ansiapps.com/bootdisk is in the ansiapps-site repo); trials T1-T10 are untested on a unit.
- `docs/STORAGE.md`: the two card slots: the system card (TF1, `/roms`) and the data card (TF2, `/roms2`), mounted by role by `r3i6nos-cards`.
- `docs/APP-DATA.md`: where an app keeps its data (rule 16): the data card first, the move and merge offers, the one engine `r3i6nos-appdata`.
- `docs/PACKAGES-OTA.md`: adding and upgrading `.deb` bundles over the network or SSH with no card out: `r3i6nos-pkg` (install, install-love, check, get, update), `scripts/push-packages.sh`, the signed catalog (`scripts/publish-packages.sh`, `scripts/catalog.py`) and system packages with rollback (the root helper `r3i6nos-sysupdate`, `scripts/build-system-package.sh`) are built (ROADMAP section 29 phases 1, 2, 3 and 5; the signing key is the maintainer's, made once with `publish-packages.sh keygen`); Settings > Packages, Roll back and Move are still proposed.
- `docs/BRAND.md`: the logo (the butterfly "3i6", grey "r" and "n", no "os"); the boot logo is `docs/brand/boot-logo.png`, made into the BMP by `scripts/make_boot_logo.py` and put on BOOT by `inject-boot.sh`. The art is the maintainer's: never redraw it.
- `apps/portmaster/` and `docs/PORTMASTER.md`: PortMaster (third-party, MIT) packed as a default app for testing (`scripts/build-portmaster.sh`); it keeps itself and its ports on `/roms/ports`, runs as the login user, installed ports are menu entries of type `port`; rules 8-14 and 16 are for apps made for r3i6nos and do not apply to it; **untested on a unit**.
- `docs/hardware-findings.md`: what the stock card showed (kernel 4.4.189, no panfrost, boot flow). Read it before touching boot or graphics.

## Rules

1. **Say what was not run on a real R36S.** The image and its apps have
   run on a real unit since 2026-11-09 (device reports in each repo, the roadmap's sections 8-17), but a claim about behaviour on the
   device is "untested" until a report or the maintainer says otherwise (ROADMAP section 2). The gamepad mapping is measured
   (memory: R36S controller mapping), not assumed.
2. **No copyrighted or proprietary content in the repo, in CI artifacts or in a release:** no ROMs, BIOS files,
   or proprietary emulator assets. **The Mali GPU blob (`libmali`), kernel modules,
   bootloader and boot files come from the user's own card at flash time
   (`capture-card.sh`, `inject-boot.sh`, `inject-rootfs.sh`) and live only in
   the gitignored `local_hardware_assets/` and on that user's cards.** Record the licence and source of every
   vendored base, kernel or runtime.
3. **Only RK3326 R36S boards.** Allwinner and other clones (spec section 1)
   must never get this image; keep the identification steps in the flashing docs.
4. **Overlay scripts stay small and dependency-light** (bash, Python 3
   standard library): they run at boot on 1 GB of RAM. Path overrides go
   through `R3I6NOS_*` environment variables so tests can run on a laptop.
5. **The menu never scrolls:** whole pages, `Page n/m` in the foot.
6. **No telemetry, no network at boot, no accounts.** Settings > Network and SSH (`docs/NETWORK.md`) and About's Check for updates (`docs/ABOUT.md`) only act when a person chooses them; SSH is off and has no shared host keys in the image.
7. **Keep the user's data safe:** the games card (`EASYROMS`) is theirs. Boot
   scripts only read it; never format, repartition or delete from it. **One exception (2026-11-11, `docs/TWO-SLOT-SETUP.md`):** the setup service may add and
   format the games partition on the card it is setting up, only when that card has no partition 3 and the space after ROOTFS is empty; never on any other card. **Second exception (2026-11-16, `docs/STORAGE.md`, "Format the data card"):** Settings > Data card may erase, partition and format the card in the second slot, only when a person chose it and confirmed (the dialog starts on No); the root helper `r3i6nos-format` never touches the boot card and refuses when it cannot tell which card that is. **Third exception (2026-11-17, `docs/APP-DATA.md`):** `r3i6nos-appdata` may delete files from `<App>/` on the system card, only after a person chose Move all or Merge in the app's dialog and only after each file's copy is verified on the data card; never anything outside `<App>/`. **Fourth (2026-11-17, `docs/PACKAGES-OTA.md`):** `r3i6nos-pkg install` may write `native_deb/<name>.deb` (and its `.prev`) on a card and `remove` may delete them, only when a person ran that command; the boot-time watcher still only reads.
8. **Every app made for r3i6nos starts from the r3i6nos-ansiapps theme** (`theme/`, `docs/R3I6NOS-ANSIAPPS-THEME.md`): the IBM VGA
   8x16 font scaled to 10x20, **64 columns by 24 rows** on the 640x480 screen, row 0 the reserved top bar, rows 1-20 the display view, rows 21-23
   the three-row command line (reply, prompt, status), list dialogs and their button rules as KlamANSI does them, only the 16 VGA colours in
   pairs that pass 4.5:1 (**a native app may offer true-colour themes instead, opt-in, as Commander does since 2026-11-16: slots 0-15 keep
   their roles, every pair of every theme still passes 4.5:1, VGA Classic stays the default; `docs/R3I6NOS-ANSIAPPS-THEME.md`, "True colour"**), nothing scrolls. New apps, LÖVE or native, copy the menu's structure; an app that cannot follow it says why in its README.
   The font is CC BY-SA 4.0: keep `NOTICE`, `theme/font/` (licence, credit, changes) and the headers of the generated font files intact.
9. **Every R36S port has a device report** (`docs/DEVICE-REPORTS.md`): one action on the unit (an app's Start menu item
   **Device report** and a `--report` flag; a command-line tool's `<command>-report`, which Settings > Device reports starts)
   that checks and measures what only the R36S can answer (the system, the pad's GUID, sound, the games card and its filesystem,
   speed against the app's goal, peak memory, every "untested on the unit" item a program can answer) and writes
   **`<App data folder>/r36s-report.txt`** (`$R3I6NOS_APP_DATA`, rule 16; `/roms/<App>/` or `/roms2/<App>/`) for Claude to read: `STATUS key: value` lines (PASS, FAIL, INFO, SKIP), sections, a summary,
   and `end` last. **A report that needs a setup (a charger out, an adapter or a cable in, headphones) lists it first on a screen and waits for a keypress** (A runs, B cancels; `docs/DEVICE-REPORTS.md`, "Setup the report needs"). **One that must be run several times counts them ("run 2 of 3": a `# RUNS: n` header, a file for each run, never one replacing another)**. Native ports use `theme/r3i6nos_report.h`; tools get `tools/qjs-node/report.py` from `package-tool.sh`. A new port
   is not done until its report exists and is tested; when a report comes back, update the port and both roadmaps from it.
10. **Every R36S port has a local build script** that makes its card file on this Mac with one command and no arguments: a native or
   tool port's **`r36s/build-deb.sh`** in its own repo (Docker only; it builds in Debian 12 arm64 and runs the package's self-test;
   `--clean-room` also installs it with `deb_watcher.py` in a clean Debian 12), a LÖVE port's `scripts/build.sh`. It fetches what it
   needs (a pinned tarball, checked by hash) or says where to put it, and leaves the file where the `PORTS` list (`scripts/ports.conf`, read by
   `flash-macos.sh` and **`scripts/build-ports.sh`**, which refreshes every port's build in one command) looks for it; that list names the script as
   the port's build command. A new port is not done until its script has been run and its line is in `scripts/ports.conf`.
11. **Every R36S port has a Rec demo** (`docs/DEMO-RECORDINGS.md`) next to its Device report: an item **Rec demo** in its menu right after
   **Device report** (and a `--rec-demo` flag; a command-line tool's `<command>-demo`, which Settings > Rec demos starts). It first opens a
   **checklist of the hero features** (all ticked) so the person picks which ones the demo shows; then it runs the real app through a scripted
   tour of the ticked ones on a **pre-generated demo file** (original, made by a script in the port, using every hero feature in the port's README
   hero list) and saves, in **`$R3I6NOS_APP_DATA/Demo/`** (rule 16): numbered PNG screenshots of the running app, **`demo-<timestamp>.mp4`**
   (30-90 s, with the app's own audio; the timestamp is `YYYYMMDD-HHMMSS`), a copy of the demo file and `rec-demo.txt` (the log, in the report
   grammar, `end` last). **The video always opens on the app's splash screen (rule 12) with a clear version line: the version, the git commit and the
   date**, so a video says which build made it. **While it runs, everything else is locked out:** no input but a cancel, the menu's and other
   processes' work paused or stopped, so the unit's full power goes to this one task, and a **progress bar** (step, bar, time) shows. The tour
   drives the app's real input path on a stepped clock, so a recording is the same on any unit. A new port is not done until Rec demo
   exists and a self-test checks the tour covers every hero feature; say untested what has not run on the unit.
12. **Every R36S port has a splash screen** made to the r3i6nos-ansiapps theme (rule 8; KlamANSI's is the example): the app's name and art on the 64x24
   screen, the version, commit and date, and the load's progress and any error, shown at once at start and before anything heavy runs. Rec demo's
   video starts on it.

13. **Every R36S port shows the hot-plug toast** (`docs/HOTPLUG.md`): when the 3.5 mm headphone jack, a USB device (OTG) or the DC power port is plugged in or
   out, a **toast over the top bar flashes for 3 seconds** saying what changed (from `/run/r3i6nos-hotplug/events`, written by the root service `r3i6nos-hotplug`;
   `theme/r3i6nos_toast.h` for native ports, `theme/r3i6nos_toast.lua` for LÖVE). **A failure stays until a key is pressed**, and that key is used up (it clears the
   toast and does nothing else). A new port wires it in (poll each frame, `key` first on every input, draw after composing) and says untested what has not run on the unit. It also announces a pad, the battery (low, critical, full) and the TF cards in and out. **A Rec demo shows no toast; a failure stops and closes it, then the failure(s) show** (rule 11).
14. **Every R36S port's device report has a `== power ==` section, for making the port use less energy** (`docs/POWER.md`; reference:
   `power_section` in `overlay/usr/local/bin/r3i6nos-report`). It measures, from `/proc` and `/sys` only, in one window (30 s by default, on
   battery): **the port's own process** (CPU as a % of a core, wake-ups a second, resident and peak memory, and its busiest threads), **the
   draw in mW** (`current_now` x `voltage_now`, only while the battery says `Discharging`; otherwise one SKIP that says to unplug) with the
   time left at that draw, and the **busiest other processes**, so a surprise shows. It measures **two states** where the app has them: **idle**
   (open, nothing pressed, which must cost almost nothing: block on input or a timer, redraw only when something changed, never a busy loop,
   never `vsync` at 60 fps for a still screen) and **working** (the hero features, as Rec demo's tour drives them). Each has a **goal** (a
   missed one is a `FAIL` line), and a final `INFO` line says what to cut first. Rec demo's tour is the load, so a number is the same on any
   unit. **A new port is not done until its power section exists and is tested**; say untested what has not run on a unit, and repeat on a
   unit at 100%, 50% and 20% charge before trusting a small difference. When a report comes back, fix what it flags and put the new figures
   in the port's roadmap (the menu's 60% idle, found by it on 2026-11-12, is the example).

15. **One nttplay, one installed copy per platform.** nttplay (Neumetik's players as a command-line tool) is the only decoder of the ansiapps formats, so
   what they support cannot drift between apps. **Every app that plays them is a frontend that runs it and nothing else decodes:** Commander is the file
   browser frontend, Hydrolik the music player frontend, and the next ones follow. **No frontend, package or card file carries, bundles or builds in its own
   copy of nttplay, or links Neumetik's readers.** The platform installs it once and every frontend finds that copy (on r3i6nos the image's
   `/usr/bin/nttplay`, from `scripts/build-nttplay.sh`; on another platform its one installed location, found on the `PATH` or named by `$NTTPLAY` or the app's
   own variable such as `R3I6NOS_NTTPLAY`); a frontend that cannot find it says so on screen. A format that is missing, or a sound that is wrong, is fixed in
   Neumetik, and every frontend gets the fix when that one copy is rebuilt. A test or development run may build a copy to decode with, but it is never packed.
   A port's build script fails if its package contains nttplay (Hydrolik's does).

16. **Every app made for r3i6nos (native or LÖVE) keeps its data on the data card when a valid one is in, and in one place only** (`docs/APP-DATA.md`). **On every launch,
   after its splash (rule 12) and before heavy work,** an app calls `r3_appdata_init("<App>")` (`theme/r3i6nos_appdata.h`; LÖVE: `appdata.init("<App>")` from `theme/r3i6nos_appdata.lua`, user data through `io.open`, not `love.filesystem`; the engine is
   `r3i6nos-appdata`, written once) and uses the folder it returns, `$R3I6NOS_APP_DATA`, for every write and every read of user data; it never builds
   `/roms/<App>` or `/roms2/<App>` itself. The check looks at **both** cards' `<App>/` folders: **(a)** a valid data card (mounted by role, writable,
   64 MiB free) and data only on the system card: **offer to move all of the app's data to the data card** (default Not now; Keep is remembered
   against the card's id); **(b)** data on both: **offer to merge into one card** (data card recommended; same path with different content keeps the
   newer and puts the other in `.merge-conflicts/`, never deleted); **(c)** no valid data card: use the system card, no question; the data card was
   used before and is now out: say "Needs the data card", never crash. **Copy, verify (size and SHA-256), then delete the source file; a journal
   resumes a power-cut; only `<App>/` is touched; free space and battery are checked first** (rule 7's third exception). Unattended runs (`--report`,
   `--rec-demo`) never ask and never move: they use the resolved folder. A new port is not done until it calls `r3_appdata_init` / `appdata.init`, names its folder (`DATA_DIR` in `app.conf`), its report
   and Rec demo write to `$R3I6NOS_APP_DATA`, its README lists what is app data, and its self-test covers the card-out case; say untested what has not
   run on the unit. A third-party game in `Love 11.5/` or `Love 12.0/` is not an r3i6nos app and is exempt.

## Check before committing

`scripts/check.sh` (Python tests, Lua test, `bash -n`, shellcheck). Report
tools that were not installed rather than calling the run clean.

# Screen mirror: the R36S's screen on a computer, a monitor or a TV

Status (2026-11-14): **the mirror itself is built and tested on a laptop: the daemon, the page, the tap in both themes and Settings > Screen mirror. It
has not run on a unit, and no link to a computer exists yet: the USB gadget (the unit as a network card) waits for the unit's answers below; a USB Ethernet
adapter or a Wi-Fi dongle, set up in Settings > Network, is the link to use today (untested).** Plan and order: ROADMAP section 28.

The goal: show what the unit shows on a bigger screen (a demo, a stream, a TV), **without making the unit work harder for it**. The unit is a battery
device (CLAUDE.md rule 14); an idle screen must cost an idle mirror.

## Decisions (2026-11-13, maintainer)

1. **Send the cells that changed, not pixels.** Every app is the 64x24 text-mode screen (CLAUDE.md rule 8): a cell is one character byte and one
   attribute byte (foreground and background, the 16 VGA colours), so a whole frame is 64 x 24 x 2 = **3072 bytes** and a change is a few bytes. Nothing
   is read back from the GPU, nothing is compressed, an idle screen sends nothing. The computer draws the grid with the VGA font at any size, so a TV
   gets sharp text, which a scaled pixel stream never gives.
2. **The viewer is a web page.** The computer needs nothing installed; a TV's browser can open it. The unit serves it.
3. **Not VNC.** A pixel protocol needs a frame read back from the Mali GPU (a sync on every change), polls for changes, and ships 614 KB a raw 640x480
   frame (295 Mbit/s at 60 fps) to say what 3 KB says.
4. **Wi-Fi is allowed as a link** (a dongle was tested working on the unit, 2026-11-13): Settings > Network already joins a network when a person
   chooses. The mirror is the same kind of act: a person starts it, it is off at reboot, nothing connects by itself (rule 6). The wired links come first
   because they cost less than a radio and need no network at all.

## What is built (2026-11-14)

| Piece | Where | Tests |
|---|---|---|
| The daemon `r3i6nos-mirror` (`start`, `serve`, `stop`, `status`, `list`) | `overlay/usr/local/bin/r3i6nos-mirror` | `tests/test_r3i6nos_mirror.py` (a real daemon on loopback, a UDP socket for an app, a plain socket for a page) |
| The page: a canvas painted from the cells with the VGA font embedded | `overlay/usr/share/r3i6nos/mirror/page.html`, made by `scripts/build_mirror_page.py` from `scripts/mirror_page.template.html` | the same file; `check.sh` fails when it is stale |
| The tap, Lua and C | `theme/r3i6nos_mirror.lua` (in `T.draw`), `theme/r3i6nos_mirror.h` (in `r3_video_draw`) | `tests/test_mirror.lua`, `tests/test_mirror.c` (the same cases; a fuzz that every delta replays to the screen) |
| Settings > Screen mirror (pick a link, the address, stop, instructions) | `menu/mirror.lua`, `menu/settings.lua` | `tests/test_mirror_menu.lua`, `tests/test_settings.lua` |

**How an app learns a mirror is on, without polling (the open question 3, answered):** the daemon creates the flag file `/run/loveos/mirror/on` when it
is listening and removes it when it stops. An app tests that file only when its screen has just changed (it was going to redraw anyway), and at most once
a second, so a still screen does nothing and a moving one does one `stat` a second. When the flag appears the tap sends the whole screen first; a page
that connects later is given the daemon's copy of the current screen. **The one gap:** a mirror started while the foreground app's screen stays still shows
the daemon's last copy (blank on a fresh start) until that screen next changes; Settings > Screen mirror itself changes when it starts, so starting
from the menu shows the menu, and an app launched after that sends its first screen whole.

**The datagrams** (UDP to `127.0.0.1:36364`, `R3I6NOS_MIRROR_INGEST` moves it): `F` and 3072 bytes (cell i is byte 2i, a code page 437 index, and 2i+1, the
attribute: foreground in the low nibble, background in the high one), or `D` and runs of (u16 big-endian start, u8 count, count x 2 bytes). A page gets the
same as Server-Sent Events `full` and `delta`, base64. Local UDP, because LÖVE has luasocket but no Unix sockets, and a datagram to nobody costs nothing
and can never block or kill an app (a pipe's SIGPIPE could). **Any local process can send cells**, so a game could write to a mirrored screen: the mirror is
for showing what the unit shows, not for trusting; the page is read-only and the token and the chosen link are what keep others out.

**What is not built:** the USB gadget link (below), a toast when the link comes up (ROADMAP section 19), pixel pages (Commander's picture viewer and
video show as the text around them), power goals (rule 14: the states to measure are in "Power" below, none measured), and a sound stream. An app that
does not draw through `T.draw` or `r3_video_draw` is not mirrored.

## The links, in the order to prefer them

| Link | What it needs | Why |
|---|---|---|
| **USB gadget Ethernet**: the unit is a network card for the computer | a device-mode controller, configfs, an ECM or NCM function (RNDIS for Windows) in the kernel; the device tree must not pin the port to host | one cable; the computer powers and charges the unit; no adapter. **Unknown on the unit.** |
| **USB Ethernet adapter**: the unit hosts it | host mode (**works**: a Wi-Fi dongle ran) and a driver for the adapter's chip (`asix`, `r8152`, `cdc_ether`, ...) | the maintainer will test it; the adapter draws from the battery, and the one port cannot also charge |
| **Wi-Fi dongle** | **tested working** | a radio costs more than a wire; use it when no cable reaches |
| DisplayLink (ROADMAP section 24) | `udl` in the kernel | goes straight to a monitor with no computer; a separate track |

The device report answers the first two (below). One thing about USB 2.0: an Ethernet adapter is usually 100 Mbit/s, and that is plenty for cells.

## The stream

- **Transport: HTTP and Server-Sent Events.** The page is `GET /`; its script opens `EventSource('/cells?...')`. One way, unit to page, Python's standard
  library on the unit, no WebSocket framing. No heartbeat (a timer would wake the unit): TCP keepalive finds a dead page.
- **Frame:** `full` (all 3072 bytes, sent on connect and when a page asks) and `delta` (runs: start cell, length, the cells). Base64 inside the event.
  A keyframe is never sent on a timer.
- **Where the cells come from:** the theme's screen layer (`theme/`, Lua and C), at the point it already holds the cell buffer and finds what changed to
  redraw. With no page connected the cost is one flag test; the redraw-only-when-changed rule (rule 14) is what makes the mirror free when idle.
- **Who serves:** a small root-free daemon, `r3i6nos-mirror` (Python standard library, started from a Settings item, never at boot). The foreground app
  hands it cells over a Unix socket under `/run/loveos/mirror/`; the daemon owns the page and the connections. (An app serving by itself would put an HTTP
  server in every LÖVE and C app; one daemon is smaller.) How an app learns a mirror is on, without polling, is an open design point.
- **Pages that are not cells** (Commander's picture viewer and video, any pixel canvas): the mirror falls back to changed **32x32 tiles**, hashed, LZ4 or
  zstd, sent only when redrawn, at a low rate; or says "this screen is a picture" and shows the cells around it. Decide after the cells work.
- **The page:** a canvas painted with the IBM VGA 8x16 font (CC BY-SA 4.0: the credit and `NOTICE` come with it), the 16 VGA colours, scaled to the
  window with `image-rendering: pixelated` or drawn as text for any size; the top bar's version line is part of the cells. **Non-interactive**, like the
  widescreen dashboard (CLAUDE.md): it sends no input to the unit.
- **Access:** listens only on the interface the person chose (the gadget's `usb0` or the adapter's), with a **random token** in the address, shown on
  the unit's screen when the mirror starts. Off at reboot. Reads nothing from the games card.

## What the device report answers (`r3i6nos-report`, written by Settings > Device reports > r3i6nos)

Two sections, read-only, no root, from `/proc` and `/sys`; code `usb_gadget` and `usb_ethernet` in `overlay/usr/local/bin/r3i6nos-report`.
**FAIL** means the kernel config proves the feature is missing; **SKIP** means nothing readable says either way (a symbol or a module file can be missing
while the code is built in, so only a config proves an absence); **PASS** means the running kernel has it.

`== usb gadget ==`:
- the kernel and where its config was read (`/proc/config.gz`, the boot partition's `config-<release>`, `/lib/modules/<release>/config`); its symbols
  and module files;
- the USB controller nodes in the device tree with their `dr_mode` (**`host` there turns device mode off**), the platform drivers, the UDCs in
  `/sys/class/udc` with their state and speed, the `extcon` states (which role the port is in now) and the USB and mains supplies;
- the pieces of a gadget: a **device-mode controller**, **configfs**, the **composite core**, **USB gadget by configfs**, each **function** (ECM, NCM,
  RNDIS, EEM), legacy `g_ether`, UVC (the unit as a camera: ROADMAP section 24); the gadgets set up now;
- one verdict line, `gadget ethernet`: PASS (ready to try), FAIL (a missing piece, named) or SKIP (what is unknown, and the next step).

`== usb ethernet ==`:
- the host drivers (`usbnet`, `asix`, `ax88179_178a`, `r8152`, `cdc_ether`, `cdc_ncm`, `smsc95xx`, `rndis_host`, `dm9601`) and one `usb ethernet adapter` line;
- every USB device plugged in: vendor and product, speed, the driver on each interface, and a **FAIL for a network-class interface with no driver**
  (an adapter the kernel does not know: its chip says which driver to build);
- every network interface: wired or wifi, gadget or USB adapter, driver, link state, carrier, speed, MAC, IPv4 address; **PASS `wired link`** when a USB
  wired interface has a carrier;
- the tools a link would need (`ip`, `modprobe`, `udhcpd`, `dnsmasq`, `busybox`);
- the verdict, `wired mirror verdict`: the gadget, the adapter, neither (a Wi-Fi dongle is the fallback), or undecided.

**To get every answer, run it three times** (Settings > Device reports > r3i6nos, which counts the runs ("run 1 of 3"), shows each run's setup and waits for A, and writes `r36s-report-run1.txt`, `-run2.txt` and `-run3.txt` (give Claude all three); read `r3i6nos/r36s-report.txt` on the data card), each time reading
the two sections: (1) nothing on the OTG port; (2) the Ethernet adapter plugged in, with the cable to a router or computer; (3) a plain USB cable from
the OTG port to a computer. Whatever the report cannot say (for example, a UDC that appears only when a cable puts the port in device mode) it says in a
SKIP line with the next step.

## What the report does not do

It does not try. **Making a gadget needs root** (a configfs write and a `modprobe`), and the report runs as `loveos`. If the report says the pieces are
there, the next step is a verb in the root helper (like `r3i6nos-net`'s) that builds an ECM or NCM gadget and brings up `usb0` with a link-local address,
and a report line that checks a computer sees it. That is ROADMAP section 28's next item; nothing is built until the report has been read.

## Power (rule 14)

Goals are set after the first measurements. What to measure, on battery, in a window: the unit idle with no mirror, with the link up and no page, with a
page open on an idle screen (should cost nothing more than the link), and with a page open during the Rec demo's tour (the cell traffic). Also the cost of
the link itself: an adapter draws from the battery, an active USB link stops the port's suspend, and a Wi-Fi radio draws more than either. The idle draw
without a mirror was about 2.96 W with a hub and peripherals on the port (ROADMAP section 23).

## Open questions

1. Is the OTG port able to be a device? (the report)
2. Which Ethernet chips have drivers in the vendor kernel? (the report, with the adapter plugged in)
3. ~~How does an app learn a mirror is on, without polling and without a timer?~~ Answered above: a flag file, tested when the screen has changed.
4. Pixel pages: tiles, or only the cells around them?
5. Audio: out of scope for the first version (the computer cannot hear the unit). A WAV or Opus stream over the same link is possible later.

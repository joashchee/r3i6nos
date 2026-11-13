# Screen mirror: the R36S's screen on a computer, a monitor or a TV

Status (2026-11-13): **designed, and the questions it depends on are now a report section. Nothing of the mirror itself is built, and nothing here has run on a
unit.** Plan and order: ROADMAP section 28.

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
3. How does an app learn a mirror is on, without polling and without a timer?
4. Pixel pages: tiles, or only the cells around them?
5. Audio: out of scope for the first version (the computer cannot hear the unit). A WAV or Opus stream over the same link is possible later.

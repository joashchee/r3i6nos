# DisplayLink adapters: what has the best chance of working on the R36S

Written 2026-11-12 for ROADMAP section 24 ("External video over USB"). **Nothing here has been tried on a unit.** What is checked and what is
from memory is marked on each line; the decisive facts (the chip, whether the kernel has `udl`) are read on the unit, not from a shop listing.

## Why only a few chips can work

- The R36S's OTG port is **USB 2.0 data only**: no DisplayPort or HDMI lanes. So a USB-C dock's HDMI (DisplayPort alt mode) cannot work: the dock
  shows up as a **Billboard** device (the report and the IO Checker's USB video page say so).
- Video has to travel as USB data, which is what DisplayLink does: the host compresses the screen and sends it over bulk transfers; the adapter
  decodes it and drives the monitor.
- The only DisplayLink generation the Linux kernel drives with its own open driver is the **USB 2.0 generation: the DL-1x0 ("Alex") and DL-1x5
  ("Ollie") chips** (kernel `udl` DRM driver, which replaced the older `udlfb`; checked against libdlo and the kernel's udlfb documentation).
  Everything newer needs DisplayLink's proprietary driver and the out-of-tree `evdi`, which CLAUDE.md rule 2 rules out of the repo, and which the
  userspace (no X, SDL KMSDRM) would not use anyway.
- Our kernel is the vendor's **4.4.189**. Whether it has `udl` built in or as a module is unknown (ROADMAP section 24: read `/proc/config.gz`, `lsmod`,
  `modinfo udl` on the unit). If it has not, the module is built out of tree against the vendor kernel source and loaded from the user's own card (rule 2).

## Chips, best chance first

| Chip | Family | Why | Confidence |
| --- | --- | --- | --- |
| **DL-195** | DL-1x5 | The top of the USB 2.0 generation: dual core, the widest mode list (the family's maximum is 2048 x 1152). The same `udl` path as the others. | chip family checked (libdlo, Synaptics brief); no model of adapter checked |
| **DL-165** | DL-1x5 | The same generation, one step down. A DisplayLink forum report: a firmware update done once from the Windows driver left Linux detecting the monitor wrongly, so **do not update the adapter's firmware**. | checked (libdlo; forum report, one user) |
| **DL-125** | DL-1x5 | Works with `udl`, but **Plugable notes mode limits (1440 x 900, 1280 x 1024)** that are common on this chip, enough for a 640x480-based UI at 2x. Prefer a DL-165 or DL-195 when there is a choice. | checked (libdlo; Plugable's note) |
| **DL-115** | DL-1x5 | The lowest of the 1x5 series (same brief). Fine for a text-mode screen if its modes include 1024 x 768 or 1280 x 720; confirm the modes from the monitor's EDID with the adapter on the unit. | listed in the Synaptics brief; limits not read |
| **DL-160** / **DL-120** | DL-1x0 | The older generation, also `udl` (libdlo lists both). Older adapters (2010 to 2013) and mostly second-hand; slower and with fewer modes. | checked (libdlo) |

What matters for this project is not the resolution but the **CPU**: the host compresses every changed region over USB 2.0 on a 1.5 GHz Cortex-A35,
so a fixed 64x24 text screen that redraws only when something changed (CLAUDE.md rule 14) is the right fit, and a video-rate mirror is not.

## Adapters

The chip is what counts, and a brand can swap a chip between revisions. **Every model below is from memory, not checked against a spec sheet:
confirm the chip first** (below).

- **Plugable:** the UGA-125 is a DL-125 adapter (confirmed in Plugable's own Linux article); the UGA-165 / UGA-2K-A line is DL-165 (from memory).
  Plugable has published Linux notes for its DisplayLink adapters, which makes it the best-documented maker.
- **Makers that sold USB 2.0 DisplayLink adapters (VGA, DVI, HDMI) in the DL-1x5 years** (about 2011 to 2018): StarTech, j5create, Sabrent,
  Kensington, Targus, Belkin, and the docks that Lenovo, Dell and HP sold. Their 2012 to 2016 "USB 2.0 to VGA / DVI / HDMI" models are the likeliest to carry a DL-1x5.
- **Second-hand is the normal place to find one**: the USB 2.0 generation is no longer made new in volume. Look for the words **"DisplayLink DL-165"**, **"DL-195"**,
  **"DL-125"** or **"DL-1x5"** in the listing, and the USB 2.0 logo (not USB 3.0 or USB-C).

## How to tell, before buying and with the adapter in hand

1. **Before buying:** the listing names the DisplayLink chip, or the maker's spec sheet does. A listing that says only "USB to HDMI adapter" is a gamble.
2. **With it in hand, on any Linux computer:** `lsusb` shows vendor **17e9** (DisplayLink) for every DisplayLink chip. That alone does not say which generation.
3. **On the unit:** the IO Checker's USB devices row shows the vendor and product ID and its drivers. A DL-1x5 with a working `udl` shows the driver `udl`, and a new card
   appears on the USB video page (`/sys/class/drm`, a connector named `DVI-I-1`, `HDMI-A-2` or `VGA-1`, "USB adapter") with the monitor's modes. If it shows **no driver**,
   the kernel has no `udl` (section 24: the out-of-tree module), or the adapter is a newer generation.

## What to avoid

- **Any USB-C dock or hub whose HDMI or DisplayPort uses alt mode.** The unit sees a Billboard device (the dock you tried: 2109:0100). It cannot work.
- **DisplayLink DL-3xxx, DL-5xxx, DL-6xxx and later** (the USB 3 and USB-C docks, "DL-3000", "DL-3500", "DL-3900", "DL-5xxx", "DL-6xxx"): proprietary driver and `evdi`. Out (rule 2).
- **Unbranded "USB 3.0 to HDMI" dongles** (Trigger, Fresco FL2000, Silicon Motion SM768 and others): out-of-tree Linux support at best, and they want USB 3.
- **Adapters that need a firmware or driver update from a Windows or Mac installer** (a DL-165 whose firmware was updated that way misreported its monitor on Linux).
- **A bus-powered adapter on the OTG port alone.** The adapter draws a few hundred mA; use a **powered hub** (the hub you have is the right idea) so the unit's 5 V is not the supply, and read the
  USB device's current in the IO Checker's report.

## Power and what it costs

An adapter on the port adds to the draw and the CPU cost of compressing frames: measure it with the power section (rule 14) with and without it, and redraw the external screen only when
something changed. The first report's idle draw of 2983 mW was with a hub, keyboard, mouse, card reader and a dock on the port: do the comparison with only the adapter on a powered hub.

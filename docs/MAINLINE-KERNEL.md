# Moving to a mainline kernel: an estimate

**Status: an estimate and a spike plan (2026-11-11), not a decision and not started.** Nothing here has run on the unit. Numbers are working days for one developer
working with Claude, with the R36S on the bench, and are **rough: plus or minus half**. They come from the size of this repo's hardware-coupled code (measured below) and from
what two other firmwares already ship; the biggest unknown is how many panel variants of the genuine R36S exist and who can test them.

## Why look at it

The stock kernel is the vendor's 4.4.189. It is the reason for the Mali blob, the modules copied from the user's card (rule 2) and the `Playback Path` sound quirk, and it is a
dead end for the Debian base: systemd 257 (Debian 13) only *recommends* kernel 5.4, and systemd 258 drops below it (`ROADMAP.md` section 7, "Debian 13"). A mainline kernel
(6.12 LTS or newer) removes all of that.

## It is a known road, not research

| Firmware | Kernel | GPU | Source |
|---|---|---|---|
| **ROCKNIX** supports the "Game Console R35S/R36S" officially | "Mainline Linux" (no version on the page) | libmali (GLES 3.2) **and** Panfrost (GL 3.1 / GLES 3.1) | <https://rocknix.org/devices/unbranded/game-console-r35s-r36s/> |
| **Arch R** (R36S and clones) | Linux **6.12.114 LTS** | Mesa 26.1.3 **Panfrost**; libmali switchable | <https://arch-r.io/> |

What they show us (and what we can copy, all GPL): a working device tree for the genuine R36S, a way to handle the **panel variants** (ROCKNIX: a `mipi-panel.dtbo` overlay on the
BOOT partition, either one of several prepared `r36s-panel*` files or **generated from the card's own stock `rk3326-r35s-linux.dtb` by a script, `importpanel.py`**), the stock
Hardkernel U-Boot loader reused for the original boards (Arch R's "original" image), and a Mesa Panfrost stack that drives the Mali-G31. Upstream has the odroid-go family and a
GameForce Chi series with an ST7703 panel (Chris Morgan, March 2024: <https://lists.infradead.org/pipermail/linux-rockchip/2024-March/045810.html>); **I did not find an R36S device
tree upstream**, so the R36S board file and its panel driver come from the ROCKNIX patches, not from kernel.org.

## What would change here (measured in this repo)

Files that mention the vendor stack: `libmali` 16, `Playback Path` 10, `power_supply` 16, `rk817` 11, the pad's GUID or name 6, `/dev/mali0` 2, `ScreenFiles` and `panel=` (the stock
panel chooser) 4. The scripts that read it: `r3i6nos-sound` (401 lines), `r3i6nos-hotplug` (427), `r3i6nos-battery` (134), `loveos-session`, `run_app.sh`, `r3i6nos-setup`,
`r3i6nos-about`, `r3i6nos-report`, the theme's pad table, both IO Checkers, and the nine ports' controller code.

What gets **simpler**: the setup no longer copies 520 MB of modules, firmware and a Mali blob from the original (our own kernel, modules and Mesa are in the image, all GPL or open),
so rule 2 shrinks to the bootloader and the card's panel data; setup takes seconds instead of minutes; `inject-rootfs`, `capture-card` and the "wrong Mali blob" bug class go; cgroup v2 and
Debian 13 and later become possible.

## The work, in packages

| # | Package | Days | Notes |
|---|---|---|---|
| 0 | **Spike**: boot ROCKNIX's RK3326 image on the unit; check panel, sound, buttons, battery, GPU; read its kernel config, DTS and patches | 0.5-1 | decides everything below; costs one card and an evening |
| 1 | Kernel recipe: kernel.org 6.12 LTS + the ROCKNIX RK3326 patches pinned by hash, our config, a CI job that cross-builds `Image`, modules and DTBs, licence notices (rule 2: record licence and source) | 4-7 | the alternative, Debian's own arm64 kernel plus out-of-tree panel and pad modules and our DTB, avoids a kernel build and gets Debian's security updates, but is less proven: 5-9 days, riskier |
| 2 | Boot chain: our `boot.ini` for a mainline `Image` + DTB + overlays on the card's own loader; the carrier boot still starts from the original's stock kernel; `r3i6nos-setup` stops copying modules and the blob and instead writes our kernel, DTBs and the panel overlay; tests | 4-6 | the web guide's `boot.ini` step stays |
| 3 | Panels: port `importpanel.py` (needs `dtc` in the image), test each panel revision | 5-10 | **hardware-limited**: we own one unit (Panel 4). The rest needs testers on Discord and calendar time |
| 4 | Graphics: Mesa Panfrost under SDL KMSDRM, LÖVE 11.5 and 12.0, native apps; drop `/opt/libmali`; compare frame times against the targets (KlamANSI 33 ms) | 4-8 | risk: Panfrost slower than the blob on the G31; ROCKNIX keeps libmali as an option for a reason |
| 5 | Audio: the mainline RK817 codec has different controls from the BSP's `Playback Path`; rewrite `r3i6nos-sound`, `asound.conf`, `docs/SOUND.md`, tests | 3-5 | |
| 6 | Input: a new pad device (adc-joystick and GPIO keys, or ROCKNIX's joypad driver), a new GUID and button numbers; `run_app.sh`, theme `PAD`, both IO Checkers, NoCoGaMa and the ports | 4-7 | everything keyed to GUID `1900bb3e...` changes |
| 7 | Power: battery and charger sysfs names, jack detection, USB OTG, DC port, TF-card events, RTC; `r3i6nos-battery`, `-hotplug`, `-about`, `-report`, `-bootstate`, tests | 4-6 | |
| 8 | Performance and power: cpufreq/GPU tables (mainline may cap lower than the vendor's 1.5 GHz), thermals, idle draw, boot time, RAM; `docs/POWER.md` numbers | 3-5 | mostly bench time |
| 9 | Image and CI: package list, remove libmali plumbing, firmware, image job, tests | 2-3 | |
| 10 | Docs and rules: rule 2 rewritten, `hardware-findings.md`, the guide, STORAGE, the ROADMAPs | 2-4 | |
| 11 | The long tail: regressions across the nine ports, Rec demo and ffmpeg encode times, suspend and poweroff, odd panels | 6-12 | always larger than it looks |
| | **Total** | **about 40-75 working days** (8 to 15 weeks of focused work) | calendar time is longer: it waits on hardware and testers |

**A first slice** (the maintainer's one unit boots our image on a mainline kernel to the menu, with graphics, sound, buttons and a battery reading; one panel; nothing else): packages 0, 1, 2,
4, 5, 6 and one panel of 3, **about 20-30 days (4 to 6 weeks)**. That slice is also the proof; the other half is parity and coverage.

## Risks, in order

1. **Panel variants and board revisions.** Every R36S revision and panel that someone owns is a thing we cannot test. ROCKNIX needed a family of panel overlays for this. Rule 3 already limits us to
   genuine RK3326 R36S boards; how many of those there are is unknown.
2. **Panfrost performance and features** on the Mali-G31 against the blob (LÖVE 12 wants GLES 3.0; ROCKNIX reports GLES 3.1).
3. **Parity of the small things**: the audio amplifier and jack switching (GPIOs in the board file), the second TF slot's controller, charging behaviour, hot-plug events. Each was measured on the BSP
   kernel and must be measured again.
4. **Maintenance**: a kernel to rebuild when 6.12 moves; mitigated by tracking ROCKNIX/Arch R and by staying on an LTS.
5. **Licensing paperwork** (GPL source offer for our kernel build) is routine but must be done before a release.

## Recommendation

1. **Finish the carrier-boot trials first** (`docs/TRIAL-T1.md`, T1 to T5): any kernel still boots through the same U-Boot and the same `boot.ini`, and the setup and the guide already work for it.
2. Then **do the spike (package 0)**: it costs about an evening and tells us whether the genuine R36S panel works on a mainline kernel today. If it does, package 1 can start from ROCKNIX's recipe.
3. Decide at that point between the **first slice** (4-6 weeks) and staying on 4.4 with Debian 12 until its long-term support ends (2028-06-30).

## Sources

ROCKNIX R35S/R36S page and Arch R's site as linked above; systemd 257 NEWS and README (kernel baseline); Debian release dates (<https://www.debian.org/releases/>). Counts in "What would change"
are from `grep` over this repo on 2026-11-11.

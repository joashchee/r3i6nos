# Review of `r36s-chips.md` (Perplexity's source list), 2026-11-19

Checked against the RK3326 datasheet V1.4 (read directly), this repo's captures of the stock card (`local_hardware_assets/`, `docs/hardware-findings.md`), and a few searches. **Nothing here was
measured on the unit beyond what the earlier reports already say.**

## Holds up

| Claim | Check |
|---|---|
| RK3326: quad Cortex-A35, Mali G31, MIPI-DSI, USB OTG, SDIO | Datasheet V1.4 (Rev 1.4, 2019-01-10): quad Cortex-A35, "ARM G31-2EE" GPU (OpenGL ES 3.2), MIPI DSI. It does not say "MP2"; the doc's "MP2" is the usual name, not in the datasheet |
| RK817 PMIC, charger, RTC, codec | Our stock DTBs (all six panel variants) have `rockchip,rk817`, `rk817,battery`, `rk817,charger` and `rockchip,rk817-codec`; the unit's sound card is `rockchip,rk817-codec` (`docs/hardware-findings.md`) |
| "Not a single fixed hardware revision"; panel varies | Matches the stock card's `ScreenFiles/Panel 0..5`; our own unit is Panel 4 (`docs/MAINLINE-KERNEL.md`) |
| 1 GB of RAM | The unit reports 919,232 kB total (`docs/hardware-findings.md`) |
| Display init sequences live in the device tree | True of the stock DTBs: the panel is a generic `simple-panel-dsi` node, and no DTB names NV3051D or ST7703 |
| Links | All resolve except the Elektroda thread (no answer; may only be blocking bots) |

## Does not hold, or is not settled

1. **"1 GB DDR3L" is called the "consistently documented core", but it is not established.**
   - The datasheet's memory interface allows **DDR3, DDR3L, DDR4, LPDDR2 and LPDDR3**.
   - Our captures contain no DRAM type: the DTBs only say `rockchip,px30-dmc` with an OPP table, and the type is set by the bootloader's DRAM blob.
   - The unit can answer it (`dmesg`, `/sys/class/devfreq/dmc`); added to ROADMAP section 35 step 2.
2. **The SK hynix H9TQ32A4GTMC-URKUM row contradicts the "DDR3L core" and is probably another board.**
   - The closest datasheet found (H9TQ32A4GTMCUR, preliminary, July 2016) is an **eMCP: 4 GB eMMC plus LPDDR3 4 Gb (x32)**, 221 balls.
   - That is **512 MB** of LPDDR3, not 1 GB of DDR3L, and the genuine R36S boots from an SD card with no eMMC.
   - The doc itself says "board-variant-specific"; do not treat it as part of the R36S.
3. **"Micron D9PXW" is unresolved.**
   - D9PXW is an FBGA code, not a part number.
   - Micron's own decoder (micron.com, "FBGA parts decoder") maps it; the doc is right that no datasheet is indexed under the code, but it could have looked the code up.
   - I could not resolve it here. The two-package, 512 MB-each claim is from one teardown.
4. **Display drivers: NV3051D and ST7703 are from other firmwares' configs**, not from our captures. Our unit's panel is Panel 4; which controller it is, is not in any file we hold.
5. **The doc's own caveat stands:** no manufacturer schematic or BOM is public, so "all chips" cannot be listed. Treat the table as a starting point for reading the board's markings.

## What this changes in the repo

- The VPU review (`docs/VIDEO-DECODE.md`) now cites the datasheet directly. It corrected the Gemini guide on one point: **the hardware decodes MPEG-4, not MPEG-1/2**.
- ROADMAP section 35 step 2 (the unit's hardware probe) also reads the DRAM type and size.
- Nothing else. The doc has no claim the code depends on, and none was copied into a source file.

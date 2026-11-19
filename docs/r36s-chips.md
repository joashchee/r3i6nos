# R36S Chip Documentation and Reachable Sources

The R36S is not a single fixed hardware revision. Its core platform is consistently **Rockchip RK3326 + RK817 PMIC/audio CODEC + 1 GB DDR3L**, but the display-driver IC, RAM package marking, and some supporting components can vary by factory, production batch, and clone variant.

## Core chips

| Chip | Role in R36S | Reachable documentation |
|---|---|---|
| **Rockchip RK3326** | Main SoC: quad-core ARM Cortex-A35, Mali-G31 MP2 GPU, DDR controller, USB, SDIO, MIPI-DSI, and audio interfaces | [RK3326 datasheet V1.4, PDF](https://rockchip.fr/RK3326%20datasheet%20V1.4.pdf) — official Rockchip document mirrored at rockchip.fr; includes features, package, pinout, electrical specifications, and peripheral interfaces. |
| **Rockchip RK817 / RK817-1** | PMIC, battery charger, RTC, and integrated audio CODEC | [RK817 datasheet V1.7, PDF](https://rockchip.fr/RK817%20datasheet%20V1.7.pdf) — covers regulators, charger, power path, audio CODEC, register map, and package details. |
| **Micron D9PXW-marked DDR3L** | 1 GB total system RAM, commonly implemented as two 512 MB DDR3L packages | No dependable public datasheet is indexed under the **D9PXW** marking itself. It is a Micron package/date/lot-style marking, not necessarily a complete orderable part number. Identify the full Micron part number from the package label or board BOM before ordering a replacement. |
| **SK hynix H9TQ32A4GTMC-URKUM** | Reported on some R36S boards as a multi-chip package (MCP), typically combining DRAM and NAND storage | Board-variant-specific; documented in an R36S hardware discussion rather than as a universal R36S component. |

## Display drivers

The R36S’s 3.5-inch, 640 x 480 MIPI-DSI panel uses different driver ICs depending on the panel revision.

| Chip | Role | Reachable documentation |
|---|---|---|
| **Novatek NV3051D-family driver** | Panel controller / MIPI-DSI display driver on certain R36S panels | Identified in the NixOS-R36S hardware configuration as the “Panel 4” variant. Full public datasheets are commonly restricted or available only through unofficial mirrors. |
| **Sitronix ST7703** | Alternative MIPI-DSI panel driver used by some R36S and clone panel variants | Its Linux driver binding is documented in an R36S clone board-support issue. Public full datasheets are often behind distributor gates or unofficial mirrors. |

For display work, the practical reference is often the **panel initialization sequence in the device tree**, rather than a public controller datasheet. R36S Linux projects use panel-specific DT overlays and initialization sequences for the installed panel.

## Board-level references

- **R36S device tree:** [rk3326-r35s-linux.dtb](https://github.com/Takiiiiiiii/R36S/blob/main/rk3326-r35s-linux.dtb) — useful for pin assignments, regulators, display, SD, USB, and audio configuration.
- **NixOS-R36S hardware configuration:** [GitHub repository](https://github.com/icefirex/nixos-r36s) — identifies RK3326, Mali-G31 MP2, RK817 audio/PMIC, MIPI-DSI display, and the NV3051D panel variant.
- **R36S hardware/ArkOS documentation:** [r36s-devbridge](https://github.com/JhonatanRian/r36s-devbridge/blob/main/docs/hardware-and-arkos.md) — consolidates RK3326, RK817, display, USB OTG, and ArkOS behavior.
- **Rockchip document index:** [rockchip.fr](https://rockchip.fr/) — reachable mirror listing multiple RK3326 datasheet revisions, including V1.1, V1.2, V1.4, and RK3326-S.
- **Odroid Go Advance schematics:** the RK3326-based Odroid Go Advance reference PDFs are useful for understanding RK3326 + RK817 reference designs; a community R36S discussion links their rev 1.0 and rev 1.1 documentation.

## Important caveat

No official, complete R36S schematic or bill of materials appears to be publicly published by its manufacturer. Therefore, “all chips” cannot be established with certainty from public sources alone: RAM marking, panel driver, USB hub/charging components, Wi-Fi module on variants, and minor power components can differ between factories and revisions.

The consistently documented core is:

- **Rockchip RK3326** SoC
- **Rockchip RK817** PMIC, RTC, charger, and audio CODEC
- **1 GB DDR3L** system memory
- **MIPI-DSI panel**, with at least NV3051D-family and ST7703 driver variants

For repair or driver porting, read the markings directly from your board—especially the two RAM packages and the display flex/PCB—then match them to the exact part number before relying on a datasheet.

## Source index

| Source | Link | What it establishes |
|---|---|---|
| Rockchip RK3326 datasheet V1.4 | https://rockchip.fr/RK3326%20datasheet%20V1.4.pdf | SoC features, interfaces, package, pinout, electrical characteristics |
| Rockchip RK817 datasheet V1.7 | https://rockchip.fr/RK817%20datasheet%20V1.7.pdf | PMIC, charger, RTC, CODEC, register map, package |
| Rockchip document index | https://rockchip.fr/ | Multiple RK3326 datasheet revisions and related Rockchip documents |
| R36S device tree | https://github.com/Takiiiiiiii/R36S/blob/main/rk3326-r35s-linux.dtb | Board configuration and hardware bindings |
| NixOS-R36S | https://github.com/icefirex/nixos-r36s | RK3326, RK817, Mali-G31 MP2, MIPI-DSI, NV3051D panel variant |
| R36S DevBridge hardware documentation | https://github.com/JhonatanRian/r36s-devbridge/blob/main/docs/hardware-and-arkos.md | RK3326, RK817, display, USB OTG, and ArkOS behavior |
| R36S clone board-support issue | https://github.com/lcdyk0517/arkos4clone/issues/530 | ST7703 panel driver and RK817 audio/power configuration |
| R36S hardware discussion | https://www.elektroda.com/rtvforum/topic4158492.html | RK3326 and H9TQ32A4GTMC-URKUM memory on some boards |
| R36S hardware teardown report | https://2bluebox.com/blogs/r36s/r36s-vs-r36max | RK3326, RK817-1, and two Micron D9PXW DDR3L chips |

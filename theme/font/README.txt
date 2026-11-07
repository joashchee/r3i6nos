r3i6nos-ansiapps theme font: attribution and licence
====================================================

Material:  "WebPlus IBM VGA 8x16" (WebPlus_IBM_VGA_8x16.woff), from The Ultimate Oldschool PC Font
           Pack v2.2, https://int10h.org/oldschool-pc-fonts/
Author:    VileR. Copyright (c) 2016-2020 VileR.
Licence:   Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0),
           https://creativecommons.org/licenses/by-sa/4.0/  (full text: LICENSE.TXT, next to this file)
Warranty:  none; the font is provided as is (LICENSE.TXT, section 5).

Changes made by r3i6nos (2026):
  1. WebPlus_IBM_VGA_8x16.woff here is the original, byte for byte.
  2. scripts/build_vga_font.py rasterises its 256 code page 437 characters at 16 px into 1-bit 8x16
     bitmaps: theme/vga8x16.lua and theme/r3i6nos_vga8x16.h. Those two files are adaptations and are
     licensed under CC BY-SA 4.0 (not MIT, unlike the rest of the repository); each carries this notice.
  3. At run time the theme scales each 8x16 bitmap up to 10x20 pixels by nearest neighbour, with no smoothing
     (docs/R3I6NOS-ANSIAPPS-THEME.md).

Whoever receives the theme, in source or in a built image, may use, share and adapt these font files under
the same licence, and must keep this notice and LICENSE.TXT with them. No further restriction is placed on
them, and no technological measure restricts what the licence allows.

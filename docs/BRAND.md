# r3i6nos Brand Guidelines

What the logo is, so a new screen, page or icon draws it the same way. The art is the maintainer's, drawn by hand; **do not redraw it**,
use `docs/brand/boot-logo.png` (640x480) or crop from it.

## The wordmark

- **"r3i6n"** is a typographic wordmark in one rounded, monoline letter shape.
- **"3i6" is a butterfly, drawn symmetrical.** The "3" and the "6" are its wings (the "3" has just its corner in the dark, in shadow), the
  "i" is the body, its dot the head with two antennae.
- **"r" is the mirror image of "n"**, with a portion in the dark like the "3".
- **Colours:** "r" and "n" are the grey of the solid-colour Apple logo; "3i6" has the banded colours of the classic Apple logo
  (green, yellow, orange, red, purple, blue from the top), the antennae and head green.
- **"os" is not part of the logo.** It is only the small wordmark set beside or under it that names the OS component of r3i6n (on the
  boot screen, in the pixel font under the butterfly).

## The boot screen

`docs/brand/boot-logo.png` is the first picture the unit shows: 640x480 on a near-black background. It is converted to the BMP the
bootloader reads by `scripts/make_boot_logo.py` (the stock card's `boot/logo.bmp` header byte for byte: 24 bits, bottom-up, no alpha,
921,656 bytes) into `overlay/usr/share/r3i6nos/boot-logo.bmp`, and `scripts/inject-boot.sh` puts it on the card's BOOT partition in place
of the stock logo. `R3I6NOS_STOCK_LOGO=1` keeps the stock one; `R3I6NOS_BOOT_LOGO=<file>` names another 640x480 24-bit BMP.

To change the art: replace the PNG (8-bit RGB or RGBA, 640x480, not interlaced), run `scripts/make_boot_logo.py`, commit both files.
`scripts/check.sh` fails when the BMP is not what the PNG gives.

**Untested on the unit:** the bootloader may read `logo.bmp` from a copy in its own logo area instead of BOOT (its strings say "Load
logo.bmp from logo part"). One boot of a card made with this logo shows which; if the stock logo still appears, find where it reads from.

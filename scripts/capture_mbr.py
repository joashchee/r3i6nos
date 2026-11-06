#!/usr/bin/env python3
"""capture_mbr.py <first-16MiB.raw> <bootloader.bin>

Reads the first sectors of an R36S card (copied by capture-card.sh), checks it
is an MBR card, lists its partitions, and writes the raw bootloader area
(sector 64 up to the first partition, at most sector 32768) to bootloader.bin,
the file inject-boot.sh puts into an image. Standard library only.
"""

import sys

SECTOR = 512
LOADER_SECTOR = 64          # Rockchip idbloader; U-Boot follows at 16384
MAX_END_SECTOR = 32768      # where r3i6nos's BOOT partition starts (16 MiB)


class CaptureError(Exception):
    pass


def parse(raw: bytes) -> dict:
    if len(raw) < MAX_END_SECTOR * SECTOR:
        raise CaptureError(f"only {len(raw)} bytes read; need {MAX_END_SECTOR * SECTOR}")
    if raw[510:512] != b"\x55\xaa":
        raise CaptureError("no MBR signature (0x55AA) in sector 0")

    parts = []
    for i in range(4):
        e = raw[446 + 16 * i: 446 + 16 * (i + 1)]
        ptype = e[4]
        start = int.from_bytes(e[8:12], "little")
        size = int.from_bytes(e[12:16], "little")
        if ptype == 0 or size == 0:
            continue
        parts.append({"number": i + 1, "type": ptype, "start": start, "size": size})
    if not parts:
        raise CaptureError("the MBR has no partitions")
    if any(p["type"] == 0xEE for p in parts):
        raise CaptureError("this card uses GPT; capture-card.sh only handles MBR cards")

    first = min(p["start"] for p in parts)
    if first <= LOADER_SECTOR:
        raise CaptureError(f"first partition starts at sector {first}, inside the bootloader area")

    end = min(first, MAX_END_SECTOR)
    loader = raw[LOADER_SECTOR * SECTOR: end * SECTOR]
    if not any(loader):
        raise CaptureError(f"sectors {LOADER_SECTOR} to {end} are all zeros: no bootloader there")
    # Trailing zero sectors are what an empty image already has: drop them.
    stripped = loader.rstrip(b"\x00")
    loader = loader[: -(-len(stripped) // SECTOR) * SECTOR] if stripped else loader[:SECTOR]

    return {
        "partitions": parts,
        "first_partition": first,
        "loader": loader,
        # Allwinner boot0 carries this magic: the spec's marker for the clones this image must not go on.
        "allwinner": b"eGON.BT0" in raw[: MAX_END_SECTOR * SECTOR],
    }


def main(argv) -> int:
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    with open(argv[1], "rb") as f:
        raw = f.read()
    try:
        info = parse(raw)
    except CaptureError as err:
        print(f"capture_mbr: {err}", file=sys.stderr)
        return 1
    with open(argv[2], "wb") as f:
        f.write(info["loader"])
    for p in info["partitions"]:
        print(f"partition {p['number']} type=0x{p['type']:02x} start={p['start']} size={p['size']}")
    print(f"first_partition={info['first_partition']}")
    print(f"loader_bytes={len(info['loader'])}")
    print(f"allwinner={'yes' if info['allwinner'] else 'no'}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

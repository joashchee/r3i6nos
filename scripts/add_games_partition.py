#!/usr/bin/env python3
"""add_games_partition.py <mbr-file> <disk-bytes>

Edits a 512-byte MBR (a file holding sector 0 of a flashed r3i6nos card) in place: adds partition 3,
type 0x07 (exFAT), from just after ROOTFS to the end of the disk. flash-macos.sh --games writes it
back and formats the partition as EASYROMS, so one card holds both the system and the games.

Refuses anything that is not an untouched r3i6nos MBR: wrong disk signature, partitions 1 and 2
missing, partition 3 or 4 already used, or less than 1 GiB left over. Prints "<start> <sectors>".
Standard library only.
"""

import sys

DISK_SIGNATURE = 0x52333669          # scripts/image-layout.sh
ALIGN = 8192                         # sectors: 4 MiB, a multiple of every SD erase block
MIN_SECTORS = 2 * 1024 * 1024        # 1 GiB
MAX_LBA = 0xFFFFFFFF                 # MBR entries hold 32-bit sector counts (2 TiB)


class GamesError(Exception):
    pass


def entry(mbr, n):
    off = 446 + 16 * n
    return mbr[off:off + 16]


def add_games_partition(mbr: bytes, disk_bytes: int):
    if len(mbr) != 512 or mbr[510:512] != b"\x55\xaa":
        raise GamesError("not an MBR (no 0x55AA signature)")
    if int.from_bytes(mbr[440:444], "little") != DISK_SIGNATURE:
        raise GamesError("this disk is not an r3i6nos image (disk signature differs)")
    for n in (0, 1):
        if entry(mbr, n)[4] == 0:
            raise GamesError(f"partition {n + 1} is missing")
    for n in (2, 3):
        if any(entry(mbr, n)):
            raise GamesError(f"partition {n + 1} is already in use")
    p2 = entry(mbr, 1)
    end2 = int.from_bytes(p2[8:12], "little") + int.from_bytes(p2[12:16], "little")
    start = -(-end2 // ALIGN) * ALIGN
    total = disk_bytes // 512
    size = min(total, MAX_LBA) - start
    if size < MIN_SECTORS:
        raise GamesError(f"only {max(size, 0) * 512 // 1000000} MB is free after ROOTFS: not enough for a games partition")
    new = (b"\x00" + b"\xfe\xff\xff" + b"\x07" + b"\xfe\xff\xff"
           + start.to_bytes(4, "little") + size.to_bytes(4, "little"))
    out = bytearray(mbr)
    out[446 + 32:446 + 48] = new
    return bytes(out), start, size


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        with open(argv[1], "rb") as f:
            mbr = f.read()
        new, start, size = add_games_partition(mbr, int(argv[2]))
    except (GamesError, ValueError, OSError) as err:
        print(f"add_games_partition: {err}", file=sys.stderr)
        return 1
    with open(argv[1], "wb") as f:
        f.write(new)
    print(start, size)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

#!/usr/bin/env python3
"""Packs this folder's project.godot, main.tscn and main.gd into Hello_Godot.pck, a Godot 3 pack (format 1), with the standard library only.

    python3 build-pck.py [OUT.pck]      (default: ../samples/Godot/Hello_Godot.pck)

The pack is what an export makes for a game with no imported assets: the files are stored as they are (text), so the 3.6 player reads them straight.
Same input, same bytes (no timestamps), so a test can compare the committed pack with a fresh one. Original sample, public domain (CC0).
"""
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FILES = ["project.godot", "main.tscn", "main.gd"]
ENGINE = (3, 6, 0)
ALIGN = 16


def pad(n, a):
    return (n + a - 1) // a * a


def build(folder=HERE):
    items = []
    for name in FILES:
        with open(os.path.join(folder, name), "rb") as f:
            items.append(("res://" + name, f.read()))
    head = struct.pack("<4sIIII", b"GDPC", 1, *ENGINE) + b"\0" * 64 + struct.pack("<I", len(items))
    table = 0
    for path, _ in items:
        table += 4 + pad(len(path.encode()), 4) + 8 + 8 + 16
    offset = pad(len(head) + table, ALIGN)
    entries, blobs = b"", b""
    for path, data in items:
        raw = path.encode()
        entries += struct.pack("<I", pad(len(raw), 4)) + raw.ljust(pad(len(raw), 4), b"\0")
        entries += struct.pack("<QQ", offset + len(blobs), len(data)) + hashlib.md5(data).digest()
        blobs += data + b"\0" * (pad(len(data), ALIGN) - len(data))
    out = head + entries
    return out + b"\0" * (offset - len(out)) + blobs


if __name__ == "__main__":
    dest = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "samples", "Godot", "Hello_Godot.pck")
    with open(dest, "wb") as f:
        f.write(build())
    print("wrote", os.path.normpath(dest))

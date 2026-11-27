#!/usr/bin/env python3
"""Generate a monochrome PICO-8 cartridge preview without dependencies."""
import struct
import zlib
from pathlib import Path

SIZE = 96
def pixel(x, y):
    # Cartridge silhouette, with an inset label and eight pixel contacts.
    body = (5 <= x < 91 and 9 <= y < 80
            and not (y < 15 and (x < 11 or x >= 85)))
    label = 15 <= x < 81 and 19 <= y < 59
    notch = 36 <= x < 60 and 72 <= y < 80
    contact = 81 <= y < 87 and 12 <= x < 84 and (x - 12) % 9 < 6
    # A pixel-art figure eight in the negative-space label.
    eight = ((38 <= x < 58 and (25 <= y < 29 or 37 <= y < 41 or 49 <= y < 53))
             or ((34 <= x < 38 or 58 <= x < 62) and 29 <= y < 49))
    return (body and not label and not notch) or contact or eight

def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)

raw = b"".join(b"\0" + b"".join(bytes((255, 255, 255, 255 if pixel(x, y) else 0))
                                 for x in range(SIZE)) for y in range(SIZE))
png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
Path(__file__).resolve().parents[1].joinpath("assets/pane.png").write_bytes(png)

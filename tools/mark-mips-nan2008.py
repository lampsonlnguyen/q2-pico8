#!/usr/bin/env python3
"""Mark the experimental Q2 executable with its intended NaN2008 ELF ABI.

All game objects are compiled with -mnan=2008 and -mfp64. Ubuntu's GCC
startup/runtime archive objects carry legacy ELF metadata, so GNU ld emits
legacy e_flags when linking with --no-warn-mismatch. Q2 requires NaN2008.
This restores the ELF header flag; it does NOT rebuild mixed-ABI library code
or certify that its floating-point corner cases are compatible.
"""
import pathlib
import struct
import sys

EF_MIPS_NAN2008 = 0x00000400
EF_MIPS_ARCH_32R2 = 0x70000000

def mark(filename):
    path = pathlib.Path(filename)
    raw = bytearray(path.read_bytes())
    if len(raw) < 52 or raw[:6] != b"\x7fELF\x01\x01":
        raise ValueError("Not a 32-bit little-endian ELF")
    if struct.unpack_from("<H", raw, 18)[0] != 8:
        raise ValueError("Not a MIPS executable")
    flags = struct.unpack_from("<I", raw, 36)[0]
    if (flags & 0xF0000000) != EF_MIPS_ARCH_32R2:
        raise ValueError(f"Unexpected MIPS CPU flags: 0x{flags:08x}")
    struct.pack_into("<I", raw, 36, flags | EF_MIPS_NAN2008)
    path.write_bytes(raw)
    print(f"NaN2008 ELF header: 0x{flags:08x} -> 0x{flags | EF_MIPS_NAN2008:08x}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: mark-mips-nan2008.py ELF")
    mark(sys.argv[1])

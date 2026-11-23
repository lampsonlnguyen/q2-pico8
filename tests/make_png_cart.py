#!/usr/bin/env python3
"""Write an original test .p8.png cart (CC0): the Lua of a .p8 text cart,
stored uncompressed at 0x4300 of the ROM, hidden in the two low bits of each
A, R, G, B channel of a 160x205 picture -- the layout FAKE-08 decodes and
the one q2-pico8-itch writes for web-export carts.

Usage: make_png_cart.py <source.p8> <out.p8.png> [label colour hex]
"""
import sys
from PIL import Image, ImageDraw


def lua_of(p8_text):
    lines, inside = [], False
    for line in p8_text.splitlines():
        if line.startswith("__") and line.endswith("__"):
            inside = line == "__lua__"
            continue
        if inside:
            lines.append(line)
    return "\n".join(lines).encode("ascii")


def main():
    src, out = sys.argv[1], sys.argv[2]
    colour = sys.argv[3] if len(sys.argv) > 3 else "29adff"
    code = lua_of(open(src).read())
    rom = bytearray(0x8000)
    assert len(code) < 0x8000 - 0x4300
    rom[0x4300:0x4300 + len(code)] = code
    img = Image.new("RGBA", (160, 205), (0x2a, 0x2d, 0x3a, 255))
    d = ImageDraw.Draw(img)
    d.rectangle([12, 20, 147, 155], fill=(0x10, 0x10, 0x14, 255))
    rgb = tuple(int(colour[i:i + 2], 16) for i in (0, 2, 4))
    d.rectangle([16, 24, 143, 151], fill=rgb + (255,))
    d.ellipse([48, 56, 111, 119], fill=(0xff, 0xf1, 0xe8, 255))
    px = img.load()
    for i in range(160 * 205):
        b = rom[i] if i < len(rom) else 0
        x, y = i % 160, i // 160
        r, g, bl, a = px[x, y]
        px[x, y] = ((r & ~3) | (b >> 4 & 3), (g & ~3) | (b >> 2 & 3),
                    (bl & ~3) | (b & 3), (a & ~3) | (b >> 6 & 3))
    img.save(out)


if __name__ == "__main__":
    main()

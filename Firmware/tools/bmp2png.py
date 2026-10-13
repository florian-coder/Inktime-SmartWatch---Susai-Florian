#!/usr/bin/env python3
"""Convert a 32-bit BMP (as written by SDL_SaveBMP) to PNG. usage: bmp2png.py in.bmp out.png"""
import struct
import sys
import zlib


def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()
    off = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp != 32:
        raise SystemExit(f"unsupported bpp {bpp}")
    rows = []
    for y in range(abs(h)):
        sy = abs(h) - 1 - y if h > 0 else y
        line = data[off + sy * w * 4: off + (sy + 1) * w * 4]
        rgb = bytearray(b"\x00")
        for x in range(w):
            b, g, r = line[4 * x], line[4 * x + 1], line[4 * x + 2]
            rgb += bytes((r, g, b))
        rows.append(bytes(rgb))

    def chunk(tag, payload):
        c = struct.pack(">I", len(payload)) + tag + payload
        return c + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)

    with open(dst, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, abs(h), 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(b"".join(rows), 9)))
        f.write(chunk(b"IEND", b""))


if __name__ == "__main__":
    main()

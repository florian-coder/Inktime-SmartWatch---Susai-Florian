#!/usr/bin/env python3
"""Convert P4 PBM screenshots from the simulator into PNG (e-paper colours, optional scale).

usage: pbm2png.py in.pbm out.png [--scale N] [--diff]
"""
import struct
import sys
import zlib

PAPER = (0xE9, 0xE7, 0xE0)
INK = (0x1C, 0x1C, 0x1C)
DIFF = (0xE0, 0x20, 0x20)


def read_pbm(path):
    with open(path, "rb") as f:
        data = f.read()
    parts = data.split(maxsplit=3)
    if parts[0] != b"P4":
        raise ValueError(f"{path}: not a P4 PBM")
    w, h = int(parts[1]), int(parts[2])
    raw = parts[3]
    stride = (w + 7) // 8
    rows = []
    for y in range(h):
        row = raw[y * stride:(y + 1) * stride]
        rows.append([(row[x // 8] >> (7 - x % 8)) & 1 for x in range(w)])
    return w, h, rows


def write_png(path, w, h, rows, scale, ink):
    lines = []
    for y in range(h):
        line = bytearray([0])
        for x in range(w):
            line += bytes(ink if rows[y][x] else PAPER) * scale
        for _ in range(scale):
            lines.append(bytes(line))

    def chunk(tag, payload):
        c = struct.pack(">I", len(payload)) + tag + payload
        return c + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(lines), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    args = sys.argv[1:]
    scale = 1
    ink = INK
    if "--scale" in args:
        i = args.index("--scale")
        scale = int(args[i + 1])
        del args[i:i + 2]
    if "--diff" in args:
        args.remove("--diff")
        ink = DIFF
    if len(args) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    w, h, rows = read_pbm(args[0])
    write_png(args[1], w, h, rows, scale, ink)
    return 0


if __name__ == "__main__":
    sys.exit(main())

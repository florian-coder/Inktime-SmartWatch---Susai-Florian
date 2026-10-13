#!/usr/bin/env python3
"""Tile PBM screenshots into one PNG for review (e.g. all golden images).

usage: contact_sheet.py out.png a.pbm b.pbm ... [--cols N] [--scale N]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pbm2png import INK, PAPER, read_pbm, write_png  # noqa: E402

GAP = 8
BORDER = (0x80, 0x80, 0x80)


def main():
    args = sys.argv[1:]
    cols, scale = 4, 1
    for flag in ("--cols", "--scale"):
        if flag in args:
            i = args.index(flag)
            val = int(args[i + 1])
            del args[i:i + 2]
            if flag == "--cols":
                cols = val
            else:
                scale = val
    if len(args) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    out, files = args[0], args[1:]
    images = [read_pbm(f) for f in files]
    w, h = images[0][0], images[0][1]
    rows_n = (len(images) + cols - 1) // cols
    W = cols * (w + GAP) + GAP
    H = rows_n * (h + GAP) + GAP
    # Build an RGB canvas as rows of colour tuples, then reuse the PNG writer via a palette trick:
    canvas = [[BORDER] * W for _ in range(H)]
    for n, (_, _, px) in enumerate(images):
        ox = GAP + (n % cols) * (w + GAP)
        oy = GAP + (n // cols) * (h + GAP)
        for y in range(h):
            row = canvas[oy + y]
            for x in range(w):
                row[ox + x] = INK if px[y][x] else PAPER
    # write_png expects 0/1 rows; encode colours directly instead.
    import struct
    import zlib
    raw = b"".join(
        b"\x00" + b"".join(bytes(c) * scale for c in row)
        for row in canvas for _ in range(scale)
    )

    def chunk(tag, payload):
        c = struct.pack(">I", len(payload)) + tag + payload
        return c + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)

    with open(out, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", W * scale, H * scale, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))
    _ = write_png  # keep the shared import explicit for readers
    return 0


if __name__ == "__main__":
    sys.exit(main())

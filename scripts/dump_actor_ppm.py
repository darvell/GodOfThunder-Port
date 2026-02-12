#!/usr/bin/env python3

"""
Dump an ACTOR* resource (from ./assets/ACTOR### or extracted from GOTRES.DAT)
into a simple PPM image for quick inspection.

Actor resource format (based on game source in src/_g1/1_file.c + 1_image.c):
- 20 frames of 16x16 pixels, 8-bit palette indices: 20 * 256 = 5120 bytes
- followed by 40 bytes of ACTOR static fields (some assets have extra padding)

This script renders the 20 frames into a 5x4 grid.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path


def read_palette_rgb(path: Path) -> list[tuple[int, int, int]]:
    data = path.read_bytes()
    if len(data) != 768:
        raise SystemExit(f"palette must be 768 bytes (got {len(data)}): {path}")
    pal: list[tuple[int, int, int]] = []
    for i in range(256):
        r = data[i * 3 + 0]
        g = data[i * 3 + 1]
        b = data[i * 3 + 2]
        pal.append((r, g, b))
    return pal


def write_ppm(path: Path, w: int, h: int, rgb: bytes) -> None:
    if len(rgb) != w * h * 3:
        raise SystemExit("rgb buffer size mismatch")
    header = f"P6\n{w} {h}\n255\n".encode("ascii")
    path.write_bytes(header + rgb)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--actor", required=True, type=Path, help="Path to ACTOR resource (e.g. assets/ACTOR100)")
    ap.add_argument("--palette", default=Path("assets/PALETTE"), type=Path, help="Path to 768-byte palette resource")
    ap.add_argument("--out", required=True, type=Path, help="Output PPM path")
    ap.add_argument("--cols", default=5, type=int, help="Grid columns (default: 5 for 20 frames)")
    ap.add_argument("--transparent", action="store_true", help="Render indices 0 and 15 as black (matches mask rules)")
    args = ap.parse_args()

    actor_data = args.actor.read_bytes()
    if len(actor_data) < 5120:
        raise SystemExit(f"actor too small (need >= 5120 bytes): {args.actor}")

    pal = read_palette_rgb(args.palette)

    frames = 20
    fw = fh = 16
    cols = args.cols
    rows = (frames + cols - 1) // cols
    out_w = cols * fw
    out_h = rows * fh

    # Render
    out = bytearray(out_w * out_h * 3)

    for fi in range(frames):
        fx = (fi % cols) * fw
        fy = (fi // cols) * fh
        frame = actor_data[fi * 256 : (fi + 1) * 256]
        for y in range(fh):
            for x in range(fw):
                idx = frame[y * fw + x]
                if args.transparent and idx in (0, 15):
                    r, g, b = (0, 0, 0)
                else:
                    r, g, b = pal[idx]
                oi = ((fy + y) * out_w + (fx + x)) * 3
                out[oi + 0] = r
                out[oi + 1] = g
                out[oi + 2] = b

    args.out.parent.mkdir(parents=True, exist_ok=True)
    write_ppm(args.out, out_w, out_h, bytes(out))

    print(f"Wrote {args.out} ({out_w}x{out_h}) from {args.actor}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


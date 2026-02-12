#!/usr/bin/env python3

"""
Extract and decompress chunks from the game's GRAPHICS.GOT container.

Based on reverse engineering of GOT.EXE routines:
- Descriptor table entry size: 0x0E bytes
- Compression types:
  - 0: raw
  - 1: LZSS-like (12-bit offset, 4-bit length + 2), bitflags LSB-first
  - 2: RLE (byte-run), terminator byte 0

File layout (for standalone GRAPHICS.GOT on disk):
- uint16le chunk_count
- chunk_count * 0x0E bytes descriptors
- chunk payloads at (descriptor.file_offset) (offset appears absolute in file)

This tool writes one chunk to disk. It does not interpret pixel formats.
"""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class ChunkDesc:
    comp_type: int
    file_offset: int
    out_size: int
    in_size: int
    width: int
    height: int


def u16(data: bytes, off: int) -> int:
    return struct.unpack_from("<H", data, off)[0]


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def parse_desc_table(blob: bytes) -> tuple[list[ChunkDesc], int]:
    if len(blob) < 2:
        raise SystemExit("GRAPHICS.GOT too small")
    n = u16(blob, 0)
    table_off = 2
    entry_sz = 0x0E
    table_sz = n * entry_sz
    if len(blob) < table_off + table_sz:
        raise SystemExit("GRAPHICS.GOT truncated descriptor table")

    descs: list[ChunkDesc] = []
    for i in range(n):
        o = table_off + i * entry_sz
        comp_type = u16(blob, o + 0x00)
        file_offset = u32(blob, o + 0x02)
        out_size = u16(blob, o + 0x06)
        in_size = u16(blob, o + 0x08)
        width = u16(blob, o + 0x0A)
        height = u16(blob, o + 0x0C)
        descs.append(
            ChunkDesc(
                comp_type=comp_type,
                file_offset=file_offset,
                out_size=out_size,
                in_size=in_size,
                width=width,
                height=height,
            )
        )
    return descs, table_off + table_sz


def decompress_lzss12(src: bytes, out_size: int) -> bytes:
    # Matches GOT.EXE sub_1C85A.
    dst = bytearray(out_size)
    si = 0
    di = 0
    flags = 0
    bits_left = 0

    while di < out_size:
        if bits_left == 0:
            if si >= len(src):
                raise SystemExit("lzss12: truncated input (need flags byte)")
            flags = src[si]
            si += 1
            bits_left = 8

        if flags & 1:
            if si >= len(src):
                raise SystemExit("lzss12: truncated literal")
            dst[di] = src[si]
            si += 1
            di += 1
        else:
            if si + 2 > len(src):
                raise SystemExit("lzss12: truncated backref")
            word = src[si] | (src[si + 1] << 8)
            si += 2
            count = ((word >> 12) & 0x0F) + 2
            offset = word & 0x0FFF
            if offset == 0 or offset > di:
                raise SystemExit(f"lzss12: invalid offset {offset} at out={di}")
            for _ in range(count):
                if di >= out_size:
                    break
                dst[di] = dst[di - offset]
                di += 1

        flags >>= 1
        bits_left -= 1

    return bytes(dst)


def decompress_rle(src: bytes, out_size: int) -> bytes:
    # Matches GOT.EXE sub_1C8AF.
    dst = bytearray()
    si = 0
    while si < len(src) and len(dst) < out_size:
        b = src[si]
        si += 1
        if b == 0:
            break
        if b & 0x80:
            count = b & 0x7F
            if si >= len(src):
                raise SystemExit("rle: truncated run byte")
            val = src[si]
            si += 1
            dst.extend([val] * count)
        else:
            count = b
            if si + count > len(src):
                raise SystemExit("rle: truncated literal run")
            dst.extend(src[si : si + count])
            si += count

    if len(dst) < out_size:
        # Some chunks may rely on terminator rather than descriptor out_size.
        # Pad with zeros to keep the contract stable.
        dst.extend(b"\x00" * (out_size - len(dst)))
    return bytes(dst[:out_size])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", required=True, type=Path, help="Path to GRAPHICS.GOT")
    ap.add_argument("--chunk", required=True, type=int, help="Chunk index (0-based)")
    ap.add_argument("--out", required=True, type=Path, help="Output file path")
    ap.add_argument("--raw", action="store_true", help="Write compressed bytes without decompressing")
    ap.add_argument("--list", action="store_true", help="List chunks and exit")
    args = ap.parse_args()

    blob = args.file.read_bytes()
    descs, _data_base = parse_desc_table(blob)

    if args.list:
        for i, d in enumerate(descs):
            print(
                f"{i:4d} type={d.comp_type} off=0x{d.file_offset:08X} in={d.in_size} out={d.out_size} w={d.width} h={d.height}"
            )
        return 0

    if args.chunk < 0 or args.chunk >= len(descs):
        raise SystemExit(f"chunk out of range: {args.chunk} (0..{len(descs)-1})")

    d = descs[args.chunk]
    start = d.file_offset
    end = start + d.in_size
    if end > len(blob):
        raise SystemExit("chunk payload out of file bounds")

    payload = blob[start:end]
    if args.raw:
        out = payload
    else:
        if d.comp_type == 0:
            out = payload[: d.out_size]
            if len(out) < d.out_size:
                out = out + b"\x00" * (d.out_size - len(out))
        elif d.comp_type == 1:
            out = decompress_lzss12(payload, d.out_size)
        elif d.comp_type == 2:
            out = decompress_rle(payload, d.out_size)
        else:
            raise SystemExit(f"unknown comp_type: {d.comp_type}")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(out)
    print(f"Wrote {args.out} ({len(out)} bytes) from chunk {args.chunk} (type {d.comp_type})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


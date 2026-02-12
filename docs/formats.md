# File And Graphics Formats (Reverse Engineering Notes)

This doc is a living scratchpad for formats discovered from the shipped source
and from reverse engineering the original `GOT.EXE` in IDA.

## `GOTRES.DAT` (Resource Archive)

Implemented by the resource manager in `src/utility/res_*.c` and
`src/utility/res_man.h`.

- Archive contains up to 256 entries (`RES_MAX_ENTRIES`).
- Each entry has an 8-char name (stored as `char name[9]` for a NUL terminator),
  plus file offset, compressed length, original size, and a compression flag.
- The resource header table is XOR-obfuscated (see `res_encrypt`/`res_decrypt`).
- Payloads may be stored uncompressed (`key=0`) or compressed (`key!=0`) using
  the LZSS codec in `src/utility/lzss.c`.

### `PALETTE` (VGA DAC palette)

`assets/PALETTE` is a raw 768-byte blob:

- 256 entries
- Each entry: 3 bytes RGB
- Each channel appears 0..255 in the resource. The game shifts right by 2
  before programming the VGA DAC (DAC expects 0..63).

### `ACTOR###` (20x 16x16 paletted frames + metadata)

From `src/_g1/1_file.c:load_actor()` and `src/_g1/1_image.c:load_standard_actors()`
and confirmed by `assets/ACTOR*` sizes.

Layout:

- Offset `0x0000..0x13FF` (5120 bytes): 20 frames
  - each frame is 16x16, 8-bit palette indices (256 bytes)
  - frame order: `frame_index * 256`
- Offset `0x1400..` (at least 40 bytes): the first 40 bytes of the `ACTOR`
  struct (static configuration fields).

Notes:

- Many `assets/ACTOR*` files are 5200 bytes, which implies an extra 40 bytes of
  padding/unused data after the 40-byte struct prefix.

Mask rule used by the game when building masked sprites in Mode X:

- Pixels with palette index `0` or `15` are treated as transparent
  (`pixel != 0 && pixel != 15` in `make_mask()`).

### `BPICS#`, `OBJECTS`, `.pic`, `.rim` (16x16 tiles, 262-byte records)

Many resources are arrays of 262-byte “tiles”:

- 6-byte header (three little-endian `uint16`)
  - tools write: `4`, `16`, `0`
  - likely means: width in “addresses” (4 bytes, Mode X), height (16), flags (0)
- 256 bytes of pixel data for a 16x16 tile stored in Mode X-friendly order.

The import tool `src/utility/xp_imp.c` writes tiles in this order:

- Iterate planes `r=0..3`
- For each plane, for each row `y=0..15`
- For each x in `0..15` stepping by 4, write pixel at `x+r`

That corresponds to the way Mode X stores pixels across 4 planes.

`.rim` files appear to be 4 tiles back-to-back (4 * 262). After writing each
tile’s 256 bytes, the tool XORs the tile data byte `i` with `i`:

- `rim_data[i] ^= i`

This is an obfuscation step specific to `.rim` (not used for `.pic`).

## `GRAPHICS.GOT` (Chunked Graphics Container)

This format is used by the launcher/menu binary (`GOT.EXE`) rather than the
episode binaries. The source code for `GOT.EXE` isn’t in this repo, so this
section is based on IDA analysis of the original executable.

### High level

`GRAPHICS.GOT` begins with a chunk descriptor table. Each chunk payload is then
read and optionally decompressed depending on the chunk’s compression type.

### Layout

- `uint16le chunk_count`
- `chunk_count * 0x0E` bytes of chunk descriptors
- chunk payloads, referenced by descriptor `file_offset`

Descriptor (14 bytes):

- `+0x00 uint16le compression_type`
  - `0`: raw
  - `1`: LZSS12 (12-bit offset, 4-bit length + 2)
  - `2`: RLE byte-run (terminator byte `0`)
- `+0x02 uint32le file_offset` (absolute file offset; base-offset overlay is a
  separate code path inside `GOT.EXE`)
- `+0x06 uint16le uncompressed_size`
- `+0x08 uint16le compressed_size`
- `+0x0A uint16le width` (strong hypothesis; returned by `FL_GetGraphicChunk`)
- `+0x0C uint16le height` (strong hypothesis)

### Compression: type 1 (LZSS12)

Algorithm matches IDA function at `0x1C85A`:

- Stream is controlled by a flags byte; bits are consumed LSB-first.
- If the bit is 1: literal byte follows.
- If the bit is 0: a 16-bit control word follows:
  - `count = ((word >> 12) & 0x0F) + 2`
  - `offset = word & 0x0FFF`
  - copy `count` bytes from `dst[-offset]` to current output, allowing overlap.

### Compression: type 2 (RLE)

Algorithm matches IDA function at `0x1C8AF`:

- Read a control byte `b`.
- If `b == 0`: end-of-stream.
- If `b & 0x80`: repeat the next byte `(b & 0x7F)` times.
- Else: copy `b` literal bytes.

### Extractor tool

See `scripts/extract_graphics_got.py` to list/extract chunks and decompress
them on the host.


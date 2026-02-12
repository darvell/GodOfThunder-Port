/* graphics_got.h - GRAPHICS.GOT container loader
 *
 * GRAPHICS.GOT is the launcher/menu graphics container from the original
 * GOT.EXE.  File format (reverse-engineered from GOT.EXE):
 *   uint16le  chunk_count
 *   chunk_count * 14-byte descriptors:
 *     uint16le  comp_type   (0=raw, 1=LZSS12, 2=RLE)
 *     uint32le  file_offset
 *     uint16le  out_size    (decompressed)
 *     uint16le  in_size     (compressed)
 *     uint16le  width
 *     uint16le  height
 *   payload data at file_offset ...
 */
#ifndef GRAPHICS_GOT_H
#define GRAPHICS_GOT_H

#include <stdint.h>

typedef struct {
    uint16_t comp_type;
    uint32_t file_offset;
    uint16_t out_size;
    uint16_t in_size;
    uint16_t width;
    uint16_t height;
} gg_chunk_desc_t;

typedef struct {
    uint8_t        *blob;       /* raw file data */
    long            blob_size;
    int             chunk_count;
    gg_chunk_desc_t *descs;     /* chunk_count entries */
} graphics_got_t;

/* Load and parse GRAPHICS.GOT from disk.  Returns 0 on success. */
int  gg_load(graphics_got_t *gg, const char *path);

/* Free all resources. */
void gg_free(graphics_got_t *gg);

/* Decompress chunk `idx` into caller-supplied buffer `out` (must be
   >= descs[idx].out_size bytes).  Returns decompressed size, or 0 on error. */
int  gg_decompress(const graphics_got_t *gg, int idx, uint8_t *out);

/* Convenience: allocate + decompress.  Caller must free() result. */
uint8_t *gg_decompress_alloc(const graphics_got_t *gg, int idx);

/* Convert a 768-byte VGA DAC palette (6-bit, 0-63) to 256*4 RGBA (8-bit). */
void gg_pal_to_rgba(const uint8_t *pal768, uint8_t rgba[256][4]);

/* Deplanarize a Mode-X fullscreen image from 4 plane buffers.
 * Each plane is (stride * lines) bytes.  Output is (stride*4 * lines) linear. */
void gg_deplanar(const uint8_t *planes[4], int stride, int lines,
                 uint8_t *linear);

#endif /* GRAPHICS_GOT_H */

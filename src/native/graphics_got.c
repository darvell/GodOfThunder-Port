/* graphics_got.c - GRAPHICS.GOT container loader
 *
 * Reverse-engineered from GOT.EXE decompression routines:
 *   sub_1C85A  (LZSS12 - 12-bit offset, 4-bit length+2)
 *   sub_1C8AF  (RLE    - byte-run, terminator 0)
 */
#include "graphics_got.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- helpers ---------- */

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---------- decompression ---------- */

/* LZSS12: flag byte (LSB first), bit=1 → literal, bit=0 → back-reference.
 * Back-ref: uint16le, lower 12 bits = offset, upper 4 bits + 2 = length.
 * Matches GOT.EXE sub_1C85A exactly. */
static int decompress_lzss12(const uint8_t *src, int in_size,
                             uint8_t *dst, int out_size) {
    int si = 0, di = 0;
    uint8_t flags = 0;
    int bits_left = 0;

    while (di < out_size) {
        if (bits_left == 0) {
            if (si >= in_size) break;
            flags = src[si++];
            bits_left = 8;
        }
        if (flags & 1) {
            /* literal byte */
            if (si >= in_size) break;
            dst[di++] = src[si++];
        } else {
            /* back-reference */
            if (si + 2 > in_size) break;
            uint16_t word = (uint16_t)src[si] | ((uint16_t)src[si+1] << 8);
            si += 2;
            int count  = ((word >> 12) & 0x0F) + 2;
            int offset = word & 0x0FFF;
            if (offset == 0 || offset > di) break;
            int j;
            for (j = 0; j < count && di < out_size; j++) {
                dst[di] = dst[di - offset];
                di++;
            }
        }
        flags >>= 1;
        bits_left--;
    }
    return di;
}

/* RLE: byte-run encoding.  Matches GOT.EXE sub_1C8AF.
 * byte == 0       → end
 * byte & 0x80     → repeat next byte (byte & 0x7F) times
 * else            → copy `byte` literal bytes */
static int decompress_rle(const uint8_t *src, int in_size,
                          uint8_t *dst, int out_size) {
    int si = 0, di = 0;
    while (si < in_size && di < out_size) {
        uint8_t b = src[si++];
        if (b == 0) break;
        if (b & 0x80) {
            int count = b & 0x7F;
            if (si >= in_size) break;
            uint8_t val = src[si++];
            int j;
            for (j = 0; j < count && di < out_size; j++)
                dst[di++] = val;
        } else {
            int count = b;
            int j;
            for (j = 0; j < count && si < in_size && di < out_size; j++)
                dst[di++] = src[si++];
        }
    }
    return di;
}

/* ---------- public API ---------- */

int gg_load(graphics_got_t *gg, const char *path) {
    FILE *f;
    long sz;
    int i;

    memset(gg, 0, sizeof(*gg));

    f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    gg->blob = (uint8_t *)malloc(sz);
    if (!gg->blob) { fclose(f); return -1; }
    if ((long)fread(gg->blob, 1, sz, f) != sz) {
        free(gg->blob); gg->blob = NULL;
        fclose(f); return -1;
    }
    fclose(f);
    gg->blob_size = sz;

    if (sz < 2) { gg_free(gg); return -1; }
    gg->chunk_count = rd16(gg->blob);

    if (sz < 2 + (long)gg->chunk_count * 14) { gg_free(gg); return -1; }

    gg->descs = (gg_chunk_desc_t *)calloc(gg->chunk_count, sizeof(gg_chunk_desc_t));
    if (!gg->descs) { gg_free(gg); return -1; }

    for (i = 0; i < gg->chunk_count; i++) {
        const uint8_t *p = gg->blob + 2 + i * 14;
        gg->descs[i].comp_type   = rd16(p + 0);
        gg->descs[i].file_offset = rd32(p + 2);
        gg->descs[i].out_size    = rd16(p + 6);
        gg->descs[i].in_size     = rd16(p + 8);
        gg->descs[i].width       = rd16(p + 10);
        gg->descs[i].height      = rd16(p + 12);
    }
    return 0;
}

void gg_free(graphics_got_t *gg) {
    free(gg->descs); gg->descs = NULL;
    free(gg->blob);  gg->blob  = NULL;
    gg->chunk_count = 0;
}

int gg_decompress(const graphics_got_t *gg, int idx, uint8_t *out) {
    const gg_chunk_desc_t *d;
    const uint8_t *src;

    if (idx < 0 || idx >= gg->chunk_count) return 0;
    d = &gg->descs[idx];
    if ((long)d->file_offset + d->in_size > gg->blob_size) return 0;

    src = gg->blob + d->file_offset;

    switch (d->comp_type) {
    case 0: /* raw */
        memcpy(out, src, d->out_size <= d->in_size ? d->out_size : d->in_size);
        return d->out_size;
    case 1: /* LZSS12 */
        return decompress_lzss12(src, d->in_size, out, d->out_size);
    case 2: /* RLE */
        return decompress_rle(src, d->in_size, out, d->out_size);
    default:
        return 0;
    }
}

uint8_t *gg_decompress_alloc(const graphics_got_t *gg, int idx) {
    uint8_t *buf;
    if (idx < 0 || idx >= gg->chunk_count) return NULL;
    buf = (uint8_t *)calloc(1, gg->descs[idx].out_size);
    if (!buf) return NULL;
    if (gg_decompress(gg, idx, buf) <= 0) { free(buf); return NULL; }
    return buf;
}

void gg_pal_to_rgba(const uint8_t *pal768, uint8_t rgba[256][4]) {
    int i;
    for (i = 0; i < 256; i++) {
        /* VGA DAC: 6-bit (0-63) → 8-bit (0-255) */
        rgba[i][0] = (pal768[i*3+0] * 255 + 31) / 63;
        rgba[i][1] = (pal768[i*3+1] * 255 + 31) / 63;
        rgba[i][2] = (pal768[i*3+2] * 255 + 31) / 63;
        rgba[i][3] = 255;
    }
}

void gg_deplanar(const uint8_t *planes[4], int stride, int lines,
                 uint8_t *linear) {
    int y, x, p;
    int width = stride * 4;
    for (y = 0; y < lines; y++) {
        for (x = 0; x < width; x++) {
            p = x & 3;            /* which plane */
            linear[y * width + x] = planes[p][y * stride + (x >> 2)];
        }
    }
}

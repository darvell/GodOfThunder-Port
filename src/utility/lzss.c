#include "lzss.h"

int lzss_read_uint16le(const char far* src, long* size, unsigned int* value);

int lzss_read_uint16le(const char far* src, long* size, unsigned int* value) {
  if (*size < 2) {
    return 0;
  }

  // Cast to unsigned to avoid sign-extension when `char` is signed.
  *value = (unsigned int)(unsigned char)src[0]
    | ((unsigned int)(unsigned char)src[1] << 8);
  *size -= 2;

  return 1;
}

long lzss_decompress(char far* src, char far* dst, long size) {
  char data, x;
  unsigned int decompressed_size, ignored, written, control
  , upper_four_bits, count, offset;
  long remaining, i, b, j;

  remaining = size;
  written = 0;
  i = 0;

  // Each file's data begins with a UINT16LE value holding the
  // decompressed size, in bytes,
  if (!lzss_read_uint16le(src, &remaining, &decompressed_size)) {
    return 0;
  }
  src += 2;

  // followed by another UINT16LE value of unknown purpose
  // which should be ignored (the value is always 0x0001.)
  if (!lzss_read_uint16le(src, &remaining, &ignored)) {
    return 0;
  }
  src += 2;

  // The rest of the data decompresses as follows:

  // If the amount of data decompressed matches the target size, finish. Otherwise:
  while (written < decompressed_size) {
    if (remaining <= 0) {
      break;
    }

    // Read a byte from the input data
    data = *src;
    src += 1;
    remaining -= 1;


    // For each bit in the previous byte, from the least significant to the most:
    for (b = 0; b < 8; b += 1) {
      if (written >= decompressed_size) {
        break;
      }

      // If the bit is 1, copy a byte unchanged from the input data to the output
      if (((data >> b) & 0x1)) {
        if (remaining <= 0) {
          return 0;
        }
        dst[written++] = *src;
        src += 1;
        remaining -= 1;
      }
      // Otherwise the bit is zero:
      else {
        // Read a UINT16LE
        if (!lzss_read_uint16le(src, &remaining, &control)) {
          return 0;
        }
        src += 2;

        i += 2;

        // Add two to the upper (most significant) four bits, and
        // treat this value as the LZSS "count"
        upper_four_bits = control >> 12;
        count = upper_four_bits + 2;

        // Take the lower 12 bits and treat the value as the LZSS "offset"
        offset = control & 0xFFF;

        // Look back "offset" bytes into the newly decompressed data

        /**
         * Copy "count" bytes from here to the end of the newly decompressed data.
         * Take note that as each byte is copied to its destination, that new byte
         * may later become a source byte in this same copy operation. For example,
         * if "offset" is 1 (i.e. look back one byte) and the counter is 15, then
         * the last byte will be copied 17 times (15 + 2 = 17). This is because as
         * each byte is copied, it becomes the source byte for the next copy cycle.
         */
        for (j = 0; j < count; j += 1) {
          x = dst[written - offset];
          dst[written++] = x;
        }
      }
    }
  }

  return (long)decompressed_size;
}

long lzss_compress(long origsize, char far* src, char far* dst) {
  long pos;
  unsigned char* out;
  unsigned char* flags_ptr;
  unsigned char flags;
  int bit;

  if (origsize < 0 || origsize > 0xFFFF) {
    return 0;
  }

  out = (unsigned char*)dst;

  // Header: decompressed size (uint16le), then a constant 0x0001.
  out[0] = (unsigned char)(origsize & 0xFF);
  out[1] = (unsigned char)((origsize >> 8) & 0xFF);
  out[2] = 0x01;
  out[3] = 0x00;
  out += 4;

  pos = 0;
  while (pos < origsize) {
    flags_ptr = out++;
    flags = 0;

    for (bit = 0; bit < 8 && pos < origsize; bit++) {
      unsigned int best_len = 0;
      unsigned int best_off = 0;
      long window_start;
      long j;

      // Search up to 0xFFF bytes back, for a match up to 17 bytes long.
      window_start = pos - 0xFFF;
      if (window_start < 0) {
        window_start = 0;
      }

      for (j = pos - 1; j >= window_start; j--) {
        unsigned int len = 0;
        unsigned int off = (unsigned int)(pos - j);

        // Limit the back-reference distance to 12 bits.
        if (off == 0 || off > 0xFFF) {
          continue;
        }

        while (len < 17 && (pos + len) < origsize && src[j + len] == src[pos + len]) {
          len++;
        }

        if (len > best_len) {
          best_len = len;
          best_off = off;
          if (best_len == 17) {
            break;
          }
        }
      }

      if (best_len >= 2) {
        // Back-reference: control word packs (len-2) into upper 4 bits and offset into lower 12 bits.
        unsigned int control = ((best_len - 2) << 12) | (best_off & 0x0FFF);
        *out++ = (unsigned char)(control & 0xFF);
        *out++ = (unsigned char)((control >> 8) & 0xFF);
        pos += best_len;
      }
      else {
        // Literal
        flags |= (unsigned char)(1U << bit);
        *out++ = (unsigned char)src[pos++];
      }
    }

    *flags_ptr = flags;
  }

  return (long)(out - (unsigned char*)dst);
}

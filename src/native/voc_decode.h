#ifndef VOC_DECODE_H
#define VOC_DECODE_H

#include <stddef.h>
#include <stdint.h>

/* Decode a Creative Voice File (VOC) stored in memory to signed 16-bit PCM.
 *
 * - Supports the common VOC blocks used for GOT sound effects:
 *   0x01 (sound data), 0x02 (sound continue), 0x03 (silence),
 *   0x05 (text), 0x06 (repeat), 0x07 (end repeat), 0x00 (terminator).
 * - Only 8-bit unsigned PCM is supported (codec 0). Other codecs fail.
 *
 * Returns 1 on success, 0 on failure. On success, *out_pcm is malloc()'d and
 * must be free()'d by the caller.
 */
int voc_decode(
  const uint8_t* data,
  size_t len,
  int16_t** out_pcm,
  uint32_t* out_samples,
  uint32_t* out_rate);

#endif /* VOC_DECODE_H */


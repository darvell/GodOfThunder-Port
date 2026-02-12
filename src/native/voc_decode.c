#include "voc_decode.h"

#include <stdlib.h>
#include <string.h>

/* VOC format reference:
 * https://moddingwiki.shikadi.net/wiki/VOC_Format
 *
 * We only need enough for GOT's sound effects: 8-bit unsigned PCM blocks.
 */

enum {
  VOC_BLOCK_TERMINATOR = 0x00,
  VOC_BLOCK_SOUND_DATA = 0x01,
  VOC_BLOCK_SOUND_CONT = 0x02,
  VOC_BLOCK_SILENCE    = 0x03,
  VOC_BLOCK_TEXT       = 0x05,
  VOC_BLOCK_REPEAT     = 0x06,
  VOC_BLOCK_END_REPEAT = 0x07
};

enum {
  VOC_CODEC_PCM_U8 = 0
};

enum { MAX_NESTED_REPEATS = 8 };

static uint16_t rd_le16(const uint8_t* p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_le24(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

/* Inverse of the SB DSP "time constant" formula used in src/digisnd/digisnd.c:
 * timeValue = 256 - (1000000 / sampleRate)
 * => sampleRate = 1000000 / (256 - timeValue)
 */
static uint32_t timeconst_to_rate(uint8_t timeconst) {
  uint32_t denom = (uint32_t)256u - (uint32_t)timeconst;
  if (denom == 0) {
    return 0;
  }
  return (uint32_t)(1000000u / denom);
}

static int ensure_i16_capacity(int16_t** buf, uint32_t* cap, uint32_t need) {
  int16_t* nbuf;
  uint32_t ncap;

  if (need <= *cap) {
    return 1;
  }

  ncap = (*cap == 0) ? 4096u : *cap;
  while (ncap < need) {
    /* Guard overflow */
    if (ncap > 0x7fffffffu / 2u) {
      return 0;
    }
    ncap *= 2u;
  }

  nbuf = (int16_t*)realloc(*buf, (size_t)ncap * sizeof(int16_t));
  if (!nbuf) {
    return 0;
  }

  *buf = nbuf;
  *cap = ncap;
  return 1;
}

static uint32_t resample_linear_pcm16(
  const int16_t* src,
  uint32_t src_frames,
  uint32_t src_rate,
  uint32_t dst_rate,
  int16_t* dst,
  uint32_t dst_cap)
{
  uint32_t pos_fp;
  uint32_t step_fp;
  uint32_t out_frames;

  if (!src || src_frames == 0) {
    return 0;
  }
  if (src_rate == 0 || dst_rate == 0) {
    return 0;
  }
  if (dst_rate == src_rate) {
    if (src_frames > dst_cap) {
      src_frames = dst_cap;
    }
    memcpy(dst, src, (size_t)src_frames * sizeof(int16_t));
    return src_frames;
  }

  step_fp = (uint32_t)(((uint32_t)src_rate << 16) / (uint32_t)dst_rate);
  if (step_fp == 0) {
    return 0;
  }

  pos_fp = 0;
  out_frames = 0;
  while (out_frames < dst_cap) {
    uint32_t idx = pos_fp >> 16;
    uint32_t frac = pos_fp & 0xffffu;
    int32_t s0, s1;
    int32_t smp;

    if (idx >= src_frames) {
      break;
    }

    s0 = (int32_t)src[idx];
    if (idx + 1u < src_frames) {
      s1 = (int32_t)src[idx + 1u];
    } else {
      s1 = s0;
    }

    smp = (int32_t)((s0 * (int32_t)(65536u - frac) + s1 * (int32_t)frac) >> 16);
    dst[out_frames++] = (int16_t)smp;

    pos_fp += step_fp;
  }

  return out_frames;
}

static int append_pcm_u8_as_s16(
  const uint8_t* src_u8,
  uint32_t src_len,
  uint32_t src_rate,
  uint32_t out_rate,
  int16_t** out_pcm,
  uint32_t* out_frames,
  uint32_t* out_cap)
{
  uint32_t i;
  int16_t* tmp;
  uint32_t tmp_frames;

  if (!src_u8 || src_len == 0) {
    return 1;
  }
  if (src_rate == 0 || out_rate == 0) {
    return 0;
  }

  /* Convert to signed 16-bit at source rate first. */
  tmp = (int16_t*)malloc((size_t)src_len * sizeof(int16_t));
  if (!tmp) {
    return 0;
  }
  for (i = 0; i < src_len; i++) {
    int v = (int)src_u8[i] - 128;
    tmp[i] = (int16_t)(v << 8);
  }
  tmp_frames = src_len;

  if (src_rate == out_rate) {
    if (!ensure_i16_capacity(out_pcm, out_cap, *out_frames + tmp_frames)) {
      free(tmp);
      return 0;
    }
    memcpy(*out_pcm + *out_frames, tmp, (size_t)tmp_frames * sizeof(int16_t));
    *out_frames += tmp_frames;
    free(tmp);
    return 1;
  }

  /* Resample to the overall output VOC rate so we can return a single stream. */
  {
    /* 32-bit is sufficient for GOT assets (frames are small). */
    uint32_t est = (uint32_t)(((tmp_frames * out_rate) + (src_rate - 1u)) / src_rate);
    uint32_t got;

    if (est == 0) {
      free(tmp);
      return 1;
    }

    if (!ensure_i16_capacity(out_pcm, out_cap, *out_frames + est)) {
      free(tmp);
      return 0;
    }

    got = resample_linear_pcm16(
      tmp, tmp_frames, src_rate, out_rate,
      *out_pcm + *out_frames, est);

    *out_frames += got;
  }

  free(tmp);
  return 1;
}

static int append_silence(
  uint32_t silence_samples_minus1,
  uint32_t silence_rate,
  uint32_t out_rate,
  int16_t** out_pcm,
  uint32_t* out_frames,
  uint32_t* out_cap)
{
  uint32_t in_samples;
  uint32_t out_silence;

  if (silence_rate == 0 || out_rate == 0) {
    return 0;
  }

  /* Per VOC spec, duration is in samples - 1. */
  in_samples = silence_samples_minus1 + 1u;

  if (silence_rate == out_rate) {
    out_silence = in_samples;
  } else {
    /* Convert duration to output samples with rounding. */
    out_silence = (uint32_t)(((in_samples * out_rate) + (silence_rate / 2u)) / silence_rate);
  }

  if (out_silence == 0) {
    return 1;
  }

  if (!ensure_i16_capacity(out_pcm, out_cap, *out_frames + out_silence)) {
    return 0;
  }

  memset(*out_pcm + *out_frames, 0, (size_t)out_silence * sizeof(int16_t));
  *out_frames += out_silence;
  return 1;
}

int voc_decode(
  const uint8_t* data,
  size_t len,
  int16_t** out_pcm,
  uint32_t* out_samples,
  uint32_t* out_rate)
{
  size_t pos;
  uint8_t cur_timeconst;
  uint8_t cur_codec;
  uint32_t overall_rate;
  int16_t* pcm;
  uint32_t frames;
  uint32_t cap;

  struct RepeatFrame {
    size_t jump_pos;
    uint16_t count;
  } repeat_stack[MAX_NESTED_REPEATS];
  int repeat_sp;

  if (!out_pcm || !out_samples || !out_rate) {
    return 0;
  }
  *out_pcm = NULL;
  *out_samples = 0;
  *out_rate = 0;

  if (!data || len < 4) {
    return 0;
  }

  /* Optional header. If present, trust the embedded data offset. */
  pos = 0;
  if (len >= 26) {
    static const char sig[] = "Creative Voice File\x1A";
    if (memcmp(data, sig, sizeof(sig) - 1) == 0) {
      uint16_t data_ofs = rd_le16(data + 20);
      if ((size_t)data_ofs < len) {
        pos = (size_t)data_ofs;
      } else {
        return 0;
      }
    }
  }

  cur_timeconst = 0;
  cur_codec = 0xffu;
  overall_rate = 0;

  pcm = NULL;
  frames = 0;
  cap = 0;

  repeat_sp = 0;

  while (pos < len) {
    uint8_t block_type;
    uint32_t block_len;
    size_t payload;

    block_type = data[pos++];
    if (block_type == VOC_BLOCK_TERMINATOR) {
      break;
    }

    if (pos + 3u > len) {
      free(pcm);
      return 0;
    }
    block_len = rd_le24(data + pos);
    pos += 3u;
    payload = pos;

    if (payload + (size_t)block_len > len) {
      free(pcm);
      return 0;
    }

    switch (block_type) {
      case VOC_BLOCK_SOUND_DATA: {
        uint32_t rate;
        if (block_len < 2u) {
          free(pcm);
          return 0;
        }
        cur_timeconst = data[payload + 0];
        cur_codec = data[payload + 1];
        if (cur_codec != VOC_CODEC_PCM_U8) {
          free(pcm);
          return 0;
        }
        rate = timeconst_to_rate(cur_timeconst);
        if (rate == 0) {
          free(pcm);
          return 0;
        }
        if (overall_rate == 0) {
          overall_rate = rate;
        }
        if (!append_pcm_u8_as_s16(
              data + payload + 2u, block_len - 2u,
              rate, overall_rate,
              &pcm, &frames, &cap)) {
          free(pcm);
          return 0;
        }
      } break;

      case VOC_BLOCK_SOUND_CONT: {
        uint32_t rate;
        if (cur_codec != VOC_CODEC_PCM_U8) {
          free(pcm);
          return 0;
        }
        rate = timeconst_to_rate(cur_timeconst);
        if (rate == 0) {
          free(pcm);
          return 0;
        }
        if (overall_rate == 0) {
          overall_rate = rate;
        }
        if (!append_pcm_u8_as_s16(
              data + payload, block_len,
              rate, overall_rate,
              &pcm, &frames, &cap)) {
          free(pcm);
          return 0;
        }
      } break;

      case VOC_BLOCK_SILENCE: {
        uint16_t dur;
        uint8_t tc;
        uint32_t rate;
        if (block_len < 3u) {
          free(pcm);
          return 0;
        }
        dur = rd_le16(data + payload);
        tc = data[payload + 2u];
        rate = timeconst_to_rate(tc);
        if (rate == 0) {
          free(pcm);
          return 0;
        }
        if (overall_rate == 0) {
          overall_rate = rate;
        }
        if (!append_silence((uint32_t)dur, rate, overall_rate, &pcm, &frames, &cap)) {
          free(pcm);
          return 0;
        }
      } break;

      case VOC_BLOCK_TEXT:
        /* Ignore text. */
        break;

      case VOC_BLOCK_REPEAT: {
        uint16_t count;
        if (block_len < 2u) {
          free(pcm);
          return 0;
        }
        count = rd_le16(data + payload);
        if (count == 0xffffu) {
          /* Avoid infinite decode. This shouldn't occur in GOT SFX. */
          count = 0u;
        }
        if (repeat_sp < MAX_NESTED_REPEATS) {
          repeat_stack[repeat_sp].jump_pos = payload + (size_t)block_len;
          repeat_stack[repeat_sp].count = count;
        }
        repeat_sp++;
      } break;

      case VOC_BLOCK_END_REPEAT: {
        if (repeat_sp == 0) {
          /* Malformed VOC, but don't crash. */
          free(pcm);
          return 0;
        }
        repeat_sp--;
        if (repeat_sp < MAX_NESTED_REPEATS) {
          if (repeat_stack[repeat_sp].count-- != 0u) {
            pos = repeat_stack[repeat_sp].jump_pos;
            repeat_sp++;
            /* Continue without applying pos += block_len below. */
            continue;
          }
        }
      } break;

      default:
        /* Skip unknown blocks. */
        break;
    }

    pos = payload + (size_t)block_len;
  }

  if (overall_rate == 0) {
    /* No audio blocks. */
    free(pcm);
    return 0;
  }

  *out_pcm = pcm;
  *out_samples = frames;
  *out_rate = overall_rate;
  return 1;
}

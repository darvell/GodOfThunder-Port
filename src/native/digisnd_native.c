#include "modern.h"
#include "digisnd.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mixer.h"
#include "voc_decode.h"

/* malloc size introspection for safer VOC bounds.
 *
 * NOTE: These APIs generally require the original pointer returned by malloc().
 * For standard DIGSOUND SFX, we use std_sound_start (the allocation base).
 */
#if defined(__APPLE__)
#  include <malloc/malloc.h>
#elif defined(__linux__)
#  include <malloc.h>
#elif defined(_WIN32)
#  include <malloc.h>
#endif

/* These globals are required by the original DOS game code. */
bool AdLibPresent = false;
bool SoundBlasterPresent = false;

static SoundFinishedCallback g_finished_cb = NULL;
static NewVocSectionCallback g_new_voc_section_cb = NULL;

/* Optional helper to determine a safe max buffer length for VOC parsing.
 * - For standard sounds, the sound pointer is into the DIGSOUND allocation,
 *   and we can clamp to end-of-allocation.
 * - For boss sounds, the sound pointer is the base of its own allocation.
 *
 * When we cannot determine capacity, we fall back to a conservative cap.
 */
static size_t ptr_capacity_bytes(const void* base_ptr) {
#if defined(__APPLE__)
  return (size_t)malloc_size(base_ptr);
#elif defined(__linux__)
  return (size_t)malloc_usable_size(base_ptr);
#elif defined(_WIN32)
  return (size_t)_msize((void*)base_ptr);
#else
  (void)base_ptr;
  return 0;
#endif
}

static size_t voc_max_len_for_ptr(const uint8_t* p) {
  /* Standard SFX: dig_sound[] points inside std_sound_start. */
  extern char far* std_sound_start;

  if (std_sound_start) {
    const uint8_t* base = (const uint8_t*)std_sound_start;
    size_t cap = ptr_capacity_bytes((const void*)std_sound_start);
    if (cap != 0) {
      const uint8_t* end = base + cap;
      if (p >= base && p < end) {
        return (size_t)(end - p);
      }
    }
  }

  /* Boss sounds: the VOC pointer is typically the base pointer of malloc. */
  {
    size_t cap = ptr_capacity_bytes((const void*)p);
    if (cap != 0) {
      return cap;
    }
  }

  /* Fallback: GOT VOCs are small (a few KB). */
  return (size_t)65536u;
}

static void scan_voc_sections_and_callback(const uint8_t* data, size_t len, bool includesHeader) {
  size_t pos;

  if (!g_new_voc_section_cb) {
    return;
  }

  pos = 0;

  if (includesHeader) {
    if (len < 22) {
      return;
    }
    /* Use the standard header offset field (at byte 20). */
    pos = (size_t)((uint16_t)data[20] | ((uint16_t)data[21] << 8));
    if (pos >= len) {
      return;
    }
  } else {
    /* If the data actually includes a VOC header anyway, skip it. */
    if (len >= 26) {
      static const char sig[] = "Creative Voice File\x1A";
      if (memcmp(data, sig, sizeof(sig) - 1) == 0) {
        pos = (size_t)((uint16_t)data[20] | ((uint16_t)data[21] << 8));
        if (pos >= len) {
          return;
        }
      }
    }
  }

  while (pos < len) {
    uint8_t t;
    uint32_t blen;
    byte huge* payload_ptr;

    t = data[pos++];
    if (t == 0) {
      g_new_voc_section_cb(0, 0, NULL);
      return;
    }

    if (pos + 3u > len) {
      return;
    }
    blen = (uint32_t)data[pos] | ((uint32_t)data[pos + 1u] << 8) | ((uint32_t)data[pos + 2u] << 16);
    pos += 3u;

    if (pos + (size_t)blen > len) {
      return;
    }

    payload_ptr = (byte huge*)(data + pos);
    g_new_voc_section_cb((word)t, (dword)blen, payload_ptr);

    pos += (size_t)blen;
  }
}

char* SB_Init(char* blasterEnvVar) {
  (void)blasterEnvVar;

  mixer_init(44100);

  /* Native build: always available (emulated). */
  AdLibPresent = true;
  SoundBlasterPresent = true;
  return NULL;
}

void SB_Shutdown(void) {
  mixer_shutdown();
  AdLibPresent = false;
  SoundBlasterPresent = false;
}

void SB_PlaySample(byte huge* data, long sampleRate, dword length) {
  uint32_t i;
  int16_t* pcm16;
  uint32_t frames;
  uint32_t rate;

  if (!data || length == 0) {
    return;
  }
  if (sampleRate <= 0) {
    return;
  }

  frames = (uint32_t)length;
  rate = (uint32_t)sampleRate;

  pcm16 = (int16_t*)malloc((size_t)frames * sizeof(int16_t));
  if (!pcm16) {
    return;
  }

  for (i = 0; i < frames; i++) {
    int v = (int)((const uint8_t*)data)[i] - 128;
    pcm16[i] = (int16_t)(v << 8);
  }

  mixer_play_pcm16(pcm16, frames, rate, 0);
}

void SB_PlaySilence(long sampleRate, dword length) {
  if (sampleRate <= 0 || length == 0) {
    return;
  }
  mixer_play_silence((uint32_t)length, (uint32_t)sampleRate);
}

wbool SB_IsSamplePlaying(void) {
  return (wbool)(mixer_is_sample_playing() ? true : false);
}

void SB_StopSound(void) {
  /* Match DOS driver behavior: stopping should not trigger finished callback. */
  SB_SetSoundFinishedCallback(NULL);
  mixer_stop_sample(0);
}

void SB_SetSoundFinishedCallback(SoundFinishedCallback callback) {
  g_finished_cb = callback;
  mixer_set_sound_finished_callback(callback);
}

void SB_PlayVoc(byte huge* data, bool includesHeader) {
  int16_t* pcm16;
  uint32_t frames;
  uint32_t rate;
  size_t max_len;

  if (!data) {
    return;
  }

  max_len = voc_max_len_for_ptr((const uint8_t*)data);

  /* Optional: notify about VOC sections as we parse. */
  scan_voc_sections_and_callback((const uint8_t*)data, max_len, includesHeader);

  if (!voc_decode((const uint8_t*)data, max_len, &pcm16, &frames, &rate)) {
    return;
  }

  mixer_play_pcm16(pcm16, frames, rate, 1);
}

wbool SB_IsVocPlaying(void) {
  return (wbool)(mixer_is_voc_playing() ? true : false);
}

void SB_SetNewVocSectionCallback(NewVocSectionCallback callback) {
  g_new_voc_section_cb = callback;
}

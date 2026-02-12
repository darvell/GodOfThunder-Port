#include "mixer.h"

#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
/* Emscripten builds may be single-threaded (no audio callback thread). Keep
   the mixer lock-free to avoid requiring pthreads. */
#  define MIXER_NO_THREADS 1
#elif defined(_WIN32)
#  include <windows.h>
#else
#  include <pthread.h>
#endif

enum { MIXER_OPL2_RATE = 49716 };
enum { OPL2_RING_SIZE = 8192 };

/* Fixed-point 16.16 resampler state. */
typedef struct {
  int16_t* pcm;
  uint32_t frames;
  uint32_t rate;
  uint32_t pos_fp;
  uint32_t step_fp;
  int playing;
  int is_voc;
} SampleState;

typedef struct {
  int enabled;
  uint32_t dst_rate;
  /* 48.16-ish fixed point in a 64-bit container to avoid wrap-around for
     continuous streams like music. */
  uint64_t pos_fp;
  uint32_t step_fp;

  int16_t ring[OPL2_RING_SIZE];
  uint32_t head;
  uint32_t count;
  uint64_t base_abs; /* abs index of ring[head] */
  uint64_t gen_abs;  /* abs index of next to generate */
} Opl2State;

typedef struct {
  uint16_t divisor;
  double phase01;
  double step01;
} PcSpkState;

typedef struct {
#if defined(MIXER_NO_THREADS)
  int dummy;
#elif defined(_WIN32)
  CRITICAL_SECTION mtx;
#else
  pthread_mutex_t mtx;
#endif
  int initialized;
  int shutting_down;
  uint32_t out_rate;

  SoundFinishedCallback finished_cb;

  SampleState sfx;
  Opl2State opl2;
  PcSpkState pc;
} MixerState;

static MixerState g_m;

static void mixer_lock(void) {
#if defined(MIXER_NO_THREADS)
  /* no-op */
#elif defined(_WIN32)
  EnterCriticalSection(&g_m.mtx);
#else
  pthread_mutex_lock(&g_m.mtx);
#endif
}

static void mixer_unlock(void) {
#if defined(MIXER_NO_THREADS)
  /* no-op */
#elif defined(_WIN32)
  LeaveCriticalSection(&g_m.mtx);
#else
  pthread_mutex_unlock(&g_m.mtx);
#endif
}

void mixer_lock_state(void) {
  if (!g_m.initialized) return;
  mixer_lock();
}

void mixer_unlock_state(void) {
  if (!g_m.initialized) return;
  mixer_unlock();
}

static void sample_reset(SampleState* s) {
  if (!s) return;
  if (s->pcm) {
    free(s->pcm);
  }
  memset(s, 0, sizeof(*s));
}

static void sample_start(SampleState* s, int16_t* pcm16, uint32_t frames, uint32_t rate, int is_voc, uint32_t out_rate) {
  if (!s) return;

  /* Preempt current sample without triggering completion. */
  sample_reset(s);

  s->pcm = pcm16;
  s->frames = frames;
  s->rate = rate;
  s->pos_fp = 0;
  s->playing = (pcm16 && frames && rate) ? 1 : 0;
  s->is_voc = is_voc ? 1 : 0;

  if (rate && out_rate) {
    s->step_fp = (uint32_t)(((uint32_t)rate << 16) / (uint32_t)out_rate);
    if (s->step_fp == 0) {
      s->step_fp = 1;
    }
  } else {
    s->step_fp = 0;
  }
}

static int16_t sample_resample_next(SampleState* s, int* out_finished) {
  uint32_t idx;
  uint32_t frac;
  int32_t s0, s1;
  int32_t v;

  if (out_finished) *out_finished = 0;
  if (!s || !s->playing || !s->pcm || s->frames == 0 || s->step_fp == 0) {
    return 0;
  }

  idx = s->pos_fp >> 16;
  if (idx >= s->frames) {
    s->playing = 0;
    if (out_finished) *out_finished = 1;
    return 0;
  }

  frac = s->pos_fp & 0xffffu;
  s0 = (int32_t)s->pcm[idx];
  if (idx + 1u < s->frames) {
    s1 = (int32_t)s->pcm[idx + 1u];
  } else {
    s1 = s0;
  }

  v = (int32_t)((s0 * (int32_t)(65536u - frac) + s1 * (int32_t)frac) >> 16);
  s->pos_fp += s->step_fp;

  /* Mark finished once we fully pass the end (avoid callback at idx==frames but not yet). */
  if ((s->pos_fp >> 16) >= s->frames) {
    s->playing = 0;
    if (out_finished) *out_finished = 1;
  }

  return (int16_t)v;
}

static void opl2_reset(Opl2State* o, uint32_t out_rate) {
  if (!o) return;
  memset(o, 0, sizeof(*o));
  o->enabled = 1;
  o->dst_rate = out_rate;
  o->pos_fp = 0;
  o->step_fp = (uint32_t)(((uint32_t)MIXER_OPL2_RATE << 16) / (uint32_t)out_rate);
  if (o->step_fp == 0) o->step_fp = 1;
  o->head = 0;
  o->count = 0;
  o->base_abs = 0;
  o->gen_abs = 0;
}

static int opl2_ring_free(const Opl2State* o) {
  if (!o) return 0;
  return (int)(OPL2_RING_SIZE - o->count);
}

static int16_t opl2_ring_get(const Opl2State* o, uint64_t abs_idx) {
  uint64_t ofs;
  uint32_t idx;

  ofs = abs_idx - o->base_abs;
  idx = (o->head + (uint32_t)ofs) % OPL2_RING_SIZE;
  return o->ring[idx];
}

static void opl2_ring_drop(Opl2State* o, uint32_t n) {
  if (!o || n == 0) return;
  if (n > o->count) n = o->count;
  o->head = (o->head + n) % OPL2_RING_SIZE;
  o->count -= n;
  o->base_abs += (uint64_t)n;
}

static void opl2_ring_push(Opl2State* o, const int16_t* src, uint32_t n) {
  uint32_t i;
  if (!o || !src) return;

  for (i = 0; i < n; i++) {
    uint32_t write_idx;
    if (o->count >= OPL2_RING_SIZE) {
      /* Should not happen if we prune, but keep things safe. */
      opl2_ring_drop(o, 1);
    }
    write_idx = (o->head + o->count) % OPL2_RING_SIZE;
    o->ring[write_idx] = src[i];
    o->count++;
    o->gen_abs++;
  }
}

static void opl2_ensure_available(Opl2State* o, uint64_t need_abs_inclusive) {
  /* Ensure ring has samples up to `need_abs_inclusive` available. */
  while (o->base_abs + (uint64_t)o->count <= need_abs_inclusive) {
    int free_slots = opl2_ring_free(o);
    int16_t tmp[512];
    uint32_t gen;

    if (free_slots <= 0) {
      /* Drop old samples: keep at least 2 behind the current read position. */
      uint64_t cur = (uint64_t)(o->pos_fp >> 16);
      uint64_t keep_from = (cur > 2u) ? (cur - 2u) : 0u;
      if (keep_from > o->base_abs) {
        uint32_t drop = (uint32_t)(keep_from - o->base_abs);
        opl2_ring_drop(o, drop);
        free_slots = opl2_ring_free(o);
      }
    }

    if (free_slots <= 0) {
      /* Still no space. Give up, output will be silence. */
      return;
    }

    gen = (uint32_t)free_slots;
    if (gen > 512u) gen = 512u;

    opl2_generate(tmp, (int)gen);
    opl2_ring_push(o, tmp, gen);
  }
}

static int16_t opl2_resample_next(Opl2State* o) {
  uint64_t idx;
  uint32_t frac;
  int32_t s0, s1;
  int32_t v;

  if (!o || !o->enabled) return 0;

  idx = (uint64_t)(o->pos_fp >> 16);
  frac = (uint32_t)(o->pos_fp & 0xffffu);

  /* Need idx and idx+1 for linear interpolation. */
  opl2_ensure_available(o, idx + 1u);
  if (idx < o->base_abs || (idx + 1u) >= (o->base_abs + (uint64_t)o->count)) {
    /* Not enough samples buffered. */
    o->pos_fp += o->step_fp;
    return 0;
  }

  s0 = (int32_t)opl2_ring_get(o, idx);
  s1 = (int32_t)opl2_ring_get(o, idx + 1u);
  v = (int32_t)((s0 * (int32_t)(65536u - frac) + s1 * (int32_t)frac) >> 16);

  o->pos_fp += o->step_fp;

  /* Drop old samples to keep ring small. */
  {
    uint64_t cur = (uint64_t)(o->pos_fp >> 16);
    uint64_t keep_from = (cur > 2u) ? (cur - 2u) : 0u;
    if (keep_from > o->base_abs) {
      uint32_t drop = (uint32_t)(keep_from - o->base_abs);
      opl2_ring_drop(o, drop);
    }
  }

  return (int16_t)v;
}

static int16_t pcspk_next(PcSpkState* pc) {
  /* Square wave with duty 50%. */
  const int16_t amp = 5000;
  double s;

  if (!pc) return 0;
  if (!pc->divisor || pc->step01 <= 0.0) {
    return 0;
  }

  s = (pc->phase01 < 0.5) ? 1.0 : -1.0;
  pc->phase01 += pc->step01;
  while (pc->phase01 >= 1.0) pc->phase01 -= 1.0;
  return (int16_t)(s * (double)amp);
}

static int16_t clamp_i16(int32_t v) {
  if (v > 32767) return 32767;
  if (v < -32768) return -32768;
  return (int16_t)v;
}

void mixer_init(int sample_rate) {
  if (sample_rate <= 0) sample_rate = 44100;

  if (g_m.initialized) {
    return;
  }

  memset(&g_m, 0, sizeof(g_m));

#if defined(MIXER_NO_THREADS)
  /* no-op */
#elif defined(_WIN32)
  InitializeCriticalSection(&g_m.mtx);
#else
  pthread_mutex_init(&g_m.mtx, NULL);
#endif

  g_m.initialized = 1;
  g_m.shutting_down = 0;
  g_m.out_rate = (uint32_t)sample_rate;
  g_m.finished_cb = NULL;

  sample_reset(&g_m.sfx);
  opl2_reset(&g_m.opl2, g_m.out_rate);

  g_m.pc.divisor = 0;
  g_m.pc.phase01 = 0.0;
  g_m.pc.step01 = 0.0;
}

void mixer_shutdown(void) {
  if (!g_m.initialized) {
    return;
  }

  mixer_lock();
  g_m.shutting_down = 1;
  sample_reset(&g_m.sfx);
  g_m.opl2.count = 0;
  g_m.opl2.head = 0;
  g_m.opl2.base_abs = 0;
  g_m.opl2.gen_abs = 0;
  g_m.pc.divisor = 0;
  g_m.pc.step01 = 0.0;
  g_m.pc.phase01 = 0.0;
  g_m.finished_cb = NULL;
  mixer_unlock();

#if defined(MIXER_NO_THREADS)
  /* no-op */
#elif defined(_WIN32)
  DeleteCriticalSection(&g_m.mtx);
#else
  pthread_mutex_destroy(&g_m.mtx);
#endif

  memset(&g_m, 0, sizeof(g_m));
}

void mixer_set_sound_finished_callback(SoundFinishedCallback cb) {
  if (!g_m.initialized) return;
  mixer_lock();
  g_m.finished_cb = cb;
  mixer_unlock();
}

void mixer_set_opl2_enabled(int enabled) {
  if (!g_m.initialized) return;
  mixer_lock();
  g_m.opl2.enabled = enabled ? 1 : 0;
  mixer_unlock();
}

void mixer_set_pc_divisor(uint16_t divisor) {
  /* Frequency = 1193182 / divisor (PIT input clock). */
  if (!g_m.initialized) return;

  mixer_lock();
  g_m.pc.divisor = divisor;
  if (divisor == 0) {
    g_m.pc.step01 = 0.0;
  } else {
    double pit = 1193182.0;
    double freq = pit / (double)divisor;
    g_m.pc.step01 = freq / (double)g_m.out_rate;
  }
  mixer_unlock();
}

void mixer_play_pcm16(int16_t* pcm16, uint32_t frames, uint32_t src_rate, int is_voc) {
  if (!g_m.initialized) {
    if (pcm16) free(pcm16);
    return;
  }

  mixer_lock();
  sample_start(&g_m.sfx, pcm16, frames, src_rate, is_voc, g_m.out_rate);
  mixer_unlock();
}

void mixer_play_u8_pcm(const uint8_t* pcm_u8, uint32_t bytes, uint32_t src_rate, int is_voc) {
  int16_t* pcm16;
  uint32_t i;

  if (!pcm_u8 || bytes == 0 || src_rate == 0) {
    return;
  }

  pcm16 = (int16_t*)malloc((size_t)bytes * sizeof(int16_t));
  if (!pcm16) {
    return;
  }

  for (i = 0; i < bytes; i++) {
    int v = (int)pcm_u8[i] - 128;
    pcm16[i] = (int16_t)(v << 8);
  }

  mixer_play_pcm16(pcm16, bytes, src_rate, is_voc);
}

void mixer_play_silence(uint32_t frames, uint32_t src_rate) {
  int16_t* pcm16;
  if (frames == 0 || src_rate == 0) return;
  pcm16 = (int16_t*)calloc((size_t)frames, sizeof(int16_t));
  if (!pcm16) return;
  mixer_play_pcm16(pcm16, frames, src_rate, 0);
}

void mixer_stop_sample(int call_finished_callback) {
  SoundFinishedCallback cb = NULL;

  if (!g_m.initialized) return;

  mixer_lock();
  if (g_m.sfx.playing) {
    sample_reset(&g_m.sfx);
    if (call_finished_callback) {
      cb = g_m.finished_cb;
    }
  }
  mixer_unlock();

  if (cb) {
    cb();
  }
}

int mixer_is_sample_playing(void) {
  int playing;
  if (!g_m.initialized) return 0;
  mixer_lock();
  playing = g_m.sfx.playing ? 1 : 0;
  mixer_unlock();
  return playing;
}

int mixer_is_voc_playing(void) {
  int playing;
  if (!g_m.initialized) return 0;
  mixer_lock();
  playing = (g_m.sfx.playing && g_m.sfx.is_voc) ? 1 : 0;
  mixer_unlock();
  return playing;
}

void mixer_generate(int16_t* buf, int frames) {
  int i;
  SoundFinishedCallback cb_to_call = NULL;

  if (!buf || frames <= 0) return;

  if (!g_m.initialized) {
    memset(buf, 0, (size_t)frames * sizeof(int16_t));
    return;
  }

  mixer_lock();

  /* Simple fixed volumes (Q8.8). */
  {
    const int vol_opl2 = 160; /* ~0.625 */
    const int vol_sfx  = 200; /* ~0.78  */
    const int vol_pc   = 120; /* ~0.47  */

    for (i = 0; i < frames; i++) {
      int32_t acc = 0;
      int finished = 0;

      if (g_m.opl2.enabled) {
        int16_t s = opl2_resample_next(&g_m.opl2);
        acc += ((int32_t)s * (int32_t)vol_opl2) >> 8;
      }

      if (g_m.sfx.playing) {
        int16_t s = sample_resample_next(&g_m.sfx, &finished);
        acc += ((int32_t)s * (int32_t)vol_sfx) >> 8;
        if (finished) {
          /* Free PCM now to release memory quickly. */
          if (g_m.sfx.pcm) {
            free(g_m.sfx.pcm);
            g_m.sfx.pcm = NULL;
          }
          g_m.sfx.frames = 0;
          g_m.sfx.rate = 0;
          g_m.sfx.pos_fp = 0;
          g_m.sfx.step_fp = 0;
          g_m.sfx.playing = 0;
          g_m.sfx.is_voc = 0;
          cb_to_call = g_m.finished_cb;
        }
      }

      {
        int16_t s = pcspk_next(&g_m.pc);
        acc += ((int32_t)s * (int32_t)vol_pc) >> 8;
      }

      buf[i] = clamp_i16(acc);
    }
  }

  mixer_unlock();

  /* Call without holding the mutex to avoid deadlocks/re-entrancy issues. */
  if (cb_to_call) {
    cb_to_call();
  }
}

/* Weak fallback so native builds link even if the OPL2 emulator isn't wired up
 * yet. A real emulator (opl2_emu.cpp) provides a strong symbol with the same
 * name.  MSVC doesn't support weak symbols, and opl2_emu.cpp is always linked
 * in our CMake targets, so skip the fallback entirely on MSVC.
 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
void opl2_generate(int16_t* out, int frames) {
  if (!out || frames <= 0) return;
  memset(out, 0, (size_t)frames * sizeof(int16_t));
}
#endif

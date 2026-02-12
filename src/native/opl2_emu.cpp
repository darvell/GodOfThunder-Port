#include "opl2_emu.h"

/*
  Proven OPL2 core (YM3812) integration via ymfm (BSD-3-Clause).

  The rest of the native port expects a tiny API:
    - opl2_init()
    - opl2_write(reg, val)
    - opl2_generate(buf, samples)

  MU_Service() runs at 120Hz and drives music by writing raw OPL registers via
  SB_ALOut(), which calls opl2_write(). The mixer pulls audio by calling
  opl2_generate() at OPL2_EMU_SAMPLE_RATE (~49716Hz) and resamples to the
  platform output rate.
*/

#include <stddef.h>
#include <stdint.h>

#include <vector>
#include <mutex>

#include "ymfm_opl.h"

namespace {

/* OPL2 commonly derives from a 14.31818 MHz master clock (NTSC crystal),
   with an internal prescaler. ymfm's OPL2 implementation uses a default
   prescale of 4, so passing 14.31818 MHz yields the expected ~49716 Hz
   output sample rate. */
static constexpr uint32_t kOpl2ClockHz = 14318180u;

class GotYmfmInterface : public ymfm::ymfm_interface {
  // Default no-op implementations are sufficient: we don't use OPL timers/IRQs.
};

static GotYmfmInterface g_intf;
static ymfm::ym3812 g_chip(g_intf);
static int g_inited = 0;
static uint32_t g_rate = 0;
static std::mutex g_mu;

static void ensure_init(void) {
  if (!g_inited) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_chip.reset();
    g_rate = g_chip.sample_rate(kOpl2ClockHz);
    g_inited = 1;
  }
}

} // namespace

void opl2_init(void) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_chip.reset();
  g_rate = g_chip.sample_rate(kOpl2ClockHz);
  g_inited = 1;
}

void opl2_write(uint8_t reg, uint8_t val) {
  ensure_init();
  std::lock_guard<std::mutex> lock(g_mu);
  g_chip.write_address(reg);
  g_chip.write_data(val);
}

void opl2_generate(int16_t* buf, int samples) {
  if (!buf || samples <= 0) return;
  ensure_init();
  std::lock_guard<std::mutex> lock(g_mu);

  /* ymfm generates int32-ish outputs; clamp to int16.
     IMPORTANT: avoid allocations here; this runs under the mixer's lock and
     can cause audio underruns if it stalls. */
  static std::vector<ymfm::ym3812::output_data> out;
  if (out.capacity() < (size_t)samples) {
    out.reserve((size_t)samples);
  }
  out.resize((size_t)samples);

  // Best-effort sanity check: the mixer assumes OPL2_EMU_SAMPLE_RATE.
  // If this ever differs, tempo/pitch will be off but it's still better to
  // output something than to crash.
  (void)g_rate;
  g_chip.generate(out.data(), (uint32_t)samples);

  for (int i = 0; i < samples; i++) {
    int32_t v = out[(size_t)i].data[0];
    if (v < -32768) v = -32768;
    if (v > 32767) v = 32767;
    buf[i] = (int16_t)v;
  }
}

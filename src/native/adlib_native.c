#include "adlib.h"

#include "adlib_native.h"
#include "opl2_emu.h"
#include "mixer.h"

/*
  SB_ALOut / SB_AL_ResetChannels are part of the original game's AdLib layer.
  MU_Service() drives music by writing raw OPL2 registers via SB_ALOut().

  On native platforms, we emulate OPL2 instead of writing to I/O ports.
*/

static int g_opl2_inited = 0;

static void ensure_init(void) {
  if (!g_opl2_inited) {
    opl2_init();
    g_opl2_inited = 1;
  }
}

void SB_ALOut(unsigned char reg, unsigned char val) {
  ensure_init();
  opl2_write((uint8_t)reg, (uint8_t)val);
}

void SB_AL_ResetChannels(void) {
  int i;
  ensure_init();

  /* Match src/utility/adlib.c semantics: disable rhythm mode and key-off
     channels 1..9 (channel 0 reserved for SFX in the original). */
  opl2_write((uint8_t)0xBD, (uint8_t)0);

  for (i = 0; i < 10; i++) {
    opl2_write((uint8_t)(0xB1 + (uint8_t)i), (uint8_t)0);
  }
}

int got_adlib_sample_rate(void) {
  return OPL2_EMU_SAMPLE_RATE;
}

void got_adlib_reset(void) {
  opl2_init();
  g_opl2_inited = 1;
}

void got_adlib_generate(int16_t* buf, int samples) {
  ensure_init();
  opl2_generate(buf, samples);
}

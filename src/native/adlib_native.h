#ifndef ADLIB_NATIVE_H_
#define ADLIB_NATIVE_H_

/*
  Native AdLib (OPL2) shim.

  The DOS build writes OPL2 registers via outportb to 0x388/0x389.
  The native build routes those writes into our OPL2 emulator.

  The audio backend/mixer can pull PCM from the emulator via
  got_adlib_generate().
*/

#include "modern.h"

int  got_adlib_sample_rate(void);
void got_adlib_reset(void);
void got_adlib_generate(int16_t* buf, int samples);

#endif /* ADLIB_NATIVE_H_ */


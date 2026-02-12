#ifndef OPL2_EMU_H_
#define OPL2_EMU_H_

/*
  Minimal OPL2 (YM3812) emulator for the native (raylib) port.

  Goals:
  - Provide "good enough" AdLib music playback for God of Thunder's
    MU_Service() stream of raw register writes (120Hz).
  - Keep the implementation self-contained (no external libs).
  - C90-compatible (project compiles with CMAKE_C_STANDARD 90).

  Notes:
  - This intentionally implements only the melodic 2-operator mode (9 channels).
  - Rhythm mode, timers, and status flags are ignored (but register writes are
    accepted and stored so the game doesn't break).
*/

#include "modern.h"

/* Native YM3812 output rate (approx). The mixer will resample as needed. */
#define OPL2_EMU_SAMPLE_RATE 49716

#ifdef __cplusplus
extern "C" {
#endif

void opl2_init(void);
void opl2_write(uint8_t reg, uint8_t val);
void opl2_generate(int16_t* buf, int samples);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OPL2_EMU_H_ */

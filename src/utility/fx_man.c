#include "fx_man.h"

// PC speaker sound effect playback.
//
// This is a small reimplementation of the PC speaker playback logic found in
// the original GOT.EXE (see functions around:
// - timer ISR @ 0x1C8DA (speaker service)
// - TML_PCPlaySound @ 0x1CA42 (start)
// - stop @ 0x1CA7A
//
// The game calls FX_ServicePC() from the timer ISR running at 120Hz.
// FX_PlayPC() arms playback by setting a far pointer to an array of 16-bit
// timer divisors (PIT channel 2). Each tick consumes one divisor value.
//
// When building on non-DOS platforms (e.g. clang), we keep these as no-ops.

#include "modern.h"

#ifdef __llvm__

void FX_ServicePC(void) {
  // no-op on modern platforms
}

void FX_StopPC(void) {
  // no-op on modern platforms
}

int FX_PCPlaying(void) {
  return 0;
}

void FX_PlayPC(PCSound far* sound, long length) {
  (void)sound;
  (void)length;
  // no-op on modern platforms
}

#else

#include <dos.h>

static volatile unsigned int  fx_pc_last_divisor = 0xFFFF;
static volatile unsigned int  fx_pc_words_left = 0;
static volatile unsigned int far* fx_pc_ptr = 0;

#define FX_LOCK()   asm pushf; asm cli
#define FX_UNLOCK() asm popf

void FX_ServicePC(void) {
  unsigned int divisor;
  unsigned char port61;

  // Safety: if the timer ISR is still calling us after the sound finished,
  // stop cleanly without reading beyond the buffer.
  if (!fx_pc_ptr || fx_pc_words_left == 0) {
    return;
  }

  // Consume one 16-bit divisor value per tick.
  divisor = *fx_pc_ptr;
  fx_pc_ptr += 1;

  if (divisor != fx_pc_last_divisor) {
    fx_pc_last_divisor = divisor;

    if (divisor) {
      // Program PIT channel 2: square wave generator, lobyte/hibyte.
      outportb(0x43, 0xB6);
      outportb(0x42, (unsigned char)(divisor & 0xFF));
      outportb(0x42, (unsigned char)((divisor >> 8) & 0xFF));

      // Enable speaker (bits 0 and 1).
      port61 = inportb(0x61);
      port61 |= 0x03;
      outportb(0x61, port61);
    }
    else {
      // Divisor 0 means silence for this tick: clear bits 0 and 1.
      port61 = inportb(0x61);
      port61 &= 0xFC;
      outportb(0x61, port61);
    }
  }

  fx_pc_words_left--;
  if (fx_pc_words_left == 0) {
    // Turn off speaker output (clear bit 1).
    port61 = inportb(0x61);
    port61 &= 0xFD;
    outportb(0x61, port61);

    fx_pc_ptr = 0;
    fx_pc_last_divisor = 0xFFFF;
  }
}

void FX_StopPC(void) {
  unsigned char port61;

  FX_LOCK();

  if (fx_pc_ptr) {
    fx_pc_last_divisor = 0xFFFF;
    fx_pc_words_left = 0;
    fx_pc_ptr = 0;

    port61 = inportb(0x61);
    port61 &= 0xFD; // clear speaker enable
    outportb(0x61, port61);
  }

  FX_UNLOCK();
}

int FX_PCPlaying(void) {
  return (fx_pc_ptr != 0);
}

void FX_PlayPC(PCSound far* sound, long length) {
  unsigned int words;

  if (!sound || length <= 0) {
    return;
  }

  // The original code complains about odd lengths and then rounds down to words.
  words = (unsigned int)(length >> 1);
  if (!words) {
    return;
  }

  FX_LOCK();
  fx_pc_last_divisor = 0xFFFF;
  fx_pc_words_left = words;
  fx_pc_ptr = (unsigned int far*)sound;
  FX_UNLOCK();
}

#endif

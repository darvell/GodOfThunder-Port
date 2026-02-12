#include "got_platform.h"

#include "raylib.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "digisnd.h"
#include "fx_man.h"
#include "mixer.h"

/* Game globals (defined in src/_g1/1_main.c) */
extern int music_flag, sound_flag, pcsound_flag;
extern char noal, nosb;

enum {
  GOT_AUDIO_RATE = 44100,
  GOT_AUDIO_CHANS = 1,
  GOT_AUDIO_SAMPLEBITS = 16,
  GOT_AUDIO_STREAM_SAMPLES = 1024
};

static int g_audio_ready = 0;
static AudioStream g_stream;
static int g_stream_has_callback = 0;

/* PC speaker playback sequencing state (120Hz service), mixed by mixer.c */
static const uint16_t* g_seq = NULL;
static uint32_t g_seq_words_left = 0;

static void audio_cb(void* bufferData, unsigned int frames) {
  /* Called by raylib's audio thread; bufferData is in stream's input format. */
  mixer_generate((int16_t*)bufferData, (int)frames);
}

int got_platform_audio_init(void) {
  if (g_audio_ready) return 1;

  /* Ensure the mixer exists even if sbfx_init ordering changes. */
  mixer_init(GOT_AUDIO_RATE);

  InitAudioDevice();
  if (!IsAudioDeviceReady()) {
    fprintf(stderr, "InitAudioDevice failed\n");
    return 0;
  }

  g_stream = LoadAudioStream(GOT_AUDIO_RATE, GOT_AUDIO_SAMPLEBITS, GOT_AUDIO_CHANS);
  if (!g_stream.buffer) {
    fprintf(stderr, "LoadAudioStream failed\n");
    CloseAudioDevice();
    return 0;
  }

  /* Prefer callback-driven streaming so audio isn't dependent on the main loop
     running frequently (prevents underruns and stutter during load/waits). */
  SetAudioStreamCallback(g_stream, audio_cb);
  g_stream_has_callback = 1;

  PlayAudioStream(g_stream);
  g_audio_ready = 1;

  return 1;
}

void got_platform_audio_shutdown(void) {
  if (!g_audio_ready) return;

  StopAudioStream(g_stream);
  UnloadAudioStream(g_stream);
  g_audio_ready = 0;

  CloseAudioDevice();
}

/* Replaces src/_g1/1_sbfx.c for the native build */
int sbfx_init(void) {
  char* sberr;

  /* Initialize emulated SB/AdLib (VOC decode is in software, OPL2 via emulator). */
  sberr = SB_Init(NULL);
  if (sberr) {
    fprintf(stderr, "SB_Init failed: %s\n", sberr);
    return 0;
  }

  /* Hardware-available flags (setup still controls actual playback). */
  music_flag = 1;
  sound_flag = 1;
  pcsound_flag = 1;

  if (noal) {
    music_flag = 0;
    mixer_set_opl2_enabled(0);
  }
  if (nosb) {
    sound_flag = 0;
  }

  if (!got_platform_audio_init()) {
    return 0;
  }

  return 1;
}

void sbfx_exit(void) {
  FX_StopPC();
  SB_Shutdown();
  got_platform_audio_shutdown();
}

void FX_ServicePC(void) {
  if (g_seq && g_seq_words_left) {
    uint16_t div = *g_seq++;
    g_seq_words_left--;

    /* Divisor 0 means silence for this tick. */
    mixer_set_pc_divisor(div);

    if (!g_seq_words_left) {
      g_seq = NULL;
      mixer_set_pc_divisor(0);
    }
  }

  /* Audio is generated in the audio thread callback. */
  (void)g_stream_has_callback;
}

void FX_StopPC(void) {
  g_seq = NULL;
  g_seq_words_left = 0;
  mixer_set_pc_divisor(0);
}

int FX_PCPlaying(void) {
  return (g_seq != NULL);
}

void FX_PlayPC(PCSound far* sound, long length) {
  uint32_t words;
  if (!sound || length <= 0) return;
  words = (uint32_t)((unsigned long)length >> 1);
  if (!words) return;

  g_seq = (const uint16_t*)sound;
  g_seq_words_left = words;

  /* Start silent; the 120Hz service will arm the first divisor. */
  mixer_set_pc_divisor(0);
}

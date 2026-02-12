#include "modern.h"
#include "digisnd.h"

/* Native build: no SoundBlaster DMA. Keep the API surface so the game can
   select PC speaker playback. */

bool AdLibPresent = false;
bool SoundBlasterPresent = false;

char* SB_Init(char* blasterEnvVar) {
  (void)blasterEnvVar;
  AdLibPresent = false;
  SoundBlasterPresent = false;
  return (char*)0;
}

void SB_Shutdown(void) {}
void SB_PlaySample(byte huge* data, long sampleRate, dword length) {
  (void)data; (void)sampleRate; (void)length;
}
void SB_PlaySilence(long sampleRate, dword length) {
  (void)sampleRate; (void)length;
}
wbool SB_IsSamplePlaying(void) { return false; }
void SB_StopSound(void) {}
void SB_SetSoundFinishedCallback(SoundFinishedCallback callback) { (void)callback; }
void SB_PlayVoc(byte huge* data, bool includesHeader) { (void)data; (void)includesHeader; }
wbool SB_IsVocPlaying(void) { return false; }
void SB_SetNewVocSectionCallback(NewVocSectionCallback callback) { (void)callback; }

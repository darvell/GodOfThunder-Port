#ifndef MIXER_H
#define MIXER_H

#include <stdint.h>

#include "digisnd.h"

/* Mixer output format: 16-bit signed PCM, mono. */

/* External OPL2 generator.
 *
 * Expected to generate `frames` samples at the OPL2 native rate (commonly
 * ~49716 Hz), signed 16-bit mono.
 *
 * Another implementation can provide a strong symbol with the same name; the
 * native mixer also provides a weak zero-fill fallback for builds where the
 * emulator isn't wired up yet.
 */
void opl2_generate(int16_t* out, int frames);

void mixer_init(int sample_rate);
void mixer_shutdown(void);

/* Called by the platform audio callback. */
void mixer_generate(int16_t* buf, int frames);

/* Control of sources. */
void mixer_set_opl2_enabled(int enabled);
void mixer_set_pc_divisor(uint16_t divisor);

/* Sample/VOC playback (single channel, preemptive). `pcm16` ownership is
 * transferred to the mixer (it will free() it).
 */
void mixer_play_pcm16(int16_t* pcm16, uint32_t frames, uint32_t src_rate, int is_voc);
void mixer_play_u8_pcm(const uint8_t* pcm_u8, uint32_t bytes, uint32_t src_rate, int is_voc);
void mixer_play_silence(uint32_t frames, uint32_t src_rate);
void mixer_stop_sample(int call_finished_callback);

int mixer_is_sample_playing(void);
int mixer_is_voc_playing(void);

void mixer_set_sound_finished_callback(SoundFinishedCallback cb);

/* Optional external synchronization hook.
 *
 * If another subsystem (e.g. OPL2 register writes) needs to synchronize with
 * mixer generation on platforms where audio runs on a separate thread, it can
 * take this lock around its state mutation.
 */
void mixer_lock_state(void);
void mixer_unlock_state(void);

#endif /* MIXER_H */

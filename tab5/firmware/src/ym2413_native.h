#ifndef R31_YM2413_NATIVE_H
#define R31_YM2413_NATIVE_H
#include "ym2413_core.h"
#include "ym2413_eg.h"
#include <stdint.h>
/* User-approved output balance; deliberately separate from YM2413 chip gain. */
/* The user-approved playback balance is x4. The independent-reference
 * comparison harness may override it to x1 without changing this default. */
#ifndef R31_BASS_DRUM_GAIN
#define R31_BASS_DRUM_GAIN 4
#endif
typedef struct {
  opll_eg eg;
  uint32_t phase;
  int16_t out[2];
  uint8_t key, initialized;
} r31_opll_slot;
typedef struct {
  r31_opll_slot slot[18];
  const uint8_t *patches[YM2413_CHANNELS];
  uint16_t base_atten[18]; /* ksl + total level, refreshed on register write */
  uint8_t native_am; /* shared AM value for one native tick */
  uint64_t clock_accum;
  uint32_t native_ticks, pm_phase, am_phase;
  uint32_t noise;
  int32_t last_pcm;
  uint32_t samples;
} r31_opll_native;
void r31_opll_native_init(r31_opll_native *p);
void r31_opll_native_refresh_patches(const ym2413_core *s, r31_opll_native *p);
float r31_opll_native_tick(const ym2413_core *s,r31_opll_native *p);
#endif

#ifndef VGMM5_YM2413_EG_H
#define VGMM5_YM2413_EG_H
#include <stdint.h>
/* Experimental reference-informed EG: isolated from melodic synth until rate/trace parity. */
typedef enum { OPLL_EG_OFF, OPLL_EG_ATTACK, OPLL_EG_DECAY, OPLL_EG_SUSTAIN, OPLL_EG_RELEASE, OPLL_EG_DAMP } opll_eg_phase;
typedef struct {
    uint8_t phase, attenuation, rate_ar, rate_dr, rate_rr, sustain_level, sustained, sus;
    uint8_t rks; /* precomputed key scale adjustment, 0..15 */
    uint8_t force_zero_rate; /* official: silent normal modulator has p_rate=0 */
    uint32_t counter;
    uint64_t clock_accum;
    double amplitude_previous, amplitude_target;
} opll_eg;
void opll_eg_init(opll_eg *e, uint8_t ar, uint8_t dr, uint8_t sl, uint8_t rr, uint8_t sustained, uint8_t sus, uint8_t rks);
void opll_eg_key_on(opll_eg *e);
void opll_eg_key_off(opll_eg *e);
/* Advance one host sample at user-specified master_clock/72 EG ticks; returns STRICT stepped amplitude. */
double opll_eg_tick(opll_eg *e, uint32_t master_clock, uint32_t sample_rate);
/* R6: same 72/1 native EG step, eliminating per-slot 64-bit time division. */
double opll_eg_tick_native(opll_eg *e);
/* View interpolation only, does not advance EG state. fraction is 0..1 within sample interval. */
double opll_eg_smooth_view(const opll_eg *e, double fraction);
#endif

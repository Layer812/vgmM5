#ifndef VGMM5_SOUND_SAFE_MATH_H
#define VGMM5_SOUND_SAFE_MATH_H

#include <math.h>
#include <stdint.h>

// YM2151 phase accumulator wraps modulo 2^32. Preserve the original
// float-to-uint32 truncation for every representable, nonnegative step.
static inline uint32_t vgm_safe_ym2151_phase_step(float step) {
    if (!(step >= 0.0f)) return 0u; // negative or NaN
    if (step < 4294967296.0f) return (uint32_t)step;
    if (!isfinite(step)) return 0u;
    return (uint32_t)fmodf(step, 4294967296.0f);
}

static inline int16_t vgm_safe_signed_pcm_byte(uint8_t dt) {
    return (int16_t)((int32_t)(int8_t)dt * 256);
}

// sdt (signed 5-bit) * 2^shift +/- companding bias: all terms fit int16.
static inline int16_t vgm_safe_c140_companded(int16_t sdt, int shift, int16_t bias) {
    int32_t sample = (int32_t)sdt * (1 << shift);
    sample += (sdt < 0) ? -(int32_t)bias : (int32_t)bias;
    return (int16_t)sample;
}

#endif

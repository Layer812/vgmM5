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

/*
 * YM2151 DT2 offsets: 0, 384, 500, 608 units of 1/64 semitone.
 * Equivalently +0, +600, +781.25 and +950 cents.
 * Applied to the current operator's frequency, never as a fixed Hz delta.
 */
// YM2151 KF register uses bits 7..2; bits 1..0 are unused.
// Do not mask bits 5..0, which shifts the pitch by up to a semitone.
static inline uint8_t vgm_ym2151_decode_kf(uint8_t reg) {
    return (uint8_t)(reg >> 2);
}

// YM2151 operator order for the 4-op algorithms is O1=slot0,
// O2=slot2, O3=slot1, O4=slot3 (register order differs).
// Input indices: 0=silence, 1=O1, 2=O2, 3=O3,
// 5=O1+O2, 6=O1+O3, 7=O2+O3.
typedef struct {
    uint8_t op2_input;
    uint8_t op3_input;
    uint8_t op4_input;
    uint8_t extra_carriers; // bits 0/1/2: O1/O2/O3, O4 always audible
} VgmYM2151Routing;

static inline VgmYM2151Routing vgm_ym2151_routing(uint8_t algorithm) {
    static const VgmYM2151Routing routes[8] = {
        {1, 2, 3, 0}, {0, 5, 3, 0}, {0, 2, 6, 0}, {1, 0, 7, 0},
        {1, 0, 3, 2}, {1, 1, 1, 6}, {1, 0, 0, 6}, {0, 0, 0, 7}
    };
    return routes[algorithm & 7u];
}

static inline float vgm_ym2151_apply_dt2(float phase_step, uint8_t dt2) {
    static const float ratios[4] = {
        1.0f, 1.41421356237f, 1.57030158878f, 1.73107312201f
    };
    unsigned index = (unsigned)dt2 & 3u;
    return index ? (phase_step * ratios[index]) : phase_step;
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

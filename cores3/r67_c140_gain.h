#pragma once
#include <stdint.h>
#include <limits.h>

// CoreS3 R67 C140 amplitude parity with the Tab5 R67 audio engine.
// C140 linear PCM is 8-bit; Tab5 expands it to 12-bit (*16), not 16-bit
// (*256). Do not modify C352's distinct 8-bit conversion.
static inline __attribute__((always_inline)) int16_t
cores3_r67_c140_linear_pcm(uint8_t byte_value) {
    return (int16_t)((int32_t)(int8_t)byte_value * 16);
}

// Tab5 mixes C140 as clamp16(namco_mix * 4), then divides by
// physical_chip_count * 3. CoreS3 keeps its original global /8 for
// all other chips. Premultiply this C140-only input by 8/3 so its
// post-master contribution matches Tab5 without changing FM/C352:
//   (clamp16(raw*4)*8/3) / (n*8)  ~= clamp16(raw*4)/(n*3)
// Avoid overflow even for synthetic test extremes.
static inline __attribute__((always_inline)) int32_t
cores3_r67_c140_to_core_mixer(int32_t namco_mix) {
    const int32_t saturated = (namco_mix > 8191) ? 32767 :
                              (namco_mix < -8192) ? -32768 :
                              namco_mix * 4;
    return (saturated * 8) / 3;
}


// FIX7: the legacy Tab5/CORE3-FIX2 C140 helper above deliberately
// saturates to int16 *before* applying the CoreS3 master attenuation.
// In a dense 22-voice C140 mix, this produces flat-topped distortion
// even when the final 16-bit output remains below 32767.
// Keep signed wide dynamic range on the C140 bus; FIX6's int64
// global summation and bounded final limiter protect the final output.
// The low-level response (|sum| <= 8191) is unchanged bit-for-bit.
static inline __attribute__((always_inline)) int32_t
cores3_r67_c140_to_core_mixer_unclipped(int32_t namco_mix) {
    const int64_t scaled = ((int64_t)namco_mix * 32) / 3;
    if (scaled > INT32_MAX) return INT32_MAX;
    if (scaled < INT32_MIN) return INT32_MIN;
    return (int32_t)scaled;
}

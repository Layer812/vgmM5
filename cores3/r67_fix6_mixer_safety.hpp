#pragma once
#include <stdint.h>
#include <limits.h>

// CoreS3 R67 FIX6: music-only DSP arithmetic. This header is included
// only for CoreS3, so no other target's output changes.
//
// Each FM/PCM chip delivers a signed 32-bit bus. Sum in 64-bit first
// so exceptionally dense FM+C140/AY/SCC combinations cannot wrap the
// 32-bit accumulator BEFORE the normal physical-chip master divisor.
static inline int32_t cores3_r67_saturate_i32(int64_t v) {
    return (v > (int64_t)INT32_MAX) ? INT32_MAX :
           (v < (int64_t)INT32_MIN) ? INT32_MIN : (int32_t)v;
}
static inline int32_t cores3_r67_divide_mix(int64_t sum, int physical_chips) {
    if (physical_chips < 1) physical_chips = 1;
    // CoreS3's existing R6 /8 headroom. Do not alter chip balance.
    return cores3_r67_saturate_i32(sum / ((int64_t)physical_chips * 8));
}

// The old '26000 + (abs(x)-26000)/2' is NOT a limiter; for input
// greater than 39534 it ultimately hard-clips at +/-32767.
// Compress only ABOVE 26000 with a continuous soft ceiling that can
// never reach/cross signed 16-bit max:
//   y = knee + excess*(32767-knee)/(excess + 32767-knee)
// Values at/below the knee remain bit-identical to the prior version.
// Single-sample, stateless: no block-edge gain jumps or note pumping.
static inline int32_t cores3_r67_true_peak_limiter(int32_t input) {
    const int64_t knee = 26000;
    const int64_t head = 32767 - knee;  // 6767
    const int64_t signed_input = (int64_t)input;
    const int64_t magnitude = signed_input < 0 ? -signed_input : signed_input;
    if (magnitude <= knee) return input;
    const int64_t excess = magnitude - knee;
    const int64_t compressed = knee + (excess * head) / (excess + head);
    return (int32_t)(signed_input < 0 ? -compressed : compressed);
}

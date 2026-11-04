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

// PR46: for two physical VGM chips (YM2151 + C140 on Dragon
// Saber), the established CoreS3 R6 /8 headroom divisor is exactly
// 2 * 8 = 16. The old variable signed 64-bit division is expensive
// on ESP32-S3. This uses unsigned magnitude and a constant shift,
// returning the SAME C++ truncation-towards-zero quotient for every
// int64_t value, including INT64_MIN and negative nonmultiples of 16.
// Other physical chip counts retain the legacy division unchanged.
static inline int32_t cores3_r67_divide_mix_fast2(int64_t sum, int physical_chips) {
    if (physical_chips != 2) return cores3_r67_divide_mix(sum, physical_chips);
    const bool negative = sum < 0;
    // Avoid undefined signed negation of INT64_MIN.
    const uint64_t magnitude = negative
        ? static_cast<uint64_t>(-(sum + 1)) + 1u
        : static_cast<uint64_t>(sum);
    const int64_t quotient = static_cast<int64_t>(magnitude >> 4);
    return cores3_r67_saturate_i32(negative ? -quotient : quotient);
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

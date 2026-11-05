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
// PR48: eliminate the per-sample variable 64-bit divide in the
// soft-peak limiter without changing a single PCM result.
//
// Original for magnitude m > knee:
//   knee + floor((m - knee)*head / ((m - knee) + head))
// = (knee + head) - ceil(head*head / ((m - knee) + head))
// = 32766 - floor((head*head - 1) / ((m - knee) + head)).
//
// Here knee=26000, head=6767. head*head-1 fits uint32_t.
// The denominator fits uint32_t even at abs(INT32_MIN).
// Only a 32-bit unsigned division remains; no int64 multiplication
// or variable int64 division. Fully exact C++ signed truncation,
// including INT32_MIN, negatives and limiter knee boundary.
static inline int32_t cores3_r67_true_peak_limiter(int32_t input) {
    constexpr uint32_t knee = 26000u;
    constexpr uint32_t head = 32767u - knee;
    const bool negative = input < 0;
    const uint32_t magnitude = negative
        ? static_cast<uint32_t>(-(static_cast<int64_t>(input)))
        : static_cast<uint32_t>(input);
    if (magnitude <= knee) return input;
    const uint32_t denominator = magnitude - knee + head;
    const uint32_t compressed =
        32766u - ((head * head - 1u) / denominator);
    return negative ? -static_cast<int32_t>(compressed)
                    : static_cast<int32_t>(compressed);
}

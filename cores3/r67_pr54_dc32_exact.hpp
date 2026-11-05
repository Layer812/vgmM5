#pragma once
#include <stdint.h>
#include <limits.h>

// PR54: exact integer DC blocker decay, optimized for the typical
// CoreS3 YM2151+C140 dynamic range. The legacy expression is
//
//     (state * 1019) / 1024     (signed truncation toward zero)
//
// For |state| = a, its exact unsigned magnitude is
//
//     a - ceil(5*a/1024)
//
// This path only uses 32-bit multiply/add/shift when 5*a+1023 fits
// uint32_t; otherwise it runs the original signed64 expression.
// All safe signed64 legacy inputs preserve the *exact* output,
// including negatives and values at the transition threshold.
//
// No reset, timing, pitch, sample rate or C140 behavior changes.
static inline int64_t cores3_pr54_dc_decay(int64_t state) {
    static constexpr int64_t fast_limit =
        static_cast<int64_t>((UINT32_MAX - 1023u) / 5u);
    if (state >= -fast_limit && state <= fast_limit) {
        const uint32_t a = static_cast<uint32_t>(
            state < 0 ? -state : state);
        const uint32_t decay = a - ((a * 5u + 1023u) >> 10);
        return state < 0 ? -static_cast<int64_t>(decay)
                         :  static_cast<int64_t>(decay);
    }
    return (state * INT64_C(1019)) / INT64_C(1024);
}

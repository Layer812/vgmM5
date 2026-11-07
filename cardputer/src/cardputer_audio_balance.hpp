#pragma once
#include <stdint.h>
#include <limits.h>

// Cardputer R7 ONLY: optional post-core Namco C140 level, before /8 master
// headroom and existing soft limiter. No shared-core changes for CoreS3.
// 256=1x (default and bit-identical to CoreS3), 512=2x, 1024=4x, 2048=8x.
// 8x mimics the original R6 PCM scale, NOT the full original FM timbre.
#ifndef VGMM5_CARDPUTER_C140_GAIN_Q8
#define VGMM5_CARDPUTER_C140_GAIN_Q8 256
#endif
#if VGMM5_CARDPUTER_C140_GAIN_Q8 != 256 && \
    VGMM5_CARDPUTER_C140_GAIN_Q8 != 512 && \
    VGMM5_CARDPUTER_C140_GAIN_Q8 != 1024 && \
    VGMM5_CARDPUTER_C140_GAIN_Q8 != 2048
#error "Allowed Cardputer C140 gain: 256, 512, 1024, 2048"
#endif
static inline int32_t cardputer_adjust_c140_level(int32_t sample) {
    const int64_t x = (int64_t)sample * (int64_t)VGMM5_CARDPUTER_C140_GAIN_Q8 / 256;
    return x > INT32_MAX ? INT32_MAX : x < INT32_MIN ? INT32_MIN : (int32_t)x;
}

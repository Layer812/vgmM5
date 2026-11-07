#pragma once
#include <stdint.h>
#include <limits.h>

// Cardputer-only post-core C140 balance. Never changes vgmM5lib, CoreS3,
// YM2151 pitch/envelopes, C140 sample decoding or DMA format.
//
// 256 = 1.00x: exactly the CoreS3 master mix for the same source.
// 512 = 2.00x: a moderate Cardputer PCM lift.
// 1024 = 4.00x, 2048 = 8.00x: comparative "classic R6-like" options.
// The original Cardputer R6 did more than boost C140, so 2048 is NOT
// guaranteed to restore identical FM tone or the complete R6 sound.
#ifndef VGMM5_CARDPUTER_C140_GAIN_Q8
#define VGMM5_CARDPUTER_C140_GAIN_Q8 256
#endif
#if VGMM5_CARDPUTER_C140_GAIN_Q8 < 0 || VGMM5_CARDPUTER_C140_GAIN_Q8 > 2048
#error "VGMM5_CARDPUTER_C140_GAIN_Q8 must be in 0..2048"
#endif

static inline int32_t cardputer_c140_adjust(int32_t input) {
    const int64_t scaled =
        (int64_t)input * VGMM5_CARDPUTER_C140_GAIN_Q8 / 256;
    if (scaled > INT32_MAX) return INT32_MAX;
    if (scaled < INT32_MIN) return INT32_MIN;
    return (int32_t)scaled;
}

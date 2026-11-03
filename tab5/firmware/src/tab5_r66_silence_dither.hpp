#pragma once
#include <stdint.h>
// R66 A experiment: -90.3dBFS stochastic PCM carrier ONLY inside an active
// VGM's digitally silent stereo frames, before Speaker.playRaw. No ultrasonic
// tone, no new Speaker buffer, no changes to R61 audio queue ownership.
// The I2S stream is already continuously fed during song playback.
static inline void tab5_r66_fill_silent_pcm(int16_t *left, int16_t *right,
                                            uint32_t *lfsr) {
    if (*left || *right) return;
    uint32_t x = *lfsr;
    if (x == 0) x = 0xA56959A5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *lfsr = x;
    *left = (x & 1u) ? 1 : -1;
    *right = (x & 2u) ? 1 : -1;
}

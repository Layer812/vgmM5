#pragma once
#include <stddef.h>
#include <stdint.h>

// CoreS3 R7: sound-chip count is not the number of software wrappers
// (YM2612 FM+DAC and OPN FM+SSG+PCM each occupy one physical chip).
static inline unsigned cores3_physical_chip_count(uint32_t sn_clock,
                                                  const uint32_t *other_clocks,
                                                  size_t count) {
    unsigned n = sn_clock ? ((sn_clock & 0x40000000u) ? 2u : 1u) : 0u;
    for (size_t i=0; i<count; ++i) if (other_clocks[i]) ++n;
    return n ? n : 1u;
}

// R66-A: user-confirmed Tab5 no-pop treatment. Do not alter nonzero
// sound samples, oscillator behavior, timing, or the idle/stop I2S path.
// Tiny +/-1 LSB pseudo-random stereo samples only for EXACT zero frames
// already being rendered during active VGM playback.
static inline void cores3_fill_silent_pcm(int16_t *left, int16_t *right, uint32_t *seed) {
    if (*left || *right) return;
    uint32_t x=*seed;
    if (!x) x=0xA56959A5u;
    x^=x<<13;
    x^=x>>17;
    x^=x<<5;
    *seed=x;
    *left=(x&1u)?1:-1;
    *right=(x&2u)?1:-1;
}

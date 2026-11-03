#pragma once
// R65: software-wrapper count is not the number of physical VGM chips.
// YM2612 has FM + DAC wrappers; YM2608/2610 have FM/SSG/PCM wrappers.
// Counting those wrappers in the master divider attenuates single-chip songs.
#include <stddef.h>
#include <stdint.h>
namespace tab5 {
static inline unsigned vgm_physical_chip_count(uint32_t sn_clock,
                                               const uint32_t* other_clocks,
                                               size_t other_count) {
    unsigned count = (sn_clock != 0u)
        ? ((sn_clock & 0x40000000u) ? 2u : 1u) : 0u;
    for (size_t i = 0; i < other_count; ++i)
        if (other_clocks[i] != 0u) ++count;
    return count ? count : 1u;
}

// A natural (non-manual) VGM EOF can land anywhere inside a 512-frame PCM
// block. Existing code queues the whole block and then cuts abruptly to
// silence. Fade only the last ~5.8ms of that final already-rendered block.
// Do not modify synth state, release callbacks, ring ownership, or manual STOP.
static inline void vgm_fade_final_pcm_block(int16_t* interleaved, size_t frames) {
    if (!interleaved || !frames) return;
    const size_t fade = (frames < 256u) ? frames : 256u;
    const size_t start = frames - fade;
    if (fade == 1u) {
        interleaved[2u * start] = 0;
        interleaved[2u * start + 1u] = 0;
        return;
    }
    for (size_t i = 0; i < fade; ++i) {
        const int32_t remaining = (int32_t)(fade - 1u - i);
        const int32_t denom = (int32_t)(fade - 1u);
        for (unsigned channel = 0; channel < 2u; ++channel) {
            const size_t offset = 2u * (start + i) + channel;
            interleaved[offset] = (int16_t)(((int32_t)interleaved[offset] * remaining) / denom);
        }
    }
}
} // namespace tab5

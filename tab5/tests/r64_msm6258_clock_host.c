/* R64: test actual Tab5 MSM6258 sample clock for VGM 1.71 0x94 codes. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "pcm_engine.h"

static PCMSoundEngine pcm;

int main(void) {
    const uint16_t dividers[4] = {1024, 768, 512, 512};
    for (unsigned code = 0; code < 4; ++code) {
        pcm_engine_init(&pcm, 44100);
        pcm_engine_set_msm6258_clock(&pcm, 4000000u, (uint8_t)code);
        const uint32_t expected_rate = 4000000u / dividers[code];
        const uint32_t expected_step = (uint32_t)(((uint64_t)expected_rate * 65536u) / 44100u);
        assert(pcm.msm6258.clock == 4000000u);
        assert(pcm.msm6258.step_size == expected_step);
        printf("PASS MSM6258 code=%u divider=%u rate=%u step=%u\n",
               code, (unsigned)dividers[code],
               (unsigned)expected_rate, (unsigned)pcm.msm6258.step_size);
    }
    // Reserved flags do not change the low two divider bits.
    pcm_engine_init(&pcm, 44100);
    pcm_engine_set_msm6258_clock(&pcm, 4000000u, 0xFCu);
    assert(pcm.msm6258.step_size == (uint32_t)(((uint64_t)(4000000u / 1024u) * 65536u) / 44100u));

    // Keep zero-clock fallback exactly as before.
    pcm_engine_init(&pcm, 44100);
    pcm_engine_set_msm6258_clock(&pcm, 0, 1);
    assert(pcm.msm6258.step_size == (uint32_t)(((uint64_t)15625u * 65536u) / 44100u));
    puts("PASS R64 actual Tab5 PCM engine MSM6258 v1.71 clock divider");
    return 0;
}

/* R63 C352 divider /4 verification against real Tab5 PCM engine. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "pcm_engine.h"

static PCMSoundEngine pcm;

static void c352_case(uint8_t header_d6, uint32_t master) {
    const uint32_t divider = header_d6 ? (uint32_t)header_d6 * 4u : 288u;
    const uint32_t native_hz = (master ? master : 24000000u) / divider;
    const uint32_t expected = (uint32_t)(((uint64_t)native_hz << 16) / 44100u);
    pcm_engine_init(&pcm, 44100);
    pcm_engine_namco_init(&pcm, master, 0xC2, header_d6);
    assert(pcm.c352_enabled && !pcm.c140_enabled);
    assert(pcm.namco.channels == 32u);
    assert(pcm.namco.step_scale == expected);
    // Register writes must inherit the corrected sample-rate step.
    pcm_engine_namco_write(&pcm, 2, 0x8000);
    assert(pcm.namco.ch[0].step_adjusted == (((uint64_t)0x8000u * expected) >> 16));
    printf("PASS C352 D6=%u divider=%u master=%u internal_hz=%u step=%u\n",
           (unsigned)header_d6, (unsigned)divider, (unsigned)master,
           (unsigned)native_hz, (unsigned)pcm.namco.step_scale);
}

static void c140_unchanged(uint8_t banking_type) {
    pcm_engine_init(&pcm, 44100);
    pcm_engine_namco_init(&pcm, 21390000u, 0xC1, banking_type);
    assert(pcm.c140_enabled && !pcm.c352_enabled);
    assert(pcm.namco.channels == 24u && pcm.namco.c140_type == banking_type);
    const uint32_t rate = 21390000u / 192u;
    assert(pcm.namco.step_scale == (uint32_t)(((uint64_t)rate << 16) / 44100u));
    printf("PASS C140 banking=%u unchanged\n", (unsigned)banking_type);
}

int main(void) {
    const uint8_t values[] = {0, 1, 36, 72, 144, 255};
    for (unsigned i = 0; i < sizeof(values); ++i)
        c352_case(values[i], 24192000u);
    c352_case(0, 0);
    c140_unchanged(0);
    c140_unchanged(1);
    puts("PASS R63 PCM actual-engine C352 divider + C140 guard");
    return 0;
}

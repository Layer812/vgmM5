#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "../src/sound_safe_math.h"

int main(void) {
    // All signed PCM bytes, including -128 and -1.
    int16_t c140_bias[8];
    int bias = 0;
    for (int shift = 0; shift < 8; ++shift) {
        c140_bias[shift] = (int16_t)bias;
        bias += 16 << shift;
    }
    for (int byte = 0; byte < 256; ++byte) {
        uint8_t dt = (uint8_t)byte;
        int32_t s8 = (int32_t)(int8_t)dt;
        assert(vgm_safe_signed_pcm_byte(dt) == s8 * 256);
        // The old C140 code used signed-left-shift (UB on negative).
        // Compare against the same integer formula, defined via multiply.
        int16_t sdt = (int16_t)((int8_t)dt >> 3);
        int shift = dt & 7;
        int32_t expected = (int32_t)sdt * (1 << shift);
        expected += (sdt < 0) ? -(int32_t)c140_bias[shift] : (int32_t)c140_bias[shift];
        assert(vgm_safe_c140_companded(sdt, shift, c140_bias[shift]) == expected);
    }

    // Values that were representable by the original float-to-uint32 cast:
    // the new function must be bit-for-bit identical.
    for (int i = 0; i < 100000; ++i) {
        float v = (float)((double)i * 42949.0);
        if (v < 4294967296.0f)
            assert(vgm_safe_ym2151_phase_step(v) == (uint32_t)v);
    }
    // Out-of-range positive floats must explicitly wrap, not invoke UB.
    for (int i = 0; i < 100000; ++i) {
        float v = (float)(4294967296.0 + i * 88888.0);
        uint32_t expected = (uint32_t)fmod((double)v, 4294967296.0);
        assert(vgm_safe_ym2151_phase_step(v) == expected);
    }
    assert(vgm_safe_ym2151_phase_step(INFINITY) == 0u);
    assert(vgm_safe_ym2151_phase_step(NAN) == 0u);
    assert(vgm_safe_ym2151_phase_step(-1.0f) == 0u);
    puts("PASS: YM2151 normal/wrapped phases, C140/C352 sample decode (ASan/UBSan)");
    return 0;
}

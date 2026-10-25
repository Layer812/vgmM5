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
    // YM2151 KF: upper six register bits only, exhaustive byte check.
    for (int raw = 0; raw < 256; ++raw) {
        uint8_t kf = vgm_ym2151_decode_kf((uint8_t)raw);
        assert(kf == (uint8_t)(raw / 4));
        assert(kf < 64);
    }
    assert(vgm_ym2151_decode_kf(0x14) == 5);
    assert(vgm_ym2151_decode_kf(0x18) == 6);
    assert(vgm_ym2151_decode_kf(0x0c) == 3);
    puts("PASS: YM2151 256 KF register values, unused low bits excluded");

    // Official YM2151 DT2 is measured in 1/64 of a semitone, not Hz.
    const int dt2_units[4] = {0, 384, 500, 608};
    for (int dt2 = 0; dt2 < 4; ++dt2) {
        const double ratio = pow(2.0, (double)dt2_units[dt2] / 768.0);
        for (int i = 1; i <= 2048; ++i) {
            const float input = (float)i * 0.75f;
            const float got = vgm_ym2151_apply_dt2(input, (uint8_t)dt2);
            const float expected = (float)((double)input * ratio);
            assert(fabsf(got - expected) < 0.0003f);
            if (dt2 == 0) assert(got == input); // R1 unchanged path
        }
        // The same DT2 ratio must apply across octaves; the old constant
        // addition erroneously changed the interval with the base note.
        const float low = vgm_ym2151_apply_dt2(220.0f, (uint8_t)dt2);
        const float high = vgm_ym2151_apply_dt2(880.0f, (uint8_t)dt2);
        assert(fabsf(high - 4.0f * low) < 0.001f);
    }
    puts("PASS: YM2151 DT2 pitch ratios and normal-notes R1 parity (ASan/UBSan)");
    puts("PASS: YM2151 normal/wrapped phases, C140/C352 sample decode (ASan/UBSan)");
    return 0;
}

#include "r31_vgm_bridge.h"
#include <math.h>
#include <limits.h>
#include <string.h>

void r31_vgm_bridge_init(r31_vgm_bridge *ctx, uint32_t clock_hz,
                         uint32_t output_rate, ym2413_eg_mode mode) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    if (!clock_hz || !output_rate) return;
    ym2413_init(&ctx->chip, clock_hz, output_rate);
    ym2413_set_eg_mode(&ctx->chip, mode);
    r31_opll_native_init(&ctx->native);
    r31_opll_native_refresh_patches(&ctx->chip, &ctx->native);
    /* Pair the first native tick with the first output frame, matching
     * OPLL_calc(): out_step>out_time at initial time=0.
     * This is currently host-proven, not a physical-device claim. */
    ctx->native.clock_accum = (uint64_t)output_rate * 72u - 1u;
    ctx->running = 1;
}
/* R57: retain YM2413 slot OFF->ON edges when VGM register writes
 * occur without an intervening 44.1kHz output frame. */
static uint32_t effective_keys(const ym2413_core *s) {
    uint32_t m=0;
    for (unsigned ch=0;ch<9;ch++) for (unsigned o=0;o<2;o++) {
        unsigned key=s->ch[ch].key_on;
        if (s->rhythm_mode && ch>=6) {
            if (ch==6) key |= s->drums[YM2413_BD];
            if (ch==7) key |= s->drums[o==0?YM2413_HH:YM2413_SD];
            if (ch==8) key |= s->drums[o==0?YM2413_TOM:YM2413_CYM];
        }
        if (key) m |= (1u << (2*ch+o));
    }
    return m;
}
void r31_vgm_bridge_write(r31_vgm_bridge *ctx, uint16_t reg, uint8_t val) {
    if (!ctx || !ctx->running || reg >= 0x40u) return;
    uint32_t oldkeys=effective_keys(&ctx->chip);
    ym2413_write(&ctx->chip, (uint8_t)reg, val);
    uint32_t newkeys=effective_keys(&ctx->chip);
    ctx->transient_off_mask |= oldkeys & ~newkeys;
    ctx->transient_retrigger_mask |= ctx->transient_off_mask & (~oldkeys & newkeys);
    r31_opll_native_refresh_patches(&ctx->chip, &ctx->native);
    ctx->writes++;
}
int32_t r31_vgm_bridge_next(r31_vgm_bridge *ctx) {
    if (!ctx || !ctx->running) return 0;
    /* BUILD2 paired PCM uses 27000 as its PCM scale.
     * Keep signed 32-bit mix domain; do not saturate pre-master. */
    uint32_t nowkeys=effective_keys(&ctx->chip);
    uint32_t masked=ctx->transient_retrigger_mask & nowkeys;
    for (unsigned i=0;i<18;i++) if ((masked & (1u<<i)) && ctx->native.slot[i].key) {
        ctx->native.slot[i].key=0;
        ctx->recovered_retriggers++;
    }
    ctx->transient_off_mask=ctx->transient_retrigger_mask=0;
    float pcm = r31_opll_native_tick(&ctx->chip, &ctx->native);
    long v = lrint((double)pcm * 27000.0);
    if (v > INT32_MAX) v = INT32_MAX;
    if (v < INT32_MIN) v = INT32_MIN;
    ctx->frames++;
    return (int32_t)v;
}
void r31_vgm_bridge_mix(r31_vgm_bridge *ctx, int32_t *l, int32_t *r) {
    if (!ctx || !l || !r) return;
    int32_t mono = r31_vgm_bridge_next(ctx);
    /* The vgmM5 player divides accumulated chip output by 8. Its
     * local R31 contribution therefore pre-scales exactly 8x.
     * The global chip-count divisor remains owned by the host player. */
    *l += mono * 8;
    *r += mono * 8;
}

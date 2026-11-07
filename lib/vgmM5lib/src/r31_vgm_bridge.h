#ifndef R31_VGM_BRIDGE_H
#define R31_VGM_BRIDGE_H
#include <stdint.h>
#include "ym2413_native.h"
#ifdef __cplusplus
extern "C" {
#endif
/* BUILD3 R0: host-verified independent YM2413 adapter. No emu2413 linkage.
 * The caller owns one instance per chip; no heap or audio-thread locks.
 * Call write() at VGM command timestamps and next() once per output frame.
 */
typedef struct {
    ym2413_core chip;
    r31_opll_native native;
    uint64_t writes;
    uint64_t frames;
    uint32_t transient_off_mask, transient_retrigger_mask;
    uint64_t recovered_retriggers;
    uint8_t running;
} r31_vgm_bridge;
void r31_vgm_bridge_init(r31_vgm_bridge *ctx, uint32_t clock_hz,
                         uint32_t output_rate, ym2413_eg_mode mode);
void r31_vgm_bridge_write(r31_vgm_bridge *ctx, uint16_t reg, uint8_t val);
/* Signed 32-bit PCM BEFORE master mix. No early clipping or heap allocations. */
int32_t r31_vgm_bridge_next(r31_vgm_bridge *ctx);
/* Main mixer output uses /8 headroom: deliberately factor it here.
 * This does not alter chip gain; x4 BD remains in the independent core.
 * The application is responsible for final clipping AFTER chip mixing. */
void r31_vgm_bridge_mix(r31_vgm_bridge *ctx, int32_t *l, int32_t *r);
#ifdef __cplusplus
}
#endif
#endif

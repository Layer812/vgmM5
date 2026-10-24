#ifndef VGMM5_YM2413_CORE_H
#define VGMM5_YM2413_CORE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define YM2413_CHANNELS 9
#define YM2413_DRUMS 5
/* Dedicated YM2413 register-plane state. No OPL2 structures or dependencies. */
typedef enum { YM2413_BD=0, YM2413_SD, YM2413_TOM, YM2413_CYM, YM2413_HH } ym2413_drum_id;
typedef enum { YM2413_STRICT=0, YM2413_SMOOTH=1 } ym2413_eg_mode;
typedef struct {
    uint16_t fnum;          /* 9 bits */
    uint8_t block;          /* 3 bits */
    uint8_t key_on, sustain;
    uint8_t instrument, volume;
    uint8_t rhythm_key;     /* active only in rhythm mode, CH6..8 */
} ym2413_channel;
typedef struct {
    uint8_t regs[0x40];
    uint8_t custom_patch[8];
    ym2413_channel ch[YM2413_CHANNELS];
    uint8_t rhythm_mode;
    uint8_t drums[YM2413_DRUMS];
    uint8_t drum_volume[YM2413_DRUMS];
    ym2413_eg_mode eg_mode;
    uint32_t clock_hz, sample_rate;
} ym2413_core;
void ym2413_init(ym2413_core *s, uint32_t clock_hz, uint32_t sample_rate);
void ym2413_set_eg_mode(ym2413_core *s, ym2413_eg_mode mode);
void ym2413_write(ym2413_core *s, uint8_t reg, uint8_t data);
/* Returns nominal fundamental frequency, NOT a measured synthesized pitch. */
double ym2413_nominal_hz(const ym2413_core *s, unsigned channel);
#ifdef __cplusplus
}
#endif
#endif

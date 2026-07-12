#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <esp_attr.h>
#include "esp_partition.h"
#include <esp_spi_flash.h>
#if __has_include(<spi_flash_mmap.h>)
#include <spi_flash_mmap.h>
#endif

// ─────────────────────────────────────────
// YM2612 DAC（8bitPCM ストリーム）
// ─────────────────────────────────────────
typedef struct {
    uint8_t dac_enable;
    uint8_t dac_data;
} YM2612_DAC;

// ─────────────────────────────────────────
// SegaPCM 構造体
// ─────────────────────────────────────────
#define SEGAPCM_MAX_BLOCKS 64

typedef struct {
    uint32_t start_addr;
    uint32_t size;
    const uint8_t* data;
    bool     is_allocated;
} SegaPCMBlock;

// ─────────────────────────────────────────
// Namco PCM (C140 / C352)
// ─────────────────────────────────────────
typedef struct {
    uint32_t start_addr;
    uint32_t size;
    const uint8_t *data;
    bool is_allocated;
} NamcoROMBlock;

typedef struct {
    // Shared by C140 and C352
    uint8_t  vol_l;
    uint8_t  vol_r;
    uint8_t  vol_rear_l;
    uint8_t  vol_rear_r;
    
    // C352 fields (MAME-style 8-field layout per channel)
    uint16_t freq;       // Field 1: Frequency / step value
    uint16_t start;      // Field 2: Start address (lower 16-bit)
    uint16_t end;        // Field 3: End address
    uint16_t loop;       // Field 4: Loop address
    uint16_t bank;       // Field 5: Bank (upper 16-bit for start/loop)
    uint16_t flags;      // Field 6: Mode/Flags (bit14=KeyOn, bit13=Loop, bit0=Reverse, bit3=muLaw, bit8=8bitPCM)
    
    // Legacy C140 compatibility
    uint16_t step;       // alias for freq
    uint16_t mode;       // alias for flags
    
    // Runtime state
    bool playing;
    uint32_t pos;        // current position (24-bit linear address for C352)
    uint32_t pos_frac;   // fractional position for interpolation (16.16 fixed point)
    int16_t  prev_sample;
    int16_t  last_sample;
    
    // C352 MAME-style counter for sub-sample interpolation
    uint32_t counter;    // accumulates freq; when >= 0x10000, advance ROM by 1
    
    // Latched values (captured at KeyOn)
    uint16_t latched_bank;
    uint16_t latched_start;
    uint16_t latched_end;
    uint16_t latched_loop;
    
    // Block cache
    int current_block;
} NamcoPCMChannel;

// Legacy alias for compatibility
typedef NamcoPCMChannel C352_Channel;

typedef struct {
    uint32_t clock;
    uint32_t step_scale;
    uint8_t  type;        // 0xC1 (C140) or 0xC2 (C352)
    uint8_t  channels;
    uint8_t  c140_type;   // 0=System 2, 1=System 21 (address mapping)
    uint32_t c140_clock;
    uint32_t c352_clock;
    uint32_t sample_rate;
    NamcoPCMChannel ch[32];
    NamcoROMBlock blocks[64];
    uint32_t block_count;
    bool     is_mmap;
    uint32_t rom_size;
} NamcoPCMEngine;

typedef struct {
    uint8_t  regs[256];       // 0x00-0x7F: ch params, 0x80-0xFF: ch status
    SegaPCMBlock blocks[SEGAPCM_MAX_BLOCKS];
    uint8_t  block_count;
    uint8_t  low[16];         // 下位8ビット（小数部）
    uint32_t step_acc[16];    // サンプルレート変換用アキュムレータ（16.16固定小数点）
    uint32_t step_scale;      // 事前計算: (clock/128 << 16) / sample_rate
    uint32_t clock;
    uint32_t bank_shift;
    uint32_t bank_mask;
    uint8_t  last_block[16];
    uint32_t unmapped_page[16];
    
    // キャッシュされたパラメータ（毎サンプルのレジスタパースを避けるため）
    uint32_t addr[16];
    uint32_t loop[16];
    uint32_t bank[16];
    uint8_t  end[16];
    uint8_t  delta[16];
    int32_t  vol_l[16];
    int32_t  vol_r[16];
    uint8_t  flags[16];
} SegaPCMEngine;

// ─────────────────────────────────────────
// OKI MSM6295（4chフレーズADPCM）
// ─────────────────────────────────────────
typedef struct {
    uint8_t  playing;
    uint32_t start_addr;
    uint32_t end_addr;
    uint32_t current_addr;
    uint32_t step_accum;
    uint32_t step_size;
    int32_t  adpcm_val;
    int32_t  adpcm_step;
    uint8_t  nibble_pos;
    int32_t  volume;
    int32_t  pan;
} OKI_Channel;

typedef struct {
    uint32_t sample_rate;
    uint32_t clock;
    uint8_t  pin7_state;
    OKI_Channel ch[4];
    const uint8_t* blocks[64];
    uint32_t block_offsets[64];
    uint32_t block_sizes[64];
    uint32_t block_count;
} OKI_MSM6295;

// ─────────────────────────────────────────
// OKI MSM6258（X68000用 1ch ADPCM ストリーム）
// ─────────────────────────────────────────
typedef struct {
    uint8_t  playing;
    int32_t  adpcm_val;
    int32_t  adpcm_step_idx;
    uint8_t  nibble_sel;    // 0=上位ニブル待ち, 1=下位ニブル待ち
    uint32_t step_accum;
    uint32_t step_size;     // サンプルレート変換用 固定小数(×65536)

    const uint8_t* blocks[64];
    uint32_t data_pos;      // 現在の読み取り位置
    uint32_t data_end;      // 終端位置

    uint32_t block_offsets[64];
    uint32_t block_sizes[64];
    uint32_t block_count;

    uint32_t clock;
} MSM6258;

// ─────────────────────────────────────────
// YM2608/YM2610 OPN PCM (Rhythm / ADPCM-A / ADPCM-B)
// ─────────────────────────────────────────
#define OPN_ADPCMA_CHANNELS 6

typedef struct {
    uint32_t start_addr;
    uint32_t end_addr;
    uint32_t pos;
    uint32_t pos_frac;
    uint32_t step;       // Freq step
    uint8_t  vol_l;
    uint8_t  vol_r;
    uint8_t  pan;
    bool     playing;
    // ADPCM decode state
    int32_t  adpcm_val;
    int32_t  adpcm_step_idx;
    uint8_t  regs[0x06]; // Channel specific registers
} OPN_ADPCM_Channel;

typedef struct {
    uint32_t start_addr;
    uint32_t end_addr;
    uint32_t pos;
    uint32_t pos_frac;
    uint32_t step;       // Freq step based on delta-N
    uint8_t  vol_l;
    uint8_t  vol_r;
    uint8_t  pan;
    bool     playing;
    // Delta-T ADPCM decode state
    int32_t  adpcm_val;
    int32_t  adpcm_step_idx;
    uint8_t  regs[0x1C]; // Channel specific registers
} OPN_DeltaT_Channel;

typedef struct {
    OPN_ADPCM_Channel  ch_a[OPN_ADPCMA_CHANNELS]; // Rhythm / ADPCM-A
    OPN_DeltaT_Channel ch_b;                      // ADPCM-B (Delta-T)
    uint8_t  regs[0x30];                          // Global registers
    
    // ROM blocks
    const uint8_t* adpcma_blocks[64];
    uint32_t adpcma_block_offsets[64];
    uint32_t adpcma_block_sizes[64];
    uint32_t adpcma_block_count;

    const uint8_t* adpcmb_blocks[64];
    uint32_t adpcmb_block_offsets[64];
    uint32_t adpcmb_block_sizes[64];
    uint32_t adpcmb_block_count;
    
    uint32_t clock;
} OPN_PCM_Matrix;

// ─────────────────────────────────────────
// PCMサウンドエンジン全体
// ─────────────────────────────────────────
typedef struct {
    uint32_t sample_rate;

    YM2612_DAC     ym2612_dac;
    OKI_MSM6295    oki;
    MSM6258        msm6258;
    SegaPCMEngine  segapcm;
    NamcoPCMEngine namco;
    OPN_PCM_Matrix opn;

    bool c140_enabled;
    bool c352_enabled;

    const uint8_t* ym2612_pcm_blocks[64];
    uint32_t ym2612_pcm_offsets[64];
    uint32_t ym2612_pcm_sizes[64];
    uint32_t ym2612_pcm_block_count;
} PCMSoundEngine;

extern PCMSoundEngine g_pcm_engine;

#ifdef __cplusplus
extern "C" {
#endif

void pcm_engine_init(PCMSoundEngine *engine, uint32_t sample_rate);
void pcm_engine_free(PCMSoundEngine *engine);

void pcm_engine_add_data_block(PCMSoundEngine *engine, uint8_t type, const uint8_t* data, uint32_t size);

// YM2612 DAC
uint8_t pcm_engine_get_ym2612_byte(PCMSoundEngine *engine, uint32_t offset);
void pcm_engine_write_ym2612(PCMSoundEngine *engine, uint16_t addr, uint8_t data);

// OKI MSM6295
void pcm_engine_write_oki(PCMSoundEngine *engine, uint8_t data);
void pcm_engine_set_oki_clock(PCMSoundEngine *engine, uint32_t clock);

// OKI MSM6258 (X68000)
void pcm_engine_set_msm6258_clock(PCMSoundEngine *engine, uint32_t clock);
void pcm_engine_write_msm6258(PCMSoundEngine *engine, uint8_t reg, uint8_t data);
void pcm_engine_msm6258_start_stream(PCMSoundEngine *engine, uint32_t offset, uint32_t length);

// SegaPCM
void pcm_engine_segapcm_init(PCMSoundEngine *engine, uint32_t clock, uint32_t interface_reg);
void pcm_engine_segapcm_write(PCMSoundEngine *engine, uint8_t reg, uint8_t data);

// Namco C140 / C352
void pcm_engine_namco_init(PCMSoundEngine *engine, uint32_t clock, uint8_t type);
void pcm_engine_namco_write(PCMSoundEngine *engine, uint16_t addr, uint16_t data);
void pcm_engine_namco_mmap(PCMSoundEngine *engine, uint32_t start_addr, const uint8_t *data, uint32_t size);

// OPN (YM2608 / YM2610) ADPCM
void pcm_engine_opn_init(PCMSoundEngine *engine, uint32_t clock);
void pcm_engine_write_opn_rhythm(PCMSoundEngine *engine, uint8_t reg, uint8_t data);
void pcm_engine_write_opn_adpcma(PCMSoundEngine *engine, uint8_t reg, uint8_t data);
void pcm_engine_write_opn_adpcmb(PCMSoundEngine *engine, uint8_t reg, uint8_t data);
void pcm_engine_opn_tick(PCMSoundEngine *engine, int32_t *out_l, int32_t *out_r);

void IRAM_ATTR pcm_engine_tick(PCMSoundEngine *engine, int32_t *out_l, int32_t *out_r);

#ifdef __cplusplus
}
#endif

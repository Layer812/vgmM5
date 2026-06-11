#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <esp_attr.h>

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
    uint8_t* data;
    bool     is_allocated;
} SegaPCMBlock;

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
    uint8_t* rom_data;
    uint32_t rom_size;
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

    uint8_t* rom_data;      // PCM ROMデータ（0x67 type=0x88, 0x04で格納）
    uint32_t rom_size;
    uint32_t data_pos;      // 現在の読み取り位置
    uint32_t data_end;      // 終端位置

    uint32_t block_offsets[64];
    uint32_t block_sizes[64];
    uint32_t block_count;

    uint32_t clock;
} MSM6258;

// ─────────────────────────────────────────
// PCMサウンドエンジン全体
// ─────────────────────────────────────────
typedef struct {
    uint32_t sample_rate;

    YM2612_DAC     ym2612_dac;
    OKI_MSM6295    oki;
    MSM6258        msm6258;
    SegaPCMEngine  segapcm;

    uint8_t* ym2612_pcm_block;
    uint32_t ym2612_pcm_size;
} PCMSoundEngine;

extern PCMSoundEngine g_pcm_engine;

#ifdef __cplusplus
extern "C" {
#endif

void pcm_engine_init(PCMSoundEngine *engine, uint32_t sample_rate);
void pcm_engine_free(PCMSoundEngine *engine);

void pcm_engine_add_data_block(PCMSoundEngine *engine, uint8_t type, uint8_t* data, uint32_t size);

// YM2612 DAC
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

void IRAM_ATTR pcm_engine_tick(PCMSoundEngine *engine, int32_t *out_l, int32_t *out_r);

#ifdef __cplusplus
}
#endif

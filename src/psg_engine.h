#pragma once
#include <stdint.h>
#include <stdbool.h>

// ─────────────────────────────────────────
// SN76489 (DCSG) / Sega VDP PSG
// ─────────────────────────────────────────
typedef struct {
    uint16_t freq;      // 10bit 周波数レジスタ (値が大きいほど低音)
    uint8_t  vol;       // 4bit ボリュームレジスタ (0=最大, 15=無音)
    int32_t  counter;   // 周期カウンタ
    int8_t   out_state; // 現在の出力状態 (1 or -1)
} SN76489_Tone;

typedef struct {
    uint8_t  ctrl;          // ノイズ制御レジスタ (3bit)
    uint8_t  vol;           // 4bit ボリュームレジスタ
    int32_t  counter;       // 周期カウンタ
    int8_t   out_state;     // 現在の出力状態
    uint16_t shift_reg;     // ノイズ生成用 LFSR (16bitシフトレジスタ)
} SN76489_Noise;

typedef struct {
    uint32_t clock;         // マスタークロック (例: 3579545 Hz)
    uint32_t sample_rate;   // 出力サンプリングレート (例: 44100 Hz)
    
    SN76489_Tone  tone[3];
    SN76489_Noise noise;
    
    uint8_t  latched_ch;    // ラッチされているチャンネル (0-3)
    uint8_t  latched_type;  // ラッチされているタイプ (0=Tone, 1=Vol)
    
    // 同期・ダウンサンプリング用のアキュムレータ
    uint32_t step_accum;
    uint32_t step_size;
    
    int32_t vol_table[16];  // 非線形ボリュームのルックアップテーブル
} SN76489;

// ─────────────────────────────────────────
// PSGサウンドエンジン全体
// ─────────────────────────────────────────
typedef struct {
    uint32_t sample_rate;
    SN76489  sn76489;
    // (将来的に AY-3-8910 などをここに追加)
} PSGSoundEngine;

extern PSGSoundEngine g_psg_engine;

#ifdef __cplusplus
extern "C" {
#endif

void psg_engine_init(PSGSoundEngine *engine, uint32_t sample_rate, uint32_t sn_clock);
void psg_engine_write_sn76489(PSGSoundEngine *engine, uint8_t data);
void psg_engine_update(PSGSoundEngine *engine, int32_t *out_l, int32_t *out_r);

#ifdef __cplusplus
}
#endif
#pragma once
#include <stdint.h>
#include <stdbool.h>

// ─────────────────────────────────────────
// AY-3-8910 (SSG) - YM2610内蔵 / 単体チップ
// ─────────────────────────────────────────
typedef struct {
    uint8_t  regs[16];      // 16個のレジスタ
    
    // トーンジェネレータ
    int32_t  tone_cnt[3];   // カウンタ
    int32_t  tone_period[3];// 周期
    int8_t   tone_out[3];   // 出力状態 (0 or 1)
    
    // ノイズジェネレータ
    int32_t  noise_cnt;
    int32_t  noise_period;
    uint32_t rng;           // 乱数生成用 LFSR
    int8_t   noise_out;
    
    // エンベロープジェネレータ
    int32_t  env_cnt;
    int32_t  env_period;
    int8_t   env_vol;       // 現在のエンベロープ音量 (0-15)
    uint8_t  env_step;      // エンベロープのステップ状態
    bool     env_hold;
    bool     env_alt;
    bool     env_attack;
    bool     env_holding;
    
    // 同期用
    uint32_t step_accum;
    uint32_t step_size;
    
    int32_t  vol_table[16]; // 非線形ボリュームテーブル
} SSGSoundEngine;

extern SSGSoundEngine g_ssg_engine;

#ifdef __cplusplus
extern "C" {
#endif

void ssg_engine_init(SSGSoundEngine *engine, uint32_t sample_rate, uint32_t clock);
void ssg_engine_write(SSGSoundEngine *engine, uint8_t reg, uint8_t val);
void ssg_engine_update(SSGSoundEngine *engine, int32_t *out_l, int32_t *out_r);

#ifdef __cplusplus
}
#endif
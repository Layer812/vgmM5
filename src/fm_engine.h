#pragma once
#include <esp_attr.h>
#include <stdint.h>
#include <stdbool.h>

// ★ OPL2/OPLLの9チャンネルに対応
#define FM_CHANNELS 9
#define OPS_PER_CH 4
#define TOTAL_OPS (FM_CHANNELS * OPS_PER_CH) 

// ★ チップタイプの定義
#define CHIP_YM2151 0
#define CHIP_YM2612 1
#define CHIP_YM3438 2
#define CHIP_YM3812 3 // OPL2
#define CHIP_YM2413 4 // OPLL
#define CHIP_YM2203 5 // OPN

typedef enum {
    EG_OFF = 0, EG_ATTACK, EG_DECAY, EG_SUSTAIN, EG_RELEASE
} EGState;

typedef struct {
    uint32_t phase;
    int32_t  output;
    uint32_t phase_step;
    int32_t  tl_atten; 

    uint8_t  env_state; 
    int32_t  env_level; 
    int32_t  ar_step;   
    int32_t  dr_step;   
    int32_t  d2r_step;  
    int32_t  sl_level;  
    int32_t  rr_step;   
    
    uint8_t  mul; 
    uint8_t  am_enable;
    uint8_t  ar;
    uint8_t  dr;
    uint8_t  d2r;
    uint8_t  rr;
    uint8_t  ks;
    uint8_t  ksl; // ★ 追加: KSL (Key Scale Level)
    int32_t  ksl_atten_val; // 計算済みのKSL減衰量
    uint8_t  dt2;

    // ★ OPL2用の波形選択フラグ
    uint8_t  wave_sel; 

    // ★ Data-Driven Routing用のバス・インデックス (0〜4)
    uint8_t  src_bus; 
    uint8_t  dst_bus;

} OperatorState;

typedef struct {
    uint8_t pan_l[FM_CHANNELS];
    uint8_t pan_r[FM_CHANNELS];
    uint8_t algo[FM_CHANNELS];     
    uint8_t fb_shift[FM_CHANNELS]; 
    int32_t fb_memory[FM_CHANNELS][2];
    uint32_t ym2151_freq_tab[13][64];
    uint32_t ym2612_fnum_tab[2048]; // ★ YM2612 F-Number 事前計算テーブル

    uint8_t kc[FM_CHANNELS]; 
    uint8_t kf[FM_CHANNELS]; 
    
    uint8_t pms[FM_CHANNELS]; 
    uint8_t ams[FM_CHANNELS]; 

    OperatorState ops[TOTAL_OPS];
    
    // ★ LFO機能
    uint32_t lfo_phase;
    uint32_t lfo_step;
    uint8_t lfo_divider;
    int32_t cached_global_pm;
    int32_t cached_global_am;
    uint32_t lfo_am_phase; // OPL2 LFO用 (3.7Hz)
    uint32_t lfo_pm_phase; // OPL2 LFO用 (6.1Hz)
    uint8_t  pmd; 
    uint8_t  amd; 
    uint8_t  lfo_wave;

    // ★ OPL2 リズムモード / YM2151 ノイズ
    uint8_t  rhythm_enable;
    uint8_t  noise_enable;
    uint8_t  noise_freq;
    uint32_t noise_phase;
    uint32_t noise_step;
    uint32_t noise_rng; // ノイズジェネレータ用 (汎用・リズム共用)

    uint32_t sample_rate;
    uint32_t clock;
    uint8_t  chip_type; 

    // YM2612 / OPL specific
    uint16_t f_number[FM_CHANNELS];
    uint8_t  block[FM_CHANNELS];
    uint16_t ch3_f_number[3]; // For YM2612/YM2203 CH3 Special Mode (Op2, Op3, Op1)
    uint8_t  ch3_block[3];
    
    uint32_t opl2_lfo_am_step;
    uint32_t opl2_lfo_pm_step;
    uint64_t fchip_step; // ★ Hardware frequency wrap-around step for anti-aliasing
    
    // ★ 事前計算ファクタ (double撲滅 & 高速化)
    float phase_step_factor;
    float ym2612_step_factor;
    float opl2_step_factor;
    
    int32_t prev_l;
    int32_t prev_r;
} FMSoundEngine;

extern FMSoundEngine g_fm_engine;
#ifdef __cplusplus
extern "C" {
#endif

void fm_engine_init(FMSoundEngine *engine, uint32_t sample_rate, uint32_t clock, uint8_t chip_type);
void fm_engine_tick(FMSoundEngine *engine, int32_t *out_l, int32_t *out_r);
void fm_engine_register_write(FMSoundEngine *engine, uint16_t addr, uint8_t data);
void fm_engine_write_ym2612(FMSoundEngine *engine, uint8_t port, uint8_t addr, uint8_t data);
void fm_engine_write_opl(FMSoundEngine *engine, uint8_t addr, uint8_t data);

#ifdef __cplusplus
}
#endif
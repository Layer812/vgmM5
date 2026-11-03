#pragma once
#include <stdint.h>
#include <stdbool.h>

// ─────────────────────────────────────────
// Konami SCC (K051649 / K052539) 波形メモリ音源
// ─────────────────────────────────────────
typedef struct {
    int8_t   wave[5][32];   // 5チャンネル x 32バイトの波形RAM (-128〜127)
    uint16_t freq[5];       // 12bit 周波数レジスタ
    uint8_t  vol[5];        // 4bit ボリュームレジスタ (0-15)
    uint8_t  enable;        // チャンネルON/OFFフラグ
#if defined(IS_CORES3)
    bool     is_scc_plus;   // R62 CoreS3: independent K052539 5th waveform bank
#endif

    uint32_t phase[5];      // 再生位置（16.16 固定小数点）
    uint32_t step_size[5];  // 1サンプルあたりの進行ステップ

    uint32_t clock;
    uint32_t sample_rate;
} SCCSoundEngine;

extern SCCSoundEngine g_scc_engine;

#ifdef __cplusplus
extern "C" {
#endif

void scc_engine_init(SCCSoundEngine *engine, uint32_t sample_rate, uint32_t clock);
#if defined(IS_CORES3)
void scc_engine_set_variant(SCCSoundEngine *engine, bool is_scc_plus);
#endif
void scc_engine_write(SCCSoundEngine *engine, uint8_t port, uint8_t reg, uint8_t data);
void scc_engine_tick(SCCSoundEngine *engine, int32_t *out_l, int32_t *out_r);

#ifdef __cplusplus
}
#endif
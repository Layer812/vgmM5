#include "ssg_engine.h"
#include <string.h>
#include <math.h>
#include <esp_attr.h>

SSGSoundEngine g_ssg_engine;

void ssg_engine_init(SSGSoundEngine *engine, uint32_t sample_rate, uint32_t clock) {
    memset(engine, 0, sizeof(SSGSoundEngine));
    
    // AY-3-8910 (SSG) の内部基準クロックは通常 MasterClock / 8 で動作するようにモデリングします。
    // トーン生成部でトグルごとに period ティック進むため、フルサイクルは 16 * period クロックとなりハードウェア仕様と一致します。
    uint32_t ssg_step_clock = clock / 8;
    engine->step_size = (uint32_t)(((uint64_t)ssg_step_clock << 16) / sample_rate);
    
    engine->rng = 1; // ノイズ用LFSR初期値

    // ボリュームテーブル作成 (対数カーブ)
    const double MAX_VOL = 12000.0;
    for (int i = 0; i < 16; i++) {
        if (i == 0) engine->vol_table[i] = 0;
        else engine->vol_table[i] = (int32_t)(MAX_VOL * pow(10.0, (i - 15) * 3.0 / 20.0));
    }
}

void ssg_engine_write(SSGSoundEngine *engine, uint8_t reg, uint8_t val) {
    if (reg > 15) return;
    engine->regs[reg] = val;

    switch (reg) {
        case 0: case 1: engine->tone_period[0] = (engine->regs[0] | ((engine->regs[1] & 0x0F) << 8)); break;
        case 2: case 3: engine->tone_period[1] = (engine->regs[2] | ((engine->regs[3] & 0x0F) << 8)); break;
        case 4: case 5: engine->tone_period[2] = (engine->regs[4] | ((engine->regs[5] & 0x0F) << 8)); break;
        case 6: engine->noise_period = (val & 0x1F) * 2; break; // ノイズ周期
        case 11: case 12: engine->env_period = (engine->regs[11] | (engine->regs[12] << 8)); break;
        case 13: 
            // エンベロープ形状のリセットとフラグ設定
            engine->env_cnt = 0;
            engine->env_attack  = (val & 0x04) ? true : false;
            engine->env_alt     = (val & 0x02) ? true : false;
            engine->env_hold    = (val & 0x01) ? true : false;
            engine->env_holding = false;
            engine->env_vol     = engine->env_attack ? 0 : 15;
            
            // Holdフラグがない場合(Bit3=0)、Bit0-2は無視してアタックなし・ホールドあり・オルタネイトなしになる
            if (!(val & 0x08)) {
                engine->env_attack = false;
                engine->env_hold = true;
                engine->env_alt = false;
                engine->env_vol = 15;
            }
            break;
    }
}

void IRAM_ATTR ssg_engine_update(SSGSoundEngine *engine, int32_t *out_l, int32_t *out_r) {
    
    // トーンジェネレータ
    for (int i = 0; i < 3; i++) {
        uint32_t period = (uint32_t)engine->tone_period[i];
        if (period == 0) period = 1;
        uint32_t period_fp = period << 16;
        
        uint32_t cnt = (uint32_t)engine->tone_cnt[i] + engine->step_size;
        if (cnt >= period_fp) {
            uint32_t toggles = cnt / period_fp;
            cnt %= period_fp;
            // エイリアシング対策: 1サンプルで2回以上反転する場合はHIGHに固定
            if (toggles >= 2) {
                engine->tone_out[i] = 1;
            } else if (toggles == 1) {
                engine->tone_out[i] ^= 1;
            }
        }
        engine->tone_cnt[i] = (int32_t)cnt;
    }

    // ノイズジェネレータ
    uint32_t n_period = (uint32_t)engine->noise_period;
    if (n_period == 0) n_period = 1;
    // ★ 修正: AYのノイズはトーンの2倍の周期で進む
    uint32_t n_period_fp = (n_period * 2) << 16;
    
    uint32_t n_cnt = (uint32_t)engine->noise_cnt + engine->step_size;
    if (n_cnt >= n_period_fp) {
        uint32_t toggles = n_cnt / n_period_fp;
        n_cnt %= n_period_fp;
        for (uint32_t t = 0; t < toggles; t++) {
            engine->rng ^= (((engine->rng & 1) ^ ((engine->rng >> 3) & 1)) << 17);
            engine->rng >>= 1;
        }
        engine->noise_out = engine->rng & 1;
    }
    engine->noise_cnt = (int32_t)n_cnt;

    // エンベロープジェネレータ
    uint32_t e_period = (uint32_t)engine->env_period;
    if (e_period == 0) e_period = 1;
    // エンベロープのステップ周波数はハードウェア仕様で MasterClock / (256 * EP) です。
    // step_size は (clock/8) ベースで計算されています。
    // そのため、閾値を (e_period * 32) にすることで、正確に clock/256 のステップ周波数になります。
    uint64_t e_period_fp = ((uint64_t)e_period * 32) << 16;
    
    uint64_t e_cnt = engine->env_cnt + engine->step_size;
    if (e_cnt >= e_period_fp) {
        uint32_t toggles = (uint32_t)(e_cnt / e_period_fp);
        e_cnt %= e_period_fp;
        for (uint32_t t = 0; t < toggles; t++) {
            if (!engine->env_holding) {
                if (engine->env_attack) engine->env_vol++;
                else                    engine->env_vol--;

                if (engine->env_vol < 0 || engine->env_vol > 15) {
                    if (engine->env_hold) {
                        engine->env_vol = (engine->env_vol < 0) ? 0 : 15;
                        if (engine->env_alt) engine->env_vol ^= 15;
                        engine->env_holding = true;
                    } else {
                        if (engine->env_alt) engine->env_attack = !engine->env_attack;
                        engine->env_vol = engine->env_attack ? 0 : 15;
                    }
                }
            }
        }
    }
    engine->env_cnt = e_cnt;

    // ミキシング
    int32_t mix = 0;
    uint8_t mixer_reg = engine->regs[7];

    for (int i = 0; i < 3; i++) {
        bool tone_en  = !(mixer_reg & (1 << i));
        bool noise_en = !(mixer_reg & (1 << (i + 3)));

        uint8_t vol_mode = engine->regs[8 + i];
        uint8_t vol = (vol_mode & 0x10) ? engine->env_vol : (vol_mode & 0x0F);

        bool out = false;
        if (tone_en && noise_en) out = engine->tone_out[i] & engine->noise_out;
        else if (tone_en)        out = engine->tone_out[i];
        else if (noise_en)       out = engine->noise_out;
        else                     out = true; // ToneもNoiseもOFFの場合、常にHIGH

        if (out) mix += engine->vol_table[vol];
    }

    *out_l += mix;
    *out_r += mix;
}
#include "ssg_engine.h"
#include <string.h>
#include <math.h>

SSGSoundEngine g_ssg_engine;

void ssg_engine_init(SSGSoundEngine *engine, uint32_t sample_rate, uint32_t clock) {
    memset(engine, 0, sizeof(SSGSoundEngine));
    
    // AY-3-8910は入力クロックを8分周して内部のマスタークロックとする
    // YM2610内蔵の場合はさらに分周比が変わる事がありますが、一旦標準のAY8910として計算
    engine->step_size = (uint32_t)(((uint64_t)(clock / 8) << 16) / sample_rate);
    engine->rng = 1; // ノイズ用LFSR初期値

    // ボリュームテーブル作成 (対数カーブ)
    const double MAX_VOL = 12000.0; // Halved to account for +/- swing
    for (int i = 0; i < 16; i++) {
        if (i == 0) engine->vol_table[i] = 0;
        else engine->vol_table[i] = (int32_t)(MAX_VOL * pow(10.0, -(15 - i) * 2.0 / 20.0));
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
            engine->env_step = 0;
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
            }
            break;
    }
}

void ssg_engine_update(SSGSoundEngine *engine, int32_t *out_l, int32_t *out_r) {
    engine->step_accum += engine->step_size;

    while (engine->step_accum >= (1 << 16)) {
        engine->step_accum -= (1 << 16);

        // トーンの更新
        for (int i = 0; i < 3; i++) {
            if (engine->tone_cnt[i] > 0) engine->tone_cnt[i]--;
            if (engine->tone_cnt[i] <= 0) {
                engine->tone_cnt[i] = engine->tone_period[i] > 0 ? engine->tone_period[i] : 1;
                engine->tone_out[i] ^= 1;
            }
        }

        // ノイズの更新
        if (engine->noise_cnt > 0) engine->noise_cnt--;
        if (engine->noise_cnt <= 0) {
            engine->noise_cnt = engine->noise_period > 0 ? engine->noise_period : 1;
            engine->rng ^= (((engine->rng & 1) ^ ((engine->rng >> 3) & 1)) << 17);
            engine->rng >>= 1;
            engine->noise_out = engine->rng & 1;
        }

        // エンベロープの更新
        if (!engine->env_holding) {
            if (engine->env_cnt > 0) engine->env_cnt--;
            if (engine->env_cnt <= 0) {
                engine->env_cnt = engine->env_period > 0 ? engine->env_period : 1;
                
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

    // ミキシング
    int32_t mix = 0;
    uint8_t mixer_reg = engine->regs[7];

    for (int i = 0; i < 3; i++) {
        bool tone_en  = !(mixer_reg & (1 << i));
        bool noise_en = !(mixer_reg & (1 << (i + 3)));
        
        bool out = false;
        if (tone_en && noise_en) out = engine->tone_out[i] & engine->noise_out;
        else if (tone_en)        out = engine->tone_out[i];
        else if (noise_en)       out = engine->noise_out;
        else                     out = true; // 両方OFFの場合は常にHIGH

        uint8_t vol_reg = engine->regs[8 + i];
        uint8_t vol = (vol_reg & 0x10) ? engine->env_vol : (vol_reg & 0x0F);
        if (out) {
            mix += engine->vol_table[vol];
        } else {
            mix -= engine->vol_table[vol];
        }
    }

    *out_l += mix;
    *out_r += mix;
}
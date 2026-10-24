#include "scc_engine.h"
#include <string.h>

SCCSoundEngine g_scc_engine;

void scc_engine_init(SCCSoundEngine *engine, uint32_t sample_rate, uint32_t clock) {
    memset(engine, 0, sizeof(SCCSoundEngine));
    engine->sample_rate = sample_rate;
    // SCCの内部クロック（通常アーケード/MSX共に約1.78MHz相当）
    engine->clock = clock; 
}

// チャンネルのフェーズステップ（再生速度）を更新する
static void update_scc_step(SCCSoundEngine *engine, int ch) {
    // SCCの実機仕様：周波数は clock / (32 * (freq + 1))
    // 1サンプリングあたりの波形インデックス(32段階)の進み具合を固定小数(16.16)で計算
    uint32_t f = engine->freq[ch];
    if (f == 0) {
        engine->step_size[ch] = 0;
    } else {
        uint64_t step = ((uint64_t)engine->clock << 16) / ((f + 1) * engine->sample_rate);
        engine->step_size[ch] = (uint32_t)step;
    }
}

void scc_engine_write(SCCSoundEngine *engine, uint8_t port, uint8_t reg, uint8_t data) {
    // VGM規格における K051649 (SCC) / K052539 (SCC+) の書き込みフォーマットは、
    // 基本的にMAMEコアのポートマッピングに準拠しています：
    // port 0: 波形RAM (SCC: 0x00-0x7F, SCC+: 0x00-0x9F)
    // port 1: 周波数 (0x00-0x09)
    // port 2: ボリューム (0x00-0x04)
    // port 3: チャンネルON/OFF (0x00 または SCC+の場合は 0x00/0xAF など)

    if (port == 1 && reg <= 0x09) {
        // MAME: 周波数レジスタ
        int ch = reg / 2;
        if (reg % 2 == 0) {
            engine->freq[ch] = (engine->freq[ch] & 0x0F00) | data;
        } else {
            engine->freq[ch] = (engine->freq[ch] & 0x00FF) | ((data & 0x0F) << 8);
        }
        update_scc_step(engine, ch);
        return;
    }
    else if (port == 2 && reg <= 0x04) {
        // MAME: ボリュームレジスタ
        engine->vol[reg] = data & 0x0F;
        return;
    }
    else if (port == 3) {
        // MAME: チャンネルON/OFF
        // (レジスタは通常0x00だが、VGMによってはMSXのオフセット0x8Fや0xAFをそのまま送ってくることもある)
        engine->enable = data;
        return;
    }

    // 古いVGMやMSXダイレクトマッピングVGMへの救済措置
    // port=0 に全レジスタが書き込まれるケース
    if (port == 0) {
        if (reg < 0x80) {
            // 波形RAM CH1〜CH4 (0x00 - 0x7F)
            int ch = reg / 0x20;
            int offset = reg % 0x20;
            engine->wave[ch][offset] = (int8_t)data;
            if (ch == 3) {
                engine->wave[4][offset] = (int8_t)data; 
            }
        }
        else if (reg >= 0x80 && reg <= 0x9F) {
            // SCC+ 波形RAM CH5 (0x80 - 0x9F)
            int ch = reg / 0x20;
            int offset = reg % 0x20;
            engine->wave[ch][offset] = (int8_t)data;
        }
        else if (reg >= 0x80 && reg <= 0x89) {
            // MSX: 周波数レジスタ (0x80 - 0x89)
            int ch = (reg - 0x80) / 2;
            if (reg % 2 == 0) {
                engine->freq[ch] = (engine->freq[ch] & 0x0F00) | data;
            } else {
                engine->freq[ch] = (engine->freq[ch] & 0x00FF) | ((data & 0x0F) << 8);
                // 周波数が更新されたタイミングで位相をリセットして音の途切れを防止
                engine->phase[ch] = 0;
            }
            update_scc_step(engine, ch);
        }
        else if (reg >= 0x8A && reg <= 0x8E) {
            // MSX: ボリューム (0x8A - 0x8E)
            int ch = reg - 0x8A;
            engine->vol[ch] = data & 0x0F;
        }
        else if (reg == 0x8F) {
            // MSX: チャンネルON/OFF (0x8F)
            uint8_t changed = engine->enable ^ data;
            engine->enable = data;
            // チャンネルのONになったものは位相をリセットして音の途切れを防止
            for (int ch = 0; ch < 5; ch++) {
            if ((changed & (1 << ch)) && (data & (1 << ch))) {
                engine->phase[ch] = 0;
            }
        }
        }
        else if (reg >= 0xA0 && reg <= 0xA9) {
            // MSX SCC+: 周波数レジスタ (0xA0 - 0xA9)
            int ch = (reg - 0xA0) / 2;
            if (reg % 2 == 0) {
                engine->freq[ch] = (engine->freq[ch] & 0x0F00) | data;
            } else {
                engine->freq[ch] = (engine->freq[ch] & 0x00FF) | ((data & 0x0F) << 8);
            }
            update_scc_step(engine, ch);
        }
        else if (reg >= 0xAA && reg <= 0xAE) {
            // MSX SCC+: ボリューム (0xAA - 0xAE)
            int ch = reg - 0xAA;
            engine->vol[ch] = data & 0x0F;
        }
        else if (reg == 0xAF) {
            // MSX SCC+: チャンネルON/OFF (0xAF)
            engine->enable = data;
        }
    }
}

void scc_engine_tick(SCCSoundEngine *engine, int32_t *out_l, int32_t *out_r) {
    int32_t mix = 0;

    for (int ch = 0; ch < 5; ch++) {
        // チャンネルが有効か判定 (Bit0=CH1, ..., Bit4=CH5)
        if (engine->enable & (1 << ch)) {
            // 位相（フェーズ）を進める
            engine->phase[ch] += engine->step_size[ch];
            
            // 整数部（上位16bit）を取り出し、波形テーブルの長さにマスク(0〜31)
            uint32_t index = (engine->phase[ch] >> 16) & 0x1F;
            
            // 波形データ (-128〜127) × ボリューム (0〜15) 
            // ※ SCCのDACはリニア(線形)出力なので単純な掛け算でOK
            int32_t sample = engine->wave[ch][index] * engine->vol[ch];
            mix += sample;
        }
    }

    // 全体ボリュームのスケール調整（他の音源と馴染むように適度に下げる）
    mix = mix * 6;

    // L/Rにミキシング（基本はモノラル）
    *out_l += mix;
    *out_r += mix;
}
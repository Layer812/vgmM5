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

void scc_engine_write(SCCSoundEngine *engine, uint8_t port, uint8_t data) {
    if (port < 0x80) {
        // 波形RAMへの書き込み (0x00 - 0x7F)
        int ch = port / 0x20;        // 32バイトごとにCH1〜CH4
        int offset = port % 0x20;
        
        engine->wave[ch][offset] = (int8_t)data; // 実機は2の補数(符号付き8bit)
        
        // SCC(K051649)の仕様：CH4とCH5は波形RAMを共有している
        if (ch == 3) {
            engine->wave[4][offset] = (int8_t)data; 
        }
    } 
    else if (port >= 0x80 && port <= 0x89) {
        // 周波数レジスタ (0x80 - 0x89)
        int ch = (port - 0x80) / 2;
        if (port % 2 == 0) {
            // 下位8bit
            engine->freq[ch] = (engine->freq[ch] & 0x0F00) | data;
        } else {
            // 上位4bit
            engine->freq[ch] = (engine->freq[ch] & 0x00FF) | ((data & 0x0F) << 8);
        }
        update_scc_step(engine, ch);
    } 
    else if (port >= 0x8A && port <= 0x8E) {
        // ボリューム (0x8A - 0x8E)
        int ch = port - 0x8A;
        engine->vol[ch] = data & 0x0F;
    } 
    else if (port == 0x8F) {
        // チャンネルON/OFF (0x8F)
        engine->enable = data;
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
    mix = mix * 3; // 音量を調整。大きすぎる場合は下げる

    // L/Rにミキシング（基本はモノラル）
    *out_l += mix;
    *out_r += mix;
}
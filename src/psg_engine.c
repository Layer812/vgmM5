#include "psg_engine.h"
#include <string.h>
#include <math.h>

PSGSoundEngine g_psg_engine;

// ─────────────────────────────────────────
// 初期化
// ─────────────────────────────────────────
void psg_engine_init(PSGSoundEngine *engine, uint32_t sample_rate, uint32_t sn_clock) {
    memset(engine, 0, sizeof(PSGSoundEngine));
    engine->sample_rate = sample_rate;
    
    SN76489 *sn = &engine->sn76489;
    sn->clock = sn_clock;
    sn->sample_rate = sample_rate;
    
    // ボリュームは初期状態では無音(15)
    for (int i = 0; i < 3; i++) sn->tone[i].vol = 15;
    sn->noise.vol = 15;
    sn->noise.shift_reg = 0x8000; // LFSRの初期値

    // SN76489はクロックを16分周して動作する
    // サンプルレートとチップの動作周波数の比率を固定小数点で管理
    sn->step_size = (uint32_t)(((uint64_t)(sn_clock / 16) << 16) / sample_rate);

    // ボリュームテーブルの作成 (2dB step)
    // 最大音量を適度な値(例: 2000)に設定し、1段ごとに2dB減衰させる
    const double MAX_VOL = 2000.0;
    for (int i = 0; i < 15; i++) {
        sn->vol_table[i] = (int32_t)(MAX_VOL * pow(10.0, -(i * 2.0) / 20.0));
    }
    sn->vol_table[15] = 0; // 15は完全な無音
}

// ─────────────────────────────────────────
// レジスタ書き込み (VGMコマンド: 0x50)
// ─────────────────────────────────────────
void psg_engine_write_sn76489(PSGSoundEngine *engine, uint8_t data) {
    SN76489 *sn = &engine->sn76489;

    if (data & 0x80) {
        // LATCHバイト (1ccctddd)
        sn->latched_ch   = (data >> 5) & 0x03;
        sn->latched_type = (data >> 4) & 0x01;
        uint8_t val      = data & 0x0F;

        if (sn->latched_type == 1) {
            // ボリューム書き込み
            if (sn->latched_ch < 3) {
                sn->tone[sn->latched_ch].vol = val;
            } else {
                sn->noise.vol = val;
            }
        } else {
            // 周波数/コントロールの 下位4bit 書き込み
            if (sn->latched_ch < 3) {
                sn->tone[sn->latched_ch].freq = (sn->tone[sn->latched_ch].freq & 0x03F0) | val;
            } else {
                sn->noise.ctrl = val & 0x07;
                sn->noise.shift_reg = 0x8000; // ノイズ設定変更時はLFSRリセット
            }
        }
    } else {
        // DATAバイト (0ddddddd) - ラッチされているレジスタの上位bitを更新
        if (sn->latched_type == 0 && sn->latched_ch < 3) {
            sn->tone[sn->latched_ch].freq = (sn->tone[sn->latched_ch].freq & 0x000F) | ((data & 0x3F) << 4);
        } else if (sn->latched_type == 1) {
            // ボリュームのデータバイト書き込みは通常起きないが、一応処理
            if (sn->latched_ch < 3) sn->tone[sn->latched_ch].vol = data & 0x0F;
            else                    sn->noise.vol = data & 0x0F;
        }
    }
}

// ─────────────────────────────────────────
// ミキサー更新 (1サンプル分)
// ─────────────────────────────────────────
void psg_engine_update(PSGSoundEngine *engine, int32_t *out_l, int32_t *out_r) {
    SN76489 *sn = &engine->sn76489;
    sn->step_accum += sn->step_size;

    // サンプリングレートと内部クロックのズレを吸収（1サンプル間にチップが何ステップ進むか）
    while (sn->step_accum >= (1 << 16)) {
        sn->step_accum -= (1 << 16);

        // トーンチャンネルの更新
        for (int i = 0; i < 3; i++) {
            SN76489_Tone *ch = &sn->tone[i];
            if (ch->counter > 0) ch->counter--;
            
            if (ch->counter <= 0) {
                // 周期が0の場合は0x400として扱う(ハードウェア仕様)
                ch->counter = (ch->freq > 0) ? ch->freq : 0x400; 
                ch->out_state = -ch->out_state; // 矩形波の反転(トグル)
            }
        }

        // ノイズチャンネルの更新
        SN76489_Noise *nch = &sn->noise;
        if (nch->counter > 0) nch->counter--;
        
        if (nch->counter <= 0) {
            // ノイズのシフトレート設定 (0, 1, 2 = 内部設定, 3 = トーンCH3の周波数に同期)
            int shift_rate = nch->ctrl & 0x03;
            if (shift_rate == 3) {
                nch->counter = (sn->tone[2].freq > 0) ? sn->tone[2].freq : 0x400;
            } else {
                nch->counter = 0x10 << shift_rate; 
            }

            // 出力のトグル
            nch->out_state = -nch->out_state;

            // トグルして反転した瞬間のみシフトレジスタを進める
            if (nch->out_state > 0) {
                int feedback;
                if (nch->ctrl & 0x04) { // ホワイトノイズ
                    // Sega VDPのタップ (Bit 0 XOR Bit 3)
                    feedback = (nch->shift_reg & 0x0001) ^ ((nch->shift_reg >> 3) & 0x0001);
                } else { // 周期ノイズ
                    feedback = nch->shift_reg & 0x0001;
                }
                nch->shift_reg = (nch->shift_reg >> 1) | (feedback << 14);
            }
        }
    }

    // 各チャンネルの出力をボリューム付きでミキシング
    int32_t mix = 0;
    
    for (int i = 0; i < 3; i++) {
        // 出力状態(1 or -1) * ボリューム
        if (sn->tone[i].out_state > 0) mix += sn->vol_table[sn->tone[i].vol];
        else                           mix -= sn->vol_table[sn->tone[i].vol];
    }
    
    // ノイズ出力 (シフトレジスタのLSBが出力となる)
    if (sn->noise.shift_reg & 0x0001) mix += sn->vol_table[sn->noise.vol];
    else                              mix -= sn->vol_table[sn->noise.vol];

    // L/Rに足し込む（SN76489はモノラル）
    *out_l += mix;
    *out_r += mix;
}
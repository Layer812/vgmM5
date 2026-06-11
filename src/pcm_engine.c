#include "pcm_engine.h"
#include <stdlib.h>
#include <string.h>
#include <esp_heap_caps.h>

PCMSoundEngine g_pcm_engine;

// ─────────────────────────────────────────
// OKI/MSM6258 共通 ADPCMテーブル
// ─────────────────────────────────────────
static int oki_step_table[49] = {
    16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107,118,130,143,
    157,173,190,209,230,253,279,307,337,371,408,449,
    494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552
};
static int oki_index_table[16] = {
    -1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8
};

// ─────────────────────────────────────────
// MSM6258 ADPCMニブルデコーダ
// ─────────────────────────────────────────
static void msm6258_decode_nibble(MSM6258 *c, int nibble) {
    int step  = oki_step_table[c->adpcm_step_idx];
    int delta = step >> 3;
    if (nibble & 1) delta += (step >> 2);
    if (nibble & 2) delta += (step >> 1);
    if (nibble & 4) delta += step;
    if (nibble & 8) delta = -delta;
    c->adpcm_val += delta;
    if (c->adpcm_val >  2047) c->adpcm_val =  2047;
    if (c->adpcm_val < -2048) c->adpcm_val = -2048;
    c->adpcm_step_idx += oki_index_table[nibble & 0x0F];
    if (c->adpcm_step_idx < 0)  c->adpcm_step_idx = 0;
    if (c->adpcm_step_idx > 48) c->adpcm_step_idx = 48;
}

// ─────────────────────────────────────────
// 初期化 / 解放
// ─────────────────────────────────────────
void pcm_engine_init(PCMSoundEngine *engine, uint32_t sample_rate) {
    memset(engine, 0, sizeof(PCMSoundEngine));
    engine->sample_rate = sample_rate;
}

void pcm_engine_free(PCMSoundEngine *engine) {
    if (engine->oki.rom_data) {
        free(engine->oki.rom_data);
        engine->oki.rom_data = NULL;
        engine->oki.rom_size = 0;
    }
    if (engine->msm6258.rom_data) {
        free(engine->msm6258.rom_data);
        engine->msm6258.rom_data = NULL;
        engine->msm6258.rom_size = 0;
    }
    
    if (engine->ym2612_pcm_block) {
        free(engine->ym2612_pcm_block);
        engine->ym2612_pcm_block = NULL;
        engine->ym2612_pcm_size  = 0;
    }
    for (int b = 0; b < engine->segapcm.block_count; b++) {
        if (engine->segapcm.blocks[b].is_allocated && engine->segapcm.blocks[b].data) {
            free(engine->segapcm.blocks[b].data);
        }
        engine->segapcm.blocks[b].data = NULL;
        engine->segapcm.blocks[b].is_allocated = false;
    }
    engine->segapcm.block_count = 0;

}

// ─────────────────────────────────────────
// データブロック追加 (0x67コマンド)
// ─────────────────────────────────────────
void pcm_engine_add_data_block(PCMSoundEngine *engine, uint8_t type,
                               uint8_t *data, uint32_t size) {
    if (type == 0x00) {
        // YM2612 PCM（連結）
        if (!engine->ym2612_pcm_block) {
            engine->ym2612_pcm_block = (uint8_t*)malloc(size);
            engine->ym2612_pcm_size  = 0;
        } else {
            uint8_t *nb = (uint8_t*)realloc(engine->ym2612_pcm_block,
                                             engine->ym2612_pcm_size + size);
            if (nb) engine->ym2612_pcm_block = nb;
        }
        if (engine->ym2612_pcm_block) {
            memcpy(engine->ym2612_pcm_block + engine->ym2612_pcm_size, data, size);
            engine->ym2612_pcm_size += size;
        }
    } else if (type == 0x04) {
        // MSM6258 ADPCMシンプルストリーム（連結）
        // 複数の0x67ブロックが来る場合はreallocで追加する
        if (!engine->msm6258.rom_data) {
            engine->msm6258.rom_data = (uint8_t*)malloc(size);
            engine->msm6258.rom_size = 0;
        } else {
            uint8_t *nb = (uint8_t*)realloc(engine->msm6258.rom_data,
                                             engine->msm6258.rom_size + size);
            if (nb) engine->msm6258.rom_data = nb;
        }
        if (engine->msm6258.rom_data) {
            if (engine->msm6258.block_count < 64) {
                engine->msm6258.block_offsets[engine->msm6258.block_count] = engine->msm6258.rom_size;
                engine->msm6258.block_sizes[engine->msm6258.block_count] = size;
                engine->msm6258.block_count++;
            }
            memcpy(engine->msm6258.rom_data + engine->msm6258.rom_size, data, size);
            engine->msm6258.rom_size += size;
        }
    } else if (type == 0x88) {
        // MSM6258 ROM形式（先頭8バイトが総サイズ+オフセット）
        if (size < 8) return;
        uint32_t rom_total = data[0]|(data[1]<<8)|(data[2]<<16)|(data[3]<<24);
        uint32_t start_ofs = data[4]|(data[5]<<8)|(data[6]<<16)|(data[7]<<24);
        uint32_t actual    = size - 8;
        if (!engine->msm6258.rom_data) {
            engine->msm6258.rom_data = (uint8_t*)malloc(rom_total);
            if (engine->msm6258.rom_data) {
                memset(engine->msm6258.rom_data, 0, rom_total);
                engine->msm6258.rom_size = rom_total;
            }
        }
        if (engine->msm6258.rom_data && start_ofs + actual <= engine->msm6258.rom_size) {
            memcpy(engine->msm6258.rom_data + start_ofs, data + 8, actual);
        }
    } else if (type == 0x8B) {
        // OKI MSM6295 ROM（先頭8バイトが総サイズ+オフセット）
        if (size < 8) return;
        uint32_t rom_total = data[0]|(data[1]<<8)|(data[2]<<16)|(data[3]<<24);
        uint32_t start_ofs = data[4]|(data[5]<<8)|(data[6]<<16)|(data[7]<<24);
        uint32_t actual    = size - 8;
        if (!engine->oki.rom_data) {
            engine->oki.rom_data = (uint8_t*)malloc(rom_total);
            if (engine->oki.rom_data) {
                memset(engine->oki.rom_data, 0, rom_total);
                engine->oki.rom_size = rom_total;
            }
        }
        if (engine->oki.rom_data && start_ofs + actual <= engine->oki.rom_size) {
            memcpy(engine->oki.rom_data + start_ofs, data + 8, actual);
        }
    } else if (type == 0x80) {
        // SegaPCM
        if (size <= 8) return;
        uint32_t start_addr = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
        
        // ★ ループ時に同じブロックが重複追加されるのを防ぐ（処理落ち対策）
        for (int i = 0; i < engine->segapcm.block_count; i++) {
            if (engine->segapcm.blocks[i].start_addr == start_addr) {
                return; 
            }
        }
        
        if (engine->segapcm.block_count >= SEGAPCM_MAX_BLOCKS) return;
        uint32_t actual = size - 8;
        engine->segapcm.blocks[engine->segapcm.block_count].start_addr = start_addr;
        engine->segapcm.blocks[engine->segapcm.block_count].size = actual;
        
        uint8_t* sram_data = (uint8_t*)heap_caps_malloc(actual, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (sram_data) {
            memcpy(sram_data, data + 8, actual);
            engine->segapcm.blocks[engine->segapcm.block_count].data = sram_data;
            engine->segapcm.blocks[engine->segapcm.block_count].is_allocated = true;
        } else {
            engine->segapcm.blocks[engine->segapcm.block_count].data = data + 8;
            engine->segapcm.blocks[engine->segapcm.block_count].is_allocated = false;
        }
        
        // print details directly via uart if printf fails, but printf is usually fine
        printf("SegaPCM Block %d: start=0x%08X size=%d\\n", engine->segapcm.block_count, start_addr, actual);
        engine->segapcm.block_count++;
    }
}

// ─────────────────────────────────────────
// MSM6258 クロック設定
// ─────────────────────────────────────────
void pcm_engine_set_msm6258_clock(PCMSoundEngine *engine, uint32_t clock) {
    engine->msm6258.clock = clock;
    // デフォルト分周 /512
    uint32_t sr = (clock > 0) ? (clock / 512) : 15625;
    engine->msm6258.step_size = ((uint64_t)sr * 65536) / engine->sample_rate;
}

// ─────────────────────────────────────────
// MSM6258 レジスタ書き込み (0xB7コマンド)
// ─────────────────────────────────────────
void pcm_engine_write_msm6258(PCMSoundEngine *engine, uint8_t reg, uint8_t data) {
    if (reg == 0) {
        // コントロールレジスタ: bit1=STOP
        if (data & 0x02) {
            engine->msm6258.playing = 0;
        } else {
            engine->msm6258.adpcm_val      = 0;
            engine->msm6258.adpcm_step_idx = 0;
            engine->msm6258.nibble_sel     = 0;
            engine->msm6258.step_accum     = 0;
            engine->msm6258.playing        = 1;
        }
    } else if (reg == 1) {
        // データレジスタ: プレディクタ初期化用の直接ニブル書き込み
        // X68000 4bitモード: 下位4bitがADPCMニブル
        msm6258_decode_nibble(&engine->msm6258, data & 0x0F);
        engine->msm6258.playing = 1;
    }
}

// ─────────────────────────────────────────
// MSM6258 ROMストリーム開始 (0x93コマンド)
// ─────────────────────────────────────────
void pcm_engine_msm6258_start_stream(PCMSoundEngine *engine,
                                     uint32_t offset, uint32_t length) {
    if (!engine->msm6258.rom_data) return;
    engine->msm6258.data_pos       = offset;
    engine->msm6258.data_end       = offset + length;
    if (engine->msm6258.data_end > engine->msm6258.rom_size)
        engine->msm6258.data_end = engine->msm6258.rom_size;
    engine->msm6258.adpcm_val      = 0;
    engine->msm6258.adpcm_step_idx = 0;
    engine->msm6258.nibble_sel     = 0;
    engine->msm6258.step_accum     = 0;
    engine->msm6258.playing        = 1;
    if (engine->msm6258.step_size == 0)
        engine->msm6258.step_size = (15625ULL * 65536) / engine->sample_rate;
}

// ─────────────────────────────────────────
// OKI MSM6295 (未使用/スタブ)
// ─────────────────────────────────────────
void pcm_engine_set_oki_clock(PCMSoundEngine *engine, uint32_t clock) {
    (void)engine; (void)clock;
}
void pcm_engine_write_oki(PCMSoundEngine *engine, uint8_t data) {
    (void)engine; (void)data;
}

// ─────────────────────────────────────────
// SegaPCM
// ─────────────────────────────────────────
void pcm_engine_segapcm_init(PCMSoundEngine *engine, uint32_t clock, uint32_t interface_reg) {
    engine->segapcm.clock = (clock == 0) ? 4000000 : clock;
    uint8_t shift = interface_reg & 0xFF;
    uint8_t mask  = (interface_reg >> 8) & 0xFF;
    engine->segapcm.bank_shift = (shift == 0) ? 12 : shift;
    engine->segapcm.bank_mask  = (mask  == 0) ? 0x70 : mask;
    // MAMEと同じく 0xFF で初期化（bit0=1 = ch disable）
    for (int i = 0; i < 256; i++) engine->segapcm.regs[i] = 0xFF;
    for (int i = 0; i < 16; i++) {
        engine->segapcm.low[i]          = 0;
        engine->segapcm.step_acc[i]     = 0;
        engine->segapcm.last_block[i]   = 0;
        engine->segapcm.unmapped_page[i] = 0xFFFFFFFF;
    }
    // step_scale = (clock/128 << 16) / sample_rate
    // tick内で delta * step_scale >> 16 でアドレス進行量を計算（除算不要）
    uint32_t chip_rate = engine->segapcm.clock / 128;
    engine->segapcm.step_scale = (uint32_t)(((uint64_t)chip_rate << 16) / engine->sample_rate);
    engine->segapcm.block_count = 0;
}

// SegaPCM レジスタ書き込み（MAMEはRAMに直書きするだけ）
void pcm_engine_segapcm_write(PCMSoundEngine *engine, uint8_t reg, uint8_t data) {
    engine->segapcm.regs[reg] = data;
    int ch = (reg & 0x7F) / 8;
    if (ch < 16) {
        uint8_t *regs = &engine->segapcm.regs[8 * ch];
        // 修正: MAMEに倣い、offset 5がHiバイト、offset 4がLoバイト
        engine->segapcm.loop[ch] = ((uint32_t)regs[5] << 16) | ((uint32_t)regs[4] << 8);
        engine->segapcm.end[ch] = regs[6] + 1;
        engine->segapcm.delta[ch] = regs[7];
        engine->segapcm.vol_l[ch] = regs[2] & 0x7F;
        engine->segapcm.vol_r[ch] = regs[3] & 0x7F;
        
        uint8_t flags = engine->segapcm.regs[0x80 + 8*ch + 6];
        engine->segapcm.bank[ch] = (uint32_t)(flags & engine->segapcm.bank_mask) << engine->segapcm.bank_shift;
        engine->segapcm.flags[ch] = flags;
        
        // 修正: offset 5がHiバイト、offset 4がLoバイト
        uint8_t cur_hi = engine->segapcm.regs[0x80 + 8*ch + 5];
        uint8_t cur_lo = engine->segapcm.regs[0x80 + 8*ch + 4];
        engine->segapcm.addr[ch] = ((uint32_t)cur_hi << 16) | ((uint32_t)cur_lo << 8) | engine->segapcm.low[ch];
    }
}

// ─────────────────────────────────────────
// YM2612 DAC 書き込み
// ─────────────────────────────────────────
void pcm_engine_write_ym2612(PCMSoundEngine *engine, uint16_t addr, uint8_t data) {
    if      (addr == 0x2B) engine->ym2612_dac.dac_enable = (data & 0x80) ? 1 : 0;
    else if (addr == 0x2A) engine->ym2612_dac.dac_data   = data;
}

// ─────────────────────────────────────────
// 1サンプル生成（ミキシング）
// ─────────────────────────────────────────
void IRAM_ATTR pcm_engine_tick(PCMSoundEngine *engine, int32_t *out_l, int32_t *out_r) {
    int32_t mix_l = 0, mix_r = 0;

    // ── YM2612 DAC ──────────────────────────
    if (engine->ym2612_dac.dac_enable) {
        int32_t v = ((int32_t)engine->ym2612_dac.dac_data - 128) << 6;
        mix_l += v; mix_r += v;
    }

    // ── MSM6258 ADPCM ────────────────────────
    if (engine->msm6258.playing &&
        engine->msm6258.rom_data &&
        engine->msm6258.data_end > engine->msm6258.data_pos) {

        engine->msm6258.step_accum += engine->msm6258.step_size;

        while (engine->msm6258.step_accum >= 65536) {
            engine->msm6258.step_accum -= 65536;

            if (engine->msm6258.data_pos >= engine->msm6258.data_end) {
                engine->msm6258.playing = 0;
                break;
            }
            // 上位ニブル→下位ニブルの順にデコード（OKI ADPCM標準）
            uint8_t byte = engine->msm6258.rom_data[engine->msm6258.data_pos];
            int nibble = (engine->msm6258.nibble_sel == 0)
                         ? ((byte >> 4) & 0x0F)   // 上位ニブル先
                         : (byte & 0x0F);          // 下位ニブル後
            engine->msm6258.nibble_sel ^= 1;
            if (engine->msm6258.nibble_sel == 0) {
                engine->msm6258.data_pos++;        // 1バイト消費
            }
            msm6258_decode_nibble(&engine->msm6258, nibble);
        }

        // adpcm_val (-2048〜2047) → 16bit相当にスケール
        // 「気持ち音を抑える」ため、<<4 (16倍) ではなく *10 (約62%の音量) に調整します
        int32_t out = engine->msm6258.adpcm_val * 10;
        mix_l += out;
        mix_r += out;
    }

    // ── SegaPCM (MAME/VGMPlay完全準拠) ────────────────────────
    
    if (engine->segapcm.block_count > 0) {
        int32_t spcm_l = 0;
        int32_t spcm_r = 0;
        // VGMPlay/MAME: regs = ram + 8*ch
        // ram[0x00+8*ch .. 0x07+8*ch]: パラメータ (vol, loop, end, delta)
        // ram[0x80+8*ch .. 0x87+8*ch]: ステータス (current addr, bank/flags)
        for (int ch = 0; ch < 16; ch++) {
            uint8_t flags = engine->segapcm.flags[ch];
            if (flags & 1) continue;

            if (engine->segapcm.vol_l[ch] == 0 && engine->segapcm.vol_r[ch] == 0) {
                engine->segapcm.step_acc[ch] += (uint32_t)engine->segapcm.delta[ch] * engine->segapcm.step_scale;
                engine->segapcm.addr[ch] = (engine->segapcm.addr[ch] + (engine->segapcm.step_acc[ch] >> 16)) & 0xFFFFFF;
                engine->segapcm.step_acc[ch] &= 0xFFFF;
                continue;
            }

            uint32_t bank = engine->segapcm.bank[ch];
            uint32_t addr = engine->segapcm.addr[ch];
            uint32_t loop = engine->segapcm.loop[ch];
            uint8_t  end  = engine->segapcm.end[ch];
            uint8_t  delta = engine->segapcm.delta[ch];

            // 終端チェック
            if ((addr >> 16) == end) {
                if (flags & 2) {
                    engine->segapcm.flags[ch] |= 1; // 停止
                    engine->segapcm.regs[0x80 + 8*ch + 6] |= 1;
                    continue;
                } else {
                    addr = loop;
                }
            }

            // ROMアドレス = (bank >> 8) + (addr >> 8)
            uint32_t rom_addr = bank + (addr >> 8);
            int32_t sample = 0;
            int b = engine->segapcm.last_block[ch];
            
            // ★ 修正: ブロックデータがロードされている場合のみ 128 の減算を行う
            if (b < engine->segapcm.block_count && engine->segapcm.blocks[b].data != NULL &&
                rom_addr >= engine->segapcm.blocks[b].start_addr &&
                rom_addr <  engine->segapcm.blocks[b].start_addr + engine->segapcm.blocks[b].size) {
                sample = (int32_t)engine->segapcm.blocks[b].data[rom_addr - engine->segapcm.blocks[b].start_addr] - 128;
            } else {
                if (engine->segapcm.unmapped_page[ch] == (rom_addr & ~0xFFF)) {
                    sample = 0; // ★ 未配置セクタは一律0(完全無音)
                } else {
                    int found = 0;
                    for (b = 0; b < engine->segapcm.block_count; b++) {
                        if (engine->segapcm.blocks[b].data != NULL &&
                            rom_addr >= engine->segapcm.blocks[b].start_addr &&
                            rom_addr <  engine->segapcm.blocks[b].start_addr + engine->segapcm.blocks[b].size) {
                            sample = (int32_t)engine->segapcm.blocks[b].data[rom_addr - engine->segapcm.blocks[b].start_addr] - 128;
                            engine->segapcm.last_block[ch] = (uint8_t)b;
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        engine->segapcm.unmapped_page[ch] = (rom_addr & ~0xFFF);
                        engine->segapcm.last_block[ch] = 255;
                        sample = 0; // ★ 探索失敗時も0に固定
                    }
                }
            }

            spcm_l += sample * engine->segapcm.vol_l[ch];
            spcm_r += sample * engine->segapcm.vol_r[ch];

            // アドレス更新：16.16固定小数点アキュムレータ（除算ゼロ）
            // step_scale = (chip_rate << 16) / sample_rate（init時に事前計算済み）
            engine->segapcm.step_acc[ch] += (uint32_t)delta * engine->segapcm.step_scale;
            uint32_t whole_steps = engine->segapcm.step_acc[ch] >> 16;   // 整数部: シフトで取得
            engine->segapcm.step_acc[ch] &= 0xFFFF;                       // 小数部: マスクで保持
            // removed whole_steps force to 1
            addr = (addr + whole_steps) & 0xFFFFFF;
            engine->segapcm.addr[ch] = addr;
        }
        // FM音源とのバランスを調整（さらに音量を上げて >>1 に）
        mix_l += spcm_l >> 1;
        mix_r += spcm_r >> 1;
    }


    *out_l += mix_l;
    *out_r += mix_r;
}
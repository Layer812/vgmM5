#include "pcm_engine.h"
#include <stdlib.h>
#include <string.h>
#include <esp_heap_caps.h>

PCMSoundEngine g_pcm_engine;

// ─────────────────────────────────────────
// Namco C140 / C352 LUT
// ─────────────────────────────────────────
static int16_t c352_mulaw_table[256];
static int16_t c140_pcmtbl[8];

static void init_namco_tables() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    int32_t segbase = 0;
    for(int i = 0; i < 8; i++) {
        c140_pcmtbl[i] = segbase;
        segbase += 16 << i;
    }
    int j = 0;
    for(int i = 0; i < 128; i++) {
        c352_mulaw_table[i] = j << 5;
        if (i < 16) j += 1;
        else if (i < 24) j += 2;
        else if (i < 48) j += 4;
        else if (i < 100) j += 8;
        else j += 16;
    }
    for(int i = 128; i < 256; i++) {
        c352_mulaw_table[i] = (~c352_mulaw_table[i - 128]) & 0xffe0;
    }
}

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
    engine->ym2612_dac.dac_data = 128;
    engine->c140_enabled = false;
    engine->c352_enabled = false;
    engine->namco.is_mmap = false;
    engine->namco.block_count = 0;
    init_namco_tables();
}

void pcm_engine_free(PCMSoundEngine *engine) {
    engine->ym2612_pcm_block_count = 0;
    engine->oki.block_count = 0;
    engine->msm6258.block_count = 0;
    engine->segapcm.block_count = 0;
    for (int b = 0; b < engine->namco.block_count; b++) {
        if (engine->namco.blocks[b].is_allocated && engine->namco.blocks[b].data) {
            free((void*)engine->namco.blocks[b].data);
        }
    }
    engine->namco.block_count = 0;
}

// ─────────────────────────────────────────
// データブロック追加 (0x67コマンド)
// ─────────────────────────────────────────
void pcm_engine_add_data_block(PCMSoundEngine *engine, uint8_t type, const uint8_t *data, uint32_t size) {
    if (size > 0x1000000) return;
    if (type == 0x00) {
        if (engine->ym2612_pcm_block_count < 64) {
            int c = engine->ym2612_pcm_block_count;
            engine->ym2612_pcm_offsets[c] = (c == 0) ? 0 : engine->ym2612_pcm_offsets[c-1] + engine->ym2612_pcm_sizes[c-1];
            engine->ym2612_pcm_sizes[c] = size;
            engine->ym2612_pcm_blocks[c] = data;
            engine->ym2612_pcm_block_count++;
        }
    } else if (type == 0x04) {
        if (engine->msm6258.block_count < 64) {
            int c = engine->msm6258.block_count;
            engine->msm6258.block_offsets[c] = (c == 0) ? 0 : engine->msm6258.block_offsets[c-1] + engine->msm6258.block_sizes[c-1];
            engine->msm6258.block_sizes[c] = size;
            engine->msm6258.blocks[c] = data;
            engine->msm6258.block_count++;
        }
    } else if (type == 0x82) { // YM2610 ADPCM-A
        if (engine->opn.adpcma_block_count < 64) {
            int c = engine->opn.adpcma_block_count;
            engine->opn.adpcma_block_offsets[c] = (c == 0) ? 0 : engine->opn.adpcma_block_offsets[c-1] + engine->opn.adpcma_block_sizes[c-1];
            engine->opn.adpcma_block_sizes[c] = size;
            engine->opn.adpcma_blocks[c] = data;
            engine->opn.adpcma_block_count++;
        }
    } else if (type == 0x81 || type == 0x83) { // YM2608 / YM2610 Delta-T
        if (engine->opn.adpcmb_block_count < 64) {
            int c = engine->opn.adpcmb_block_count;
            engine->opn.adpcmb_block_offsets[c] = (c == 0) ? 0 : engine->opn.adpcmb_block_offsets[c-1] + engine->opn.adpcmb_block_sizes[c-1];
            engine->opn.adpcmb_block_sizes[c] = size;
            engine->opn.adpcmb_blocks[c] = data;
            engine->opn.adpcmb_block_count++;
        }
    } else if (type == 0x8B) {
        if (engine->oki.block_count < 64) {
            int c = engine->oki.block_count;
            engine->oki.block_offsets[c] = (c == 0) ? 0 : engine->oki.block_offsets[c-1] + engine->oki.block_sizes[c-1];
            engine->oki.block_sizes[c] = size;
            engine->oki.blocks[c] = data;
            engine->oki.block_count++;
        }
    } else if (type == 0x80) {
        if (size < 8) return;
        uint32_t start_addr = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
        for (int i = 0; i < engine->segapcm.block_count; i++) {
            if (engine->segapcm.blocks[i].start_addr == start_addr) return;
        }
        if (engine->segapcm.block_count >= SEGAPCM_MAX_BLOCKS) return;
        uint32_t actual = size - 8;
        engine->segapcm.blocks[engine->segapcm.block_count].start_addr = start_addr;
        engine->segapcm.blocks[engine->segapcm.block_count].size = actual;
        engine->segapcm.blocks[engine->segapcm.block_count].data = data + 8;
        engine->segapcm.blocks[engine->segapcm.block_count].is_allocated = false;
        engine->segapcm.block_count++;
    } else if (type == 0x8D || type == 0x92) { // C140 (0x8D) and C352 (0x92)
        if (size < 8) return;
        uint32_t start_addr = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
        uint32_t actual = size - 8;
        pcm_engine_namco_mmap(engine, start_addr, data + 8, actual);
    }
}

uint8_t pcm_engine_get_ym2612_byte(PCMSoundEngine *engine, uint32_t offset) {
    for (int i = 0; i < engine->ym2612_pcm_block_count; i++) {
        if (offset >= engine->ym2612_pcm_offsets[i] && offset < engine->ym2612_pcm_offsets[i] + engine->ym2612_pcm_sizes[i]) {
            return engine->ym2612_pcm_blocks[i][offset - engine->ym2612_pcm_offsets[i]];
        }
    }
    return 0x80;
}

// ─────────────────────────────────────────
// MSM6258 クロック設定
// ─────────────────────────────────────────
void pcm_engine_set_msm6258_clock(PCMSoundEngine *engine, uint32_t clock) {
    engine->msm6258.clock = clock;
    uint32_t sr = (clock > 0) ? (clock / 512) : 15625;
    engine->msm6258.step_size = ((uint64_t)sr * 65536) / engine->sample_rate;
}

// ─────────────────────────────────────────
// MSM6258 レジスタ書き込み (0xB7コマンド)
// ─────────────────────────────────────────
void pcm_engine_write_msm6258(PCMSoundEngine *engine, uint8_t reg, uint8_t data) {
    if (reg == 0) {
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
        msm6258_decode_nibble(&engine->msm6258, data & 0x0F);
        engine->msm6258.playing = 1;
    }
}

// ─────────────────────────────────────────
// MSM6258 ROMストリーム開始 (0x93コマンド)
// ─────────────────────────────────────────
void pcm_engine_msm6258_start_stream(PCMSoundEngine *engine,
                                     uint32_t offset, uint32_t length) {
    engine->msm6258.data_pos       = offset;
    engine->msm6258.data_end       = offset + length;
    engine->msm6258.adpcm_val      = 0;
    engine->msm6258.adpcm_step_idx = 0;
    engine->msm6258.nibble_sel     = 0;
    engine->msm6258.step_accum     = 0;
    engine->msm6258.playing        = 1;
    if (engine->msm6258.step_size == 0)
        engine->msm6258.step_size = (15625ULL * 65536) / engine->sample_rate;
}

// ─────────────────────────────────────────
// OKI MSM6295 (スタブ)
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
    for (int i = 0; i < 256; i++) engine->segapcm.regs[i] = 0xFF;
    for (int i = 0; i < 16; i++) {
        engine->segapcm.low[i]          = 0;
        engine->segapcm.step_acc[i]     = 0;
        engine->segapcm.last_block[i]   = 0;
        engine->segapcm.unmapped_page[i] = 0xFFFFFFFF;
    }
    uint32_t chip_rate = engine->segapcm.clock / 128;
    engine->segapcm.step_scale = (uint32_t)(((uint64_t)chip_rate << 16) / engine->sample_rate);
    engine->segapcm.block_count = 0;
}

void pcm_engine_segapcm_write(PCMSoundEngine *engine, uint8_t reg, uint8_t data) {
    engine->segapcm.regs[reg] = data;
    int ch = (reg & 0x7F) / 8;
    if (ch < 16) {
        uint8_t *regs = &engine->segapcm.regs[8 * ch];
        engine->segapcm.loop[ch] = ((uint32_t)regs[5] << 16) | ((uint32_t)regs[4] << 8);
        engine->segapcm.end[ch] = regs[6] + 1;
        engine->segapcm.delta[ch] = regs[7];
        engine->segapcm.vol_l[ch] = regs[2] & 0x7F;
        engine->segapcm.vol_r[ch] = regs[3] & 0x7F;
        
        uint8_t flags = engine->segapcm.regs[0x80 + 8*ch + 6];
        engine->segapcm.bank[ch] = (uint32_t)(flags & engine->segapcm.bank_mask) << engine->segapcm.bank_shift;
        engine->segapcm.flags[ch] = flags;
        
        int reg_idx = reg % 8;
        if (reg >= 0x80 && (reg_idx == 4 || reg_idx == 5 || reg_idx == 6)) {
            uint8_t cur_hi = engine->segapcm.regs[0x80 + 8*ch + 5];
            uint8_t cur_lo = engine->segapcm.regs[0x80 + 8*ch + 4];
            engine->segapcm.addr[ch] = ((uint32_t)cur_hi << 16) | ((uint32_t)cur_lo << 8) | engine->segapcm.low[ch];
        }
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
// Namco C140 / C352
// ─────────────────────────────────────────
void pcm_engine_namco_init(PCMSoundEngine *engine, uint32_t clock, uint8_t type, uint8_t c140_type) {
    engine->namco.clock = clock;
    engine->namco.type = type;
    engine->namco.sample_rate = engine->sample_rate;
    if (type == 0xC1) {
        engine->c140_enabled = true;
        engine->c352_enabled = false;
        engine->namco.channels = 24;
        engine->namco.c140_type = c140_type;
    } else {
        engine->c352_enabled = true;
        engine->c140_enabled = false;
        engine->namco.channels = 32;
    }
    memset(engine->namco.ch, 0, sizeof(engine->namco.ch));

    uint32_t native_rate;
    if (type == 0xC1) {
        native_rate = (clock > 0) ? (clock / 192) : (21390000 / 192); // C140 baserate is clock / 384, but MAME multiplies freq by 2
    } else {
        native_rate = (clock > 0) ? (clock / 288) : 83333; // C352
    }
    engine->namco.step_scale = (uint32_t)(((uint64_t)native_rate << 16) / engine->sample_rate);
}

void pcm_engine_namco_write(PCMSoundEngine *engine, uint16_t addr, uint16_t data) {
    if (engine->namco.type == 0xC2) { // C352 (VGM 0xE1: 5バイト, addr=16bit LE, data=16bit LE)
        // Global registers
        if (addr >= 0x200) {
            if (addr == 0x202) {
                /* Execute KeyOn/KeyOff for channels with flags set */
                for (int i = 0; i < engine->namco.channels; i++) {
                    NamcoPCMChannel *ch = &engine->namco.ch[i];
                    if (ch->flags & 0x4000) { // C352_FLG_KEYON
                        ch->playing = true;
                        ch->latched_bank = ch->bank;
                        ch->latched_start = ch->start;
                        ch->latched_end = ch->end;
                        ch->latched_loop = ch->loop;
                        ch->pos = ((uint32_t)ch->latched_bank << 16) | (ch->latched_start & 0xFFFF);
                        ch->counter = 0xFFFF;
                        ch->prev_sample = 0;
                        ch->last_sample = 0;
                        ch->flags &= ~0x4800; // Clear KEYON and LOOPHIST
                        ch->mode = ch->flags;
                    }
                    if (ch->flags & 0x2000) { // C352_FLG_KEYOFF
                        ch->playing = false;
                        ch->flags &= ~0x2000;
                        ch->mode = ch->flags;
                    }
                }
            }
            return;
        }

        // Channel registers: addr is word address
        int ch_idx = addr / 8;
        int word_reg = addr % 8;
        if (ch_idx >= engine->namco.channels) return;

        NamcoPCMChannel *ch = &engine->namco.ch[ch_idx];

        switch (word_reg) {
            case 0:
                ch->vol_l = data & 0xFF;
                ch->vol_r = (data >> 8) & 0xFF;
                break;
            case 1:
                ch->vol_rear_l = data & 0xFF;
                ch->vol_rear_r = (data >> 8) & 0xFF;
                break;
            case 2:
                ch->freq = data;
                ch->step = data;
                break;
            case 3:
                ch->flags = data;
                ch->mode = data;
                break;
            case 4:
                ch->bank = data;
                break;
            case 5:
                ch->start = data;
                break;
            case 6:
                ch->end = data;
                break;
            case 7:
                ch->loop = data;
                break;
        }
    } else if (engine->namco.type == 0xC1) { // C140
        uint8_t data8 = data & 0xFF;
        int ch_idx = addr / 16;
        int offset = addr % 16;
        if (ch_idx >= 24) return;
        NamcoPCMChannel *ch = &engine->namco.ch[ch_idx];

        // MAME仕様に基づくC140のレジスタデコード (16bit値は上位バイトが先)
        switch (offset) {
            case 0x00: ch->vol_r = data8; break;
            case 0x01: ch->vol_l = data8; break;
            case 0x02: ch->step = (ch->step & 0x00FF) | (data8 << 8); break; // Freq MSB
            case 0x03: ch->step = (ch->step & 0xFF00) | data8; break;        // Freq LSB
            case 0x04: ch->bank = data8; break;                              // Bank
            case 0x05: 
                ch->mode = data8; // Mode
                if (data8 & 0x80) { // C140 KeyOn is 0x05 bit7
                    ch->playing = true;
                    ch->latched_bank = ch->bank;
                    ch->latched_end = ch->end;
                    ch->latched_loop = ch->loop;
                    ch->pos = ((uint32_t)ch->latched_bank << 16) | ch->start;
                    ch->pos_frac = 0;
                    ch->counter = 0;
                    ch->prev_sample = 0;
                    ch->last_sample = 0;
                } else {
                    ch->playing = false;
                }
                break;
            case 0x06: ch->start = (ch->start & 0x00FF) | (data8 << 8); break; // Start MSB
            case 0x07: ch->start = (ch->start & 0xFF00) | data8; break;        // Start LSB
            case 0x08: ch->end = (ch->end & 0x00FF) | (data8 << 8); break;     // End MSB
            case 0x09: ch->end = (ch->end & 0xFF00) | data8; break;            // End LSB
            case 0x0A: ch->loop = (ch->loop & 0x00FF) | (data8 << 8); break;   // Loop MSB
            case 0x0B: ch->loop = (ch->loop & 0xFF00) | data8; break;          // Loop LSB
            case 0x0C:
            case 0x0D:
            case 0x0E:
            case 0x0F:
                break; // 未使用
        }
    }
}

void pcm_engine_namco_mmap(PCMSoundEngine *engine, uint32_t start_addr, const uint8_t *data, uint32_t size) {
    if (engine->namco.block_count >= 64) return;
    engine->namco.blocks[engine->namco.block_count].start_addr = start_addr;
    engine->namco.blocks[engine->namco.block_count].size = size;
    engine->namco.blocks[engine->namco.block_count].data = data;
    engine->namco.blocks[engine->namco.block_count].is_allocated = false;
    engine->namco.is_mmap = true;
    engine->namco.block_count++;
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
        engine->msm6258.block_count > 0 &&
        engine->msm6258.data_end > engine->msm6258.data_pos) {

        engine->msm6258.step_accum += engine->msm6258.step_size;

        while (engine->msm6258.step_accum >= 65536) {
            engine->msm6258.step_accum -= 65536;

            if (engine->msm6258.data_pos >= engine->msm6258.data_end) {
                engine->msm6258.playing = 0;
                break;
            }
            uint8_t byte = 0;
            bool byte_found = false;
            for (int b = 0; b < engine->msm6258.block_count; b++) {
                if (engine->msm6258.blocks[b] != NULL &&
                    engine->msm6258.data_pos >= engine->msm6258.block_offsets[b] &&
                    engine->msm6258.data_pos < engine->msm6258.block_offsets[b] + engine->msm6258.block_sizes[b]) {
                    byte = engine->msm6258.blocks[b][engine->msm6258.data_pos - engine->msm6258.block_offsets[b]];
                    byte_found = true;
                    break;
                }
            }
            if (!byte_found) {
                engine->msm6258.playing = 0;
                break;
            }
            int nibble = (engine->msm6258.nibble_sel == 0)
                         ? ((byte >> 4) & 0x0F)
                         : (byte & 0x0F);
            engine->msm6258.nibble_sel ^= 1;
            if (engine->msm6258.nibble_sel == 0) {
                engine->msm6258.data_pos++;
            }
            msm6258_decode_nibble(&engine->msm6258, nibble);
        }

        int32_t out = engine->msm6258.adpcm_val * 10;
        mix_l += out;
        mix_r += out;
    }

    // ── SegaPCM ──────────────────────────────
    if (engine->segapcm.block_count > 0) {
        int32_t spcm_l = 0;
        int32_t spcm_r = 0;
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

            if ((addr >> 16) == end) {
                if (flags & 2) {
                    engine->segapcm.flags[ch] |= 1;
                    engine->segapcm.regs[0x80 + 8*ch + 6] |= 1;
                    continue;
                } else {
                    addr = loop;
                }
            }

            uint32_t rom_addr = bank + (addr >> 8);
            int32_t sample = 0;
            int b = engine->segapcm.last_block[ch];
            
            if (b < engine->segapcm.block_count && engine->segapcm.blocks[b].data != NULL &&
                rom_addr >= engine->segapcm.blocks[b].start_addr &&
                rom_addr <  engine->segapcm.blocks[b].start_addr + engine->segapcm.blocks[b].size) {
                sample = (int32_t)engine->segapcm.blocks[b].data[rom_addr - engine->segapcm.blocks[b].start_addr] - 128;
            } else {
                if (engine->segapcm.unmapped_page[ch] == (rom_addr & ~0xFFF)) {
                    sample = 0;
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
                        sample = 0;
                    }
                }
            }

            spcm_l += sample * engine->segapcm.vol_l[ch];
            spcm_r += sample * engine->segapcm.vol_r[ch];

            engine->segapcm.step_acc[ch] += (uint32_t)delta * engine->segapcm.step_scale;
            uint32_t whole_steps = engine->segapcm.step_acc[ch] >> 16;
            engine->segapcm.step_acc[ch] &= 0xFFFF;
            addr = (addr + whole_steps) & 0xFFFFFF;
            engine->segapcm.addr[ch] = addr;
        }
        mix_l += spcm_l << 3;
        mix_r += spcm_r << 3;
    }

    // ── Namco C140/C352 ──────────────────────
    if (engine->c140_enabled || engine->c352_enabled) {
        int32_t namco_mix_l = 0;
        int32_t namco_mix_r = 0;
        uint8_t max_ch = engine->c352_enabled ? 32 : 24;
        
        for (int ch = 0; ch < max_ch; ch++) {
            NamcoPCMChannel *v = &engine->namco.ch[ch];
            if (!v->playing) continue;

            // Counter-based interpolation
            v->counter += (uint32_t)(((uint64_t)v->step * engine->namco.step_scale) >> 16);
            
            uint32_t step_amount = v->counter >> 16;
            if (step_amount > 0) {
                v->counter &= 0xFFFF;

                for (uint32_t i = 0; i < step_amount; i++) {
                    v->prev_sample = v->last_sample;

                    bool hit_end = false;
                    if (engine->c352_enabled) {
                        if (v->mode & 0x0001) v->pos--;
                        else v->pos++;
                        if ((v->pos & 0xFFFF) == v->latched_end) hit_end = true;
                    } else {
                        v->pos++;
                        if ((v->pos & 0xFFFF) == v->latched_end) hit_end = true;
                    }

                    if (hit_end) {
                        if (engine->c352_enabled && (v->mode & 0x0010) && (v->mode & 0x0002)) { // LINK && LOOP
                            v->pos = ((uint32_t)v->latched_start << 16) | v->latched_loop;
                            v->mode |= 0x0800;
                        } else if (engine->c352_enabled && (v->mode & 0x0002)) { // LOOP only
                            v->pos = (v->pos & 0xFF0000) | v->latched_loop;
                            v->mode |= 0x0800;
                        } else if (engine->c140_enabled && (v->mode & 0x10)) {
                            v->pos = (v->pos & 0xFF0000) | v->latched_loop;
                        } else {
                            v->playing = false;
                            break;
                        }
                    }

                    uint32_t addr;
                    if (engine->c352_enabled) {
                        addr = v->pos;
                    } else {
                        uint32_t base_addr = ((uint32_t)v->latched_bank << 16) + (v->pos & 0xFFFF);
                        if (engine->namco.c140_type == 0) {
                            addr = ((base_addr & 0x200000) >> 2) | (base_addr & 0x7FFFF);
                        } else {
                            addr = ((base_addr & 0x300000) >> 1) | (base_addr & 0x7FFFF);
                        }
                    }

                    uint8_t dt = 0x00;
                    if (engine->c140_enabled) dt = 0x80;

                    int b = v->current_block;
                    if (b < engine->namco.block_count && 
                        addr >= engine->namco.blocks[b].start_addr && 
                        addr < engine->namco.blocks[b].start_addr + engine->namco.blocks[b].size) {
                        dt = engine->namco.blocks[b].data[addr - engine->namco.blocks[b].start_addr];
                    } else {
                        for (int j = 0; j < engine->namco.block_count; j++) {
                            if (addr >= engine->namco.blocks[j].start_addr && 
                                addr < engine->namco.blocks[j].start_addr + engine->namco.blocks[j].size) {
                                dt = engine->namco.blocks[j].data[addr - engine->namco.blocks[j].start_addr];
                                v->current_block = j;
                                break;
                            }
                        }
                    }

                    int16_t new_sample = 0;
                    if (engine->c352_enabled) {
                        if (v->mode & 0x0004) { // 0x0004 is MuLaw
                            new_sample = c352_mulaw_table[dt];
                        } else {
                            new_sample = (int16_t)((int8_t)dt) << 8;
                        }
                    } else {
                        if (v->mode & 0x08) {
                            int16_t sdt = ((int8_t)dt) >> 3;
                            if (sdt < 0) new_sample = (sdt << (dt & 7)) - c140_pcmtbl[dt & 7];
                            else         new_sample = (sdt << (dt & 7)) + c140_pcmtbl[dt & 7];
                        } else {
                            int16_t sdt = (int8_t)dt;
                            new_sample = sdt << 5;
                        }
                    }
                    v->last_sample = new_sample;
                }
            }

            if (!v->playing) continue;

            int32_t dltdt = v->last_sample - v->prev_sample;
            int16_t sample = ((dltdt * v->counter) >> 16) + v->prev_sample;

            int16_t s_fl = (v->mode & 0x0100) ? -sample : sample;
            int16_t s_rl = (v->mode & 0x0200) ? -sample : sample;
            int16_t s_fr = (v->mode & 0x0080) ? -sample : sample;
            int16_t s_rr = (v->mode & 0x0080) ? -sample : sample; // MAME maps FR phase to RR as well
            
            namco_mix_l += (s_fl * v->vol_l + s_rl * v->vol_rear_l) >> 8;
            namco_mix_r += (s_fr * v->vol_r + s_rr * v->vol_rear_r) >> 8;
        }
        // Scale up Namco PCM because internal amplitude is small compared to FM
        mix_l += namco_mix_l * 4;
        mix_r += namco_mix_r * 4;
    }

    pcm_engine_opn_tick(engine, &mix_l, &mix_r);

    *out_l += mix_l;
    *out_r += mix_r;
}
// ────────────────────────────────────────────────────────────────────────────
// YM2608 / YM2610 OPN PCM (Rhythm / ADPCM-A / ADPCM-B)
// ────────────────────────────────────────────────────────────────────────────
static void opn_adpcm_decode_nibble(int nibble, int32_t *adpcm_val, int32_t *adpcm_step_idx) {
    int step  = oki_step_table[*adpcm_step_idx];
    int delta = step >> 3;
    if (nibble & 1) delta += (step >> 2);
    if (nibble & 2) delta += (step >> 1);
    if (nibble & 4) delta += step;
    if (nibble & 8) delta = -delta;
    *adpcm_val += delta;
    if (*adpcm_val >  2047) *adpcm_val =  2047;
    if (*adpcm_val < -2048) *adpcm_val = -2048;
    *adpcm_step_idx += oki_index_table[nibble & 0x0F];
    if (*adpcm_step_idx < 0)  *adpcm_step_idx = 0;
    if (*adpcm_step_idx > 48) *adpcm_step_idx = 48;
}

static uint8_t opn_read_rom(const uint8_t** blocks, uint32_t* offsets, uint32_t* sizes, uint32_t count, uint32_t addr) {
    for (uint32_t i = 0; i < count; i++) {
        if (addr >= offsets[i] && addr < offsets[i] + sizes[i]) {
            return blocks[i][addr - offsets[i]];
        }
    }
    return 0;
}

void pcm_engine_opn_init(PCMSoundEngine *engine, uint32_t clock) {
    memset(&engine->opn, 0, sizeof(OPN_PCM_Matrix));
    engine->opn.clock = clock;
}

void pcm_engine_write_opn_rhythm(PCMSoundEngine *engine, uint8_t reg, uint8_t data) {
    engine->opn.regs[reg] = data;
    // YM2608 Rhythm Registers (0x10 - 0x1D)
    if (reg == 0x10) {
        // Dump / KeyOn
        if (data & 0x80) { // Dump (stop all?)
             for(int i=0; i<6; i++) engine->opn.ch_a[i].playing = false;
        } else {
             // bit0-5: KeyOn for ch0-5
             for(int i=0; i<6; i++) {
                 if (data & (1<<i)) {
                     engine->opn.ch_a[i].playing = true;
                     engine->opn.ch_a[i].pos_frac = 0;
                     engine->opn.ch_a[i].adpcm_val = 0;
                     engine->opn.ch_a[i].adpcm_step_idx = 0;
                     engine->opn.ch_a[i].pos = engine->opn.ch_a[i].start_addr;
                 }
             }
        }
    } else if (reg >= 0x18 && reg <= 0x1D) {
        int ch = reg - 0x18;
        engine->opn.ch_a[ch].vol_l = (data & 0x80) ? (data & 0x1F) : 0;
        engine->opn.ch_a[ch].vol_r = (data & 0x40) ? (data & 0x1F) : 0;
        // YM2608 rhythm freq is fixed. We assume 44.1kHz sample rate, 
        // YM2608 rhythm sample rate is clock / 144 / 6? Wait, it's clock / 72.
        // Let's use a fixed step. YM2610 ADPCM-A sets step via 0x100.
        engine->opn.ch_a[ch].step = (engine->opn.clock / 72) * 65536 / engine->sample_rate;
    }
}

void pcm_engine_write_opn_adpcma(PCMSoundEngine *engine, uint8_t reg, uint8_t data) {
    engine->opn.regs[reg] = data;
    // YM2610 ADPCM-A Registers (0x100 - 0x12F)
    // Actually in VGM it's Port 1, reg 0x10-0x2F
    if (reg == 0x100 || reg == 0x10) {
        // KeyOn
        if (data & 0x80) { // Dump
             for(int i=0; i<6; i++) engine->opn.ch_a[i].playing = false;
        } else {
             for(int i=0; i<6; i++) {
                 if (data & (1<<i)) {
                     engine->opn.ch_a[i].playing = true;
                     engine->opn.ch_a[i].pos_frac = 0;
                     engine->opn.ch_a[i].adpcm_val = 0;
                     engine->opn.ch_a[i].adpcm_step_idx = 0;
                     engine->opn.ch_a[i].pos = engine->opn.ch_a[i].start_addr;
                 }
             }
        }
    } else if (reg >= 0x108 && reg <= 0x10D) {
        int ch = reg - 0x108;
        engine->opn.ch_a[ch].vol_l = (data & 0x80) ? (data & 0x1F) : 0;
        engine->opn.ch_a[ch].vol_r = (data & 0x40) ? (data & 0x1F) : 0;
    } else if (reg >= 0x110 && reg <= 0x115) { // Start address LSB
        // start = (start_msb << 16) | (start_lsb << 8)
    } else if (reg >= 0x118 && reg <= 0x11D) { // Start address MSB
        int ch = reg - 0x118;
        uint16_t start = (data << 8) | engine->opn.regs[0x110 + ch];
        engine->opn.ch_a[ch].start_addr = start << 8;
    } else if (reg >= 0x120 && reg <= 0x125) { // End address LSB
    } else if (reg >= 0x128 && reg <= 0x12D) { // End address MSB
        int ch = reg - 0x128;
        uint16_t end = (data << 8) | engine->opn.regs[0x120 + ch];
        engine->opn.ch_a[ch].end_addr = (end << 8) + 0xFF;
    }
}

void pcm_engine_write_opn_adpcmb(PCMSoundEngine *engine, uint8_t reg, uint8_t data) {
    // Port 1 0x00 - 0x1C (YM2608 and YM2610 Delta-T)
    engine->opn.ch_b.regs[reg] = data;
    if (reg == 0x00) {
        if (data & 0x80) {
            engine->opn.ch_b.playing = true;
            engine->opn.ch_b.pos_frac = 0;
            engine->opn.ch_b.adpcm_val = 0;
            engine->opn.ch_b.adpcm_step_idx = 0;
            engine->opn.ch_b.pos = engine->opn.ch_b.start_addr;
        }
        if (data & 0x01) { // Reset
            engine->opn.ch_b.playing = false;
        }
    } else if (reg == 0x02) {
        // Start LSB
    } else if (reg == 0x03) {
        uint16_t start = (data << 8) | engine->opn.ch_b.regs[0x02];
        engine->opn.ch_b.start_addr = start << 8;
    } else if (reg == 0x04) {
        // End LSB
    } else if (reg == 0x05) {
        uint16_t end = (data << 8) | engine->opn.ch_b.regs[0x04];
        engine->opn.ch_b.end_addr = (end << 8) + 0xFF;
    } else if (reg == 0x09) { // Delta-N LSB
    } else if (reg == 0x0A) { // Delta-N MSB
        uint16_t delta_n = (data << 8) | engine->opn.ch_b.regs[0x09];
        // step = delta_n * clock / (72 * sample_rate) ?
        engine->opn.ch_b.step = (uint32_t)delta_n * 65536 / 72; // rough approx
    } else if (reg == 0x0B) { // Vol
        engine->opn.ch_b.vol_l = data;
        engine->opn.ch_b.vol_r = data;
    }
}

void pcm_engine_opn_tick(PCMSoundEngine *engine, int32_t *out_l, int32_t *out_r) {
    int32_t mix_l = 0, mix_r = 0;
    
    // ADPCM-A
    if (engine->opn.adpcma_block_count > 0) {
        for (int i = 0; i < OPN_ADPCMA_CHANNELS; i++) {
            OPN_ADPCM_Channel *ch = &engine->opn.ch_a[i];
            if (!ch->playing) continue;

            ch->pos_frac += ch->step;
            uint32_t step_amount = ch->pos_frac >> 16;
            ch->pos_frac &= 0xFFFF;

            for (uint32_t s = 0; s < step_amount; s++) {
                if (ch->pos >= ch->end_addr) { ch->playing = false; break; }
                uint8_t byte = opn_read_rom(engine->opn.adpcma_blocks, engine->opn.adpcma_block_offsets, engine->opn.adpcma_block_sizes, engine->opn.adpcma_block_count, ch->pos / 2);
                uint8_t nibble = (ch->pos & 1) ? (byte & 0x0F) : (byte >> 4);
                opn_adpcm_decode_nibble(nibble, &ch->adpcm_val, &ch->adpcm_step_idx);
                ch->pos++;
            }
            if (ch->playing) {
                mix_l += (ch->adpcm_val * ch->vol_l * 5) >> 8;
                mix_r += (ch->adpcm_val * ch->vol_r * 5) >> 8;
            }
        }
    }

    // ADPCM-B
    if (engine->opn.adpcmb_block_count > 0) {
        OPN_DeltaT_Channel *ch = &engine->opn.ch_b;
        if (ch->playing) {
            ch->pos_frac += ch->step;
            uint32_t step_amount = ch->pos_frac >> 16;
            ch->pos_frac &= 0xFFFF;

            for (uint32_t s = 0; s < step_amount; s++) {
                if (ch->pos >= ch->end_addr) { ch->playing = false; break; }
                uint8_t byte = opn_read_rom(engine->opn.adpcmb_blocks, engine->opn.adpcmb_block_offsets, engine->opn.adpcmb_block_sizes, engine->opn.adpcmb_block_count, ch->pos / 2);
                uint8_t nibble = (ch->pos & 1) ? (byte & 0x0F) : (byte >> 4);
                opn_adpcm_decode_nibble(nibble, &ch->adpcm_val, &ch->adpcm_step_idx);
                ch->pos++;
            }
            if (ch->playing) {
                mix_l += (ch->adpcm_val * ch->vol_l * 5) >> 8;
                mix_r += (ch->adpcm_val * ch->vol_r * 5) >> 8;
            }
        }
    }
    
    *out_l += mix_l;
    *out_r += mix_r;
}

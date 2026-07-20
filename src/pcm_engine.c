#include "pcm_engine.h"
#include "fm_engine.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <esp_heap_caps.h>

PCMSoundEngine g_pcm_engine;

#define NAMCO_MCACHE_SIZE 32
#define NAMCO_MCACHE_MASK (~(NAMCO_MCACHE_SIZE - 1))
static uint32_t namco_mcache_addr[32];
static uint8_t  namco_mcache_data[32][NAMCO_MCACHE_SIZE];

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
    int step  = oki_step_table[c->adpcm_step];
    int delta = step >> 3;
    if (nibble & 1) delta += (step >> 2);
    if (nibble & 2) delta += (step >> 1);
    if (nibble & 4) delta += step;
    if (nibble & 8) delta = -delta;
    c->adpcm_val -= c->adpcm_val >> 6;
    c->adpcm_val += delta;
    if (c->adpcm_val >  2047) c->adpcm_val =  2047;
    if (c->adpcm_val < -2048) c->adpcm_val = -2048;
    c->adpcm_step += oki_index_table[nibble & 0x0F];
    if (c->adpcm_step < 0)  c->adpcm_step = 0;
    if (c->adpcm_step > 48) c->adpcm_step = 48;
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
        if (engine->opn.adpcma_block_count < 64 && size > 8) {
            int c = engine->opn.adpcma_block_count;
            uint32_t start_addr = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
            engine->opn.adpcma_block_offsets[c] = start_addr;
            engine->opn.adpcma_block_sizes[c] = size - 8;
            engine->opn.adpcma_blocks[c] = data + 8;
            engine->opn.adpcma_block_count++;
        }
    } else if (type == 0x81 || type == 0x83) { // YM2608 / YM2610 Delta-T
        if (engine->opn.adpcmb_block_count < 64 && size > 8) {
            int c = engine->opn.adpcmb_block_count;
            uint32_t start_addr = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
            engine->opn.adpcmb_block_offsets[c] = start_addr;
            engine->opn.adpcmb_block_sizes[c] = size - 8;
            engine->opn.adpcmb_blocks[c] = data + 8;
            engine->opn.adpcmb_block_count++;
        }
    } else if (type == 0x8B) {
        if (engine->oki.block_count < 64 && size > 8) {
            int c = engine->oki.block_count;
            uint32_t start_addr = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
            engine->oki.block_offsets[c] = start_addr;
            engine->oki.block_sizes[c] = size - 8;
            engine->oki.blocks[c] = data + 8;
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
            engine->msm6258.adpcm_step = 0;
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
    engine->msm6258.adpcm_step = 0;
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
    memset(engine->segapcm.regs, 0x00, 256);
    
    for (int i = 0; i < 16; i++) {
        engine->segapcm.low[i]          = 0;
        engine->segapcm.step_acc[i]     = 0;
        engine->segapcm.last_block[i]   = 0xFF;
        engine->segapcm.unmapped_page[i] = 0xFFFFFFFF;
        engine->segapcm.flags[i] = 1;
        engine->segapcm.line_addr[i] = 0xFFFFFFFF;
    }
    uint32_t chip_rate = engine->segapcm.clock / 128;
    engine->segapcm.step_scale = (uint32_t)(((uint64_t)chip_rate << 16) / engine->sample_rate);
}

void pcm_engine_segapcm_write(PCMSoundEngine *engine, uint8_t reg, uint8_t data) {
    engine->segapcm.regs[reg] = data;
    int ch = (reg & 0x7F) / 8;
    if (ch >= 16) return;

    uint8_t *lo = &engine->segapcm.regs[8 * ch];
    engine->segapcm.vol_l[ch] = lo[2] & 0x7F;
    engine->segapcm.vol_r[ch] = lo[3] & 0x7F;
    uint16_t loop_val = (uint16_t)lo[4] | ((uint16_t)lo[5] << 8);
    engine->segapcm.loop[ch] = (uint32_t)loop_val << 8;
    engine->segapcm.end[ch]  = lo[6];
    engine->segapcm.delta[ch] = (lo[7] == 0) ? 256 : lo[7];

    uint8_t *hi = &engine->segapcm.regs[0x80 + 8*ch];
    uint8_t ctrl = hi[6];
    engine->segapcm.flags[ch] = ctrl;
    engine->segapcm.bank[ch]  = (uint32_t)(ctrl & engine->segapcm.bank_mask) << engine->segapcm.bank_shift;

    if (reg >= 0x80) {
        int ri = reg % 8;
        if (ri == 4 || ri == 5 || ri == 6) {
            engine->segapcm.addr[ch] = ((uint32_t)hi[5] << 16) | ((uint32_t)hi[4] << 8);
            engine->segapcm.last_block[ch]    = 0xFF;
            engine->segapcm.unmapped_page[ch] = 0xFFFFFFFF;
            engine->segapcm.line_addr[ch] = 0xFFFFFFFF; // Invalidate cache on bank switch
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

static inline void update_namco_volumes(PCMSoundEngine *engine, NamcoPCMChannel *ch) {
    if (engine->c352_enabled) {
        int16_t s_fl = (ch->mode & 0x0100) ? -1 : 1;
        int16_t s_rl = (ch->mode & 0x0200) ? -1 : 1;
        int16_t s_fr = (ch->mode & 0x0080) ? -1 : 1;
        int16_t s_rr = (ch->mode & 0x0080) ? -1 : 1;
        int32_t vl = s_fl * (int32_t)ch->vol_l + s_rl * (int32_t)ch->vol_rear_l;
        int32_t vr = s_fr * (int32_t)ch->vol_r + s_rr * (int32_t)ch->vol_rear_r;
        if (vl >  510) vl =  510;
        if (vl < -510) vl = -510;
        if (vr >  510) vr =  510;
        if (vr < -510) vr = -510;
        ch->mix_vol_l = (int16_t)vl;
        ch->mix_vol_r = (int16_t)vr;
    } else {
        ch->mix_vol_l = ch->vol_l;
        ch->mix_vol_r = ch->vol_r;
    }
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

    for (int i = 0; i < 32; i++) {
        namco_mcache_addr[i] = 0xFFFFFFFF; // ★ ミニキャッシュのアドレスを初期化
    }

    uint32_t native_rate;
    if (type == 0xC1) {
        native_rate = (clock > 0) ? (clock / 192) : (21390000 / 192);
    } else {
        native_rate = (clock > 0) ? (clock / 288) : 83333; // C352
    }
    engine->namco.step_scale = (uint32_t)(((uint64_t)native_rate << 16) / engine->sample_rate);
}

void pcm_engine_namco_write(PCMSoundEngine *engine, uint16_t addr, uint16_t data) {
    if (engine->namco.type == 0xC2) { // C352
        if (addr >= 0x200) {
            if (addr == 0x202) {
                for (int i = 0; i < engine->namco.channels; i++) {
                    NamcoPCMChannel *ch = &engine->namco.ch[i];
                    if (ch->flags & 0x4000) {
                        ch->playing = true;
                        ch->latched_bank = ch->bank;
                        ch->latched_start = ch->start;
                        ch->latched_end = ch->end;
                        ch->latched_loop = ch->loop;
                        ch->pos = ((uint32_t)ch->latched_bank << 16) | (ch->latched_start & 0xFFFF);
                        ch->counter = 0xFFFF;
                        ch->prev_sample = 0;
                        ch->last_sample = 0;
                        ch->current_block = -1;
                        ch->flags &= ~0x4800;
                        ch->mode = ch->flags;
                        namco_mcache_addr[i] = 0xFFFFFFFF; // キャッシュクリア
                        update_namco_volumes(engine, ch);
                    }
                    if (ch->flags & 0x2000) {
                        ch->playing = false;
                        ch->flags &= ~0x2000;
                        ch->mode = ch->flags;
                        update_namco_volumes(engine, ch);
                    }
                }
            }
            return;
        }

        int ch_idx = addr / 8;
        int word_reg = addr % 8;
        if (ch_idx >= engine->namco.channels) return;
        NamcoPCMChannel *ch = &engine->namco.ch[ch_idx];

        switch (word_reg) {
            case 0: ch->vol_l = data & 0xFF; ch->vol_r = (data >> 8) & 0xFF; update_namco_volumes(engine, ch); break;
            case 1: ch->vol_rear_l = data & 0xFF; ch->vol_rear_r = (data >> 8) & 0xFF; update_namco_volumes(engine, ch); break;
            case 2: ch->freq = data; ch->step = data; ch->step_adjusted = (uint32_t)(((uint64_t)ch->step * engine->namco.step_scale) >> 16); break;
            case 3: ch->flags = data; ch->mode = data; update_namco_volumes(engine, ch); break;
            case 4: ch->bank = data; break;
            case 5: ch->start = data; break;
            case 6: ch->end = data; break;
            case 7: ch->loop = data; break;
        }
    } else if (engine->namco.type == 0xC1) { // C140
        uint8_t data8 = data & 0xFF;
        int ch_idx = addr / 16;
        int offset = addr % 16;
        if (ch_idx >= 24) return;
        NamcoPCMChannel *ch = &engine->namco.ch[ch_idx];

        switch (offset) {
            case 0x00: ch->vol_r = data8; update_namco_volumes(engine, ch);break;
            case 0x01: ch->vol_l = data8; update_namco_volumes(engine, ch);break;
            case 0x02: ch->step = (ch->step & 0x00FF) | (data8 << 8); ch->step_adjusted = (uint32_t)(((uint64_t)ch->step * engine->namco.step_scale) >> 16); break;
            case 0x03: ch->step = (ch->step & 0xFF00) | data8; ch->step_adjusted = (uint32_t)(((uint64_t)ch->step * engine->namco.step_scale) >> 16);break;
            case 0x04: ch->bank = data8; break;
            case 0x05: 
                ch->mode = data8;
                if (data8 & 0x80) {
                    ch->playing = true;
                    ch->latched_bank = ch->bank;
                    ch->latched_end = ch->end;
                    ch->latched_loop = ch->loop;
                    ch->pos = ((uint32_t)ch->latched_bank << 16) | ch->start;
                    ch->pos_frac = 0;
                    ch->counter = 0;
                    ch->prev_sample = 0;
                    ch->last_sample = 0;
                    ch->current_block = -1;
                    namco_mcache_addr[ch_idx] = 0xFFFFFFFF; // キャッシュクリア
                } else {
                    ch->playing = false;
                }
                break;
            case 0x06: ch->start = (ch->start & 0x00FF) | (data8 << 8); break;
            case 0x07: ch->start = (ch->start & 0xFF00) | data8; break;
            case 0x08: ch->end = (ch->end & 0x00FF) | (data8 << 8); break;
            case 0x09: ch->end = (ch->end & 0xFF00) | data8; break;
            case 0x0A: ch->loop = (ch->loop & 0x00FF) | (data8 << 8); break;
            case 0x0B: ch->loop = (ch->loop & 0xFF00) | data8; break;
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
        int32_t v = ((int32_t)engine->ym2612_dac.dac_data - 128) << 7; // ±16384 (matches FM max)
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

        int32_t out = engine->msm6258.adpcm_val * 16; // ±16384 (matches FM max)
        mix_l += out;
        mix_r += out;
    }

    // ── SegaPCM ──────────────────────────────
    if (engine->segapcm.block_count > 0) {
        int32_t spcm_l = 0;
        int32_t spcm_r = 0;
        uint32_t step_scale = engine->segapcm.step_scale;
        int block_cnt = engine->segapcm.block_count;
        SegaPCMBlock *blocks = engine->segapcm.blocks;

        for (int ch = 0; ch < 16; ch++) {
            uint8_t flags = engine->segapcm.flags[ch];
            if (flags & 1) continue;

            int32_t vl = engine->segapcm.vol_l[ch];
            int32_t vr = engine->segapcm.vol_r[ch];

            if (vl == 0 && vr == 0) {
                engine->segapcm.step_acc[ch] += (uint32_t)engine->segapcm.delta[ch] * step_scale;
                engine->segapcm.addr[ch] = (engine->segapcm.addr[ch] + (engine->segapcm.step_acc[ch] >> 16)) & 0xFFFFFF;
                engine->segapcm.step_acc[ch] &= 0xFFFF;
                continue;
            }

            uint32_t addr = engine->segapcm.addr[ch];
            if ((addr >> 16) == engine->segapcm.end[ch]) {
                if (flags & 2) {
                    engine->segapcm.flags[ch] |= 1;
                    engine->segapcm.regs[0x80 + 8*ch + 6] |= 1;
                    continue;
                } else {
                    addr = engine->segapcm.loop[ch];
                }
            }

            uint32_t rom_addr = engine->segapcm.bank[ch] + (addr >> 8);
            
            int32_t sample = 0;
            int b = engine->segapcm.last_block[ch];
            
            // Cache lookup
            if (rom_addr >= engine->segapcm.line_addr[ch] && rom_addr < engine->segapcm.line_addr[ch] + 32) {
                // Cache hit
                sample = (int32_t)engine->segapcm.line_buf[ch][rom_addr - engine->segapcm.line_addr[ch]] - 128;
            } else {
                // Cache miss
                if (b < block_cnt && blocks[b].data != NULL &&
                    rom_addr >= blocks[b].start_addr &&
                    rom_addr <  blocks[b].start_addr + blocks[b].size) {
                    
                    uint32_t offset = rom_addr - blocks[b].start_addr;
                    uint32_t bytes_to_copy = blocks[b].size - offset;
                    if (bytes_to_copy > 32) bytes_to_copy = 32;
                    
                    memcpy(engine->segapcm.line_buf[ch], &blocks[b].data[offset], bytes_to_copy);
                    engine->segapcm.line_addr[ch] = rom_addr;
                    
                    sample = (int32_t)engine->segapcm.line_buf[ch][0] - 128;
                } else {
                    if (engine->segapcm.unmapped_page[ch] == (rom_addr & ~0xFFF)) {
                        sample = 0;
                    } else {
                        int found = 0;
                        for (b = 0; b < block_cnt; b++) {
                            if (blocks[b].data != NULL &&
                                rom_addr >= blocks[b].start_addr &&
                                rom_addr <  blocks[b].start_addr + blocks[b].size) {
                                
                                engine->segapcm.last_block[ch] = (uint8_t)b;
                                uint32_t offset = rom_addr - blocks[b].start_addr;
                                uint32_t bytes_to_copy = blocks[b].size - offset;
                                if (bytes_to_copy > 32) bytes_to_copy = 32;
                                
                                memcpy(engine->segapcm.line_buf[ch], &blocks[b].data[offset], bytes_to_copy);
                                engine->segapcm.line_addr[ch] = rom_addr;
                                
                                sample = (int32_t)engine->segapcm.line_buf[ch][0] - 128;
                                found = 1;
                                break;
                            }
                        }
                        if (!found) {
                            engine->segapcm.unmapped_page[ch] = rom_addr & ~0xFFF;
                        }
                    }
                }
            }

            spcm_l += sample * vl;
            spcm_r += sample * vr;

            engine->segapcm.step_acc[ch] += (uint32_t)engine->segapcm.delta[ch] * step_scale;
            uint32_t whole_steps = engine->segapcm.step_acc[ch] >> 16;
            engine->segapcm.step_acc[ch] &= 0xFFFF;
            engine->segapcm.addr[ch] = (addr + whole_steps) & 0xFFFFFF;
        }
        mix_l += spcm_l * 2; // Doubled to increase volume to match FM
        mix_r += spcm_r * 2;
    }

    // ── Namco C140/C352 ──────────────────────
    if (engine->c140_enabled || engine->c352_enabled) {
        int32_t namco_mix_l = 0;
        int32_t namco_mix_r = 0;
        uint8_t max_ch = engine->c352_enabled ? 32 : 24;
        
        for (int ch = 0; ch < max_ch; ch++) {
            NamcoPCMChannel *v = &engine->namco.ch[ch];
            if (!v->playing) continue;

            v->counter += v->step_adjusted;
            uint32_t step_amount = v->counter >> 16;
            if (step_amount > 0) {
                v->counter &= 0xFFFF;
                v->prev_sample = v->last_sample;

                if (engine->c352_enabled) {
                    if (v->mode & 0x0001) v->pos -= step_amount;
                    else v->pos += step_amount;
                } else {
                    v->pos += step_amount;
                }

                bool hit_end = false;
                if (engine->c352_enabled) {
                    if ((v->mode & 0x0001) && (v->pos & 0xFFFF) <= v->latched_end) hit_end = true;
                    else if (!(v->mode & 0x0001) && (v->pos & 0xFFFF) >= v->latched_end) hit_end = true;
                } else {
                    if ((v->pos & 0xFFFF) >= v->latched_end) hit_end = true;
                }

                if (hit_end) {
                    if (engine->c352_enabled && (v->mode & 0x0010) && (v->mode & 0x0002)) {
                        v->pos = ((uint32_t)v->latched_start << 16) | v->latched_loop;
                        v->mode |= 0x0800;
                    } else if (engine->c352_enabled && (v->mode & 0x0002)) {
                        v->pos = (v->pos & 0xFF0000) | v->latched_loop;
                        v->mode |= 0x0800;
                    } else if (engine->c140_enabled && (v->mode & 0x10)) {
                        v->pos = (v->pos & 0xFF0000) | v->latched_loop;
                    } else {
                        v->playing = false;
                        continue;
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

                // ★ Namco用: 32バイトミニキャッシュ
                uint32_t page = addr & NAMCO_MCACHE_MASK;
                if (namco_mcache_addr[ch] != page) {
                    namco_mcache_addr[ch] = page;
                    int b = v->current_block;
                    if (b >= 0 && b < engine->namco.block_count && engine->namco.blocks[b].data && page >= engine->namco.blocks[b].start_addr && (page + NAMCO_MCACHE_SIZE) <= (engine->namco.blocks[b].start_addr + engine->namco.blocks[b].size)) {
                        memcpy(namco_mcache_data[ch], &engine->namco.blocks[b].data[page - engine->namco.blocks[b].start_addr], NAMCO_MCACHE_SIZE);
                    } else {
                        for (int i = 0; i < NAMCO_MCACHE_SIZE; i++) {
                            uint32_t ra = page + i;
                            uint8_t byte = 0; 
                            int fb = v->current_block;
                            if (fb >= 0 && fb < engine->namco.block_count && engine->namco.blocks[fb].data && ra >= engine->namco.blocks[fb].start_addr && ra < engine->namco.blocks[fb].start_addr + engine->namco.blocks[fb].size) {
                                byte = engine->namco.blocks[fb].data[ra - engine->namco.blocks[fb].start_addr];
                            } else {
                                for (int j = 0; j < engine->namco.block_count; j++) {
                                    if (engine->namco.blocks[j].data && ra >= engine->namco.blocks[j].start_addr && ra < engine->namco.blocks[j].start_addr + engine->namco.blocks[j].size) {
                                        v->current_block = j;
                                        byte = engine->namco.blocks[j].data[ra - engine->namco.blocks[j].start_addr];
                                        break;
                                    }
                                }
                            }
                            namco_mcache_data[ch][i] = byte;
                        }
                    }
                }
                uint8_t dt = namco_mcache_data[ch][addr & (NAMCO_MCACHE_SIZE - 1)];

                int16_t new_sample = 0;
                if (engine->c352_enabled) {
                    if (v->mode & 0x0004) {
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
                        new_sample = (int16_t)((int8_t)dt) << 4;
                    }
                }
                v->last_sample = new_sample;
            }

            if (!v->playing) continue;

            int32_t dltdt = (v->last_sample - v->prev_sample) / 2;
            int32_t sample = v->prev_sample + ((dltdt * (int32_t)(v->counter & 0xFFFF)) >> 15);

            namco_mix_l += (sample * (int32_t)v->mix_vol_l) >> 8;
            namco_mix_r += (sample * (int32_t)v->mix_vol_r) >> 8;
        }

        int32_t final_namco_l = namco_mix_l * 4;
        int32_t final_namco_r = namco_mix_r * 4;

        if (final_namco_l > 32767) final_namco_l = 32767;
        else if (final_namco_l < -32768) final_namco_l = -32768;

        if (final_namco_r > 32767) final_namco_r = 32767;
        else if (final_namco_r < -32768) final_namco_r = -32768;

        mix_l += final_namco_l >> 1; // ±16384 (matches FM max)
        mix_r += final_namco_r >> 1;
    }

    pcm_engine_opn_tick(engine, &mix_l, &mix_r);

    *out_l += mix_l;
    *out_r += mix_r;
}

static const int32_t ym_vol_table[128] = {
    1024, 939, 862, 790, 725, 665, 610, 559, 513, 471, 432, 396, 363, 333, 306, 280, 257, 236, 216, 199, 182, 167, 153, 141, 129, 118, 108, 99, 91, 84, 77, 70, 65, 59, 54, 50, 46, 42, 38, 35, 32, 30, 27, 25, 23, 21, 19, 18, 16, 15, 14, 13, 11, 11, 10, 9, 8, 7, 7, 6, 6, 5, 5, 4, 4, 4, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// ────────────────────────────────────────────────────────────────────────────
// YM2608 / YM2610 OPN PCM (Rhythm / ADPCM-A / ADPCM-B)
// ────────────────────────────────────────────────────────────────────────────
// OKI ADPCM step table for ADPCM-A (YM2610 rhythm) (49 steps)
static const int opn_oki_step_table[49] = {
    230, 245, 260, 275, 290, 305, 320, 335, 350, 365, 380, 395, 410, 425, 440, 455,
    480, 510, 530, 560, 590, 620, 650, 680, 710, 740, 770, 800, 830, 860, 890, 920,
    960, 1000, 1040, 1080, 1120, 1160, 1200, 1240, 1280, 1320, 1360, 1400, 1440, 1480, 1520, 1560, 1600
};
// OKI ADPCM step index table for ADPCM-A
static const signed char opn_oki_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};
static void opn_adpcma_decode_nibble(int nibble, int32_t *adpcm_val, int32_t *adpcm_step) {
    int step = opn_oki_step_table[*adpcm_step];
    int delta = step >> 3;
    if (nibble & 1) delta += (step >> 2);
    if (nibble & 2) delta += (step >> 1);
    if (nibble & 4) delta += step;
    if (nibble & 8) delta = -delta;
    
    // アキュムレータは12bitスケール (±2048) で計算
    int acc = (*adpcm_val >> 4) + delta;
    if (acc > 2047) acc = 2047;
    if (acc < -2048) acc = -2048;
    // ミキサーに渡すために4倍して16bitスケール (±8192) に戻す (FM音源と音量バランス)
    *adpcm_val = acc * 4;
    
    int next_step = *adpcm_step + opn_oki_index_table[nibble & 0x0F];
    if (next_step < 0) next_step = 0;
    if (next_step > 48) next_step = 48;
    *adpcm_step = next_step;
}

static const int ym_deltat_decode[16] = {
     1,  3,  5,  7,  9, 11, 13, 15,
    -1, -3, -5, -7, -9,-11,-13,-15
};
static const int ym_deltat_step[8] = {
     57,  57,  57,  57,  77, 102, 128, 153
};
static void opn_adpcmb_decode_nibble(int nibble, int32_t *adpcm_val, int32_t *adpcm_step) {
    int step = *adpcm_step;
    int acc = *adpcm_val;
    acc += (ym_deltat_decode[nibble] * step) / 8;
    if (acc > 32767) acc = 32767;
    if (acc < -32768) acc = -32768;
    *adpcm_val = acc;
    step = (step * ym_deltat_step[nibble & 7]) / 64;
    if (step < 127) step = 127;
    if (step > 24576) step = 24576;
    *adpcm_step = step;
}

static uint8_t opn_read_rom(const uint8_t** blocks1, uint32_t* offsets1, uint32_t* sizes1, uint32_t count1, 
                            const uint8_t** blocks2, uint32_t* offsets2, uint32_t* sizes2, uint32_t count2, 
                            uint32_t addr) {
    for (uint32_t i = 0; i < count1; i++) {
        if (addr >= offsets1[i] && addr < offsets1[i] + sizes1[i]) {
            return blocks1[i][addr - offsets1[i]];
        }
    }
    if (blocks2) {
        for (uint32_t i = 0; i < count2; i++) {
            if (addr >= offsets2[i] && addr < offsets2[i] + sizes2[i]) {
                return blocks2[i][addr - offsets2[i]];
            }
        }
    }
    return 0;
}

void pcm_engine_opn_init(PCMSoundEngine *engine, uint32_t clock, uint8_t chip_type) {
    memset(&engine->opn, 0, sizeof(OPN_PCM_Matrix));
    engine->opn.clock = clock;

    static const uint32_t ym2608_rhythm_start[6] = {
        0x0000, 0x01C0, 0x0440, 0x1B80, 0x1D00, 0x1F80
    };
    static const uint32_t ym2608_rhythm_end[6] = {
        0x01BF, 0x043F, 0x1B7F, 0x1CFF, 0x1F7F, 0x1FFF
    };
    for (int i = 0; i < 6; i++) {
        if (chip_type == CHIP_YM2610) {
            engine->opn.ch_a[i].start_addr = 0;
            engine->opn.ch_a[i].end_addr = 512;
        } else {
            engine->opn.ch_a[i].start_addr = ym2608_rhythm_start[i] << 1;
            engine->opn.ch_a[i].end_addr   = (ym2608_rhythm_end[i] + 1) << 1;
        }
        engine->opn.ch_a[i].vol_l = 0x1F;
        engine->opn.ch_a[i].vol_r = 0x1F;
        uint32_t adpcma_div = 432;
        uint32_t native_adpcma = (clock > 0) ? (clock / adpcma_div) : 18500;
        if (engine->sample_rate > 0) {
            engine->opn.ch_a[i].step = (uint32_t)(((uint64_t)native_adpcma << 16) / engine->sample_rate);
        } else {
            engine->opn.ch_a[i].step = (uint32_t)(((uint64_t)native_adpcma << 16) / 22050);
        }
    }
}

void pcm_engine_write_opn_rhythm(PCMSoundEngine *engine, uint8_t reg, uint8_t data) {
    engine->opn.regs[reg] = data;
    if (reg == 0x10) {
        if (data & 0x80) {
             for(int i=0; i<6; i++) {
                 if (data & (1<<i)) {
                     engine->opn.ch_a[i].playing = false;
                 }
             }
        } else {
             for(int i=0; i<6; i++) {
                 if (data & (1<<i)) {
                     engine->opn.ch_a[i].playing = true;
                     engine->opn.ch_a[i].pos_frac = 0;
                     engine->opn.ch_a[i].adpcm_val = 0;
                      engine->opn.ch_a[i].adpcm_step = 0;
                      engine->opn.ch_a[i].pos = engine->opn.ch_a[i].start_addr;
                  }
              }
         }
    } else if (reg == 0x11) {
        engine->opn.adpcma_tl = data & 0x3F;
    } else if (reg >= 0x18 && reg <= 0x1D) {
        int ch = reg - 0x18;
        engine->opn.ch_a[ch].vol_l = data;
        uint32_t native_rate = (engine->opn.clock > 0) ? (engine->opn.clock / 432) : 18500;
        if (engine->sample_rate > 0) {
            engine->opn.ch_a[ch].step = (uint32_t)(((uint64_t)native_rate << 16) / engine->sample_rate);
        }
    }
}

void pcm_engine_write_opn_adpcma(PCMSoundEngine *engine, uint8_t reg, uint8_t data) {
    engine->opn.regs[reg] = data;
    
    if (reg == 0x00) {
        // キーオン / キーオフ制御 (bit 0-5 = Ch 0-5)
        if (data & 0x80) {
            for(int i = 0; i < 6; i++) {
                if (data & (1 << i)) {
                    engine->opn.ch_a[i].playing = false;
                }
            }
        } else {
            for(int i = 0; i < 6; i++) {
                if (data & (1 << i)) {
                    engine->opn.ch_a[i].playing = true;
                    engine->opn.ch_a[i].pos_frac = 0;
                    engine->opn.ch_a[i].adpcm_val = 0;
                     engine->opn.ch_a[i].adpcm_step = 0;
                     engine->opn.ch_a[i].pos = engine->opn.ch_a[i].start_addr;
                }
            }
        }
    } else if (reg == 0x01) {
        // マスターボリューム (TL)
        engine->opn.adpcma_tl = data & 0x3F;
    } else if (reg >= 0x08 && reg <= 0x0D) {
        // チャンネル個別ボリューム・パン (IL) (Ch 0-5)
        int ch = reg - 0x08;
        engine->opn.ch_a[ch].vol_l = data;
    } else if ((reg >= 0x10 && reg <= 0x15) || (reg >= 0x18 && reg <= 0x1D)) {
        // スタートアドレス (L: 0x10-0x15, H: 0x18-0x1D)
        int ch = (reg >= 0x18) ? (reg - 0x18) : (reg - 0x10);
        uint16_t start_reg = (engine->opn.regs[0x18 + ch] << 8) | engine->opn.regs[0x10 + ch];
        // YM2610仕様: レジスタ値はA8〜A23。<< 8 でバイトアドレスに復元し、<< 1 でニブルアドレスにする (計 << 9)
        engine->opn.ch_a[ch].start_addr = start_reg << 9; 
    } else if ((reg >= 0x20 && reg <= 0x25) || (reg >= 0x28 && reg <= 0x2D)) {
        // エンドアドレス (L: 0x20-0x25, H: 0x28-0x2D)
        int ch = (reg >= 0x28) ? (reg - 0x28) : (reg - 0x20);
        uint16_t end_reg = (engine->opn.regs[0x28 + ch] << 8) | engine->opn.regs[0x20 + ch];
        // 終端アドレスは省略された下位8ビットが全て1(0xFF)として扱われる
        engine->opn.ch_a[ch].end_addr = (((end_reg << 8) | 0xFF) << 1) | 1;
    }
}

void pcm_engine_write_opn_adpcmb(PCMSoundEngine *engine, uint8_t reg, uint8_t data) {
    engine->opn.ch_b.regs[reg] = data;
    if (reg == 0x00) {
        if (data & 0x80) {
            engine->opn.ch_b.playing = true;
            engine->opn.ch_b.pos_frac = 0;
            engine->opn.ch_b.adpcm_val = 0;
            engine->opn.ch_b.adpcm_step = 127;
            engine->opn.ch_b.pos = engine->opn.ch_b.start_addr;
        }
        if (data & 0x01) {
            engine->opn.ch_b.playing = false;
        }
    } else if (reg == 0x02 || reg == 0x03) {
        uint16_t start = (engine->opn.ch_b.regs[0x03] << 8) | engine->opn.ch_b.regs[0x02];
        engine->opn.ch_b.start_addr = (start << 8) << 1;
    } else if (reg == 0x04 || reg == 0x05) {
        uint16_t end = (engine->opn.ch_b.regs[0x05] << 8) | engine->opn.ch_b.regs[0x04];
        engine->opn.ch_b.end_addr = ((end << 8) + 0x100) << 1;
    } else if (reg == 0x09 || reg == 0x0A) { 
        uint16_t delta_n = (engine->opn.ch_b.regs[0x0A] << 8) | engine->opn.ch_b.regs[0x09];
        if (engine->sample_rate > 0) {
            engine->opn.ch_b.step = (uint32_t)( ((uint64_t)engine->opn.clock * delta_n) / (144 * engine->sample_rate) );
        } else {
            engine->opn.ch_b.step = (uint32_t)delta_n * 65536 / 144; 
        }
    } else if (reg == 0x0B) { 
        engine->opn.ch_b.vol_l = data;
        engine->opn.ch_b.vol_r = data;
    }
}

void pcm_engine_opn_tick(PCMSoundEngine *engine, int32_t *out_l, int32_t *out_r) {
    int32_t mix_l = 0, mix_r = 0;
    
    if (engine->opn.adpcma_block_count > 0) {
        for (int i = 0; i < OPN_ADPCMA_CHANNELS; i++) {
            OPN_ADPCM_Channel *ch = &engine->opn.ch_a[i];
            if (ch->playing) {
                ch->pos_frac += ch->step;
                uint32_t step_amount = ch->pos_frac >> 16;
                ch->pos_frac &= 0xFFFF;

                for (uint32_t s = 0; s < step_amount; s++) {
                    if (ch->pos >= ch->end_addr) { ch->playing = false; break; }
                    uint8_t byte = opn_read_rom(engine->opn.adpcma_blocks, engine->opn.adpcma_block_offsets, engine->opn.adpcma_block_sizes, engine->opn.adpcma_block_count, 
                                                NULL, NULL, NULL, 0,
                                                ch->pos / 2);
                    uint8_t nibble = (ch->pos & 1) ? (byte & 0x0F) : (byte >> 4);
                    opn_adpcma_decode_nibble(nibble, &ch->adpcm_val, &ch->adpcm_step);
                    ch->pos++;
                }
            }
            
            // 正しい音量制御とミキシング
            if (ch->playing) {
                // TL/ILは値が大きいほど音が大きい(減衰0)という正論理のため、反転(~)が必須。
                uint8_t master_tl = (~engine->opn.adpcma_tl) & 0x3F;
                
                // 倍率を2に下げる (音量を半分程度に調整)
                int32_t out_val = ch->adpcm_val * 2;

                if (ch->vol_l & 0x80) { // 左パン
                    uint8_t il = (~ch->vol_l) & 0x1F;
                    int atten = master_tl + (il << 1); 
                    if (atten > 127) atten = 127;
                    mix_l += (out_val * ym_vol_table[atten]) >> 9;
                }
                if (ch->vol_l & 0x40) { // 右パン
                    uint8_t il = (~ch->vol_l) & 0x1F; 
                    int atten = master_tl + (il << 1);
                    if (atten > 127) atten = 127;
                    mix_r += (out_val * ym_vol_table[atten]) >> 9;
                }
            }
        }
    }

    if (engine->opn.adpcmb_block_count > 0) {
        OPN_DeltaT_Channel *ch = &engine->opn.ch_b;
        if (ch->playing) {
            ch->pos_frac += ch->step;
            uint32_t step_amount = ch->pos_frac >> 16;
            ch->pos_frac &= 0xFFFF;

            for (uint32_t s = 0; s < step_amount; s++) {
                if (ch->pos >= ch->end_addr) { ch->playing = false; break; }
                uint8_t byte = opn_read_rom(engine->opn.adpcmb_blocks, engine->opn.adpcmb_block_offsets, engine->opn.adpcmb_block_sizes, engine->opn.adpcmb_block_count, 
                                            NULL, NULL, NULL, 0,
                                            ch->pos / 2);
                uint8_t nibble = (ch->pos & 1) ? (byte & 0x0F) : (byte >> 4);
                opn_adpcmb_decode_nibble(nibble, &ch->adpcm_val, &ch->adpcm_step);
                ch->pos++;
            }
            
            // ADPCM-B (Delta-T) ボリュームはリニア（0〜255の正論理）
            if (ch->adpcm_val != 0) {
                uint8_t pan = ch->regs[0x01];
                uint8_t vol = ch->vol_l;
                int32_t out = (ch->adpcm_val * vol) >> 8;
                if (pan & 0x80) mix_l += out;
                if (pan & 0x40) mix_r += out;
            }
        }
    }
    
    *out_l += mix_l;
    *out_r += mix_r;
}

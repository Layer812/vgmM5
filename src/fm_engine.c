#include "fm_engine.h"
#include <string.h>
#include <math.h>

FMSoundEngine g_fm_engine;

#define SIN_BITS 10
#define SIN_LEN  (1 << SIN_BITS) // 1024
#define SIN_MASK (SIN_LEN - 1)   // 1023

static uint16_t SIN_TAB[SIN_LEN];
static uint16_t EXP_TAB[256];

static uint32_t RATE_TABLE[32]; 
static bool table_initialized = false;
static int8_t  LFO_PM_TAB[4][256];
static uint8_t LFO_AM_TAB[4][256];


static uint8_t DT_TAB[4][32] = {
    {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},
    {0,0,0,0, 1,1,1,1, 1,1,2,2, 2,2,2,3, 3,3,4,4, 4,5,5,6, 6,7,8,8, 8,8,8,8},
    {1,1,1,1, 2,2,2,2, 2,3,3,3, 4,4,4,5, 5,6,6,7, 8,8,9,10, 11,12,13,14, 16,16,16,16},
    {2,2,2,2, 2,3,3,3, 4,4,4,5, 5,6,6,7, 8,8,9,10, 11,12,13,14, 16,17,19,20, 22,22,22,22}
};

#define EG_FRACTION_BITS 16
#define EG_MAX (4096 << EG_FRACTION_BITS)

static void update_algorithm_routing(FMSoundEngine *engine, int ch, uint8_t algo);

static void init_tables() {
    if (table_initialized) return;
    for (int i = 0; i < SIN_LEN; i++) {
        float sin_val = sinf(((float)i + 0.5f) * (float)M_PI / 2048.0f); 
        SIN_TAB[i] = (uint16_t)(-256.0f * log2f(sin_val));
    }
    for (int i = 0; i < 256; i++) {
        EXP_TAB[i] = (uint16_t)(16384.0f * powf(2.0f, -(float)i / 256.0f));
    }
    for (int i = 0; i < 32; i++) {
        if (i == 0) RATE_TABLE[i] = 0;
        else RATE_TABLE[i] = (uint32_t)(2.0f * powf(4096.0f, (float)(i - 1) / 29.0f)) << 6;
    }
    for (int p = 0; p < 256; p++) {
        LFO_PM_TAB[0][p] = p - 128;
        LFO_AM_TAB[0][p] = 255 - p;
        LFO_PM_TAB[1][p] = (p < 128) ? 127 : -128;
        LFO_AM_TAB[1][p] = (p < 128) ? 255 : 0;
        if (p < 64) LFO_PM_TAB[2][p] = p * 2;
        else if (p < 192) LFO_PM_TAB[2][p] = 128 - (p - 64) * 2;
        else LFO_PM_TAB[2][p] = -128 + (p - 192) * 2;
        LFO_AM_TAB[2][p] = (p < 128) ? (255 - p * 2) : ((p - 128) * 2);
        LFO_PM_TAB[3][p] = (int32_t)((((uint32_t)p * 1103515245U + 12345U) >> 16) & 255) - 128;
        LFO_AM_TAB[3][p] = (((uint32_t)p * 1103515245U + 12345U) >> 16) & 255;
    }
    table_initialized = true;
}

static void update_phase_step(FMSoundEngine *engine, int ch) {
    int kc = engine->kc[ch];
    int octave = (kc >> 4) & 7;
    int note = kc & 0x0F;
    // YM2151 KC to Semitone mapping:
    // 0:C#, 1:D, 2:D#, 4:E, 5:F, 6:F#, 8:G, 9:G#, A:A, C:A#, D:B, E:C
    static int note_map[16] = {
        1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12
    };
    
    int real_note = note_map[note];
    int kf = engine->kf[ch] >> 2;
    uint32_t base_step = engine->ym2151_freq_tab[real_note][kf] << octave;

    for(int op = 0; op < 4; op++) {
        int idx = ch * 4 + op;
        int mul_val = engine->ops[idx].mul & 0x0F;
        uint64_t step_64 = (mul_val == 0) ? (base_step >> 1) : ((uint64_t)base_step * mul_val);
        
        // True hardware wraps around its internal sample rate
        if (engine->fchip_step > 0) {
            step_64 %= engine->fchip_step;
        }
        
        uint32_t step;
        // If the resulting frequency is ultrasonic in our 44.1kHz engine, it would alias 
        // down to an audible piercing "piiin". Mute it instead, exactly as it would be 
        // silent/ultrasonic to a human on the real hardware.
        if (step_64 >= 0x80000000ULL) { // Nyquist (half of 4294967296)
            step = 0; // Mute the ultrasonic frequency
        } else {
            step = (uint32_t)step_64;
        }
        
        int dt1 = (engine->ops[idx].mul >> 4) & 7;
        uint8_t dt_val = dt1 & 3;
        uint8_t dt_mag = DT_TAB[dt_val][kc >> 2];
        int32_t dt_add = (dt1 & 4) ? -dt_mag : dt_mag;
        int32_t dt_step = dt_add * 5194; 

        int dt2 = engine->ops[idx].dt2;
        if (dt2 == 1) step = (uint32_t)(((uint64_t)step * 92668) >> 16);
        else if (dt2 == 2) step = (uint32_t)(((uint64_t)step * 102891) >> 16);
        else if (dt2 == 3) step = (uint32_t)(((uint64_t)step * 113508) >> 16);
        
        if (dt_step < 0 && step < (uint32_t)(-dt_step)) {
            step = 0;
        } else {
            step += dt_step;
        }
        engine->ops[idx].phase_step = step;
    }
}

static void update_eg_rates(FMSoundEngine *engine, int ch) {
    for (int op = 0; op < 4; op++) {
        int idx = ch * 4 + op;
        int ks_val = engine->ops[idx].ks;
        
        int ks_offset;
        if (engine->chip_type == CHIP_YM2151) {
            ks_offset = engine->kc[ch] >> (5 - ks_val);
        } else {
            int kc = engine->kc[ch] >> 2; 
            ks_offset = (ks_val == 0) ? 0 : (kc >> (3 - ks_val));
        }
        
        int r_ar = engine->ops[idx].ar == 0 ? 0 : engine->ops[idx].ar + ks_offset;
        if (r_ar > 31) r_ar = 31;
        engine->ops[idx].ar_step = RATE_TABLE[r_ar] << 3;
        if (engine->ops[idx].ar_step == 0 && r_ar > 0) engine->ops[idx].ar_step = 1;

        int r_dr = engine->ops[idx].dr == 0 ? 0 : engine->ops[idx].dr + ks_offset;
        if (r_dr > 31) r_dr = 31;
        engine->ops[idx].dr_step = RATE_TABLE[r_dr] >> 1;
        if (engine->ops[idx].dr_step == 0 && r_dr > 0) engine->ops[idx].dr_step = 1;

        int r_d2r = engine->ops[idx].d2r == 0 ? 0 : engine->ops[idx].d2r + ks_offset;
        if (r_d2r > 31) r_d2r = 31;
        engine->ops[idx].d2r_step = RATE_TABLE[r_d2r] >> 1;
        if (engine->ops[idx].d2r_step == 0 && r_d2r > 0) engine->ops[idx].d2r_step = 1;

        int r_rr = engine->ops[idx].rr == 0 ? 0 : engine->ops[idx].rr + ks_offset;
        if (r_rr > 31) r_rr = 31;
        engine->ops[idx].rr_step = RATE_TABLE[r_rr] >> 1;
        if (engine->ops[idx].rr_step == 0 && r_rr > 0) engine->ops[idx].rr_step = 1;
    }
}

static void update_phase_step_ym2612(FMSoundEngine *engine, int ch) {
    static uint8_t fn_note_table[16] = {0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 3, 3, 3, 3, 3};
    
    for(int op = 0; op < 4; op++) {
        uint32_t op_fn = engine->f_number[ch];
        uint32_t op_blk = engine->block[ch];
        
        // CH3 Special Mode handling
        if (ch == 2 && engine->kc[8] != 0) {
            if (op == 0) { op_fn = engine->ch3_f_number[2]; op_blk = engine->ch3_block[2]; }      // Op1
            else if (op == 1) { op_fn = engine->ch3_f_number[0]; op_blk = engine->ch3_block[0]; } // Op2
            else if (op == 2) { op_fn = engine->ch3_f_number[1]; op_blk = engine->ch3_block[1]; } // Op3
            // Op4 uses normal CH3 freq
        }
        
        uint32_t base_step;
        if (engine->chip_type == CHIP_YM2612) {
            base_step = engine->ym2612_fnum_tab[op_fn] << op_blk;
        } else {
            // YM2203 has different prescaler (72.0f instead of 144.0f) so the base_step is exactly twice as fast
            base_step = (engine->ym2612_fnum_tab[op_fn] << op_blk) * 2; 
        }
        
        int note = fn_note_table[(op_fn >> 7) & 0x0F];
        int kc = (op_blk << 2) | note;
        if (kc > 31) kc = 31;
        
        int idx = ch * 4 + op;
        int mul_val = engine->ops[idx].mul & 0x0F;
        uint64_t step_64 = (mul_val == 0) ? (base_step >> 1) : ((uint64_t)base_step * mul_val);
        
        // True hardware wraps around its internal sample rate
        if (engine->fchip_step > 0) {
            step_64 %= engine->fchip_step;
        }
        
        uint32_t step;
        if (step_64 >= 0x80000000ULL) { // Nyquist
            step = 0; // Mute the ultrasonic frequency
        } else {
            step = (uint32_t)step_64;
        }
        
        int dt1 = (engine->ops[idx].mul >> 4) & 7;
        uint8_t dt_val = dt1 & 3;
        uint8_t dt_mag = DT_TAB[dt_val][kc];
        int32_t dt_add = (dt1 & 4) ? -dt_mag : dt_mag;
        int32_t dt_step = dt_add * 4946; 

        if (dt_step < 0 && step < (uint32_t)(-dt_step)) {
            step = 0;
        } else {
            step += dt_step;
        }
        engine->ops[idx].phase_step = step;
    }
}

static void update_eg_rates_ym2612(FMSoundEngine *engine, int ch) {
    uint32_t fn = engine->f_number[ch];
    uint32_t blk = engine->block[ch];
    static uint8_t fn_note_table[16] = {0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 3, 3, 3, 3, 3};
    int note = fn_note_table[(fn >> 7) & 0x0F];
    int kc = (blk << 2) | note;
    if (kc > 31) kc = 31;
    
    for (int op = 0; op < 4; op++) {
        int idx = ch * 4 + op;
        int ks_val = engine->ops[idx].ks;
        int ks_offset = (ks_val == 0) ? 0 : (kc >> (3 - ks_val));
        
        int r_ar = engine->ops[idx].ar == 0 ? 0 : engine->ops[idx].ar + ks_offset;
        if (r_ar > 31) r_ar = 31;
        engine->ops[idx].ar_step = RATE_TABLE[r_ar] << 3;
        if (engine->ops[idx].ar_step == 0 && r_ar > 0) engine->ops[idx].ar_step = 1;

        int r_dr = engine->ops[idx].dr == 0 ? 0 : engine->ops[idx].dr + ks_offset;
        if (r_dr > 31) r_dr = 31;
        engine->ops[idx].dr_step = RATE_TABLE[r_dr] >> 1;
        if (engine->ops[idx].dr_step == 0 && r_dr > 0) engine->ops[idx].dr_step = 1;

        int r_d2r = engine->ops[idx].d2r == 0 ? 0 : engine->ops[idx].d2r + ks_offset;
        if (r_d2r > 31) r_d2r = 31;
        engine->ops[idx].d2r_step = RATE_TABLE[r_d2r] >> 1;
        if (engine->ops[idx].d2r_step == 0 && r_d2r > 0) engine->ops[idx].d2r_step = 1;

        int r_rr = engine->ops[idx].rr == 0 ? 0 : engine->ops[idx].rr + ks_offset;
        if (r_rr > 31) r_rr = 31;
        engine->ops[idx].rr_step = RATE_TABLE[r_rr] >> 1;
        if (engine->ops[idx].rr_step == 0 && r_rr > 0) engine->ops[idx].rr_step = 1;
    }
}
// ── ☁EPL2用 フェーズE周波数E計算E修正牁E──
static void update_phase_step_opl(FMSoundEngine *engine, int ch) {
    uint32_t fn = engine->f_number[ch];
    uint32_t blk = engine->block[ch];
    
    // 正しいOPL2周波数: Freq = (F-Num * 2^Block * Clock) / (72 * 2^20)
    float base_step = (float)(fn * (1 << blk)) * engine->opl2_step_factor * engine->phase_step_factor;
    
    static float OPL2_MUL_TABLE[16] = {
        0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 10.0f, 12.0f, 12.0f, 15.0f, 15.0f
    };
    
    for(int op = 0; op < 2; op++) {
        int idx = ch * 4 + op;
        int mul_val = engine->ops[idx].mul & 0x0F;
        float m = OPL2_MUL_TABLE[mul_val];
        
        uint64_t step_64 = (uint64_t)(base_step * m);
        if (engine->fchip_step > 0) {
            step_64 %= engine->fchip_step;
        }
        
        uint32_t step;
        if (step_64 >= 0x80000000ULL) { // Nyquist
            step = 0; // Mute the ultrasonic frequency
        } else {
            step = (uint32_t)step_64;
        }
        engine->ops[idx].phase_step = step;
    }
}
// ── ☁EPL2用 エンベロープ計箁E(パEカチEブモード対忁E ──
static void update_eg_rates_opl(FMSoundEngine *engine, int ch) {
    uint32_t fn = engine->f_number[ch];
    uint32_t blk = engine->block[ch];
    int ksn = (blk << 1) | ((fn >> 9) & 1); 
    
    for (int op = 0; op < 2; op++) {
        int idx = ch * 4 + op;
        int ks_val = engine->ops[idx].ks;
        int ks_offset = (ks_val == 0) ? (ksn >> 2) : ksn;
        
        int r_ar = engine->ops[idx].ar == 0 ? 0 : engine->ops[idx].ar + ks_offset;
        if (r_ar > 31) r_ar = 31;
        engine->ops[idx].ar_step = RATE_TABLE[r_ar] << 3;
        if (engine->ops[idx].ar_step == 0 && r_ar > 0) engine->ops[idx].ar_step = 1;

        int r_dr = engine->ops[idx].dr == 0 ? 0 : engine->ops[idx].dr + ks_offset;
        if (r_dr > 31) r_dr = 31;
        engine->ops[idx].dr_step = RATE_TABLE[r_dr] >> 1;
        if (engine->ops[idx].dr_step == 0 && r_dr > 0) engine->ops[idx].dr_step = 1;

        int r_rr = engine->ops[idx].rr == 0 ? 0 : engine->ops[idx].rr + ks_offset;
        if (r_rr > 31) r_rr = 31;
        engine->ops[idx].rr_step = RATE_TABLE[r_rr] >> 1;
        if (engine->ops[idx].rr_step == 0 && r_rr > 0) engine->ops[idx].rr_step = 1;
        
        // ☁EKSL (Key Scale Level) の計算
        int ksl = engine->ops[idx].ksl;
        if (ksl == 0) {
            engine->ops[idx].ksl_atten_val = 0;
        } else {
            // OPL2のKSL近似計箁E ksn(0-15) に対して、KSL=1(3.0dB), KSL=2(1.5dB), KSL=3(6.0dB) を適用、E            // 1 TLスチEチE0.75dB) = 32冁E単佁Eなので、E.5dB=64, 3.0dB=128, 6.0dB=256
            // ksn1つにつぁE.5オクターブ、Eオクターブあたりの減衰を計算
            int base_ksn = (ksn > 4) ? (ksn - 4) : 0; // 低音域は減衰しない近似
            int atten = 0;
            switch(ksl) {
                case 1: atten = base_ksn * 64; break;  // 3.0dB / oct (1.5dB / ksn)
                case 2: atten = base_ksn * 32; break;  // 1.5dB / oct (0.75dB / ksn)
                case 3: atten = base_ksn * 128; break; // 6.0dB / oct (3.0dB / ksn)
            }
            engine->ops[idx].ksl_atten_val = atten;
        }
        
        // ☁E修正: EG_TYP (0: パEカチEチE 1: サスチEン) の適用
        int eg_typ = engine->ops[idx].dt2;
        if (eg_typ == 0) {
            engine->ops[idx].d2r_step = engine->ops[idx].rr_step; 
        } else {
            engine->ops[idx].d2r_step = 0; 
        }
    }
}


static IRAM_ATTR void update_envelopes(FMSoundEngine *engine, int active_channels) {
    int ops_per_ch = (engine->chip_type == CHIP_YM3812) ? 2 : 4;
    for (int ch = 0; ch < active_channels; ch++) {
        int b = ch * 4;
        for (int i = 0; i < ops_per_ch; i++) {
            int op_idx = b + i;
            if (engine->ops[op_idx].env_state == EG_OFF) continue;
            switch (engine->ops[op_idx].env_state) {
                case EG_ATTACK:
                    engine->ops[op_idx].env_level -= engine->ops[op_idx].ar_step;
                    if (engine->ops[op_idx].env_level <= 0) {
                        engine->ops[op_idx].env_level = 0; 
                        engine->ops[op_idx].env_state = EG_DECAY;
                    }
                    break;
                case EG_DECAY:
                    engine->ops[op_idx].env_level += engine->ops[op_idx].dr_step;
                    if (engine->ops[op_idx].env_level >= engine->ops[op_idx].sl_level) {
                        engine->ops[op_idx].env_level = engine->ops[op_idx].sl_level;
                        engine->ops[op_idx].env_state = EG_SUSTAIN;
                    }
                    break;
                case EG_SUSTAIN:
                    engine->ops[op_idx].env_level += engine->ops[op_idx].d2r_step;
                    if (engine->ops[op_idx].env_level >= EG_MAX) engine->ops[op_idx].env_level = EG_MAX;
                    break;
                case EG_RELEASE:
                    engine->ops[op_idx].env_level += engine->ops[op_idx].rr_step;
                    if (engine->ops[op_idx].env_level >= EG_MAX) { 
                        engine->ops[op_idx].env_level = EG_MAX;
                        engine->ops[op_idx].env_state = EG_OFF;
                    }
                    break;
            }
        }
    }
}

static IRAM_ATTR __attribute__((always_inline)) inline int32_t calc_op_internal(FMSoundEngine *engine, int op_idx, int32_t modulation, int32_t pm_amount, int32_t am_offset, int is_noise, int is_opl2) {
    if (engine->ops[op_idx].env_state == EG_OFF) return 0;
    
    uint32_t active_phase_step = engine->ops[op_idx].phase_step;
    if (pm_amount != 0) {
        active_phase_step += (((int32_t)(active_phase_step >> 5) * pm_amount) >> 8);
    }
    engine->ops[op_idx].phase += active_phase_step;

    int32_t current_atten = (engine->ops[op_idx].env_level >> EG_FRACTION_BITS) + engine->ops[op_idx].tl_atten;
    current_atten += (am_offset << 1) * engine->ops[op_idx].am_enable; 
    if (current_atten >= 8192) return 0;

    int32_t mod_idx = modulation >> 7; 
    uint32_t phase_12bit = engine->ops[op_idx].phase >> 20;
    uint32_t idx = (phase_12bit + mod_idx) & 0xFFF;
    
    int32_t out;
    if (is_noise) {
        int32_t noise_val;
        if (is_noise == 2) {
            uint32_t metallic = ((phase_12bit * 17) ^ (engine->noise_rng)) & 0x7FFF;
            noise_val = (int32_t)metallic - 16384;
            noise_val >>= 1; 
        } else {
            noise_val = (int32_t)(engine->noise_rng & 0x7FFF) - 16384; 
            noise_val >>= 1; 
        }
        noise_val = (noise_val * 3) >> 2; 
        uint32_t exp_shift = current_atten >> 8;
        uint32_t exp_idx = current_atten & 0xFF;
        int32_t env_linear = EXP_TAB[exp_idx] >> exp_shift;
        out = (noise_val * env_linear) >> 14;
    } else {
        uint32_t sin_idx = idx & 0x3FF;          
        if (idx & 0x400) sin_idx = 1023 - sin_idx; 
        
        int is_negative = (idx & 0x800); 
        int output_enable = 1;
        
        if (is_opl2) {
            uint8_t ws = engine->ops[op_idx].wave_sel;
            if (ws == 1) {       
                if (is_negative) output_enable = 0;
            } else if (ws == 2) { 
                is_negative = 0;
            } else if (ws == 3) { 
                if (idx & 0x400) output_enable = 0;
                is_negative = 0;
            }
        }
        
        if (!output_enable) {
            out = 0;
        } else {
            int32_t total_atten = current_atten + engine->ops[op_idx].ksl_atten_val + SIN_TAB[sin_idx];
            if (total_atten < 0) total_atten = 0;
            if (total_atten >= 8192) {
                out = 0;
            } else {
                uint32_t exp_shift = total_atten >> 8;
                uint32_t exp_idx = total_atten & 0xFF;
                out = EXP_TAB[exp_idx] >> exp_shift;
                if (is_negative) out = -out;
            }
        }
    }
    return out;
}

static uint8_t YM2612_OP_MAP[4] = { 0, 2, 1, 3 }; // Op1(0), Op3(2), Op2(1), Op4(3)
void fm_engine_init(FMSoundEngine *engine, uint32_t sample_rate, uint32_t clock, uint8_t chip_type) {
    memset(engine, 0, sizeof(FMSoundEngine));
    engine->sample_rate = sample_rate;
    engine->clock = clock;
    engine->chip_type = chip_type;
    engine->noise_rng = 1;
    engine->lfo_step = (uint32_t)((5.0 * 4294967296.0) / sample_rate); 
    engine->opl2_lfo_am_step = (uint32_t)((3.7f * 4294967296.0f) / (float)sample_rate);
    engine->opl2_lfo_pm_step = (uint32_t)((6.1f * 4294967296.0f) / (float)sample_rate);
    
    // ☁E事前計算ファクタ (double撲滁E& 高速化)
    engine->phase_step_factor = 4294967296.0f / (float)sample_rate;
    float prescaler = (chip_type == CHIP_YM2203 || chip_type == CHIP_YM3812) ? 72.0f : ((chip_type == CHIP_YM2151) ? 64.0f : 144.0f);
    engine->ym2612_step_factor = ((float)clock) / (prescaler * 1048576.0f);
    engine->opl2_step_factor = ((float)clock) / (72.0f * 1048576.0f);
    
    engine->fchip_step = ((uint64_t)clock * 4294967296ULL) / (uint64_t)(prescaler * engine->sample_rate);
    
    int active_channels = (engine->chip_type == CHIP_YM2612) ? 6 : ((engine->chip_type == CHIP_YM2151) ? 8 : ((engine->chip_type == CHIP_YM2203) ? 3 : 9));
          for (int ch = 0; ch < active_channels; ch++) {
          engine->pan_l[ch] = 1;
          engine->pan_r[ch] = 1;
          update_algorithm_routing(engine, ch, 0);
      }
    
    // YM2151 ピッチテーブルの事前計算
    for (int n = 0; n <= 12; n++) {
        for (int k = 0; k < 64; k++) {
            float semi = (float)n + (k / 64.0f);
            float base_freq = 16.3516f * ((float)clock / 3579545.0f) * powf(1.059463094f, semi);
            engine->ym2151_freq_tab[n][k] = (uint32_t)(base_freq * engine->phase_step_factor);
        }
    }
    
    // ☁EYM2612 F-Number 事前計算テーブル (float演算E排除)
    float factor = ((float)clock * 4096.0f) / (144.0f * (float)sample_rate);
    for (int i = 0; i < 2048; i++) {
        engine->ym2612_fnum_tab[i] = (uint32_t)((float)i * factor);
    }
    
    
    for (int i = 0; i < TOTAL_OPS; i++) {
        engine->ops[i].env_state = EG_OFF;
        engine->ops[i].env_level = EG_MAX;
    }
    init_tables();
}

static uint8_t YM2151_OP_MAP[4] = { 0, 1, 2, 3 }; // M1(0), M2(1), C1(2), C2(3)
void fm_engine_register_write(FMSoundEngine *engine, uint16_t addr, uint8_t data) {
    if (addr == 0x08) { 
        int ch = data & 7;
        if ((data >> 3) & 1) { if(engine->ops[ch*4+0].env_state == EG_OFF) engine->ops[ch*4+0].env_level = EG_MAX; engine->ops[ch*4+0].env_state = EG_ATTACK; engine->ops[ch*4+0].phase = 0; } else { if(engine->ops[ch*4+0].env_state != EG_OFF) engine->ops[ch*4+0].env_state = EG_RELEASE; }
        if ((data >> 4) & 1) { if(engine->ops[ch*4+1].env_state == EG_OFF) engine->ops[ch*4+1].env_level = EG_MAX; engine->ops[ch*4+1].env_state = EG_ATTACK; engine->ops[ch*4+1].phase = 0; } else { if(engine->ops[ch*4+1].env_state != EG_OFF) engine->ops[ch*4+1].env_state = EG_RELEASE; }
        if ((data >> 5) & 1) { if(engine->ops[ch*4+2].env_state == EG_OFF) engine->ops[ch*4+2].env_level = EG_MAX; engine->ops[ch*4+2].env_state = EG_ATTACK; engine->ops[ch*4+2].phase = 0; } else { if(engine->ops[ch*4+2].env_state != EG_OFF) engine->ops[ch*4+2].env_state = EG_RELEASE; }
        if ((data >> 6) & 1) { if(engine->ops[ch*4+3].env_state == EG_OFF) engine->ops[ch*4+3].env_level = EG_MAX; engine->ops[ch*4+3].env_state = EG_ATTACK; engine->ops[ch*4+3].phase = 0; } else { if(engine->ops[ch*4+3].env_state != EG_OFF) engine->ops[ch*4+3].env_state = EG_RELEASE; }
        return;
    }
    if (addr == 0x0F) {
        engine->noise_enable = (data >> 7) & 1;
        uint8_t n_freq_reg = data & 0x1F;
        if (n_freq_reg == 31) n_freq_reg = 30; // 30 and 31 are identical on hardware
        float n_freq = (float)engine->clock / (32.0f * (32.0f - (float)n_freq_reg));
        float f_step = n_freq * engine->phase_step_factor;
        f_step = fmodf(f_step, 4294967296.0f);
        engine->noise_step = (uint32_t)f_step;
        return;
    }
    if (addr == 0x18) { float lfo_freq = (float)(data + 1) * 0.15f; engine->lfo_step = (uint32_t)(lfo_freq * engine->phase_step_factor); return; }
    if (addr == 0x19) { if (data & 0x80) engine->amd = data & 0x7F; else engine->pmd = data & 0x7F; return; }
    if (addr == 0x1B) { engine->lfo_wave = data & 3; return; }
    if (addr >= 0x20 && addr <= 0x27) { int ch = addr & 7; engine->pan_l[ch] = (data >> 7) & 1; engine->pan_r[ch] = (data >> 6) & 1; engine->fb_shift[ch] = (data >> 3) & 7; engine->algo[ch] = data & 7; update_algorithm_routing(engine, ch, engine->algo[ch]); return; }
    if (addr >= 0x28 && addr <= 0x2F) { int ch = addr & 7; engine->kc[ch] = data; update_phase_step(engine, ch); update_eg_rates(engine, ch); return; }
    if (addr >= 0x30 && addr <= 0x37) { int ch = addr & 7; engine->kf[ch] = data; update_phase_step(engine, ch); return; }
    if (addr >= 0x38 && addr <= 0x3F) { int ch = addr & 7; engine->pms[ch] = (data >> 4) & 7; engine->ams[ch] = data & 3; return; }
    if (addr >= 0x40) {
        int ch = addr & 7;
        int op_type = (addr >> 3) & 3; 
        int idx = ch * 4 + YM2151_OP_MAP[op_type]; 
        switch (addr & 0xE0) {
            case 0x40: engine->ops[idx].mul = data & 0x0F; update_phase_step(engine, ch); break;
            case 0x60: engine->ops[idx].tl_atten = (data & 0x7F) * 32; break;
            case 0x80: engine->ops[idx].ks = data >> 6; engine->ops[idx].ar = data & 0x1F; update_eg_rates(engine, ch); break;
            case 0xA0: engine->ops[idx].am_enable = (data >> 7) & 1; engine->ops[idx].dr = data & 0x1F; update_eg_rates(engine, ch); break;
            case 0xC0: engine->ops[idx].dt2 = (data >> 6) & 3; engine->ops[idx].d2r = data & 0x1F; update_phase_step(engine, ch); update_eg_rates(engine, ch); break;
            case 0xE0: { int rr = data & 0x0F; engine->ops[idx].rr = (rr << 1) + 1; int d1l = (data >> 4) & 0x0F; engine->ops[idx].sl_level = (d1l == 15) ? EG_MAX : ((d1l * 4) * 32 << EG_FRACTION_BITS); update_eg_rates(engine, ch); } break;
        }
    }
}


void fm_engine_write_ym2612(FMSoundEngine *engine, uint8_t port, uint8_t addr, uint8_t data) {
    if (port == 0 && addr == 0x28) {
        int ch = data & 3; 
        if (ch == 3) return;
        if (data & 4) ch += 3;
        
        int b = ch * 4;
        uint8_t m1 = (data >> 4) & 1; // Slot 1
        uint8_t c1 = (data >> 5) & 1; // Slot 2
        uint8_t m2 = (data >> 6) & 1; // Slot 3
        uint8_t c2 = (data >> 7) & 1; // Slot 4
        
        uint8_t key_on[4] = { m1, c1, m2, c2 }; // internal 0=Slot 1, 1=Slot 2, 2=Slot 3, 3=Slot 4
        for (int op = 0; op < 4; op++) {
            int idx = b + op;
            if (key_on[op]) {
                if (engine->ops[idx].env_state == EG_OFF) engine->ops[idx].env_level = EG_MAX;
                engine->ops[idx].env_state = EG_ATTACK;
                engine->ops[idx].phase = 0;
            } else {
                if (engine->ops[idx].env_state != EG_OFF) engine->ops[idx].env_state = EG_RELEASE;
            }
        }
        return;
    }
    
    int ch = addr & 3;
    if (ch == 3) return;
    if (port == 1) ch += 3;
    
    if (addr == 0x22) {
        if (data & 8) {
            static float lfo_freqs[8] = { 3.98f, 5.56f, 6.02f, 6.37f, 6.88f, 9.63f, 48.1f, 72.2f };
            float freq = lfo_freqs[data & 7];
            engine->lfo_step = (uint32_t)(freq * engine->phase_step_factor);
        } else {
            engine->lfo_step = 0;
        }
        return;
    }
    if (addr == 0x27) {
        engine->kc[8] = data >> 6; // CH3 mode stored in kc[8]
        return;
    }
    
    if (addr >= 0xA0 && addr <= 0xA2) {
        engine->f_number[ch] = (engine->f_number[ch] & 0x0700) | data;
        update_phase_step_ym2612(engine, ch);
    } else if (addr >= 0xA4 && addr <= 0xA6) {
        ch = (addr - 0xA4); if (port == 1) ch += 3;
        engine->f_number[ch] = (engine->f_number[ch] & 0x00FF) | ((data & 7) << 8);
        engine->block[ch] = (data >> 3) & 7;
        update_phase_step_ym2612(engine, ch);
    } else if (addr >= 0xA8 && addr <= 0xAA && port == 0) {
        int idx = addr - 0xA8; // 0=Op2, 1=Op3, 2=Op1 (Op4 is normal CH3)
        engine->ch3_f_number[idx] = (engine->ch3_f_number[idx] & 0x0700) | data;
        update_phase_step_ym2612(engine, 2); // CH3 is 2
    } else if (addr >= 0xAC && addr <= 0xAE && port == 0) {
        int idx = addr - 0xAC; // 0=Op2, 1=Op3, 2=Op1
        engine->ch3_f_number[idx] = (engine->ch3_f_number[idx] & 0x00FF) | ((data & 7) << 8);
        engine->ch3_block[idx] = (data >> 3) & 7;
        update_phase_step_ym2612(engine, 2); // CH3 is 2
    } else if (addr >= 0xB0 && addr <= 0xB2) {
        ch = (addr - 0xB0); if (port == 1) ch += 3;
        engine->fb_shift[ch] = (data >> 3) & 7;
        engine->algo[ch] = data & 7;
        update_algorithm_routing(engine, ch, engine->algo[ch]);
    } else if (addr >= 0xB4 && addr <= 0xB6) {
        ch = (addr - 0xB4); if (port == 1) ch += 3;
        engine->pan_l[ch] = (data >> 7) & 1;
        engine->pan_r[ch] = (data >> 6) & 1;
        engine->ams[ch] = (data >> 4) & 3;
        engine->pms[ch] = data & 7;
    } else if (addr >= 0x30 && addr <= 0x9F) {
        int op_type = (addr >> 2) & 3; 
        int idx = ch * 4 + YM2612_OP_MAP[op_type]; 
        switch (addr & 0xF0) {
            case 0x30: engine->ops[idx].mul = data & 0x0F; engine->ops[idx].mul |= (data & 0x70); update_phase_step_ym2612(engine, ch); break;
            case 0x40: engine->ops[idx].tl_atten = (data & 0x7F) * 32; break;
            case 0x50: engine->ops[idx].ar = data & 0x1F; engine->ops[idx].ks = data >> 6; update_eg_rates_ym2612(engine, ch); break;
            case 0x60: engine->ops[idx].dr = data & 0x1F; engine->ops[idx].am_enable = (data >> 7) & 1; update_eg_rates_ym2612(engine, ch); break;
            case 0x70: engine->ops[idx].d2r = data & 0x1F; update_eg_rates_ym2612(engine, ch); break;
            case 0x80: { int d1l = (data >> 4) & 0x0F; engine->ops[idx].sl_level = (d1l == 15) ? EG_MAX : ((d1l * 4) * 32 << EG_FRACTION_BITS); engine->ops[idx].rr = data & 0x0F; engine->ops[idx].rr = engine->ops[idx].rr ? engine->ops[idx].rr*2+1 : 0; update_eg_rates_ym2612(engine, ch); } break;
            case 0x90: break; // SSG-EG (unsupported)
        }
    }
}
// ── ☁EOPL2 レジスタ書き込み (無限アタチE防止) ──
void fm_engine_write_opl(FMSoundEngine *engine, uint8_t addr, uint8_t data) {
    if (addr == 0x01) return; // 波形選択ON/OFFE常にONとして扱ぁEE
    // チャンネルレジスタ (0xA0-0xC8)
    if ((addr >= 0xA0 && addr <= 0xA8) || (addr >= 0xB0 && addr <= 0xB8) || (addr >= 0xC0 && addr <= 0xC8)) {
        int ch;
        if (addr >= 0xA0 && addr <= 0xA8) {
            ch = addr - 0xA0;
            engine->f_number[ch] = (engine->f_number[ch] & 0x0300) | data;
            update_phase_step_opl(engine, ch);
            update_eg_rates_opl(engine, ch); // ☁E修正: ピッチ変化でエンベロープ速度も更新
        } else if (addr >= 0xB0 && addr <= 0xB8) {
            ch = addr - 0xB0;
            engine->f_number[ch] = (engine->f_number[ch] & 0x00FF) | ((data & 3) << 8);
            engine->block[ch] = (data >> 2) & 7;
            
            // ☁E修正: 以前EKey-On状態を記Eし、変化した瞬間だけ処理する
            uint8_t prev_key_on = engine->kc[ch] & 0x20;
            engine->kc[ch] = data; // 生Eレジスタ値を保孁E            
            if ((data & 0x20) && !prev_key_on) { 
                // OFF -> ON の瞬間EみアタチE開姁E                engine->ops[ch*4+0].env_level = EG_MAX; engine->ops[ch*4+0].phase = 0;
                engine->ops[ch*4+1].env_level = EG_MAX; engine->ops[ch*4+1].phase = 0;
                engine->ops[ch*4+0].env_state = EG_ATTACK;
                engine->ops[ch*4+1].env_state = EG_ATTACK;
            } else if (!(data & 0x20) && prev_key_on) { 
                // ON -> OFF の瞬間Eみリリース開姁E                if(engine->ops[ch*4+0].env_state != EG_OFF) engine->ops[ch*4+0].env_state = EG_RELEASE;
                if(engine->ops[ch*4+1].env_state != EG_OFF) engine->ops[ch*4+1].env_state = EG_RELEASE;
            }
            update_phase_step_opl(engine, ch);
            update_eg_rates_opl(engine, ch);
        } else if (addr >= 0xC0 && addr <= 0xC8) {
            ch = addr - 0xC0;
            engine->fb_shift[ch] = (data >> 1) & 7;
            engine->algo[ch] = data & 1; // 0=直刁E 1=並刁E            update_algorithm_routing(engine, ch, engine->algo[ch]);
        } else if (addr == 0xBD) {
            uint8_t old_rhy = engine->rhythm_enable;
            uint8_t old_val = engine->kc[8]; // kc[8] は通常使われなぁEEで0xBDの前回値保存用に流用
            engine->kc[8] = data;
            
            engine->rhythm_enable = (data & 0x20) ? 1 : 0;
            engine->amd = (data & 0x80) ? 1 : 0; // AM Depth
            engine->pmd = (data & 0x40) ? 1 : 0; // PM Depth
            
            if (engine->rhythm_enable != old_rhy) {
                
                
                
            }
            
            if (engine->rhythm_enable) {
                uint8_t trg = (data ^ old_val) & data;   // 0 -> 1 になったビチE
                uint8_t rel = (data ^ old_val) & ~data;  // 1 -> 0 になったビチE
                
                int ops[5] = {24, 29, 32, 33, 28}; // BD(ch6c), SD(ch7c), TOM(ch8m), TC(ch8c), HH(ch7m)
                int bits[5] = {0x10, 0x08, 0x04, 0x02, 0x01};
                
                for(int i=0; i<5; i++) {
                    int idx = ops[i];
                    if (trg & bits[i]) {
                        engine->ops[idx].env_level = EG_MAX; engine->ops[idx].phase = 0;
                        engine->ops[idx].env_state = EG_ATTACK;
                        if (i == 0) { // BDはモジュレータ(24)とキャリア(25)の両方をトリガー
                            engine->ops[25].env_level = EG_MAX; engine->ops[25].phase = 0;
                            engine->ops[25].env_state = EG_ATTACK;
                        }
                    } else if (rel & bits[i]) {
                        if (engine->ops[idx].env_state != EG_OFF) engine->ops[idx].env_state = EG_RELEASE;
                        if (i == 0 && engine->ops[25].env_state != EG_OFF) engine->ops[25].env_state = EG_RELEASE;
                    }
                }
            }
        }
        return;
    }
    
    // オペレータレジスタ (0x20-0x9F, 0xE0-0xFF)
    if ((addr >= 0x20 && addr <= 0x9F) || (addr >= 0xE0 && addr <= 0xFF)) {
        int ofs = addr & 0x1F;
        int group = ofs / 8; // 0, 1, 2
        int rem = ofs % 8;   // 0-7
        if (rem >= 6) return; // 無効なオフセチE
        
        int is_car = (rem >= 3) ? 1 : 0;
        int ch = group * 3 + (rem % 3);
        
        if (ch >= 0 && ch < 9) {
            int idx = ch * 4 + is_car;
            switch (addr & 0xE0) {
                case 0x20: // AM / VIB / EG_TYP / KSR / MULTI
                    engine->ops[idx].mul = data & 0x0F;
                    engine->ops[idx].ks = (data >> 4) & 1; 
                    engine->ops[idx].dt2 = (data >> 5) & 1; // ☁E修正: EG_TYPを保孁E                    engine->ops[idx].am_enable = (data >> 7) & 1;
                    update_phase_step_opl(engine, ch);
                    update_eg_rates_opl(engine, ch);
                    break;
                case 0x40: // KSL / TL
                    engine->ops[idx].ksl = (data >> 6) & 3; // ☁E追加: KSL (Key Scale Level)
                    engine->ops[idx].tl_atten = (data & 0x3F) * 32; // ☁E修正: YM2612と同じぁEスチEチE.75dB (32単佁E
                    update_eg_rates_opl(engine, ch); // KSL変更を反映
                    break;
                case 0x60: // AR / DR
                    engine->ops[idx].ar = (data >> 4) & 0x0F; engine->ops[idx].ar = engine->ops[idx].ar ? engine->ops[idx].ar*2+1 : 0;
                    engine->ops[idx].dr = data & 0x0F; engine->ops[idx].dr = engine->ops[idx].dr ? engine->ops[idx].dr*2+1 : 0;
                    update_eg_rates_opl(engine, ch);
                    break;
                case 0x80: // SL / RR
                    engine->ops[idx].sl_level = ((data >> 4) == 15) ? EG_MAX : (((data >> 4) * 4) * 32 << EG_FRACTION_BITS);
                    engine->ops[idx].rr = data & 0x0F; engine->ops[idx].rr = engine->ops[idx].rr ? engine->ops[idx].rr*2+1 : 0;
                    update_eg_rates_opl(engine, ch);
                    break;
                case 0xE0: // Waveform Select
                    engine->ops[idx].wave_sel = data & 0x03;
                    break;
            }
        }
    }
}


typedef int32_t (*AlgoFunc)(FMSoundEngine*, int, int32_t, int32_t, int32_t, int);

// YM2151 / YM2612 / OPL2 Routing Array Updater
static void update_algorithm_routing(FMSoundEngine *engine, int ch, uint8_t algo) {
    int b = ch * 4;

    if (engine->chip_type == CHIP_YM3812) {
        // OPL2 uses 2 operators per channel (OP1 is at b, OP2 is at b+1)
        switch (algo & 1) {
            case 0: // OP1 -> OP2 -> MIX
                engine->ops[b+0].src_bus = 0; engine->ops[b+0].dst_bus = 1;
                engine->ops[b+1].src_bus = 1; engine->ops[b+1].dst_bus = 4;
                break;
            case 1: // (OP1, OP2) -> MIX
                engine->ops[b+0].src_bus = 0; engine->ops[b+0].dst_bus = 4;
                engine->ops[b+1].src_bus = 0; engine->ops[b+1].dst_bus = 4;
                break;
        }
    } else {
        // 4 Operator Algorithms
        switch (algo & 7) {
            case 0: // 直列 (OP1 -> OP2 -> OP3 -> OP4)
                engine->ops[b+0].src_bus=0; engine->ops[b+0].dst_bus=1;  engine->ops[b+1].src_bus=1; engine->ops[b+1].dst_bus=2;  engine->ops[b+2].src_bus=2; engine->ops[b+2].dst_bus=3;  engine->ops[b+3].src_bus=3; engine->ops[b+3].dst_bus=4;
                break;
            case 1: // (OP1 + OP2) -> OP3 -> OP4
                engine->ops[b+0].src_bus=0; engine->ops[b+0].dst_bus=2;  engine->ops[b+1].src_bus=0; engine->ops[b+1].dst_bus=2;  engine->ops[b+2].src_bus=2; engine->ops[b+2].dst_bus=3;  engine->ops[b+3].src_bus=3; engine->ops[b+3].dst_bus=4;
                break;
            case 2: // OP1 + (OP2 -> OP3) -> OP4
                engine->ops[b+0].src_bus=0; engine->ops[b+0].dst_bus=3;  engine->ops[b+1].src_bus=0; engine->ops[b+1].dst_bus=2;  engine->ops[b+2].src_bus=2; engine->ops[b+2].dst_bus=3;  engine->ops[b+3].src_bus=3; engine->ops[b+3].dst_bus=4;
                break;
            case 3: // (OP1 -> OP2) + OP3 -> OP4
                engine->ops[b+0].src_bus=0; engine->ops[b+0].dst_bus=1;  engine->ops[b+1].src_bus=1; engine->ops[b+1].dst_bus=3;  engine->ops[b+2].src_bus=0; engine->ops[b+2].dst_bus=3;  engine->ops[b+3].src_bus=3; engine->ops[b+3].dst_bus=4;
                break;
            case 4: // (OP1 -> OP2), (OP3 -> OP4) -> MIX
                engine->ops[b+0].src_bus=0; engine->ops[b+0].dst_bus=1;  engine->ops[b+1].src_bus=1; engine->ops[b+1].dst_bus=4;  engine->ops[b+2].src_bus=0; engine->ops[b+2].dst_bus=3;  engine->ops[b+3].src_bus=3; engine->ops[b+3].dst_bus=4;
                break;
            case 5: // OP1 -> (OP2, OP3, OP4) -> MIX
                engine->ops[b+0].src_bus=0; engine->ops[b+0].dst_bus=1;  engine->ops[b+1].src_bus=1; engine->ops[b+1].dst_bus=4;  engine->ops[b+2].src_bus=1; engine->ops[b+2].dst_bus=4;  engine->ops[b+3].src_bus=1; engine->ops[b+3].dst_bus=4;
                break;
            case 6: // (OP1 -> OP2), OP3, OP4 -> MIX
                engine->ops[b+0].src_bus=0; engine->ops[b+0].dst_bus=1;  engine->ops[b+1].src_bus=1; engine->ops[b+1].dst_bus=4;  engine->ops[b+2].src_bus=0; engine->ops[b+2].dst_bus=4;  engine->ops[b+3].src_bus=0; engine->ops[b+3].dst_bus=4;
                break;
            case 7: // 並列 (OP1, OP2, OP3, OP4) -> MIX
                engine->ops[b+0].src_bus=0; engine->ops[b+0].dst_bus=4;  engine->ops[b+1].src_bus=0; engine->ops[b+1].dst_bus=4;  engine->ops[b+2].src_bus=0; engine->ops[b+2].dst_bus=4;  engine->ops[b+3].src_bus=0; engine->ops[b+3].dst_bus=4;
                break;
        }
    }
}

void IRAM_ATTR fm_engine_tick(FMSoundEngine *engine, int32_t *out_l, int32_t *out_r) {
    static int tick_counter = 0;
    int active_channels = (engine->chip_type == CHIP_YM2612) ? 6 : ((engine->chip_type == CHIP_YM2151) ? 8 : ((engine->chip_type == CHIP_YM2203) ? 3 : 9));
    
    if ((tick_counter++ & 3) == 0) {
        update_envelopes(engine, active_channels);
    }

    int32_t mix_l = 0;
    int32_t mix_r = 0;

    if (engine->noise_enable || (engine->chip_type == CHIP_YM3812 && engine->rhythm_enable)) {
        engine->noise_phase += engine->noise_step;
        if (engine->noise_step == 0) engine->noise_step = 0x01000000 >> 2; 
        if (engine->noise_phase < engine->noise_step) {
            engine->noise_rng ^= engine->noise_rng << 13;
            engine->noise_rng ^= engine->noise_rng >> 17;
            engine->noise_rng ^= engine->noise_rng << 5;
        }
    }

    int32_t global_pm = 0;
    int32_t global_am = 0;

    if (engine->pmd > 0 || engine->amd > 0) {
        engine->lfo_phase += engine->lfo_step;
        uint32_t p = (engine->lfo_phase >> 24) & 0xFF; 
        
        int32_t lfo_pm = LFO_PM_TAB[engine->lfo_wave][p];
        int32_t lfo_am = LFO_AM_TAB[engine->lfo_wave][p];
        
        global_pm = (engine->chip_type == CHIP_YM2151) ? ((lfo_pm * engine->pmd) >> 7) : lfo_pm; 
        global_am = (engine->chip_type == CHIP_YM2151) ? ((lfo_am * engine->amd) >> 7) : lfo_am; 
    }

    static int32_t pms_mult[8] = { 0, 1, 2, 4, 8, 16, 32, 64 };
    static int32_t ams_mult[4] = { 0, 1, 4, 16 };

    if (engine->chip_type == CHIP_YM3812) {
        engine->lfo_am_phase += engine->opl2_lfo_am_step;
        engine->lfo_pm_phase += engine->opl2_lfo_pm_step;
        int32_t opl2_am = (engine->lfo_am_phase & 0x80000000U) ? 105 : 0; 
        int32_t opl2_pm = (engine->lfo_pm_phase & 0x80000000U) ? 1 : -1;
        int32_t ch_am = engine->amd ? opl2_am : (opl2_am >> 2); 
        int32_t ch_pm = engine->pmd ? (opl2_pm << 1) : opl2_pm; 

        for (int ch = 0; ch < active_channels; ch++) {
            int b = ch * 4; 
            if (engine->ops[b+0].env_state == EG_OFF && engine->ops[b+1].env_state == EG_OFF) continue;
            
            int32_t fb_sum = engine->fb_memory[ch][0] + engine->fb_memory[ch][1];
            int32_t fb_mod = (engine->fb_shift[ch] == 0) ? 0 : (fb_sum >> (10 - engine->fb_shift[ch]));
            int32_t ch_out = 0;

            if (engine->rhythm_enable && ch >= 6) {
                if (ch == 6) {
                    int32_t b0 = fb_mod;
                    int32_t o0 = calc_op_internal(engine, b+0, b0, ch_pm, engine->ops[b+0].am_enable?ch_am:0, 0, 1);
                    engine->fb_memory[ch][0] = engine->fb_memory[ch][1]; engine->fb_memory[ch][1] = o0;
                    int32_t bd_mod = (engine->algo[ch] == 0) ? o0 : 0;
                    int32_t bd_out = calc_op_internal(engine, b+1, bd_mod, ch_pm, engine->ops[b+1].am_enable?ch_am:0, 0, 1);
                    ch_out = (engine->algo[ch] == 0) ? bd_out : (o0 + bd_out);
                    ch_out <<= 1;
                } else if (ch == 7) {
                    int32_t sd_out = calc_op_internal(engine, b+1, 0, ch_pm, engine->ops[b+1].am_enable?ch_am:0, 1, 1);
                    int32_t hh_out = calc_op_internal(engine, b+0, 0, ch_pm, engine->ops[b+0].am_enable?ch_am:0, 2, 1);
                    ch_out = (sd_out + hh_out) << 1;
                } else if (ch == 8) {
                    int32_t tom_out = calc_op_internal(engine, b+0, 0, ch_pm, engine->ops[b+0].am_enable?ch_am:0, 0, 1);
                    int32_t tc_out = calc_op_internal(engine, b+1, 0, ch_pm, engine->ops[b+1].am_enable?ch_am:0, 2, 1);
                    ch_out = (tom_out + tc_out) << 1;
                }
            } else {
                int32_t bus[5] = {0};
                int32_t mod0 = bus[engine->ops[b+0].src_bus] + fb_mod;
                int32_t out0 = calc_op_internal(engine, b+0, mod0, ch_pm, engine->ops[b+0].am_enable?ch_am:0, 0, 1);
                engine->fb_memory[ch][0] = engine->fb_memory[ch][1];
                engine->fb_memory[ch][1] = out0;
                bus[engine->ops[b+0].dst_bus] += out0;

                int32_t mod1 = bus[engine->ops[b+1].src_bus];
                int32_t out1 = calc_op_internal(engine, b+1, mod1, ch_pm, engine->ops[b+1].am_enable?ch_am:0, 0, 1);
                bus[engine->ops[b+1].dst_bus] += out1;

                ch_out = bus[4] << 1;
            }
            mix_l += ch_out * engine->pan_l[ch];
            mix_r += ch_out * engine->pan_r[ch];
        }
    } else {
        int is_ym2151 = (engine->chip_type == CHIP_YM2151);
        for (int ch = 0; ch < active_channels; ch++) {
            int b = ch * 4; 
            if (engine->ops[b+0].env_state == EG_OFF && engine->ops[b+1].env_state == EG_OFF && 
                engine->ops[b+2].env_state == EG_OFF && engine->ops[b+3].env_state == EG_OFF) continue;
            
            int32_t ch_pm = (global_pm * pms_mult[engine->pms[ch]]) >> 5; 
            int32_t ch_am = (global_am * ams_mult[engine->ams[ch]]) >> 4; 

            int32_t fb_sum = engine->fb_memory[ch][0] + engine->fb_memory[ch][1];
            int32_t fb_mod = (engine->fb_shift[ch] == 0) ? 0 : (fb_sum >> (10 - engine->fb_shift[ch]));

            int is_noise_ch = (is_ym2151 && ch == 7 && engine->noise_enable);
            int32_t bus[5] = {0};
            
            int32_t mod0 = bus[engine->ops[b+0].src_bus] + fb_mod;
            int32_t out0 = calc_op_internal(engine, b+0, mod0, ch_pm, ch_am, 0, 0);
            engine->fb_memory[ch][0] = engine->fb_memory[ch][1];
            engine->fb_memory[ch][1] = out0;
            bus[engine->ops[b+0].dst_bus] += out0;

            int32_t mod1 = bus[engine->ops[b+1].src_bus];
            int32_t out1 = calc_op_internal(engine, b+1, mod1, ch_pm, ch_am, 0, 0);
            bus[engine->ops[b+1].dst_bus] += out1;

            int32_t mod2 = bus[engine->ops[b+2].src_bus];
            int32_t out2 = calc_op_internal(engine, b+2, mod2, ch_pm, ch_am, 0, 0);
            bus[engine->ops[b+2].dst_bus] += out2;

            int32_t mod3 = bus[engine->ops[b+3].src_bus];
            int32_t out3 = calc_op_internal(engine, b+3, mod3, ch_pm, ch_am, is_noise_ch ? 1 : 0, 0);
            bus[engine->ops[b+3].dst_bus] += out3;

            int32_t ch_out = bus[4];
            mix_l += ch_out * engine->pan_l[ch];
            mix_r += ch_out * engine->pan_r[ch];
        }
    }

    mix_l = (mix_l + engine->prev_l) / 2;
    mix_r = (mix_r + engine->prev_r) / 2;
    engine->prev_l = mix_l;
    engine->prev_r = mix_r;

    *out_l += mix_l; 
    *out_r += mix_r; 
}


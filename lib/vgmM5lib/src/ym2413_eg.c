#if defined(__GNUC__)
/* R4: -O2 for this proven hot audio translation unit only. Do not enable -ffast-math. */
#pragma GCC optimize ("O2")
#endif
#include "ym2413_eg.h"
#include <string.h>
#include <math.h>
static const uint8_t decay_steps[4][8] = {
    {0,1,0,1,0,1,0,1}, {0,1,0,1,1,1,0,1},
    {0,1,1,1,0,1,1,1}, {0,1,1,1,1,1,1,1}
};
static unsigned eg_rate_high(unsigned base, unsigned rks);
/* 128-value static attenuation lookup: no pow() in the 50kHz/native EG path.
 * The same 0.375 dB level steps are preserved exactly (apart from the
 * one-time initialization rounding inherent to double). */
static double eg_amplitude_table[128];
static int eg_amplitude_ready;
static double to_amplitude(uint8_t a) {
    if (!eg_amplitude_ready) {
        for (unsigned i=0;i<124u;i++)
            eg_amplitude_table[i]=pow(10.0,-(double)i*0.375/20.0);
        for (unsigned i=124u;i<128u;i++)eg_amplitude_table[i]=0.0;
        eg_amplitude_ready=1;
    }
    return eg_amplitude_table[a&127u];
}
void opll_eg_init(opll_eg *e, uint8_t ar, uint8_t dr, uint8_t sl, uint8_t rr, uint8_t sustained, uint8_t sus, uint8_t rks) {
    memset(e,0,sizeof(*e));
    e->phase=OPLL_EG_OFF; e->attenuation=127;
    e->rate_ar=ar&15; e->rate_dr=dr&15; e->sustain_level=sl&15;
    e->rate_rr=rr&15; e->sustained=sustained!=0; e->sus=sus!=0;
    e->rks=rks>15?15:rks;
}
/* YM2413 does not jump directly to ATTACK on a rising key edge.
 * It enters DAMP at rate 12 to quiet a preceding note without discontinuous
 * instant-mute before (re)starting the oscillator envelope. */
void opll_eg_key_on(opll_eg *e) {
    e->phase=OPLL_EG_DAMP;
}
/* emu2413 slotOff() changes the envelope state to RELEASE when an
 * independently releasable slot loses its key, even if fully attenuated. */
void opll_eg_key_off(opll_eg *e) {e->phase=OPLL_EG_RELEASE;}
/* STAGE 7: reference-informed EG rate/counter rules.
 * rks: 4-bit key scale value, upper 2 bits adjust rate, lower 2
 * select one of four increment sequences (emu2413 get_parameter_rate).
 * This laboratory module is NOT yet the sound-producing PCM path.
 */
static unsigned eg_rate_high(unsigned base, unsigned rks) {
    /* emu2413: a zero parameter rate stays zero regardless of RKS.
     * Previously high-key notes incorrectly advanced AR/DR/RR=0. */
    if (base == 0u) return 0u;
    unsigned hi = base + (rks >> 2);
    return hi > 15 ? 15 : hi;
}
static unsigned eg_rate_shift(unsigned hi, int attack) {
    if (attack) return (hi > 0 && hi < 12) ? (13 - hi) : 0;
    return hi < 13 ? (13 - hi) : 0;
}
static unsigned eg_attack_step(unsigned hi, unsigned lo, unsigned counter, unsigned shift) {
    unsigned index;
    if (hi == 0 || hi == 15) return 0;
    if (hi >= 12) {
        index = (counter & 12u) >> 1;
        return (16u - hi) - decay_steps[lo][index];
    }
    index = counter >> shift;
    return decay_steps[lo][index & 7u] ? 4u : 0u;
}
static unsigned eg_decay_step(unsigned hi, unsigned lo, unsigned counter, unsigned shift) {
    unsigned index;
    if (hi == 0) return 0;
    if (hi == 13) {
        index = ((counter & 12u) >> 1) | (counter & 1u);
        return decay_steps[lo][index];
    }
    if (hi == 14) {
        index = (counter & 12u) >> 1;
        return decay_steps[lo][index] + 1u;
    }
    if (hi == 15) return 2;
    index = counter >> shift;
    return decay_steps[lo][index & 7u];
}
static void advance_one_tick(opll_eg *e) {
    ++e->counter;
    if (e->phase == OPLL_EG_OFF) return;
    unsigned base = e->phase == OPLL_EG_ATTACK ? e->rate_ar :
                    e->phase == OPLL_EG_DECAY ? e->rate_dr :
                    e->phase == OPLL_EG_RELEASE ? (e->sus ? 5u : (e->sustained ? e->rate_rr : 7u)) :
                    e->phase == OPLL_EG_DAMP ? 12u :
                    e->rate_rr;
    if (e->phase == OPLL_EG_SUSTAIN && e->sustained) return;
    /* Upstream get_parameter_rate() returns 0 for unkeyed normal modulators,
     * regardless of EG state. This does NOT freeze the global EG counter. */
    if (e->force_zero_rate) base = 0u;
    /* Rate zero freezes attenuation steps, not EG state transitions. */
    unsigned hi = eg_rate_high(base, e->rks);
    unsigned lo = e->rks & 3u;
    unsigned shift = eg_rate_shift(hi, e->phase == OPLL_EG_ATTACK);
    unsigned mask = (1u << shift) - 1u;
    if (e->phase == OPLL_EG_ATTACK) {
        if (e->attenuation && hi && (e->counter & mask & ~3u) == 0) {
            unsigned step = eg_attack_step(hi, lo, e->counter, shift);
            if (step) {
                int next = (int)e->attenuation - ((int)e->attenuation >> step) - 1;
                e->attenuation = (uint8_t)(next < 0 ? 0 : next);
            }
        }
        if (e->attenuation == 0) e->phase = OPLL_EG_DECAY;
    } else {
        if (hi && (e->counter & mask) == 0) {
            unsigned value = e->attenuation + eg_decay_step(hi, lo, e->counter, shift);
            e->attenuation = (uint8_t)(value > 127 ? 127 : value);
        }
        int transitioned_from_damp=0;
        if (e->phase == OPLL_EG_DAMP && e->attenuation >= 123 &&
            (e->counter & mask) == 0u) {
            transitioned_from_damp=1;
            if (eg_rate_high(e->rate_ar,e->rks) == 15u) {
                e->attenuation = 0u;
                e->phase = OPLL_EG_DECAY;
            } else {
                e->phase = OPLL_EG_ATTACK;
            }
        }
        if (!transitioned_from_damp && e->phase == OPLL_EG_DECAY &&
            (e->attenuation >> 3) == e->sustain_level)
            e->phase = OPLL_EG_SUSTAIN;
        /* emu2413 retains the actual RELEASE/SUSTAIN state even when the
         * attenuator reaches 127. No hardware OFF transition occurs here. */
    }
}
double opll_eg_tick(opll_eg *e, uint32_t clock, uint32_t sample_rate) {
    if(!sample_rate)return 0;
    e->amplitude_previous=e->amplitude_target;
    /* Each EG subcycle is nominally clock/72, not one tick per 32 host samples. */
    e->clock_accum+=(uint64_t)clock;
    uint64_t divisor=(uint64_t)72*sample_rate;
    while(e->clock_accum>=divisor){e->clock_accum-=divisor;advance_one_tick(e);}
    e->amplitude_target=to_amplitude(e->attenuation);
    return e->amplitude_target;
}
double opll_eg_tick_native(opll_eg *e) {
    e->amplitude_previous=e->amplitude_target;
    /* All callers previously used clock=72, sample_rate=1.
     * Accumulator starts at zero and stays zero after each full tick. */
    advance_one_tick(e);
    e->amplitude_target=to_amplitude(e->attenuation);
    return e->amplitude_target;
}
double opll_eg_smooth_view(const opll_eg *e,double fraction) {
    if (fraction < 0) fraction = 0;
    if (fraction > 1) fraction = 1;
    return e->amplitude_previous+(e->amplitude_target-e->amplitude_previous)*fraction;
}

#include "ym2413_core.h"
#include <string.h>

static void update_pitch(ym2413_core *s, unsigned ch) {
    const uint8_t lo=s->regs[0x10+ch], hi=s->regs[0x20+ch];
    s->ch[ch].fnum=(uint16_t)(lo | ((hi & 1u)<<8));
    s->ch[ch].block=(hi>>1)&7u;
    s->ch[ch].key_on=(hi>>4)&1u;
    s->ch[ch].sustain=(hi>>5)&1u;
}
static void update_timbre(ym2413_core *s, unsigned ch) {
    const uint8_t data=s->regs[0x30+ch];
    s->ch[ch].instrument=data>>4;
    s->ch[ch].volume=data&15u;
}
static void refresh_rhythm(ym2413_core *s) {
    const uint8_t v=s->regs[0x0e];
    s->rhythm_mode=(v>>5)&1u;
    /* The rhythm register bits are BD, SD, TOM, CYM, HH = 4,3,2,1,0. */
    for(unsigned d=0; d<YM2413_DRUMS; ++d)
        s->drums[d]=(uint8_t)(s->rhythm_mode && ((v>>(4u-d))&1u));
    s->ch[6].rhythm_key=s->drums[YM2413_BD];
    s->ch[7].rhythm_key=s->drums[YM2413_SD] | s->drums[YM2413_HH];
    s->ch[8].rhythm_key=s->drums[YM2413_TOM] | s->drums[YM2413_CYM];
}
static void refresh_drums(ym2413_core *s) {
    s->drum_volume[YM2413_BD]=s->regs[0x36]&15u;
    s->drum_volume[YM2413_SD]=s->regs[0x37]&15u;
    s->drum_volume[YM2413_HH]=s->regs[0x37]>>4;
    s->drum_volume[YM2413_CYM]=s->regs[0x38]&15u;
    s->drum_volume[YM2413_TOM]=s->regs[0x38]>>4;
}
void ym2413_init(ym2413_core *s, uint32_t clock_hz, uint32_t sample_rate) {
    memset(s,0,sizeof(*s));
    s->clock_hz=clock_hz;
    s->sample_rate=sample_rate;
    s->eg_mode=YM2413_STRICT;
}
void ym2413_set_eg_mode(ym2413_core *s, ym2413_eg_mode mode) {
    if(mode==YM2413_STRICT || mode==YM2413_SMOOTH) s->eg_mode=mode;
}
void ym2413_write(ym2413_core *s, uint8_t reg, uint8_t data) {
    if(reg>=0x40) return;
    /* YM2413 mirrored register windows, matching official emu2413.
     * Canonicalize before changing the register file or derived state. */
    if((reg>=0x19 && reg<=0x1f) ||
       (reg>=0x29 && reg<=0x2f) ||
       (reg>=0x39 && reg<=0x3f)) reg=(uint8_t)(reg-9u);
    s->regs[reg]=data;
    if(reg<8) s->custom_patch[reg]=data;
    if(reg==0x0e) refresh_rhythm(s);
    if(reg>=0x10 && reg<=0x28) {
        if(reg<=0x18) update_pitch(s,reg-0x10);
        else if(reg>=0x20) update_pitch(s,reg-0x20);
    }
    if(reg>=0x30 && reg<=0x38) {
        update_timbre(s,reg-0x30);
        refresh_drums(s);
    }
}
double ym2413_nominal_hz(const ym2413_core *s,unsigned channel) {
    if(channel>=YM2413_CHANNELS || s->sample_rate==0) return 0.0;
    /* YM2413: f = fnum * 2^(block-1) * master_clock / (72*2^18)
       This is a register-derived estimate only, NOT verified synthesized PCM. */
    const ym2413_channel *c=&s->ch[channel];
    return ((double)c->fnum * (double)(1u<<c->block) * s->clock_hz) / (72.0*524288.0);
}

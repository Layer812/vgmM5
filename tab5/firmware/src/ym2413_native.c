#if defined(__GNUC__)
/* R4: -O2 for this proven hot audio translation unit only. Do not enable -ffast-math. */
#pragma GCC optimize ("O2")
#endif
#include "ym2413_native.h"
#include <math.h>
#include <string.h>
#include <limits.h>
/* Independent 18-slot, fixed-point phase and logarithmic-sine YM2413 study core.
 * Numerical layout follows the public YM2413 model documented by emu2413,
 * but implementation is independent and not a linked third-party core.
 * Native domain is master_clock/72. This remains a HOST validation candidate.
 */
#define PI2 6.28318530717958647692
#define NPHASE 1024
#define NPHMASK 1023
#define DP_MASK 0x7ffffu
#define PG_SHIFT 9
#define EG_MAX 123
static const uint8_t inst[19][8]={
 {0,0,0,0,0,0,0,0},
 {0x71,0x61,0x1e,0x17,0xd0,0x78,0x00,0x17},
 {0x13,0x41,0x1a,0x0d,0xd8,0xf7,0x23,0x13},
 {0x13,0x01,0x99,0x00,0xf2,0xc4,0x21,0x23},
 {0x11,0x61,0x0e,0x07,0x8d,0x64,0x70,0x27},
 {0x32,0x21,0x1e,0x06,0xe1,0x76,0x01,0x28},
 {0x31,0x22,0x16,0x05,0xe0,0x71,0x00,0x18},
 {0x21,0x61,0x1d,0x07,0x82,0x81,0x11,0x07},
 {0x33,0x21,0x2d,0x13,0xb0,0x70,0x00,0x07},
 {0x61,0x61,0x1b,0x06,0x64,0x65,0x10,0x17},
 {0x41,0x61,0x0b,0x18,0x85,0xf0,0x81,0x07},
 {0x33,0x01,0x83,0x11,0xea,0xef,0x10,0x04},
 {0x17,0xc1,0x24,0x07,0xf8,0xf8,0x22,0x12},
 {0x61,0x50,0x0c,0x05,0xd2,0xf5,0x40,0x42},
 {0x01,0x01,0x55,0x03,0xe9,0x90,0x03,0x02},
 {0x41,0x41,0x89,0x03,0xf1,0xe4,0xc0,0x13},
 {0x01,0x01,0x18,0x0f,0xdf,0xf8,0x6a,0x6d},
 {0x01,0x01,0x00,0x00,0xc8,0xd8,0xa7,0x68},
 {0x05,0x01,0x00,0x00,0xf8,0xaa,0x59,0x55}
};
static const uint8_t mult2[16]={1,2,4,6,8,10,12,14,16,18,20,20,24,24,30,30};
static const int8_t pm[8][8]={
 {0,0,0,0,0,0,0,0},{0,0,1,0,0,0,-1,0},{0,1,2,1,0,-1,-2,-1},
 {0,1,3,1,0,-1,-3,-1},{0,2,4,2,0,-2,-4,-2},{0,2,5,2,0,-2,-5,-2},
 {0,3,6,3,0,-3,-6,-3},{0,3,7,3,0,-3,-7,-3}
};
#include "ym2413_tables.h"
static unsigned ksl_steps(unsigned kl,unsigned block,unsigned fnum){
 return native_ksl_table[((block&7u)<<4)|((fnum>>5)&15u)][kl&3u];
}
static unsigned get_am(unsigned am_phase){
 unsigned i=(am_phase>>6)%210u;
 return i<104?i/8u:(i<107?13u:(210u-i)/8u);
}
static const uint8_t *slot_patch(const ym2413_core *s,unsigned ch){
 if(s->rhythm_mode && ch>=6)return inst[16+(ch-6)];
 unsigned n=s->ch[ch].instrument;
 return n==0?s->custom_patch:inst[(n<16?n:1)];
}
/* Patch selection depends only on YM2413 register writes. Never redo it at the
 * ~50kHz native clock. All pointers refer to stable ROM/custom_patch storage. */
static void cache_slot_params(const ym2413_core *s,r31_opll_native *p,unsigned idx);
void r31_opll_native_refresh_patches(const ym2413_core *s, r31_opll_native *p) {
 for (unsigned ch=0; ch<YM2413_CHANNELS; ++ch) p->patches[ch]=slot_patch(s,ch);
 for (unsigned idx=0;idx<18;idx++)cache_slot_params(s,p,idx);
}
static unsigned slot_key(const ym2413_core *s,unsigned ch,unsigned o){
 unsigned key=s->ch[ch].key_on;
 if(s->rhythm_mode && ch>=6){
   if(ch==6) key |= s->drums[YM2413_BD];
   if(ch==7) key |= s->drums[o==0?YM2413_HH:YM2413_SD];
   if(ch==8) key |= s->drums[o==0?YM2413_TOM:YM2413_CYM];
 }
 return key;
}
static unsigned slot_volume(const ym2413_core *s,unsigned ch,unsigned o){
 if(s->rhythm_mode && ch>=6){
   if(ch==6) return s->drum_volume[YM2413_BD];
   if(ch==7)return s->drum_volume[o==0?YM2413_HH:YM2413_SD];
   return s->drum_volume[o==0?YM2413_TOM:YM2413_CYM];
 }
 return s->ch[ch].volume;
}
/* R6: pitch, patch, TL, RKS and sustain settings change ONLY on YM2413
 * register writes; the per-native-tick key/EG phase logic remains untouched.
 * Store the exact old integer expression, not an approximation. */
static void cache_slot_params(const ym2413_core *s,r31_opll_native *p,unsigned idx){
 unsigned ch=idx/2,o=idx%2;
 const ym2413_channel *c=&s->ch[ch];
 const uint8_t *patch=p->patches[ch];
 unsigned tl=(o==0 && !(s->rhythm_mode && ch>=7)) ?
       (patch[2]&63u) : (slot_volume(s,ch,o)<<2);
 p->base_atten[idx]=(uint16_t)(ksl_steps((o==0?patch[2]:patch[3])>>6,
             c->block,c->fnum)+(tl<<1));
 r31_opll_slot *z=&p->slot[idx];
 if(!z->initialized)return;
 z->eg.rate_ar=(patch[4+o]>>4)&15u;
 z->eg.rate_dr=patch[4+o]&15u;
 z->eg.sustain_level=(patch[6+o]>>4)&15u;
 z->eg.rate_rr=patch[6+o]&15u;
 z->eg.sustained=(patch[o]>>5)&1u;
 z->eg.rks=(uint8_t)((patch[o]&0x10u)
       ? ((c->block<<1)|((c->fnum>>8)&1u)) : c->block>>1);
 z->eg.sus=(uint8_t)((o==1 || (s->rhythm_mode && ch>=7)) ? c->sustain : 0u);
}
static int16_t linear(int32_t i,unsigned att){
 int32_t h=i+((att>127u?127u:att)<<4);
 int32_t ex= (int32_t)exp_values[(h&255)^255]+1024;
 int32_t shift=(h>>8)&127;
 int32_t v=shift<20?ex>>shift:0;
 v=(h&0x8000)?~v:v;
 /* Multiplying signed values avoids C's undefined left-shift of negatives. */
 return (int16_t)(v*2);
}
static int16_t slot_output(const ym2413_core *s, r31_opll_native *p,const uint8_t *patch,unsigned idx,unsigned phase_shift){
 unsigned o=idx%2;
 const r31_opll_slot *slot=&p->slot[idx];
 const opll_eg *e=&slot->eg;
 /* STRICT mutes at the documented EG_MAX threshold. SMOOTH may
  * interpolate the ONE tick crossing EG_MAX: otherwise the early
  * return defeats EG interpolation exactly at the final transition.
  * Do not modify envelope state, clock count, or hardware mode. */
 const unsigned eg_att=(e->attenuation>EG_MAX)?EG_MAX:e->attenuation;
 const int smooth_mute_edge=(s->eg_mode==YM2413_SMOOTH &&
       e->attenuation>EG_MAX && e->amplitude_previous>0.0);
 if(e->attenuation>EG_MAX && !smooth_mute_edge)return 0;
 unsigned waveform=(patch[3]>>(o==0?3:4))&1u;
 unsigned pg=(slot->phase>>PG_SHIFT)+phase_shift;
 uint16_t h=sinlog[waveform][pg&NPHMASK];
 /* In rhythm mode SD/HH/TOM/CYM slots become type=3; their
  * 4-bit register volumes replace the melodic modulator TL as well. */
 unsigned att=eg_att+p->base_atten[idx];
 if(patch[o]&0x80u)att+=p->native_am;
 int16_t output=linear(h,att);
 if(s->eg_mode==YM2413_SMOOTH){
   /* Smooth is an EG-only presentation change. The phase, trigger times,
    * F-Number, and native-clock count remain identical to STRICT. */
   /* On a mute-edge, use the EG_MAX level solely as the
    * waveform reference and interpolate down to silence. */
   double at=smooth_mute_edge ? 0.0 : e->amplitude_target;
   double reference_amp=smooth_mute_edge ?
      0.004940262207658599 : at; /* 10^(-123*0.375/20) */
   /* When native EG amplitudes are identical, interpolation is an identity.
    * Preserve STRICT and smooth mute-edge behavior; no clock/pitch changes. */
   if(reference_amp>0.0 &&
      (e->amplitude_previous != e->amplitude_target || smooth_mute_edge)){
     double interp=opll_eg_smooth_view(e,0.5);
     int32_t v=(int32_t)lrint((double)output*(interp/reference_amp));
     if(v>32767)v=32767;
     if(v< -32768)v=-32768;
     output=(int16_t)v;
   }
 }
 return output;
}
static void update_phase_and_eg(const ym2413_core *s,r31_opll_native *p,const uint8_t * const patches[9]){
 p->native_ticks++;
 p->am_phase++;
 /* AM index changes only once per 64 native clocks; preserve wraparound. */
 if((p->am_phase & 63u)==0u) p->native_am=(uint8_t)get_am(p->am_phase);
 p->pm_phase++;
 for(unsigned ch=0;ch<9;ch++)for(unsigned o=0;o<2;o++){
   unsigned idx=2*ch+o;
   r31_opll_slot *z=&p->slot[idx];
   const uint8_t *patch=patches[ch];
   const ym2413_channel *c=&s->ch[ch];
   const unsigned rks=(patch[o]&0x10u)
          ? ((c->block<<1)|((c->fnum>>8)&1u)) : c->block>>1;
   if(!z->initialized){
      opll_eg_init(&z->eg,(patch[4+o]>>4)&15u,patch[4+o]&15u,
        (patch[6+o]>>4)&15u,patch[6+o]&15u,
        (patch[o]>>5)&1u,(uint8_t)((o==1 || (s->rhythm_mode && ch>=7)) ? c->sustain : 0u),(uint8_t)rks);
      /* Match the global EG clock at the first native step. */
      z->eg.counter=p->native_ticks-1u;
      z->initialized=1;
      cache_slot_params(s,p,idx); /* include the first native tick */
   }
   /* The above envelope parameters are updated at register-write time
    * by cache_slot_params(), never 18x at every ~50kHz native tick. */
   unsigned key=slot_key(s,ch,o);
   if(key&&!z->key)opll_eg_key_on(&z->eg);
   if(!key&&z->key&&(o==1 || (s->rhythm_mode&&ch>=7)))
       opll_eg_key_off(&z->eg);
   z->key=(uint8_t)key;
   /* A normal unkeyed modulator must retain its EG level; emu2413
    * get_parameter_rate() yields zero while slot type=0 and key_flag=0.
    * Rhythm HH/TOM slots have type=3 and must not be held. */
   z->eg.force_zero_rate=(uint8_t)(o==0 && !(s->rhythm_mode && ch>=7) && !key);
   unsigned old_state=z->eg.phase;
   (void)opll_eg_tick_native(&z->eg);
   if(old_state==OPLL_EG_DAMP&&z->eg.phase!=OPLL_EG_DAMP){
     if(s->rhythm_mode && ch>=7){
       /* Rhythm CH7/8: type=3, per-slot phase restart except HH and
        * CYM which carry pg_keep=1 across triggers. */
       if(!((ch==7 && o==0)||(ch==8 && o==1)))z->phase=0;
     }else if(o==1){
       /* Normal melodic 2-operator carrier starts both oscillators. */
       p->slot[2*ch].phase=0;
       z->phase=0;
     }
   }
   int mod= (patch[o]&0x40u) ? pm[(c->fnum>>6)&7u][(p->pm_phase>>10)&7u] : 0;
   uint32_t step=(uint32_t)(((((int)c->fnum*2+mod)*mult2[patch[o]&15u])<<c->block)>>2);
   z->phase=(z->phase+step)&DP_MASK;
 }
}
static void shift_noise(r31_opll_native *p,unsigned count){
 for(unsigned i=0;i<count;i++){
   /* YM2413 internal noise polynomial, same 22-bit shift sequence
    * as the documented emu2413 generator, not a generic 17-bit LFSR. */
   if(p->noise&1u)p->noise^=0x800200u;
   p->noise>>=1;
 }
}
static int calc_mod(const ym2413_core *s,r31_opll_native *p,const uint8_t *patch,unsigned ch){
 unsigned idx=2*ch; r31_opll_slot *z=&p->slot[idx];
 unsigned fb=patch[3]&7u;
 int32_t fm=fb? ((int32_t)z->out[0]+z->out[1])>>(9-fb) : 0;
 z->out[1]=z->out[0];
 z->out[0]=slot_output(s,p,patch,idx,(unsigned)fm);
 return z->out[0];
}
static int calc_car(const ym2413_core *s,r31_opll_native *p,const uint8_t *patch,unsigned ch,int fm){
 r31_opll_slot *z=&p->slot[2*ch+1];
 z->out[1]=z->out[0];
 z->out[0]=slot_output(s,p,patch,2*ch+1,2*(unsigned)(fm>>1));
 return z->out[0];
}
static unsigned noise_phase(const r31_opll_native *p){
 unsigned hh=(p->slot[14].phase>>PG_SHIFT)&NPHMASK;
 unsigned cy=(p->slot[17].phase>>PG_SHIFT)&NPHMASK;
 return (((hh>>2)^(hh>>7))&1u) | (((hh>>3)^(cy>>5))&1u) | (((cy>>3)^(cy>>5))&1u);
}
static int calc_special(const ym2413_core *s,r31_opll_native *p,const uint8_t *patch,unsigned slot,unsigned phase){
 return slot_output(s,p,patch,slot,phase-((p->slot[slot].phase>>PG_SHIFT)&NPHMASK));
}
/* emu2413 _MO(x) = (-x) >> 1 (arithmetic shift).  C truncation
 * toward zero is NOT equivalent for odd samples. Compute floor(-x/2)
 * explicitly without relying on implementation-defined shifts. */
static int melody_mix_half(int v){
  return v >= 0 ? -((v+1)/2) : -(v/2);
}
static int native_tick(const ym2413_core *s,r31_opll_native *p){
 /* emu2413 update_output() samples HH/CYM phase bits BEFORE update_slots().
  * Only the noise phase selector uses this one-native-tick old phase.
  * Playback/EG/phase progression itself stays unchanged. */
 const unsigned shortn = s->rhythm_mode ? noise_phase(p) : 0u;
 const uint8_t *const *patches=p->patches;
 update_phase_and_eg(s,p,patches);
 int sum=0;
 for(unsigned ch=0;ch<6;ch++)sum+=melody_mix_half(calc_car(s,p,patches[ch],ch,calc_mod(s,p,patches[ch],ch)));
 if(!s->rhythm_mode){
   for(unsigned ch=6;ch<9;ch++)sum+=melody_mix_half(calc_car(s,p,patches[ch],ch,calc_mod(s,p,patches[ch],ch)));
   shift_noise(p,18);
 }else{
   sum+=calc_car(s,p,patches[6],6,calc_mod(s,p,patches[6],6))*R31_BASS_DRUM_GAIN;
   shift_noise(p,14);
   unsigned pg_sd=(p->slot[15].phase>>PG_SHIFT)&NPHMASK;
   unsigned n=p->noise&1u;
   unsigned hh=shortn?(n?0x2d0u:0x234u):(n?0x034u:0x0d0u);
   unsigned sd=(pg_sd&0x100u)?(n?0x300u:0x200u):(n?0x000u:0x100u);
   sum+=calc_special(s,p,patches[7],14,hh);
   sum+=calc_special(s,p,patches[7],15,sd);
   shift_noise(p,2);
   sum+=slot_output(s,p,patches[8],16,0);
   sum+=calc_special(s,p,patches[8],17,shortn?0x300u:0x100u);
   shift_noise(p,2);
 }
 /* output stage leaves headroom; reference mixer uses signed 16bit. */
 /* Keep full signed 32-bit mix precision. Never saturate before
  * normalization; saturation inside the chip mixer alters harmonics. */
 return sum;
}
void r31_opll_native_init(r31_opll_native *p){
 memset(p,0,sizeof(*p));p->noise=1;
}
float r31_opll_native_tick(const ym2413_core *s,r31_opll_native *p){
 if(!s||!p||!s->clock_hz||!s->sample_rate)return 0.0f;
 if(!p->patches[0])r31_opll_native_refresh_patches(s,p);
 p->clock_accum+=s->clock_hz;
 uint64_t divisor=72ull*s->sample_rate;
 while(p->clock_accum>=divisor){
   p->clock_accum-=divisor;
   p->last_pcm=(int32_t)native_tick(s,p);
 }
 p->samples++;
 return (float)p->last_pcm/(4096.0f*9.0f);
}

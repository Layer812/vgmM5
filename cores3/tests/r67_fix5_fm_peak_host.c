// CoreS3 R67 FIX5: REAL FM+C140 engines, synthetic stress input.
// No copyrighted song samples or user media are committed.
// Two chip mixes exercise the existing CoreS3 output path at 44.1kHz.
// These figures are not claims about any particular physical codec.
#include "fm_engine.h"
#include "pcm_engine.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FMSoundEngine fm;
static PCMSoundEngine pcm;
static uint8_t rom[65536];
static void fmw(unsigned reg,unsigned val){fm_engine_write_ym2151(&fm,(uint8_t)reg,(uint8_t)val);}
static void c140w(int ch,int reg,int val){pcm_engine_namco_write(&pcm,(uint16_t)(ch*16+reg),(uint16_t)val);}
static void init_fm(unsigned strong){
    fm_engine_init(&fm,44100,3579545,CHIP_YM2151);
    // Two mixes: common OPM algorithm 5/4/2/0 and worst-case algorithm 7.
    const uint8_t musical[8]={5,5,4,4,2,2,0,0};
    for(unsigned ch=0;ch<8;++ch){
        const unsigned algo=strong?7:musical[ch];
        fmw(0x20+ch,0xC0|((ch%4)<<3)|algo);
        fmw(0x28+ch,0x30+(ch%7)*2);
        fmw(0x30+ch,(ch%4)*16);
        for(unsigned op=0;op<4;++op){
            const unsigned off=op*8+ch;
            fmw(0x40+off,1+(op&1));
            fmw(0x60+off,strong?0:(op<2?28:12));
            fmw(0x80+off,0x1f);
            fmw(0xa0+off,0x00);
            fmw(0xc0+off,0x00);
            fmw(0xe0+off,0x0f);
        }
        fmw(0x08,0x78|ch);
    }
}
static void init_pcm(void){
    pcm_engine_init(&pcm,44100);
    pcm_engine_namco_init(&pcm,12288000,0xC1,0);
    for(unsigned i=0;i<sizeof(rom);++i)rom[i]=(uint8_t)((i*37u+(i>>5u))&0xffu);
    pcm_engine_namco_mmap(&pcm,0,rom,sizeof(rom));
    for(int ch=0;ch<22;ch++){
        const unsigned start=(unsigned)(ch*240);
        c140w(ch,0,155);c140w(ch,1,155);
        c140w(ch,2,0x39);c140w(ch,3,0x20);
        c140w(ch,4,0);
        c140w(ch,6,(start>>8)&255);c140w(ch,7,start&255);
        c140w(ch,8,0xF0);c140w(ch,9,0x00);
        c140w(ch,10,(start>>8)&255);c140w(ch,11,start&255);
        c140w(ch,5,0x90);
    }
}
static int64_t absi64(int64_t v){return v<0?-v:v;}
static void measure(unsigned strong,unsigned with_pcm){
    init_fm(strong);
    init_pcm();
    int64_t max_fm=0,max_c140=0,max_input=0,max_after_dc=0,sumout=0;
    int64_t dc_l=0,dc_r=0;
    int32_t prev_l=0,prev_r=0;
    uint32_t pre_limiter=0,hard_clipped=0;
    for(unsigned i=0;i<44100;++i){
        int32_t fl=0,fr=0,pl=0,pr=0;
        fm_engine_tick(&fm,&fl,&fr);
        if(with_pcm)pcm_engine_tick(&pcm,&pl,&pr);
        if(absi64(fl)>max_fm)max_fm=absi64(fl);
        if(absi64(pl)>max_c140)max_c140=absi64(pl);
        int32_t mix_l=(fl+pl)/(with_pcm?16:8);
        int32_t mix_r=(fr+pr)/(with_pcm?16:8);
        if(absi64(mix_l)>max_input)max_input=absi64(mix_l);
        if(absi64(mix_r)>max_input)max_input=absi64(mix_r);
        dc_l=(int64_t)(mix_l-prev_l)*16384+(dc_l*1019)/1024;
        dc_r=(int64_t)(mix_r-prev_r)*16384+(dc_r*1019)/1024;
        prev_l=mix_l;prev_r=mix_r;
        int64_t out_l=dc_l/16384, out_r=dc_r/16384;
        if(absi64(out_l)>max_after_dc)max_after_dc=absi64(out_l);
        if(absi64(out_r)>max_after_dc)max_after_dc=absi64(out_r);
        if(absi64(out_l)>26000||absi64(out_r)>26000)++pre_limiter;
        if(out_l>26000)out_l=26000+(out_l-26000)/2;
        else if(out_l< -26000)out_l= -26000+(out_l+26000)/2;
        if(out_r>26000)out_r=26000+(out_r-26000)/2;
        else if(out_r< -26000)out_r= -26000+(out_r+26000)/2;
        if(out_l>32767||out_l< -32768||out_r>32767||out_r< -32768)++hard_clipped;
        if(out_l>32767)out_l=32767;
        if(out_l< -32768)out_l= -32768;
        if(out_r>32767)out_r=32767;
        if(out_r< -32768)out_r= -32768;
        sumout+=absi64(out_l)+absi64(out_r);
        if(i==22050) {
            for(int ch=0;ch<8;ch++)fmw(0x30+ch,0x40+(ch%4)*8);
        }
    }
    printf("FM_SYNTH_%s_%s: maxFM=%lld maxC140=%lld maxMix=%lld maxDC=%lld limiter=%u/44100 hardClip=%u/44100 sumAbs=%lld\n",
           strong?"ALL_CARRIER":"MUSICAL",
           with_pcm?"FM_C140":"FM_ONLY",(long long)max_fm,
           (long long)max_c140,(long long)max_input,
           (long long)max_after_dc,pre_limiter,hard_clipped,(long long)sumout);
    if(max_fm==0||sumout==0){fputs("FAIL: mute in FM synth stress\n",stderr);exit(2);}
}
int main(void){
    measure(0,0);measure(0,1);measure(1,0);measure(1,1);
    puts("PASS CoreS3 real YM2151 core / C140 mix peak sweep (does not modify audio)");
    return 0;
}

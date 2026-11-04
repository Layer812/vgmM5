// CoreS3 FIX7 regression: C140 22-voice polyphony does not flatten
// at ±32767 on the internal chip bus before FM mixing.
// Host-only; synthetic ROM 0x7F, no copyrighted song samples.
#include "pcm_engine.h"
#include "../r67_c140_gain.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

static PCMSoundEngine e;
static uint8_t rom[0x10000];
static void put(int ch,int r,int v) {
    pcm_engine_namco_write(&e,(uint16_t)(ch*16+r),(uint16_t)v);
}
static void start_ch(int ch) {
    put(ch,0,255);
    put(ch,1,255);
    put(ch,2,0x40);
    put(ch,3,0x00);
    put(ch,4,0x00);
    put(ch,6,0x00);put(ch,7,0x00); // start 0
    put(ch,8,0x40);put(ch,9,0x00); // end 0x4000
    put(ch,10,0x00);put(ch,11,0x00); // loop 0
    put(ch,5,0x90);
}
int main(void) {
    // No change below the old 16-bit saturation knee.
    for(int32_t sum=-8192;sum<=8191;sum++){
        assert(cores3_r67_c140_to_core_mixer_unclipped(sum)
               ==cores3_r67_c140_to_core_mixer(sum));
    }
    // The old method prematurely flattens every large polyphonic crest.
    const int32_t old_10k=cores3_r67_c140_to_core_mixer(10000);
    const int32_t old_20k=cores3_r67_c140_to_core_mixer(20000);
    const int32_t old_40k=cores3_r67_c140_to_core_mixer(40000);
    assert(old_10k==old_20k && old_20k==old_40k);
    assert(cores3_r67_c140_to_core_mixer_unclipped(10000)
          <cores3_r67_c140_to_core_mixer_unclipped(20000));
    assert(cores3_r67_c140_to_core_mixer_unclipped(20000)
          <cores3_r67_c140_to_core_mixer_unclipped(40000));
    assert(cores3_r67_c140_to_core_mixer_unclipped(-20000)
          <cores3_r67_c140_to_core_mixer_unclipped(-10000));
    assert(cores3_r67_c140_to_core_mixer_unclipped(INT32_MAX)==INT32_MAX);
    assert(cores3_r67_c140_to_core_mixer_unclipped(INT32_MIN)==INT32_MIN);

    pcm_engine_init(&e,44100);
    pcm_engine_namco_init(&e,12288000,0xC1,0);
    memset(rom,0x7f,sizeof(rom));
    pcm_engine_namco_mmap(&e,0,rom,sizeof(rom));
    for(int ch=0;ch<22;ch++)start_ch(ch);

    int32_t peak=0;
    unsigned active=0;
    for(int i=0;i<4096;i++){
        int32_t l=0,r=0;
        pcm_engine_tick(&e,&l,&r);
        if(l>peak)peak=l;
        if(l!=0)active++;
        assert(l==r);
        for(int ch=0;ch<22;ch++)assert(e.namco.ch[ch].playing);
    }
    // Old pre-master clamp peak cannot exceed (32767*8/3)=87378.
    assert(peak>100000);
    assert(active>3000);
    printf("PASS CoreS3 FIX7: 22-voice C140 unclipped peak=%ld (>87378 old ceiling), active=%u; quiet gain unchanged\n",
           (long)peak,active);
    return 0;
}

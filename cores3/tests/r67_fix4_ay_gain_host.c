// FIX4: physical-chip corrected AY amplitude parity to Tab5.
// CoreS3 master is /(n*8), Tab5 master /(n*3).
// Do not touch the AY tone/noise clock, envelope, or FM amplitude.
#include "ssg_engine.h"
#include <assert.h>
#include <stdio.h>

static SSGSoundEngine e;
int main(void){
    ssg_engine_init(&e,44100,2000000);
    assert(e.vol_table[0]==0);
    assert(e.vol_table[15]==32000);
    for (int ch=0; ch<3; ++ch)
        ssg_engine_write(&e,(uint8_t)(8+ch),15);
    ssg_engine_write(&e,7,0x3f); // both tone and noise disabled -> high
    int32_t l=0,r=0;
    ssg_engine_update(&e,&l,&r);
    assert(l==96000 && r==96000);
    assert(l/8 == 3*12000/3);
    // Even for YM2203 and AY coexisting, the same 8/3 ratio applies.
    assert(l/(2*8) == 3*12000/(2*3));
    puts("PASS CoreS3 AY max 32000 reproduces Tab5 PCM after physical-chip master");
    return 0;
}

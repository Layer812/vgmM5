// CoreS3 FIX1 regression: 22 active C140 voices, three disjoint ROM
// banks as seen in the submitted 100-second VGM (also has YM2151 writes).
// Synthetic bytes only: do not commit user-provided copyrighted VGM media.
#include "pcm_engine.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t rom_a[0x7300];
static uint8_t rom_b[0xFA10];
static uint8_t rom_c[0xA760];

static void put(PCMSoundEngine *e, int ch, int r, int v) {
    pcm_engine_namco_write(e, (uint16_t)(ch * 16 + r), (uint16_t)v);
}
int main(void) {
    PCMSoundEngine e;
    pcm_engine_init(&e,44100);
    pcm_engine_namco_init(&e,12288000,0xC1,0); // uploaded VGM: C140 type 0
    for (size_t i=0;i<sizeof(rom_a);i++) rom_a[i]=(uint8_t)((i*29u+(i>>3))&0xffu);
    for (size_t i=0;i<sizeof(rom_b);i++) rom_b[i]=(uint8_t)((i*41u+(i>>4))&0xffu);
    for (size_t i=0;i<sizeof(rom_c);i++) rom_c[i]=(uint8_t)((i*37u+(i>>2))&0xffu);
    pcm_engine_namco_mmap(&e,0xC8490u,rom_a,sizeof(rom_a));
    pcm_engine_namco_mmap(&e,0xD0000u,rom_b,sizeof(rom_b));
    pcm_engine_namco_mmap(&e,0xE0000u,rom_c,sizeof(rom_c));
    for(int ch=0;ch<22;ch++){
        const int bank=0x34+(ch%3);
        const int start=(bank==0x34) ? 0x8490 : 0x10;
        const int end=start+4000;
        put(&e,ch,0,90);put(&e,ch,1,90);
        put(&e,ch,2,0x35+(ch%10));put(&e,ch,3,0x00+(ch%19));
        put(&e,ch,4,bank);
        put(&e,ch,6,start>>8);put(&e,ch,7,start&0xff);
        put(&e,ch,8,end>>8);put(&e,ch,9,end&0xff);
        put(&e,ch,10,start>>8);put(&e,ch,11,start&0xff);
        put(&e,ch,5,0x90); // key on + looping
    }
    uint32_t nonzero=0;
    int64_t abs_sum=0;
    for(int i=0;i<44100;i++){
        int32_t left=0,right=0;
        pcm_engine_tick(&e,&left,&right);
        if(left||right) nonzero++;
        abs_sum += left<0 ? -(int64_t)left : (int64_t)left;
        assert(e.namco.ch[0].playing);
        assert(e.namco.ch[21].playing);
    }
    assert(nonzero>35000);
    assert(abs_sum>10000000);
    assert(e.namco.block_count==3);
    printf("PASS CoreS3 22-voice C140 polyphony: nonzero=%u, peak load surrogate sum=%lld\n",
           (unsigned)nonzero,(long long)abs_sum);
    return 0;
}

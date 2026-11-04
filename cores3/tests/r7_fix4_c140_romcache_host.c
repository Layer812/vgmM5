// CoreS3 FIX4: compare the REAL C140 generator's decoded PCM with the
// legacy per-byte cache implementation using identical synthetic ROM data.
// Nine noncontiguous ROM regions, 22 looping channels and boundary crossings.
// Build twice: default optimized and -DVGMM5_C140_CACHE_REFERENCE=1 legacy.
// The two output fingerprints must match exactly, not just approximately.
#include "pcm_engine.h"
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#define ROM_COUNT 9
#define ROM_SIZE 0x1600
static uint8_t rom[ROM_COUNT][ROM_SIZE];

static void reg(PCMSoundEngine *e,int ch,int regno,int val) {
    pcm_engine_namco_write(e,(uint16_t)(ch*16+regno),(uint16_t)val);
}
int main(void) {
    PCMSoundEngine e;
    pcm_engine_init(&e,44100);
    pcm_engine_namco_init(&e,12288000,0xC1,0);
    for(int b=0;b<ROM_COUNT;b++) {
        for(int i=0;i<ROM_SIZE;i++)
            rom[b][i]=(uint8_t)((i*37u+(i>>5)*13u+b*47u)&255u);
        uint32_t address=0x0800u+(uint32_t)b*0x1A00u+11u;
        pcm_engine_namco_mmap(&e,address,rom[b],ROM_SIZE);
    }
    assert(e.namco.block_count==ROM_COUNT);
    for(int ch=0;ch<22;ch++) {
        const int b=ch%ROM_COUNT;
        int start=0x0800+b*0x1A00+11+((ch%5)==0?ROM_SIZE-48:0);
        int end=0x0800+b*0x1A00+11+ROM_SIZE-1;
        if((ch%5)==0) end+=64; // unmapped gap after ROM region
        reg(&e,ch,0,120-(ch%5)*8);
        reg(&e,ch,1,128-(ch%7)*6);
        reg(&e,ch,2,(0x6500+ch*443)>>8);
        reg(&e,ch,3,(0x6500+ch*443)&255);
        reg(&e,ch,4,0);
        reg(&e,ch,6,start>>8);reg(&e,ch,7,start&255);
        reg(&e,ch,8,end>>8);reg(&e,ch,9,end&255);
        reg(&e,ch,10,start>>8);reg(&e,ch,11,start&255);
        reg(&e,ch,5,0x90); // key-on + looping
    }
    uint64_t digest=14695981039346656037ull;
    uint64_t nonzero=0;
    for(int i=0;i<88200;i++) {
        int32_t left=0,right=0;
        pcm_engine_tick(&e,&left,&right);
        digest=(digest^(uint32_t)left)*1099511628211ull;
        digest=(digest^(uint32_t)right)*1099511628211ull;
        if(left!=0 || right!=0) nonzero++;
    }
    assert(nonzero>40000);
    printf("PCM_C140_FINGERPRINT=%016" PRIx64 " nonzero=%" PRIu64 "\n", digest, nonzero);
    return 0;
}

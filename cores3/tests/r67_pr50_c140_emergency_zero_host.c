// PR50 C140 22-voice adaptive budget: healthy PCM and cursor recovery parity.
// Deliberately cross unaligned ROM spans, unmapped gaps, adjacent and
// overlapping blocks while 22 voices play/loop concurrently.
// Compile against PR46 32B, PR47 32B, and experimental PR47 64B.
#include "pcm_engine.h"
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#if defined(PR49_ADAPTIVE)
void pcm_engine_cores3_c140_set_voice_budget(PCMSoundEngine *engine, unsigned limit);
#endif
#define NROM 12
#define ROM_SIZE 0x260
#define FRAMES 720000
static uint8_t rom[NROM][ROM_SIZE];
static void reg(PCMSoundEngine *e,int ch,int at,int val) {
    pcm_engine_namco_write(e,(uint16_t)(ch*16+at),(uint16_t)val);
}
static uint64_t hash_u32(uint64_t h,uint32_t v){
    return (h^(uint64_t)v)*UINT64_C(1099511628211);
}
static void fixture(PCMSoundEngine *e) {
    pcm_engine_init(e,44100);
    pcm_engine_namco_init(e,12288000,0xC1,0);
    for(int b=0;b<NROM;b++) {
        for(int i=0;i<ROM_SIZE;i++)
            rom[b][i]=(uint8_t)((b*47u+i*31u+(i>>5)*23u)&255u);
        // Paired overlapping unaligned blocks, some 0x100B-spaced
        // gaps to exercise the old byte-wise fallback.
        uint32_t start=0x1000+(uint32_t)(b/2)*0x800+
            (b%2 ? 0x1D1u : (uint32_t)(b%3)*9u);
        pcm_engine_namco_mmap(e,start,rom[b],ROM_SIZE-(b%4)*7);
    }
    for(int ch=0;ch<22;ch++) {
        const int b=(ch*7)%NROM;
        int start=0x1000+(b/2)*0x800+(b%2 ? 0x1D1 : (b%3)*9);
        // All channels roam across mapped/unmapped/overlapping
        // boundaries; use default C140 System2 address mode.
        if(ch%4==0) start += ROM_SIZE-45;
        const int end=start+0x310+(ch%3)*73;
        const int step=0x4800+(ch*613)%0x9000;
        reg(e,ch,0,98+(ch%9)*6);reg(e,ch,1,104+(ch%7)*9);
        reg(e,ch,2,step>>8);reg(e,ch,3,step&255);
        reg(e,ch,4,0);
        reg(e,ch,6,start>>8);reg(e,ch,7,start&255);
        reg(e,ch,8,end>>8);reg(e,ch,9,end&255);
        reg(e,ch,10,start>>8);reg(e,ch,11,start&255);
        reg(e,ch,5,0x90);
    }
}
int main(void) {
    PCMSoundEngine e;
    fixture(&e);
    uint64_t healthy=UINT64_C(14695981039346656037);
    uint64_t emergency=UINT64_C(14695981039346656037);
    uint64_t restored=UINT64_C(14695981039346656037);
    uint64_t samples=0;
    for(int frame=0;frame<FRAMES;frame++){
#if defined(PR49_ADAPTIVE)
        if(frame==80000) pcm_engine_cores3_c140_set_voice_budget(&e,16u);
        if(frame==140000) pcm_engine_cores3_c140_set_voice_budget(&e,8u);
        if(frame==170000) pcm_engine_cores3_c140_set_voice_budget(&e,0u);
        if(frame==210000) pcm_engine_cores3_c140_set_voice_budget(&e,4u);
        if(frame==255000) pcm_engine_cores3_c140_set_voice_budget(&e,24u);
#endif
        int32_t l=0,r=0;
        pcm_engine_tick(&e,&l,&r);
#if defined(PR49_ADAPTIVE)
        // Between frame 170000..209999 all C140 audio is dropped,
        // but every channel's position/loop/end must still advance.
        if(frame>=170000 && frame<210000) assert(l==0 && r==0);
#endif
        if(frame<80000) {
            healthy=hash_u32(healthy,(uint32_t)l);
            healthy=hash_u32(healthy,(uint32_t)r);
        } else if(frame>=80000 && frame<255000) {
            emergency=hash_u32(emergency,(uint32_t)l);
            emergency=hash_u32(emergency,(uint32_t)r);
        } else if(frame>=280000) {
            restored=hash_u32(restored,(uint32_t)l);
            restored=hash_u32(restored,(uint32_t)r);
        }
        if(l || r) ++samples;
    }
    uint64_t state=UINT64_C(14695981039346656037);
    for(int ch=0;ch<22;ch++) {
        NamcoPCMChannel *v=&e.namco.ch[ch];
        state=hash_u32(state,v->pos);
        state=hash_u32(state,v->counter);
        state=hash_u32(state,v->mode);
        state=hash_u32(state,v->playing?1u:0u);
    }
    assert(samples>450000);
    printf("PR50_HEALTHY=%016" PRIx64
           " PR50_EMERGENCY=%016" PRIx64
           " PR50_RESTORED=%016" PRIx64
           " PR50_STATE=%016" PRIx64
           " nonzero=%" PRIu64 " frames=%u\n",
           healthy,emergency,restored,state,samples,FRAMES);
    return 0;
}

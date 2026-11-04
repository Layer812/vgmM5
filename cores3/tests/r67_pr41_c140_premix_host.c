// CoreS3 PR41: Real imported Tab5 pcm_engine.c: verify that the C140
// twenty-two-voice submix is no longer clamped before the FM+PCM bus.
// Build twice (with/without IS_CORES3) using the exact same C source.
// In the QUIET region both outputs must hash identically; in the DENSE
// region CoreS3 must preserve >16-bit signed headroom while Tab5 keeps
// its untouched signed16 premix behavior.
#include "pcm_engine.h"
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

#define ROM_COUNT 9
#define ROM_SIZE 0x1600
#define FRAMES 44100
static uint8_t rom[ROM_COUNT][ROM_SIZE];
static PCMSoundEngine e;

static void reg_write(int ch,int regno,int val) {
    pcm_engine_namco_write(&e,(uint16_t)(ch*16+regno),(uint16_t)val);
}
static void start(const int volume) {
    pcm_engine_init(&e,44100);
    pcm_engine_namco_init(&e,12288000,0xC1,0);
    for(int b=0;b<ROM_COUNT;b++) {
        // A positive 8-bit PCM ROM makes early 16-bit saturation
        // unavoidable under a 22-voice sustained chord.
        for(int i=0;i<ROM_SIZE;i++) rom[b][i]=0x50;
        const uint32_t address=0x0800u+(uint32_t)b*0x1A00u+11u;
        pcm_engine_namco_mmap(&e,address,rom[b],ROM_SIZE);
    }
    assert(e.namco.block_count==ROM_COUNT);
    for(int ch=0;ch<22;ch++) {
        const int b=ch%ROM_COUNT;
        const int start_addr=0x0800+b*0x1A00+11+32;
        const int end_addr=0x0800+b*0x1A00+11+ROM_SIZE-32;
        reg_write(ch,0,volume);
        reg_write(ch,1,volume);
        reg_write(ch,2,(0x4500+ch*115)>>8);
        reg_write(ch,3,(0x4500+ch*115)&255);
        reg_write(ch,4,0);
        reg_write(ch,6,start_addr>>8);
        reg_write(ch,7,start_addr&255);
        reg_write(ch,8,end_addr>>8);
        reg_write(ch,9,end_addr&255);
        reg_write(ch,10,start_addr>>8);
        reg_write(ch,11,start_addr&255);
        reg_write(ch,5,0x90);
    }
}
static void measure(const int volume,uint64_t *digest,int32_t *peak,int *nonzero){
    start(volume);
    *digest=14695981039346656037ull;
    *peak=0;
    *nonzero=0;
    for (int i=0;i<FRAMES;i++) {
        int32_t l=0,r=0;
        pcm_engine_tick(&e,&l,&r);
        *digest=(*digest ^ (uint32_t)l)*1099511628211ull;
        *digest=(*digest ^ (uint32_t)r)*1099511628211ull;
        if (l!=0 || r!=0) ++(*nonzero);
        int32_t al=(l<0)?-l:l, ar=(r<0)?-r:r;
        if(al>*peak)*peak=al;
        if(ar>*peak)*peak=ar;
    }
}
int main(void) {
    uint64_t q=0,d=0;
    int32_t qp=0,dp=0;
    int qn=0,dn=0;
    measure(1,&q,&qp,&qn);
    measure(240,&d,&dp,&dn);
    assert(qn>FRAMES/2 && dn>FRAMES/2);
    assert(qp>0 && qp<32767);
#if defined(IS_CORES3)
    assert(dp>32767);
#else
    assert(dp<=32767);
#endif
    printf("QUIET_FINGERPRINT=%016" PRIx64 " quiet_peak=%" PRId32 "\n",q,qp);
    printf("DENSE_FINGERPRINT=%016" PRIx64 " dense_peak=%" PRId32 "\n",d,dp);
    return 0;
}

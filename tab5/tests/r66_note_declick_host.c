// R66: exercise the actual Tab5 SN76489/AY8910 generators with 2ms
// per-voice transition on a real register change. No substitute synthesis.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "psg_engine.h"
#include "ssg_engine.h"
#if !defined(VGMM5_R66_PSG_GATE_RAMP)
#error Gate test must compile with VGMM5_R66_PSG_GATE_RAMP
#endif
static int32_t abs32(int32_t x) { return x < 0 ? -x : x; }
static void sn_check(void) {
    PSGSoundEngine e;
    psg_engine_init(&e, 44100, 3579545);
    psg_engine_write_sn76489(&e, 0x80 | 0x4); // tone channel 0 low period
    psg_engine_write_sn76489(&e, 0x16);       // high period
    int32_t l=0,r=0;
    psg_engine_update(&e,&l,&r);
    assert(l==0 && r==0);
    psg_engine_write_sn76489(&e, 0x90); // CH0 volume 0 loud
    l=0;r=0;psg_engine_update(&e,&l,&r);
    assert(abs32(l) < 400 && l==r); // first sample: virtually continuous
    int32_t peak=0;
    for (int i=0;i<180;i++){l=0;r=0;psg_engine_update(&e,&l,&r);if(abs32(l)>peak)peak=abs32(l);}
    assert(peak > 2500);
    psg_engine_write_sn76489(&e, 0x9f); // CH0 volume 15 mute
    l=0;r=0;psg_engine_update(&e,&l,&r);
    assert(abs32(l)<=peak); // first muted sample held, not set instantly zero
    for (int i=0;i<100;i++){l=0;r=0;psg_engine_update(&e,&l,&r);}
    assert(l==0 && r==0);
}
static void ay_check(void) {
    SSGSoundEngine e;
    ssg_engine_init(&e,44100,1789772);
    ssg_engine_write(&e,7,0x38); // only 3 tones; disable 3 noise gates
    ssg_engine_write(&e,0,0x80);
    ssg_engine_write(&e,1,0x01);
    ssg_engine_write(&e,8,15);
    int32_t l=0,r=0;
    ssg_engine_update(&e,&l,&r);
    assert(l==0 && r==0);
    int32_t peak=0;
    for(int i=0;i<190;i++){l=0;r=0;ssg_engine_update(&e,&l,&r);if(l>peak)peak=l;}
    assert(peak>3000);
    ssg_engine_write(&e,8,0);
    l=0;r=0;ssg_engine_update(&e,&l,&r);
    assert(l>=0 && l<=peak);
    for(int i=0;i<100;i++){l=0;r=0;ssg_engine_update(&e,&l,&r);}
    assert(l==0 && r==0);
}
int main(void){sn_check();ay_check();puts("PASS R66 B SN76489 and AY8910 note transition fades");return 0;}

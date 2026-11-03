/*
 * Tab5 R62: real AY8910 + K051649 / K052539 engine regression tests.
 * Uses the actual tab5/firmware/src/ssg_engine.c and scc_engine.c;
 * no audio files are uploaded, no device flash/readback.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ssg_engine.h"
#include "scc_engine.h"
#define CHECK(c) do {if (!(c)) {fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);exit(1);}} while (0)

static void ay_noise_clock_test(void) {
    SSGSoundEngine s;
    const uint32_t clock = 1789772, rate = 44100;
    ssg_engine_init(&s,rate,clock);
    for (unsigned p=1;p<=31;p++) {
        ssg_engine_init(&s,rate,clock);
        ssg_engine_write(&s,6,(uint8_t)p);
        CHECK((unsigned)s.noise_period == p); /* hardware register, no pre-double */
        const uint64_t expected_remainder =
            ((uint64_t)s.step_size * rate) % ((uint64_t)p * 2u * 65536u);
        for(unsigned i=0;i<rate;i++) {
            int32_t l=0,r=0;
            ssg_engine_update(&s,&l,&r);
        }
        CHECK((uint64_t)s.noise_cnt == expected_remainder);
    }
    ssg_engine_write(&s,6,0);
    CHECK(s.noise_period==0); /* period=0 treated as one only when ticking */
    puts("PASS AY8910 R6 noise 1..31: one /2 prescaler, step clock /8, 44.1kHz");
}

static void scc_variant_test(void) {
    SCCSoundEngine base,plus;
    scc_engine_init(&base,44100,1789772);
    scc_engine_init(&plus,44100,1789772);
    scc_engine_set_variant(&base,false);
    scc_engine_set_variant(&plus,true);
    CHECK(!base.is_scc_plus && plus.is_scc_plus);
    // Identical command stream, solely the VGM clock flag differs:
    // SCC = four 32-byte waveform banks with channels 4 and 5 shared.
    // SCC+ = five independent banks.
    scc_engine_write(&base,0,0x60,0x21);
    scc_engine_write(&plus,0,0x60,0x21);
    CHECK(base.wave[3][0]==0x21 && base.wave[4][0]==0x21);
    CHECK(plus.wave[3][0]==0x21 && plus.wave[4][0]==0);
    scc_engine_write(&base,0,0x80,0x46);
    scc_engine_write(&plus,0,0x80,0x46);
    CHECK(base.wave[4][0]==0x21);
    CHECK(plus.wave[4][0]==0x46);
    scc_engine_write(&plus,4,0x80,0x32); /* standard SCC+ waveform port */
    CHECK(plus.wave[4][0]==0x32 && plus.wave[3][0]==0x21);
    scc_engine_write(&base,0,0x60,0x30);
    scc_engine_write(&plus,0,0x60,0x30);
    CHECK(base.wave[4][0]==0x30 && plus.wave[4][0]==0x32);
    scc_engine_write(&base,1,0,0x40);
    scc_engine_write(&plus,1,0,0x40);
    scc_engine_write(&base,2,4,0x0f);
    scc_engine_write(&plus,2,4,0x0f);
    scc_engine_write(&base,3,0,0x10);
    scc_engine_write(&plus,3,0,0x10);
    int32_t a=0,b=0,c=0,d=0;
    scc_engine_tick(&base,&a,&b);
    scc_engine_tick(&plus,&c,&d);
    CHECK(a!=c);
    printf("PASS SCC vs SCC+: CH4/CH5 shared vs independent; outputs %ld/%ld\n",(long)a,(long)c);
}
int main(void) {
    ay_noise_clock_test();
    scc_variant_test();
    puts("PASS R62 chip-specific regression (NO FLASH)");
    return 0;
}

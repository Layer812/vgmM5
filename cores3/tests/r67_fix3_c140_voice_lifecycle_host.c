// CoreS3 FIX3: real C140 engine test for Tab5-equivalent voice lifecycle.
// No copyrighted VGM bytes; use a synthetic full-scale ROM waveform.
// Key-on starts at zero history; key-off cannot leak a 25ms ghost voice.
// Preserve C352 (a separate IC) behavior unchanged.
#include "pcm_engine.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static PCMSoundEngine engine;
static uint8_t rom[0x4000];

static void put(int reg, int value) {
    pcm_engine_namco_write(&engine, (uint16_t)reg, (uint16_t)value);
}

static void begin_note(int mode) {
    put(0x00,255);
    put(0x01,255);
    put(0x02,0x40);
    put(0x03,0x00);
    put(0x04,0);
    put(0x06,0);
    put(0x07,0);
    put(0x08,0x20);
    put(0x09,0);
    put(0x0A,0);
    put(0x0B,0);
    put(0x05,mode|0x80);
}

int main(void) {
    for (unsigned i=0;i<sizeof(rom);++i)
        rom[i]=(uint8_t)((i&31u)?0x7f:0x80);
    pcm_engine_init(&engine,44100);
    pcm_engine_namco_init(&engine,12288000,0xC1,0);
    pcm_engine_namco_mmap(&engine,0,rom,sizeof(rom));

    // Start: do not preload the first +/- full-scale ROM sample.
    begin_note(0x10);
    NamcoPCMChannel *c=&engine.namco.ch[0];
    assert(c->playing);
    assert(c->prev_sample==0 && c->last_sample==0);
    assert(c->counter==0 && c->pos_frac==0);
    uint32_t nonzero=0;
    for(int i=0;i<1024;i++){
        int32_t l=0,r=0;
        pcm_engine_tick(&engine,&l,&r);
        if(l||r)nonzero++;
    }
    assert(nonzero>128);

    // No ghost tail after an explicit VGM key-off, even for a sample
    // close to full scale and with nonzero volume.
    put(0x05,0);
    assert(!c->playing);
    for(int i=0;i<2048;i++){
        int32_t l=0,r=0;
        pcm_engine_tick(&engine,&l,&r);
        assert(l==0 && r==0);
    }

    // A second key-on must never inherit PCM history from a previous note.
    begin_note(0x10);
    assert(c->playing);
    assert(c->prev_sample==0 && c->last_sample==0);
    puts("PASS CoreS3 C140 key-on zero history / immediate key-off / no 25ms tail");
    return 0;
}

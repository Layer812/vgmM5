/*
 * R7 Cardputer/CoreS3 vs original Cardputer R6.
 * Runs REAL FM and PCM source files with the SAME register writes and ROM.
 * Output is pre-speaker/chip-mix 44.1kHz stereo signed PCM: hardware speaker
 * / acoustic response is intentionally outside this deterministic comparison.
 */
#include "fm_engine.h"
#include "pcm_engine.h"
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void fm_engine_write_ym2151(FMSoundEngine *, uint16_t, uint8_t);

#if defined(IS_CORES3)
#define VERSION "cores3_vgmm5lib"
#elif defined(VGMM5_CARDPUTER_VGMM5LIB)
#define VERSION "cardputer_vgmm5lib"
#else
#define VERSION "original_cardputer_r6"
#endif

#define NFRAMES 44100
static uint8_t rom[131072];
static int32_t fm_frames[NFRAMES][2];
static int32_t c140_frames[NFRAMES][2];

typedef struct {
    uint64_t hash;
    uint64_t abs_sum;
    int32_t peak;
    unsigned nonzero;
} AudioStats;

static void observe(AudioStats *stats, int32_t l, int32_t r) {
    const int32_t samples[2] = {l, r};
    for (int k = 0; k < 2; ++k) {
        const int32_t sample = samples[k];
        const int64_t amplitude = sample < 0 ? -(int64_t)sample : (int64_t)sample;
        stats->hash = (stats->hash ^ (uint32_t)sample) * UINT64_C(1099511628211);
        stats->abs_sum += (uint64_t)amplitude;
        if (amplitude > stats->peak) stats->peak = (int32_t)amplitude;
        if (sample != 0) ++stats->nonzero;
    }
}

static void setup_opm(FMSoundEngine *e) {
    fm_engine_init(e, 44100, 3579545, CHIP_YM2151);
    for (int ch = 0; ch < 8; ++ch) {
        fm_engine_write_ym2151(e, 0x20 + ch, 0xc0 | ((ch & 3) << 3) | (ch & 7));
        fm_engine_write_ym2151(e, 0x28 + ch, 0x35 + ((ch * 5) & 0x38));
        fm_engine_write_ym2151(e, 0x30 + ch, 0x18 + ch * 4);
        fm_engine_write_ym2151(e, 0x38 + ch, 0x33);
        for (int slot = 0; slot < 4; ++slot) {
            const int off = slot * 8 + ch;
            fm_engine_write_ym2151(e, 0x40 + off, 0x01 + (slot & 3));
            fm_engine_write_ym2151(e, 0x60 + off, (slot == 3) ? 8 : 18);
            fm_engine_write_ym2151(e, 0x80 + off, 0x1f);
            fm_engine_write_ym2151(e, 0xa0 + off, 0x06);
            fm_engine_write_ym2151(e, 0xc0 + off, 0x0c);
            fm_engine_write_ym2151(e, 0xe0 + off, 0x17);
        }
        fm_engine_write_ym2151(e, 0x08, 0x78 | ch);
    }
    fm_engine_write_ym2151(e, 0x18, 0x40);
    fm_engine_write_ym2151(e, 0x19, 0x10);
    fm_engine_write_ym2151(e, 0x1b, 0x02);
}

static AudioStats run_opm(void) {
    AudioStats stats = {UINT64_C(14695981039346656037),0,0,0};
    setup_opm(&g_fm_engine);
    for (int frame = 0; frame < NFRAMES; ++frame) {
        if (frame == 11025) fm_engine_write_ym2151(&g_fm_engine, 0x08, 0);
        if (frame == 12000) fm_engine_write_ym2151(&g_fm_engine, 0x08, 0x78);
        if (frame == 24000) fm_engine_write_ym2151(&g_fm_engine, 0x28, 0x58);
        if (frame == 33000) fm_engine_write_ym2151(&g_fm_engine, 0x0f, 0x9a);
        int32_t l = 0, r = 0;
        fm_engine_tick(&g_fm_engine, &l, &r);
        fm_frames[frame][0]=l; fm_frames[frame][1]=r;
        observe(&stats,l,r);
    }
    assert(stats.nonzero > 0);
    return stats;
}

static void c140_write(PCMSoundEngine *e, int ch, int reg, int val) {
    pcm_engine_namco_write(e,(uint16_t)(ch*16+reg),(uint16_t)val);
}

static AudioStats run_c140(void) {
    AudioStats stats = {UINT64_C(14695981039346656037),0,0,0};
    for (size_t i = 0; i < sizeof(rom); ++i) {
        rom[i] = (uint8_t)((i*73u + (i >> 5)*17u + (i >> 12)*43u) & 0xffu);
    }
    pcm_engine_init(&g_pcm_engine,44100);
    pcm_engine_namco_init(&g_pcm_engine,21390000,0xc1,0);
    pcm_engine_namco_mmap(&g_pcm_engine,0,rom,sizeof(rom));
    for (int ch=0;ch<24;++ch) {
        const uint32_t start = 0x1000u + (uint32_t)ch*1811u;
        const uint32_t end = start + 1359u;
        c140_write(&g_pcm_engine,ch,0,0x60+(ch%10)*5);
        c140_write(&g_pcm_engine,ch,1,0x55+(ch%9)*6);
        c140_write(&g_pcm_engine,ch,2,0x80+(ch%7)*9);
        c140_write(&g_pcm_engine,ch,3,0x30+(ch%5)*8);
        c140_write(&g_pcm_engine,ch,4,0);
        c140_write(&g_pcm_engine,ch,6,(int)(start>>8));
        c140_write(&g_pcm_engine,ch,7,(int)(start&255));
        c140_write(&g_pcm_engine,ch,8,(int)(end>>8));
        c140_write(&g_pcm_engine,ch,9,(int)(end&255));
        c140_write(&g_pcm_engine,ch,10,(int)(start>>8));
        c140_write(&g_pcm_engine,ch,11,(int)(start&255));
        c140_write(&g_pcm_engine,ch,5,0x90);
    }
    for (int frame=0;frame<NFRAMES;++frame) {
        if (frame==NFRAMES/2) {
            for (int ch=0;ch<24;ch+=3) c140_write(&g_pcm_engine,ch,5,0x90);
        }
        int32_t l = 0,r = 0;
        pcm_engine_tick(&g_pcm_engine,&l,&r);
        c140_frames[frame][0]=l; c140_frames[frame][1]=r;
        observe(&stats,l,r);
    }
    assert(stats.nonzero > 0);
    pcm_engine_free(&g_pcm_engine);
    return stats;
}

// Reconstruct the board-side 2-chip FM+C140 master mix. The shared
// variants use the exact CoreS3 64-bit sum, /16 headroom, DC blocker
// and continuous 26000-knee limiter. Original R6 uses its old limiter.
static void write_le(FILE *fp, uint32_t val, int bytes) {
    for (int b=0;b<bytes;++b) fputc((int)((val>>(8*b))&255u),fp);
}
static int32_t soft_peak(int32_t v) {
    const int64_t knee=26000, head=6767;
    const int64_t x=v, mag=x<0?-x:x;
    if (mag<=knee) return v;
    const int64_t excess=mag-knee;
    const int64_t compressed=knee+(excess*head)/(excess+head);
    return (int32_t)(x<0?-compressed:compressed);
}
static int32_t hard_limit_i32(int64_t v) {
    return v>INT32_MAX ? INT32_MAX : v<INT32_MIN ? INT32_MIN : (int32_t)v;
}
static int16_t s16(int32_t x) {
    if(x>32767)x=32767;
    if(x<-32768)x=-32768;
    return (int16_t)x;
}
static AudioStats render_mix_wav(const char *path) {
    AudioStats stats={UINT64_C(14695981039346656037),0,0,0};
    FILE *fp=path?fopen(path,"wb"):NULL;
    if(path && !fp){perror(path);exit(2);}
    if(fp) {
        fwrite("RIFF",1,4,fp);
        write_le(fp,36+NFRAMES*4u,4);
        fwrite("WAVEfmt ",1,8,fp);
        write_le(fp,16,4); write_le(fp,1,2); write_le(fp,2,2);
        write_le(fp,44100,4); write_le(fp,44100*4u,4);
        write_le(fp,4,2); write_le(fp,16,2);
        fwrite("data",1,4,fp);
        write_le(fp,NFRAMES*4u,4);
    }
    int64_t dc[2]={0,0};
    int32_t prev[2]={0,0};
    for(int frame=0;frame<NFRAMES;++frame){
        int16_t out[2];
        for(int ch=0;ch<2;++ch){
            const int64_t sum=(int64_t)fm_frames[frame][ch]
                              +(int64_t)c140_frames[frame][ch];
#if defined(VGMM5_CARDPUTER_VGMM5LIB) || defined(IS_CORES3)
            const int32_t in=hard_limit_i32(sum/16);
            dc[ch]=((int64_t)in-(int64_t)prev[ch])*16384
                   +(dc[ch]*1019)/1024;
            int32_t sample=soft_peak(hard_limit_i32(dc[ch]/16384));
#else
            // Historical Cardputer R6 mix before the shared-library port.
            const int32_t in=(int32_t)sum/16;
            dc[ch]=((int64_t)(in-prev[ch]))*16384
                   +(dc[ch]*1019)/1024;
            int32_t sample=(int32_t)(dc[ch]/16384);
            if(sample>26000)sample=26000+(sample-26000)/2;
            else if(sample<-26000)sample=-26000+(sample+26000)/2;
#endif
            prev[ch]=in;
            out[ch]=s16(sample);
            if(fp)write_le(fp,(uint16_t)out[ch],2);
        }
        observe(&stats,out[0],out[1]);
    }
    if(fp)fclose(fp);
    assert(stats.nonzero>0);
    return stats;
}

int main(int argc, char **argv) {
    const AudioStats fm=run_opm();
    const AudioStats pcm=run_c140();
    const AudioStats mix=render_mix_wav(argc>1?argv[1]:NULL);
    printf("{\"edition\":\"%s\",\"rate\":44100,\"frames\":%d,\"fm_hash\":\"%016" PRIx64 "\","
           "\"fm_abs\":%" PRIu64 ",\"fm_peak\":%" PRId32 ",\"fm_nonzero\":%u,"
           "\"c140_hash\":\"%016" PRIx64 "\",\"c140_abs\":%" PRIu64 ","
           "\"c140_peak\":%" PRId32 ",\"c140_nonzero\":%u,"
           "\"mix_hash\":\"%016" PRIx64 "\",\"mix_abs\":%" PRIu64 ","
           "\"mix_peak\":%" PRId32 ",\"mix_nonzero\":%u}\n",
           VERSION,NFRAMES,fm.hash,fm.abs_sum,fm.peak,fm.nonzero,
           pcm.hash,pcm.abs_sum,pcm.peak,pcm.nonzero,
           mix.hash,mix.abs_sum,mix.peak,mix.nonzero);
    return 0;
}

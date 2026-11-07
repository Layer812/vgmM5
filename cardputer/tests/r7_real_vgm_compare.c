/*
 * Offline comparison with a REAL .vgm command stream (not a synthesized
 * register sequence). Uses each edition's actual fm_engine.c/pcm_engine.c.
 * YM2151 (0x54) + C140 (0xD4, ROM blocks 0x8D) only. All chip command
 * timestamps are maintained; unmatched chips are explicitly reported.
 * The three WAV stems are software PCM before device-specific DAC/speaker.
 *
 * Invocation: r7_real_vgm_compare input.vgm output_prefix [max_seconds]
 * Input is never copied into any repo or CI artifact. Convert .vgz in the
 * invoking Python helper. No copyrighted tracks are included with vgmM5.
 */
#include "fm_engine.h"
#include "pcm_engine.h"
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#if defined(VGMM5_CARDPUTER_VGMM5LIB)
#include "cardputer_audio_balance.hpp"
#endif

#define RATE 44100u
#define MAX_FILE (64u*1024u*1024u)
#define MAX_FRAMES (RATE*180u)
#define N_STEMS 3
static const char *stem_name[N_STEMS]={"fm","c140","mix"};
#if defined(IS_CORES3)
#define NAME "CoreS3_R67"
#elif defined(VGMM5_CARDPUTER_VGMM5LIB)
#define NAME "Cardputer_R7_shared"
#else
#define NAME "Cardputer_R6_original"
#endif

typedef struct {
    FILE *fp;
    uint64_t hash,abs_sum,energy;
    int32_t peak;
    uint32_t nonzero;
    int64_t dc[2];
    int32_t prev[2];
} Stem;

typedef struct {
    uint8_t *bytes;
    size_t size,logical_end,data_start,pos;
    uint32_t wait;
    uint32_t fm_clock,pcm_clock;
    uint8_t bank;
    unsigned rom_blocks,other_commands,opm_writes,pcm_writes;
    int done;
} VGM;

static uint32_t le32(const uint8_t *p){
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint16_t le16(const uint8_t *p){
    return (uint16_t)p[0]|((uint16_t)p[1]<<8);
}
static void putle(FILE *fp,uint32_t v,int n){
    for(int b=0;b<n;++b)fputc((int)(v>>(8*b)&255u),fp);
}
static void fatal(const char *msg){fprintf(stderr,"R7 VGM compare: %s\n",msg);exit(3);}
static uint8_t* readfile(const char *path,size_t *n){
    FILE *f=fopen(path,"rb");
    if(!f)fatal("Cannot open input VGM");
    if(fseek(f,0,SEEK_END)!=0)fatal("VGM seek failed");
    long sz=ftell(f);
    if(sz<0||sz<0x40||sz>(long)MAX_FILE)fatal("VGM file invalid or >64MiB");
    rewind(f);
    uint8_t *buf=(uint8_t*)malloc((size_t)sz);
    if(!buf)fatal("No RAM for VGM input");
    if(fread(buf,1,(size_t)sz,f)!=(size_t)sz)fatal("VGM read failed");
    fclose(f);
    *n=(size_t)sz;
    return buf;
}
/* VGM 1.71 reserved opcode widths match the production VGM decoder. */
static size_t command_length(uint8_t op){
    if(op>=0x30&&op<=0x3f)return 2;
    if(op>=0x40&&op<=0x4e)return 3;
    if(op==0x4f||op==0x50)return 2;
    if(op>=0x51&&op<=0x5f)return 3;
    if(op==0x61)return 3;
    if(op==0x64)return 4;
    if(op>=0x62&&op<=0x66)return 1;
    if(op==0x67)return 7;
    if(op==0x68)return 12;
    if(op>=0x70&&op<=0x8f)return 1;
    if(op==0x90||op==0x91)return 5;
    if(op==0x92)return 6;
    if(op==0x93)return 11;
    if(op==0x94)return 2;
    if(op==0x95)return 5;
    if(op>=0x96&&op<=0x9f)return 1;
    if(op>=0xa0&&op<=0xbf)return 3;
    if(op>=0xc0&&op<=0xdf)return 4;
    if(op>=0xe0&&op<=0xff)return 5;
    return 0;
}
static size_t bound_next(const VGM *v,size_t pos){
    if(pos>=v->logical_end)fatal("VGM truncated command");
    const uint8_t op=v->bytes[pos];
    size_t len=command_length(op);
    if(!len)fatal("VGM contains unrecognized command width");
    if(op==0x67){
        if(pos+7>v->logical_end||v->bytes[pos+1]!=0x66)
            fatal("VGM invalid data block");
        const uint32_t payload=le32(v->bytes+pos+3)&0x7fffffffu;
        if(payload>MAX_FILE)fatal("VGM oversized data block");
        len+=(size_t)payload;
    }
    if(len>v->logical_end-pos)fatal("VGM truncated data block or opcode");
    return pos+len;
}
static void init_vgm(VGM *v,const char *path){
    memset(v,0,sizeof(*v));
    v->bytes=readfile(path,&v->size);
    if(memcmp(v->bytes,"Vgm ",4)!=0)fatal("Input must be an uncompressed .vgm");
    const uint32_t version=le32(v->bytes+8);
    const uint32_t eof_rel=le32(v->bytes+4);
    v->logical_end=eof_rel ? (size_t)eof_rel+4:v->size;
    if(v->logical_end>v->size||v->logical_end<0x40)fatal("Invalid VGM EOF offset");
    const uint32_t data_rel=le32(v->bytes+0x34);
    v->data_start=version>=0x150&&data_rel!=0?
                 (size_t)0x34+data_rel:0x40;
    if(v->data_start>=v->logical_end)fatal("No VGM command stream");
    v->fm_clock=le32(v->bytes+0x30)&0x3fffffffu;
    if(v->size>0xac)
        v->pcm_clock=le32(v->bytes+0xa8)&0x3fffffffu;
    v->bank=v->size>0x96?v->bytes[0x96]:0;
    if(!v->fm_clock&&!v->pcm_clock)fatal("VGM has no YM2151 or C140 header clocks");
    if(v->fm_clock)fm_engine_init(&g_fm_engine,RATE,v->fm_clock,CHIP_YM2151);
    pcm_engine_init(&g_pcm_engine,RATE);
    if(v->pcm_clock)pcm_engine_namco_init(&g_pcm_engine,v->pcm_clock,0xc1,v->bank);
    /* The firmware preloads 0x8D ROM blocks before starting to render.
     * These pointers remain valid until the comparison finishes. */
    size_t p=v->data_start;
    while(p<v->logical_end){
        const size_t next=bound_next(v,p);
        const uint8_t op=v->bytes[p];
        if(op==0x67&&v->bytes[p+2]==0x8d&&v->pcm_clock){
            const uint32_t payload=le32(v->bytes+p+3)&0x7fffffffu;
            if(payload<8)fatal("C140 ROM block has no address/size header");
            const uint32_t rom_size=le32(v->bytes+p+7);
            const uint32_t address=le32(v->bytes+p+11);
            const uint32_t available=payload-8;
            if(address>rom_size||(uint64_t)address+available>rom_size)
                fatal("C140 ROM block exceeds declared ROM bounds");
            if(v->rom_blocks>=64)fatal("More than 64 C140 ROM blocks");
            pcm_engine_namco_mmap(&g_pcm_engine,address,v->bytes+p+15,available);
            ++v->rom_blocks;
        }
        if(op==0x66)break;
        p=next;
    }
    v->pos=v->data_start;
}
static void parse_commands_until_wait(VGM *v){
    for(unsigned cap=0;!v->done&&v->wait==0;++cap){
        if(cap>100000)fatal("Too many consecutive VGM commands without wait");
        if(v->pos>=v->logical_end){v->done=1;break;}
        size_t p=v->pos;
        uint8_t op=v->bytes[p];
        const size_t next=bound_next(v,p);
        v->pos=next;
        if(op==0x66){v->done=1;break;}
        if(op==0x61){v->wait=le16(v->bytes+p+1);continue;}
        if(op==0x62){v->wait=735;continue;}
        if(op==0x63){v->wait=882;continue;}
        if(op>=0x70&&op<=0x7f){v->wait=(op&15u)+1;continue;}
        if(op>=0x80&&op<=0x8f){v->wait=op&15u;++v->other_commands;continue;}
        if(op==0x54){
            if(v->fm_clock){
                fm_engine_register_write(&g_fm_engine,v->bytes[p+1],v->bytes[p+2]);
                ++v->opm_writes;
            }
            continue;
        }
        if(op==0xd4){
            if(v->pcm_clock){
                const uint16_t address=(uint16_t)(((v->bytes[p+1]&0x7fu)<<8)|v->bytes[p+2]);
                pcm_engine_namco_write(&g_pcm_engine,address,v->bytes[p+3]);
                ++v->pcm_writes;
            }
            continue;
        }
        if(op!=0x67&&op!=0x4f)++v->other_commands;
    }
}
static int32_t saturate32(int64_t x){
    return x>INT32_MAX?INT32_MAX:x<INT32_MIN?INT32_MIN:(int32_t)x;
}
static int16_t saturate16(int32_t x){
    return x>32767?32767:x<-32768?-32768:(int16_t)x;
}
static int32_t knee_limiter(int32_t x){
    const int64_t knee=26000,head=6767;
    const int64_t v=x,mag=v<0?-v:v;
    if(mag<=knee)return x;
    const int64_t ext=mag-knee;
    const int64_t y=knee+(ext*head)/(ext+head);
    return (int32_t)(v<0?-y:y);
}
static void open_stem(Stem *s,const char *prefix,const char *name,uint32_t maxframes){
    memset(s,0,sizeof(*s));
    s->hash=UINT64_C(14695981039346656037);
    char path[512];
    if(snprintf(path,sizeof(path),"%s_%s.wav",prefix,name)>=(int)sizeof(path))
        fatal("WAV output path too long");
    s->fp=fopen(path,"wb");
    if(!s->fp)fatal("Cannot create comparison output WAV");
    fwrite("RIFF",1,4,s->fp);putle(s->fp,36+maxframes*4,4);
    fwrite("WAVEfmt ",1,8,s->fp);
    putle(s->fp,16,4);putle(s->fp,1,2);putle(s->fp,2,2);
    putle(s->fp,RATE,4);putle(s->fp,RATE*4,4);
    putle(s->fp,4,2);putle(s->fp,16,2);
    fwrite("data",1,4,s->fp);putle(s->fp,maxframes*4,4);
}
static void close_stem(Stem *s,uint32_t frames){
    /* The stream may have ended before max_seconds. Patch WAV length. */
    if(fseek(s->fp,4,SEEK_SET)!=0)fatal("WAV seek error");
    putle(s->fp,36+frames*4,4);
    if(fseek(s->fp,40,SEEK_SET)!=0)fatal("WAV seek error");
    putle(s->fp,frames*4,4);
    fclose(s->fp);s->fp=NULL;
}
static void stem_frame(Stem *s,int32_t left,int32_t right,int chip_count){
    const int32_t input[2]={left,right};
    for(int ch=0;ch<2;++ch){
        const int64_t x=input[ch];
#if defined(VGMM5_CARDPUTER_VGMM5LIB) || defined(IS_CORES3)
        const int32_t in=saturate32(x/((int64_t)chip_count*8));
        s->dc[ch]=((int64_t)in-(int64_t)s->prev[ch])*16384
                  +(s->dc[ch]*1019)/1024;
        const int32_t out=knee_limiter(saturate32(s->dc[ch]/16384));
#else
        const int32_t in=(int32_t)x/(chip_count*8);
        s->dc[ch]=(int64_t)(in-s->prev[ch])*16384
                  +(s->dc[ch]*1019)/1024;
        int32_t out=(int32_t)(s->dc[ch]/16384);
        if(out>26000)out=26000+(out-26000)/2;
        else if(out<-26000)out=-26000+(out+26000)/2;
#endif
        s->prev[ch]=in;
        const int16_t sample=saturate16(out);
        const int64_t mag=sample<0?-(int64_t)sample:(int64_t)sample;
        putle(s->fp,(uint16_t)sample,2);
        s->hash=(s->hash^(uint16_t)sample)*UINT64_C(1099511628211);
        s->abs_sum+=(uint64_t)mag;
        s->energy+=(uint64_t)((int64_t)sample*sample);
        if(mag>s->peak)s->peak=(int32_t)mag;
        if(sample!=0)++s->nonzero;
    }
}
int main(int argc,char **argv){
    if(argc<3||argc>4)fatal("Usage: <input.vgm> <output-prefix> [max-seconds 1..180]");
    const int seconds=argc==4?atoi(argv[3]):30;
    if(seconds<1||seconds>180)fatal("max-seconds must be 1..180");
    VGM v;
    init_vgm(&v,argv[1]);
    const uint32_t maxframes=RATE*(uint32_t)seconds;
    Stem stems[N_STEMS];
    for(int i=0;i<N_STEMS;++i)open_stem(&stems[i],argv[2],stem_name[i],maxframes);
    const int count=(v.fm_clock?1:0)+(v.pcm_clock?1:0);
    uint32_t frames=0;
    for(;frames<maxframes;++frames){
        if(v.wait==0)parse_commands_until_wait(&v);
        if(v.done)break;
        int32_t fm[2]={0,0},pcm[2]={0,0};
        if(v.fm_clock)fm_engine_tick(&g_fm_engine,&fm[0],&fm[1]);
        if(v.pcm_clock)pcm_engine_tick(&g_pcm_engine,&pcm[0],&pcm[1]);
#if defined(VGMM5_CARDPUTER_VGMM5LIB)
        if(v.pcm_clock){
            pcm[0]=cardputer_c140_adjust(pcm[0]);
            pcm[1]=cardputer_c140_adjust(pcm[1]);
        }
#endif
        stem_frame(&stems[0],fm[0],fm[1],1);
        stem_frame(&stems[1],pcm[0],pcm[1],1);
        stem_frame(&stems[2],saturate32((int64_t)fm[0]+pcm[0]),
                   saturate32((int64_t)fm[1]+pcm[1]),count);
        if(v.wait>0)--v.wait;
    }
    for(int i=0;i<N_STEMS;++i)close_stem(&stems[i],frames);
    printf("{\"edition\":\"%s\",\"sample_rate\":44100,\"frames\":%u,"
           "\"fm_clock\":%u,\"c140_clock\":%u,\"rom_blocks\":%u,"
           "\"fm_writes\":%u,\"c140_writes\":%u,\"skipped_chip_commands\":%u,"
#if defined(VGMM5_CARDPUTER_VGMM5LIB)
           "\"cardputer_c140_gain_q8\":%d,"
#else
           "\"cardputer_c140_gain_q8\":256,"
#endif
           "\"stems\":{",NAME,frames,v.fm_clock,v.pcm_clock,v.rom_blocks,
           v.opm_writes,v.pcm_writes,v.other_commands
#if defined(VGMM5_CARDPUTER_VGMM5LIB)
           ,VGMM5_CARDPUTER_C140_GAIN_Q8
#endif
    );
    for(int i=0;i<N_STEMS;++i){
        const Stem *s=&stems[i];
        const double rms=frames?sqrt((double)s->energy/((double)frames*2.0)):0.0;
        printf("%s\"%s\":{\"fnv\":\"%016" PRIx64 "\",\"peak\":%d,"
               "\"rms\":%.4f,\"nonzero\":%u}",
               i?",":"",stem_name[i],s->hash,s->peak,rms,s->nonzero);
    }
    puts("}}");
    pcm_engine_free(&g_pcm_engine);
    free(v.bytes);
    return 0;
}

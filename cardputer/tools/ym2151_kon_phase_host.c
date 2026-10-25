/* Host-only Cardputer R5 YM2151 KEY ON assertions and offline VGM PCM renderer.
 * No music or firmware flashing. Uses the REAL Tab5 fm_engine.c linked separately.
 * Out: little-endian signed int32 stereo at 44.1kHz, before Cardputer speaker path.
 * This direct-FM rendering checks KON semantics; it is NOT Tab5 speaker output.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "fm_engine.h"

extern void fm_engine_write_ym2151(FMSoundEngine*, uint16_t, uint8_t);
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static FMSoundEngine *engine(void){
    FMSoundEngine* e=(FMSoundEngine*)calloc(1,sizeof(*e));
    if(!e){perror("alloc");exit(2);}
    fm_engine_init(e,44100,4000000,CHIP_YM2151);
    return e;
}
static void check_mask(uint8_t chan,uint8_t mask,uint8_t expected){
    FMSoundEngine *e=engine();
    fm_engine_write_ym2151(e,0x08,(uint8_t)(mask|chan));
    unsigned observed=0;
    for(unsigned i=0;i<4;i++) if(e->ops[chan*4+i].env_state==EG_ATTACK)observed|=(1u<<i);
    if(observed!=expected){
        fprintf(stderr,"KON FAIL ch=%u mask=%02x expected internal %x got %x\n",chan,mask,expected,observed);
        exit(3);
    }
    free(e);
}
static void test_keyon(void){
    /* Physical internal op[] order: M1=0,M2=1,C1=2,C2=3. */
    for(unsigned ch=0;ch<8;ch++){
        check_mask(ch,0x00,0x0);
        check_mask(ch,0x08,0x1);
        check_mask(ch,0x10,0x4);
        check_mask(ch,0x20,0x2);
        check_mask(ch,0x40,0x8);
        check_mask(ch,0x58,0xD); /* M1 + C1 + C2, M2 OFF */
        check_mask(ch,0x78,0xF);
    }
    puts("PASS YM2151 KEY ON 7 patterns x 8 channels = 56 cases; D4->C1, D5->M2.");
}
static void test_keyon_edges(void){
    for (unsigned ch=0;ch<8;ch++) {
        FMSoundEngine *e=engine();
        const unsigned idx=ch*4+2; /* C1 has KON D4 */
        OperatorState *p=&e->ops[idx];
        p->phase=0x11223344u;
        fm_engine_write_ym2151(e,0x08,(uint8_t)(ch|0x10));
        if (p->phase!=0 || p->env_state!=EG_ATTACK || !p->key_on) {
            fprintf(stderr,"KON rising edge phase/attack mismatch ch=%u\n",ch);exit(20);
        }
        p->phase=0x55667788u;
        p->env_state=EG_DECAY;
        fm_engine_write_ym2151(e,0x08,(uint8_t)(ch|0x10));
        if (p->phase!=0x55667788u || p->env_state!=EG_DECAY) {
            fprintf(stderr,"Repeated KON must NOT restart phase/attack ch=%u\n",ch);exit(21);
        }
        fm_engine_write_ym2151(e,0x08,(uint8_t)ch);
        if (p->env_state!=EG_RELEASE || p->key_on) {
            fprintf(stderr,"KON falling edge release mismatch ch=%u\n",ch);exit(22);
        }
        p->phase=0xaabbccddu;
        fm_engine_write_ym2151(e,0x08,(uint8_t)(ch|0x10));
        if (p->phase!=0 || p->env_state!=EG_ATTACK || !p->key_on) {
            fprintf(stderr,"KON reattack phase mismatch ch=%u\n",ch);exit(23);
        }
        free(e);
    }
    puts("PASS YM2151 8 channels x 4 edge/phase checks = 32 cases.");
}
static void emit(FMSoundEngine *e,FILE *out,unsigned n,uint64_t *elapsed){
    int32_t lr[2];
    if(*elapsed+n>44100ull*120ull){fputs("VGM duration safety cap\n",stderr);exit(4);}
    for(unsigned i=0;i<n;i++){
        lr[0]=lr[1]=0;
        fm_engine_tick(e,&lr[0],&lr[1]);
        if(fwrite(lr,sizeof(int32_t),2,out)!=2){perror("raw write");exit(5);}
    }
    *elapsed+=n;
}
int main(int argc,char**argv){
    if(argc==2 && strcmp(argv[1],"--test")==0){test_keyon();test_keyon_edges();return 0;}
    if(argc!=3){fputs("usage: host input.vgm output_int32_stereo.raw OR host --test\n",stderr);return 2;}
    FILE *f=fopen(argv[1],"rb");if(!f){perror("vgm file");return 3;}
    if(fseek(f,0,SEEK_END)){return 4;}long n=ftell(f);rewind(f);
    if(n<0x100||n>40000000){fputs("VGM size not supported\n",stderr);return 5;}
    uint8_t *v=(uint8_t*)malloc((size_t)n);if(!v)return 6;
    if(fread(v,1,n,f)!=(size_t)n)return 7;fclose(f);
    if(memcmp(v,"Vgm ",4)||!(u32(v+0x30)&0x3fffffff)){fputs("Expected YM2151 VGM\n",stderr);return 8;}
    uint32_t clock=u32(v+0x30)&0x3fffffffu;
    if(clock!=4000000){fprintf(stderr,"Expected 4MHz OPM, got %u\n",clock);return 9;}
    uint32_t at=0x34 + u32(v+0x34);
    if(at>= (uint32_t)n||at<0x40)return 10;
    FILE*out=fopen(argv[2],"wb");if(!out)return 11;
    FMSoundEngine *e=engine();
    uint64_t elapsed=0; unsigned writes=0,partial=0,events=0;
    while(at<(uint32_t)n){
        uint8_t op=v[at++];events++;
        if(op==0x66)break;
        if(op==0x54){
            if(at+2>(uint32_t)n)return 12;
            uint8_t reg=v[at++],val=v[at++];
            fm_engine_write_ym2151(e,reg,val);writes++;
            if(reg==0x08 && (val&0x78)!=0x78 && (val&0x78)!=0)partial++;
        }else if(op==0x61){
            if(at+2>(uint32_t)n)return 13;
            unsigned wait=v[at]|((unsigned)v[at+1]<<8);at+=2;
            emit(e,out,wait,&elapsed);
        }else if(op==0x62)emit(e,out,735,&elapsed);
        else if(op==0x63)emit(e,out,882,&elapsed);
        else if(op>=0x70 && op<=0x7f)emit(e,out,(op&15)+1,&elapsed);
        else if(op==0x4f||op==0x50){at+=1;}
        else if((op>=0x51&&op<=0x5f)||(op>=0xa0&&op<=0xbf)){at+=2;}
        else if(op==0x67){if(at+6>(uint32_t)n||v[at]!=0x66)return 14;at+=2;uint32_t bytes=u32(v+at);at+=4+bytes;}
        else if(op==0x68){at+=11;}
        else if(op>=0x80&&op<=0x8f){emit(e,out,op&15,&elapsed);}
        else if(op>=0xc0&&op<=0xdf){at+=3;}
        else if(op>=0xe0){at+=4;}
        else {fprintf(stderr,"Unexpected VGM command %02x\n",op);return 15;}
        if(at>(uint32_t)n)return 16;
    }
    fclose(out);free(e);free(v);
    fprintf(stderr,"PASS VGM render frames=%llu, commands=%u, YMwrites=%u, partialKON=%u\n",
      (unsigned long long)elapsed,events,writes,partial);
    return 0;
}

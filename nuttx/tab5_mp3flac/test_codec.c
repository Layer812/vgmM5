// Host-only test harness. NuttX target uses vgmm5_decode_file() directly.
#include "vgmm5_nuttx_codec.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef struct {
  FILE* out;
  uint64_t frames;
  uint32_t peak;
  uint32_t rate;
} test_t;
static int receive(void* opaque,const int16_t* pcm,size_t frames,uint32_t hz) {
  test_t* t=(test_t*)opaque;
  if(hz!=48000||!frames||frames>512)return -1;
  for(size_t i=0;i<frames*2;i++) {
    int32_t v=pcm[i];if(v<0)v=-v;
    if((uint32_t)v>t->peak)t->peak=(uint32_t)v;
  }
  t->frames+=frames;
  t->rate=hz;
  if(t->out && fwrite(pcm,sizeof(int16_t),frames*2,t->out)!=frames*2)
    return -1;
  return 0;
}
int main(int argc,char** argv) {
  if(argc<2||argc>3){fprintf(stderr,"usage: %s INPUT.mp3|flac [OUTPUT.raw]\n",argv[0]);return 2;}
  test_t t={0};
  if(argc==3){t.out=fopen(argv[2],"wb");if(!t.out){perror(argv[2]);return 3;}}
  vgmm5_decode_stats stats={0};
  int rc=vgmm5_decode_file(argv[1],48000,receive,NULL,&t,&stats);
  if(t.out)fclose(t.out);
  printf("rc=%d format=%s source=%u Hz channels=%u output=%u Hz frames=%llu peak=%u\n",
         rc,stats.flac?"FLAC":"MP3",stats.source_hz,stats.channels,
         stats.output_hz,(unsigned long long)stats.frames_output,t.peak);
  if(rc||stats.frames_output<8000||stats.frames_output>96000||
     !t.peak||stats.frames_output!=t.frames)return 1;
  return 0;
}

// CoreS3 R7 real helper regression. Two mutually exclusive output
// platforms (ES8388 module / internal Speaker) both consume same PCM frames.
#include "../cores3_r7_audio_helpers.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
int main(){
  const uint32_t empty[4]={0,0,0,0};
  const uint32_t opn[3]={8000000u,0,0};
  assert(cores3_physical_chip_count(0,empty,4)==1);
  assert(cores3_physical_chip_count(3579545u,empty,4)==1);
  assert(cores3_physical_chip_count(0x40000000u | 3579545u,empty,4)==2);
  assert(cores3_physical_chip_count(0,opn,3)==1);
  assert(cores3_physical_chip_count(3579545u,opn,3)==2);
  uint32_t state=0xA56959A5u;
  int pos=0,neg=0;
  for(int i=0;i<44100;++i){
    int16_t l=0,r=0;
    cores3_fill_silent_pcm(&l,&r,&state);
    assert((l==-1||l==1)&&(r==-1||r==1));
    if(l==1)++pos;else ++neg;
  }
  assert(pos>10000 && neg>10000);
  int16_t l=2500,r=-9000;
  const uint32_t previous=state;
  cores3_fill_silent_pcm(&l,&r,&state);
  assert(l==2500 && r==-9000 && state==previous);
  l=0;r=-450;
  cores3_fill_silent_pcm(&l,&r,&state);
  assert(l==0 && r==-450 && state==previous);
  puts("PASS CoreS3 physical-chip count + exact-silence-only 1LSB output");
  return 0;
}

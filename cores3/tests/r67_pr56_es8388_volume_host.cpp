#include "../r67_ui_controls.hpp"
#include <cassert>
#include <cstdio>
#include <cstdint>

int main() {
  for (int vol=0;vol<=255;vol++){
    const uint8_t hw=cores3_r67_es8388_volume_register((uint8_t)vol);
    assert(hw<=33);
    if(vol==0) assert(hw==0);
    else assert(hw>=1);
    if(vol>0) assert(hw>=cores3_r67_es8388_volume_register((uint8_t)(vol-1)));
  }
  assert(cores3_r67_es8388_volume_register(255)==33);
  assert(cores3_r67_es8388_volume_register(185)==23);
  struct Case {int x,y,oldvol,newvol;bool handled,up;};
  const Case cases[]={
    { 8,26,185,215,true,true}, {167,102,185,215,true,true},
    { 8,103,185,155,true,false},{167,179,185,155,true,false},
    { 8,26,255,255,true,true},{8,103,0,0,true,false},
    { 8,26,252,255,true,true},{8,103,14,0,true,false},
    { 7,50,185,185,false,false},{168,50,185,185,false,false},
    {12,25,185,185,false,false},{12,180,185,185,false,false}
  };
  for(const auto& x:cases){
    auto got=cores3_r67_thumbnail_volume_tap(x.x,x.y,(uint8_t)x.oldvol);
    assert(got.handled==x.handled);
    assert((int)got.volume==x.newvol);
    if(got.handled)assert(got.increase==x.up);
  }
  std::puts("PASS PR56 ES8388 0..255 output mapping, exact full mute and perceivable +/-30 thumb volume; all hitbox bounds");
  return 0;
}

#include "../r67_ui_controls.hpp"
#include <assert.h>
#include <stdio.h>
static void check(int x,int y,int current,int handled,int next,int up) {
    auto t=cores3_r67_thumbnail_volume_tap(x,y,(uint8_t)current);
    assert((int)t.handled==handled);
    assert((int)t.volume==next);
    if(handled) assert((int)t.increase==up);
}
int main() {
    check(8,26,128,1,138,1);
    check(167,102,128,1,138,1);
    check(8,103,128,1,118,0);
    check(167,179,128,1,118,0);
    check(7,26,128,0,128,0);
    check(168,50,128,0,128,0);
    check(8,25,128,0,128,0);
    check(8,180,128,0,128,0); // transport bar begins here
    check(15,210,128,0,128,0);
    check(16,26,255,1,255,1);
    check(16,175,0,1,0,0);
    check(10,101,250,1,255,1);
    check(10,104,5,1,0,0);
    puts("PASS CoreS3 R67 thumbnail up/down, bounds, clamp, transport toolbar");
    return 0;
}

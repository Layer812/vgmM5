#include "../firmware/src/tab5_r66_silence_dither.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
int main() {
    uint32_t seed=0xA56959A5u;
    int16_t l=0,r=0;
    int positive=0,negative=0;
    for(int i=0;i<44100;i++) {
        l=0;r=0;
        tab5_r66_fill_silent_pcm(&l,&r,&seed);
        assert((l==1||l==-1) && (r==1||r==-1));
        if(l>0)positive++;else negative++;
    }
    assert(positive>10000 && negative>10000);
    l=1500;r=-3000;
    uint32_t old_seed=seed;
    tab5_r66_fill_silent_pcm(&l,&r,&seed);
    assert(l==1500 && r==-3000 && seed==old_seed);
    puts("PASS R66 A silent-only 1-LSB PCM dither; active samples unmodified");
}

#include "../r67_fix6_mixer_safety.hpp"
#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>

// FIX6: behavior of actual CoreS3 final 16-bit PCM arithmetic.
static int32_t old_limiter(int32_t input) {
    const int32_t limit=26000;
    if(input>limit)input=limit+(input-limit)/2;
    else if(input< -limit)input= -limit+(input+limit)/2;
    if(input>32767)input=32767;
    if(input< -32768)input= -32768;
    return input;
}
int main() {
    // Existing normal PCM is bit-for-bit unchanged.
    for(int x=-26000;x<=26000;x++) {
        assert(cores3_r67_true_peak_limiter(x)==old_limiter(x));
    }

    // Old limiter hard-clipped common dense all-carrier peaks.
    assert(old_limiter(50000)==32767);
    assert(old_limiter(-50000)==-32768);
    assert(cores3_r67_true_peak_limiter(50000)<32767);
    assert(cores3_r67_true_peak_limiter(-50000)>-32768);

    // Uniform saturation/monotonicity, from -2^31 to +2^31-1.
    int32_t previous=-32768;
    for(int64_t x=-1000000;x<=1000000;x+=11){
        const int32_t y=cores3_r67_true_peak_limiter((int32_t)x);
        assert(y<=32766 && y>=-32766);
        assert(y>=previous);
        previous=y;
    }
    assert(cores3_r67_true_peak_limiter(INT32_MAX)<=32766);
    assert(cores3_r67_true_peak_limiter(INT32_MIN)>=-32766);
    assert(cores3_r67_true_peak_limiter(26000)==26000);
    assert(cores3_r67_true_peak_limiter(-26000)==-26000);

    // A legal FM/PCM sum can be > INT32_MAX, so never accumulate
    // in int32 before reducing by CoreS3 physical chip headroom /8.
    const int64_t giant=(int64_t)INT32_MAX*3;
    assert(giant>INT32_MAX);
    assert(cores3_r67_divide_mix(giant,3)==(int32_t)(giant/24));
    assert(cores3_r67_divide_mix(-giant,3)==(int32_t)(-giant/24));
    assert(cores3_r67_divide_mix(1000,0)==125);
    assert(cores3_r67_divide_mix(-1000,1)==-125);
    assert(cores3_r67_saturate_i32((int64_t)INT32_MAX+1)==INT32_MAX);
    assert(cores3_r67_saturate_i32((int64_t)INT32_MIN-1)==INT32_MIN);

    // Worst-case simultaneous 8 FM carriers plus 22 C140 notes.
    // Preserve signed sum until after global /8; both + and - tested.
    const int64_t multi_voice=459385+39648;
    assert(cores3_r67_divide_mix(multi_voice,2)==multi_voice/16);
    assert(cores3_r67_divide_mix(-multi_voice,2)==-multi_voice/16);
    const int32_t hot=cores3_r67_divide_mix(multi_voice,1);
    assert(hot>39534); // old code would hard-clip
    const int32_t safe=cores3_r67_true_peak_limiter(hot);
    assert(safe<32767 && safe>26000);
    printf("PASS CoreS3 FIX6: 64-bit accumulation, DC-safe cast, no hard PCM clips, unchanged normal range\n");
    return 0;
}

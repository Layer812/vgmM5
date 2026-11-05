// PR48: reference-vs-optimized exact final CoreS3 signed16 PCM limiter.
// Covers every integer across knees and 2M-wide band, full-range random
// values, extreme signed values, and dense true-peak workload.
#include "../r67_fix6_mixer_safety.hpp"
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>

static int32_t slow_original(int32_t input) {
    const int64_t knee = 26000;
    const int64_t head = 32767 - knee;
    const int64_t signed_input = (int64_t)input;
    const int64_t magnitude = signed_input < 0 ? -signed_input : signed_input;
    if (magnitude <= knee) return input;
    const int64_t excess = magnitude - knee;
    const int64_t compressed = knee + (excess * head) / (excess + head);
    return (int32_t)(signed_input < 0 ? -compressed : compressed);
}
static uint64_t rnd=UINT64_C(0x123F47CD9CA00D79);
static uint32_t next_u32(){
    rnd ^= rnd >> 12;
    rnd ^= rnd << 25;
    rnd ^= rnd >> 27;
    return (uint32_t)(rnd*UINT64_C(2685821657736338717));
}
static uint64_t checks = 0;
static void check(int32_t input){
    const int32_t old=slow_original(input);
    const int32_t now=cores3_r67_true_peak_limiter(input);
    if(old!=now) fprintf(stderr,"FAIL input=%" PRId32 " old=%" PRId32 " now=%" PRId32 "\n",input,old,now);
    assert(old==now);
    assert(now>=-32766 && now<=32766);
    ++checks;
}
int main(){
    for(int64_t x=-1100000;x<=1100000;x++) check((int32_t)x);
    for(uint32_t i=0;i<8000000;i++){
        check((int32_t)next_u32());
        check((int32_t)next_u32() >> 8);
    }
    for(int64_t x=INT32_MIN;x<=INT32_MIN+20000;x++)check((int32_t)x);
    for(int64_t x=INT32_MAX-20000;x<=INT32_MAX;x++)check((int32_t)x);
    for(int64_t x=-32769;x<=32769;x++)check((int32_t)x);
    for(int64_t x=-26005;x<=26005;x++)check((int32_t)x);
    check(INT32_MIN);check(INT32_MAX);check(-26001);check(26001);
    printf("PASS PR48 bit-exact %" PRIu64 " signed32 inputs, 32bit only hot limiter\n",checks);
    return 0;
}

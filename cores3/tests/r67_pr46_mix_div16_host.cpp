// CoreS3 PR46: exhaustive edge and broad random signed-64 parity.
// Verify the actual CoreS3 R67 divider and final true-peak PCM limiter,
// including INT64_MIN, values not divisible by 16 and saturating sums.
#include "../r67_fix6_mixer_safety.hpp"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>

static uint64_t seed = UINT64_C(0x635D981234ABCD9F);
static uint64_t next_random() {
    seed ^= seed >> 12;
    seed ^= seed << 25;
    seed ^= seed >> 27;
    return seed * UINT64_C(2685821657736338717);
}
static uint64_t checks = 0;
static void check(int64_t sum, int chips) {
    const int32_t expected=cores3_r67_divide_mix(sum,chips);
    const int32_t actual=cores3_r67_divide_mix_fast2(sum,chips);
    if (expected != actual) {
        fprintf(stderr, "FAIL sum=%lld chips=%d ref=%ld opt=%ld\n",
            (long long)sum,chips,(long)expected,(long)actual);
        assert(expected == actual);
    }
    assert(cores3_r67_true_peak_limiter(expected)==
           cores3_r67_true_peak_limiter(actual));
    ++checks;
}
int main() {
    static const int64_t edges[] = {
        INT64_MIN, INT64_MIN+1, INT64_MIN+2, INT64_MIN+15,
        -(int64_t)INT32_MAX*16-31,-(int64_t)INT32_MAX*16-16,
        -(int64_t)INT32_MAX*16-15,-(int64_t)INT32_MAX*16,
        -(int64_t)INT32_MAX*16+1,-INT32_MAX,-32769,-32768,
        -26001,-26000,-17,-16,-15,-9,-8,-7,-2,-1,0,
        1,2,7,8,9,15,16,17,26000,26001,32767,32768,
        32769,INT32_MAX,(int64_t)INT32_MAX*16-1,
        (int64_t)INT32_MAX*16,(int64_t)INT32_MAX*16+1,
        INT64_MAX-15,INT64_MAX-1,INT64_MAX
    };
    const int chips[]={-1,0,1,2,3,4,7,8,16,31,100};
    for (unsigned c=0;c<sizeof(chips)/sizeof(chips[0]);++c) {
        for (unsigned i=0;i<sizeof(edges)/sizeof(edges[0]);++i)
            check(edges[i],chips[c]);
    }
    for (int64_t i=-1048576; i<=1048576; ++i) check(i,2);
    // Full-width randomized signed sums, plus realistic FM+C140
    // +/- 2^25 22-voice output values, and other-chip fallback.
    for (uint32_t i=0;i<2500000u;++i) {
        const int64_t x=(int64_t)next_random();
        check(x,2);
        check((int64_t)((int32_t)next_random() >> 6),2);
        check(x,(i&1u)?3:1);
    }
    printf("PASS CoreS3 PR46: %llu signed64 cases and exact downstream limiter parity\n",
        (unsigned long long)checks);
    return 0;
}

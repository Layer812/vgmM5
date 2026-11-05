// CoreS3 PR54: exact stereo DC-blocker waveform parity vs original.
#include "../r67_pr54_dc32_exact.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <inttypes.h>
#include <limits.h>

static int64_t legacy_decay(int64_t x) {
    return (x * INT64_C(1019)) / INT64_C(1024);
}
static uint64_t seed = UINT64_C(0x94ae818674c5d56d);
static uint32_t rnd() {
    seed ^= seed >> 12; seed ^= seed << 25; seed ^= seed >> 27;
    return static_cast<uint32_t>(seed * UINT64_C(2685821657736338717));
}
static uint64_t checks=0;
static void check_state(int64_t x) {
    const int64_t old=legacy_decay(x), now=cores3_pr54_dc_decay(x);
    if(old!=now) {
        fprintf(stderr,"Mismatch x=%" PRId64 " old=%" PRId64 " now=%" PRId64 "\n",x,old,now);
        abort();
    }
    ++checks;
}
static void check_stereo_stream() {
    int64_t left=0, right=0, ref_left=0, ref_right=0;
    int32_t prev_l=0, prev_r=0,ref_prev_l=0,ref_prev_r=0;
    uint64_t checksum=UINT64_C(14695981039346656037);
    // 100 seconds @ 44,100fps: every synthesized final PCM frame.
    // Both original and PR54 paths see identical signed32 values.
    for(int i=0;i<4410000;i++) {
        int32_t in_l=(int32_t)(rnd()%65536u)-32768;
        int32_t in_r=(int32_t)(rnd()%65536u)-32768;
        ref_left=((int64_t)in_l-ref_prev_l)*16384+legacy_decay(ref_left);
        ref_right=((int64_t)in_r-ref_prev_r)*16384+legacy_decay(ref_right);
        left=((int64_t)in_l-prev_l)*16384+cores3_pr54_dc_decay(left);
        right=((int64_t)in_r-prev_r)*16384+cores3_pr54_dc_decay(right);
        ref_prev_l=prev_l=in_l;ref_prev_r=prev_r=in_r;
        assert(left==ref_left && right==ref_right);
        checksum=(checksum^(uint64_t)left)*UINT64_C(1099511628211);
        checksum=(checksum^(uint64_t)right)*UINT64_C(1099511628211);
        ++checks;
    }
    printf("PR54_STEREO_DIGEST=%016" PRIx64 " frames=4410000\n",checksum);
}
int main() {
    for(int64_t v=-2000000;v<=2000000;v++)check_state(v);
    constexpr int64_t edge=(UINT32_MAX-1023u)/5u;
    for(int64_t v=edge-5000;v<=edge+5000;v++){
        check_state(v); check_state(-v);
    }
    for(uint32_t i=0;i<2000000u;i++){
        const int64_t v=static_cast<int64_t>(rnd())-INT64_C(0x80000000);
        check_state(v*1000LL);
        check_state(v);
    }
    // Original signed64 expression is defined in this range.
    for(uint32_t i=0;i<200000u;i++){
        int64_t v=(int64_t)(rnd()%500000000u)*1000000LL;
        check_state(v);
        check_state(-v);
    }
    check_stereo_stream();
    printf("PASS PR54 exact DC output: %" PRIu64 " tested signed states/frames\n",checks);
    return 0;
}

// R67 CoreS3 C140: numeric parity with Tab5 including 16-bit
// saturation before the different core-wide master headroom. No VGM
// media is needed and no other chip is affected.
#include "../r67_c140_gain.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int32_t tab5_c140_final(int32_t sum) {
    int32_t mixed = (sum > 8191) ? 32767 :
                    (sum < -8192) ? -32768 : sum * 4;
    return mixed;
}

static void check(int32_t sum, int chip_count) {
    const int32_t core_after_master =
       cores3_r67_c140_to_core_mixer(sum) / (chip_count*8);
    const int32_t tab_after_master =
       tab5_c140_final(sum) / (chip_count*3);
    const int32_t diff = core_after_master - tab_after_master;
    assert(diff >= -1 && diff <= 1);
}

int main(void) {
    assert(cores3_r67_c140_linear_pcm(0) == 0);
    assert(cores3_r67_c140_linear_pcm(1) == 16);
    assert(cores3_r67_c140_linear_pcm(0x7f) == 2032);
    assert(cores3_r67_c140_linear_pcm(0x80) == -2048);
    assert(cores3_r67_c140_linear_pcm(0xff) == -16);
    assert(cores3_r67_c140_to_core_mixer(0) == 0);
    assert(cores3_r67_c140_to_core_mixer(8192) == (32767*8)/3);
    assert(cores3_r67_c140_to_core_mixer(-8193) == (-32768*8)/3);
    // Sweeping the clipped and unclipped regions for solo and FM+C140.
    for (int32_t sum=-24000; sum<=24000; sum+=13) {
        for (int n=1;n<=4;n++) check(sum,n);
    }
    check(24*2032, 2); // dense high-polyphony positive full-scale
    check(24*-2048, 2); // dense high-polyphony negative full-scale
    printf("PASS C140 12-bit linear and pre-master saturation matches Tab5 within 1 LSB\n");
    return 0;
}

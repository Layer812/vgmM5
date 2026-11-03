#include "../firmware/src/tab5_vgm_mix_tail.hpp"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>
int main() {
    const uint32_t none[] = {0,0,0,0};
    assert(tab5::vgm_physical_chip_count(0,none,4) == 1);
    assert(tab5::vgm_physical_chip_count(3579545,none,4) == 1);
    assert(tab5::vgm_physical_chip_count(0x40000000u|3579545u,none,4) == 2);
    const uint32_t ym2612[] = {7670454u,0,0};
    assert(tab5::vgm_physical_chip_count(0,ym2612,3) == 1);
    // YM2608's FM/SSG/PCM wrappers must remain ONE physical chip.
    const uint32_t opna[] = {8000000u,0,0};
    assert(tab5::vgm_physical_chip_count(0,opna,3) == 1);
    const uint32_t fm_and_sn[] = {7670454u,0,0};
    assert(tab5::vgm_physical_chip_count(3579545u,fm_and_sn,3) == 2);
    // Original SN and C140 master divisor stays /3; one YM2612/OPNA
    // is not divided by 2 or 3 solely because of software wrappers.
    std::vector<int16_t> pcm(512 * 2, 20000);
    pcm[0] = -32768;
    pcm[1] = 32767;
    tab5::vgm_fade_final_pcm_block(pcm.data(), 512);
    assert(pcm[0] == -32768 && pcm[1] == 32767);
    assert(pcm[2*255] == 20000 && pcm[2*255+1] == 20000);
    assert(pcm[2*256] == 20000 && pcm[2*256+1] == 20000);
    assert(pcm[2*511] == 0 && pcm[2*511+1] == 0);
    for (int i = 257; i < 512; ++i)
       assert(pcm[2*i] <= pcm[2*(i-1)] && pcm[2*i] >= 0);
    std::vector<int16_t> tiny = {32767,-32768};
    tab5::vgm_fade_final_pcm_block(tiny.data(),1);
    assert(tiny[0] == 0 && tiny[1] == 0);
    printf("PASS R65 physical chip count and final block declick, no synth changes\n");
    return 0;
}

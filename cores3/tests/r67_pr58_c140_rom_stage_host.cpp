// PR58: prove that moving whole C140 ROM banks to owned internal-memory
// copies changes neither PCM samples nor C140 channel state.
#include <pcm_engine.h>
#include "../r67_pr58_c140_rom_stage.hpp"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static constexpr int kBlocks = 9;
static constexpr size_t kMaxRom = 65536;
static const uint32_t kSizes[kBlocks] =
    {324u, 9608u, 14149u, 20200u, 23688u, 29444u, 42868u, 45000u, 64024u};
static uint8_t rom[kBlocks][kMaxRom];
static size_t pretend_free = 256u * 1024u;

static void reg(PCMSoundEngine *e, int ch, int address, int value) {
    pcm_engine_namco_write(e, (uint16_t)(ch * 16 + address), (uint16_t)value);
}

static void init_fixture(PCMSoundEngine *e) {
    pcm_engine_init(e, 44100);
    pcm_engine_namco_init(e, 12288000, 0xC1, 0);
    for (int b = 0; b < kBlocks; ++b) {
        for (uint32_t i = 0; i < kSizes[b]; ++i)
            rom[b][i] = (uint8_t)((i * 37u + (i >> 5) * 13u + b * 47u) & 255u);
        // Intentional overlapping address ranges, unaligned ROM pages.
        const uint32_t address = 0x1000u + (uint32_t)b * 0x1900u + 11u;
        pcm_engine_namco_mmap(e, address, rom[b], kSizes[b]);
    }
    assert(e->namco.block_count == kBlocks);
}

static uint64_t digest_run(bool stage, bool reject_alloc) {
    PCMSoundEngine *e = (PCMSoundEngine *)malloc(sizeof(*e));
    assert(e);
    init_fixture(e);
    pretend_free = 256u * 1024u;
    if (stage) {
        const auto result = cores3_c140_stage_small_rom(
            e, 64u*1024u, 96u*1024u,
            []() -> size_t { return pretend_free; },
            [reject_alloc](size_t n) -> void * {
                if (reject_alloc) return nullptr;
                void *p = malloc(n);
                if (p) pretend_free -= n;
                return p;
            });
        if (reject_alloc) {
            assert(result.blocks == 0 && result.bytes == 0);
            assert(result.alloc_failures == kBlocks);
        } else {
            // Smallest-first: 324 + 9608 + 14149 + 20200 = 44281 bytes.
            assert(result.blocks == 4 && result.bytes == 44281u);
            assert(result.alloc_failures == 0);
        }
        for (int b = 0; b < kBlocks; ++b) {
            const auto *block = &e->namco.blocks[b];
            assert(block->size == kSizes[b]);
            assert(memcmp(block->data, rom[b], kSizes[b]) == 0);
            assert(block->is_allocated == (!reject_alloc && b < 4));
        }
    }
    for (int ch = 0; ch < 24; ++ch) {
        const int b = (ch * 7) % kBlocks;
        const uint32_t base = 0x1000u + (uint32_t)b * 0x1900u + 11u;
        const uint32_t start = base + (uint32_t)(ch % 4) * 37u;
        const uint32_t end = start + 850u;
        const uint32_t step = 0x5600u + (uint32_t)ch * 341u;
        reg(e,ch,0,110+(ch%8)*6);
        reg(e,ch,1,100+(ch%7)*7);
        reg(e,ch,2,step>>8);
        reg(e,ch,3,step&255u);
        reg(e,ch,4,0);
        reg(e,ch,6,start>>8);
        reg(e,ch,7,start&255u);
        reg(e,ch,8,end>>8);
        reg(e,ch,9,end&255u);
        reg(e,ch,10,start>>8);
        reg(e,ch,11,start&255u);
        reg(e,ch,5,0x90);
    }
    uint64_t digest = UINT64_C(14695981039346656037);
    for (int frame = 0; frame < 220500; ++frame) {
        int32_t left=0,right=0;
        pcm_engine_tick(e,&left,&right);
        digest = (digest ^ (uint32_t)left) * UINT64_C(1099511628211);
        digest = (digest ^ (uint32_t)right) * UINT64_C(1099511628211);
        if (frame % 1024 == 0) {
            for (int ch = 0; ch < 24; ++ch) {
                digest = (digest ^ e->namco.ch[ch].pos) * UINT64_C(1099511628211);
                digest = (digest ^ (uint32_t)e->namco.ch[ch].current_block) * UINT64_C(1099511628211);
            }
        }
    }
    pcm_engine_free(e); // must free ONLY internally-owned promoted ROM banks
    free(e);
    return digest;
}

int main() {
    const uint64_t direct = digest_run(false,false);
    const uint64_t staged = digest_run(true,false);
    const uint64_t alloc_failure = digest_run(true,true);
    assert(direct == staged && staged == alloc_failure);

    PCMSoundEngine *c352 = (PCMSoundEngine *)malloc(sizeof(*c352));
    assert(c352);
    pcm_engine_init(c352,44100);
    pcm_engine_namco_init(c352,24000000,0xC2,0);
    const auto no_stage = cores3_c140_stage_small_rom(
        c352, 64u*1024u, 96u*1024u,
        []() -> size_t {return 256u*1024u;},
        [](size_t n) -> void * {return malloc(n);});
    assert(no_stage.blocks == 0 && no_stage.bytes == 0);
    pcm_engine_free(c352);
    free(c352);
    printf("PASS PR58 24-voice C140 original/staged/fallback exact PCM+state digest=%016llx; C352 untouched\\n",
           (unsigned long long)direct);
    return 0;
}

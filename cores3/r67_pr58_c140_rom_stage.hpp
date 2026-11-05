#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// CoreS3 PR58: at song-load time only, copy a bounded number of whole
// C140 ROM blocks from PSRAM/Flash-MMAP into internal 8-bit SRAM.
// The original VGM and its 0x67 block payloads are never changed.
// pcm_engine_free() already frees NamcoROMBlock::data when is_allocated.
// This helper is allocator-agnostic to allow exact portable host tests.
struct CoreS3C140RomStageResult {
    uint32_t blocks;
    uint32_t bytes;
    uint32_t alloc_failures;
};

template<class FreeBytes, class Allocator>
static inline CoreS3C140RomStageResult cores3_c140_stage_small_rom(
    PCMSoundEngine *engine, size_t max_stage_bytes, size_t reserve_internal_bytes,
    FreeBytes get_free_internal, Allocator allocate_internal) {
    CoreS3C140RomStageResult result = {0, 0, 0};
    if (!engine || !engine->c140_enabled || engine->c352_enabled)
        return result;

    // Existing NamcoROMBlock data pointers and first-matching address
    // precedence are preserved; only their physical backing changes.
    uint8_t attempted[64] = {0};
    const uint32_t count = engine->namco.block_count < 64u
                         ? engine->namco.block_count : 64u;
    while ((size_t)result.bytes < max_stage_bytes) {
        const size_t free_now = get_free_internal();
        if (free_now <= reserve_internal_bytes)
            break;
        size_t can_stage = max_stage_bytes - result.bytes;
        const size_t spare = free_now - reserve_internal_bytes;
        if (can_stage > spare) can_stage = spare;

        // Smallest-first: more independent C140 banks can avoid external
        // memory with a fixed, conservative internal-memory budget.
        int best = -1;
        size_t best_size = (size_t)-1;
        for (uint32_t i = 0; i < count; ++i) {
            const NamcoROMBlock *b = &engine->namco.blocks[i];
            if (attempted[i] || b->is_allocated || !b->data ||
                b->size == 0 || (size_t)b->size > can_stage)
                continue;
            if ((size_t)b->size < best_size) {
                best = (int)i;
                best_size = b->size;
            }
        }
        if (best < 0) break;
        attempted[best] = 1;

        NamcoROMBlock *b = &engine->namco.blocks[best];
        uint8_t *copy = (uint8_t *)allocate_internal(best_size);
        if (!copy) {
            ++result.alloc_failures;
            continue;
        }
        memcpy(copy, b->data, best_size);
        b->data = copy;
        b->is_allocated = true; // existing pcm_engine_free() takes ownership
        ++result.blocks;
        result.bytes += (uint32_t)best_size;
    }
    return result;
}

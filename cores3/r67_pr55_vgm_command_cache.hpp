#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// CoreS3 PR55: small explicit L1 staging buffer for sequential VGM commands.
// The *source* remains PSRAM / Flash MMAP; only command bytes are copied
// into internal DRAM. C140/C352 ROM block pointers still point at the
// original (unmodified) source data.
//
// Keep this structure POD so a static DRAM_ATTR instance has no constructor.
// The caller checks position < source_size before calling read().
// Reads across pages, 0x67 data skips and loop seeks work automatically;
// no stateful parser or timing changes are required.
template<size_t PageSize = 1024>
struct CoreS3VgmCommandCache {
    uint8_t bytes[PageSize];
    uint32_t first;
    uint32_t count;

    inline void reset() {
        first = 0;
        count = 0;
    }

    inline uint8_t read(uint32_t position,
                        const uint8_t* source,
                        uint32_t source_size) {
        // The subtraction is only evaluated for positions at/after first.
        // A backward loop seek (or forward 0x67 skip) triggers a refill
        // if it leaves the currently staged region.
        if (position < first || position - first >= count) {
            first = position;
            const uint32_t remaining = source_size - position;
            count = remaining < PageSize ? remaining : PageSize;
            memcpy(bytes, source + position, count);
        }
        return bytes[position - first];
    }
};

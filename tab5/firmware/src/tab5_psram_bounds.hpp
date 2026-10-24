#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace tab5 {
// Subtraction avoids overflow in offset+length, even for hostile gzip headers.
inline bool range_fits(size_t offset, size_t length, size_t capacity) {
    return offset <= capacity && length <= capacity - offset;
}
struct CheckedRamSink { unsigned char* data; size_t capacity; size_t used; };
inline int checked_ram_append(void* ctx, const unsigned char* source, unsigned long len) {
    auto* sink = static_cast<CheckedRamSink*>(ctx);
    if (!sink || !source || !sink->data ||
        !range_fits(sink->used, (size_t)len, sink->capacity)) return -1;
    memcpy(sink->data + sink->used, source, (size_t)len);
    sink->used += (size_t)len;
    return 0;
}
inline bool pdx_sample_range(uint32_t offset, uint32_t length, uint32_t file_size) {
    return offset >= 768 && offset <= file_size && length <= file_size - offset;
}
inline bool decoded_pdx_fits(uint64_t bytes_so_far, uint32_t adpcm_bytes, size_t limit) {
    return bytes_so_far <= static_cast<uint64_t>(limit) &&
           static_cast<uint64_t>(adpcm_bytes) * 4u <= static_cast<uint64_t>(limit) - bytes_so_far;
}
}

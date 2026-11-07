#pragma once
#include <stddef.h>

// PR60: the I2S driver may return fewer bytes than requested. A ring
// slot must remain owned by the writer until EVERY byte is accepted.
// This small state machine contains no ESP-IDF dependency so a host
// regression can use the exact production transfer logic.
struct CoreS3I2SPendingBlock {
    size_t accepted = 0;

    void reset() { accepted = 0; }

    template<typename Writer>
    bool advance(size_t total_bytes, Writer writer) {
        if (accepted >= total_bytes) return true;
        const size_t remaining = total_bytes - accepted;
        size_t written = writer(accepted, remaining);
        if (written > remaining) written = remaining;
        accepted += written;
        return accepted == total_bytes;
    }
};

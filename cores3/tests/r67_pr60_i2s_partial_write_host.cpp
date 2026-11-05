// CoreS3 PR60: verify exact byte stream when legacy I2S writes short/zero.
// This is deliberately the same state machine used by AudioPlayTask.
#include "../r67_pr60_i2s_partial_write.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

static constexpr size_t kBytesPerBlock = 512u * 2u * 2u;
static constexpr int kBlocks = 10000;

int main() {
    std::array<uint8_t, kBytesPerBlock> source{};
    std::vector<uint8_t> emitted;
    emitted.reserve(kBlocks * kBytesPerBlock);
    CoreS3I2SPendingBlock pending;
    uint64_t expected = UINT64_C(14695981039346656037);
    uint64_t got = UINT64_C(14695981039346656037);
    int zero_writes = 0;
    int partial_writes = 0;

    for (int block = 0; block < kBlocks; ++block) {
        for (size_t i = 0; i < source.size(); ++i) {
            source[i] = static_cast<uint8_t>((block * 13u + i * 37u) & 0xffu);
            expected = (expected ^ source[i]) * UINT64_C(1099511628211);
        }
        size_t n_attempts = 0;
        bool delivered = false;
        while (!delivered) {
            // The actual PCM ring slot cannot be consumed before complete
            // submission; pending.accepted survives each partial DMA write.
            delivered = pending.advance(source.size(),
                [&](size_t offset, size_t remaining) -> size_t {
                    ++n_attempts;
                    size_t transfer = remaining;
                    if (n_attempts % 7 == 1) transfer = 0;
                    else if (n_attempts % 3 == 0 && transfer > 64) transfer = 64;
                    else if (n_attempts % 3 == 1 && transfer > 512) transfer = 512;
                    if (transfer == 0) ++zero_writes;
                    if (transfer < remaining) ++partial_writes;
                    emitted.insert(emitted.end(), source.begin() + offset,
                                   source.begin() + offset + transfer);
                    return transfer;
                });
            assert(pending.accepted <= source.size());
            if (!delivered) assert(pending.accepted < source.size());
            assert(n_attempts < 30);
        }
        assert(pending.accepted == source.size());
        pending.reset();
        assert(pending.accepted == 0);
    }
    assert(emitted.size() == kBlocks * kBytesPerBlock);
    for (uint8_t b : emitted) got = (got ^ b) * UINT64_C(1099511628211);
    assert(expected == got);
    assert(zero_writes > 0 && partial_writes > 0);

    // Stopping or starting another song must never retain a half-sent
    // offset into the new song's buffer.
    assert(!pending.advance(source.size(),
        [](size_t, size_t) -> size_t { return 256u; }));
    assert(pending.accepted == 256u);
    pending.reset();
    assert(pending.accepted == 0u);
    assert(pending.advance(source.size(),
        [](size_t, size_t remaining) -> size_t { return remaining; }));
    pending.reset();

    // Drivers must never be allowed to overrun a PCM block even if
    // a faulty stub reports more bytes than requested.
    assert(pending.advance(source.size(),
        [](size_t, size_t remaining) -> size_t { return remaining + 64; }));
    assert(pending.accepted == source.size());

    std::printf("PASS PR60 exact DMA PCM order, %d blocks, 10000 zero/partial retries; FNV=%016llx, partial=%d, zero=%d\n",
                kBlocks,(unsigned long long)got,partial_writes,zero_writes);
}

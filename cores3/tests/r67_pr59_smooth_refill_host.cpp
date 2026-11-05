// PR59: host-level verification of FIFO order and refill burst reduction.
// No fake DMA timing claims: this checks scheduling/ownership decisions only.
#include "../r67_pr59_smooth_refill.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>

static constexpr int kSlots = 16;
static constexpr int kHighWater = kSlots - 4;
static constexpr int kFrames = 100000;

struct Result {
    uint64_t digest = UINT64_C(14695981039346656037);
    int max_batch = 0;
    int producer_wakeups = 0;
    int empty_count = 0;
};

static Result run(bool smoothed) {
    std::array<int, kSlots> fifo{};
    int rd = 0, wd = 0, count = 0, sequence = 0;
    Result result;

    const auto produce = [&]() {
        assert(count >= 0 && count < kHighWater);
        const int previous = count;
        fifo[wd] = sequence++;
        wd = (wd + 1) % kSlots;
        ++count;
        assert(cores3_r67_pr59_wake_i2s_writer(previous) == (previous == 0));
    };
    while (count < kHighWater) produce();

    for (int frame = 0; frame < kFrames; ++frame) {
        if (count == 0) { ++result.empty_count; break; }
        const int pcm = fifo[rd];
        rd = (rd + 1) % kSlots;
        --count;
        assert(pcm == frame);
        result.digest = (result.digest ^ static_cast<uint32_t>(pcm))
                      * UINT64_C(1099511628211);

        const bool request = smoothed
            ? cores3_r67_pr59_request_refill(count, kSlots)
            : count <= kSlots / 2;
        if (request) {
            ++result.producer_wakeups;
            int batch = 0;
            while (count < kHighWater) { produce(); ++batch; }
            if (batch > result.max_batch) result.max_batch = batch;
        }
    }
    return result;
}

int main() {
    assert(cores3_r67_pr59_request_refill(11, kSlots));
    assert(!cores3_r67_pr59_request_refill(12, kSlots));
    assert(!cores3_r67_pr59_request_refill(13, kSlots));
    assert(cores3_r67_pr59_wake_i2s_writer(0));
    assert(!cores3_r67_pr59_wake_i2s_writer(1));

    auto before = run(false), after = run(true);
    assert(before.empty_count == 0 && after.empty_count == 0);
    assert(before.digest == after.digest);
    assert(before.max_batch == 4 && after.max_batch == 1);
    assert(after.producer_wakeups > before.producer_wakeups);

    // A block is published BEFORE a waiting reader is notified.
    int occupied = 0;
    int notifications = 0;
    for (int i = 0; i < 3; ++i) {
        const int previous = occupied++;
        if (cores3_r67_pr59_wake_i2s_writer(previous)) ++notifications;
    }
    assert(occupied == 3 && notifications == 1);
    occupied = 0;
    if (cores3_r67_pr59_wake_i2s_writer(occupied++)) ++notifications;
    assert(notifications == 2);

    std::printf(
        "PASS PR59 FIFO/PCM digest=%016llx; max producer batch old=%d new=%d; "
        "producer wakeups old=%d new=%d; no empties, empty->nonempty notify once\\n",
        (unsigned long long)after.digest,
        before.max_batch, after.max_batch,
        before.producer_wakeups, after.producer_wakeups);
}

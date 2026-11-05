#pragma once

// PR59: smooth the CoreS3 producer/DMA-consumer rendezvous.
// High water remains 12 of 16 blocks; request a refill after the FIRST
// consumed block instead of waiting until four have been consumed.
// No PCM arithmetic or DMA format changes.
static inline bool cores3_r67_pr59_request_refill(int occupied, int ring_size) {
    return occupied < ring_size - 4;
}

// Only signal CPU0 on an actual empty -> nonempty transition.
// An already-running I2S writer needs no extra per-block wakeups.
static inline bool cores3_r67_pr59_wake_i2s_writer(int occupied_before_produce) {
    return occupied_before_produce == 0;
}

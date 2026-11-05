#pragma once
#include <stdint.h>

// CoreS3 PR50: queue-depth-only C140 audio continuity policy.
// The producer runs while the ring has <14 blocks, waking whenever
// the consumer has drained it to <=12. At 44.1kHz each 512-frame
// block represents ~11.6ms of audio. Start reducing C140 voices
// BEFORE the queue is nearly empty, not at 2 blocks as in PR49.
// No wall-clock access, samples dropped or VGM commands skipped.
static inline unsigned cores3_pr50_c140_budget_target(int queued) {
    if (queued <= 1) return 0u;   // last resort: preserve FM at all costs
    if (queued <= 3) return 4u;
    if (queued <= 5) return 8u;
    if (queued <= 7) return 12u;
    if (queued <= 9) return 16u;
    if (queued <= 11) return 20u;
    return 24u; // full fidelity, healthy queue of >=12 blocks
}

// Small conservative recovery rate: don't bounce repeatedly between
// full 22-voice C140 load and emergency modes on a wav_count edge.
static inline unsigned cores3_pr50_update_c140_budget(
    int queued, unsigned current, unsigned *recovery_blocks) {
    const unsigned target=cores3_pr50_c140_budget_target(queued);
    if (target < current) {
        *recovery_blocks=0u;
        return target;
    }
    if (target > current && queued >= 12) {
        if (++*recovery_blocks >= 32u) {
            *recovery_blocks=0u;
            const unsigned next=current+4u;
            return next < target ? next : target;
        }
    } else {
        *recovery_blocks=0u;
    }
    return current;
}

static inline bool cores3_pr50_should_generate(int queued) {
    return queued < 14;
}

static inline bool cores3_pr50_wake_producer(int queued) {
    return queued <= 12;
}

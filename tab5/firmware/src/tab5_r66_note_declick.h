#pragma once
#include <stdint.h>
// R66 experimental note-boundary crossfade. Only compiled for the GATE variant.
// Keep frequency, phase, volume registers and the original chip cores unchanged.
// A register discontinuity smoothly transitions from the previous rendered
// contribution to the new contribution over 88 samples (~2ms at Tab5 44.1kHz).
typedef struct {
    int32_t previous;
    int32_t from;
    uint16_t remaining;
} tab5_r66_note_fade;

static inline void tab5_r66_note_begin(tab5_r66_note_fade *state) {
    state->from = state->previous;
    state->remaining = 88u;
}

static inline int32_t tab5_r66_note_sample(tab5_r66_note_fade *state, int32_t raw) {
    if (!state->remaining) {
        state->previous = raw;
        return raw;
    }
    const uint32_t old_weight = state->remaining;
    const int64_t value = (int64_t)state->from * old_weight +
                          (int64_t)raw * (88u - old_weight);
    const int32_t blended = (int32_t)(value / 88);
    --state->remaining;
    state->previous = blended;
    return blended;
}

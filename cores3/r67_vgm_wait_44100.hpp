#pragma once
#include <stdint.h>

// CoreS3's output and VGM timebases are both exactly 44,100 samples/sec.
// This is the algebraic equivalent of the existing accumulator + division
// loop WHEN actual_sample_rate == 44100 AND vgm_time_acc == 0.
//
// The two process() calls are intentional: if the parser emits a one-sample
// wait, the old code consumes it and processes the NEXT command in the same
// output sample. Preserve that ordering for FM/C140 register writes.
// No audio-chip source or register value is modified here.
template <typename Process>
static inline void cores3_vgm_step_44100(
    uint32_t& wait_samples, volatile bool& playing, Process process) {
    if (!playing) return;
    if (wait_samples == 0) process();
    if (playing && wait_samples != 0) --wait_samples;
    if (playing && wait_samples == 0) process();
}

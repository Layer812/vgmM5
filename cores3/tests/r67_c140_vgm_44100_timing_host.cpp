// Bit-exact scheduling regression for the real CoreS3 44.1kHz wait helper.
// Replay 100 seconds (4,410,000 stereo frames) of dense synthetic FM/C140
// writes through the OLD accumulator algorithm and the NEW constant-rate
// step. Compare EVERY sample's wait, playing status and VGM register-write
// fingerprint (including the exact sample index of each write).
#include "../r67_vgm_wait_44100.hpp"
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

static volatile uint32_t sample_rate = 44100;
struct Sim {
    volatile bool playing = true;
    uint32_t wait = 8820;
    uint32_t acc = 0;
    uint32_t seed = 0x65A7B1CDu;
    uint32_t events = 0;
    uint32_t calls = 0;
    uint32_t frame = 0;
    uint64_t digest = 14695981039346656037ull;
};

static void process_vgm(Sim &s) {
    ++s.calls;
    static const uint16_t delay_samples[16] = {
        0, 0, 1, 1, 2, 7, 12, 16, 19, 24, 32, 42, 67, 83, 25, 26
    };
    // processVGM() consumes consecutive register writes and 0-wait commands
    // until reaching a nonzero wait, just like the real VGM interpreter.
    for (int i=0; i<128 && s.playing; ++i) {
        s.seed ^= s.seed << 13;
        s.seed ^= s.seed >> 17;
        s.seed ^= s.seed << 5;
        ++s.events;
        // Use the frame index so an off-by-one command dispatch mismatches.
        const uint64_t event = ((uint64_t)s.frame << 32) |
                              ((uint64_t)(s.events & 65535) << 16) |
                              ((uint64_t)(s.seed & 255) << 8) |
                              (uint64_t)((s.seed >> 16) & 255);
        s.digest = (s.digest ^ event) * 1099511628211ull;
        const uint32_t delay = delay_samples[s.seed & 15u];
        if (delay) { s.wait = delay; return; }
    }
    // Command-limit fallback not expected for this deterministic input.
    s.playing = false;
}

static void legacy_step(Sim &s) {
    s.acc += 44100;
    while (s.acc >= sample_rate && s.playing) {
        if (s.wait > 0) {
            uint32_t consume = s.acc / sample_rate;
            if (consume > s.wait) consume = s.wait;
            s.wait -= consume;
            s.acc -= consume * sample_rate;
        }
        if (s.wait == 0 && s.playing) {
            process_vgm(s);
            if (s.wait == 0) continue;
        }
    }
}

int main() {
    Sim original, optimized;
    for (uint32_t frame = 0; frame < 4410000u; ++frame) {
        original.frame = optimized.frame = frame;
        legacy_step(original);
        cores3_vgm_step_44100(
            optimized.wait, optimized.playing,
            [&optimized]() { process_vgm(optimized); });
        if (original.wait != optimized.wait ||
            original.playing != optimized.playing ||
            original.calls != optimized.calls ||
            original.events != optimized.events ||
            original.digest != optimized.digest) {
            fprintf(stderr,
                    "MISMATCH frame=%u old_wait=%u new_wait=%u events=%u/%u digest=%016" PRIx64 "/%016" PRIx64 "\n",
                    frame, original.wait, optimized.wait, original.events,
                    optimized.events, original.digest, optimized.digest);
            return 1;
        }
        assert(original.acc == 0); // invariant required by app fast path
    }
    assert(original.events >= 100000u);
    assert(original.events == optimized.events);
    printf("PASS CoreS3 44.1k VGM 100s exact event schedule: frames=4410000 commands=%u parser_calls=%u fingerprint=%016" PRIx64 "\n",
           original.events, original.calls, original.digest);
    return 0;
}

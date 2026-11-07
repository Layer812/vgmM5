#pragma once
#include <stdint.h>

// CoreS3 320x240 R67 playback layout: the album art is drawn at
// x=8..167 and y=26..179. y>=180 belongs to the transport toolbar.
struct CoreS3R67VolumeTap {
    bool handled;
    uint8_t volume;
    bool increase;
};
static inline CoreS3R67VolumeTap cores3_r67_thumbnail_volume_tap(
    int x, int y, uint8_t current_volume) {
    if (x < 8 || x >= 168 || y < 26 || y >= 180)
        return {false, current_volume, false};
    const bool increase = y < 103; // two equal 77-pixel halves
    // PR56: +/-10 barely changes ES8388's 0..33 analog-gain scale.
    // +/-30 makes each tap audibly distinct without touching PCM math.
    int new_volume = (int)current_volume + (increase ? 30 : -30);
    if (new_volume < 0) new_volume = 0;
    if (new_volume > 255) new_volume = 255;
    return {true, (uint8_t)new_volume, increase};
}

// Module Audio analog-output registers 0x2E..0x31: 0..0x21.
// Returns an exact bounded mapping, also exercised by native host tests.
static inline uint8_t cores3_r67_es8388_volume_register(uint8_t volume) {
    if (volume == 0u) return 0u;
    const unsigned v = ((unsigned)volume * 33u) / 255u;
    return (uint8_t)(v == 0u ? 1u : v);
}

#pragma once
#include <cstddef>
#include <cstdint>
namespace tab5 {
// Observes existing vgmM5 audio settings; never starts a second I2S/codec owner.
struct AudioContract {
 uint32_t actual_rate_hz;
 uint16_t frames_per_block;
 uint16_t stereo_channels;
 uint8_t bytes_per_sample;
 bool m5_speaker_owned_by_engine;
 bool tab5_codec_route_verified;
};
constexpr AudioContract kOriginalStereo44100 = {44100,512,2,2,true,false};
constexpr uint32_t payload_bytes(const AudioContract& c) {
 return uint32_t(c.frames_per_block)*c.stereo_channels*c.bytes_per_sample;
}
constexpr bool valid(const AudioContract& c) {
 return c.actual_rate_hz==44100 && c.stereo_channels==2 && c.bytes_per_sample==2 &&
        c.frames_per_block==512 && c.m5_speaker_owned_by_engine;
}
// Requires post-BSP / device validation before a real Tab5 playback build.
constexpr bool ready_for_physical_audio(const AudioContract& c) {
 return valid(c) && c.tab5_codec_route_verified;
}
} // namespace tab5

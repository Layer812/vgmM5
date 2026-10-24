#pragma once
#include <cstddef>
#include <cstdint>
namespace tab5 {
constexpr std::size_t kMaxChannels = 128;
constexpr std::size_t kPageChannels = 16;
constexpr std::size_t kChipLabel = 20;
enum class VoiceClass : uint8_t { FM, PSG, SSG, PCM, ADPCM, Rhythm, Noise, Other };
enum class VoiceState : uint8_t { Unknown, Silent, KeyOn, Active };
struct Channel {
  char chip[kChipLabel] = {};
  VoiceClass type = VoiceClass::Other;
  uint8_t chip_instance = 0;
  uint8_t chip_channel = 0;
  VoiceState state = VoiceState::Unknown;
  int16_t note = -1;    // MIDI note, -1 if unknown. Do not synthesize note from mix output.
  uint8_t level = 0;    // 0..255, 0 when unobserved; state Unknown distinguishes unavailable data.
  int8_t pan = 0;       // -1 left / 0 center or unknown / 1 right
};
struct Snapshot {
  Channel channels[kMaxChannels] = {};
  std::size_t count = 0;
  uint32_t generation = 0;
  bool overflow = false;
};
// Observer-only container. The audio/guest engine must explicitly publish measured data.
// No analysis of PCM output and no fabricated channel readings.
// A single owner publishes; render thread must take its own synchronized copy if needed.
class ChannelMonitor {
public:
  void begin_frame();
  bool append(const Channel& c);
  void end_frame();
  const Snapshot& snapshot() const { return published_; }
  static std::size_t pages(const Snapshot& s);
  static std::size_t page_offset(const Snapshot& s, std::size_t page);
  static std::size_t page_count(const Snapshot& s, std::size_t page);
private:
  Snapshot staging_ = {};
  Snapshot published_ = {};
};
} // namespace tab5

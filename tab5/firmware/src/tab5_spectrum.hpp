#pragma once
// R8 UI telemetry: non-blocking, lossy snapshots of REAL submitted stereo PCM.
// No dynamic allocation, no FFT or floating-point work on the audio core.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace tab5 {
class SpectrumTap final {
public:
  static constexpr std::size_t kSize = 256;
  void publish_if_due(const int16_t* stereo, std::size_t frames);
  bool consume(int16_t* dst);
private:
  // 0=free, 1=writer copying, 2=ready, 3=UI reading.
  std::atomic<unsigned> ownership_{0};
  unsigned blocks_{0}; // written from the one audio consumer task only
  alignas(4) int16_t frame_[kSize]{};
};
extern SpectrumTap g_spectrum_tap;

class SpectrumAnalyzer final {
public:
  static constexpr int kBars = 24;
  void analyze(const int16_t* pcm);
  const std::array<uint8_t,kBars>& levels() const { return levels_; }
private:
  std::array<uint8_t,kBars> levels_{};
};
} // namespace tab5

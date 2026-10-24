#pragma once
// R17: UI-only peak hold for the real PCM FFT levels. Zero audio task changes.
#include <array>
#include <algorithm>
#include <cstdint>
#include "tab5_spectrum.hpp"
namespace tab5 {
class SpectrumPeakHold final {
 public:
  static constexpr uint32_t kHoldMs=500;
  static constexpr uint32_t kDecayUnitsPerSecond=150;
  void update(const std::array<uint8_t,SpectrumAnalyzer::kBars>& levels,
              bool active,uint32_t now) {
    if(!active){reset();return;}
    for(int i=0;i<SpectrumAnalyzer::kBars;++i){
      const uint8_t level=levels[i];
      if(level>=peaks_[i]) {
        peaks_[i]=level;
        raise_ms_[i]=now;
        step_ms_[i]=now;
      } else if((uint32_t)(now-raise_ms_[i])>kHoldMs) {
        // The initial 500 ms is a true HOLD: never charge that time as decay.
        // Subtract unsigned times so rollover at 2^32 ms is also supported.
        if((uint32_t)(step_ms_[i]-raise_ms_[i])<kHoldMs)
          step_ms_[i]=raise_ms_[i]+kHoldMs;
        const uint32_t elapsed=(uint32_t)(now-step_ms_[i]);
        const uint32_t fall=std::min<uint32_t>(elapsed,1000u)*kDecayUnitsPerSecond/1000u;
        if(fall>0){
          peaks_[i]=(uint8_t)std::max<int>(level,(int)peaks_[i]-(int)std::min<uint32_t>(fall,255u));
          step_ms_[i]=now;
        }
      }
    }
  }
  const std::array<uint8_t,SpectrumAnalyzer::kBars>& peaks() const {return peaks_;}
  void reset(){peaks_.fill(0);raise_ms_.fill(0);step_ms_.fill(0);}
 private:
  std::array<uint8_t,SpectrumAnalyzer::kBars> peaks_{};
  std::array<uint32_t,SpectrumAnalyzer::kBars> raise_ms_{};
  std::array<uint32_t,SpectrumAnalyzer::kBars> step_ms_{};
};
} // namespace tab5

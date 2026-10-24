#include "tab5_spectrum.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace tab5 {
SpectrumTap g_spectrum_tap;

void SpectrumTap::publish_if_due(const int16_t* stereo, std::size_t frames) {
  if (++blocks_ < 8) return;   // one frame per approx 93ms @ 44100/512
  blocks_ = 0;
  if (!stereo || frames < kSize * 2) return;
  unsigned expected=0;
  if (!ownership_.compare_exchange_strong(expected,1,std::memory_order_acquire)) return;
  // Downsample 44.1 kHz to 22.05 kHz, L/R mix with safe 32-bit sum.
  for (std::size_t i=0;i<kSize;++i) {
    const std::size_t j=i*4;
    frame_[i] = static_cast<int16_t>((static_cast<int32_t>(stereo[j])+
                                       static_cast<int32_t>(stereo[j+1]))/2);
  }
  ownership_.store(2,std::memory_order_release);
}

bool SpectrumTap::consume(int16_t* dst) {
  if(!dst)return false;
  unsigned expected=2;
  if(!ownership_.compare_exchange_strong(expected,3,std::memory_order_acquire))return false;
  std::memcpy(dst,frame_,sizeof(frame_));
  ownership_.store(0,std::memory_order_release);
  return true;
}

void SpectrumAnalyzer::analyze(const int16_t* pcm) {
  constexpr unsigned n=SpectrumTap::kSize;
  constexpr float pi=3.14159265358979323846f;
  // 24 log-spaced contiguous groups of FFT bins, 86Hz..11kHz at fs=22.05kHz.
  static constexpr unsigned edges[kBars+1] = {
    1,2,3,4,5,6,7,8,9,11,13,16,19,23,28,34,41,49,59,71,84,98,112,121,129
  };
  float real[n], imag[n];
  if(!pcm){levels_.fill(0);return;}
  // Hann window. Static precompute runs exclusively on the UI core.
  static float hann[n];
  static bool initialized=false;
  if(!initialized) {
    for(unsigned i=0;i<n;++i)hann[i]=0.5f-0.5f*cosf(2*pi*i/(n-1));
    initialized=true;
  }
  for(unsigned i=0;i<n;++i){
    real[i]=pcm[i]*(hann[i]/32768.0f); imag[i]=0;
  }
  for(unsigned i=1,j=0;i<n;++i){
    unsigned bit=n>>1;
    for(;j&bit;bit>>=1)j^=bit;
    j^=bit;
    if(i<j){std::swap(real[i],real[j]);std::swap(imag[i],imag[j]);}
  }
  for(unsigned len=2;len<=n;len<<=1){
    const float angle=-2.0f*pi/len;
    const float ur=cosf(angle),ui=sinf(angle);
    for(unsigned base=0;base<n;base+=len){
      float wr=1.0f,wi=0.0f;
      for(unsigned j=0;j<len/2;++j){
        const unsigned a=base+j,b=a+len/2;
        const float tr=wr*real[b]-wi*imag[b],ti=wr*imag[b]+wi*real[b];
        real[b]=real[a]-tr;imag[b]=imag[a]-ti;
        real[a]+=tr;imag[a]+=ti;
        const float r=wr*ur-wi*ui;
        wi=wr*ui+wi*ur;wr=r;
      }
    }
  }
  for(int i=0;i<kBars;++i){
    float power=0.0f;
    for(unsigned b=edges[i];b<edges[i+1];++b){
      const float p=real[b]*real[b]+imag[b]*imag[b];
      if(p>power)power=p;
    }
    // 4/n corrects one-sided Hann-window FFT magnitude.
    const float mag=sqrtf(power)*(4.0f/n);
    const float db=20.0f*log10f(std::max(mag,1.0e-7f));
    const float level=std::max(0.0f,std::min(255.0f,(db+52.0f)*(255.0f/52.0f)));
    levels_[i]=static_cast<uint8_t>(level);
  }
}
} // namespace tab5

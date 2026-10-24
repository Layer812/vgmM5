#pragma once
// R10 monitor: actual VGM register/key events + MDX driver track state.
// Never reads the mix buffer, never estimates volume by FFT, no locks/audio timing.
#include "tab5_channel_monitor.hpp"
#include <atomic>
#include <cstdint>
namespace tab5 {
class LiveMonitor {
 public:
  enum Chip : uint8_t { OPLL, OPM, OPN, AY, SN, SCC, MDX_FM, MDX_PCM };
  static constexpr unsigned kSlots=64;
  void configure_vgm(bool opll,bool opm,bool opn,bool ay,bool sn,bool scc);
  void configure_mdx(unsigned tracks);
  void opll_write(unsigned reg,unsigned value);
  void opm_write(unsigned reg,unsigned value);
  void ay_write(unsigned reg,unsigned value);
  void mdx_track(unsigned track,bool is_used,bool is_active,int note);
  // R14: block-boundary only; Active describes observed envelope/level, NOT Key-On.
  void observe_activity(Chip type,unsigned channel,bool active);
  void snapshot(Snapshot& dst) const;
 private:
  struct Slot { Chip type=OPLL;uint8_t chip_channel=0; std::atomic<uint32_t> observed{0}; };
  Slot slots_[kSlots];
  std::atomic<unsigned> size_{0};
  int offsets_[8]={};
  void add(Chip type,unsigned count);
  void write(Chip type,unsigned ch,bool active,int note=-1,int level=-1);
};
extern LiveMonitor g_live_monitor;
} // namespace tab5

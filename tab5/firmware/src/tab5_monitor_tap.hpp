#pragma once
// R10: Single producer (VGM OR MDX), single consumer (UI); no data races or audio locks.
#include "tab5_channel_monitor.hpp"
#include <atomic>
namespace tab5 {
class MonitorTap final {
public:
  Snapshot* begin_write() {
    unsigned expected=0;
    if (!owner_.compare_exchange_strong(expected,1,std::memory_order_acquire))return nullptr;
    stage_.count=0;stage_.overflow=false;
    return &stage_;
  }
  void publish() { stage_.generation=++serial_; owner_.store(2,std::memory_order_release); }
  bool consume(Snapshot& into) {
    unsigned expected=2;
    if(!owner_.compare_exchange_strong(expected,3,std::memory_order_acquire))return false;
    into=stage_;
    owner_.store(0,std::memory_order_release);
    return true;
  }
private:
  std::atomic<unsigned> owner_{0};
  Snapshot stage_={};
  uint32_t serial_=0;
};
extern MonitorTap g_monitor_tap;
inline void observe_channel(Snapshot& dst,const char* name,VoiceClass type,unsigned channel,
                            VoiceState state,unsigned reg_level,int note=-1,int pan=0){
  if(dst.count>=kMaxChannels){dst.overflow=true;return;}
  Channel& c=dst.channels[dst.count++];
  unsigned i=0;for(;i<kChipLabel-1&&name[i];++i)c.chip[i]=name[i];c.chip[i]=0;
  c.type=type;c.chip_instance=0;c.chip_channel=(uint8_t)channel;c.state=state;
  c.level=(uint8_t)(reg_level>255?255:reg_level);c.note=(int16_t)note;c.pan=(int8_t)pan;
}
}

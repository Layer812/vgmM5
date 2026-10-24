#include "tab5_live_monitor.hpp"
#include <cstring>
#include <cstdio>
namespace tab5 {
LiveMonitor g_live_monitor;
void LiveMonitor::add(Chip type,unsigned count){
  unsigned start=size_.load(std::memory_order_relaxed);
  offsets_[(unsigned)type]=(int)start;
  while(count-- && start<kSlots) {
    slots_[start].type=type;
    slots_[start].chip_channel=(uint8_t)(start-(unsigned)offsets_[(unsigned)type]);
    slots_[start].observed.store(0,std::memory_order_relaxed);
    ++start;
  }
  size_.store(start,std::memory_order_release);
}
void LiveMonitor::configure_vgm(bool opll,bool opm,bool opn,bool ay,bool sn,bool scc){
  size_.store(0,std::memory_order_release);
  for(int &offset:offsets_)offset=-1;
  if(opll)add(OPLL,9);
  if(opm)add(OPM,8);
  if(opn)add(OPN,6);
  if(ay)add(AY,3);
  if(sn)add(SN,4);
  if(scc)add(SCC,5);
}
void LiveMonitor::configure_mdx(unsigned tracks){
  size_.store(0,std::memory_order_release);
  for(int &offset:offsets_)offset=-1;
  if(tracks>16)tracks=16;
  add(MDX_FM,tracks<8?tracks:8);
  if(tracks>8)add(MDX_PCM,tracks-8);
}
void LiveMonitor::write(Chip type,unsigned ch,bool active,int note,int level){
  const int first=offsets_[(unsigned)type];
  if(first<0)return;
  const unsigned idx=(unsigned)first+ch;
  if(idx>=size_.load(std::memory_order_acquire) || slots_[idx].type!=type)return;
  const uint32_t val=(1u<<31) | (active?(1u<<30):0u) |
      ((uint32_t)(level>=0?(level&255):0)<<16) | (uint32_t)((note>=0?note:0xFFFF)&0xFFFF);
  slots_[idx].observed.store(val,std::memory_order_relaxed);
}
void LiveMonitor::opll_write(unsigned reg,unsigned value){
  if(reg>=0x20 && reg<=0x28)write(OPLL,reg-0x20,(value&0x10)!=0);
  // OPLL 0x0E rhythm mode is a state change; do not assert a melodic note.
}
void LiveMonitor::opm_write(unsigned reg,unsigned value){
  if(reg==0x08)write(OPM,value&7,(value&0x78)!=0);
}
void LiveMonitor::ay_write(unsigned reg,unsigned value){
  if(reg>=8 && reg<=10){
    write(AY,reg-8,false,-1,(int)(value&0x1F));
    // Volume write is NOT a Key-On event: mark it as observed register state.
    const int first=offsets_[(unsigned)AY];
    if(first>=0)slots_[(unsigned)first+(reg-8)].observed.fetch_or(0x20000000u,std::memory_order_relaxed);
  }
}
void LiveMonitor::observe_activity(Chip type,unsigned ch,bool active){
  const int first=offsets_[(unsigned)type];
  if(first<0)return;
  const unsigned idx=(unsigned)first+ch;
  if(idx>=size_.load(std::memory_order_acquire) || slots_[idx].type!=type)return;
  // Bit29=observed/active but not definite KEY-ON. Unknown note=0xFFFF.
  slots_[idx].observed.store(0x8000FFFFu | (active?0x20000000u:0u),std::memory_order_relaxed);
}
void LiveMonitor::mdx_track(unsigned track,bool is_used,bool is_active,int note){
  if(!is_used) return;
  if(track<8)write(MDX_FM,track,is_active,note);
  else write(MDX_PCM,track-8,is_active,note);
}
void LiveMonitor::snapshot(Snapshot& dst) const{
  dst.count=0;dst.overflow=false;
  const unsigned n=size_.load(std::memory_order_acquire);
  static constexpr const char* tags[]={"YM2413","YM2151","YM2612","AY-3-8910","SN76489","SCC","MDX FM","MDX PCM"};
  for(unsigned i=0;i<n && i<kMaxChannels;++i){
    Channel& out=dst.channels[dst.count++];out={};
    const Slot& slot=slots_[i];
    snprintf(out.chip,sizeof(out.chip),"%s",tags[(unsigned)slot.type]);
    out.chip_channel=slot.chip_channel;
    out.type=(slot.type==AY || slot.type==SN)?VoiceClass::PSG:
             (slot.type==MDX_PCM?VoiceClass::PCM:VoiceClass::FM);
    const uint32_t val=slot.observed.load(std::memory_order_relaxed);
    out.state=!(val&0x80000000u)?VoiceState::Unknown:
        (val&0x40000000u)?VoiceState::KeyOn:
        (val&0x20000000u)?VoiceState::Active:VoiceState::Silent;
    // Unobserved slots MUST NOT appear to have MIDI note 0.
    out.note=!(val&0x80000000u) || (val&0xFFFFu)==0xFFFFu ? -1 : (int16_t)(val&0xFFFFu);
    out.level=(uint8_t)((val>>16)&255);
  }
  ++dst.generation;
}
} // namespace tab5

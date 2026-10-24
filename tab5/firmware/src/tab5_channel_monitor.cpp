#include "tab5_channel_monitor.hpp"
namespace tab5 {
void ChannelMonitor::begin_frame() {
  staging_.count = 0;
  staging_.overflow = false;
}
bool ChannelMonitor::append(const Channel& c) {
  if (staging_.count >= kMaxChannels) {
    staging_.overflow = true;
    return false;
  }
  staging_.channels[staging_.count++] = c;
  return true;
}
void ChannelMonitor::end_frame() {
  staging_.generation = published_.generation + 1;
  published_ = staging_;
}
std::size_t ChannelMonitor::pages(const Snapshot& s) {
  return s.count ? (s.count + kPageChannels - 1) / kPageChannels : 0;
}
std::size_t ChannelMonitor::page_offset(const Snapshot& s, std::size_t page) {
  return page < pages(s) ? page * kPageChannels : s.count;
}
std::size_t ChannelMonitor::page_count(const Snapshot& s, std::size_t page) {
  const std::size_t start = page_offset(s, page);
  if (start >= s.count) return 0;
  const std::size_t remain = s.count - start;
  return remain < kPageChannels ? remain : kPageChannels;
}
} // namespace tab5

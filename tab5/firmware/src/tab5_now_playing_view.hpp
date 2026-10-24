#pragma once
// UI-only model for the active track: never confuse the highlighted browser row
// with the engine's actual playback identity. Independent of LCD/SD/Arduino.
#include "tab5_engine_bridge.hpp"
#include <string>
#include <utility>
#include <vector>

namespace tab5 {
struct NowPlayingView {
  bool playing=false;
  bool failed=false;
  std::string filename;
  int ordinal=0;
  int total=0;
};

inline NowPlayingView now_playing_view(const Browser& browser, PlayerState state) {
  NowPlayingView view;
  view.failed = (state == PlayerState::Failed);
  view.playing = (state == PlayerState::PlayingVgm || state == PlayerState::PlayingMdx)
               && !browser.playing_path.empty();
  if (!view.playing) return view;
  const std::string::size_type slash=browser.playing_path.find_last_of('/');
  view.filename=browser.playing_path.substr(slash==std::string::npos?0:slash+1);
  // This counter is only meaningful when the active file is in the displayed folder.
  // Browsing another folder does not change the active track's filename.
  for (size_t i=0;i<browser.entries.size();++i) {
    const auto& e=browser.entries[i];
    if (e.kind!=EntryKind::Audio) continue;
    ++view.total;
    if ((int)i==browser.playing_index && Browser::join(browser.directory,e.name)==browser.playing_path)
      view.ordinal=view.total;
  }
  if (view.ordinal==0) view.total=0;
  return view;
}

// UTF-8 safe character boundaries; never slice inside a multibyte SD filename.
inline std::size_t utf8_next_boundary(const std::string& s, std::size_t pos) {
  std::size_t next=pos+1;
  while (next<s.size() && (static_cast<unsigned char>(s[next]) & 0xc0u)==0x80u) ++next;
  return next;
}
inline void utf8_pop_character(std::string& s) {
  if (s.empty()) return;
  std::size_t p=s.size()-1;
  while (p>0 && (static_cast<unsigned char>(s[p]) & 0xc0u)==0x80u) --p;
  s.erase(p);
}

// Width function measures rendered pixels with the selected (large) LCD font.
// Last line is ellipsized only when truly needed; earlier lines remain uncut.
template<class Width>
std::vector<std::string> wrap_now_playing(const std::string& filename,
                                         int available_px, int max_lines, Width width) {
  std::vector<std::string> result;
  if (available_px<=0 || max_lines<=0) return result;
  std::size_t pos=0;
  while (pos<filename.size() && (int)result.size()<max_lines) {
    std::string line;
    while (pos<filename.size()) {
      std::size_t next=utf8_next_boundary(filename,pos);
      std::string proposed=line+filename.substr(pos,next-pos);
      if (!line.empty() && width(proposed)>available_px) break;
      line=std::move(proposed);
      pos=next;
    }
    if ((int)result.size()==max_lines-1 && pos<filename.size()) {
      while (!line.empty() && width(line+"...")>available_px) utf8_pop_character(line);
      line+="...";
    }
    result.push_back(std::move(line));
  }
  return result;
}
} // namespace tab5

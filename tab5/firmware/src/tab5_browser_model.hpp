#pragma once
// vgmM5 Tab5 R0: pure state model (no display, SD, or engine dependencies).
// File taps/select cursor never start playback. Only explicit PLAY issues PlayFile.
#include <cstddef>
#include <string>
#include <vector>

namespace tab5 {
enum class EntryKind { Parent, Folder, Audio };
struct Entry { EntryKind kind; std::string name; };
enum class ActionType { None, OpenFolder, PlayFile, Stop, Next, Previous, SetVolume };
struct Action { ActionType type=ActionType::None; std::string path; int value=0; };
class Browser {
public:
  std::string directory="/";
  std::vector<Entry> entries;
  int cursor=-1;
  int playing_index=-1;
  std::string playing_path;
  bool repeat=false;
  bool folder_auto_play=false;
  int volume=128;
  static bool audio(const std::string& name);
  static std::string join(const std::string& base,const std::string& name);
  void load(std::string dir,std::vector<Entry> list);
  void select(int index);
  void move(int delta);
  Action enter() const; // folders only; audio selection alone must not play
  Action play();
  Action start_selected_or_first(); // R14: toggle controls can start the current folder playlist
  Action primary(bool engine_playing); // PLAY: stop when active, else enter folder/play selected track
  Action stop();
  Action next();
  Action previous();
  Action ended();
  Action adjust_volume(int delta);
private:
  Action play_at(int index);
  int find_audio(int from,int step,bool wrap) const;
};
}

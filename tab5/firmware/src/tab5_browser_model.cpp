#include "tab5_browser_model.hpp"
#include <algorithm>
#include <cctype>
#include <utility>
namespace tab5 {
bool Browser::audio(const std::string& name) {
  const auto p=name.find_last_of('.'); if(p==std::string::npos) return false;
  std::string ext=name.substr(p); std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return (char)std::tolower(c);});
  return ext==".vgm" || ext==".vgz" || ext==".mdx";
}
std::string Browser::join(const std::string& base,const std::string& name){return (base=="/"?"/":base+"/")+name;}
void Browser::load(std::string dir,std::vector<Entry> list){
  if(dir.empty()) dir="/";
  directory=std::move(dir);entries=std::move(list);cursor=entries.empty()?-1:0;
  // Keep active playback identity even as user browses elsewhere.
  playing_index=-1;
  for(size_t i=0;i<entries.size();++i) if(entries[i].kind==EntryKind::Audio && join(directory,entries[i].name)==playing_path) playing_index=(int)i;
}
void Browser::select(int index){if(index>=0 && index<(int)entries.size()) cursor=index;}
void Browser::move(int delta){if(entries.empty())return;cursor=std::max(0,std::min((int)entries.size()-1,cursor+delta));}
Action Browser::enter() const {
  if(cursor<0 || cursor>=(int)entries.size())return {};
  const Entry& e=entries[cursor];
  if(e.kind==EntryKind::Audio)return {}; // selection only
  if(e.kind==EntryKind::Parent){
    if(directory=="/")return {};
    auto pos=directory.find_last_of('/');
    std::string parent=(pos==0 || pos==std::string::npos)?"/":directory.substr(0,pos);
    return {ActionType::OpenFolder,parent,0};
  }
  return {ActionType::OpenFolder,join(directory,e.name),0};
}
Action Browser::play_at(int index){
  if(index<0 || index>=(int)entries.size() || entries[index].kind!=EntryKind::Audio)return {};
  playing_index=index;playing_path=join(directory,entries[index].name);
  return {ActionType::PlayFile,playing_path,0};
}
Action Browser::play(){return play_at(cursor);}
Action Browser::start_selected_or_first(){
  if(cursor>=0 && cursor<(int)entries.size() && entries[cursor].kind==EntryKind::Audio)
    return play_at(cursor);
  // If a directory/parent row is selected, play the first audio in the
  // currently opened folder; never guess or descend into another folder.
  for(int i=0;i<(int)entries.size();++i)
    if(entries[i].kind==EntryKind::Audio) return play_at(i);
  return {};
}
Action Browser::primary(bool engine_playing){
  // R8 single PLAY key: first press during playback is ALWAYS STOP,
  // even if the file selection or browsed directory has changed.
  if(engine_playing) return stop();
  if(cursor<0 || cursor>=(int)entries.size())return {};
  if(entries[cursor].kind!=EntryKind::Audio)return enter();
  return play_at(cursor);
}
Action Browser::stop(){playing_index=-1;playing_path.clear();return {ActionType::Stop,"",0};}
int Browser::find_audio(int from,int step,bool wrap) const {
  const int n=(int)entries.size();if(!n)return -1;
  for(int tries=0,idx=from+step;tries<n;++tries,idx+=step){
    if(idx<0||idx>=n){if(!wrap)return -1;idx=(idx<0)?n-1:0;}
    if(entries[idx].kind==EntryKind::Audio)return idx;
  }
  return -1;
}
Action Browser::next(){int from=playing_index>=0?playing_index:cursor;return play_at(find_audio(from,1,repeat));}
Action Browser::previous(){int from=playing_index>=0?playing_index:cursor;return play_at(find_audio(from,-1,repeat));}
Action Browser::ended(){
  if(!folder_auto_play){playing_index=-1;playing_path.clear();return {};}
  const int idx=find_audio(playing_index,1,repeat);
  if(idx<0){playing_index=-1;playing_path.clear();return {};}
  return play_at(idx);
}
Action Browser::adjust_volume(int delta){volume=std::max(0,std::min(255,volume+delta));return {ActionType::SetVolume,"",volume};}
}

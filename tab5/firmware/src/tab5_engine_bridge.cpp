#include "tab5_engine_bridge.hpp"
#include <algorithm>
#include <cctype>
namespace tab5 {
void EngineBridge::stop_both(){if(api_.vgm_stop)api_.vgm_stop();if(api_.mdx_stop)api_.mdx_stop();}
bool EngineBridge::any_producer_active() const {
 return (api_.vgm_active && api_.vgm_active()) ||
        (api_.mdx_active && api_.mdx_active()) ||
        (api_.vgm_idle && !api_.vgm_idle()) ||
        (api_.queue_flushed && !api_.queue_flushed());
}
void EngineBridge::clear_failed_play(Browser& b){b.playing_index=-1;b.playing_path.clear();state_=PlayerState::Failed;}
bool EngineBridge::dispatch(Browser& b,const Action& a){
 switch(a.type){
 case ActionType::Stop:
   stop_both();
   if(any_producer_active()){error_="Audio worker or speaker queue not stopped";state_=PlayerState::Failed;return false;}
   state_=PlayerState::Idle;error_="";return true;
 case ActionType::SetVolume: if(api_.vgm_volume)api_.vgm_volume((unsigned char)std::max(0,std::min(255,a.value)));return true;
 case ActionType::PlayFile: {
   stop_both();state_=PlayerState::Idle;error_="";
   if (any_producer_active()) {
     error_="Previous audio worker or speaker queue not stopped";
     clear_failed_play(b);
     return false;
   }
   auto dot=a.path.find_last_of('.');
   if(dot==std::string::npos){error_="Unsupported extension";clear_failed_play(b);return false;}
   auto ext=a.path.substr(dot);
   std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return (char)std::tolower(c);});
   bool ok=false;
   if(ext==".mdx") {if(api_.mdx_play)ok=api_.mdx_play(a.path.c_str(),true);if(ok)state_=PlayerState::PlayingMdx;}
   else if(ext==".vgm"||ext==".vgz") {if(api_.vgm_play)ok=api_.vgm_play(a.path.c_str(),true);if(ok)state_=PlayerState::PlayingVgm;}
   else {error_="Unsupported extension";clear_failed_play(b);return false;}
   if(!ok){error_="Playback start failed or engine unavailable";clear_failed_play(b);return false;}
   // R14: highlight the track actually started by PREV/NEXT or Folder Auto.
   // Do not move the cursor on failed starts or when browsing another folder.
   if(b.playing_index>=0 && b.playing_index<(int)b.entries.size() &&
      b.entries[b.playing_index].kind==EntryKind::Audio &&
      Browser::join(b.directory,b.entries[b.playing_index].name)==a.path)
     b.select(b.playing_index);
   return true;
 }
 default:return false;
 }
}
bool EngineBridge::poll_end(Browser& b){
 bool active=false;
 if(state_==PlayerState::PlayingVgm){if(!api_.vgm_active)return false;active=api_.vgm_active();}
 else if(state_==PlayerState::PlayingMdx){if(!api_.mdx_active)return false;active=api_.mdx_active();}
 else return false;
 if(active)return false;
 // EOF is not end of audible playback. Wait for the last PCM pointers to
 // be returned by Speaker before advancing to the next track.
 if(api_.vgm_idle && !api_.vgm_idle())return false;
 if(api_.queue_flushed && !api_.queue_flushed())return false;
 state_=PlayerState::Idle;
 const Action next=b.ended();
 if(next.type==ActionType::PlayFile)return dispatch(b,next);
 return false;
}
} // namespace tab5

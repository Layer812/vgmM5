#pragma once
#include "tab5_browser_model.hpp"
namespace tab5 {
// Explicitly injected engine entrypoints; no sound driver modifications.
struct EngineApi {
  bool (*vgm_play)(const char*, bool)=nullptr;
  bool (*mdx_play)(const char*, bool)=nullptr;
  void (*vgm_stop)()=nullptr;
  void (*mdx_stop)()=nullptr;
  bool (*vgm_active)()=nullptr;
  bool (*mdx_active)()=nullptr;
  void (*vgm_volume)(unsigned char)=nullptr;
  bool (*vgm_idle)()=nullptr;
  bool (*queue_flushed)()=nullptr;
};
enum class PlayerState { Idle, PlayingVgm, PlayingMdx, Failed };
class EngineBridge {
public:
 explicit EngineBridge(EngineApi api): api_(api) {}
 bool dispatch(Browser& browser, const Action& action);
 // Poll only after a successful PlayFile; once the active engine reports it has ended,
 // schedule a folder-next request. Returns whether a subsequent track began successfully.
 bool poll_end(Browser& browser);
 PlayerState state() const {return state_;}
 const char* error() const {return error_;}
private:
 EngineApi api_;
 PlayerState state_ = PlayerState::Idle;
 const char* error_="";
 void stop_both();
 bool any_producer_active() const;
 void clear_failed_play(Browser& browser);
};
} // namespace tab5

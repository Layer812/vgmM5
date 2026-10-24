#include "tab5_touch_ui.hpp"
namespace tab5 {
TouchResult hit_test(int x,int y,int first_row,std::size_t count){
  if(x<0||y<0||x>=kScreenW||y>=kScreenH)return {};
#define BTN(name,code) if(UiLayout::name.contains(x,y)) return {TouchCommand::code,-1}
  BTN(volume_down,VolumeDown);BTN(volume_up,VolumeUp);
  BTN(cursor_up,CursorUp);BTN(cursor_down,CursorDown);
  BTN(play,Play);
  BTN(repeat,Repeat);BTN(folder_auto,FolderAuto);
  BTN(page_back,PageBack);BTN(page_next,PageNext);
#undef BTN
  if(UiLayout::list.contains(x,y) && first_row>=0){
    const int idx=first_row+(y-UiLayout::list.y)/UiLayout::row_height;
    if(idx>=0 && static_cast<std::size_t>(idx)<count) return {TouchCommand::SelectRow,idx};
  }
  return {};
}
Action apply_touch(Browser& browser,const TouchResult& t,bool engine_playing){
  switch(t.command){
  case TouchCommand::SelectRow:browser.select(t.row);return {};
  case TouchCommand::CursorUp:if(engine_playing)return browser.previous();browser.move(-1);return {};
  case TouchCommand::CursorDown:if(engine_playing)return browser.next();browser.move(1);return {};
  case TouchCommand::Play:return browser.primary(engine_playing);
  case TouchCommand::Next:return browser.next();
  case TouchCommand::Previous:return browser.previous();
  case TouchCommand::Repeat:
    // R15: repeating the *active* mode button is STOP, not just a mode toggle.
    if(engine_playing && browser.repeat){
      browser.repeat=false;
      return browser.stop();
    }
    browser.repeat=true;
    return engine_playing?Action{}:browser.start_selected_or_first();
  case TouchCommand::FolderAuto:
    if(engine_playing && browser.folder_auto_play){
      browser.folder_auto_play=false;
      return browser.stop();
    }
    browser.folder_auto_play=true;
    return engine_playing?Action{}:browser.start_selected_or_first();
  case TouchCommand::VolumeDown:return browser.adjust_volume(-16);
  case TouchCommand::VolumeUp:return browser.adjust_volume(16);
  default:return {};
  }
}
} // namespace tab5

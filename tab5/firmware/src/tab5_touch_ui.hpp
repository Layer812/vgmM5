#pragma once
#include "tab5_browser_model.hpp"
#include <cstddef>
namespace tab5 {
// Logical pixel coordinates, landscape 1280x720.
constexpr int kScreenW=1280, kScreenH=720;
struct Rect { int x,y,w,h; bool contains(int px,int py) const {return px>=x&&py>=y&&px<x+w&&py<y+h;} };
enum class TouchCommand { None, CursorUp, CursorDown, Previous, Play, Next, Repeat, FolderAuto, VolumeDown, VolumeUp, SelectRow, PageBack, PageNext };
struct TouchResult { TouchCommand command=TouchCommand::None; int row=-1; };
struct UiLayout {
  // R12: left metadata/browser widened to approximately half of Tab5 landscape.
  // Keep a 12px gutter; right spectrum/monitor stays a coherent column.
  static constexpr Rect now_playing={24,95,600,244};
  static constexpr Rect browser={24,351,600,226};
  static constexpr Rect spectrum={636,95,620,324};
  static constexpr Rect monitor={636,431,620,146};
  static constexpr Rect volume_down={1010,28,107,52};
  static constexpr Rect volume_up={1125,28,117,52};
  // Combined selectors fill exactly the left BROWSER/NOW PLAYING column.
  static constexpr Rect cursor_up={24,614,294,67};
  static constexpr Rect cursor_down={330,614,294,67};
  // PLAY, REPEAT, FOLDER AUTO fill exactly the right Spectrum/Monitor column.
  static constexpr Rect play={636,614,198,67};
  static constexpr Rect repeat={846,614,198,67};
  static constexpr Rect folder_auto={1056,614,200,67};
  static constexpr Rect page_back={1158,434,36,34};
  static constexpr Rect page_next={1206,434,36,34};
  static constexpr Rect list={35,420,578,144};
  static constexpr int row_height=32;
  static constexpr int visible_rows=4;
};
// The renderer decides which directory rows are currently visible and provides first_row.
// Hit-testing returns SELECT ONLY on file rows, never a playback instruction.
TouchResult hit_test(int x,int y,int first_row,std::size_t entry_count);
// Deterministic command mapping; navigating a folder must be fulfilled by an SD browser adapter.
Action apply_touch(Browser& browser,const TouchResult& t,bool engine_playing=false);
} // namespace tab5

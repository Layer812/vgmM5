// R3: Tab5 display/touch boot proof; NO AUDIO and NO fabricated channel activity.
#include <Arduino.h>
#include <SPI.h>
// M5GFX enables fs::FS image-decoding overloads only when FS_H is defined
// before M5GFX.h is parsed indirectly via M5Unified.h.
#include <SD.h>
#include <M5Unified.h>
#include <esp_system.h>
#include "sdkconfig.h"
#include "tab5_sd_browser.hpp"
#include "tab5_cover_art.hpp"
#include "tab5_cover_dimensions.hpp"
#include "tab5_touch_ui.hpp"
#include "tab5_channel_monitor.hpp"
#include "tab5_live_monitor.hpp"
#include "tab5_track_meta.hpp"
#include "tab5_audio_contract.hpp"
#include "tab5_engine_bridge.hpp"
#include "tab5_now_playing_view.hpp"
#include "tab5_spectrum.hpp"
#include "tab5_peak_hold.hpp"
#include "vgm_engine.h"
#include "mdx_engine.hpp"
extern void mdx_engine_diag_print(void);
#include <algorithm>
#include <vector>
using namespace tab5;
// R19: the Tab5 display uses RGB565; do not pass RGB888 literals as raw colors.
static constexpr uint16_t tab5_rgb565(uint32_t rgb) {
  return static_cast<uint16_t>((((rgb >> 16) & 0xffu) >> 3) << 11 |
                               ((((rgb >> 8) & 0xffu) >> 2) << 5) |
                               ((rgb & 0xffu) >> 3));
}

// VGMM5_TAB5_R12_ENGINE_WIRING (build-only candidate, NO FLASH)
namespace tab5 { EngineApi make_real_engine_api(); }
static Browser browser;
static EngineBridge engine(make_real_engine_api());
static bool engine_ready=false;
static uint32_t last_engine_poll=0;
static std::size_t monitor_page=0;
static uint32_t last_monitor_draw=0;
static String last_command="None";
static uint32_t last_draw=0;
static bool previous_touch=false;
static bool display_ready=false;
static int first_row=0;
static bool sd_ready=false;
// R54: Keep the last hardware reset reason visible even when USB serial misses boot.
static esp_reset_reason_t r54_last_reset_reason=ESP_RST_UNKNOWN;
static const char* r54_reset_name(esp_reset_reason_t reason) {
  switch(reason) {
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_WDT: return "OTHER_WDT";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_SW: return "SOFTWARE";
    default: return "OTHER";
  }
}
static bool r54_reset_abnormal(esp_reset_reason_t reason) {
  return reason==ESP_RST_TASK_WDT || reason==ESP_RST_INT_WDT ||
         reason==ESP_RST_WDT || reason==ESP_RST_PANIC || reason==ESP_RST_BROWNOUT;
}
static tab5::SpectrumAnalyzer spectrum_analyzer;
// R17: peak hold is UI-core state only; no audio callback or extra PCM copy.
static tab5::SpectrumPeakHold spectrum_peaks;
static uint32_t last_spectrum_draw=0;
static uint32_t last_spectrum_signal=0;
static bool spectrum_drawn_active=false;
static bool refresh_folder(const std::string& directory){
  if(!sd_ready || !valid_directory_path(directory))return false;
  File folder=SD.open(directory.c_str(),FILE_READ);
  if(!folder || !folder.isDirectory()){if(folder)folder.close();return false;}
  std::vector<SdRow> rows;
  File child=folder.openNextFile();
  while(child && rows.size()<1024){
    String full=child.name();
    const int slash=full.lastIndexOf('/');
    String base=slash>=0?full.substring(slash+1):full;
    rows.push_back({std::string(base.c_str()),child.isDirectory()});
    child.close();
    child=folder.openNextFile();
  }
  if(child)child.close();
  folder.close();
  browser.load(directory,make_entries(directory,rows));
  first_row=0;
  return true;
}
// R20: Readable UI. Keep touch rectangles and all engine/SD semantics unchanged.
static void r20_centered(const Rect& r,const char* text,uint16_t foreground,uint16_t background,int textsize){
  M5.Display.setTextSize(textsize);
  M5.Display.setTextColor(foreground,background);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(text,r.x+r.w/2,r.y+r.h/2);
  M5.Display.setTextDatum(top_left);
}
static void button(const Rect& r,const char* label,uint32_t bg=tab5_rgb565(0x1a3451)) {
  const uint16_t fill=(uint16_t)bg;
  M5.Display.fillRoundRect(r.x,r.y,r.w,r.h,7,fill);
  M5.Display.drawRoundRect(r.x,r.y,r.w,r.h,7,tab5_rgb565(0x6cacc7));
  // Longer button labels must stay inside the actual clickable area.
  M5.Display.setTextSize(2);
  // R31 OPT2: buttons never fall back to 1x text; labels are bounded by layout.
  r20_centered(r,label,TFT_WHITE,fill,2);
}
static void panel(const Rect& r,const char* title) {
  M5.Display.fillRoundRect(r.x,r.y,r.w,r.h,8,tab5_rgb565(0x102139));
  M5.Display.drawRoundRect(r.x,r.y,r.w,r.h,8,tab5_rgb565(0x315e7b));
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(tab5_rgb565(0x54dce6),tab5_rgb565(0x102139));
  M5.Display.setCursor(r.x+12,r.y+12);
  M5.Display.print(title);
}
static void r20_fit_text(String value,int width){
  // Pixel-width clipping on UTF-8 strings: do not cut through a codepoint.
  M5.Display.setTextWrap(false);
  if(M5.Display.textWidth(value)<=width){M5.Display.print(value);return;}
  const String tail="...";
  while(value.length() && M5.Display.textWidth(value+tail)>width){
    int p=value.length()-1;
    while(p>0 && (((uint8_t)value[p]&0xc0)==0x80))--p;
    value.remove(p);
  }
  M5.Display.print(value+tail);
}
// Small status-only repaint: do not blank the full 1280x720 panel on PLAY/STOP.
static void render_status() {
  const uint16_t bg=tab5_rgb565(0x090f20);
  const uint16_t soft=tab5_rgb565(0xa9bacd);
  M5.Display.fillRect(25,683,1230,34,bg);
  M5.Display.setTextWrap(false);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(soft,bg);
  M5.Display.setCursor(28,688);
  M5.Display.printf("R16 | %s | VOL=%d | PLAY / OPEN, STOP",last_command.c_str(),browser.volume);
}
// R54: On-demand offline album art. Only runs when NOW PLAYING changes;
// does not allocate large image buffers or alter any audio/speaker code.
static bool draw_cover_for_track(const std::string& playing_path,
                                 int area_x, int area_y, int area_size) {
  if (!sd_ready || playing_path.empty() || playing_path[0]!='/') return false;
  const auto slash=playing_path.find_last_of('/');
  if (slash==std::string::npos) return false;
  const std::string folder_path=(slash==0) ? "/" : playing_path.substr(0,slash);
  if (!tab5::valid_directory_path(folder_path)) return false;
  File folder=SD.open(folder_path.c_str(), FILE_READ);
  if (!folder || !folder.isDirectory()) {if(folder)folder.close();return false;}
  std::vector<tab5::CoverCandidate> images;
  File entry=folder.openNextFile();
  unsigned scanned=0;
  while (entry && scanned++ < 1024) {
    // Filesystem enumeration runs on the UI core. Let IDLE and lower-priority
    // tasks run even in directories containing hundreds of entries.
    if ((scanned & 31u)==0u) vTaskDelay(1);
    if (!entry.isDirectory()) {
      const String full=entry.name();
      const int pos=full.lastIndexOf('/');
      const String base=pos>=0 ? full.substring(pos+1) : full;
      if (tab5::cover_format(std::string(base.c_str())) != tab5::CoverFormat::None)
        images.push_back({std::string(base.c_str()),(unsigned long)entry.size()});
    }
    entry.close();
    entry=folder.openNextFile();
  }
  if (entry) entry.close();
  folder.close();
  const auto selected=tab5::select_cover(images, playing_path);
  if (selected.format==tab5::CoverFormat::None) {
    Serial.printf("[R54][COVER] not found: %s\n", folder_path.c_str());
    return false;
  }
  const std::string path=(folder_path=="/"?"":folder_path)+"/"+selected.name;
  File file=SD.open(path.c_str(), FILE_READ);
  uint32_t w=0,h=0;
  const bool dims_ok=tab5::cover_read_dimensions(file,selected.format,w,h);
  if(file) file.close();
  if (!dims_ok) {
    Serial.printf("[R54][COVER] unsupported dimensions: %s\n",path.c_str());
    return false;
  }
  const float sx=(float)area_size/(float)w;
  const float sy=(float)area_size/(float)h;
  const float scale=std::min(1.0f,std::min(sx,sy));
  const int dw=std::max(1,std::min(area_size,(int)(w*scale)));
  const int dh=std::max(1,std::min(area_size,(int)(h*scale)));
  const int x=area_x+(area_size-dw)/2;
  const int y=area_y+(area_size-dh)/2;
  // Never hold CPU1 without yielding immediately before a bounded decode.
  vTaskDelay(1);
  bool ok=false;
  if (selected.format == tab5::CoverFormat::Jpeg)
    ok=M5.Display.drawJpgFile(SD,path.c_str(),x,y,dw,dh,0,0,scale,scale);
  else if (selected.format == tab5::CoverFormat::Png)
    ok=M5.Display.drawPngFile(SD,path.c_str(),x,y,dw,dh,0,0,scale,scale);
  else if (selected.format == tab5::CoverFormat::Bmp)
    ok=M5.Display.drawBmpFile(SD,path.c_str(),x,y,dw,dh,0,0,scale,scale);
  Serial.printf("[R54][COVER] %s file=%s source=%lux%lu draw=%dx%d\n",
                ok?"DISPLAYED":"DECODE_FAIL",path.c_str(),
                (unsigned long)w,(unsigned long)h,dw,dh);
  return ok;
}
// R12: real metadata confined to widened top-left NOW PLAYING panel.
// Folder-auto, NEXT/PREV, STOP, EOF and failure all refresh from engine state.
static std::string displayed_track_path;
static std::string displayed_directory;
static PlayerState displayed_track_state=PlayerState::Failed;
static int displayed_track_index=-2;
static void render_now_playing(bool force=false) {
  if (!force && displayed_track_path==browser.playing_path &&
      displayed_directory==browser.directory &&
      displayed_track_state==engine.state() &&
      displayed_track_index==browser.playing_index) return;
  displayed_track_path=browser.playing_path;
  displayed_directory=browser.directory;
  displayed_track_state=engine.state();
  displayed_track_index=browser.playing_index;
  const uint16_t surf=tab5_rgb565(0x102139);
  const uint16_t cyan=tab5_rgb565(0x54dce6);
  const uint16_t soft=tab5_rgb565(0xa9bacd);
  const Rect& now=UiLayout::now_playing;
  const int x=now.x+18;
  // R54: load the actual SD cover for the active track, not browser selection.
  constexpr int artwork_size=172;
  constexpr int artwork_gap=16;
  const int artwork_x=now.x+now.w-18-artwork_size;
  const int artwork_y=now.y+52;
  const int width=artwork_x-x-artwork_gap;
  M5.Display.fillRect(x,now.y+44,now.w-36,now.h-50,surf);
  M5.Display.fillRoundRect(artwork_x,artwork_y,artwork_size,artwork_size,6,tab5_rgb565(0x09182b));
  M5.Display.drawRoundRect(artwork_x,artwork_y,artwork_size,artwork_size,6,tab5_rgb565(0x315e7b));
  const NowPlayingView view=now_playing_view(browser,engine.state());
  const bool cover_ok=view.playing &&
      draw_cover_for_track(browser.playing_path,artwork_x,artwork_y,artwork_size);
  if (!cover_ok) {
    // Decode may have partially painted; restore an honest fallback.
    M5.Display.fillRoundRect(artwork_x,artwork_y,artwork_size,artwork_size,6,tab5_rgb565(0x09182b));
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(tab5_rgb565(0x7a91a3),tab5_rgb565(0x09182b));
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("COVER ART",artwork_x+artwork_size/2,artwork_y+artwork_size/2-12);
    M5.Display.drawString(view.playing?"NOT FOUND":"NOT LOADED",
                          artwork_x+artwork_size/2,artwork_y+artwork_size/2+13);
  }
  M5.Display.drawRoundRect(artwork_x,artwork_y,artwork_size,artwork_size,6,tab5_rgb565(0x315e7b));
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextWrap(false);
  const tab5::TrackMeta* meta=nullptr;
  if(view.playing) meta=engine.state()==PlayerState::PlayingMdx?
                  &mdx_engine_get_metadata():&vgm_engine_get_metadata();
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(cyan,surf);
  M5.Display.setCursor(x,now.y+48);
  if(view.playing){
    if(view.ordinal>0) M5.Display.printf("PLAYING  %02d / %02d",view.ordinal,view.total);
    else M5.Display.print("PLAYING");
  }else if(view.failed) M5.Display.print("PLAY FAILED");
  else M5.Display.print("STOPPED / READY");
  // Prefer decoded GD3/MDX title, fall back to the actual filename.
  const std::string title=view.playing?(meta && meta->title[0]?meta->title:view.filename):
                         (view.failed?"Playback failed":"Select + PLAY / OPEN");
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(TFT_WHITE,surf);
  const auto lines=wrap_now_playing(title,width,2,[](const std::string& value)->int{
    return M5.Display.textWidth(String(value.c_str()));
  });
  for(size_t i=0;i<lines.size();++i){
    M5.Display.setCursor(x,now.y+72+(int)i*29);
    M5.Display.print(lines[i].c_str());
  }
  if(meta && view.playing){
    // Display metadata in a fixed, large-enough font. No fabricated data.
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(soft,surf);
    auto detail=[&](int ypos,const String& content){
      M5.Display.setCursor(x,now.y+ypos);
      r20_fit_text(content,width);
    };
    if(engine.state()==PlayerState::PlayingMdx){
      detail(139,String("FORMAT: MDX / TRACKS ")+String(meta->tracks));
      detail(164,String("SOUND: ")+meta->chips);
      detail(189,meta->pdx_requested?(meta->pdx_loaded?(String("PDX: ")+String(meta->pdx_samples)+" SAMPLE SLOTS"):"PDX: UNAVAILABLE"):"PDX: NOT REFERENCED");
      detail(214,String("FILE: ")+view.filename.c_str());
    }else{
      detail(139,String("GAME: ")+(meta->game[0]?meta->game:"--"));
      detail(164,String("COMPOSER: ")+(meta->author[0]?meta->author:"--"));
      detail(189,String("CHIPS: ")+(meta->chips[0]?meta->chips:"--"));
      if(meta->samples){
        const unsigned sec=(meta->samples+22050u)/44100u;
        detail(214,String("LENGTH: ")+String(sec/60)+":"+String(sec%60<10?"0":"")+String(sec%60)+
                    (meta->loop_samples?" / LOOP":""));
      }else detail(214,"LENGTH: UNKNOWN");
    }
  }
  if (view.playing)
    Serial.printf("[R31][NOW_PLAYING] %s (%d/%d)\n",browser.playing_path.c_str(),view.ordinal,view.total);
}
// The file list is the only area repainted by cursor/selection changes.
static void render_browser_rows() {
  const uint16_t surf=tab5_rgb565(0x102139);
  const uint16_t cyan=tab5_rgb565(0x54dce6);
  M5.Display.fillRect(UiLayout::list.x,UiLayout::list.y,UiLayout::list.w,UiLayout::list.h,surf);
  for(int i=0;i<UiLayout::visible_rows;++i){
    const int idx=first_row+i;
    if(idx >= (int)browser.entries.size())break;
    const auto&e=browser.entries[idx];const int y=UiLayout::list.y+UiLayout::row_height*i;
    const uint16_t fill=idx==browser.cursor?tab5_rgb565(0x25577a):surf;
    M5.Display.fillRect(UiLayout::list.x,y,UiLayout::list.w,31,fill);
    if(idx==browser.cursor) M5.Display.drawRect(UiLayout::list.x,y,UiLayout::list.w,31,cyan);
    M5.Display.setTextSize(3);M5.Display.setTextColor(idx==browser.cursor?tab5_rgb565(0x92edfa):TFT_WHITE,fill);
    M5.Display.setCursor(UiLayout::list.x+8,y+3);
    if(M5.Display.textWidth(String(e.kind==EntryKind::Audio?"[M] ":"[D] ")+e.name.c_str())>UiLayout::list.w-18){M5.Display.setTextSize(2);M5.Display.setCursor(UiLayout::list.x+8,y+7);}
    r20_fit_text(String(e.kind==EntryKind::Audio?"[M] ":"[D] ")+e.name.c_str(),UiLayout::list.w-18);
  }
}
// Folder navigation repaints only the left pane, even during playback.
static void render_browser_panel() {
  const uint16_t surf=tab5_rgb565(0x102139);
  const uint16_t cyan=tab5_rgb565(0x54dce6);
  panel(UiLayout::browser,sd_ready?"BROWSER / SD CARD":"BROWSER / NO SD");
  M5.Display.setTextSize(2);M5.Display.setTextColor(cyan,surf);
  M5.Display.setCursor(UiLayout::browser.x+18,UiLayout::browser.y+45);
  r20_fit_text(String("SD:")+browser.directory.c_str(),UiLayout::browser.w-30);
  render_browser_rows();
}
// R17 compact categorical channel indicators. One row each for KEY and ACT,
// then channel number + observed NOTE. They are NOT amplitude/velocity gauges.
// Preserve R16 incremental cell invalidation; NO tall/full-height activity bars.
static void render_channel_monitor(bool active,bool force=false){
  const auto& r=UiLayout::monitor;
  const uint16_t bg=tab5_rgb565(0x102139);
  const uint16_t grid=tab5_rgb565(0x25466b);
  const uint16_t lit=tab5_rgb565(0x7a91ef);
  const uint16_t soft=tab5_rgb565(0xa9bacd);
  static tab5::Snapshot snapshot;
  if(active)tab5::g_live_monitor.snapshot(snapshot);
  else snapshot.count=0;
  constexpr unsigned per_page=16;
  const unsigned pages=snapshot.count ? (unsigned)((snapshot.count+per_page-1)/per_page):1u;
  if(monitor_page>=pages)monitor_page=0;
  struct VisualCell {
    bool present=false;
    unsigned channel=0;
    tab5::VoiceState state=tab5::VoiceState::Unknown;
    int16_t note=-1;
  };
  static VisualCell previous[per_page];
  static bool ready=false,prev_active=false;
  static std::size_t prev_page=~(std::size_t)0;
  static std::size_t prev_count=~(std::size_t)0;
  const bool full=force||!ready||prev_active!=active||prev_page!=monitor_page||prev_count!=snapshot.count;
  const int top=r.y+40;
  constexpr int plot_height=103;
  const int plot_x=r.x+73;
  const int plot_w=r.w-94;
  const int stride=plot_w/(int)per_page;
  if(full){
    M5.Display.fillRect(r.x+12,r.y+37,r.w-24,r.h-42,bg);
    M5.Display.setTextWrap(false);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(soft,bg);
    M5.Display.setCursor(r.x+r.w-270,r.y+13);
    M5.Display.printf("PAGE %u/%u",(unsigned)monitor_page+1,pages);
    M5.Display.setCursor(UiLayout::page_back.x+6,UiLayout::page_back.y+7);M5.Display.print("<");
    M5.Display.setCursor(UiLayout::page_next.x+6,UiLayout::page_next.y+7);M5.Display.print(">");
    M5.Display.setCursor(r.x+17,top+4);M5.Display.print("KEY");
    M5.Display.setCursor(r.x+17,top+29);M5.Display.print("ACT");
    M5.Display.setCursor(r.x+17,top+56);M5.Display.print("CH");
    M5.Display.setCursor(r.x+17,top+79);M5.Display.print("NOTE");
    for(int k=1;k<=3;++k){
      const int y=top+(k==1?26:(k==2?52:77));
      M5.Display.drawFastHLine(plot_x,y,plot_w,grid);
    }
    for(unsigned i=0;i<per_page;++i)
      M5.Display.drawFastVLine(plot_x+(int)i*stride,top,plot_height,grid);
  }
  const unsigned base=(unsigned)monitor_page*per_page;
  for(unsigned i=0;i<per_page;++i){
    VisualCell current;
    if(active&&base+i<snapshot.count){
      const auto& ch=snapshot.channels[base+i];
      current.present=true;
      current.channel=(unsigned)ch.chip_channel;
      current.state=ch.state;
      current.note=ch.note;
    }
    const auto& old=previous[i];
    if(!full && current.present==old.present && current.channel==old.channel &&
       current.state==old.state && current.note==old.note)continue;
    const int left=plot_x+(int)i*stride;
    // One narrow cell at a time; do NOT erase other panels or full LCD.
    M5.Display.fillRect(left+1,top+1,stride-1,plot_height-2,bg);
    for(int k=1;k<=3;++k)
      M5.Display.drawFastHLine(left+1,top+(k==1?26:(k==2?52:77)),stride-1,grid);
    if(current.present){
      if(current.state==tab5::VoiceState::KeyOn)
        M5.Display.fillRect(left+9,top+8,stride-18,9,lit);
      else if(current.state==tab5::VoiceState::Active)
        M5.Display.fillRect(left+9,top+34,stride-18,9,lit);
      M5.Display.setTextSize(2);
      M5.Display.setTextColor(current.state==tab5::VoiceState::KeyOn?TFT_WHITE:soft,bg);
      M5.Display.setCursor(left+6,top+55);
      M5.Display.printf("%02u",current.channel+1);
      M5.Display.setCursor(left+4,top+79);
      if(current.note>=0)M5.Display.printf("%02d",(int)current.note);
      else M5.Display.print("--");
    }
    previous[i]=current;
  }
  ready=true;prev_active=active;prev_page=monitor_page;prev_count=snapshot.count;
}
// R11 upper-right Spectrum Analyzer: true 256-point PCM FFT from UI snapshot.
// MMDSP-inspired blue grids, no fictitious spectrum data.
// R13: avoid repeated full-width LCD clear. Static grid paints once; each
// 24-bar column updates only when its *visible* height changes. UI core only.
static void render_spectrum(bool active) {
  const auto& r=UiLayout::spectrum;
  const uint16_t bg=tab5_rgb565(0x102139);
  const uint16_t grid=tab5_rgb565(0x25466b);
  const uint16_t bar=tab5_rgb565(0x7a91ef);
  const uint16_t peak_color=tab5_rgb565(0xcbd9f8);
  const uint16_t soft=tab5_rgb565(0xa9bacd);
  const int plot_x=r.x+51, plot_y=r.y+63;
  const int plot_width=r.w-72, plot_height=r.h-85;
  const int stride=plot_width/tab5::SpectrumAnalyzer::kBars;
  const int bar_width=stride-7;
  static int old_heights[tab5::SpectrumAnalyzer::kBars];
  static int old_peaks[tab5::SpectrumAnalyzer::kBars];
  static bool initial=true;
  static int old_active=-1;
  if(initial) {
    M5.Display.fillRect(plot_x-2,plot_y-2,plot_width+4,plot_height+4,bg);
    for(int j=0;j<=6;++j)
      M5.Display.drawFastHLine(plot_x,plot_y+j*plot_height/6,plot_width,grid);
    for(int i=0;i<tab5::SpectrumAnalyzer::kBars;++i)
      M5.Display.drawFastVLine(plot_x+i*stride,plot_y,plot_height+1,grid);
    for(int &v:old_heights)v=-1;
    for(int &v:old_peaks)v=-1;
    initial=false;
  }
  const auto& levels=spectrum_analyzer.levels();
  // 500 ms maximum hold followed by decaying marker, exclusively on UI core.
  spectrum_peaks.update(levels,active,millis());
  const auto& peaks=spectrum_peaks.peaks();
  for(int i=0;i<tab5::SpectrumAnalyzer::kBars;++i){
    const int height=active?((int)levels[i]*(plot_height-2)/255):0;
    const int peak_height=active?((int)peaks[i]*(plot_height-2)/255):0;
    // Quantize bar height to reduce redundant LCD writes/noise regressions.
    const int paint_height=(height/4)*4;
    const int paint_peak=(peak_height/4)*4;
    if(paint_height==old_heights[i] && paint_peak==old_peaks[i])continue;
    const int x=plot_x+i*stride+3;
    M5.Display.fillRect(x,plot_y,bar_width,plot_height,bg);
    for(int j=0;j<=6;++j){
      const int y=plot_y+j*plot_height/6;
      M5.Display.drawFastHLine(x,y,bar_width,grid);
    }
    if(paint_height>0)
      M5.Display.fillRect(x,plot_y+plot_height-paint_height,bar_width,paint_height,bar);
    if(paint_peak>0)
      M5.Display.drawFastHLine(x,plot_y+plot_height-1-paint_peak,bar_width,peak_color);
    old_heights[i]=paint_height;
    old_peaks[i]=paint_peak;
  }
  if(old_active!=(active?1:0)){
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(soft,bg);
    M5.Display.fillRect(r.x+18,r.y+37,r.w-36,25,bg);
    M5.Display.setCursor(r.x+18,r.y+39);
    M5.Display.print(active?"PCM FFT / PEAK 0.5s":"NO SIGNAL / 256 FFT");
    M5.Display.setCursor(r.x+r.w-211,r.y+39);
    M5.Display.print("86Hz - 11kHz");
    old_active=active?1:0;
  }
}
static void update_spectrum(bool active, uint32_t now) {
  if((uint32_t)(now-last_spectrum_draw)<160)return; // <= 6.25 Hz; UI core
  last_spectrum_draw=now;
  int16_t frame[tab5::SpectrumTap::kSize];
  if(active && tab5::g_spectrum_tap.consume(frame)) {
    spectrum_analyzer.analyze(frame);
    last_spectrum_signal=now;
  }
  const bool live=active && (uint32_t)(now-last_spectrum_signal)<=450;
  if(live || spectrum_drawn_active)render_spectrum(live);
  spectrum_drawn_active=live;
}
static bool engine_is_playing() {
  const PlayerState state=engine.state();
  return state==PlayerState::PlayingVgm || state==PlayerState::PlayingMdx;
}
static void render_primary_button(bool force=false) {
  static int last_visual_state=-1;
  const int state=engine_is_playing()?1:0;
  if(!force && state==last_visual_state)return;
  last_visual_state=state;
  button(UiLayout::play,engine_is_playing()?"STOP":"PLAY / OPEN",engine_is_playing()?tab5_rgb565(0x1b6985):tab5_rgb565(0x1a3451));
}
static void render() {
  const uint16_t bg=tab5_rgb565(0x090f20);
  const uint16_t surf=tab5_rgb565(0x102139);
  const uint16_t cyan=tab5_rgb565(0x54dce6);
  const uint16_t soft=tab5_rgb565(0xa9bacd);
  M5.Display.fillScreen(bg);
  M5.Display.setTextWrap(false);
  M5.Display.setTextSize(4);M5.Display.setTextColor(cyan,bg);
  M5.Display.setCursor(25,23);M5.Display.print("vgmM5");
  M5.Display.setTextSize(2);M5.Display.setTextColor(soft,bg);
  M5.Display.setCursor(210,40);M5.Display.print("MMDSP INSPIRED / TAB5");
  button(UiLayout::volume_down,"VOL -");button(UiLayout::volume_up,"VOL +");
  render_browser_panel();
  panel(UiLayout::now_playing,"NOW PLAYING");
  // GD3/MDX metadata is displayed exclusively in the top-left panel.
  render_now_playing(true);
  panel(UiLayout::monitor,"MMDSP CHANNEL GRAPHICS");
  render_channel_monitor(engine_is_playing(),true);
  panel(UiLayout::spectrum,"SPECTRUM ANALYZER");
  render_spectrum(false);
  button(UiLayout::cursor_up,"UP / PREV");button(UiLayout::cursor_down,"DOWN / NEXT");
  render_primary_button(true);
  // State is shown by button fill; never append ON/OFF to labels.
  button(UiLayout::repeat,"REPEAT",browser.repeat?tab5_rgb565(0x1b6985):tab5_rgb565(0x1a3451));
  button(UiLayout::folder_auto,"FOLDER AUTO",browser.folder_auto_play?tab5_rgb565(0x1b6985):tab5_rgb565(0x1a3451));
  render_status();
}
static void keep_playing_row_visible(){
  if(browser.cursor<0)return;
  if(browser.cursor<first_row)first_row=browser.cursor;
  if(browser.cursor>=first_row+UiLayout::visible_rows)
    first_row=browser.cursor-UiLayout::visible_rows+1;
}
static void execute_action(const Action& a){
 switch(a.type){
 case ActionType::PlayFile:
   if(!engine_ready){last_command="AUDIO NOT INITIALIZED";break;}
   last_command=engine.dispatch(browser,a)?"PLAY START REQUESTED":"PLAY FAILED";
   if(!strcmp(last_command.c_str(),"PLAY FAILED"))Serial.printf("VGMM5_R12 engine error: %s\n",engine.error());
   break;
 case ActionType::Stop:
   if(engine_ready){last_command=engine.dispatch(browser,a)?"STOPPED":"STOP FAILED";}
   else last_command="AUDIO NOT INITIALIZED";
   break;
 case ActionType::OpenFolder:last_command=refresh_folder(a.path)?"FOLDER OPENED":"FOLDER OPEN FAILED";break;
 case ActionType::SetVolume:
   if(engine_ready){engine.dispatch(browser,a);last_command="VOLUME UPDATED";}
   else last_command="VOLUME STATE ONLY";
   break;
 default:break;
 }
}
void setup(){
 Serial.begin(115200);
 r54_last_reset_reason=esp_reset_reason();
 Serial.printf("[R22][BOOT] reset_reason=%d (ESP-IDF enum); free_heap=%u\n",(int)r54_last_reset_reason,(unsigned)ESP.getFreeHeap());
 // This line reports the *previous* reset cause; configuration WDT lines
 // below are NOT watchdog-trigger events.
 Serial.printf("[R54][LAST_RESET] %s (%d), abnormal=%d; WDT enabled logs are configuration only\n",
               r54_reset_name(r54_last_reset_reason),(int)r54_last_reset_reason,
               r54_reset_abnormal(r54_last_reset_reason)?1:0);
#if defined(CONFIG_ESP_TASK_WDT_EN)
 Serial.printf("[R22][WDT] task=enabled init=%d panic=%d timeout_s=%d\n",
#if defined(CONFIG_ESP_TASK_WDT_INIT)
 1,
#else
 0,
#endif
#if defined(CONFIG_ESP_TASK_WDT_PANIC)
 1,
#else
 0,
#endif
#if defined(CONFIG_ESP_TASK_WDT_TIMEOUT_S)
 CONFIG_ESP_TASK_WDT_TIMEOUT_S
#else
 -1
#endif
 );
#else
 Serial.println("[R22][WDT] CONFIG_ESP_TASK_WDT_EN not defined");
#endif
#if defined(CONFIG_ESP_INT_WDT)
 Serial.println("[R22][WDT] interrupt WDT enabled");
#else
 Serial.println("[R22][WDT] interrupt WDT setting unavailable/disabled");
#endif
 auto cfg=M5.config();
 M5.begin(cfg);
 Serial.printf("VGMM5_R6 audio_contract=%lu Hz stereo=%u frames=%u; ES8388 route=UNVERIFIED; dedicated codec init not present\n", (unsigned long)kOriginalStereo44100.actual_rate_hz,(unsigned)kOriginalStereo44100.stereo_channels,(unsigned)kOriginalStereo44100.frames_per_block);
 M5.Display.setRotation(1);
 const int w=M5.Display.width(), h=M5.Display.height();
 Serial.printf("VGMM5_R4 display=%dx%d touch=%d\n",w,h,M5.Touch.isEnabled());
 display_ready=(w>0&&h>0);
 if(!display_ready)return;
 SPI.begin(43,39,44,42); // Official M5Stack Tab5 microSD SPI pin mapping.
 sd_ready=SD.begin(42,SPI,25000000);
 Serial.printf("VGMM5_R4 microSD=%s\n",sd_ready?"mounted":"unavailable");
 if(sd_ready && !refresh_folder("/")) last_command="SD ROOT READ FAILED";
 else if(!sd_ready) last_command="SD MOUNT FAILED";
 // Engine initialization order mirrors original voice.cpp: speaker/audio before MDX task.
 // R11 is a compile-only integration step, not evidence of correct Tab5 audio routing.
 vgm_engine_init();
 engine_ready=vgm_engine_audio_ready(); // R15_AUDIO_STARTUP_GUARD
 Serial.printf("VGMM5_R15 audio startup=%s\n",engine_ready?"PASS":"FAILED");
 if(engine_ready)mdx_engine_init();
 if (r54_reset_abnormal(r54_last_reset_reason) && last_command=="None") {
   last_command=String("LAST RESET: ")+r54_reset_name(r54_last_reset_reason);
 }
 render();
}
void loop(){
 static bool reported_ui_core=false;
 if(!reported_ui_core){
   reported_ui_core=true;
   Serial.printf("[R13][CORE] Arduino loop / GUI on CPU%d (intended CPU1)\n",(int)xPortGetCoreID());
   if(xPortGetCoreID()!=1) Serial.println("[R13][WARN] GUI and Core0 audio may compete: no runtime repinning attempted");
 }
 static uint32_t r22_last=0;
 const uint32_t r22_now=millis();
 if((uint32_t)(r22_now-r22_last)>=2000){
   r22_last=r22_now;
   vgm_engine_diag_print();
   mdx_engine_diag_print();
   Serial.printf("[R22][HEARTBEAT] ms=%lu heap=%lu\n",(unsigned long)r22_now,(unsigned long)ESP.getFreeHeap());
 }
 if(!display_ready){delay(1000);return;}
 M5.update();
 if (engine_ready) vgm_engine_service_headphone_route();
 const bool down=M5.Touch.getCount()>0;
 if(down&&!previous_touch){
  auto pt=M5.Touch.getDetail(0);
  const int x=pt.x*1280/M5.Display.width();
  const int y=pt.y*720/M5.Display.height();
  const TouchResult t=hit_test(x,y,first_row,browser.entries.size());
  if(t.command!=TouchCommand::None){
   if(t.command==TouchCommand::PageBack || t.command==TouchCommand::PageNext){
     static tab5::Snapshot snap;
     if(engine_is_playing()) tab5::g_live_monitor.snapshot(snap);
     else snap.count=0;
     const std::size_t pages=(snap.count+15u)/16u;
     if(pages){
       monitor_page=(monitor_page+pages+(t.command==TouchCommand::PageNext?1:pages-1))%pages;
       render_channel_monitor(true,true);
     }
     previous_touch=down;
     return;
   }
   const Action a=apply_touch(browser,t,engine_is_playing());
   if(t.command==TouchCommand::SelectRow)last_command="SELECT ONLY";
   else if(t.command==TouchCommand::CursorUp)last_command=engine_is_playing()?"PREVIOUS TRACK":"CURSOR UP";
   else if(t.command==TouchCommand::CursorDown)last_command=engine_is_playing()?"NEXT TRACK":"CURSOR DOWN";
   else if(t.command==TouchCommand::Repeat)last_command="REPEAT TOGGLE";
   else if(t.command==TouchCommand::FolderAuto)last_command="FOLDER AUTO TOGGLE";
   else if(t.command==TouchCommand::Play && a.type==ActionType::None)last_command="SELECT A TRACK";
   const int cursor_before=browser.cursor;
   const int page_before=first_row;
   execute_action(a);
   keep_playing_row_visible();
   const bool selection_changed=browser.cursor!=cursor_before || first_row!=page_before;
   // OPT3: never clear the full LCD in the interactive playback path.
   if(a.type==ActionType::OpenFolder)
     render_browser_panel();
   else if(selection_changed || t.command==TouchCommand::SelectRow ||
           t.command==TouchCommand::CursorUp || t.command==TouchCommand::CursorDown)
     render_browser_rows();
   else if(t.command==TouchCommand::Repeat)
     button(UiLayout::repeat,"REPEAT",browser.repeat?tab5_rgb565(0x1b6985):tab5_rgb565(0x1a3451));
   else if(t.command==TouchCommand::FolderAuto)
     button(UiLayout::folder_auto,"FOLDER AUTO",browser.folder_auto_play?tab5_rgb565(0x1b6985):tab5_rgb565(0x1a3451));
   render_now_playing();
   render_primary_button();
   render_channel_monitor(engine_is_playing());
   render_status();
   Serial.printf("VGMM5_R4 touch=%d,%d cmd=%d cursor=%d action=%d\n",x,y,(int)t.command,browser.cursor,(int)a.type);
  }
 }
 previous_touch=down;
 if(engine_ready && millis()-last_engine_poll>=100){
   last_engine_poll=millis();
   const int cursor_before=browser.cursor;
   const int page_before=first_row;
   if(engine.poll_end(browser)){last_command="AUTO NEXT STARTED";render_status();}
   keep_playing_row_visible();
   if(browser.cursor!=cursor_before || first_row!=page_before)render_browser_rows();
   // poll_end() also changes state at EOF with auto-play disabled; refresh on both.
   render_now_playing();
   render_primary_button();
 }
 if((uint32_t)(millis()-last_monitor_draw)>=200u){
   last_monitor_draw=millis();
   render_channel_monitor(engine_is_playing());
 }
 update_spectrum(engine_is_playing(),millis());
 delay(15);
}

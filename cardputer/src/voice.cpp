// vgmM5 / Layer812

#include <Arduino.h>
#include <M5Unified.h>
#include "vgm_engine.h"
#include "mdx_engine.hpp"
#include "esp_bt.h"  // ★これを追加

// AtomS3 specific
#include <FFat.h>
#include <USB.h>
#include <USBMSC.h>
#include <esp_partition.h>
#include <wear_levelling.h>

// Cardputer specific
#if defined(IS_CARDPUTER)
#include <M5Cardputer.h>
#include <SD.h>
#include "filer.hpp"
#include "wifi_manager.hpp"
#include "cloud_db.hpp"
#include <Preferences.h>
Preferences recentPrefs;
uint16_t recent_albums[5] = {0};
bool showing_recent_albums = false;

void load_recent_albums() {
    recentPrefs.begin("vgm", false);
    size_t len = recentPrefs.getBytes("recent", recent_albums, sizeof(recent_albums));
    if (len != sizeof(recent_albums)) {
        memset(recent_albums, 0, sizeof(recent_albums));
    }
    recentPrefs.end();
}

void add_recent_album(uint16_t album_id) {
    if (album_id == 0) return;
    int idx = -1;
    for (int i=0; i<5; i++) {
        if (recent_albums[i] == album_id) { idx = i; break; }
    }
    if (idx == -1) idx = 4;
    for (int i=idx; i>0; i--) {
        recent_albums[i] = recent_albums[i-1];
    }
    recent_albums[0] = album_id;
    recentPrefs.begin("vgm", false);
    recentPrefs.putBytes("recent", recent_albums, sizeof(recent_albums));
    recentPrefs.end();
}


#include "composer_sort.hpp"
#include "cache_manager.hpp"
#endif

static bool cloud_force_redraw = true;

// ============================================================================
// 1. 初期化・設定フェーズ (Initialization)
// ============================================================================

// AtomS3 USB Drive Logic
#if defined(IS_ATOMS3)
USBMSC MSC;
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    if (s_wl_handle == WL_INVALID_HANDLE) return 0;
    uint32_t sector_size = wl_sector_size(s_wl_handle);
    uint32_t address = (lba * sector_size) + offset;
    if (offset == 0) wl_erase_range(s_wl_handle, address, bufsize);
    wl_write(s_wl_handle, address, buffer, bufsize);
    return bufsize;
}

static int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    if (s_wl_handle == WL_INVALID_HANDLE) return 0;
    wl_read(s_wl_handle, (lba * wl_sector_size(s_wl_handle)) + offset, buffer, bufsize);
    return bufsize;
}

static bool onStartStop(uint8_t power_condition, bool start, bool load_eject) { return true; }

void enter_usb_mode() {
    Serial.println("Entering USB Drive Mode...");
    // 画面がある場合 (AtomS3)
    M5.Display.fillScreen(TFT_BLUE);
    M5.Display.setCursor(10, 40);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(2);
    M5.Display.println("USB Mode");

    // 省電力などのためにVGMタスクを停止（または再開させない）
    vgm_engine_stop();
    
    FFat.begin(true);
    FFat.end();

    const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "ffat");
    if (!partition) {
        Serial.println("No FAT partition found!");
        while(1) delay(100);
    }
    
    if (wl_mount(partition, &s_wl_handle) != ESP_OK) {
        Serial.println("wl_mount failed!");
        while(1) delay(100);
    }
    
    uint32_t sector_size = wl_sector_size(s_wl_handle); 
    uint32_t sector_count = wl_size(s_wl_handle) / sector_size;
    
    MSC.vendorID("vgmM5");
    MSC.productID("USB_Drive");
    MSC.productRevision("1.0");
    MSC.onStartStop(onStartStop);
    MSC.onRead(onRead);
    MSC.onWrite(onWrite);
    MSC.mediaPresent(true);
    MSC.begin(sector_count, sector_size);
//    update_namco_volumes(engine, ch);
    
    Serial.println("USB Drive Mode Ready.");
    while(1) {
        M5.update();
        if (M5.BtnA.wasPressed()) {
            Serial.println("Restarting...");
            ESP.restart();
        }
        delay(10);
    }
}
#endif // IS_ATOMS3

// ============================================================
// CoreS3 UI State Machine & Logic
// ============================================================
#if defined(IS_CORES3)
#include <SD.h>

// Embedded binary file symbols (from platformio.ini board_build.embed_files)
extern const uint8_t _binary_logo_png_start[] asm("_binary_logo_png_start");
extern const uint8_t _binary_logo_png_end[]   asm("_binary_logo_png_end");
extern const uint8_t _binary_up_png_start[]   asm("_binary_up_png_start");
extern const uint8_t _binary_up_png_end[]     asm("_binary_up_png_end");
extern const uint8_t _binary_down_png_start[] asm("_binary_down_png_start");
extern const uint8_t _binary_down_png_end[]   asm("_binary_down_png_end");
extern const uint8_t _binary_play_png_start[] asm("_binary_play_png_start");
extern const uint8_t _binary_play_png_end[]   asm("_binary_play_png_end");
extern const uint8_t _binary_stop_png_start[] asm("_binary_stop_png_start");
extern const uint8_t _binary_stop_png_end[]   asm("_binary_stop_png_end");
extern const uint8_t _binary_auto_png_start[] asm("_binary_auto_png_start");
extern const uint8_t _binary_auto_png_end[]   asm("_binary_auto_png_end");
extern const uint8_t _binary_back_png_start[]  asm("_binary_back_png_start");
extern const uint8_t _binary_back_png_end[]    asm("_binary_back_png_end");
extern const uint8_t _binary_forward_png_start[] asm("_binary_forward_png_start");
extern const uint8_t _binary_forward_png_end[]   asm("_binary_forward_png_end");

enum CoreS3State {
    CORES3_STATE_BROWSER,
    CORES3_STATE_PLAYBACK
};

static CoreS3State cores3_state = CORES3_STATE_BROWSER;
static String cores3_current_dir = "/";
static String cores3_file_list[128];
static bool   cores3_is_dir[128];
static int    cores3_file_count = 0;
static int    cores3_list_offset = 0;
static int    cores3_cursor = 0;
static bool   cores3_force_redraw = true;

static String cores3_thumb_path = "";
static LGFX_Sprite cores3_canvas(&M5.Display);
static bool cores3_auto_play = false;

static void cores3_load_directory(String dirPath) {
    cores3_file_count = 0;
    cores3_list_offset = 0;
    cores3_cursor = 0;
    cores3_thumb_path = "";
    
    if (dirPath != "/") {
        cores3_file_list[0] = "..";
        cores3_is_dir[0] = true;
        cores3_file_count++;
    }
    File root = SD.open(dirPath);
    if (!root || !root.isDirectory()) return;
    File file = root.openNextFile();
    while (file && cores3_file_count < 128) {
        String fname = file.name();
        // System Volume Information を除外
        if (fname == "System Volume Information") {
            file = root.openNextFile();
            continue;
        }
        String lname = fname; lname.toLowerCase();
        bool isDir = file.isDirectory();
        
        // 最初に見つかった画像をサムネイルとして記憶
        if (!isDir && cores3_thumb_path == "" && (lname.endsWith(".jpg") || lname.endsWith(".png") || lname.endsWith(".bmp"))) {
            cores3_thumb_path = (dirPath == "/" ? "" : dirPath) + "/" + fname;
        }

        if (isDir || lname.endsWith(".vgm") || lname.endsWith(".vgz") || lname.endsWith(".mdx")) {
            cores3_file_list[cores3_file_count] = fname;
            cores3_is_dir[cores3_file_count] = isDir;
            cores3_file_count++;
        }
        file = root.openNextFile();
    }
    root.close();
    
    // ソート処理
    for (int i = 0; i < cores3_file_count - 1; i++) {
        for (int j = i + 1; j < cores3_file_count; j++) {
            if ((cores3_is_dir[j] && !cores3_is_dir[i]) || 
               ((cores3_is_dir[i] == cores3_is_dir[j]) && cores3_file_list[i].compareTo(cores3_file_list[j]) > 0)) {
                String tempS = cores3_file_list[i]; bool tempD = cores3_is_dir[i];
                cores3_file_list[i] = cores3_file_list[j]; cores3_is_dir[i] = cores3_is_dir[j];
                cores3_file_list[j] = tempS; cores3_is_dir[j] = tempD;
            }
        }
    }
    cores3_force_redraw = true;
}

static void cores3_play_selected(bool enable_auto_play) {
    if (cores3_file_count == 0) return;
    String fname = cores3_file_list[cores3_cursor];
    if (fname == "..") {
        int lastSlash = cores3_current_dir.lastIndexOf('/');
        cores3_current_dir = (lastSlash > 0) ? cores3_current_dir.substring(0, lastSlash) : "/";
        cores3_load_directory(cores3_current_dir);
        cores3_force_redraw = true;
        return;
    }
    if (cores3_is_dir[cores3_cursor]) {
        cores3_current_dir += (cores3_current_dir == "/" ? "" : "/") + fname;
        cores3_load_directory(cores3_current_dir);
        cores3_force_redraw = true;
        return;
    }
    
    String fullPath = cores3_current_dir + (cores3_current_dir == "/" ? "" : "/") + fname;
    String lname = fullPath; lname.toLowerCase();
    
    vgm_engine_stop();
    mdx_engine_stop();
    delay(50);
    
    cores3_auto_play = enable_auto_play;
    
    if (lname.endsWith(".mdx")) mdx_engine_play(fullPath.c_str(), true);
    else vgm_engine_play(fullPath.c_str(), true);
    
    cores3_state = CORES3_STATE_PLAYBACK;
    cores3_force_redraw = true;
}

// --- スプラッシュ画面表示（裏でディレクトリ読み込み） ---
static void cores3_show_splash() {
    size_t logo_size = (size_t)(_binary_logo_png_end - _binary_logo_png_start);
    Serial.printf("CoreS3: logo.png size = %u bytes\n", logo_size);
    
    // 黒背景
    M5.Display.fillScreen(TFT_BLACK);
    
    // 位置を微調整するための変数（2倍表示で中央に寄せる）
    int pos_x = 40;
    int pos_y = 40;
    
    // logo.png を2倍サイズで描画
    M5.Display.drawPng(_binary_logo_png_start, logo_size, pos_x, pos_y, 0, 0, 0, 0, 2.0, 2.0);
    
    // 下部にバージョン表示
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_CYAN);
    M5.Display.drawString("vgmM5 0.98", 100, 195);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.drawString("by Layer812", 110, 220);
    
    // ロゴ表示の裏でディレクトリ読み込み＆スプライト初期化
    uint32_t t = millis();
    cores3_canvas.setPsram(true);
    cores3_canvas.createSprite(320, 240);
    cores3_load_directory("/");
    
    // 2秒間はロゴを表示（読み込みが早すぎた場合のウェイト）
    uint32_t elapsed = millis() - t;
    if (elapsed < 2000) {
        delay(2000 - elapsed);
    }
    
    // 2秒表示
    //delay(2000);
}
#endif // IS_CORES3

void setup() {
#if defined(IS_ATOMS3)
    // ★大至急！ ここに移動させます。一番最初にUSBを起動し、PCの認識を待ちます
//    update_namco_volumes(engine, ch);
   delay(2000);
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
#else
    esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
#endif

    auto cfg = M5.config();
    
#if defined(IS_ATOMS3)
    cfg.fallback_board = m5::board_t::board_M5AtomVoiceS3R;
#endif



    M5.begin(cfg);
    Serial.begin(115200);

    Serial.println("--- Boot Start ---"); // ★これがシリアルモニタに出るか確認！
    // 共通の再生エンジン初期化（I2S等の設定）
    Serial.println("Calling vgm_engine_init...");
    vgm_engine_init();
    Serial.println("Calling mdx_engine_init...");
    mdx_engine_init(); // MDXエンジン初期化（FreeRTOSタスク生成）

    // boardに合わせた初期化処理
#if defined(IS_CARDPUTER)
        Serial.println("Initializing Cardputer Board...");
        // M5Cardputer.beginを呼ぶとキーボード等も初期化される
        M5Cardputer.begin(cfg, true);
        M5Cardputer.Display.setBrightness(128);
        M5Cardputer.Display.setTextSize(2);
        
        SPI.begin(40, 39, 14, 12);
        if(!SD.begin(12, SPI, 40000000)){
            Serial.println("SD Card Mount Failed!");
        } else {
            Serial.println("SD Card Initialized.");
            cache_manager_init();
        }
#elif defined(IS_ATOMS3)
        Serial.println("Initializing AtomS3 Board...");
        
        // 起動直後のピン状態が安定し、M5.update()が正確にボタンを読めるように少し待機
        delay(100);
        M5.update();
        
        if (M5.BtnA.isPressed()) {
            Serial.println("Entering USB mode...");
            enter_usb_mode();
        }
        USB.begin();
        
        // ★ 注意：ここにあった古い update_namco_volumes(engine, ch);
        
        if (!FFat.begin(true)) {
            Serial.println("FFat Mount Failed!");
            return;
        }
        Serial.println("FFat Initialized.");
#elif defined(IS_CORES3)
        Serial.println("Initializing CoreS3 Board...");
        M5.Display.setBrightness(128);

        // 先にSDカードをマウントする（これがないと裏読みができない）
        SPI.begin(36, 35, 37, 4);
        if(!SD.begin(4, SPI, 20000000)){
            Serial.println("SD Card Mount Failed!");
        } else {
            Serial.println("SD Card Initialized.");
        }

        // スプラッシュ画面表示（この表示の直後に、上でマウントしたSDを裏で読み込みます）
        cores3_show_splash();
#endif
}

// ============================================================================
// 2. ファイル取得フェーズ (File Selection)
// ============================================================================

// AtomS3 File State
#define MAX_SONGS 100
String atoms3_file_list[MAX_SONGS];
int atoms3_file_count = 0;
int current_file_index = 0;

void atoms3_load_files() {
    if (atoms3_file_count > 0) return; // 既にロード済みならスキップ
    File root = FFat.open("/");
    if (!root) return;
    
    File file = root.openNextFile();
    while (file && atoms3_file_count < MAX_SONGS) {
        if (!file.isDirectory()) {
            String fname = file.name();
            if (!fname.startsWith("/")) fname = "/" + fname;
            String lname = fname; lname.toLowerCase();
            if (lname.endsWith(".vgm") || lname.endsWith(".vgz") || lname.endsWith(".mdx")) {
                atoms3_file_list[atoms3_file_count] = fname;
                atoms3_file_count++;
            }
        }
        file = root.openNextFile();
    }
    root.close();
    
    // ソート
    for (int i = 0; i < atoms3_file_count - 1; i++) {
        for (int j = i + 1; j < atoms3_file_count; j++) {
            if (atoms3_file_list[i].compareTo(atoms3_file_list[j]) > 0) {
                String temp = atoms3_file_list[i];
                atoms3_file_list[i] = atoms3_file_list[j];
                atoms3_file_list[j] = temp;
            }
        }
    }
    Serial.printf("AtomS3: Loaded %d songs\n", atoms3_file_count);
}



// ============================================================
// Cardputer Cloud UI State Machine
// ============================================================
#if defined(IS_CARDPUTER)

enum UIState {
    STATE_BOOT_MENU,
    STATE_TOP_MENU,
    STATE_COMPANY_LIST,
    STATE_COMPOSER_LIST,
    STATE_CHIP_LIST,
    STATE_ALBUM_LIST,
    STATE_TRACK_LIST,
    STATE_LOCAL_MODE
};

static UIState        current_state     = STATE_BOOT_MENU;
static int            cursor_pos        = 0;
static uint16_t       filter_company_id = 0xFFFF;
static uint32_t       filter_chip_mask  = 0;
static uint16_t       selected_album_id = 0;
static uint16_t       filtered_albums[6000];
static uint16_t       filtered_album_count = 0;
static uint32_t       filtered_tracks[1000];
static uint16_t       filtered_track_count = 0;

// ==========================================
// 階層の履歴管理と安全なカーソル移動
// ==========================================
static int            saved_cursor_top      = 0;
static int            saved_cursor_company  = 0;
static int            saved_cursor_composer = 0;
static int            saved_cursor_chip     = 0;
static int            saved_cursor_album    = 0;
static UIState        parent_state_of_album = STATE_TOP_MENU;
#ifndef VERSION
#define VERSION 0.98
#endif

void disptitle(int stat, char *title){
  M5Cardputer.Display.setTextSize(2.0f);
  M5Cardputer.Display.setCursor(0, 0);
  M5Cardputer.Display.fillRect(0, 0, 240, 34, BLACK);
  M5Cardputer.Display.setTextColor(OLIVE);
  M5Cardputer.Display.printf("vgmM5 %.2f ", VERSION);
  M5Cardputer.Display.drawLine(0,33,240,33,OLIVE);
  if(title){
    M5Cardputer.Display.setCursor(0, 18);
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.setTextSize(1.5f);
    M5Cardputer.Display.printf("%.*s", 26, title);
    M5Cardputer.Display.setTextSize(2);
  }
}


static void move_cursor(int delta, int max_val) {
    if (max_val <= 0) { cursor_pos = 0; return; }
    int old_pos = cursor_pos;
    cursor_pos += delta;
    while (cursor_pos < 0) cursor_pos += max_val; // 絶対にマイナスにさせない
    cursor_pos %= max_val;
    if (cursor_pos != old_pos) cloud_force_redraw = true;
}
// ==========================================

enum PlayMode { MODE_SINGLE, MODE_ALL, MODE_RANDOM };

static int composer_sort_mode = 0; // 0=ID, 1=Ascending, 2=Descending

uint16_t get_sorted_composer_id(int idx) {
    if (composer_sort_mode == 1) {
        return COMPOSER_SORT_ASC[idx];
    } else if (composer_sort_mode == 2) {
        int total = cloud_db_composer_count();
        if (idx < total) return COMPOSER_SORT_ASC[total - 1 - idx];
        return 0;
    }
    return idx;
}

void init_composer_sort(int mode) {
    composer_sort_mode = mode;
    cloud_force_redraw = true;
}

static PlayMode  current_play_mode = MODE_SINGLE;
static bool      auto_play_active  = false;
static int       auto_play_cursor  = 0;
static uint32_t  play_start_ms     = 0;
static uint32_t  play_duration_ms  = 180000;

struct SupportedChip { const char* name; uint32_t mask; };
const SupportedChip SUPPORTED_CHIPS[] = {
    {"YM2151 (OPM)",    (1<<0)},
    {"YM2612 (OPN2)",   (1<<1)},
    {"SN76489 (DCSG)",  (1<<2)},
    {"Namco C140",      (1<<3)},
    {"Namco C352",      (1<<4)},
    {"AY-3-8910",       (1<<5)},
    {"YM2203 (OPN)",    (1<<6)},
    {"OKI MSM6295",     (1<<7)},
    {"HuC6280",         (1<<8)}
};
const int NUM_SUPPORTED_CHIPS = sizeof(SUPPORTED_CHIPS) / sizeof(SUPPORTED_CHIPS[0]);

static String get_chip_string(uint32_t cm) {
    String chips = "";
    if (cm & (1<<0))  chips += "YM2151 ";
    if (cm & (1<<1))  chips += "YM2612 ";
    if (cm & (1<<2))  chips += "SN76489 ";
    if (cm & (1<<9))  chips += "NES ";
    if (cm & (1<<10)) chips += "GB ";
    if (cm & (1<<5))  chips += "AY ";
    if (cm & (1<<6))  chips += "YM2203 ";
    if (cm & (1<<8))  chips += "HuC6280 ";
    if (chips.length() == 0) chips = "(unknown)";
    return chips;
}

static void reset_to_top() {
    current_state = STATE_TOP_MENU;
    cursor_pos = saved_cursor_top;
    filter_company_id = 0xFFFF;
    filter_chip_mask = 0;
}

static void cloud_play_track_at(int idx) {
    if (idx < 0 || idx >= (int)filtered_track_count) return;
    uint32_t track_id = filtered_tracks[idx];
    TrackRecord trk = cloud_db_get_track(track_id);
    AlbumRecord alb = cloud_db_get_album(trk.album_id);
    
    String short_url = cloud_db_get_track_path(track_id);
    String track_title = short_url;
    if (track_title.length() > 0) {
        track_title.replace("%20", " ");
        track_title.replace("%27", "'");
        track_title.replace("%28", "(");
        track_title.replace("%29", ")");
        track_title.replace("%26", "&");
        track_title.replace("%2C", ",");
        if (track_title.endsWith(".vgz") || track_title.endsWith(".vgm")) {
            track_title = track_title.substring(0, track_title.length() - 4);
        }
    } else {
        track_title = cloud_db_get_string(trk.title_offset);
    }
    
    String full_url  = cloud_db_get_full_url(track_id);

    auto& cv = M5Cardputer.Display;
    cv.fillScreen(BLACK);
    String alb_title = cloud_db_get_string(alb.title_offset);
    
    cv.setTextColor(0xAAAA);
    cv.setTextSize(1.5f);
    String alb_st = alb_title;
    if (alb_st.length() > 24) alb_st = alb_st.substring(0, 24) + "..";
    cv.drawString(alb_st, 10, 36);
    
    cv.setTextSize(1.0f);
    cv.setTextColor(0x8410);
    String sys_name = cloud_db_get_system_name(alb.sys_id);
    if (sys_name.length() > 0) cv.drawString("Sys: " + sys_name, 10, 56);
    
    String comp_name = cloud_db_get_company_name(alb.comp_id);
    if (comp_name.length() > 0) {
        if (comp_name.length() > 20) comp_name = comp_name.substring(0, 20) + "..";
        cv.drawString("Maker: " + comp_name, 10, 72);
    }
    String compo_name = cloud_db_get_composer_name(alb.composer_id);
    if (compo_name.length() > 0) {
        if (compo_name.length() > 18) compo_name = compo_name.substring(0, 18) + "..";
        cv.drawString("Composer: " + compo_name, 10, 88);
    }
    cv.drawString("Chips: " + get_chip_string(alb.chips_mask), 10, 104);

    String thumb_url      = cloud_db_get_thumb_url(alb.thumb_offset);
    String thumb_path_str = cloud_db_get_string(alb.thumb_offset);
    cache_draw_thumb(thumb_path_str, thumb_url, 174, 48, 64, 64, "Now", "Downloading");
    
        disptitle(STATLOOP, (char*)track_title.c_str());

    String vgz_path = cache_get_or_download_vgz(short_url, full_url);
    if (vgz_path.length() > 0) {
        Serial.printf("[Play] %s\n", vgz_path.c_str());
        if (vgm_engine_play(vgz_path.c_str(), true)) {
            // Update thumbnail text to Now Playing
            cache_draw_thumb(thumb_path_str, thumb_url, 174, 48, 64, 64, "Now", "Playing");
            
            uint16_t dur = trk.duration_sec;
            if (dur == 0) dur = 180;
            play_start_ms    = millis();
            play_duration_ms = (uint32_t)(dur + 2) * 1000;
            auto_play_cursor = idx;
        } else {
            const char* err = vgm_engine_get_error();
            cv.fillScreen(BLACK);
            cv.setTextSize(1.5f);
            cv.setTextColor(RED);
            cv.drawString("Play FAILED:", 10, 10);
            cv.setTextColor(YELLOW);
            String se = String(err);
            if (se.length() > 25) se = se.substring(0, 25);
            cv.drawString(se, 10, 35);
            cv.setTextColor(0x8410);
            cv.drawString("Press any key", 10, 90);
            delay(3000);
            auto_play_active = false;
            cloud_force_redraw = true;
        }
    } else {
        cv.fillScreen(BLACK);
        cv.setTextSize(1.5f);
        cv.setTextColor(RED);
        cv.drawString("Download FAILED", 10, 10);
        cv.setTextColor(0x8410);
        cv.drawString("Check serial log.", 10, 40);
        delay(5000);
        auto_play_active = false;
        cloud_force_redraw = true;
    }
}

// 待機中（ファイル選択モード）の処理
void phase_file_selection() {
    M5Cardputer.update();
    bool keyChanged = M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed();
    char key = 0;
    bool enter_pressed = false;

    if (keyChanged) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
        for (auto c : status.word) { key = c; break; }
        enter_pressed = status.enter;
        // cloud_force_redraw is now set within specific key handlers or move_cursor
    }

    // ── 再生終了を検出してリドロー要求 ──
    static bool s_was_playing = false;
    bool s_now_playing = vgm_engine_is_playing() || mdx_engine_is_playing();
    if (s_was_playing != s_now_playing) cloud_force_redraw = true;
    s_was_playing = s_now_playing;

    // ── オートパイロット（自動次曲再生） ──
    if (auto_play_active && current_play_mode != MODE_SINGLE && !s_now_playing) {
        int next_idx;
        if (current_play_mode == MODE_ALL) {
            next_idx = (auto_play_cursor + 1) % (filtered_track_count > 0 ? filtered_track_count : 1);
        } else {
            next_idx = (filtered_track_count > 0) ? (int)(esp_random() % filtered_track_count) : 0;
        }
        cursor_pos = next_idx;
        cloud_play_track_at(next_idx);
        return;
    }

    // ── STATE_LOCAL_MODE: ローカルSDモード ──
    if (current_state == STATE_LOCAL_MODE) {
        static bool card_initialized = false;
        if (!card_initialized) { filerinit(); card_initialized = true; }
        disptitle(STATCLR, (char*)"VGM Player");
        filenum = makevgmlist(SD);
        int i = selectfile();
        if (i == -1) {
            // Exited local mode
            current_state = STATE_BOOT_MENU;
            cursor_pos = 0;
            cloud_force_redraw = true;
            return;
        }
        uint8_t fname[PATHMAX];
        disptitle(STATLOAD, (char*)"Local Files");
        if (cnvfile(SD, &filelist[i], fname)) {
            Serial.printf("Cardputer: Selected %s (type=%d)\n", (char*)fname, filelist[i].type);
            if (filelist[i].type == TYPE_MDX) {
                // MDXファイル再生
                if (mdx_engine_play((const char*)fname)) {
                    const char* title = mdx_engine_get_title();
                    if (title == nullptr || title[0] == '\0') {
                        const char* basename = strrchr((const char*)fname, '/');
                        title = basename ? basename + 1 : (const char*)fname;
                    }
                    disptitle(STATLOOP, (char*)title);
                } else {
                    const char* err = mdx_engine_get_error();
                    Serial.printf("MDX Error: %s\n", err);
                    disptitle(STATCLR, (char*)"");
                    M5Cardputer.Display.setCursor(0, 18);
                    M5Cardputer.Display.setTextColor(RED, BLACK);
                    M5Cardputer.Display.setTextSize(1.5f);
                    M5Cardputer.Display.printf("Err: %.*s", 22, err);
                    M5Cardputer.Display.setTextSize(2);
                    delay(3000);
                    cloud_force_redraw = true;
                }
            } else {
                // VGM/VGZファイル再生（従来通り）
                // ★ 排他制御: MDXエンジンが再生中なら停止
                if (mdx_engine_is_playing()) mdx_engine_stop();
                delay(50);
                if (vgm_engine_play((const char*)fname, true)) {
                    const char* title = vgm_engine_get_title();
                    if (title == nullptr || title[0] == '\0') {
                        const char* basename = strrchr((const char*)fname, '/');
                        title = basename ? basename + 1 : (const char*)fname;
                    }
                    disptitle(STATLOOP, (char*)title);
                } else {
                    const char* err = vgm_engine_get_error();
                    Serial.printf("Playback Error: %s\n", err);
                    disptitle(STATCLR, (char*)"");
                    M5Cardputer.Display.setCursor(0, 18);
                    M5Cardputer.Display.setTextColor(RED, BLACK);
                    M5Cardputer.Display.setTextSize(1.5f);
                    M5Cardputer.Display.printf("Err: %.*s", 22, err);
                    M5Cardputer.Display.setTextSize(2);
                    delay(3000);
                    cloud_force_redraw = true;
                }
            }
        }
        return;
    }



    // === NEW KEY PROCESSING LOGIC ===
    if (keyChanged) {
        switch (current_state) {
            case STATE_BOOT_MENU: {
                int tot = 3;
                if (key == '.') move_cursor(1, tot);
                else if (key == ';') move_cursor(-1, tot);
                else if (key == '1') { cursor_pos = 0; enter_pressed = true; }
                else if (key == '2') { cursor_pos = 1; enter_pressed = true; }
                else if (key == '3') { cursor_pos = 2; enter_pressed = true; }

                if (key == ' ' || enter_pressed) {
                    if (cursor_pos == 0) {
                        if (WiFi.status() != WL_CONNECTED) {
                            M5Cardputer.Display.fillScreen(BLACK);
                            M5Cardputer.Display.setTextSize(1.5f);
                            M5Cardputer.Display.setTextColor(YELLOW);
                            M5Cardputer.Display.drawString("Connecting WiFi...", 10, 60);
                            delay(100);
                            wifi_manager_connect_saved();
                            if (WiFi.status() != WL_CONNECTED) {
                                M5Cardputer.Display.fillScreen(BLACK);
                                M5Cardputer.Display.setTextColor(RED);
                                M5Cardputer.Display.drawString("WiFi Required!", 10, 30);
                                M5Cardputer.Display.setTextColor(WHITE);
                                M5Cardputer.Display.drawString("Please setup WiFi", 10, 50);
                                delay(2000);
                                cursor_pos = 2;
                                cloud_force_redraw = true;
                                break;
                            }
                        }
                        if (!cloud_db_is_ready()) {
                            M5Cardputer.Display.fillScreen(BLACK);
                            M5Cardputer.Display.setTextSize(1.5f);
                            M5Cardputer.Display.setTextColor(WHITE);
                            M5Cardputer.Display.drawString("Loading DB...", 10, 60);
                            delay(100);
                            if (!cloud_db_init()) {
                                M5Cardputer.Display.setTextColor(RED);
                                M5Cardputer.Display.drawString("DB Load Failed!", 10, 80);
                                delay(2000);
                                cloud_force_redraw = true;
                                break;
                            }
                        }
                        current_state = STATE_TOP_MENU;
                        cursor_pos = saved_cursor_top;
                        cloud_force_redraw = true;
                    } else if (cursor_pos == 1) {
                        current_state = STATE_LOCAL_MODE;
                        cloud_force_redraw = true;
                    } else if (cursor_pos == 2) {
                        M5Cardputer.Display.fillScreen(BLACK);
                        M5Cardputer.Display.setTextSize(1.5f);
                        M5Cardputer.Display.setTextColor(YELLOW);
                        M5Cardputer.Display.drawString("WiFi Setup...", 10, 60);
                        delay(300);
                        wifi_manager_clear_saved();
                        wifi_manager_prompt_and_save();
                        cursor_pos = 0;
                        cloud_force_redraw = true;
                    }
                }
                break;
            }
            case STATE_TOP_MENU: {
                int tot = 4;
                if (key == '.') move_cursor(1, tot);
                else if (key == ';') move_cursor(-1, tot);
                else if (key == '1') { cursor_pos = 0; enter_pressed = true; }
                else if (key == '2') { cursor_pos = 1; enter_pressed = true; }
                else if (key == '3') { cursor_pos = 2; enter_pressed = true; }
                else if (key == '4') { cursor_pos = 3; enter_pressed = true; }
                else if (key == 'l') {
                    showing_recent_albums = true;
                    parent_state_of_album = STATE_TOP_MENU;
                    filtered_album_count = 0;
                    for (int i=0; i<5; i++) {
                        if (recent_albums[i] != 0) {
                            filtered_albums[filtered_album_count++] = recent_albums[i];
                        }
                    }
                    if (filtered_album_count > 0) {
                        current_state = STATE_ALBUM_LIST;
                        cursor_pos = 0;
                        cloud_force_redraw = true;
                    }
                }
                
                if (key == ' ' || enter_pressed) {
                    saved_cursor_top = cursor_pos;
                    int next_action = cursor_pos;
                    if (next_action == 0) {
                        filter_company_id = 0xFFFF;
                        filter_chip_mask = 0;
                        filtered_album_count = cloud_db_album_count();
                        for (uint16_t i = 0; i < filtered_album_count; i++) filtered_albums[i] = i;
                        parent_state_of_album = STATE_TOP_MENU;
                        current_state = STATE_ALBUM_LIST;
                        cursor_pos = 0;
                    } else if (next_action == 1) {
                        current_state = STATE_COMPANY_LIST;
                        cursor_pos = saved_cursor_company;
                    } else if (next_action == 2) {
                        current_state = STATE_COMPOSER_LIST;
                        cursor_pos = saved_cursor_composer;
                    } else if (next_action == 3) {
                        current_state = STATE_CHIP_LIST;
                        cursor_pos = saved_cursor_chip;
                    }
                    cloud_force_redraw = true;
                }
                break;
            }
            case STATE_COMPANY_LIST: {
                int tot = cloud_db_company_count();
                if (tot <= 0) tot = 1;
                if (key == '.') move_cursor(1, tot);
                else if (key == ';') move_cursor(-1, tot);
                else if (key == ',') move_cursor(-5, tot);
                else if (key == '/') move_cursor(5, tot);
                else if (key == 'b' || key == 'B') { 
                    saved_cursor_company = cursor_pos;
                    current_state = STATE_TOP_MENU; 
                    cursor_pos = saved_cursor_top;
                    cloud_force_redraw = true; 
                }
                else if (key == ' ' || enter_pressed) {
                    saved_cursor_company = cursor_pos;
                    filter_company_id = cursor_pos;
                    filter_chip_mask = 0;
                    filtered_album_count = cloud_db_get_albums_by_company(filter_company_id, filtered_albums, 6000);
                    parent_state_of_album = STATE_COMPANY_LIST;
                    current_state = STATE_ALBUM_LIST;
                    cursor_pos = 0;
                    cloud_force_redraw = true;
                }
                break;
            }
            case STATE_COMPOSER_LIST: {
                int total = cloud_db_composer_count();
                int tot = total > 0 ? total : 1;
                if (key == '.') move_cursor(1, tot);
                else if (key == ';') move_cursor(-1, tot);
                else if (key == '/') move_cursor(5, tot);
                else if (key == ',') move_cursor(-5, tot);
                else if (key == 'b' || key == 'B') { 
                    saved_cursor_composer = cursor_pos;
                    current_state = STATE_TOP_MENU; 
                    cursor_pos = saved_cursor_top;
                    cloud_force_redraw = true; 
                }
                else if (key == '1') {
                    init_composer_sort(1);
                    cursor_pos = 0;
                }
                else if (key == '2') {
                    init_composer_sort(2);
                    cursor_pos = 0;
                }
                else if (key == '0') {
                    init_composer_sort(0);
                    cursor_pos = 0;
                }
                else if (key == ' ' || enter_pressed) {
                    saved_cursor_composer = cursor_pos;
                    uint16_t comp_id = get_sorted_composer_id(cursor_pos);
                    filtered_album_count = cloud_db_get_albums_by_composer(comp_id, filtered_albums, 6000);
                    parent_state_of_album = STATE_COMPOSER_LIST;
                    current_state = STATE_ALBUM_LIST;
                    cursor_pos = 0;
                    cloud_force_redraw = true;
                }
                break;
            }
            case STATE_CHIP_LIST: {
                int tot = NUM_SUPPORTED_CHIPS;
                if (tot <= 0) tot = 1;
                if (key == '.') move_cursor(1, tot);
                else if (key == ';') move_cursor(-1, tot);
                else if (key == ',') move_cursor(-5, tot);
                else if (key == '/') move_cursor(5, tot);
                else if (key == 'b' || key == 'B') { 
                    saved_cursor_chip = cursor_pos;
                    current_state = STATE_TOP_MENU; 
                    cursor_pos = saved_cursor_top;
                    cloud_force_redraw = true; 
                }
                else if (key == ' ' || enter_pressed) {
                    saved_cursor_chip = cursor_pos;
                    filter_chip_mask = SUPPORTED_CHIPS[cursor_pos].mask;
                    filter_company_id = 0xFFFF;
                    filtered_album_count = cloud_db_get_albums_by_chip(filter_chip_mask, filtered_albums, 6000);
                    parent_state_of_album = STATE_CHIP_LIST;
                    current_state = STATE_ALBUM_LIST;
                    cursor_pos = 0;
                    cloud_force_redraw = true;
                }
                break;
            }
            case STATE_ALBUM_LIST: {
                int tot = filtered_album_count > 0 ? filtered_album_count : 1;
                if (key == '.') move_cursor(1, tot);
                else if (key == ';') move_cursor(-1, tot);
                else if (key == ',') move_cursor(-5, tot);
                else if (key == '/') move_cursor(5, tot);
                else if (key == 'b' || key == 'B') { 
                    saved_cursor_album = cursor_pos;
                    current_state = parent_state_of_album; 
                    showing_recent_albums = false;
                    if (current_state == STATE_TOP_MENU) cursor_pos = saved_cursor_top;
                    else if (current_state == STATE_COMPANY_LIST) cursor_pos = saved_cursor_company;
                    else if (current_state == STATE_COMPOSER_LIST) cursor_pos = saved_cursor_composer;
                    else if (current_state == STATE_CHIP_LIST) cursor_pos = saved_cursor_chip;
                    cloud_force_redraw = true; 
                }
                else if (key == ' ' || enter_pressed) {
                    if (filtered_album_count > 0) {
                        saved_cursor_album = cursor_pos;
                        selected_album_id = filtered_albums[cursor_pos];
                        add_recent_album(selected_album_id);
                        filtered_track_count = cloud_db_get_tracks_by_album(selected_album_id, filtered_tracks, 1000);
                        current_state = STATE_TRACK_LIST;
                        cursor_pos = 0;
                        cloud_force_redraw = true;
                    }
                }
                break;
            }
            case STATE_TRACK_LIST: {
                int tot = filtered_track_count > 0 ? filtered_track_count : 1;
                if (key == '.') move_cursor(1, tot);
                else if (key == ';') move_cursor(-1, tot);
                else if (key == ',') move_cursor(-5, tot);
                else if (key == '/') move_cursor(5, tot);
                else if (key == 'b' || key == 'B') { 
                    current_state = STATE_ALBUM_LIST; 
                    cursor_pos = saved_cursor_album;
                    cloud_force_redraw = true; 
                }
                else if (key == 'm' || key == 'M') { 
                    reset_to_top(); 
                    cloud_force_redraw = true; 
                }
                else if (key == ' ' || enter_pressed) {
                    if (filtered_track_count > 0) {
                        auto_play_active = false;
                        current_play_mode = MODE_SINGLE;
                        cloud_play_track_at(cursor_pos);
                    }
                }
                else if (key == 'a' || key == 'A') {
                    if (filtered_track_count > 0) {
                        auto_play_active = true;
                        current_play_mode = MODE_ALL;
                        auto_play_cursor = cursor_pos;
                        cloud_play_track_at(cursor_pos);
                    }
                }
                else if (key == 'r' || key == 'R') {
                    if (filtered_track_count > 0) {
                        auto_play_active = true;
                        current_play_mode = MODE_RANDOM;
                        auto_play_cursor = cursor_pos;
                        cloud_play_track_at(cursor_pos);
                    }
                }
                break;
            }
        }
        
        // Since we already processed keys, we don't want the drawing switch to process them again.
        // We will just set keyChanged to false so the drawing blocks ignore it.
        keyChanged = false;
    }
    // === END NEW KEY PROCESSING LOGIC ===

    // 描画の必要がないフレームはここで即リターン
    if (!cloud_force_redraw) return;
    cloud_force_redraw = false;
    if (vgm_engine_is_playing()) return;

    auto& canvas = M5Cardputer.Display;
    canvas.fillScreen(BLACK);
    canvas.setTextSize(1.5f);

    switch (current_state) {
        // ============================================================
        // 0. BOOT MENU
        // ============================================================
        case STATE_BOOT_MENU: {
            const char* items[] = {"1. Online", "2. Local", "3. WiFi Setup"};
            int num_items = 3;

            canvas.setTextColor(CYAN);
            canvas.drawString("vgmM5 0.98", 10, 10);
            canvas.drawLine(10, 26, 230, 26, DARKGREY);

            for (int i = 0; i < num_items; i++) {
                int y = 34 + i * 18;
                if (i == cursor_pos) {
                    canvas.fillRect(10, y, 220, 18, DARKGREY);
                    canvas.setTextColor(YELLOW);
                    canvas.drawString(String("> ") + items[i], 14, y + 2);
                } else {
                    canvas.setTextColor(WHITE);
                    canvas.drawString(String("  ") + items[i], 14, y + 2);
                }
            }

            canvas.setTextSize(1.0f);
            if (WiFi.status() == WL_CONNECTED) {
                canvas.setTextColor(GREEN);
                canvas.drawString("WiFi: Connected", 10, 100);
            } else {
                canvas.setTextColor(RED);
                canvas.drawString("WiFi: Disconnected", 10, 100);
            }
            canvas.setTextColor(0x8410);
            canvas.drawString("1-3: Direct   Enter/Space: OK", 10, 120);

            if (keyChanged) {
                if (key == '.') move_cursor(1, num_items);
                else if (key == ';') move_cursor(-1, num_items);
                else if (key == '1') { cursor_pos = 0; enter_pressed = true; }
                else if (key == '2') { cursor_pos = 1; enter_pressed = true; }
                else if (key == '3') { cursor_pos = 2; enter_pressed = true; }

                if (key == ' ' || enter_pressed) {
                    if (cursor_pos == 0) {
                        if (WiFi.status() != WL_CONNECTED) {
                            canvas.fillScreen(BLACK);
                            canvas.setTextSize(1.5f);
                            canvas.setTextColor(YELLOW);
                            canvas.drawString("Connecting WiFi...", 10, 60);
                            delay(100);
                            wifi_manager_connect_saved();
                            if (WiFi.status() != WL_CONNECTED) {
                                canvas.fillScreen(BLACK);
                                canvas.setTextColor(RED);
                                canvas.drawString("WiFi Required!", 10, 30);
                                canvas.setTextColor(WHITE);
                                canvas.drawString("Please setup WiFi", 10, 50);
                                delay(2000);
                                cursor_pos = 2;
                                cloud_force_redraw = true;
                                break;
                            }
                        }
                        if (!cloud_db_is_ready()) {
                            canvas.fillScreen(BLACK);
                            canvas.setTextSize(1.5f);
                            canvas.setTextColor(WHITE);
                            canvas.drawString("Loading DB...", 10, 60);
                            delay(100);
                            if (!cloud_db_init()) {
                                canvas.setTextColor(RED);
                                canvas.drawString("DB Load Failed!", 10, 80);
                                delay(2000);
                                cloud_force_redraw = true;
                                break;
                            }
                        }
                        current_state = STATE_TOP_MENU;
                        cursor_pos = saved_cursor_top;
                        cloud_force_redraw = true;

                    } else if (cursor_pos == 1) {
                        current_state = STATE_LOCAL_MODE;
                        cloud_force_redraw = true;

                    } else if (cursor_pos == 2) {
                        canvas.fillScreen(BLACK);
                        canvas.setTextSize(1.5f);
                        canvas.setTextColor(YELLOW);
                        canvas.drawString("WiFi Setup...", 10, 60);
                        delay(300);
                        wifi_manager_clear_saved();
                        wifi_manager_prompt_and_save();
                        cursor_pos = 0;
                        cloud_force_redraw = true;
                    }
                }
            }
            break;
        }

        // ============================================================
        // 1. TOP MENU
        // ============================================================
        case STATE_TOP_MENU: {
            const char* items[] = {"1. All Albums", "2. By Company", "3. By Composer", "4. By Sound Chip"};
            int num_items = 4;

            canvas.setTextColor(0x7FFD);
            canvas.drawString("Cloud VGM Player", 10, 8);
            canvas.drawLine(10, 24, 230, 24, 0x3975);

            for (int i = 0; i < num_items; i++) {
                int y = 38 + i * 22;
                if (i == cursor_pos) {
                    canvas.fillRect(8, y, 224, 18, 0x3030);
                    canvas.setTextColor(0xFFE0);
                    canvas.drawString("> " + String(items[i]), 14, y + 2);
                } else {
                    canvas.setTextColor(0xAAAA);
                    canvas.drawString("  " + String(items[i]), 14, y + 2);
                }
            }

            canvas.setTextSize(1.0f);
            canvas.setTextColor(0x8410);
            canvas.drawString(".,;/: Nav  1/2: Sort  Enter: Sel  B: Back", 5, 120);

            if (keyChanged) {
                int next_action = -1;
                if (key == '.') move_cursor(1, num_items);
                else if (key == ';') move_cursor(-1, num_items);
                else if (key == '1') next_action = 0;
                else if (key == '2') next_action = 1;
                else if (key == '3') next_action = 2;
                else if (key == 'b' || key == 'B') { 
                    saved_cursor_top = cursor_pos;
                    current_state = STATE_BOOT_MENU; 
                    cursor_pos = 0; 
                    cloud_force_redraw = true; 
                }
                else if (key == ' ' || enter_pressed) next_action = cursor_pos;
                
                if (next_action != -1) {
                    saved_cursor_top = (next_action >= 0 && next_action < 3) ? next_action : 0;
                    if (next_action == 0) {
                        filtered_album_count = cloud_db_album_count();
                        for (uint16_t i = 0; i < filtered_album_count; i++) filtered_albums[i] = i;
                        parent_state_of_album = STATE_TOP_MENU;
                        current_state = STATE_ALBUM_LIST;
                        cursor_pos = 0;
                    } else if (next_action == 1) {
                        current_state = STATE_COMPANY_LIST;
                        cursor_pos = saved_cursor_company;
                    } else if (next_action == 2) {
                        current_state = STATE_CHIP_LIST;
                        cursor_pos = saved_cursor_chip;
                    }
                    cloud_force_redraw = true;
                }
            }
            break;
        }

        // ============================================================
        // 2. COMPANY LIST
        // ============================================================
        case STATE_COMPANY_LIST: {
            canvas.setTextColor(0x7FFD);
            canvas.drawString("Select Company", 10, 8);
            canvas.drawLine(10, 24, 230, 24, 0x3975);

            int total = cloud_db_company_count();
            int page_start = (cursor_pos / 5) * 5;
            int page_count = (total < 5) ? total : 5;
            if (page_start + page_count > total) page_count = total - page_start;

            for (int i = 0; i < page_count; i++) {
                int idx = page_start + i;
                int y = 38 + i * 18;
                MasterRecord co = cloud_db_get_company(idx);
                String name = cloud_db_get_string(co.name_offset);
                if (name.length() > 26) name = name.substring(0, 26) + "..";
                
                if (idx == cursor_pos) {
                    canvas.fillRect(8, y, 224, 18, 0x3030);
                    canvas.setTextColor(0xFFE0);
                    canvas.drawString("> " + name, 14, y + 2);
                } else {
                    canvas.setTextColor(0xAAAA);
                    canvas.drawString("  " + name, 14, y + 2);
                }
            }

            canvas.setTextSize(1.0f);
            canvas.setTextColor(0x8410);
            canvas.drawString(".,;/: Nav  1/2: Sort  Enter: Sel  B: Back", 5, 120);

            if (keyChanged) {
                int tot = total > 0 ? total : 1;
                if (key == '.') move_cursor(1, tot);
                else if (key == ';') move_cursor(-1, tot);
                else if (key == ',') move_cursor(-5, tot);
                else if (key == '/') move_cursor(5, tot);
                else if (key == 'b' || key == 'B') { 
                    saved_cursor_company = cursor_pos;
                    current_state = STATE_TOP_MENU; 
                    cursor_pos = saved_cursor_top;
                    cloud_force_redraw = true; 
                }
                else if (key == ' ' || enter_pressed) {
                    saved_cursor_company = cursor_pos;
                    filter_company_id = cursor_pos;
                    filter_chip_mask = 0;
                    filtered_album_count = cloud_db_get_albums_by_company(filter_company_id, filtered_albums, 6000);
                    parent_state_of_album = STATE_COMPANY_LIST;
                    current_state = STATE_ALBUM_LIST;
                    cursor_pos = 0;
                    cloud_force_redraw = true;
                }
            }
            break;
        }

        // ============================================================
        // 3. CHIP LIST
        // ============================================================
                case STATE_COMPOSER_LIST: {
            disptitle(STATCLR, (char*)"Select Composer");

            int total = cloud_db_composer_count();
            int page_start = (cursor_pos / 5) * 5;
            int page_count = (total < 5) ? total : 5;
            if (page_start + page_count > total) page_count = total - page_start;

            for (int i = 0; i < page_count; i++) {
                int idx = page_start + i;
                int y = 38 + i * 18;
                MasterRecord co = cloud_db_get_composer(get_sorted_composer_id(idx));
                String name = cloud_db_get_string(co.name_offset);
                if (name.length() > 26) name = name.substring(0, 26) + "..";
                
                if (idx == cursor_pos) {
                    canvas.fillRect(8, y, 224, 18, 0x3030);
                    canvas.setTextColor(0xFFE0);
                    canvas.drawString("> " + name, 14, y + 2);
                } else {
                    canvas.setTextColor(0xAAAA);
                    canvas.drawString("  " + name, 14, y + 2);
                }
            }

            canvas.setTextSize(1.0f);
            canvas.setTextColor(0x8410);
            canvas.drawString(".,;/: Nav  1/2: Sort  Enter: Sel  B: Back", 5, 120);
            
            // Note: The input handling is done at the top now, we shouldn't need duplicate if (keyChanged) 
            // inside the drawing block if it's already at the top. Let's just break;
            break;
        }
case STATE_CHIP_LIST: {
            canvas.setTextColor(0x7FFD);
            canvas.drawString("Select Sound Chip", 10, 8);
            canvas.drawLine(10, 24, 230, 24, 0x3975);

            int page_start = (cursor_pos / 5) * 5;
            int page_count = (NUM_SUPPORTED_CHIPS < 5) ? NUM_SUPPORTED_CHIPS : 5;
            if (page_start + page_count > NUM_SUPPORTED_CHIPS) page_count = NUM_SUPPORTED_CHIPS - page_start;

            for (int i = 0; i < page_count; i++) {
                int idx = page_start + i;
                int y = 46 + i * 16;
                if (idx == cursor_pos) {
                    canvas.fillRect(8, y, 224, 18, 0x3030);
                    canvas.setTextColor(0xFFE0);
                    canvas.drawString("> " + String(SUPPORTED_CHIPS[idx].name), 14, y + 2);
                } else {
                    canvas.setTextColor(0xAAAA);
                    canvas.drawString("  " + String(SUPPORTED_CHIPS[idx].name), 14, y + 2);
                }
            }

            canvas.setTextSize(1.0f);
            canvas.setTextColor(0x8410);
            canvas.drawString(".,;/: Nav  1/2: Sort  Enter: Sel  B: Back", 5, 120);

            if (keyChanged) {
                int tot = NUM_SUPPORTED_CHIPS;
                if (key == '.') move_cursor(1, tot);
                else if (key == ';') move_cursor(-1, tot);
                else if (key == ',') move_cursor(-5, tot);
                else if (key == '/') move_cursor(5, tot);
                else if (key == 'b' || key == 'B') { 
                    saved_cursor_chip = cursor_pos;
                    current_state = STATE_TOP_MENU; 
                    cursor_pos = saved_cursor_top;
                    cloud_force_redraw = true; 
                }
                else if (key == ' ' || enter_pressed) {
                    saved_cursor_chip = cursor_pos;
                    filter_chip_mask = SUPPORTED_CHIPS[cursor_pos].mask;
                    filter_company_id = 0xFFFF;
                    filtered_album_count = cloud_db_get_albums_by_chip(filter_chip_mask, filtered_albums, 6000);
                    parent_state_of_album = STATE_CHIP_LIST;
                    current_state = STATE_ALBUM_LIST;
                    cursor_pos = 0;
                    cloud_force_redraw = true;
                }
            }
            break;
        }

        // ============================================================
        // 4. ALBUM LIST
        // ============================================================
        case STATE_ALBUM_LIST: {
            canvas.setTextColor(0x7FFD);
            if (showing_recent_albums) {
                canvas.drawString("Recent Albums (" + String(filtered_album_count) + ")", 10, 8);
            } else {
                canvas.drawString("Albums (" + String(filtered_album_count) + ")", 10, 8);
            }
            canvas.drawLine(10, 24, 230, 24, 0x3975);

            int page_start = (cursor_pos / 5) * 5;
            int page_count = (filtered_album_count < 5) ? filtered_album_count : 5;
            if (page_start + page_count > filtered_album_count) page_count = filtered_album_count - page_start;

            for (int i = 0; i < page_count; i++) {
                int idx = page_start + i;
                int y = 32 + i * 18;
                AlbumRecord alb = cloud_db_get_album(filtered_albums[idx]);
                String title = cloud_db_get_string(alb.title_offset);
                if (title.length() > 26) title = title.substring(0, 26) + "..";
                
                if (idx == cursor_pos) {
                    canvas.fillRect(8, y, 224, 18, 0x3030);
                    canvas.setTextColor(0xFFE0);
                    canvas.drawString("> " + title, 14, y + 2);
                } else {
                    canvas.setTextColor(0xAAAA);
                    canvas.drawString("  " + title, 14, y + 2);
                }
            }

            canvas.setTextSize(1.0f);
            canvas.setTextColor(0x8410);
            canvas.drawString(".,;/: Nav  1/2: Sort  Enter: Sel  B: Back", 5, 120);

            if (keyChanged) {
                int tot = filtered_album_count > 0 ? filtered_album_count : 1;
                if (key == '.') move_cursor(1, tot);
                else if (key == ';') move_cursor(-1, tot);
                else if (key == ',') move_cursor(-5, tot);
                else if (key == '/') move_cursor(5, tot);
                else if (key == 'b' || key == 'B') { 
                    saved_cursor_album = cursor_pos;
                    current_state = parent_state_of_album; 
                    showing_recent_albums = false;
                    if (current_state == STATE_TOP_MENU) cursor_pos = saved_cursor_top;
                    else if (current_state == STATE_COMPANY_LIST) cursor_pos = saved_cursor_company;
                    else if (current_state == STATE_COMPOSER_LIST) cursor_pos = saved_cursor_composer;
                    else if (current_state == STATE_CHIP_LIST) cursor_pos = saved_cursor_chip;
                    cloud_force_redraw = true; 
                }
                else if (key == ' ' || enter_pressed) {
                    if (filtered_album_count > 0) {
                        saved_cursor_album = cursor_pos;
                        selected_album_id = filtered_albums[cursor_pos];
                        add_recent_album(selected_album_id);
                        filtered_track_count = cloud_db_get_tracks_by_album(selected_album_id, filtered_tracks, 1000);
                        current_state = STATE_TRACK_LIST;
                        cursor_pos = 0;
                        cloud_force_redraw = true;
                    }
                }
            }
            break;
        }

        // ============================================================
        // 5. TRACK LIST
        // ============================================================
        case STATE_TRACK_LIST: {
            AlbumRecord alb = cloud_db_get_album(selected_album_id);
            String albTitle = cloud_db_get_string(alb.title_offset);
            canvas.setTextColor(0x7FFD);
            canvas.drawString("Tracks (" + String(filtered_track_count) + ")", 10, 8);
            canvas.setTextSize(1.0f);
            canvas.setTextColor(0xAAAA);
            String at = albTitle;
            if (at.length() > 30) at = at.substring(0, 30) + "..";
            canvas.drawString(at, 10, 22);
            canvas.drawLine(10, 28, 230, 28, 0x3975);

            int page_start = (cursor_pos / 5) * 5;
            int page_count = (filtered_track_count < 5) ? filtered_track_count : 5;
            if (page_start + page_count > filtered_track_count) page_count = filtered_track_count - page_start;

            for (int i = 0; i < page_count; i++) {
                int idx = page_start + i;
                int y = 32 + i * 18;
                uint32_t tid = filtered_tracks[idx];
                TrackRecord trk = cloud_db_get_track(tid);
                String tname = "";
                
                String short_url = cloud_db_get_track_path(tid);
                bool is_cached = cache_has_file(short_url);
                String filename = short_url;
                if (filename.length() > 0) {
                    filename.replace("%20", " ");
                    filename.replace("%27", "'");
                    filename.replace("%28", "(");
                    filename.replace("%29", ")");
                    filename.replace("%26", "&");
                    filename.replace("%2C", ",");
                    if (filename.endsWith(".vgz") || filename.endsWith(".vgm")) {
                        filename = filename.substring(0, filename.length() - 4);
                    }
                    tname = filename;
                } else {
                    tname = cloud_db_get_string(trk.title_offset);
                }
                
                if (tname.length() > 26) tname = tname.substring(0, 26) + "..";
                
                if (idx == cursor_pos) {
                    canvas.fillRect(8, y, 224, 18, 0x3030);
                    canvas.setTextColor(0xFFE0);
                    canvas.drawString("> ", 14, y + 2);
                    canvas.setTextColor(is_cached ? 0x07E0 : 0xFFE0);
                    canvas.drawString(tname, 14 + canvas.textWidth("> "), y + 2);
                } else {
                    canvas.setTextColor(is_cached ? 0x07E0 : 0xAAAA);
                    canvas.drawString("  " + tname, 14, y + 2);
                }
            }

            canvas.setTextSize(1.0f);
            canvas.setTextColor(0x8410);
            canvas.drawString("Enter:Play A:All R:Rnd M:Menu", 10, 120);

            if (keyChanged) {
                int tot = filtered_track_count > 0 ? filtered_track_count : 1;
                if (key == '.') move_cursor(1, tot);
                else if (key == ';') move_cursor(-1, tot);
                else if (key == ',') move_cursor(-5, tot);
                else if (key == '/') move_cursor(5, tot);
                else if (key == 'b' || key == 'B') { 
                    current_state = STATE_ALBUM_LIST; 
                    cursor_pos = saved_cursor_album;
                    cloud_force_redraw = true; 
                }
                else if (key == 'm' || key == 'M') { 
                    reset_to_top(); 
                    cloud_force_redraw = true; 
                }
                else if (key == ' ' || enter_pressed) {
                    if (filtered_track_count > 0) {
                        auto_play_active = false;
                        current_play_mode = MODE_SINGLE;
                        cloud_play_track_at(cursor_pos);
                    }
                }
                else if (key == 'a' || key == 'A') {
                    if (filtered_track_count > 0) {
                        auto_play_active = true;
                        current_play_mode = MODE_ALL;
                        auto_play_cursor = cursor_pos;
                        cloud_play_track_at(cursor_pos);
                    }
                }
                else if (key == 'r' || key == 'R') {
                    if (filtered_track_count > 0) {
                        auto_play_active = true;
                        current_play_mode = MODE_RANDOM;
                        auto_play_cursor = cursor_pos;
                        cloud_play_track_at(cursor_pos);
                    }
                }
            }
            break;
        }
    }
    
    if (s_now_playing) {
        canvas.setTextSize(1.0f);
        canvas.setTextColor(0x07E0);
        canvas.drawString("Playing", 190, 8);
    }
}

#endif // IS_CARDPUTER

// 待機中（ファイル選択モード）の処理 — AtomS3 version
#if defined(IS_ATOMS3)
static void atoms3_play_current();

// 再生時・停止時共通のボタン判定関数
static void atoms3_handle_buttons() {
    M5.update();
    static int clickCount = 0;
    static unsigned long lastClickTime = 0;

    if (M5.BtnA.wasPressed()) {
        unsigned long currentMillis = millis();
        if (currentMillis - lastClickTime < 400) clickCount++;
        else clickCount = 1;
        lastClickTime = currentMillis;
    }

    if (clickCount == 1 && (millis() - lastClickTime >= 400)) {
        bool is_playing = vgm_engine_is_playing() || mdx_engine_is_playing();
        if (is_playing) {
            Serial.println("AtomS3: Stop Playback");
            vgm_engine_stop();
            mdx_engine_stop();
        } else {
            Serial.println("AtomS3: Start Playback");
            atoms3_play_current();
        }
        clickCount = 0;
    } else if (clickCount == 2 && (millis() - lastClickTime >= 400)) {
        Serial.println("AtomS3: Next Track");
        if (atoms3_file_count > 0) {
            current_file_index = (current_file_index + 1) % atoms3_file_count;
            atoms3_play_current();
        }
        clickCount = 0;
    } else if (clickCount >= 3) {
        Serial.println("AtomS3: Previous Track");
        if (atoms3_file_count > 0) {
            current_file_index = (current_file_index - 1 + atoms3_file_count) % atoms3_file_count;
            atoms3_play_current();
        }
        clickCount = 0;
    }
}

void phase_file_selection() {
    static bool initialized = false;
    if (!initialized) {
        atoms3_load_files();
        if (atoms3_file_count > 0) {
            Serial.printf("AtomS3: Found %d songs. Ready to play.\n", atoms3_file_count);
            current_file_index = 0;
            // 起動時は自動再生せず、無音待機
        } else {
            Serial.println("AtomS3: No songs found. Entering idle state.");
        }
        initialized = true;
    }

    // 停止中もボタン操作を監視
    atoms3_handle_buttons();
}

static void atoms3_play_current() {
    if (atoms3_file_count <= 0) return;
    String fname = atoms3_file_list[current_file_index];
    String lname = fname; lname.toLowerCase();
    
    // Stop any running engine first
    vgm_engine_stop();
    mdx_engine_stop();
    delay(50);
    
    if (lname.endsWith(".mdx")) {
        Serial.printf("AtomS3: Playing MDX %s\n", fname.c_str());
        // AtomS3ではSDカードがないため、第2引数に false を指定して FFat で開く
        if (mdx_engine_play(fname.c_str(), false)) {
            const char* title = mdx_engine_get_title();
            if (title == nullptr || title[0] == '\0') {
                const char* basename = strrchr(fname.c_str(), '/');
                title = basename ? basename + 1 : fname.c_str();
            }
            Serial.printf("AtomS3: MDX Title: %s\n", title);
        } else {
            Serial.printf("AtomS3: MDX Play Failed: %s\n", mdx_engine_get_error());
        }
    } else {
        Serial.printf("AtomS3: Playing VGM %s\n", fname.c_str());
        vgm_engine_play(fname.c_str(), false);
    }
}
#endif

// ============================================================================
// 3. 再生フェーズ (Playback)
// ============================================================================
// 再生中の処理（ボタン監視や画面更新）
#if !defined(IS_CORES3)
void phase_playback() {
    // 共通の再生部は audio_play_task (Core 0) 内で自動的に実行され続けている
    
    // ボードに合わせたキー入力（停止・スキップ等）
#if defined(IS_CARDPUTER)
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange()) {
            if (M5Cardputer.Keyboard.isKeyPressed(' ')) {
                Serial.println("Cardputer: Stop requested");
                vgm_engine_stop();
                mdx_engine_stop();
            }
            if (M5Cardputer.Keyboard.isKeyPressed('=')) {
                int v = M5.Speaker.getVolume() + 20;
                if(v > 255) v = 255;
                M5.Speaker.setVolume(v);
                Serial.printf("Cardputer: Volume %d\n", v);
            }
            if (M5Cardputer.Keyboard.isKeyPressed('-')) {
                int v = M5.Speaker.getVolume() - 20;
                if(v < 0) v = 0;
                M5.Speaker.setVolume(v);
                Serial.printf("Cardputer: Volume %d\n", v);
            }
            
            // Cloud Playback Navigation
            if (filtered_track_count > 0 && current_state != STATE_LOCAL_MODE) {
                if (M5Cardputer.Keyboard.isKeyPressed('.')) {
                    // Next track
                    Serial.println("Cardputer: Next Track");
                    vgm_engine_stop();
                    auto_play_cursor = (auto_play_cursor + 1) % filtered_track_count;
                    cloud_play_track_at(auto_play_cursor);
                }
                else if (M5Cardputer.Keyboard.isKeyPressed(';')) {
                    // Prev track
                    Serial.println("Cardputer: Prev Track");
                    vgm_engine_stop();
                    auto_play_cursor = (auto_play_cursor - 1 + filtered_track_count) % filtered_track_count;
                    cloud_play_track_at(auto_play_cursor);
                }
                else if (M5Cardputer.Keyboard.isKeyPressed('/')) {
                    // Next album
                    Serial.println("Cardputer: Next Album");
                    vgm_engine_stop();
                    if (filtered_album_count > 0) {
                        saved_cursor_album = (saved_cursor_album + 1) % filtered_album_count;
                        selected_album_id = filtered_albums[saved_cursor_album];
                        filtered_track_count = cloud_db_get_tracks_by_album(selected_album_id, filtered_tracks, 1000);
                        auto_play_cursor = 0;
                        cloud_play_track_at(auto_play_cursor);
                    }
                }
                else if (M5Cardputer.Keyboard.isKeyPressed(',')) {
                    // Prev album
                    Serial.println("Cardputer: Prev Album");
                    vgm_engine_stop();
                    if (filtered_album_count > 0) {
                        saved_cursor_album = (saved_cursor_album - 1 + filtered_album_count) % filtered_album_count;
                        selected_album_id = filtered_albums[saved_cursor_album];
                        filtered_track_count = cloud_db_get_tracks_by_album(selected_album_id, filtered_tracks, 1000);
                        auto_play_cursor = 0;
                        cloud_play_track_at(auto_play_cursor);
                    }
                }
            }
            

        }
#elif defined(IS_ATOMS3)
        // 再生中も共通のボタン処理を呼び出す
        atoms3_handle_buttons();
#endif
}
#endif // !IS_CORES3

// ------------------------------------------------------------
// CoreS3 UI Drawing & Logic
// ------------------------------------------------------------
#if defined(IS_CORES3)

// --- モダンなアイコン付きボタンを描画するヘルパー関数 ---
static void drawCoolIconButton(LGFX_Sprite& cv, int x, int y, int w, int h, uint32_t bg_color, int icon_type, bool is_highlight = false) {
    
    // 背景の四角形（fillSmoothRoundRectなど）はすべて削除し、画像のみを描画する
    const uint8_t* png_data = nullptr;
    size_t png_size = 0;
    
    if (icon_type == 1) {
        png_data = _binary_up_png_start;       png_size = _binary_up_png_end - _binary_up_png_start;
    } else if (icon_type == 2) {
        png_data = _binary_down_png_start;     png_size = _binary_down_png_end - _binary_down_png_start;
    } else if (icon_type == 3) {
        png_data = _binary_play_png_start;     png_size = _binary_play_png_end - _binary_play_png_start;
    } else if (icon_type == 4) {
        png_data = _binary_stop_png_start;     png_size = _binary_stop_png_end - _binary_stop_png_start;
    } else if (icon_type == 5) {
        png_data = _binary_auto_png_start;     png_size = _binary_auto_png_end - _binary_auto_png_start;
    } else if (icon_type == 6) {
        png_data = _binary_back_png_start;     png_size = _binary_back_png_end - _binary_back_png_start;
    } else if (icon_type == 7) {
        png_data = _binary_forward_png_start;  png_size = _binary_forward_png_end - _binary_forward_png_start;
    }
    
    if (png_data) {
        // 元の広めのタッチ判定(w, h)の「ど真ん中」に40x40の画像をポンと置く
        cv.drawPng(png_data, png_size, x + (w - 40) / 2, y + (h - 40) / 2);
    }
}

static void cores3_draw_browser() {
    cores3_canvas.fillScreen(TFT_BLACK);
    
    // ヘッダー (パス表示)
    cores3_canvas.fillRect(0, 0, 240, 30, TFT_NAVY);
    cores3_canvas.setTextColor(TFT_WHITE);
    cores3_canvas.setTextSize(2);
    String shortDir = cores3_current_dir;
    if (shortDir.length() > 18) shortDir = ".." + shortDir.substring(shortDir.length() - 16);
    cores3_canvas.drawString(shortDir, 10, 8);
    
    // リスト表示領域 (左側 240px)
    int y = 40;
    for (int i = 0; i < 6; i++) {
        int idx = cores3_list_offset + i;
        if (idx >= cores3_file_count) break;
        
        if (idx == cores3_cursor) {
            cores3_canvas.fillRect(0, y, 240, 30, TFT_DARKGREEN);
        }
        String dispName = cores3_file_list[idx];
        if (cores3_is_dir[idx]) dispName = "[" + dispName + "]";
        
        // 20文字以上は省略記号をつける
        if (dispName.length() > 20) {
            dispName = dispName.substring(0, 18) + "..";
        }
        
        cores3_canvas.drawString(dispName, 5, y + 8);
        y += 32;
    }

    uint32_t color_btn_bg = cores3_canvas.color565(60, 60, 80);
    bool is_playing = vgm_engine_is_playing() || mdx_engine_is_playing();
    
    // ボタンの幅とX座標を「x=230, w=80」で統一し、左ズレを解消
    drawCoolIconButton(cores3_canvas, 230, 10, 80, 40, color_btn_bg, 1);  // 1=UP
    drawCoolIconButton(cores3_canvas, 230, 55, 80, 40, color_btn_bg, 2);  // 2=DOWN

    uint32_t color_play = is_playing ? cores3_canvas.color565(220, 50, 60) : cores3_canvas.color565(30, 200, 80);
    drawCoolIconButton(cores3_canvas, 230, 110, 80, 40, color_play, is_playing ? 4 : 3, is_playing);

    uint32_t color_auto = cores3_auto_play ? cores3_canvas.color565(50, 60, 70) : color_btn_bg;
    drawCoolIconButton(cores3_canvas, 230, 165, 80, 40, color_auto, 5, cores3_auto_play);
    
    cores3_canvas.pushSprite(0, 0);
}

static void cores3_draw_playback() {
    cores3_canvas.fillScreen(TFT_BLACK);
    
    // サムネイル (左側: 幅160ドット内)
    bool has_thumb = false;
    if (cores3_thumb_path != "") {
        String lthumb = cores3_thumb_path; lthumb.toLowerCase();
        File thumbFile = SD.open(cores3_thumb_path.c_str(), FILE_READ);
        if (thumbFile) {
            size_t fsize = thumbFile.size();
            uint8_t* buf = (uint8_t*)ps_calloc(fsize, 1);
            if (buf) {
                thumbFile.read(buf, fsize);
                if (lthumb.endsWith(".jpg")) cores3_canvas.drawJpg(buf, fsize, 8, 26, 160, 160, 0, 0, JPEG_DIV_2);
                else if (lthumb.endsWith(".png")) cores3_canvas.drawPng(buf, fsize, 8, 26, 160, 160, 0, 0, 0.5, 0.5);
                else if (lthumb.endsWith(".bmp")) cores3_canvas.drawBmp(buf, fsize, 8, 26, 160, 160);
                free(buf);
                has_thumb = true;
            }
            thumbFile.close();
        }
    }
    
    // サムネイルがない場合は水色の枠と "Now printing" を描画
    if (!has_thumb) {
        cores3_canvas.drawRect(8, 26, 144, 144, TFT_CYAN);
        cores3_canvas.setTextColor(TFT_CYAN);
        cores3_canvas.setTextDatum(middle_center);
        cores3_canvas.drawString("Now printing", 8 + 72, 26 + 72);
        cores3_canvas.setTextDatum(top_left);
    }

    // 情報を右側 (x=160) から描画。
    // 専用のSprite(幅160)を作ってそこに流し込み、はみ出た分をカットする
    LGFX_Sprite textSp(&cores3_canvas);
    textSp.createSprite(160, 170);
    textSp.fillScreen(TFT_BLACK);
    textSp.setTextDatum(top_left);
    
    // ★ 折り返しを無効にする（160ドット幅ではみ出た分を見えなくする）
    textSp.setTextWrap(false);
    textSp.setFont(&fonts::lgfxJapanGothic_12); // 日本語ゴシック体（12px）を使用
    
    int text_y = 24;
    

    // ★ ここから下はフォントサイズ1（小さい文字）にする
    textSp.setTextSize(2);
    
    // アプリ名の描画 (ボールド風に見せるため x=0 と x=1 に重ね描き)
    textSp.setTextColor(TFT_CYAN);
    textSp.setCursor(0, text_y);
    textSp.print("vgmM5 v0.98");
    textSp.setCursor(1, text_y);
    textSp.println("vgmM5 v0.98");
    text_y += 36; // 1行分文字を書き、さらに1行分(12ドット)空ける

    textSp.setTextSize(1);
    // 5. ゲーム名 (GD3にデベロッパー単独枠がないためゲーム名を表示)
    String gameName = "Game: Unknown";
    if (mdx_engine_is_playing()) {
        gameName = "Game: -";
    } else {
        const char* g = vgm_engine_get_game();
        if (g && strlen(g) > 0) gameName = "Game: " + String(g);
    }
    textSp.setTextColor(TFT_WHITE);
    textSp.setCursor(0, text_y);
    textSp.println(gameName);
    text_y += 12; // ゲーム名を描画する前に1行下げる
    
 
    // タイトル文字列の取得 (MDX / VGM)
    String title = "";
    if (mdx_engine_is_playing()) {
        const char* t = mdx_engine_get_title();
        if (t) title = String(t);
    } else {
        const char* t = vgm_engine_get_title();
        if (t && strlen(t) > 0) title = String(t);
    }
    // タイトルが取得できない場合はファイル名を代用
    if (title == "") title = cores3_file_list[cores3_cursor]; 
    
    // 2. タイトル
    textSp.setTextColor(TFT_LIGHTGREY);
    textSp.setCursor(0, text_y);
    textSp.println(title);
    text_y += 12; // サイズ1の場合は行間を詰める(12ドット間隔)
    
    // 3. システム
    String sysName = "System: Unknown";
    if (mdx_engine_is_playing()) {
        sysName = "System: X68000";
    } else {
        const char* s = vgm_engine_get_system();
        if (s && strlen(s) > 0) sysName = "System: " + String(s);
    }
    textSp.setTextColor(TFT_LIGHTGREY);
    textSp.setCursor(0, text_y);
    textSp.println(sysName);
    text_y += 12;

    // 4. サウンドチップ
    String chipName = "Chip: Unknown";
    if (mdx_engine_is_playing()) {
        chipName = "Chip: YM2151+OKI";
    } else {
        const char* c = vgm_engine_get_chip();
        if (c && strlen(c) > 0) chipName = "Chip: " + String(c);
    }
    textSp.setCursor(0, text_y);
    textSp.println(chipName);
    text_y += 12;


    // 6. コンポーザー
    String compName = "Comp: Unknown";
    if (mdx_engine_is_playing()) {
        compName = "Comp: -";
    } else {
        const char* a = vgm_engine_get_author();
        if (a && strlen(a) > 0) compName = "Comp: " + String(a);
    }
    textSp.setCursor(0, text_y);
    textSp.println(compName);
    
    // Status display (yellow)
    text_y += 24;
    String statusStr = "";
    if (vgm_engine_is_playing() || mdx_engine_is_playing()) {
        if (cores3_auto_play) {
            statusStr = "<auto playing>";
        } else {
            statusStr = "<playing>";
        }
    }
    textSp.setTextColor(TFT_YELLOW);
    textSp.setCursor(0, text_y);
    textSp.println(statusStr);
    
    // 生成したテキスト用Spriteを x=160, y=10 に転送してメモリ解放
    textSp.pushSprite(160, 10);
    textSp.deleteSprite();
    
    // 4つのボタンを最下部 (y=180) に配置 (画面幅320 ÷ 4 = 幅80ずつ)
    uint32_t color_btn_bg = cores3_canvas.color565(60, 60, 80);
    bool is_playing = vgm_engine_is_playing() || mdx_engine_is_playing();
    
    drawCoolIconButton(cores3_canvas, 0,   180, 80, 40, color_btn_bg, 1); // [1] ブラウザへ戻る (up.png)
    drawCoolIconButton(cores3_canvas, 80,  180, 80, 40, color_btn_bg, 6); // [6] 1曲前 (back.png)
    
    uint32_t color_play = is_playing ? cores3_canvas.color565(220, 50, 60) : cores3_canvas.color565(30, 200, 80);
    drawCoolIconButton(cores3_canvas, 160, 180, 80, 40, color_play, is_playing ? 4 : 3, is_playing); // [3/4] 再生/停止
    
    drawCoolIconButton(cores3_canvas, 240, 180, 80, 40, color_btn_bg, 7); // [7] 1曲次 (forward.png)
    
    cores3_canvas.pushSprite(0, 0);
}

void phase_file_selection() {
    static bool initialized = false;
    if (!initialized) {
        // cores3_load_directory("/"); // 既にスプラッシュ画面で読み込み完了
        initialized = true;
    }
    if (cores3_force_redraw) {
        cores3_draw_browser();
        cores3_force_redraw = false;
    }

    M5.update();
    auto t = M5.Touch.getDetail();
    if (t.wasClicked()) {
        int x = t.x;
        int y = t.y;
        
        if (x >= 220) {
            // 右側のボタン領域 (UP: y=10-50, DOWN: y=55-95, PLAY: y=110-150, AUTO: y=165-205)
            if (y >= 10 && y < 50) {
                // UP
                if (cores3_cursor > 0) cores3_cursor--;
                if (cores3_cursor < cores3_list_offset) cores3_list_offset--;
                cores3_force_redraw = true;
            } else if (y >= 55 && y < 95) {
                // DOWN
                if (cores3_cursor < cores3_file_count - 1) cores3_cursor++;
                if (cores3_cursor >= cores3_list_offset + 6) cores3_list_offset++;
                cores3_force_redraw = true;
            } else if (y >= 110 && y < 150) {
                // PLAY
                cores3_play_selected(false);
            } else if (y >= 165 && y < 205) {
                // AUTO: toggle on/off. When on, skip dirs and start playing
                if (cores3_auto_play) {
                    cores3_auto_play = false;
                    cores3_force_redraw = true;
                } else {
                    if (cores3_is_dir[cores3_cursor]) {
                        for (int i = 0; i < cores3_file_count; i++) {
                            if (!cores3_is_dir[i]) {
                                cores3_cursor = i;
                                break;
                            }
                        }
                    }
                    cores3_play_selected(true);
                }
            }
        }
    }
}

void phase_playback() {
    if (cores3_force_redraw) {
        cores3_draw_playback();
        cores3_force_redraw = false;
    }

    M5.update();
    auto t = M5.Touch.getDetail();
    if (t.wasClicked()) {
        int x = t.x;
        int y = t.y;
        
        // サムネイル領域 (x=8~168, y=26~186) タップでボリューム調整
        if (x >= 8 && x <= 168 && y >= 26 && y <= 186) {
            int v = M5.Speaker.getVolume();
            if (y < 106) {
                // 上半分: ボリュームアップ
                v += 10;
                if (v > 255) v = 255;
            } else {
                // 下半分: ボリュームダウン
                v -= 10;
                if (v < 0) v = 0;
            }
            M5.Speaker.setVolume(v);
            Serial.printf("Cardputer: Volume %d\n", v);
            return;
        }
        
        if (y >= 180 && y <= 240) {
            if (x < 80) { 
                // [戻る] ブラウザに戻る
                vgm_engine_stop();
                mdx_engine_stop();
                cores3_state = CORES3_STATE_BROWSER;
                cores3_force_redraw = true;
            } else if (x >= 80 && x < 160) { 
                // [1曲前]
                if (cores3_cursor > 0) {
                    cores3_cursor--;
                    if (cores3_file_list[cores3_cursor] == "..") {
                        cores3_cursor = cores3_file_count - 1; 
                    }
                } else {
                    cores3_cursor = cores3_file_count - 1;
                }
                cores3_play_selected(cores3_auto_play);
            } else if (x >= 160 && x < 240) { 
                // [再生/停止]
                bool is_playing = vgm_engine_is_playing() || mdx_engine_is_playing();
                if (is_playing) {
                    vgm_engine_stop();
                    mdx_engine_stop();
                    cores3_auto_play = false;
                } else {
                    cores3_play_selected(cores3_auto_play);
                }
                cores3_force_redraw = true;
            } else if (x >= 240 && x < 320) { 
                // [1曲次]
                cores3_cursor++;
                if (cores3_cursor >= cores3_file_count) {
                    cores3_cursor = (cores3_file_list[0] == "..") ? 1 : 0;
                }
                cores3_play_selected(cores3_auto_play);
            }
        }
    }
}
#endif // IS_CORES3

// ============================================================================
// Main Loop
// ============================================================================
void loop() {
    static unsigned long last_update = 0;
    static bool was_playing_last_frame = false;
    
    if (millis() - last_update > 50) {
        last_update = millis();
        
        bool is_playing = vgm_engine_is_playing() || mdx_engine_is_playing();
        
        // 曲が終わった瞬間を検知
        bool just_finished = false;
        if (was_playing_last_frame && !is_playing) {
            just_finished = true;
#if defined(IS_CARDPUTER)
            // cloud_force_redraw = true; 
#endif
        }
        was_playing_last_frame = is_playing;
        
#if defined(IS_CORES3)
        if (cores3_state == CORES3_STATE_PLAYBACK) {
            // オートプレイ（順再生）処理: 次の曲へループ周回（ディレクトリはスキップ）
            if (just_finished && cores3_auto_play) {
                do {
                    cores3_cursor++;
                    if (cores3_cursor >= cores3_file_count) {
                        cores3_cursor = 0;
                    }
                } while (cores3_is_dir[cores3_cursor]);
                
                cores3_play_selected(true);
            }
            phase_playback();
        } else {
            phase_file_selection();
        }
#else
        if (is_playing) {
            phase_playback();
        } else {
            phase_file_selection();
        }
#endif
    }
    delay(10);
}

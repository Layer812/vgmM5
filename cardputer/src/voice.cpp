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

// Cardputer R67: microSD-only application.
#if defined(IS_CARDPUTER)
#include <M5Cardputer.h>
#include <SD.h>
#include "filer.hpp"
static bool cardputer_sd_ready = false;
#endif


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
        cardputer_sd_ready = SD.begin(12, SPI, 40000000);
        Serial.println(cardputer_sd_ready ? "Cardputer SD initialized" : "Cardputer SD mount failed");
        M5Cardputer.Display.fillScreen(BLACK);
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setTextColor(CYAN);
        M5Cardputer.Display.drawString("vgmM5 R67", 8, 8);
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



// Cardputer R67: microSD-only menu and playback view.
#if defined(IS_CARDPUTER)
static String cardputer_display_title;
static bool cardputer_player_dirty = true;
static uint32_t cardputer_track_start_ms = 0;
static bool cardputer_browser_initialized = false;

static String cardputer_fit_text(String value, int max_px) {
    auto& lcd = M5Cardputer.Display;
    if (lcd.textWidth(value) <= max_px) return value;
    while (value.length() && lcd.textWidth(value + "..") > max_px) {
        int cut = (int)value.length() - 1;
        while (cut > 0 && (((uint8_t)value[(unsigned)cut] & 0xC0u) == 0x80u)) --cut;
        value.remove((unsigned)cut);
    }
    return value + "..";
}

void disptitle(int stat, char *title) {
    auto& lcd = M5Cardputer.Display;
    lcd.fillRect(0, 0, 240, 35, BLACK);
    lcd.setTextSize(2);
    lcd.setTextColor(CYAN);
    lcd.drawString("vgmM5 R67", 6, 0);
    lcd.setTextSize(1.5f);
    lcd.setTextColor(stat == STATLOOP ? GREEN : LIGHTGREY);
    if (title && title[0]) lcd.drawString(cardputer_fit_text(String(title), 228), 6, 19);
    lcd.drawLine(0, 34, 239, 34, OLIVE);
    if (stat == STATLOOP && title) {
        cardputer_display_title = title;
        cardputer_track_start_ms = millis();
    }
    cardputer_player_dirty = true;
}

static void cardputer_error(const char *message) {
    auto& lcd = M5Cardputer.Display;
    lcd.fillScreen(BLACK);
    lcd.setTextSize(2);
    lcd.setTextColor(CYAN);
    lcd.drawString("vgmM5 R67", 8, 8);
    lcd.setTextSize(1.5f);
    lcd.setTextColor(YELLOW);
    lcd.drawString(cardputer_fit_text(String(message ? message : "Unknown error"), 226), 8, 48);
    delay(850);
}

// Boot directly to the SD file browser; no online/local/Wi-Fi selection.
void phase_file_selection() {
    if (!cardputer_browser_initialized) {
        filerinit();
        cardputer_browser_initialized = true;
    }
    if (!cardputer_sd_ready) {
        cardputer_error("Insert microSD card");
        cardputer_sd_ready = SD.begin(12, SPI, 20000000);
        return;
    }
    disptitle(STATCLR, (char*)"MICRO SD");
    filenum = makevgmlist(SD);
    const int i = selectfile();
    if (i < 0 || i >= filenum || !filelist) return;
    uint8_t path[PATHMAX] = {0};
    if (!cnvfile(SD, &filelist[i], path)) return;
    const bool is_mdx = filelist[i].type == TYPE_MDX;
    if (!is_mdx && filelist[i].type != TYPE_VGM) return;
    vgm_engine_stop();
    mdx_engine_stop();
    delay(20);
    disptitle(STATLOAD, (char*)"LOADING");
    const bool ok = is_mdx ? mdx_engine_play((const char*)path, true)
                           : vgm_engine_play((const char*)path, true);
    if (!ok) {
        cardputer_error(is_mdx ? mdx_engine_get_error() : vgm_engine_get_error());
        return;
    }
    const char *title = is_mdx ? mdx_engine_get_title() : vgm_engine_get_title();
    const char *name = strrchr((const char*)path, '/');
    if (!title || !title[0]) title = name ? name + 1 : (const char*)path;
    disptitle(STATLOOP, (char*)title);
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
#if defined(IS_CARDPUTER)
// R67 240x135 playback screen. The synth/DMA tasks never draw.
// Static labels are drawn on track/key changes; only the small footer
// and progress/activity strip are refreshed at a slow 2 Hz cadence.
static void cardputer_draw_playback_view() {
    const bool is_vgm = vgm_engine_is_playing();
    const bool is_mdx = mdx_engine_is_playing();
    if (!is_vgm && !is_mdx) return;

    static uint32_t last_tick = 0;
    const uint32_t now = millis();
    if (!cardputer_player_dirty && (uint32_t)(now - last_tick) < 500) return;
    last_tick = now;

    auto& lcd = M5Cardputer.Display;
    if (cardputer_player_dirty) {
        lcd.fillScreen(BLACK);
        lcd.setTextSize(2.0f);
        lcd.setTextColor(CYAN);
        lcd.drawString("vgmM5 R67", 6, 2);
        lcd.setTextSize(1.5f);
        lcd.setTextColor(is_mdx ? YELLOW : GREEN);
        lcd.drawString(is_mdx ? "MDX" : "VGM", 197, 7);
        lcd.drawLine(0, 30, 239, 30, DARKGREY);

        String title = cardputer_display_title;
        if (title.length() == 0) {
            const char* name = is_mdx ? mdx_engine_get_title() : vgm_engine_get_title();
            if (name && name[0]) title = String(name);
        }
        if (title.length() == 0) title = "Now Playing";
        lcd.setTextColor(WHITE);
        lcd.drawString(cardputer_fit_text(title, 226), 7, 37);

        String chip = is_mdx ? "YM2151 + ADPCM" : String(vgm_engine_get_chip());
        if (chip.length() == 0) chip = "Sound chip";
        lcd.setTextColor(0x7FFD);
        lcd.drawString(cardputer_fit_text(chip, 226), 7, 58);

        lcd.setTextColor(LIGHTGREY);
        lcd.drawString(cardputer_player_cloud ? "ONLINE" : "MICRO SD", 7, 78);
        lcd.setTextColor(YELLOW);
        lcd.drawString(cardputer_fit_text(
            cardputer_player_cloud ? "SPACE Stop  . Next  ; Prev"
                                  : "SPACE Stop   +/- Volume", 230), 6, 99);
        cardputer_player_dirty = false;
    }

    const uint32_t elapsed_ms = now - cardputer_track_start_ms;
    const uint32_t seconds = elapsed_ms / 1000u;
    char status[40];
    snprintf(status, sizeof(status), "VOL %3d  %02lu:%02lu",
             (int)M5.Speaker.getVolume(),
             (unsigned long)(seconds / 60u), (unsigned long)(seconds % 60u));
    lcd.fillRect(0, 115, 240, 20, BLACK);
    lcd.setTextSize(1.5f);
    lcd.setTextColor(GREEN);
    lcd.drawString(status, 7, 115);
    lcd.fillRect(7, 131, 226, 4, DARKGREY);
    if (cardputer_player_cloud && play_duration_ms > 0) {
        const uint32_t bounded = elapsed_ms > play_duration_ms ?
                                 play_duration_ms : elapsed_ms;
        const int width = (int)(((uint64_t)bounded * 226u) / play_duration_ms);
        if (width > 0) lcd.fillRect(7, 131, width, 4, GREEN);
    } else {
        // Local MDX/VGM length is unknown; movement indicates live playback,
        // it is not a misleading 100% completion progress bar.
        const int pos = (int)((seconds % 30u) * 208u / 30u);
        lcd.fillRect(7 + pos, 131, 18, 4, CYAN);
    }
}
#endif // IS_CARDPUTER

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
                cloud_force_redraw = true;
                cardputer_player_dirty = true;
            }
            if (M5Cardputer.Keyboard.isKeyPressed('=')) {
                int v = M5.Speaker.getVolume() + 20;
                if(v > 255) v = 255;
                M5.Speaker.setVolume(v);
                Serial.printf("Cardputer: Volume %d\n", v);
                cardputer_player_dirty = true;
            }
            if (M5Cardputer.Keyboard.isKeyPressed('-')) {
                int v = M5.Speaker.getVolume() - 20;
                if(v < 0) v = 0;
                M5.Speaker.setVolume(v);
                Serial.printf("Cardputer: Volume %d\n", v);
                cardputer_player_dirty = true;
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
        cardputer_draw_playback_view();

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

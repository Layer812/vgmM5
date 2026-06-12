#include <Arduino.h>
#include <M5Unified.h>
#include "vgm_engine.h"

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
    USB.begin();
    
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
#endif

void setup() {
    auto cfg = M5.config();
#if defined(IS_ATOMS3)
    // ATOM Echo S3R の内蔵スピーカーを確実に初期化するためフォールバックを指定
    cfg.fallback_board = m5::board_t::board_M5AtomVoiceS3R;
#endif
    M5.begin(cfg);
    Serial.begin(115200);

    // 共通の再生エンジン初期化（I2S等の設定）
    vgm_engine_init();

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
        }
#elif defined(IS_ATOMS3)
        Serial.println("Initializing AtomS3 Board...");
        M5.update();
        if (M5.BtnA.isPressed()) {
            enter_usb_mode();
        }
        if (!FFat.begin(true)) {
            Serial.println("FFat Mount Failed!");
            return;
        }
        Serial.println("FFat Initialized.");
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
            if (lname.endsWith(".vgm") || lname.endsWith(".vgz")) {
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


// 待機中（ファイル選択モード）の処理
void phase_file_selection() {
#if defined(IS_CARDPUTER)
        static bool card_initialized = false;
        if (!card_initialized) {
            filerinit();
            card_initialized = true;
        }

        disptitle(STATCLR, (char*)"VGM Player");
        filenum = makevgmlist(SD);
        int i = selectfile();
        
        uint8_t fname[PATHMAX];
        disptitle(STATLOAD, (char*)"VGM Player");
        if (cnvfile(SD, &filelist[i], fname)) {
            Serial.printf("Cardputer: Playing %s\n", (char*)fname);
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
            }
        }
#elif defined(IS_ATOMS3)
        static bool initialized = false;
        if (!initialized) {
            atoms3_load_files();
            if (atoms3_file_count > 0) {
                Serial.printf("AtomS3: Found %d VGM files. Ready to play.\n", atoms3_file_count);
                current_file_index = 0;
                vgm_engine_play(atoms3_file_list[current_file_index].c_str(), false);
                vgm_engine_toggle_pause(); // 起動時は一時停止状態で待機
            } else {
                Serial.println("AtomS3: No VGM files found. Entering idle state.");
            }
            initialized = true;
        } else {
            // 曲が自然に最後まで再生され終わった場合（ループ再生用）
            if (atoms3_file_count > 0) {
                current_file_index = (current_file_index + 1) % atoms3_file_count;
                vgm_engine_play(atoms3_file_list[current_file_index].c_str(), false);
            } else {
                delay(1000);
            }
        }
#endif
}

// ============================================================================
// 3. 再生フェーズ (Playback)
// ============================================================================
// 再生中の処理（ボタン監視や画面更新）
void phase_playback() {
    // 共通の再生部は audio_play_task (Core 0) 内で自動的に実行され続けている
    
    // ボードに合わせたキー入力（停止・スキップ等）
#if defined(IS_CARDPUTER)
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange()) {
            if (M5Cardputer.Keyboard.isKeyPressed(' ')) {
                Serial.println("Cardputer: Stop requested");
                vgm_engine_stop();
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
        }
#elif defined(IS_ATOMS3)
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
            Serial.println("AtomS3: Pause/Resume");
            vgm_engine_toggle_pause();
            clickCount = 0;
        } else if (clickCount == 2 && (millis() - lastClickTime >= 400)) {
            Serial.println("AtomS3: Next Track");
            vgm_engine_stop();
            current_file_index = (current_file_index + 1) % atoms3_file_count;
            vgm_engine_play(atoms3_file_list[current_file_index].c_str(), false);
            clickCount = 0;
        } else if (clickCount >= 3) {
            Serial.println("AtomS3: Previous Track");
            vgm_engine_stop();
            current_file_index = (current_file_index - 1 + atoms3_file_count) % atoms3_file_count;
            vgm_engine_play(atoms3_file_list[current_file_index].c_str(), false);
            clickCount = 0;
        }
#endif
}

// ============================================================================
// Main Loop
// ============================================================================
void loop() {
    static unsigned long last_update = 0;
    if (millis() - last_update > 50) {
        last_update = millis();
        
        if (vgm_engine_is_playing()) {
            phase_playback();
        } else {
            phase_file_selection();
        }
    }
    delay(10);
}

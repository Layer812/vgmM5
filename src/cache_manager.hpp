// cache_manager.hpp — SDカードへのVGZ/サムネイルキャッシュ管理
// vgmM5 / Layer812
#pragma once

#if defined(IS_CARDPUTER)

#include <Arduino.h>
#include <SD.h>
#include <WiFiClient.h>
#include <M5Cardputer.h>
#include "cloud_db.hpp"
#include "vgm_engine.h"

#define CACHE_VGZ_DIR    "/cache/vgz"
#define CACHE_THUMB_DIR  "/cache/thumbs"
#define MAX_DOWNLOAD_SIZE (2 * 1024 * 1024) 

// ★外部ツール：vgm_engine.cpp に実装された安全な解凍関数
extern bool vgm_tool_decompress(const char* in_filepath, const char* out_filepath);

bool cache_manager_init() {
    if (!SD.exists("/cache")) SD.mkdir("/cache");
    if (!SD.exists(CACHE_VGZ_DIR)) SD.mkdir(CACHE_VGZ_DIR);
    if (!SD.exists(CACHE_THUMB_DIR)) SD.mkdir(CACHE_THUMB_DIR);
    return true;
}

String cache_get_vgz_path(const String& short_url) {
    char buf[40];
    snprintf(buf, sizeof(buf), "%s/%08X.vgz", CACHE_VGZ_DIR, (unsigned)calculateHash(short_url));
    return String(buf);
}

bool cache_has_file(const String& short_url) {
    String vgz = cache_get_vgz_path(short_url);
    if (SD.exists(vgz)) return true;
    vgz.replace(".vgz", ".vgm");
    return SD.exists(vgz);
}

String cache_get_thumb_path(const String& thumb_path_str) {
    char buf[64];
    String ext = ".jpg"; // Always use JPG because wsrv.nl proxy converts to JPG
    snprintf(buf, sizeof(buf), "%s/%08X%s", CACHE_THUMB_DIR, (unsigned)calculateHash(thumb_path_str), ext.c_str());
    return String(buf);
}
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

class ProgressStream : public Stream {
private:
    File* _file;
    int _total_written;
    uint32_t _last_update;
    uint32_t _start_time;

public:
    ProgressStream(File* file) : _file(file), _total_written(0), _last_update(0) {
        _start_time = millis();
    }

    virtual size_t write(uint8_t c) override {
        return write(&c, 1);
    }
    
    virtual size_t write(const uint8_t *buffer, size_t size) override {
        size_t w = _file->write(buffer, size);
        _total_written += w;
        
        uint32_t now = millis();
        if (now - _last_update > 250) { // Update display every 250ms
            float elapsed_sec = (now - _start_time) / 1000.0f;
            float kbps = 0;
            if (elapsed_sec > 0.1f) {
                kbps = (_total_written / 1024.0f) / elapsed_sec;
            }
            
            M5Cardputer.Display.fillRect(150, 104, 90, 10, TFT_BLACK);
            M5Cardputer.Display.setTextSize(1.0f);
            M5Cardputer.Display.setTextColor(0xAAAA);
            M5Cardputer.Display.setCursor(150, 104);
            M5Cardputer.Display.printf("%dK(%.1fK/s)", _total_written / 1024, kbps);
            _last_update = now;
        }
        return w;
    }
    
    virtual int available() override { return 0; }
    virtual int read() override { return -1; }
    virtual int peek() override { return -1; }
    virtual void flush() override { _file->flush(); }
};

static bool _standard_download_to_sd(String url, const String& save_path) {
    bool success = false;
    Serial.printf("[DL] Start URL: %s -> %s\n", url.c_str(), save_path.c_str());
    for(int i = 0; i < 3; i++) {
        WiFiClientSecure *client = new WiFiClientSecure;
        if(client) {
            client->setInsecure(); // SSLClientの代わりに証明書検証をスキップしてRAMを節約
            client->setTimeout(30); // TCP/Read timeouts in seconds (fail fast instead of waiting 120s)
            
            HTTPClient http;
            http.setTimeout(30000); // 30 seconds timeout
            http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
            http.useHTTP10(true);
            http.addHeader("Connection", "close");
            http.setUserAgent("Mozilla/5.0");
            
            Serial.printf("[DL] Attempt %d: http.begin\n", i + 1);
            if (http.begin(*client, url)) {
                int httpCode = http.GET();
                Serial.printf("[DL] httpCode: %d\n", httpCode);
                if (httpCode == HTTP_CODE_OK) {
                    int content_length = http.getSize();
                    Serial.printf("[DL] size: %d\n", content_length);
                    File f = SD.open(save_path.c_str(), FILE_WRITE);
                    if (f) {
                          ProgressStream ps(&f);
                          int total_read = http.writeToStream(&ps);
                          f.close();
                          M5Cardputer.Display.fillRect(150, 104, 90, 10, TFT_BLACK);
                        Serial.printf("[DL] written: %d\n", total_read);
                        
                        if (content_length > 0 && total_read != content_length) {
                            SD.remove(save_path.c_str());
                            success = false;
                        } else if (total_read > 0) {
                            success = true;
                        }
                    } else {
                        Serial.println("[DL] File open failed");
                    }
                } else {
                    Serial.printf("[DL] Error: %s\n", http.errorToString(httpCode).c_str());
                }
                http.end();
            } else {
                Serial.println("[DL] http.begin failed");
            }
            delete client;
        }
        if (success) break;
        delay(2000);
    }
    Serial.printf("[DL] Success: %d\n", success);
    return success;
}

// ============================================================
// ★ レイヤー0：DB同期のための判定ロジック
// ============================================================
String cache_get_or_download_vgz(const String& short_url, const String& full_url) {
    String cache_path = cache_get_vgz_path(short_url);
    if (SD.exists(cache_path)) return cache_path;
    return _standard_download_to_sd(full_url, cache_path) ? cache_path : "";
}

String cache_ensure_vgm(const String& vgz_path) {
    if (vgz_path.length() == 0 || !SD.exists(vgz_path)) return "";
    
    String vgm_path = vgz_path;
    vgm_path.replace(".vgz", ".vgm");

    if (SD.exists(vgm_path)) return vgm_path;

    // ここでエラーが出た場合、呼び出し元(ファイラー側)が 
    // 「ファイル名はあるが未DL」とみなす挙動を維持する
    if (vgm_tool_decompress(vgz_path.c_str(), vgm_path.c_str())) {
        return vgm_path;
    }
    
    // 展開失敗時はキャッシュ自体を破棄し、空文字を返して再試行を促す
    SD.remove(vgz_path.c_str());
    return "";
}

// ============================================================
// UIサムネイル キャッシュ機構
// ============================================================
// Thumbnails logic completely removed to save flash memory
// ============================================================
void cache_draw_thumb(const String& thumb_path_str, const String& thumb_url, int x, int y, int w, int h, const char* t1 = "Now", const char* t2 = "printing") {
    uint16_t grey = M5Cardputer.Display.color565(100, 100, 100);
    M5Cardputer.Display.fillRect(x, y, w, h, grey);
    M5Cardputer.Display.setTextSize(1.0f);
    M5Cardputer.Display.setTextColor(WHITE);
    int x1 = x + (w - strlen(t1) * 6) / 2;
    int x2 = x + (w - strlen(t2) * 6) / 2;
    M5Cardputer.Display.drawString(t1, x1 > x ? x1 : x, y + 20);
    M5Cardputer.Display.drawString(t2, x2 > x ? x2 : x, y + 35);
}

void cache_prefetch_thumb(const String& thumb_path_str, const String& thumb_url) {
    return;
}

void cache_clear_all() {
    File dir = SD.open(CACHE_VGZ_DIR);
    if (!dir || !dir.isDirectory()) return;
    File file = dir.openNextFile();
    while(file) {
        String path = String(CACHE_VGZ_DIR) + "/" + file.name();
        file.close();
        SD.remove(path.c_str());
        file = dir.openNextFile();
    }
    dir.close();
}

#endif // IS_CARDPUTER
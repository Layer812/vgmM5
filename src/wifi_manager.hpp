// wifi_manager.hpp — Wi-Fi接続・NVS保存管理
// vgmM5 / Layer812
#pragma once

#if defined(IS_CARDPUTER)

#include <WiFi.h>
#include <WiFiMulti.h>
#include <Preferences.h>
#include <M5Cardputer.h>

#define WIFI_CONNECT_TIMEOUT_MS  15000
#define WIFI_NVS_NAMESPACE       "vgm_wifi"
#define WIFI_NVS_KEY_SSID        "ssid"
#define WIFI_NVS_KEY_PASS        "pass"

static bool s_wifi_connected = false;
WiFiMulti wifiMulti;

static void _wifi_clear_area() {
    M5Cardputer.Display.fillRect(0, 34, 240, 101, BLACK);
    M5Cardputer.Display.setCursor(0, 36);
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.setTextSize(1.5f);
}

static void _wifi_print(const char* msg, uint16_t color = WHITE) {
    M5Cardputer.Display.setTextColor(color);
    M5Cardputer.Display.println(msg);
    M5Cardputer.Display.setTextColor(WHITE);
}

void setClock() {
    configTime(0, 0, "pool.ntp.org");
    Serial.print("[WiFi] Waiting for NTP time sync: ");
    time_t nowSecs = time(nullptr);
    int retry = 0;
    while (nowSecs < 8 * 3600 * 2 && retry < 20) {
        delay(500);
        Serial.print(".");
        nowSecs = time(nullptr);
        retry++;
    }
    Serial.println();
    struct tm timeinfo;
    gmtime_r(&nowSecs, &timeinfo);
    Serial.print("[WiFi] Current time: ");
    char buf[26];
    Serial.println(asctime_r(&timeinfo, buf));
}

bool wifi_manager_connect_saved() {
    Preferences prefs;
    prefs.begin(WIFI_NVS_NAMESPACE, true);
    String ssid = prefs.getString(WIFI_NVS_KEY_SSID, "");
    String pass = prefs.getString(WIFI_NVS_KEY_PASS, "");
    prefs.end();

    if (ssid.length() == 0) return false;

    Serial.printf("[WiFi] Connecting to saved SSID: %s\n", ssid.c_str());
    _wifi_clear_area();
    M5Cardputer.Display.printf("Connecting: %.20s\n", ssid.c_str());

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(8, 8, 8, 8), IPAddress(1, 1, 1, 1));

    wifiMulti.addAP(ssid.c_str(), pass.c_str());
    Serial.print("[WiFi] Waiting for WiFi to connect...");
    unsigned long start = millis();
    while (wifiMulti.run() != WL_CONNECTED) {
        if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println(" Timeout.");
            WiFi.disconnect(true);
            _wifi_print("Timeout. Retrying later.", RED);
            delay(1500);
            s_wifi_connected = false;
            return false;
        }
        Serial.print(".");
        M5Cardputer.Display.print(".");
        delay(500);
    }
    
    Serial.println(" connected");
    M5Cardputer.Display.println("\nConnected!");
    setClock();
    
    delay(1500);
    s_wifi_connected = true;
    return true;
}

bool wifi_manager_prompt_and_connect(const String& ssid, const String& pass) {
    _wifi_clear_area();
    M5Cardputer.Display.printf("Connecting to:\n%s\n", ssid.c_str());

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(8, 8, 8, 8), IPAddress(1, 1, 1, 1));
    wifiMulti.addAP(ssid.c_str(), pass.c_str());

    Serial.print("[WiFi] Waiting for WiFi to connect...");
    unsigned long start = millis();
    while (wifiMulti.run() != WL_CONNECTED) {
        if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println(" Timeout.");
            WiFi.disconnect(true);
            _wifi_print("Connection Failed.", RED);
            delay(2000);
            s_wifi_connected = false;
            return false;
        }
        Serial.print(".");
        M5Cardputer.Display.print(".");
        delay(500);
    }

    Serial.println(" connected");
    _wifi_clear_area();
    _wifi_print("Connected!", GREEN);
    M5Cardputer.Display.printf("IP: %s\n", WiFi.localIP().toString().c_str());

    setClock();

    Preferences prefs;
    prefs.begin(WIFI_NVS_NAMESPACE, false);
    prefs.putString(WIFI_NVS_KEY_SSID, ssid);
    prefs.putString(WIFI_NVS_KEY_PASS, pass);
    prefs.end();
    
    delay(1500);
    s_wifi_connected = true;
    return true;
}

String _wifi_input_string(const char* prompt, bool is_password = false) {
    String input = "";
    _wifi_clear_area();
    M5Cardputer.Display.println(prompt);
    M5Cardputer.Display.drawLine(0, M5Cardputer.Display.getCursorY(), 240, M5Cardputer.Display.getCursorY(), DARKGREY);
    int start_y = M5Cardputer.Display.getCursorY() + 4;

    while (true) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            auto status = M5Cardputer.Keyboard.keysState();
            if (status.enter) return input;
            else if (status.del && input.length() > 0) input.remove(input.length() - 1);
            else {
                for (auto i : status.word) if (input.length() < 32) input += i;
            }
            M5Cardputer.Display.fillRect(0, start_y, 240, 20, BLACK);
            M5Cardputer.Display.setCursor(0, start_y);
            if (is_password) {
                for (size_t i = 0; i < input.length(); i++) M5Cardputer.Display.print("*");
            } else {
                M5Cardputer.Display.print(input);
            }
        }
        delay(10);
    }
}

void wifi_manager_prompt_and_save() {
    String ssid = _wifi_input_string("Enter SSID:");
    if (ssid.length() == 0) return;
    String pass = _wifi_input_string("Enter Password (opt):", true);
    wifi_manager_prompt_and_connect(ssid, pass);
}

void wifi_manager_clear_saved() {
    Preferences prefs;
    prefs.begin(WIFI_NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
}

bool wifi_manager_is_connected() {
    s_wifi_connected = (WiFi.status() == WL_CONNECTED);
    return s_wifi_connected;
}

#endif // IS_CARDPUTER
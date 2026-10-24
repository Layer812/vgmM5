#include <Arduino.h>
#include <Wire.h>

#include <FFat.h>
#include <SD.h>
#include "vgm_engine.h"
#include <USB.h>
#include <USBMSC.h>
#include <esp_partition.h>
#include <wear_levelling.h>
#include <M5Unified.h>
#include <driver/i2s.h>
#include <atomic> // ★ C++11標準のロックフリー同期

// ★ MDXエンジンとのバッファ共有：MDX再生中もaudio_play_taskが動作するようにする
extern "C" bool mdx_engine_is_playing(void);

extern "C" int puff_stream(int (*write_cb)(void*, const unsigned char*, unsigned long),
                           void* write_ctx,
                           unsigned long window_size,
                           int (*read_cb)(void*, unsigned char*, unsigned long),
                           void* read_ctx,
                           unsigned long in_buf_size,
                           unsigned char *dest,
                           unsigned long *destlen,
                           const unsigned char *source,
                           unsigned long *sourcelen);

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_partition.h"
#include <esp_spi_flash.h>
#include "fm_engine.h"
#include "pcm_engine.h"
#include "psg_engine.h"
PSGSoundEngine g_psg_engine;
PSGSoundEngine g_psg_engine2;
#include "ssg_engine.h"
#include "scc_engine.h"
}

#define YM2151_CLOCK 3579545
#define VGM_SAMPLE_RATE 44100

#define WAV_BUFF_COUNT 16
#define BUFF_SIZE 512

int16_t wav_buff[WAV_BUFF_COUNT][BUFF_SIZE * 2];
size_t wav_buff_size[WAV_BUFF_COUNT];
volatile int rd = 0;
volatile int wd = 0;

// ★ クリティカルセクションを全廃し、ロックフリーなアトミック変数に変更
std::atomic<int> wav_count(0); 


// ★ 追加：完全整数（固定小数点）型のDCブロッカー用状態変数
int64_t dc_state_l = 0;
int64_t dc_state_r = 0;
int32_t eq_lpf_l = 0;
int32_t eq_lpf_r = 0;
int32_t eq_bass_l = 0;
int32_t eq_bass_r = 0;
int32_t dc_prev_l = 0;
int32_t dc_prev_r = 0;
static volatile bool isPlaying = false;
static volatile bool isPaused = false;
static SemaphoreHandle_t xSemaphore = NULL;
TaskHandle_t playTaskHandle = NULL;

#if defined(IS_ATOMS3)
uint32_t actual_sample_rate = 48000;
#else
uint32_t actual_sample_rate = 44100;
#endif
uint8_t chip_type = 0;
bool prebuffering = false;
int currentSong = 0;

// ★ ミキサー用の除数（チップ数）をグローバルに一度だけ事前計算
static int global_chip_count = 1;

#define MAX_SONGS 100
String songs[MAX_SONGS];
int numSongs = 0;

uint32_t waitSamples = 0;
uint32_t vgm_time_acc = 0;
uint32_t vgmDataStart = 0;
uint32_t vgmLoopStart = 0;
bool vgm_has_loop = false;
int vgm_loop_count = 0;

uint8_t* vgm_data = nullptr;
bool vgm_is_streaming = false;
File vgm_stream_file;
uint8_t vgm_stream_buf[1024];
uint32_t vgm_stream_buf_start = 0;
uint32_t vgm_stream_buf_len = 0;
uint32_t vgm_file_size = 0;
uint32_t vgm_ptr = 0;
uint32_t ym2612_pcm_offset = 0;

char current_vgm_title[256] = {0};
char current_vgm_system[256] = {0};
char current_vgm_game[256] = {0};
char current_vgm_author[256] = {0};
char current_vgm_chip[256] = {0};

const char* vgm_engine_get_title(void) { return current_vgm_title; }
const char* vgm_engine_get_system(void) { return current_vgm_system; }
const char* vgm_engine_get_game(void) { return current_vgm_game; }
const char* vgm_engine_get_author(void) { return current_vgm_author; }
const char* vgm_engine_get_chip(void) { return current_vgm_chip; }

char vgm_last_error[128] = {0};
const char* vgm_engine_get_error(void) { return vgm_last_error; }

// VGMコマンドのバイト長を取得 (VGMフォーマット仕様に準拠)
static int get_cmd_length(uint8_t cmd) {
    if (cmd >= 0x30 && cmd <= 0x3F) return 2;
    if (cmd >= 0x40 && cmd <= 0x4E) return 3;
    if (cmd == 0x4F) return 2;
    if (cmd == 0x50) return 2;                     // SN76489
    if (cmd >= 0x51 && cmd <= 0x5F) return 3;
    if (cmd == 0x61) return 3;                     // Wait 16-bit
    if (cmd == 0x64) return 4;                     // Wait override (3-byte arg)
    if (cmd >= 0x62 && cmd <= 0x65) return 1;
    if (cmd == 0x66) return 1;                     // EOF
    if (cmd == 0x67) return 7;                     // PCM Data block header
    if (cmd == 0x68) return 12;                    // PCM RAM write
    if (cmd >= 0x70 && cmd <= 0x7F) return 1;
    if (cmd >= 0x80 && cmd <= 0x8F) return 1;
    if (cmd == 0x90 || cmd == 0x91) return 5;
    if (cmd == 0x92) return 6;
    if (cmd == 0x93) return 11;                    // X68000 (OKIM6258) Stream
    if (cmd == 0x94) return 2;
    if (cmd == 0x95) return 5;
    if (cmd >= 0x96 && cmd <= 0x9F) return 1;
    if (cmd >= 0xA0 && cmd <= 0xBF) return 3;
    if (cmd >= 0xC0 && cmd <= 0xDF) return 4;
    if (cmd >= 0xE0 && cmd <= 0xFF) return 5;
    return 1; // 未知のコマンドは進行不能を防ぐため最低1を進める
}

void vgm_engine_toggle_pause(void) {
    isPaused = !isPaused;
}

// ────────────────────────────────────────────────────────────────────────
// 仮想ミキサー
// ────────────────────────────────────────────────────────────────────────
class ISoundChip {
public:
    typedef void (*TickFunc)(ISoundChip* obj, int32_t* out_l, int32_t* out_r);
    TickFunc fast_tick;
    ISoundChip(TickFunc t) : fast_tick(t) {}
    virtual ~ISoundChip() {}
    IRAM_ATTR inline void tick(int32_t* out_l, int32_t* out_r) { fast_tick(this, out_l, out_r); }
    virtual void write(uint8_t port, uint16_t reg, uint8_t data) = 0;
};

void IRAM_ATTR wrap_fm_tick(ISoundChip* obj, int32_t* l, int32_t* r) { fm_engine_tick(&g_fm_engine, l, r); }
void IRAM_ATTR wrap_pcm_tick(ISoundChip* obj, int32_t* l, int32_t* r) { pcm_engine_tick(&g_pcm_engine, l, r); }
void IRAM_ATTR wrap_psg_tick1(ISoundChip* obj, int32_t* l, int32_t* r) { psg_engine_update(&g_psg_engine, l, r); }
void IRAM_ATTR wrap_psg_tick2(ISoundChip* obj, int32_t* l, int32_t* r) { psg_engine_update(&g_psg_engine2, l, r); }
void IRAM_ATTR wrap_ssg_tick(ISoundChip* obj, int32_t* l, int32_t* r) { ssg_engine_update(&g_ssg_engine, l, r); }
void IRAM_ATTR wrap_scc_tick(ISoundChip* obj, int32_t* l, int32_t* r) { scc_engine_tick(&g_scc_engine, l, r); }

class FmChip final : public ISoundChip { public: FmChip() : ISoundChip(wrap_fm_tick) {} void write(uint8_t port, uint16_t reg, uint8_t data) override { if (chip_type == 0) fm_engine_register_write(&g_fm_engine, reg, data); else if (chip_type == 1 || chip_type == 5 || chip_type == 6 || chip_type == 7) fm_engine_write_ym2612(&g_fm_engine, port, reg, data); else fm_engine_write_opl(&g_fm_engine, reg, data); } };
class PcmChip final : public ISoundChip { 
public: 
    PcmChip() : ISoundChip(wrap_pcm_tick) {} 
    void write(uint8_t port, uint16_t reg, uint8_t data) override {} 
};
class PsgChip final : public ISoundChip { public: PsgChip(TickFunc tf, PSGSoundEngine* e) : ISoundChip(tf), engine(e) {} void write(uint8_t port, uint16_t reg, uint8_t data) override { psg_engine_write_sn76489(engine, data); } PSGSoundEngine* engine; };
class SsgChip final : public ISoundChip { public: SsgChip() : ISoundChip(wrap_ssg_tick) {} void write(uint8_t port, uint16_t reg, uint8_t data) override { ssg_engine_write(&g_ssg_engine, reg, data); } };
class SccChip final : public ISoundChip { public: SccChip() : ISoundChip(wrap_scc_tick) {} void write(uint8_t port, uint16_t reg, uint8_t data) override { scc_engine_write(&g_scc_engine, port, reg, data); } };

extern FmChip fm_wrapper;

class OpnxChip final : public ISoundChip {
public:
    OpnxChip() : ISoundChip(wrap_fm_tick) {}
    void write(uint8_t port, uint16_t reg, uint8_t data) override {
        if (g_fm_engine.chip_type == CHIP_YM2610) {
            if (port == 0 && reg < 0x10) {
                ssg_engine_write(&g_ssg_engine, reg, data);
            } else if (port == 0 && (reg >= 0x10 && reg <= 0x1B)) {
                // YM2610 ADPCM-B (Delta-T) is on Port 0, reg 0x10-0x1B
                pcm_engine_write_opn_adpcmb(&g_pcm_engine, reg - 0x10, data);
            } else if (port == 1 && reg <= 0x2F) {
                // YM2610 ADPCM-A is on Port 1, reg 0x00-0x2F
                pcm_engine_write_opn_adpcma(&g_pcm_engine, reg, data);
            } else {
                fm_wrapper.write(port, reg, data);
            }
        } else {
            // YM2608
            if (port == 0 && reg < 0x10) {
                ssg_engine_write(&g_ssg_engine, reg, data);
            } else if (port == 0 && (reg >= 0x10 && reg <= 0x1D)) {
                // YM2608 Rhythm is on Port 0, reg 0x10-0x1D
                pcm_engine_write_opn_rhythm(&g_pcm_engine, reg, data);
            } else if (port == 1 && reg <= 0x0B) {
                // YM2608 ADPCM (Delta-T) is on Port 1, reg 0x00-0x0B
                pcm_engine_write_opn_adpcmb(&g_pcm_engine, reg, data);
            } else {
                fm_wrapper.write(port, reg, data);
            }
        }
    }
};

#define MAX_CHIPS 10
ISoundChip* active_chips[MAX_CHIPS];
int active_chip_count = 0;
int active_channel_count = 0;

FmChip      fm_wrapper;
PcmChip     pcm_wrapper;
PsgChip     psg_wrapper(wrap_psg_tick1, &g_psg_engine);
PsgChip     psg_wrapper2(wrap_psg_tick2, &g_psg_engine2);
SsgChip     ssg_wrapper;
SccChip     scc_wrapper;
OpnxChip    opnx_wrapper;

int skipGzipHeader(const uint8_t *data, size_t size) {
    if (size < 10) return -1;
    if (data[0] != 0x1F || data[1] != 0x8B || data[2] != 0x08) return -1;
    int flags = data[3]; int offset = 10;
    if (flags & 0x04) { if (offset + 2 > size) return -1; int xlen = data[offset] | (data[offset+1] << 8); offset += 2 + xlen; }
    if (flags & 0x08) { while (offset < size && data[offset] != 0) offset++; offset++; }
    if (flags & 0x10) { while (offset < size && data[offset] != 0) offset++; offset++; }
    if (flags & 0x02) { offset += 2; }
    return (offset >= size) ? -1 : offset;
}

int skipGzipHeaderStream(File& f) {
    if (f.size() < 10) return -1;
    uint8_t head[10];
    if (f.read(head, 10) != 10) return -1;
    if (head[0] != 0x1F || head[1] != 0x8B || head[2] != 0x08) return -1;
    int flags = head[3];
    if (flags & 0x04) {
        uint8_t xlen_b[2];
        if (f.read(xlen_b, 2) != 2) return -1;
        int xlen = xlen_b[0] | (xlen_b[1] << 8);
        f.seek(f.position() + xlen);
    }
    if (flags & 0x08) {
        while (f.available() && f.read() != 0);
    }
    if (flags & 0x10) {
        while (f.available() && f.read() != 0);
    }
    if (flags & 0x02) {
        f.seek(f.position() + 2);
    }
    return f.position();
}

void vgmSeek(uint32_t pos) { if (pos < vgm_file_size) vgm_ptr = pos; }

bool is_dual_sn76489 = false;
int sn76489_writes = 0;

static const esp_partition_t* vgm_swap_partition = nullptr;
static uint32_t         vgm_swap_offset = 0;
static uint32_t         vgm_swap_erased_limit = 0;
static spi_flash_mmap_handle_t vgm_mmap_handle = 0;



int puff_write_cb(void* ctx, const unsigned char* buf, unsigned long len) {
    if (!vgm_swap_partition) return -1;
    if (vgm_swap_offset + len > vgm_swap_partition->size) {
        snprintf(vgm_last_error, sizeof(vgm_last_error), "Size Exceeded: %lu", vgm_swap_offset + len);
        return -1;
    }
    
    uint32_t needed_erase_limit = (vgm_swap_offset + len + 4095) & ~4095;
    if (needed_erase_limit > vgm_swap_erased_limit) {
        uint32_t erase_len = needed_erase_limit - vgm_swap_erased_limit;
        esp_err_t err = esp_partition_erase_range(vgm_swap_partition, vgm_swap_erased_limit, erase_len);
        if (err != ESP_OK) {
            snprintf(vgm_last_error, sizeof(vgm_last_error), "Erase Failed: %d", err);
            return -1;
        }
        vgm_swap_erased_limit = needed_erase_limit;
    }
    
    esp_err_t err = esp_partition_write(vgm_swap_partition, vgm_swap_offset, buf, len);
    if (err != ESP_OK) {
        snprintf(vgm_last_error, sizeof(vgm_last_error), "Write Failed: %d", err);
        return -1;
    }
    
    vgm_swap_offset += len;
    return 0;
}

int puff_read_cb(void* ctx, unsigned char* buf, unsigned long len) {
    File* f = (File*)ctx;
    if (!f) return -1;
    
    unsigned long bytes_read = 0;
    int retries = 0;
    while (bytes_read < len) {
        int n = f->read(buf + bytes_read, len - bytes_read);
        if (n > 0) {
            bytes_read += n;
            retries = 0;
        } else {
            if (f->available()) {
                vTaskDelay(pdMS_TO_TICKS(5));
                retries++;
                if (retries > 100) break;
            } else {
                break;
            }
        }
    }
    return bytes_read;
}

static int vgm_cmd_debug_count = 0;
	
bool vgm_engine_play(const char* filepath, bool use_sd) {
    // Always stop and clean up before starting a new song, regardless of current state
    vgm_engine_stop();
    
    // Additionally, free vgm_data if it was heap-allocated (not MMAPed)
    if (vgm_mmap_handle == 0 && vgm_data != nullptr) {
        free(vgm_data);
        vgm_data = nullptr;
    }
    
    isPaused = false;
    vgm_last_error[0] = '\0';
    vgm_is_streaming = false;
    if (vgm_stream_file) vgm_stream_file.close();
    
    File f;
    if (use_sd) {
        f = SD.open(filepath, "r");
    } else {
        f = FFat.open(filepath, "r");
    }

    if (!f) { 
        snprintf(vgm_last_error, sizeof(vgm_last_error), "File Open Failed");
        isPlaying = false; return false; 
    }

    is_dual_sn76489 = false;
    active_chip_count = 0;
    active_channel_count = 0;
    vgm_file_size = f.size();
    if (vgm_data != nullptr) { free(vgm_data); vgm_data = nullptr; }
    
    String lname = String(filepath); lname.toLowerCase();
    bool isVgz = lname.endsWith(".vgz");
    
    uint8_t magic[2] = {0};
    f.read(magic, 2);
    f.seek(0);
    if (magic[0] == 0x1F && magic[1] == 0x8B) {
        isVgz = true;
    }
    
    uint32_t uncompressed_size = vgm_file_size;

    if (isVgz) {
        if (f.size() < 4) { 
            snprintf(vgm_last_error, sizeof(vgm_last_error), "Invalid VGZ size");
            f.close(); isPlaying = false; return false; 
        }
        f.seek(f.size() - 4); uint8_t sizeBytes[4]; f.read(sizeBytes, 4);
        uncompressed_size = sizeBytes[0] | (sizeBytes[1] << 8) | (sizeBytes[2] << 16) | (sizeBytes[3] << 24);
        f.seek(0);
    }
    
    vgm_data = nullptr;
    
    // PSRAMが利用可能な場合は、安全のため512KBを残して最大サイズを計算
    size_t max_psram_size = ESP.getPsramSize() > 0 ? ESP.getFreePsram() - (512 * 1024) : 0;
    
    // 曲のサイズがPSRAMの空き容量より大きい場合、またはPSRAMが無い場合のみストリーミング(Swap)にする
    if (uncompressed_size > max_psram_size || max_psram_size == 0) {
        vgm_is_streaming = true;
        vgm_swap_partition = nullptr;
        if (isVgz) {
            vgm_swap_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "vgm_swap");
            if (!vgm_swap_partition) {
                snprintf(vgm_last_error, sizeof(vgm_last_error), "Swap partition not found");
                f.close(); isPlaying = false; return false;
            }
            vgm_swap_offset = 0;
            vgm_swap_erased_limit = 0;
            
            f.seek(0);
            int gzipOffset = skipGzipHeaderStream(f);
            if (gzipOffset < 0) {
                snprintf(vgm_last_error, sizeof(vgm_last_error), "Invalid GZIP Header");
                f.close(); isPlaying = false; return false;
            }
            
            uint8_t* window = (uint8_t*)malloc(32768);
            if (!window) {
                snprintf(vgm_last_error, sizeof(vgm_last_error), "OOM (32KB Window)");
                f.close(); isPlaying = false; return false;
            }
            
            uint8_t* in_window = (uint8_t*)malloc(1024);
            if (!in_window) {
                snprintf(vgm_last_error, sizeof(vgm_last_error), "OOM (1KB In Window)");
                free(window); f.close(); isPlaying = false; return false;
            }
            
            unsigned long destlen = uncompressed_size; 
            int ret = puff_stream(puff_write_cb, nullptr, 32768, puff_read_cb, &f, 1024, window, &destlen, in_window, nullptr);
            free(window);
            free(in_window);
            f.close();
            
            if (ret != 0) {
                if (vgm_last_error[0] == ' ') {
                    snprintf(vgm_last_error, sizeof(vgm_last_error), "Decomp. Failed: %d", ret);
                }
                isPlaying = false; return false;
            }
            vgm_file_size = destlen;
            
            const void* mapped_ptr;
            esp_err_t mmap_err = esp_partition_mmap(vgm_swap_partition, 0, (vgm_file_size + 0xFFFF) & ~0xFFFF, SPI_FLASH_MMAP_DATA, &mapped_ptr, &vgm_mmap_handle);
            if (mmap_err == ESP_OK) {
                vgm_data = (uint8_t*)mapped_ptr;
                vgm_is_streaming = false;
            } else {
                snprintf(vgm_last_error, sizeof(vgm_last_error), "MMAP Failed: %d", mmap_err);
                isPlaying = false; return false;
            }
        } else {
            // .vgm ファイルも Swap パーティションにコピーして MMAP で高速アクセス
            vgm_swap_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "vgm_swap");
            if (vgm_swap_partition) {
                Serial.printf("[VGM] Copying .vgm to Swap: %lu bytes\n", vgm_file_size);
                vgm_swap_offset = 0;
                vgm_swap_erased_limit = 0;
                
                // 全体を Swap にコピー
                uint32_t to_copy = vgm_file_size;
                uint8_t buf[1024];
                f.seek(0);
                while (to_copy > 0) {
                    uint32_t chunk = to_copy > sizeof(buf) ? sizeof(buf) : to_copy;
                    int n = f.read(buf, chunk);
                    if (n <= 0) break;
                    
                    uint32_t needed_erase = (vgm_swap_offset + chunk + 4095) & ~4095;
                    if (needed_erase > vgm_swap_erased_limit) {
                        esp_partition_erase_range(vgm_swap_partition, vgm_swap_erased_limit, needed_erase - vgm_swap_erased_limit);
                        vgm_swap_erased_limit = needed_erase;
                    }
                    
                    esp_partition_write(vgm_swap_partition, vgm_swap_offset, buf, chunk);
                    vgm_swap_offset += chunk;
                    to_copy -= chunk;
                }
                
                Serial.printf("[VGM] Copied to Swap, now MMAPing...\n");
                
                // MMAP
                const void* mapped_ptr;
                esp_err_t mmap_err = esp_partition_mmap(vgm_swap_partition, 0, (vgm_file_size + 0xFFFF) & ~0xFFFF, SPI_FLASH_MMAP_DATA, &mapped_ptr, &vgm_mmap_handle);
                if (mmap_err == ESP_OK) {
                    vgm_data = (uint8_t*)mapped_ptr;
                    vgm_is_streaming = false;
                    Serial.printf("[VGM] .vgm MMAP success!\n");
                } else {
                    Serial.printf("[VGM] .vgm MMAP failed: %d\n", mmap_err);
                }
            }
            
            f.close();
            
            // Swap が使えない場合はストリーミングフォールバック
            if (vgm_is_streaming) {
                if (use_sd) {
                    vgm_stream_file = SD.open(filepath, "r");
                } else {
                    vgm_stream_file = FFat.open(filepath, "r");
                }
                if (!vgm_stream_file) {
                    snprintf(vgm_last_error, sizeof(vgm_last_error), "Failed to open stream file");
                    isPlaying = false; return false;
                }
                Serial.printf("[VGM] .vgm fallback to SD streaming\n");
            }
        }
        
        vgm_stream_buf_start = 0;
        vgm_stream_buf_len = 0;
    } else {
        // PSRAMに直接読み込む（SDカードとのSPI衝突を回避）
        Serial.printf("[VGM] Loading %lu bytes directly into PSRAM (free: %lu)\n", uncompressed_size, max_psram_size);
        vgm_data = (uint8_t*)ps_malloc(uncompressed_size);
        if (!vgm_data) {
            snprintf(vgm_last_error, sizeof(vgm_last_error), "OOM (PSRAM %lu bytes)", uncompressed_size);
            f.close(); isPlaying = false; return false;
        }
        
        if (isVgz) {
            f.seek(0);
            int gzipOffset = skipGzipHeaderStream(f);
            if (gzipOffset < 0) { 
                snprintf(vgm_last_error, sizeof(vgm_last_error), "Invalid GZIP Header");
                free(vgm_data); vgm_data = nullptr; f.close(); isPlaying = false; return false; 
            }

            uint8_t* in_window = (uint8_t*)malloc(1024);
            if (!in_window) {
                snprintf(vgm_last_error, sizeof(vgm_last_error), "OOM (1KB In Window)");
                free(vgm_data); vgm_data = nullptr; f.close(); isPlaying = false; return false;
            }
            unsigned long destlen = uncompressed_size;
            int ret = puff_stream(0, 0, 0, puff_read_cb, &f, 1024, vgm_data, &destlen, in_window, nullptr);
            free(in_window);
            f.close(); 
            if (ret != 0) { 
                if (vgm_last_error[0] == '\0') {
                    snprintf(vgm_last_error, sizeof(vgm_last_error), "Decomp. Failed: %d", ret);
                }
                free(vgm_data); vgm_data = nullptr; isPlaying = false; return false; 
            }
            vgm_file_size = destlen;
        } else {
            f.read(vgm_data, vgm_file_size); f.close();
        }
        Serial.printf("[VGM] PSRAM load complete, streaming=false\n");
    }

    uint8_t header[256];
    if (vgm_is_streaming) {
        if (vgm_swap_partition) {
            esp_partition_read(vgm_swap_partition, 0, header, 256);
        } else {
            vgm_stream_file.seek(0);
            vgm_stream_file.read(header, 256);
        }
    } else {
        memcpy(header, vgm_data, vgm_file_size < 256 ? vgm_file_size : 256);
    }
    
    if (vgm_file_size < 256 || header[0] != 'V' || header[1] != 'g' || header[2] != 'm' || header[3] != ' ') {
        snprintf(vgm_last_error, sizeof(vgm_last_error), "Invalid VGM Header");
        isPlaying = false; return false;
    }

    // --- GD3タグの読み取りとUTF-8変換 ---
    memset(current_vgm_title, 0, sizeof(current_vgm_title));
    memset(current_vgm_system, 0, sizeof(current_vgm_system));
    memset(current_vgm_game, 0, sizeof(current_vgm_game));
    memset(current_vgm_author, 0, sizeof(current_vgm_author));
    memset(current_vgm_chip, 0, sizeof(current_vgm_chip));

    uint32_t gd3_offset = header[0x14] | (header[0x15] << 8) | (header[0x16] << 16) | (header[0x17] << 24);
    if (gd3_offset != 0) {
        uint32_t abs_gd3 = gd3_offset + 0x14;
        if (abs_gd3 + 12 < vgm_file_size) {
            uint8_t gd3_sig[4];
            if (vgm_is_streaming) {
                if (vgm_swap_partition) esp_partition_read(vgm_swap_partition, abs_gd3, gd3_sig, 4);
                else { vgm_stream_file.seek(abs_gd3); vgm_stream_file.read(gd3_sig, 4); }
            } else {
                memcpy(gd3_sig, &vgm_data[abs_gd3], 4);
            }
            if (gd3_sig[0] == 'G' && gd3_sig[1] == 'd' && gd3_sig[2] == '3' && gd3_sig[3] == ' ') {
                uint32_t str_offset = abs_gd3 + 12;
                // スタックオーバーフロー回避のためPSRAMに確保
                char (*gd3_strings)[256] = (char (*)[256])ps_malloc(11 * 256);
                if (gd3_strings) {
                    memset(gd3_strings, 0, 11 * 256);
                
                for (int str_idx = 0; str_idx < 11; str_idx++) {
                    int out_idx = 0;
                    while (str_offset + 1 < vgm_file_size && out_idx < 250) {
                        uint8_t c_buf[2];
                        if (vgm_is_streaming) {
                            if (vgm_swap_partition) esp_partition_read(vgm_swap_partition, str_offset, c_buf, 2);
                            else { vgm_stream_file.seek(str_offset); vgm_stream_file.read(c_buf, 2); }
                        } else {
                            c_buf[0] = vgm_data[str_offset];
                            c_buf[1] = vgm_data[str_offset+1];
                        }
                        str_offset += 2;
                        
                        uint16_t c = c_buf[0] | (c_buf[1] << 8);
                        if (c == 0) break;
                        
                        // UTF-16LE から UTF-8 へ変換（日本語対応）
                        if (c < 0x0080) {
                            gd3_strings[str_idx][out_idx++] = (char)c;
                        } else if (c < 0x0800) {
                            gd3_strings[str_idx][out_idx++] = 0xC0 | (c >> 6);
                            gd3_strings[str_idx][out_idx++] = 0x80 | (c & 0x3F);
                        } else {
                            gd3_strings[str_idx][out_idx++] = 0xE0 | (c >> 12);
                            gd3_strings[str_idx][out_idx++] = 0x80 | ((c >> 6) & 0x3F);
                            gd3_strings[str_idx][out_idx++] = 0x80 | (c & 0x3F);
                        }
                    }
                    if (str_offset >= vgm_file_size) break;
                }
                
                // 日本語名があれば優先し、なければ英語名を使用
                if (strlen(gd3_strings[1]) > 0) strncpy(current_vgm_title,  gd3_strings[1], 255); else strncpy(current_vgm_title,  gd3_strings[0], 255);
                if (strlen(gd3_strings[3]) > 0) strncpy(current_vgm_game,   gd3_strings[3], 255); else strncpy(current_vgm_game,   gd3_strings[2], 255);
                if (strlen(gd3_strings[5]) > 0) strncpy(current_vgm_system, gd3_strings[5], 255); else strncpy(current_vgm_system, gd3_strings[4], 255);
                if (strlen(gd3_strings[7]) > 0) strncpy(current_vgm_author, gd3_strings[7], 255); else strncpy(current_vgm_author, gd3_strings[6], 255);
                    
                free(gd3_strings); // 使い終わったらPSRAMを解放
                }
            }
        }
    }
    // ------------------------------------------
    
    uint32_t dataOffset = header[0x34] | (header[0x35] << 8) | (header[0x36] << 16) | (header[0x37] << 24);
    vgmDataStart = (dataOffset == 0) ? 0x40 : (0x34 + dataOffset);
    uint32_t loopOffset = header[0x1C] | (header[0x1D] << 8) | (header[0x1E] << 16) | (header[0x1F] << 24);
    vgm_has_loop = (loopOffset != 0);
    vgmLoopStart = vgm_has_loop ? (0x1C + loopOffset) : vgmDataStart;
    
    vgmSeek(vgmDataStart);
    uint32_t vgm_version = header[0x08] | (header[0x09] << 8) | (header[0x0A] << 16) | (header[0x0B] << 24);
#if defined(IS_ATOMS3)
    actual_sample_rate = 48000;
#else
    actual_sample_rate = 44100;
#endif
    
    // Initialize PCM engine first before loading any specific PCM chips or ROMs
    pcm_engine_init(&g_pcm_engine, actual_sample_rate);
    
    uint32_t vgm_sn_clock      = (vgmDataStart > 0x0F) ? (header[0x0C] | (header[0x0D] << 8) | (header[0x0E] << 16) | (header[0x0F] << 24)) : 0;
    uint32_t vgm_ym2413_clock  = (vgmDataStart > 0x13) ? (header[0x10] | (header[0x11] << 8) | (header[0x12] << 16) | (header[0x13] << 24)) : 0;
    uint32_t vgm_ym2612_clock  = (vgmDataStart > 0x2F) ? (header[0x2C] | (header[0x2D] << 8) | (header[0x2E] << 16) | (header[0x2F] << 24)) : 0;
    uint32_t vgm_ym2151_clock  = (vgmDataStart > 0x33) ? (header[0x30] | (header[0x31] << 8) | (header[0x32] << 16) | (header[0x33] << 24)) : 0;
    uint32_t vgm_segapcm_clock = (vgm_version >= 0x151 && vgmDataStart > 0x3B) ? (header[0x38] | (header[0x39] << 8) | (header[0x3A] << 16) | (header[0x3B] << 24)) : 0;
    uint32_t vgm_spcm_intf     = (vgm_version >= 0x151 && vgmDataStart > 0x3F) ? (header[0x3C] | (header[0x3D] << 8) | (header[0x3E] << 16) | (header[0x3F] << 24)) : 0;
    uint32_t vgm_ym2203_clock  = (vgm_version >= 0x151 && vgmDataStart > 0x47) ? (header[0x44] | (header[0x45] << 8) | (header[0x46] << 16) | (header[0x47] << 24)) : 0;
    uint32_t vgm_ym2608_clock  = (vgm_version >= 0x151 && vgmDataStart > 0x4B) ? (header[0x48] | (header[0x49] << 8) | (header[0x4A] << 16) | (header[0x4B] << 24)) : 0;
    uint32_t vgm_ym2610_clock  = (vgm_version >= 0x151 && vgmDataStart > 0x4F) ? (header[0x4C] | (header[0x4D] << 8) | (header[0x4E] << 16) | (header[0x4F] << 24)) : 0;
    uint32_t vgm_ym3812_clock  = (vgm_version >= 0x151 && vgmDataStart > 0x53) ? (header[0x50] | (header[0x51] << 8) | (header[0x52] << 16) | (header[0x53] << 24)) : 0;
    uint32_t vgm_ay8910_clock  = (vgm_version >= 0x151 && vgmDataStart > 0x77) ? (header[0x74] | (header[0x75] << 8) | (header[0x76] << 16) | (header[0x77] << 24)) : 0;
    uint32_t vgm_msm6258_clock = (vgm_version >= 0x151 && vgmDataStart > 0x93) ? (header[0x90] | (header[0x91] << 8) | (header[0x92] << 16) | (header[0x93] << 24)) : 0;
    uint32_t vgm_oki_clock     = (vgm_version >= 0x151 && vgmDataStart > 0x9B) ? (header[0x98] | (header[0x99] << 8) | (header[0x9A] << 16) | (header[0x9B] << 24)) : 0;
    uint32_t vgm_scc_clock     = (vgm_version >= 0x151 && vgmDataStart > 0x9F) ? (header[0x9C] | (header[0x9D] << 8) | (header[0x9E] << 16) | (header[0x9F] << 24)) : 0;
    // Namco C140 clock at offset 0xA8-0xAB, C352 clock at 0xDC-0xDF (VGM 1.51+)
    uint32_t vgm_c140_clock    = (vgm_version >= 0x151 && vgmDataStart > 0xAB) ? (header[0xA8] | (header[0xA9] << 8) | (header[0xAA] << 16) | (header[0xAB] << 24)) : 0;
    uint32_t vgm_c352_clock    = (vgm_version >= 0x151 && vgmDataStart > 0xDF) ? (header[0xDC] | (header[0xDD] << 8) | (header[0xDE] << 16) | (header[0xDF] << 24)) : 0;

    // --- 搭載サウンドチップ文字列の生成 ---
    {
        String chip_str = "";
        auto add_chip = [&](uint32_t clk, const char* name) {
            if (clk > 0) {
                if (chip_str.length() > 0) chip_str += " + ";
                chip_str += name;
            }
        };
        add_chip(vgm_sn_clock, "SN76489");
        add_chip(vgm_ym2413_clock, "YM2413");
        add_chip(vgm_ym2612_clock, "YM2612");
        add_chip(vgm_ym2151_clock, "YM2151");
        add_chip(vgm_segapcm_clock, "SegaPCM");
        add_chip(vgm_ym2203_clock, "YM2203");
        add_chip(vgm_ym2608_clock, "YM2608");
        add_chip(vgm_ym2610_clock, "YM2610");
        add_chip(vgm_ym3812_clock, "YM3812");
        add_chip(vgm_ay8910_clock, "AY8910");
        add_chip(vgm_msm6258_clock, "MSM6258");
        add_chip(vgm_oki_clock, "OKIM6258");
        add_chip(vgm_scc_clock, "K051649");
        add_chip(vgm_c140_clock, "C140");
        add_chip(vgm_c352_clock, "C352");
        
        uint32_t vgm_ymf262_clock  = (vgm_version >= 0x151 && vgmDataStart > 0x5B) ? (header[0x58] | (header[0x59] << 8) | (header[0x5A] << 16) | (header[0x5B] << 24)) : 0;
        add_chip(vgm_ymf262_clock, "YMF262");
        
        if (chip_str == "") chip_str = "Unknown Chip";
        strncpy(current_vgm_chip, chip_str.c_str(), 255);
    }
    // ------------------------------------------

    if (vgm_ym2612_clock != 0 || vgm_ym2151_clock != 0 || vgm_ym2610_clock != 0 || vgm_ym2203_clock != 0 || vgm_ym2608_clock != 0 || vgm_ym3812_clock != 0 || vgm_ym2413_clock != 0) {
        uint32_t fm_clock = 0;
        if      (vgm_ym2612_clock != 0) { chip_type = CHIP_YM2612; fm_clock = vgm_ym2612_clock & 0x3FFFFFFF; }
        else if (vgm_ym2608_clock != 0) { chip_type = CHIP_YM2608; fm_clock = vgm_ym2608_clock & 0x3FFFFFFF; }
        else if (vgm_ym2610_clock != 0) { chip_type = CHIP_YM2610; fm_clock = vgm_ym2610_clock & 0x3FFFFFFF; }
        else if (vgm_ym2203_clock != 0) { chip_type = CHIP_YM2203; fm_clock = vgm_ym2203_clock & 0x3FFFFFFF; }
        else if (vgm_ym3812_clock != 0) { chip_type = CHIP_YM3812; fm_clock = vgm_ym3812_clock & 0x3FFFFFFF; }
        else if (vgm_ym2413_clock != 0) { chip_type = CHIP_YM2413; fm_clock = vgm_ym2413_clock & 0x3FFFFFFF; }
        else                            { chip_type = CHIP_YM2151; fm_clock = vgm_ym2151_clock & 0x3FFFFFFF; }
        fm_engine_init(&g_fm_engine, actual_sample_rate, fm_clock, chip_type);
        pcm_engine_opn_init(&g_pcm_engine, fm_clock, chip_type);
        
        if (chip_type == CHIP_YM2608 || chip_type == CHIP_YM2610) {
            active_chips[active_chip_count++] = &opnx_wrapper;
            active_channel_count += 6;
            
            if (chip_type == CHIP_YM2608) {
                static uint8_t *ym2608_rom = nullptr;
                if (!ym2608_rom && SD.exists("/ym2608_rom.bin")) {
                    File f = SD.open("/ym2608_rom.bin", "r");
                    if (f) {
                        size_t rsize = f.size();
                        ym2608_rom = (uint8_t*)malloc(rsize);
                        if (ym2608_rom) {
                            f.read(ym2608_rom, rsize);
                            Serial.println("[VGM] YM2608 Rhythm ROM loaded from SD.");
                        }
                        f.close();
                    }
                }
                if (ym2608_rom) {
                    pcm_engine_add_data_block(&g_pcm_engine, 0x82, ym2608_rom, 32768);
                }
            }
        } else {
            active_chips[active_chip_count++] = &fm_wrapper;
            if (chip_type == CHIP_YM2612) active_channel_count += 6;
            else if (chip_type == CHIP_YM2151) active_channel_count += 8;
            else if (chip_type == CHIP_YM2203) active_channel_count += 3;
            else if (chip_type == CHIP_YM3812) active_channel_count += 9;
            else if (chip_type == CHIP_YM2413) active_channel_count += 9;
        }
    }


    is_dual_sn76489 = false;
    Serial.printf("[VGM] SN76489 Clock: %lu, Dual: %d\n", vgm_sn_clock & 0x3FFFFFFF, (vgm_sn_clock & 0x40000000) != 0);
    if (vgm_sn_clock != 0) {
        uint32_t sn_clock = vgm_sn_clock & 0x3FFFFFFF; if (sn_clock == 0) sn_clock = 3579545;
        psg_engine_init(&g_psg_engine, actual_sample_rate, sn_clock); active_chips[active_chip_count++] = &psg_wrapper;
        active_channel_count += 4;
        if (vgm_sn_clock & 0x40000000) {
            is_dual_sn76489 = true;
            psg_engine_init(&g_psg_engine2, actual_sample_rate, sn_clock); active_chips[active_chip_count++] = &psg_wrapper2;
            active_channel_count += 4;
        }
    }
    Serial.printf("[VGM] AY8910: %lu, YM2610: %lu, YM2608: %lu, YM2203: %lu\n", vgm_ay8910_clock, vgm_ym2610_clock, vgm_ym2608_clock, vgm_ym2203_clock);
    if (vgm_ay8910_clock != 0 || vgm_ym2610_clock != 0 || vgm_ym2608_clock != 0 || vgm_ym2203_clock != 0) {
        uint32_t ssg_clock = vgm_ay8910_clock & 0x3FFFFFFF;
        if (ssg_clock == 0) ssg_clock = (vgm_ym2610_clock != 0) ? vgm_ym2610_clock / 4 : ((vgm_ym2608_clock != 0) ? vgm_ym2608_clock / 4 : ((vgm_ym2203_clock != 0) ? vgm_ym2203_clock / 2 : 1500000));
        ssg_engine_init(&g_ssg_engine, actual_sample_rate, ssg_clock); 
        active_chips[active_chip_count++] = &ssg_wrapper;
        active_channel_count += 3;
    }
    if (vgm_scc_clock != 0) {
        bool is_scc_plus = (vgm_scc_clock & 0x80000000) != 0;
        Serial.printf("[VGM] SCC Clock: %lu, SCC+: %d\n", vgm_scc_clock & 0x3FFFFFFF, is_scc_plus);
        scc_engine_init(&g_scc_engine, actual_sample_rate, vgm_scc_clock & 0x3FFFFFFF); active_chips[active_chip_count++] = &scc_wrapper;
        active_channel_count += 5;
    }

    // ========================================================
    // PCM engine init: add pcm_wrapper when any PCM chip present
    // ========================================================

    if (vgm_msm6258_clock != 0 || vgm_oki_clock != 0 || vgm_segapcm_clock != 0 || vgm_ym2612_clock != 0 || vgm_ym2608_clock != 0 || vgm_ym2610_clock != 0 || vgm_c140_clock != 0 || vgm_c352_clock != 0) {
        active_chips[active_chip_count++] = &pcm_wrapper;
        active_channel_count += 1;
        if (vgm_msm6258_clock != 0) pcm_engine_set_msm6258_clock(&g_pcm_engine, vgm_msm6258_clock & 0x3FFFFFFF);
        if (vgm_oki_clock != 0)     pcm_engine_set_oki_clock(&g_pcm_engine, vgm_oki_clock & 0x3FFFFFFF);
        if (vgm_segapcm_clock != 0) pcm_engine_segapcm_init(&g_pcm_engine, vgm_segapcm_clock & 0x3FFFFFFF, vgm_spcm_intf);
        if (vgm_c140_clock != 0) {
            uint8_t c140_bank_type = (vgm_c140_clock >> 31) & 1; // 0 = System 2, 1 = System 21
            Serial.printf("[VGM] C140 Clock: %lu, Type: %d\n", vgm_c140_clock & 0x3FFFFFFF, c140_bank_type);
            pcm_engine_namco_init(&g_pcm_engine, vgm_c140_clock & 0x3FFFFFFF, 0xC1, c140_bank_type);
        }
        if (vgm_c352_clock != 0) {
            uint8_t clkdiv = (vgmDataStart > 0xD6) ? header[0xD6] : 0;
            Serial.printf("[VGM] C352 Clock: %lu (Div: %d)\n", vgm_c352_clock & 0x3FFFFFFF, clkdiv);
            pcm_engine_namco_init(&g_pcm_engine, vgm_c352_clock & 0x3FFFFFFF, 0xC2, clkdiv);
        }
    }

    // ========================================================
    // ★ ミキサー用の除数（チップ数）をグローバルに一度だけ事前計算
    // ========================================================
    global_chip_count = (active_chip_count > 0) ? active_chip_count : 1;
    Serial.printf("[VGM] Mixer: chip_count=%d\n", global_chip_count);

    waitSamples = 8820; vgm_time_acc = 0; ym2612_pcm_offset = 0;
    sn76489_writes = 0;
    vgm_loop_count = 0;
    rd = 0; wd = 0; wav_count = 0;
    
    // ==========================================
    // Pre-scan for 0x67 PCM blocks
    // ==========================================
    Serial.printf("[VGM] Pre-scan: vgm_is_streaming=%d, vgm_data=%p\n", vgm_is_streaming, (void*)vgm_data);
    if (vgm_is_streaming) {
        Serial.printf("[VGM] Pre-scan: Streaming mode (SD card)\n");
        if (!vgm_swap_partition) {
            vgm_swap_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "vgm_swap");
            if (vgm_swap_partition) {
                vgm_swap_offset = 0;
                vgm_swap_erased_limit = 0;
            }
        }
        if (vgm_swap_partition) {
            Serial.printf("[VGM] Pre-scan: Scanning for 0x67 blocks (streaming)...\n");
            uint32_t scan_ptr = vgmDataStart;
            vgm_stream_file.seek(scan_ptr);
            while (scan_ptr < vgm_file_size) {
                uint8_t cmd = vgm_stream_file.read(); scan_ptr++;
                if (cmd == 0x67) {
                    uint8_t marker = vgm_stream_file.read(); scan_ptr++;
                    if (marker != 0x66) {
                        Serial.printf("[VGM] WARN: 0x67 without 0x66 marker (Streaming), skipping\n");
                        vgm_stream_file.seek(scan_ptr - 1);
                        scan_ptr--;
                        continue;
                    }
                    uint8_t type = vgm_stream_file.read(); scan_ptr++;
                    uint8_t b1 = vgm_stream_file.read();
                    uint8_t b2 = vgm_stream_file.read();
                    uint8_t b3 = vgm_stream_file.read();
                    uint8_t b4 = vgm_stream_file.read();
                    uint32_t size = b1 | (b2 << 8) | (b3 << 16) | (b4 << 24);
                    size &= 0x7FFFFFFF; // ★ これを追加！(VGM 1.71+ の巨大サイズ化を防ぐ)
                    
                    scan_ptr += 4;
                    
                    uint32_t needed_erase_limit = (vgm_swap_offset + size + 4095) & ~4095;
                    if (needed_erase_limit > vgm_swap_erased_limit) {
                        esp_partition_erase_range(vgm_swap_partition, vgm_swap_erased_limit, needed_erase_limit - vgm_swap_erased_limit);
                        vgm_swap_erased_limit = needed_erase_limit;
                    }
                    
                    uint32_t remain = size;
                    uint8_t copy_buf[1024];
                    vgm_stream_file.seek(scan_ptr);
                    while (remain > 0) {
                        uint32_t rl = remain > sizeof(copy_buf) ? sizeof(copy_buf) : remain;
                        vgm_stream_file.read(copy_buf, rl);
                        esp_partition_write(vgm_swap_partition, vgm_swap_offset + (size - remain), copy_buf, rl);
                        remain -= rl;
                    }
                    vgm_swap_offset += size;
                    scan_ptr += size;
                    vgm_stream_file.seek(scan_ptr);
                } else {
                    int len = get_cmd_length(cmd);
                    if (len > 0) { scan_ptr += len - 1; vgm_stream_file.seek(scan_ptr); }
                    else if (cmd == 0x66) break;
                }
            }
            
            // Now mmap the partition
            if (vgm_swap_offset > 0) {
                // ★ 修正: 二重 MMAP 防止。VGZ展開時などに既にハンドルが設定されている可能性があるため、
                // 再マッピング前に既存ハンドルを確実に解放し、リソースリーク・クラッシュを回避する
                if (vgm_mmap_handle) {
                    spi_flash_munmap(vgm_mmap_handle);
                    vgm_mmap_handle = 0;
                }
                const void* mapped_ptr;
                esp_err_t mmap_err = esp_partition_mmap(vgm_swap_partition, 0, (vgm_swap_offset + 0xFFFF) & ~0xFFFF, SPI_FLASH_MMAP_DATA, &mapped_ptr, &vgm_mmap_handle);
                if (mmap_err == ESP_OK) {
                    uint8_t* mmap_base = (uint8_t*)mapped_ptr;
                    uint32_t current_swap_offset = 0;
                    // Re-scan to register pointers
                    scan_ptr = vgmDataStart;
                    vgm_stream_file.seek(scan_ptr);
                    while (scan_ptr < vgm_file_size) {
                        uint8_t cmd = vgm_stream_file.read(); scan_ptr++;
                        if (cmd == 0x67) {
                            uint8_t marker = vgm_stream_file.read(); scan_ptr++;
                            if (marker != 0x66) {
                                // Invalid 0x67, skip it (same as first pass)
                                vgm_stream_file.seek(scan_ptr - 1);
                                scan_ptr--;
                                continue;
                            }
                            uint8_t type = vgm_stream_file.read(); scan_ptr++;
                            uint32_t size = vgm_stream_file.read() | (vgm_stream_file.read() << 8) | (vgm_stream_file.read() << 16) | (vgm_stream_file.read() << 24);
                            size &= 0x7FFFFFFF; // ★ これを追加！(VGM 1.71+ の巨大サイズ化を防ぐ)
                            
                            scan_ptr += 4;
                            if (current_swap_offset + size <= vgm_swap_offset) {
                                pcm_engine_add_data_block(&g_pcm_engine, type, mmap_base + current_swap_offset, size);
                                current_swap_offset += size;
                            }
                            scan_ptr += size;
                            vgm_stream_file.seek(scan_ptr);
                        } else {
                            int len = get_cmd_length(cmd);
                            if (len > 0) { scan_ptr += len - 1; vgm_stream_file.seek(scan_ptr); }
                            else if (cmd == 0x66) break;
                        }
                    }
                }
            }
        }
    } else {
        // MMAP mode (.vgz decompressed or .vgm copied to Swap)
        // Register PCM pointers directly from vgm_data (MMAPed Flash)
        Serial.printf("[VGM] Pre-scan: MMAP mode (Flash) - scanning for 0x67 blocks...\n");
        uint32_t scan_ptr = vgmDataStart;
        int pcm_block_count = 0;
        while (scan_ptr < vgm_file_size) {
            uint8_t cmd = vgm_data[scan_ptr++];
            if (cmd == 0x67) {
                // Validate: 0x67 must be followed by 0x66 (VGM spec)
                if (scan_ptr >= vgm_file_size) break;
                uint8_t marker = vgm_data[scan_ptr];
                if (marker != 0x66) {
                    Serial.printf("[VGM] WARN: 0x67 without 0x66 marker at offset %lu, skipping\n", scan_ptr - 1);
                    continue;
                }
                scan_ptr++; // skip 0x66
                if (scan_ptr + 4 > vgm_file_size) break;
                uint8_t type = vgm_data[scan_ptr++];
                uint32_t size = vgm_data[scan_ptr] | (vgm_data[scan_ptr+1] << 8) | (vgm_data[scan_ptr+2] << 16) | (vgm_data[scan_ptr+3] << 24);
                size &= 0x7FFFFFFF; // ★ これを追加！(VGM 1.71+ の巨大サイズ化を防ぐ)
                scan_ptr += 4;
                // Bounds check: ensure data doesn't extend beyond file
                if (scan_ptr + size > vgm_file_size) {
                    uint32_t clipped = vgm_file_size - scan_ptr;
                    Serial.printf("[VGM] PCM Block #%d: type=0x%02X, size=%lu (CLIPPED to %lu, exceeds file by %lu)\n",
                                  pcm_block_count, type, size, clipped, (scan_ptr + size) - vgm_file_size);
                    size = clipped;
                }
                // Sanity check: reject blocks larger than 16MB (reasonable VGM PCM limit)
                if (size > 0x1000000) {
                    Serial.printf("[VGM] PCM Block #%d: type=0x%02X, size=%lu exceeds 16MB limit, skipping\n",
                                  pcm_block_count, type, size);
                    continue;
                }
                Serial.printf("[VGM] PCM Block #%d: type=0x%02X, size=%lu, ptr=%p\n", pcm_block_count, type, size, (void*)(vgm_data + scan_ptr));
                pcm_engine_add_data_block(&g_pcm_engine, type, vgm_data + scan_ptr, size);
                pcm_block_count++;
                scan_ptr += size;
            } else {
                int len = get_cmd_length(cmd);
                if (len > 0) scan_ptr += len - 1;
                else if (cmd == 0x66) break;
            }
        }
        Serial.printf("[VGM] Pre-scan complete: %d PCM blocks registered\n", pcm_block_count);
    }


    
    // 曲の開始時にDCブロッカーの状態を完全にクリアする
    dc_state_l = 0; dc_state_r = 0; dc_prev_l = 0; dc_prev_r = 0;
    eq_lpf_l = 0; eq_lpf_r = 0; eq_bass_l = 0; eq_bass_r = 0;
    

    // ==========================================
    // DEBUG: VGM ポインタの最終位置を確認
    // ==========================================
    Serial.printf("[VGM] PLAY INIT: vgm_ptr=%lu, vgmDataStart=%lu, vgm_file_size=%lu\n", vgm_ptr, vgmDataStart, vgm_file_size);
    if (!vgm_is_streaming && vgm_data != nullptr) {
        Serial.printf("[VGM] Header Magic: %c%c%c%c\n", vgm_data[0], vgm_data[1], vgm_data[2], vgm_data[3]);
        // データ開始位置の最初の数バイトをダンプ
        if (vgmDataStart < vgm_file_size) {
            Serial.printf("[VGM] DataStart Dump: ");
            for (int i = 0; i < 16 && (vgmDataStart + i) < vgm_file_size; i++) {
                Serial.printf("%02X ", vgm_data[vgmDataStart + i]);
            }
            Serial.println();
        }
    }
    vgm_cmd_debug_count = 0; // ★この1行を追加！曲の開始時に必ずログカウンタをリセットする

    // アンプの起動遅延（ミュート解除）を待つため、300ミリ秒間無音を流してアンプを完全に起こす
    vTaskDelay(pdMS_TO_TICKS(300));

    prebuffering = true;
    isPlaying = true;
    xSemaphoreGive(xSemaphore); 
    return true;
}

void vgm_engine_stop() { 
    isPlaying = false; 
    delay(50); 
    pcm_engine_free(&g_pcm_engine); 
    if (vgm_mmap_handle) {
        spi_flash_munmap(vgm_mmap_handle);
        vgm_mmap_handle = 0;
        vgm_data = nullptr;
    }
}

static inline uint8_t readByte() {
    if (vgm_ptr >= vgm_file_size) { isPlaying = false; return 0x66; }
    if (vgm_is_streaming) {
        if (vgm_ptr < vgm_stream_buf_start || vgm_ptr >= vgm_stream_buf_start + vgm_stream_buf_len) {
            uint32_t read_len = sizeof(vgm_stream_buf);
            if (vgm_ptr + read_len > vgm_file_size) read_len = vgm_file_size - vgm_ptr;
            
            // ★修正: vgm_swap_partitionからの読み込みを完全削除
            // ストリーミング時は必ずSDカード(vgm_stream_file)からコマンドを読む
            vgm_stream_file.seek(vgm_ptr);
            vgm_stream_buf_len = vgm_stream_file.read(vgm_stream_buf, read_len);
            vgm_stream_buf_start = vgm_ptr;
        }
        return vgm_stream_buf[vgm_ptr++ - vgm_stream_buf_start];
    }
    return vgm_data[vgm_ptr++];
}



void processVGM() {
    if (!isPlaying || (!vgm_is_streaming && vgm_data == nullptr)) return;

    while (isPlaying && vgm_ptr < vgm_file_size && waitSamples == 0) {
        uint8_t cmd = readByte();
        
        // DEBUG: 最初の20コマンドをダンプ
        if (vgm_cmd_debug_count < 20) {
            Serial.printf("[VGM] CMD #%d: ptr=%lu, cmd=0x%02X\n", vgm_cmd_debug_count, vgm_ptr - 1, cmd);
            vgm_cmd_debug_count++;
        }
        switch (cmd) {
            case 0x30: { uint8_t val = readByte(); if (is_dual_sn76489) psg_wrapper2.write(0, 0, val); break; }
            case 0x50: { 
                uint8_t val = readByte(); 
                psg_wrapper.write(0, 0, val); 
                break; 
            }
            case 0x4F: { uint8_t val = readByte(); /* Game Gear Stereo - not supported yet */ break; }
            case 0x51: { 
                uint8_t reg = readByte(); uint8_t val = readByte(); 
                fm_engine_register_write(&g_fm_engine, reg, val);
                break; 
            }
            case 0x5A: { uint8_t reg = readByte(); uint8_t val = readByte(); fm_wrapper.write(0, reg, val); break; }
            case 0x52: case 0x53: {
                uint8_t port = cmd & 1;
                uint8_t reg = readByte(); uint8_t val = readByte();
                uint16_t addr = (port << 8) | reg;
                fm_engine_register_write(&g_fm_engine, addr, val);
                if (port == 0 && (reg == 0x2A || reg == 0x2B)) pcm_engine_write_ym2612(&g_pcm_engine, reg, val);
                break;
            }
            case 0xA0: {
                uint8_t reg = readByte(); uint8_t val = readByte();
                ssg_engine_write(&g_ssg_engine, reg, val);
                break;
            }
            case 0x56: case 0x57: 
            case 0x58: case 0x59: {
                uint8_t port = cmd & 1;
                uint8_t reg = readByte(); uint8_t val = readByte();
                opnx_wrapper.write(port, reg, val);
                break;
            }
            case 0x54: { uint8_t reg = readByte(); uint8_t val = readByte(); fm_wrapper.write(0, reg, val); break; }
            case 0x55: { uint8_t reg = readByte(); uint8_t val = readByte(); if (reg < 0x10) ssg_wrapper.write(0, reg, val); else fm_wrapper.write(0, reg, val); break; }
            case 0x61: { uint16_t n = readByte(); n |= (readByte() << 8); waitSamples = n; break; }
            case 0x62: waitSamples = 735; break;
            case 0x63: waitSamples = 882; break;
            case 0x64: vgm_ptr += 3; break;        // Wait override: skip 3-byte argument
            case 0x66: {
                if (!vgm_has_loop) { isPlaying = false; }
                else if (vgm_loop_count >= 2) { isPlaying = false; }
                else { 
                    vgm_loop_count++;
                    for (int i = 0; i < TOTAL_OPS; i++) {
                        if (g_fm_engine.ops[i].rr <= 3) {
                            g_fm_engine.ops[i].env_state = EG_OFF;
                            g_fm_engine.ops[i].env_level = (0x3FF << 11);
                        } else {
                            g_fm_engine.ops[i].env_state = EG_RELEASE;
                        }
                    }
                    vgmSeek(vgmLoopStart); 
                    if (waitSamples == 0) waitSamples = 1;
                }
                break;
            }
            case 0x67: { 
                uint8_t marker = readByte(); 
                if (marker != 0x66) {
                    // Sync with pre-scan: invalid 0x67 without 0x66 marker.
                    // This indicates garbage data at the end of the VGM stream.
                    // Stop playback immediately to avoid infinite loop or emitting invalid commands.
                    Serial.printf("[VGM] Invalid 0x67 (no 0x66 marker) at offset %lu, stopping playback\n", vgm_ptr - 1);
                    isPlaying = false;
                    break; 
                }
                uint8_t type = readByte(); 
                uint32_t size = readByte(); 
                size |= (readByte() << 8); 
                size |= (readByte() << 16); 
                size |= (readByte() << 24); 
                size &= 0x7FFFFFFF; // Mask VGM 1.71 flag bits
                vgm_ptr += size; // SKIP data, since it's already mapped!
                break; 
            }
            case 0x68: { vgm_ptr += 11; break; }
            case 0xB7: { uint8_t reg = readByte(); uint8_t val = readByte(); pcm_engine_write_msm6258(&g_pcm_engine, reg, val); break; }
            case 0xB8: { uint8_t val = readByte(); pcm_engine_write_oki(&g_pcm_engine, val); break; }
            case 0xC0: { uint16_t offset = readByte(); offset |= (readByte() << 8); uint8_t val = readByte(); pcm_engine_segapcm_write(&g_pcm_engine, offset & 0xFF, val); break; }
            case 0xC4: { // QSound (not implemented)
                vgm_ptr += 3;
                break;
            }
            case 0xD4: { // Namco C140
                uint8_t port = readByte(); 
                uint8_t reg = readByte(); 
                uint8_t val = readByte(); 
                pcm_engine_namco_write(&g_pcm_engine, (port << 8) | reg, val); 
                break; 
            }
	case 0xE1: { // Namco C352 Write
	    // VGM 0xE1 format is Big Endian for both address and data!
            uint16_t addr = (readByte() << 8);
            addr |= readByte();
            uint16_t val = (readByte() << 8);
            val |= readByte();
            if (vgm_cmd_debug_count < 100) {
                Serial.printf("[VGM] C352 Write: addr=0x%04X, data=0x%04X\n", addr, val);
                vgm_cmd_debug_count++;
            }
            pcm_engine_namco_write(&g_pcm_engine, addr, val);
            break;
        }
            
            case 0xD2: { 
                uint8_t port = readByte(); uint8_t reg = readByte(); uint8_t val = readByte(); 
                scc_engine_write(&g_scc_engine, port, reg, val); 
                break; 
            }
            
            default:
                if (cmd >= 0x70 && cmd <= 0x7F) waitSamples = (cmd & 0x0F) + 1;
                else if (cmd >= 0x80 && cmd <= 0x8F) { 
                    waitSamples = (cmd & 0x0F); 
                    if (g_pcm_engine.ym2612_pcm_block_count > 0 && ym2612_pcm_offset < g_pcm_engine.ym2612_pcm_offsets[g_pcm_engine.ym2612_pcm_block_count-1] + g_pcm_engine.ym2612_pcm_sizes[g_pcm_engine.ym2612_pcm_block_count-1]) {
                        pcm_engine_write_ym2612(&g_pcm_engine, 0x2A, pcm_engine_get_ym2612_byte(&g_pcm_engine, ym2612_pcm_offset++));
                    }
                }
                else if (cmd >= 0x90 && cmd <= 0x95) { 
                    if      (cmd == 0x90 || cmd == 0x91) vgm_ptr += 4;
                    else if (cmd == 0x92) vgm_ptr += 5;
                    else if (cmd == 0x93) { readByte(); uint32_t offset = readByte(); offset |= (readByte() << 8); offset |= (readByte() << 16); offset |= (readByte() << 24); uint32_t length = readByte(); length |= (readByte() << 8); length |= (readByte() << 16); length |= (readByte() << 24); readByte(); pcm_engine_msm6258_start_stream(&g_pcm_engine, offset, length); }
                    else if (cmd == 0x94) { readByte(); g_pcm_engine.msm6258.playing = 0; }
                    else if (cmd == 0x95) { readByte(); uint16_t block_id = readByte(); block_id |= (readByte() << 8); readByte(); if (block_id < g_pcm_engine.msm6258.block_count) pcm_engine_msm6258_start_stream(&g_pcm_engine, g_pcm_engine.msm6258.block_offsets[block_id], g_pcm_engine.msm6258.block_sizes[block_id]); }
                }
                else if (cmd >= 0x31 && cmd <= 0x3F) vgm_ptr += 1;
                else if (cmd >= 0x40 && cmd <= 0x4E) vgm_ptr += 2;
                else if (cmd == 0x4F) vgm_ptr += 1;
                else if (cmd >= 0x50 && cmd <= 0x5F) vgm_ptr += 2;
                else if (cmd >= 0xA1 && cmd <= 0xBF) vgm_ptr += 2;
                else if (cmd >= 0xC0 && cmd <= 0xDF) vgm_ptr += 3;
		else if (cmd == 0xE0) { uint32_t offset = readByte(); offset |= (readByte() << 8); offset |= (readByte() << 16); offset |= (readByte() << 24); ym2612_pcm_offset = offset; }
		else if (cmd >= 0xE2 && cmd <= 0xFF) {
		                // 未知のE2〜FFコマンドは引数4バイトを読み飛ばす
		                readByte(); readByte(); readByte(); readByte();
		} else {
            isPlaying = false;
        }
                 break;
        }
    }
    
    if (vgm_ptr >= vgm_file_size) {
        isPlaying = false;
    }
}


static uint32_t prof_fm_time = 0;
static uint32_t prof_pcm_time = 0;
static uint32_t prof_vgm_time = 0;

static uint32_t profile_start_time = 0;
static uint32_t profile_total_time = 0;
static uint32_t profile_blocks = 0;

void IRAM_ATTR processAudioBlock() {
    if (!isPlaying) return;
    uint32_t t_start = esp_timer_get_time();
    
    int16_t *out_buf = wav_buff[wd];


    for (int i = 0; i < BUFF_SIZE; i++) {
        vgm_time_acc += 44100;
        while (vgm_time_acc >= actual_sample_rate && isPlaying) {
            if (waitSamples > 0) {
                uint32_t consume = vgm_time_acc / actual_sample_rate;
                if (consume > waitSamples) consume = waitSamples;
                waitSamples -= consume;
                vgm_time_acc -= consume * actual_sample_rate;
            }
            if (waitSamples == 0 && isPlaying) {
                uint32_t t_vgm = esp_timer_get_time(); processVGM(); prof_vgm_time += esp_timer_get_time() - t_vgm; 
                if (waitSamples == 0) continue;
            }
        }

        // =====================================
        // ミキサー処理（各チップの出力を合計し、チップ数で割る）
        // =====================================
        int32_t mix_l = 0;
        int32_t mix_r = 0;

        for (int c = 0; c < active_chip_count; c++) {
            int32_t ch_l = 0, ch_r = 0;
            
            uint32_t t_tick = esp_timer_get_time();
            active_chips[c]->tick(&ch_l, &ch_r);
            uint32_t t_diff = esp_timer_get_time() - t_tick;
            if (active_chips[c] == &fm_wrapper) prof_fm_time += t_diff;
            else if (active_chips[c] == &pcm_wrapper) prof_pcm_time += t_diff;

            mix_l += ch_l;
            mix_r += ch_r;
        }

        // グローバルに計算済みのチップ数で割る
        // （YM2612等は最大出力が20万を超えるため、8で割って音割れとメロディ消失を防ぐ）
        mix_l /= (global_chip_count * 8);
        mix_r /= (global_chip_count * 8);

        int32_t in_l = mix_l;
        int32_t in_r = mix_r;
        
        dc_state_l = (int64_t)(in_l - dc_prev_l) * 16384 + (dc_state_l * 1019) / 1024;
        dc_state_r = (int64_t)(in_r - dc_prev_r) * 16384 + (dc_state_r * 1019) / 1024;
        
        dc_prev_l = in_l;
        dc_prev_r = in_r;
        
        mix_l = (int32_t)(dc_state_l / 16384);
        mix_r = (int32_t)(dc_state_r / 16384);

if (M5.getBoard() == m5::board_t::board_M5AtomS3 || M5.getBoard() == m5::board_t::board_M5AtomVoiceS3R || M5.getBoard() == m5::board_t::board_M5AtomS3R) {
        // ☁E小型スピーカー用 EQ補正 (高音マイルド化 ＋ 低音ブースト)
        
        
        
        // 1. 高音をさらに削る (より強いローパス)
        eq_lpf_l += (mix_l - eq_lpf_l) >> 3;
        eq_lpf_r += (mix_r - eq_lpf_r) >> 3;
        mix_l = eq_lpf_l;
        mix_r = eq_lpf_r;

        // 2. 低音を抽出し、元の音に強く加算してブースト (2倍加算)
        eq_bass_l += (mix_l - eq_bass_l) >> 5;
        eq_bass_r += (mix_r - eq_bass_r) >> 5;
        mix_l += (eq_bass_l * 2);
        mix_r += (eq_bass_r * 2);
}

        int32_t limit = 26000;
        if (mix_l > limit) mix_l = limit + (mix_l - limit) / 2;
        else if (mix_l < -limit) mix_l = -limit + (mix_l + limit) / 2;
        if (mix_r > limit) mix_r = limit + (mix_r - limit) / 2;
        else if (mix_r < -limit) mix_r = -limit + (mix_r + limit) / 2;

        if (mix_l > 32767) mix_l = 32767; else if (mix_l < -32768) mix_l = -32768;
        if (mix_r > 32767) mix_r = 32767; else if (mix_r < -32768) mix_r = -32768;

        out_buf[i * 2]     = (int16_t)mix_l;
        out_buf[i * 2 + 1] = (int16_t)mix_r;
    }
    wav_buff_size[wd] = BUFF_SIZE;
    wd = (wd + 1) % WAV_BUFF_COUNT;
    wav_count++;
    
    uint32_t t_end = esp_timer_get_time();
    if (profile_start_time == 0) profile_start_time = t_end;
    profile_total_time += (t_end - t_start);
    profile_blocks++;

    if (t_end - profile_start_time >= 1000000) {
        uint64_t expected_time = ((uint64_t)profile_blocks * (uint64_t)BUFF_SIZE * 1000000ULL) / (uint64_t)actual_sample_rate;
        float cpu_load = ((float)profile_total_time / (float)expected_time) * 100.0f;
        float fm_load = ((float)prof_fm_time / (float)expected_time) * 100.0f;
        float pcm_load = ((float)prof_pcm_time / (float)expected_time) * 100.0f;
        float vgm_load = ((float)prof_vgm_time / (float)expected_time) * 100.0f;
        // Serial.printf("[DSP] Load: %5.1f%% (FM:%5.1f%% PCM:%5.1f%% VGM:%5.1f%%) | Buf: %2d/%2d\n", cpu_load, fm_load, pcm_load, vgm_load, (int)wav_count.load(), WAV_BUFF_COUNT);
        prof_fm_time = 0;
        prof_pcm_time = 0;
        prof_vgm_time = 0;
        profile_start_time = t_end;
        profile_total_time = 0;
        profile_blocks = 0;
    }
}

// ────────────────────────────────────────────────────────────────────────
// Core 1：波形生成（セマフォ要求駆動型・1024バッファの高速連射）
// ────────────────────────────────────────────────────────────────────────



void audio_generate_task(void *args) {
    while (true) {
        xSemaphoreTake(xSemaphore, portMAX_DELAY);

        uint32_t consecutive_blocks = 0;

        while (isPlaying) {
            if (wav_count < (WAV_BUFF_COUNT - 4)) {
                processAudioBlock();
                if (chip_type == CHIP_YM2151 || chip_type == CHIP_YM2608) {
                    consecutive_blocks++;
                    if (consecutive_blocks >= 4) {
                        vTaskDelay(1);
                        consecutive_blocks = 0;
                    } else {
                        taskYIELD();
                    }
                } else {
                    taskYIELD();
                }
            } else {
                break;
            }
        }
    }
}


// 前方宣言
static bool g_es8388_detected = false;

// ────────────────────────────────────────────────────────────────────────
// Core 0：I2S（DMA）転送 ＆ 減衰検知時のセマフォ発行
// ────────────────────────────────────────────────────────────────────────
void audio_play_task(void *args) {
    while (true) {
        bool any_playing = (isPlaying && !isPaused) || mdx_engine_is_playing();
        if (prebuffering && wav_count >= (WAV_BUFF_COUNT - 4)) {
            prebuffering = false;
        }
        if (any_playing && wav_count > 0 && !prebuffering) {
#if defined(IS_ATOMS3)
            static int16_t mono_buff[512];
            int frames = wav_buff_size[rd];
            for (int i = 0; i < frames; i++) {
                mono_buff[i] = (wav_buff[rd][i * 2] + wav_buff[rd][i * 2 + 1]) / 2;
            }
            bool queued = M5.Speaker.playRaw(mono_buff, frames, actual_sample_rate, false, 1, 0, false);
#else
            bool queued = false;
            if (g_es8388_detected) {
                size_t bytes_written = 0;
                i2s_write(I2S_NUM_0, (const void *)wav_buff[rd], wav_buff_size[rd] * 4, &bytes_written, portMAX_DELAY);
                queued = (bytes_written > 0);
            } else {
                queued = M5.Speaker.playRaw((const int16_t *)wav_buff[rd], wav_buff_size[rd] * 2, actual_sample_rate, true, 1, 0, false);
            }
#endif
            if (queued) {
                rd = (rd + 1) % WAV_BUFF_COUNT;
                wav_count--;
                if (wav_count <= (WAV_BUFF_COUNT / 2)) {
                    xSemaphoreGive(xSemaphore);
                }
                vTaskDelay(pdMS_TO_TICKS(8));
            } else {
                vTaskDelay(1);
            }
        } else {
            static const int16_t idle_silence[512 * 2] = {0};
            if (g_es8388_detected) {
                size_t bytes_written = 0;
                i2s_write(I2S_NUM_0, (const void *)idle_silence, sizeof(idle_silence), &bytes_written, portMAX_DELAY);
            } else {
                M5.Speaker.playRaw(idle_silence, 512 * 2, actual_sample_rate, true, 1, 0, false);
            }
            vTaskDelay(1);
        }
    }
}

// ============================================================================
// ES8388 (M5Stack Module-Audio) 初期化
// ============================================================================
void vgm_engine_init() {
    delay(10);
    
    M5.Speaker.setVolume(0);
    
#if defined(IS_CORES3)
    M5.Speaker.end();
    gpio_reset_pin((gpio_num_t)34);
    gpio_reset_pin((gpio_num_t)33);

    i2s_driver_uninstall(I2S_NUM_0);
    
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,
        .dma_buf_len = 160,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    };
    
    i2s_pin_config_t pin_config = {
        .mck_io_num = 7,
        .bck_io_num = 0,
        .ws_io_num = 6,
        .data_out_num = 13,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    delay(50);
    
    uint8_t addr = 0x10;
    if (!M5.In_I2C.writeRegister8(0x10, 0x00, 0x00, 400000)) {
        if (!M5.In_I2C.writeRegister8(0x11, 0x00, 0x00, 400000)) {
            g_es8388_detected = false;
            goto skip_es8388_init;
        } else {
            addr = 0x11;
        }
    }
    
    {
        auto write_reg = [&](uint8_t reg, uint8_t val) { M5.In_I2C.writeRegister8(addr, reg, val, 400000); };

        write_reg(0x08, 0x40);
        write_reg(0x02, 0xFF);
        write_reg(0x2B, 0x80);
        write_reg(0x00, 0x05);
        write_reg(0x01, 0x40);
        write_reg(0x04, 0x3F);
        write_reg(0x17, 0x18);
        write_reg(0x18, 0x02);
        write_reg(0x19, 0x00);
        write_reg(0x1A, 0x05);
        write_reg(0x1B, 0x05);
        write_reg(0x26, 0x00);
        write_reg(0x27, 0x90);
        write_reg(0x28, 0x38);
        write_reg(0x29, 0x38);
        write_reg(0x2A, 0x90);
        write_reg(0x2B, 0x80);
        write_reg(0x2E, 0x18);
        write_reg(0x2F, 0x18);
        write_reg(0x30, 0x18);
        write_reg(0x31, 0x18);
        write_reg(0x02, 0x00);

        M5.In_I2C.writeRegister8(0x33, 0x10, 0x01, 400000);
        M5.In_I2C.writeRegister8(0x33, 0x30, 100, 400000);
        uint8_t color_green[3] = {0x00, 0xFF, 0x00};
        M5.In_I2C.writeRegister(0x33, 0x40, color_green, 3, 400000);
        
        M5.In_I2C.bitOff(0x58, 0x02, 0x04, 400000);
        
        g_es8388_detected = true;
    }

skip_es8388_init:
    // ES8388未接続時にCoreS3内蔵スピーカーを再起動
    if (!g_es8388_detected) {
        auto spk_cfg = M5.Speaker.config();
        spk_cfg.sample_rate = 44100;
        M5.Speaker.config(spk_cfg);
        M5.Speaker.begin();
        
        // 内蔵スピーカー用に十分な初期ボリュームを設定 (0〜255)
        M5.Speaker.setVolume(128);
    }
#else
    // CoreS3以外のボードではES8388関連の初期化を完全にスキップ
    g_es8388_detected = false;

    // ★ 以前のM5.Speaker初期化処理を復元（AtomS3等のため）
    auto spk_cfg = M5.Speaker.config();
#if defined(IS_ATOMS3)
    spk_cfg.sample_rate = 48000;
#else
    spk_cfg.sample_rate = 44100;
#endif
    spk_cfg.dma_buf_len = 512; 
    spk_cfg.dma_buf_count = 8;
    spk_cfg.task_pinned_core = 0; 
    spk_cfg.task_priority = configMAX_PRIORITIES - 1;
    
    if (M5.getBoard() == m5::board_t::board_M5AtomS3) {
        spk_cfg.pin_bck = 5;
        spk_cfg.pin_ws = 39;
        spk_cfg.pin_data_out = 38;
    }
    else if (M5.getBoard() == m5::board_t::board_M5AtomVoiceS3R) {
        // ATOM Voice S3R 内蔵スピーカー用のI2Sピン設定 (BCK=17, WS=3, DOUT=48)
        spk_cfg.pin_bck = 17;
        spk_cfg.pin_ws = 3;
        spk_cfg.pin_data_out = 48;
    }
    
    M5.Speaker.config(spk_cfg);
    M5.Speaker.begin();
#endif

    // ボード固有のデフォルトボリューム設定
#if defined(IS_CARDPUTER)
    M5.Speaker.setVolume(168);
#elif defined(IS_ATOMS3)
    M5.Speaker.setVolume(80);
    M5.Speaker.tone(0, 50);
#else
    M5.Speaker.setVolume(48);
#endif
    
    // タスク起動
    xSemaphore = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(audio_generate_task, "AudioGenTask", 8192, NULL, 5, &playTaskHandle, 1);
    xTaskCreatePinnedToCore(audio_play_task, "AudioPlayTask", 4096, NULL, 4, NULL, 0);

    isPlaying = false;
}

bool vgm_engine_is_playing(void) {
    return isPlaying;
}

void vgm_engine_set_volume(uint8_t vol) {
    if (g_es8388_detected) {
        // M5Stackのボリューム(0〜255)を ES8388のハードウェアボリューム(0〜0x21)に変換
        uint8_t reg = 0;
        if (vol > 0) {
            reg = (vol * 33) / 255; // 0x21(最大) = 33
            if (reg > 0x21) reg = 0x21;
            if (reg == 0 && vol > 0) reg = 1; // 最小でもミュートにはしない
        }
        
        M5.In_I2C.writeRegister8(0x10, 0x2E, reg, 400000);
        M5.In_I2C.writeRegister8(0x10, 0x2F, reg, 400000);
        M5.In_I2C.writeRegister8(0x10, 0x30, reg, 400000);
        M5.In_I2C.writeRegister8(0x10, 0x31, reg, 400000);
        M5.In_I2C.writeRegister8(0x11, 0x2E, reg, 400000);
        M5.In_I2C.writeRegister8(0x11, 0x2F, reg, 400000);
        M5.In_I2C.writeRegister8(0x11, 0x30, reg, 400000);
        M5.In_I2C.writeRegister8(0x11, 0x31, reg, 400000);
        
        // ★重要：外部モジュール接続時は M5.Speaker.setVolume(vol) を「絶対に呼ばない」
        // （呼ぶとM5Unifiedが内蔵スピーカーの電源を勝手に再投入してしまうため）
    } else {
        // モジュールが無い場合のみ内蔵スピーカーの音量を変更する
        M5.Speaker.setVolume(vol);
    }
}

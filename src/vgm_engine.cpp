#include <Arduino.h>


#include <FFat.h>
#include <SD.h>
#include "vgm_engine.h"
#include <USB.h>
#include <USBMSC.h>
#include <esp_partition.h>
#include <wear_levelling.h>
#include <M5Unified.h>
#include <atomic> // ★ C++11標準のロックフリー同期

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

uint32_t actual_sample_rate = 44100;
uint8_t chip_type = 0;
bool prebuffering = false;
int currentSong = 0;

#define MAX_SONGS 100
String songs[MAX_SONGS];
int numSongs = 0;

uint32_t waitSamples = 0;
uint32_t vgm_time_acc = 0;
uint32_t vgmDataStart = 0;
uint32_t vgmLoopStart = 0;
bool vgm_has_loop = false;

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
const char* vgm_engine_get_title(void) { return current_vgm_title; }

char vgm_last_error[128] = {0};
const char* vgm_engine_get_error(void) { return vgm_last_error; }

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

class FmChip final : public ISoundChip { public: FmChip() : ISoundChip(wrap_fm_tick) {} void write(uint8_t port, uint16_t reg, uint8_t data) override { if (chip_type == 0) fm_engine_register_write(&g_fm_engine, reg, data); else if (chip_type == 1 || chip_type == 5) fm_engine_write_ym2612(&g_fm_engine, port, reg, data); else fm_engine_write_opl(&g_fm_engine, reg, data); } };
class PcmChip final : public ISoundChip { 
public: 
    PcmChip() : ISoundChip(wrap_pcm_tick) {} 
    void write(uint8_t port, uint16_t reg, uint8_t data) override {
        // 何もしない（processVGM内で直接エンジンを呼ぶため）
    } 
};
class PsgChip final : public ISoundChip { 
public: 
    PSGSoundEngine* engine;
    PsgChip(TickFunc tf, PSGSoundEngine* e) : ISoundChip(tf), engine(e) {} 
    void write(uint8_t port, uint16_t reg, uint8_t data) override { 
        psg_engine_write_sn76489(engine, data); 
    } 
};
class SsgChip final : public ISoundChip { public: SsgChip() : ISoundChip(wrap_ssg_tick) {} void write(uint8_t port, uint16_t reg, uint8_t data) override { ssg_engine_write(&g_ssg_engine, reg, data); } };
class SccChip final : public ISoundChip { public: SccChip() : ISoundChip(wrap_scc_tick) {} void write(uint8_t port, uint16_t reg, uint8_t data) override { scc_engine_write(&g_scc_engine, port, reg, data); } };

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

const esp_partition_t* vgm_swap_partition = nullptr;
uint32_t vgm_swap_offset = 0;
uint32_t vgm_swap_erased_limit = 0;

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

bool vgm_engine_play(const char* filepath, bool use_sd) {
    if (isPlaying) {
        vgm_engine_stop();
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
    
    vgm_data = (uint8_t*) heap_caps_malloc(uncompressed_size, MALLOC_CAP_SPIRAM);
    if (!vgm_data) vgm_data = (uint8_t*) malloc(uncompressed_size);
    if (!vgm_data) { 
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
                if (vgm_last_error[0] == '\0') {
                    snprintf(vgm_last_error, sizeof(vgm_last_error), "Decomp. Failed: %d", ret);
                }
                isPlaying = false; return false;
            }
            vgm_file_size = destlen;
        } else {
            if (use_sd) {
                vgm_stream_file = SD.open(filepath, "r");
            } else {
                vgm_stream_file = FFat.open(filepath, "r");
            }
            f.close();
            if (!vgm_stream_file) {
                snprintf(vgm_last_error, sizeof(vgm_last_error), "Failed to open stream file");
                isPlaying = false; return false;
            }
        }
        
        vgm_stream_buf_start = 0;
        vgm_stream_buf_len = 0;
    } else {
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

    memset(current_vgm_title, 0, sizeof(current_vgm_title));
    uint32_t gd3_offset = header[0x14] | (header[0x15] << 8) | (header[0x16] << 16) | (header[0x17] << 24);
    if (gd3_offset != 0) {
        uint32_t abs_gd3 = gd3_offset + 0x14;
        if (abs_gd3 + 12 < vgm_file_size) {
            uint8_t gd3_sig[4];
            if (vgm_is_streaming) {
                if (vgm_swap_partition) {
                    esp_partition_read(vgm_swap_partition, abs_gd3, gd3_sig, 4);
                } else {
                    vgm_stream_file.seek(abs_gd3);
                    vgm_stream_file.read(gd3_sig, 4);
                }
            } else {
                memcpy(gd3_sig, &vgm_data[abs_gd3], 4);
            }
            if (gd3_sig[0] == 'G' && gd3_sig[1] == 'd' && gd3_sig[2] == '3' && gd3_sig[3] == ' ') {
                uint32_t str_offset = abs_gd3 + 12;
                int out_idx = 0;
                while (str_offset + 1 < vgm_file_size && out_idx < 255) {
                    uint8_t c_buf[2];
                    if (vgm_is_streaming) {
                        if (vgm_swap_partition) {
                            esp_partition_read(vgm_swap_partition, str_offset, c_buf, 2);
                        } else {
                            vgm_stream_file.seek(str_offset);
                            vgm_stream_file.read(c_buf, 2);
                        }
                    } else {
                        c_buf[0] = vgm_data[str_offset];
                        c_buf[1] = vgm_data[str_offset+1];
                    }
                    uint16_t c = c_buf[0] | (c_buf[1] << 8);
                    if (c == 0) break;
                    current_vgm_title[out_idx++] = (char)(c & 0xFF);
                    str_offset += 2;
                }
            }
        }
    }

    uint32_t dataOffset = header[0x34] | (header[0x35] << 8) | (header[0x36] << 16) | (header[0x37] << 24);
    vgmDataStart = (dataOffset == 0) ? 0x40 : (0x34 + dataOffset);
    uint32_t loopOffset = header[0x1C] | (header[0x1D] << 8) | (header[0x1E] << 16) | (header[0x1F] << 24);
    vgm_has_loop = (loopOffset != 0);
    vgmLoopStart = vgm_has_loop ? (0x1C + loopOffset) : vgmDataStart;
    
    vgmSeek(vgmDataStart);
    uint32_t vgm_version = header[0x08] | (header[0x09] << 8) | (header[0x0A] << 16) | (header[0x0B] << 24);
    actual_sample_rate = 44100;
    
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

    if (vgm_ym2612_clock != 0 || vgm_ym2151_clock != 0 || vgm_ym2610_clock != 0 || vgm_ym2203_clock != 0 || vgm_ym2608_clock != 0 || vgm_ym3812_clock != 0 || vgm_ym2413_clock != 0) {
        uint32_t fm_clock = 0;
        if      (vgm_ym2612_clock != 0) { chip_type = CHIP_YM2612; fm_clock = vgm_ym2612_clock; }
        else if (vgm_ym2608_clock != 0) { chip_type = CHIP_YM2612; fm_clock = vgm_ym2608_clock; }
        else if (vgm_ym2610_clock != 0) { chip_type = CHIP_YM2612; fm_clock = vgm_ym2610_clock; }
        else if (vgm_ym2203_clock != 0) { chip_type = CHIP_YM2203; fm_clock = vgm_ym2203_clock; }
        else if (vgm_ym3812_clock != 0) { chip_type = CHIP_YM3812; fm_clock = vgm_ym3812_clock; }
        else if (vgm_ym2413_clock != 0) { chip_type = CHIP_YM2413; fm_clock = vgm_ym2413_clock; }
        else                            { chip_type = CHIP_YM2151; fm_clock = vgm_ym2151_clock; }
        fm_engine_init(&g_fm_engine, actual_sample_rate, fm_clock, chip_type);
        active_chips[active_chip_count++] = &fm_wrapper;
        if (chip_type == CHIP_YM2612) active_channel_count += 6;
        else if (chip_type == CHIP_YM2151) active_channel_count += 8;
        else if (chip_type == CHIP_YM2203) active_channel_count += 3;
        else active_channel_count += 9; // OPL系等は9音
    }

    if (vgm_msm6258_clock != 0 || vgm_oki_clock != 0 || vgm_segapcm_clock != 0 || vgm_ym2612_clock != 0) {
        pcm_engine_init(&g_pcm_engine, actual_sample_rate); active_chips[active_chip_count++] = &pcm_wrapper;
        active_channel_count += 1;
        if (vgm_msm6258_clock != 0) pcm_engine_set_msm6258_clock(&g_pcm_engine, vgm_msm6258_clock);
        if (vgm_oki_clock != 0)     pcm_engine_set_oki_clock(&g_pcm_engine, vgm_oki_clock);
        if (vgm_segapcm_clock != 0) pcm_engine_segapcm_init(&g_pcm_engine, vgm_segapcm_clock & 0x3FFFFFFF, vgm_spcm_intf);
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
        if (ssg_clock == 0) ssg_clock = (vgm_ym2610_clock != 0) ? vgm_ym2610_clock / 4 : ((vgm_ym2608_clock != 0) ? vgm_ym2608_clock / 4 : ((vgm_ym2203_clock != 0) ? vgm_ym2203_clock / 4 : 1500000));
        ssg_engine_init(&g_ssg_engine, actual_sample_rate, ssg_clock); active_chips[active_chip_count++] = &ssg_wrapper;
        active_channel_count += 3;
    }
    if (vgm_scc_clock != 0) {
        bool is_scc_plus = (vgm_scc_clock & 0x80000000) != 0;
        Serial.printf("[VGM] SCC Clock: %lu, SCC+: %d\n", vgm_scc_clock & 0x3FFFFFFF, is_scc_plus);
        scc_engine_init(&g_scc_engine, actual_sample_rate, vgm_scc_clock & 0x3FFFFFFF); active_chips[active_chip_count++] = &scc_wrapper;
        active_channel_count += 5;
    }

    waitSamples = 0; vgm_time_acc = 0; ym2612_pcm_offset = 0;
    
    sn76489_writes = 0;
    rd = 0; wd = 0; wav_count = 0;
    
    // 曲の開始時にDCブロッカーの状態を完全にクリアする
    dc_state_l = 0; dc_state_r = 0; dc_prev_l = 0; dc_prev_r = 0;
    eq_lpf_l = 0; eq_lpf_r = 0; eq_bass_l = 0; eq_bass_r = 0;
    


    // 背景タスク(audio_generate_task)が常に無音を流しているため、アンプ起動用の500ms待機は不要になりました
    prebuffering = true;
    isPlaying = true;
    xSemaphoreGive(xSemaphore); 
    return true;
}

void vgm_engine_stop() { 
    isPlaying = false; 
    delay(50); 
    pcm_engine_free(&g_pcm_engine); 
}

static inline uint8_t readByte() {
    if (vgm_ptr >= vgm_file_size) { isPlaying = false; return 0x66; }
    if (vgm_is_streaming) {
        if (vgm_ptr < vgm_stream_buf_start || vgm_ptr >= vgm_stream_buf_start + vgm_stream_buf_len) {
            uint32_t read_len = sizeof(vgm_stream_buf);
            if (vgm_ptr + read_len > vgm_file_size) read_len = vgm_file_size - vgm_ptr;
            if (vgm_swap_partition) {
                esp_partition_read(vgm_swap_partition, vgm_ptr, vgm_stream_buf, read_len);
                vgm_stream_buf_len = read_len;
            } else {
                vgm_stream_file.seek(vgm_ptr);
                vgm_stream_buf_len = vgm_stream_file.read(vgm_stream_buf, sizeof(vgm_stream_buf));
            }
            vgm_stream_buf_start = vgm_ptr;
        }
        return vgm_stream_buf[vgm_ptr++ - vgm_stream_buf_start];
    }
    return vgm_data[vgm_ptr++];
}

void processVGM() {
    if (!isPlaying || (!vgm_is_streaming && vgm_data == nullptr)) return;

    while (vgm_ptr < vgm_file_size && waitSamples == 0) {
        uint8_t cmd = readByte(); 
        switch (cmd) {
            case 0x30: { uint8_t val = readByte(); if (is_dual_sn76489) psg_wrapper2.write(0, 0, val); break; }
            case 0x50: { 
                uint8_t val = readByte(); 
                if (sn76489_writes < 5) {
                    Serial.printf("[VGM] SN76489 Write: %02X\n", val);
                    sn76489_writes++;
                }
                psg_wrapper.write(0, 0, val); 
                break; 
            }
            case 0x4F: { uint8_t val = readByte(); /* Game Gear Stereo - not supported yet */ break; }
            case 0x51: 
            case 0x5A: { uint8_t reg = readByte(); uint8_t val = readByte(); fm_wrapper.write(0, reg, val); break; }
            case 0x52: case 0x53: 
            case 0x56: case 0x57: 
            case 0x58: case 0x59: {
                uint8_t port = cmd & 1;
                uint8_t reg = readByte(); uint8_t val = readByte();
                if (port == 0 && reg < 0x10) {
                    ssg_wrapper.write(0, reg, val);
                } else {
                    fm_wrapper.write(port, reg, val);
                }
                // 直接 YM2612 PCM関数を呼ぶ
                if (port == 0 && (reg == 0x2A || reg == 0x2B)) pcm_engine_write_ym2612(&g_pcm_engine, reg, val);
                break;
            }
            case 0x54: { uint8_t reg = readByte(); uint8_t val = readByte(); fm_wrapper.write(0, reg, val); break; }
            case 0x55: { uint8_t reg = readByte(); uint8_t val = readByte(); if (reg < 0x10) ssg_wrapper.write(0, reg, val); else fm_wrapper.write(0, reg, val); break; }
            case 0x61: { uint16_t n = readByte(); n |= (readByte() << 8); waitSamples = n; break; }
            case 0x62: waitSamples = 735; break;
            case 0x63: waitSamples = 882; break;
            case 0x66: {
                if (!vgm_has_loop) { isPlaying = false; }
                else { 
                    // ループ時の処理：
                    // シンバルなどの余韻（RR > 0）は自然に減衰（EG_RELEASE）させることで不自然な切れ方を防ぐ。
                    // 一方で、オルガンやビープ音などの持続音（RR == 0）はリリース状態にしても永遠に鳴り続けるため、
                    // 強制的に無音（EG_OFF）にして高周波の残り（ピーー音）を完全に防ぐ。
                    for (int i = 0; i < TOTAL_OPS; i++) {
                        // エンジン内部では register_rr に (rr<<1)+1 を入れているため、
                        // 元のレジスタ値が 0 や 1 の極端に遅いリリース（無限持続）は rr <= 3 となる。
                        if (g_fm_engine.ops[i].rr <= 3) {
                            g_fm_engine.ops[i].env_state = EG_OFF;
                            g_fm_engine.ops[i].env_level = (0x3FF << 11);
                        } else {
                            g_fm_engine.ops[i].env_state = EG_RELEASE;
                        }
                    }
                    vgmSeek(vgmLoopStart); 
                }
                break;
            }
            case 0x67: { 
                readByte(); 
                uint8_t type = readByte(); 
                uint32_t size = readByte(); 
                size |= (readByte() << 8); 
                size |= (readByte() << 16); 
                size |= (readByte() << 24); 
                if (vgm_ptr + size > vgm_file_size) {
                    size = (vgm_file_size > vgm_ptr) ? (vgm_file_size - vgm_ptr) : 0;
                }
                if (vgm_is_streaming) {
                    uint8_t* temp_block = (uint8_t*)malloc(size);
                    if (temp_block) {
                        if (vgm_swap_partition) {
                            esp_partition_read(vgm_swap_partition, vgm_ptr, temp_block, size);
                        } else {
                            vgm_stream_file.seek(vgm_ptr);
                            vgm_stream_file.read(temp_block, size);
                        }
                        pcm_engine_add_data_block(&g_pcm_engine, type, temp_block, size);
                        free(temp_block);
                    }
                } else {
                    pcm_engine_add_data_block(&g_pcm_engine, type, vgm_data + vgm_ptr, size); 
                }
                vgm_ptr += size; 
                break; 
            }
            case 0x68: { vgm_ptr += 11; break; }
            case 0xA0: { 
                uint8_t reg = readByte(); uint8_t val = readByte(); 
                ssg_wrapper.write(0, reg, val); 
                break; 
            }
            // ★ 修正：各PCMチップの専用関数を直接呼ぶ
            case 0xB7: { uint8_t reg = readByte(); uint8_t val = readByte(); pcm_engine_write_msm6258(&g_pcm_engine, reg, val); break; }
            case 0xB8: { uint8_t val = readByte(); pcm_engine_write_oki(&g_pcm_engine, val); break; }
            case 0xC0: { uint16_t offset = readByte(); offset |= (readByte() << 8); uint8_t val = readByte(); pcm_engine_segapcm_write(&g_pcm_engine, offset & 0xFF, val); break; }
            case 0xD2: { 
                uint8_t port = readByte(); uint8_t reg = readByte(); uint8_t val = readByte(); 
                scc_engine_write(&g_scc_engine, port, reg, val); 
                break; 
            }
            
            default:
                if (cmd >= 0x70 && cmd <= 0x7F) waitSamples = (cmd & 0x0F) + 1;
                else if (cmd >= 0x80 && cmd <= 0x8F) { 
                    waitSamples = (cmd & 0x0F); 
                    if (g_pcm_engine.ym2612_pcm_block != nullptr && ym2612_pcm_offset < g_pcm_engine.ym2612_pcm_size) {
                        pcm_engine_write_ym2612(&g_pcm_engine, 0x2A, g_pcm_engine.ym2612_pcm_block[ym2612_pcm_offset++]);
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
                else if (cmd >= 0xE1 && cmd <= 0xFF) vgm_ptr += 4;
                break;
        }
    }
}

void IRAM_ATTR processAudioBlock() {
    if (!isPlaying) return;
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
                processVGM(); 
                if (waitSamples == 0) break;
            }
        }

        int32_t mix_l = 0, mix_r = 0;
        
        for (int c = 0; c < active_chip_count; c++) { 
            active_chips[c]->tick(&mix_l, &mix_r); 
        }

        // 全チップの合計音量を鳴っているチップ数に応じてスケールダウン
        // チャンネル数で割ると音が小さくなりすぎてダイナミックレンジが狭まるため、チップ数で割ります
        int divisor = (active_chip_count > 0) ? active_chip_count : 1;
        
        mix_l /= divisor;
        mix_r /= divisor;

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
}

// ────────────────────────────────────────────────────────────────────────
// Core 1：波形生成（セマフォ要求駆動型・1024バッファの高速連射）
// ────────────────────────────────────────────────────────────────────────
void audio_generate_task(void *args) {
    while (true) {
        xSemaphoreTake(xSemaphore, portMAX_DELAY); 

        while (isPlaying) {
            if (wav_count < (WAV_BUFF_COUNT - 4)) {
                processAudioBlock(); 
                // ★ 追加：Core 1の独占を防ぎ、PSRAMやDMAバスの渋滞（ノイズ）を解消する
                taskYIELD(); 
            } else {
                break; // 満杯になったらスリープに戻る
            }
        }
    }
}

// ────────────────────────────────────────────────────────────────────────
// Core 0：I2S（DMA）転送 ＆ 減衰検知時のセマフォ発行
// ────────────────────────────────────────────────────────────────────────


void audio_play_task(void *args) {
    while (true) {
        if (prebuffering && wav_count >= (WAV_BUFF_COUNT - 4)) {
            prebuffering = false;
        }
        if (wav_count > 0 && isPlaying && !prebuffering) {
            if (isPaused) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            bool queued = M5.Speaker.playRaw((const int16_t *)wav_buff[rd], wav_buff_size[rd] * 2, actual_sample_rate, true, 1, 0, false);
            
            if (queued) {
                // エンキュー成功
                rd = (rd + 1) % WAV_BUFF_COUNT;
                wav_count--; 
                
                if (wav_count <= (WAV_BUFF_COUNT / 2)) {
                    xSemaphoreGive(xSemaphore); 
                }
                
                // ★ M5.Speaker内部のキュー枯渇を防ぐため、物理的な再生時間（11.6ms）に対して
                // WDT回避用の8msだけ休み、残りはスピンロックで即座に次のデータを叩き込む
                vTaskDelay(pdMS_TO_TICKS(8)); 
            } else {
                // taskYIELD() は絶対に使用しないでください！Priority 5の無限スピンロックとなり、
                // Core 0を占有してM5.Speakerのバックグラウンドタスク(Priority 2)をCore 1に追い出し、
                // 波形生成タスク(Priority 1)を意図せずプリエンプト（横取り）して処理落ちクリック音を生みます。
                vTaskDelay(1); 
            }
        } else {
            // データがない・またはプリバッファリング・停止中も常に無音を流してアンプの電源ON/OFF（プツ音）を防ぐ
            static const int16_t idle_silence[512 * 2] = {0};
            M5.Speaker.playRaw(idle_silence, 512 * 2, actual_sample_rate, true, 1, 0, false);
            vTaskDelay(1);
        }
    }
}


    
    void vgm_engine_init() {
    
    auto spk_cfg = M5.Speaker.config();
    spk_cfg.sample_rate = 44100;
    spk_cfg.dma_buf_len = 512; 
    spk_cfg.dma_buf_count = 8;
    spk_cfg.task_pinned_core = 0; // DMAはCore 0
    spk_cfg.task_priority = configMAX_PRIORITIES - 1; // 内部I2Sタスクの優先度を最大化
    
    // board認識によるスピーカーピンの動的設定
    if (M5.getBoard() == m5::board_t::board_M5AtomS3) {
        // AtomS3 + Atomic SPK 用のI2Sピン設定 (BCLK=5, WS/LRCK=39, DOUT=38)
        spk_cfg.pin_bck = 5;
        spk_cfg.pin_ws = 39;
        spk_cfg.pin_data_out = 38;
    }
    
    M5.Speaker.config(spk_cfg);
    M5.Speaker.begin();
    if (M5.getBoard() == m5::board_t::board_M5Cardputer) {
        M5.Speaker.setVolume(128);
    } else if (M5.getBoard() == m5::board_t::board_M5AtomVoiceS3R || M5.getBoard() == m5::board_t::board_M5AtomS3R) {
        M5.Speaker.setVolume(80); // 音割れ防止のためAtomS3R専用に音量を下げる
    } else {
        M5.Speaker.setVolume(48); // AtomS3 + 外部アンプ等は音量を絞る
    }
    
    
    xSemaphore = xSemaphoreCreateBinary(); 
    

    // DSP生成タスクをCore 0に移動し、メインループ(Core 1)のM5.update()による遅延から完全に切り離す
    // 優先度は2に設定
    xTaskCreatePinnedToCore(audio_generate_task, "AudioGenTask", 8192, NULL, 2, &playTaskHandle, 0);
    
    // 再生タスクはCore 0の高優先度
    xTaskCreatePinnedToCore(audio_play_task, "AudioPlayTask", 4096, NULL, 4, NULL, 0);

    isPlaying = false;
}

bool vgm_engine_is_playing(void) {
    return isPlaying;
}

void vgm_engine_set_volume(uint8_t vol) {
    M5.Speaker.setVolume(vol);
}

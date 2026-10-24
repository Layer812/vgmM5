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
#include "tab5_audio_queue_accounting.hpp"
#include "tab5_audio_consumer_fence.hpp"
#include "tab5_audio_completion.hpp"
#include "tab5_spectrum.hpp"
#include <esp_heap_caps.h>
#include <esp_timer.h> // R42: per-completed-block CPU budget observer (not in sample loop)
#include "tab5_psram_bounds.hpp"
#include "tab5_track_meta.hpp"
#include "tab5_live_monitor.hpp"

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
#include "fm_engine.h"
#include "r41_ym2151_native.h"
#include "r47_ym2151_fir25.h"
#if defined(VGMM5_R49_SEGMENT_SWITCH)
#include "r49_3s_audio_segments.h"
#endif
#include "r31_vgm_bridge.h"
#include "pcm_engine.h"
#include "psg_engine.h"
extern PSGSoundEngine g_psg_engine;
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
static Tab5AudioQueueAccounting s_audio_queue;
static Tab5AudioConsumerFence s_audio_fence;
// Natural EOF must drain the short final PCM blocks before folder auto-next.
// Manual STOP explicitly cancels this post-roll; generation timing unchanged.
static std::atomic<bool> s_audio_tail_drain{false};
static std::atomic<bool> s_vgm_manual_stop{true};
void vgm_audio_queue_begin_tail_drain(void) {
    s_audio_tail_drain.store(true, std::memory_order_release);
}
void vgm_audio_queue_cancel_tail_drain(void) {
    s_audio_tail_drain.store(false, std::memory_order_release);
}
static bool audio_consumer_enter() { return s_audio_fence.enter(); }
// M5Unified 0.2.26 calls this from its speaker task AFTER it is done
// reading the PCM pointer. The callback must never block or call Speaker.
static void tab5_speaker_buffer_released(void*, const void* data, uint8_t channel) {
    if (channel != 0) return;
    for (unsigned slot = 0; slot < WAV_BUFF_COUNT; ++slot) {
        if (data == static_cast<const void*>(wav_buff[slot])) {
            s_audio_queue.released(slot);
            return;
        }
    }
}
static void audio_consumer_leave() { s_audio_fence.leave(); }
void vgm_audio_queue_publish(void) { s_audio_queue.publish(wav_count); }
bool vgm_audio_queue_is_flushed(void) {
    // Callback completion does not necessarily coincide with Speaker channel
    // becoming idle. Never auto-next while it still reports playback active.
    return tab5::audio_output_complete(
        !s_audio_fence.closed(),
        wav_count.load(std::memory_order_acquire),
        s_audio_queue.pending(),
        s_audio_queue.outstanding(),
        M5.Speaker.isPlaying());
}

// R22: bounded observer counters; no logging or timing in synthesis loop.
static std::atomic<uint32_t> r22_gen_blocks{0};
static std::atomic<uint32_t> r22_play_ok{0}, r22_play_fail{0}, r22_starve{0}, r22_silence{0};
static std::atomic<uint32_t> r22_low_water{WAV_BUFF_COUNT};
// R42: normal firmware-only performance observability; two wall-time samples
// per completed VGM block when YM2151 native-rate bridge is active.
// No timers or allocations inside the per-sample FM/resampling path.
static std::atomic<uint32_t> r42_native_blocks{0};
static std::atomic<uint32_t> r42_native_over_budget{0};
static std::atomic<uint32_t> r42_native_max_gen_us{0};
static inline void r42_atomic_max(std::atomic<uint32_t>& target, uint32_t value) {
    uint32_t old=target.load(std::memory_order_relaxed);
    while (value>old && !target.compare_exchange_weak(old,value,std::memory_order_relaxed)) {}
}

static inline void r22_min(std::atomic<uint32_t>& a,uint32_t n){
 uint32_t p=a.load(std::memory_order_relaxed);
 while(n<p && !a.compare_exchange_weak(p,n,std::memory_order_relaxed)){}
}
 


// ★ 追加：完全整数（固定小数点）型のDCブロッカー用状態変数
int64_t dc_state_l = 0;
int64_t dc_state_r = 0;
#if !defined(VGMM5_TAB5_FORCE_NEUTRAL_EQ)
int32_t eq_lpf_l = 0;
int32_t eq_lpf_r = 0;
int32_t eq_bass_l = 0;
int32_t eq_bass_r = 0;
#endif
int32_t dc_prev_l = 0;
int32_t dc_prev_r = 0;
static std::atomic<bool> isPlaying{false};
TaskHandle_t playTaskHandle = NULL;
std::atomic<bool> prebuffering{false};
// Generation-idle acknowledgment: protects PCM backing storage while stopping.
// Set to false BEFORE posting a play semaphore; set true only after the worker
// has left the generation loop. A fixed delay is not a completion guarantee.
static std::atomic<bool> s_vgm_gen_idle{true};
bool vgm_engine_generation_idle(void) {
    return s_vgm_gen_idle.load(std::memory_order_acquire);
}
// True only if generators cannot publish, AudioPlayTask cannot submit, and
// Speaker_Class has relinquished all queued PCM pointers.
bool vgm_audio_queue_flush_if_idle(void) {
    if (!vgm_engine_generation_idle() || mdx_engine_is_playing()) return false;
    s_audio_fence.close();
    uint32_t began = millis();
    while (!s_audio_fence.idle()) {
        if ((uint32_t)(millis() - began) >= 2000u) {
            Serial.println("[R31_OPT4][AUDIO] FLUSH TIMEOUT: AudioPlayTask busy");
            return false; // fail closed: consumer remains fenced
        }
        vTaskDelay(1);
    }
    // Do NOT call Speaker.stop() before natural drain: M5Unified explicitly
    // does not call release callbacks for queued requests discarded by stop().
    // The consumer is fenced, so the remaining pending (not yet submitted)
    // slots may be dropped once every submitted request has been released.
    began = millis();
    while (s_audio_queue.outstanding() != 0 || M5.Speaker.isPlaying()) {
        if ((uint32_t)(millis() - began) >= 2000u) {
            Serial.println("[R31_OPT6][AUDIO] FLUSH TIMEOUT: speaker PCM still borrowed");
            return false; // fail closed; never recycle a live PCM pointer
        }
        vTaskDelay(1);
    }
    // No speaker callbacks can reference a ring slot now; the speaker's
    // last callback has finished its state update (release ordering).
    s_audio_queue.reclaim(wav_count);
    rd = 0;
    wd = 0;
    s_audio_queue.reset(wav_count);
    prebuffering.store(false, std::memory_order_release);
    vgm_audio_queue_cancel_tail_drain();
    s_audio_fence.reopen();
    return true;
}
void vgm_engine_diag_print(void){
 Serial.printf("[R22][AUDIO] vgm=%d mdx=%d q=%d retained=%d gen=%lu maxGenUs=%lu nativeBlocks=%lu overBudget=%lu playOK=%lu playFail=%lu maxPlayUs=OFF starve=%lu silence=%lu lowQ=%lu\n",
 (int)isPlaying,(int)mdx_engine_is_playing(),s_audio_queue.pending(),wav_count.load(),
 (unsigned long)r22_gen_blocks.exchange(0),
 (unsigned long)r42_native_max_gen_us.exchange(0),
 (unsigned long)r42_native_blocks.exchange(0),
 (unsigned long)r42_native_over_budget.exchange(0),
 (unsigned long)r22_play_ok.exchange(0),
 (unsigned long)r22_play_fail.exchange(0),
 (unsigned long)r22_starve.exchange(0),
 (unsigned long)r22_silence.exchange(0),
 (unsigned long)r22_low_water.exchange(WAV_BUFF_COUNT));
}
static std::atomic<bool> isPaused{false};
static SemaphoreHandle_t xSemaphore = NULL;
#if defined(IS_ATOMS3)
uint32_t actual_sample_rate = 48000;
#else
uint32_t actual_sample_rate = 44100;
#endif
uint8_t chip_type = 0;
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

uint8_t* vgm_data = nullptr;
bool vgm_is_streaming = false;
File vgm_stream_file;
uint8_t vgm_stream_buf[1024];
uint32_t vgm_stream_buf_start = 0;
uint32_t vgm_stream_buf_len = 0;
uint32_t vgm_file_size = 0;
uint32_t vgm_ptr = 0;
uint32_t ym2612_pcm_offset = 0;

static tab5::TrackMeta g_vgm_meta;
const tab5::TrackMeta& vgm_engine_get_metadata(){return g_vgm_meta;}
char current_vgm_title[256] = {0};
const char* vgm_engine_get_title(void) { return current_vgm_title; }

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

// R5: the YM2413 register + 18-slot phase/EG state is ~1.2 KiB.
// ESP32-P4 rev1.3 names its legacy TCM region SPM (~8 KiB total).
// Prefer SPM for CPU-only hot state, but preserve the original RAM fallback.
// NEVER put Speaker/I2S DMA buffers or shared audio rings in SPM.
static r31_vgm_bridge g_r31_vgm_dram;
static r31_vgm_bridge* g_r31_vgm_ptr = &g_r31_vgm_dram;
static bool g_r31_spm_checked = false;
static void r31_choose_hot_state_memory() {
    if (g_r31_spm_checked) return;
    g_r31_spm_checked = true;
#if defined(CONFIG_IDF_TARGET_ESP32P4) && defined(MALLOC_CAP_SPM)
    void* mem = heap_caps_malloc(sizeof(r31_vgm_bridge), MALLOC_CAP_SPM | MALLOC_CAP_8BIT);
    if (mem) {
        g_r31_vgm_ptr = static_cast<r31_vgm_bridge*>(mem);
        Serial.printf("[R31][OPLL_SPM] allocated %u CPU-state bytes in SPM\n",
                      (unsigned)sizeof(r31_vgm_bridge));
    } else {
        Serial.printf("[R31][OPLL_SPM] unavailable; using internal fallback state (%u bytes)\n",
                      (unsigned)sizeof(r31_vgm_bridge));
    }
#else
    Serial.println("[R31][OPLL_SPM] SDK lacks MALLOC_CAP_SPM; using RAM fallback");
#endif
}
static std::atomic<bool> g_r31_active{false};
void IRAM_ATTR wrap_r31_tick(ISoundChip*, int32_t* l, int32_t* r) {
    if(g_r31_active) r31_vgm_bridge_mix(g_r31_vgm_ptr,l,r);
}
// Only YM2151 uses the native-rate bridge. Other chips use their exact old tick path.
static R41YM2151Native r41_ym2151_native = {};
static R47YM2151Fir25 r47_ym2151_fir = {};
#if defined(VGMM5_R49_SEGMENT_SWITCH)
static R49AudioSegments s_r49_segments = {};
static const char* const s_r49_segment_names[4] = {
    "FIR_FM_PLUS_PCM", "LINEAR_FM_PLUS_PCM", "PCM_ONLY", "FIR_FM_ONLY"
};
#endif
static void IRAM_ATTR r41_fm_native_core(void* engine, int32_t* l, int32_t* r) {
    // Called once per native-rate FM tick; never filter other sound chips.
    int32_t fm_l=0, fm_r=0;
    fm_engine_tick(static_cast<FMSoundEngine*>(engine), &fm_l, &fm_r);
#if defined(VGMM5_R47_YM2151_FIR25_NATIVE)
#if defined(VGMM5_R49_SEGMENT_SWITCH)
    int32_t filtered_l=0, filtered_r=0;
    r47_ym2151_fir_tick(&r47_ym2151_fir, fm_l, fm_r, &filtered_l, &filtered_r);
    const uint16_t fir_q8=s_r49_segments.fir_q8;
    fm_l=r49_q8_blend(fm_l,filtered_l,fir_q8);
    fm_r=r49_q8_blend(fm_r,filtered_r,fir_q8);
#else
    r47_ym2151_fir_tick(&r47_ym2151_fir, fm_l, fm_r, &fm_l, &fm_r);
#endif
#endif
    *l += fm_l; *r += fm_r;
}
void IRAM_ATTR wrap_fm_tick(ISoundChip* obj, int32_t* l, int32_t* r) {
    if (g_fm_engine.chip_type == CHIP_YM2151 && r41_ym2151_native.enabled) {
        r41_native_tick(&r41_ym2151_native, r41_fm_native_core, &g_fm_engine, l, r);
    } else {
        fm_engine_tick(&g_fm_engine,l,r);
    }
}
void IRAM_ATTR wrap_pcm_tick(ISoundChip* obj, int32_t* l, int32_t* r) { pcm_engine_tick(&g_pcm_engine, l, r); }
void IRAM_ATTR wrap_psg_tick1(ISoundChip* obj, int32_t* l, int32_t* r) { psg_engine_update(&g_psg_engine, l, r); }
void IRAM_ATTR wrap_psg_tick2(ISoundChip* obj, int32_t* l, int32_t* r) { psg_engine_update(&g_psg_engine2, l, r); }
void IRAM_ATTR wrap_ssg_tick(ISoundChip* obj, int32_t* l, int32_t* r) { ssg_engine_update(&g_ssg_engine, l, r); }
void IRAM_ATTR wrap_scc_tick(ISoundChip* obj, int32_t* l, int32_t* r) { scc_engine_tick(&g_scc_engine, l, r); }

class R31Ym2413Chip final : public ISoundChip {
public:
    R31Ym2413Chip() : ISoundChip(wrap_r31_tick) {}
    void write(uint8_t, uint16_t reg, uint8_t data) override {
        if (g_r31_active) r31_vgm_bridge_write(g_r31_vgm_ptr,reg,data);
        // R13: no UI channel logging in the Core0 register-write hot path.
    }
};
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
R31Ym2413Chip r31_ym2413_wrapper;
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
static esp_partition_mmap_handle_t vgm_mmap_handle = 0;



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
    // Direct API calls must not load VGM on top of an active MDX publisher.
    if (mdx_engine_is_playing()) {
        snprintf(vgm_last_error, sizeof(vgm_last_error), "MDX still active");
        return false;
    }
    // Always stop and clean up before starting a new song, regardless of current state
    vgm_engine_stop();
    if (!vgm_audio_queue_is_flushed()) {
        snprintf(vgm_last_error, sizeof(vgm_last_error), "Audio queue not stopped");
        return false;
    }
    if (!vgm_engine_generation_idle()) {
        snprintf(vgm_last_error, sizeof(vgm_last_error), "VGM generator failed to stop");
        Serial.println("[R31_OPT3][VGM] Refusing reload: generation task still active");
        return false;
    }
    
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
        f.seek(f.size() - 4); uint8_t sizeBytes[4];
        if (f.read(sizeBytes, 4) != 4) {
            snprintf(vgm_last_error, sizeof(vgm_last_error), "Invalid VGZ size trailer");
            f.close(); isPlaying = false; return false;
        }
        uncompressed_size = (uint32_t)sizeBytes[0] | ((uint32_t)sizeBytes[1] << 8) |
                            ((uint32_t)sizeBytes[2] << 16) | ((uint32_t)sizeBytes[3] << 24);
        f.seek(0);
    }
    
    vgm_data = nullptr;
    if (true) {
        vgm_is_streaming = true;
        vgm_swap_partition = nullptr;
        if (isVgz) {
            vgm_swap_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "vgm_swap");
            if (!vgm_swap_partition) {
                // ESP32-P4 Tab5 live layout has no vgm_swap. Never touch the
                // unpartitioned flash tail: allocate exactly the decompressed
                // VGM size in PSRAM and reuse the normal in-memory PCM scan.
                if (uncompressed_size < 256) {
                    snprintf(vgm_last_error, sizeof(vgm_last_error), "Invalid VGZ uncompressed size");
                    f.close(); isPlaying = false; return false;
                }
                vgm_data = static_cast<uint8_t*>(heap_caps_malloc(
                    uncompressed_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                if (!vgm_data) {
                    snprintf(vgm_last_error, sizeof(vgm_last_error),
                             "VGZ needs %lu bytes PSRAM", (unsigned long)uncompressed_size);
                    f.close(); isPlaying = false; return false;
                }
                Serial.printf("[VGM] VGZ -> PSRAM: %lu bytes\n", (unsigned long)uncompressed_size);
            } else {
                vgm_swap_offset = 0;
                vgm_swap_erased_limit = 0;
            }
            
            f.seek(0);
            int gzipOffset = skipGzipHeaderStream(f);
            if (gzipOffset < 0) {
                snprintf(vgm_last_error, sizeof(vgm_last_error), "Invalid GZIP Header");
                if (!vgm_swap_partition) { free(vgm_data); vgm_data = nullptr; }
                f.close(); isPlaying = false; return false;
            }
            
            uint8_t* window = (uint8_t*)malloc(32768);
            if (!window) {
                snprintf(vgm_last_error, sizeof(vgm_last_error), "OOM (32KB Window)");
                if (!vgm_swap_partition) { free(vgm_data); vgm_data = nullptr; }
                f.close(); isPlaying = false; return false;
            }
            
            uint8_t* in_window = (uint8_t*)malloc(1024);
            if (!in_window) {
                snprintf(vgm_last_error, sizeof(vgm_last_error), "OOM (1KB In Window)");
                if (!vgm_swap_partition) { free(vgm_data); vgm_data = nullptr; }
                free(window); f.close(); isPlaying = false; return false;
            }
            
            unsigned long destlen = uncompressed_size;
            tab5::CheckedRamSink sink { vgm_data, uncompressed_size, 0 };
            int ret = puff_stream(vgm_swap_partition ? puff_write_cb : tab5::checked_ram_append,
                                  vgm_swap_partition ? nullptr : &sink,
                                  32768, puff_read_cb, &f, 1024, window, &destlen, in_window, nullptr);
            free(window);
            free(in_window);
            f.close();
            if (ret != 0 || destlen != uncompressed_size ||
                (!vgm_swap_partition && sink.used != uncompressed_size)) {
                snprintf(vgm_last_error, sizeof(vgm_last_error), "VGZ inflate failed: %d", ret);
                if (!vgm_swap_partition) { free(vgm_data); vgm_data = nullptr; }
                isPlaying = false; return false;
            }
            vgm_file_size = destlen;
            if (!vgm_swap_partition) {
                vgm_is_streaming = false;
            } else {
                const void* mapped_ptr = nullptr;
                esp_err_t mmap_err = esp_partition_mmap(vgm_swap_partition, 0,
                    (vgm_file_size + 0xFFFF) & ~0xFFFF, ESP_PARTITION_MMAP_DATA,
                    &mapped_ptr, &vgm_mmap_handle);
                if (mmap_err == ESP_OK) {
                    vgm_data = (uint8_t*)mapped_ptr;
                    vgm_is_streaming = false;
                } else {
                    snprintf(vgm_last_error, sizeof(vgm_last_error), "MMAP Failed: %d", mmap_err);
                    isPlaying = false; return false;
                }
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
                esp_err_t mmap_err = esp_partition_mmap(vgm_swap_partition, 0, (vgm_file_size + 0xFFFF) & ~0xFFFF, ESP_PARTITION_MMAP_DATA, &mapped_ptr, &vgm_mmap_handle);
                if (mmap_err == ESP_OK) {
                    vgm_data = (uint8_t*)mapped_ptr;
                    vgm_is_streaming = false;
                    Serial.printf("[VGM] .vgm MMAP success!\n");
                } else {
                    Serial.printf("[VGM] .vgm MMAP failed: %d\n", mmap_err);
                }
            }
            
            // A full PSRAM image also preserves pointers to 0x67 PCM blocks.
            // If it does not fit, retain original uncompressed VGM SD stream.
            if (!vgm_swap_partition && vgm_file_size >= 256) {
                uint8_t* ram = static_cast<uint8_t*>(heap_caps_malloc(
                    vgm_file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                if (ram) {
                    f.seek(0);
                    size_t nread = f.read(ram, vgm_file_size);
                    if (nread == vgm_file_size) {
                        vgm_data = ram;
                        vgm_is_streaming = false;
                        Serial.printf("[VGM] .vgm -> PSRAM: %lu bytes\n", (unsigned long)vgm_file_size);
                    } else {
                        free(ram);
                    }
                } else {
                    Serial.printf("[VGM] .vgm too large for PSRAM (%lu bytes): SD fallback\n",
                                  (unsigned long)vgm_file_size);
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

    // R10: UTF-16LE GD3 metadata from the *actual* loaded VGM/VGZ stream.
    // Bounded read at song load only; the generator and its scheduling remain unchanged.
    g_vgm_meta = {};
    g_vgm_meta.samples = tab5::meta_u32(header+0x18);
    g_vgm_meta.loop_samples = tab5::meta_u32(header+0x20);
    memset(current_vgm_title, 0, sizeof(current_vgm_title));
    const uint32_t gd3_offset = tab5::meta_u32(header+0x14);
    if (gd3_offset && uint64_t(gd3_offset)+0x14u+12u <= vgm_file_size) {
        const uint32_t abs_gd3=gd3_offset+0x14u;
        uint8_t tag_head[12] = {};
        auto read_at=[&](uint32_t off,uint8_t* dst,size_t len)->bool{
            if (uint64_t(off)+len>vgm_file_size) return false;
            if (!vgm_is_streaming) {memcpy(dst,vgm_data+off,len);return true;}
            if (vgm_swap_partition) return esp_partition_read(vgm_swap_partition,off,dst,len)==ESP_OK;
            return vgm_stream_file.seek(off) && vgm_stream_file.read(dst,len)==len;
        };
        if(read_at(abs_gd3,tag_head,sizeof(tag_head)) && memcmp(tag_head,"Gd3 ",4)==0) {
            const uint32_t payload_len=tab5::meta_u32(tag_head+8);
            // Avoid stack/heap explosion with malformed tag size. Keep Unicode complete.
            if(payload_len<=2048u && uint64_t(abs_gd3)+12u+payload_len<=vgm_file_size) {
                uint8_t gd3_buffer[12+2048] = {};
                memcpy(gd3_buffer,tag_head,12);
                if(read_at(abs_gd3+12,gd3_buffer+12,payload_len))
                    tab5::parse_gd3(g_vgm_meta,gd3_buffer,12u+payload_len);
            }
        }
    }
    snprintf(current_vgm_title,sizeof(current_vgm_title),"%s",g_vgm_meta.title);

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
    // R41: no stale YM2151 interpolation state may survive across track loads.
    r41_native_init(&r41_ym2151_native, 0, actual_sample_rate);
#if defined(VGMM5_R47_YM2151_FIR25_NATIVE)
    r47_ym2151_fir_reset(&r47_ym2151_fir);  // Prevent FIR history leaking across tracks.
#endif
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

    if (vgm_ym2612_clock != 0 || vgm_ym2151_clock != 0 || vgm_ym2610_clock != 0 || vgm_ym2203_clock != 0 || vgm_ym2608_clock != 0 || vgm_ym3812_clock != 0) {
        uint32_t fm_clock = 0;
        if      (vgm_ym2612_clock != 0) { chip_type = CHIP_YM2612; fm_clock = vgm_ym2612_clock; }
        else if (vgm_ym2608_clock != 0) { chip_type = CHIP_YM2608; fm_clock = vgm_ym2608_clock; }
        else if (vgm_ym2610_clock != 0) { chip_type = CHIP_YM2610; fm_clock = vgm_ym2610_clock; }
        else if (vgm_ym2203_clock != 0) { chip_type = CHIP_YM2203; fm_clock = vgm_ym2203_clock; }
        else if (vgm_ym3812_clock != 0) { chip_type = CHIP_YM3812; fm_clock = vgm_ym3812_clock; }

        else                            { chip_type = CHIP_YM2151; fm_clock = vgm_ym2151_clock; }
        // R41: VGM remains at 44.1kHz; only YM2151 FM core runs at clock/64.
        // Never apply the native rate to OPN/OPLL/PCM/SSG or the I2S ring.
        const uint32_t r41_clock = fm_clock & 0x3FFFFFFFu;
        // R43: user-reported continuous crackle on Tab5 R42 RC1.
        // Temporarily retain the R33-R39 44.1kHz core path for YM2151;
        // preserve the R41 native bridge implementation for future recovery.
        // This is an evidence-driven safety fallback, not proof of underrun.
        // Only the internal YM2151 tick rate changes; VGM/I2S remain 44.1kHz.
#if defined(VGMM5_R47_YM2151_FIR25_NATIVE)
        // R47 coefficients are valid ONLY for 62.5kHz to 44.1kHz.
        // All other clocks/rates deliberately use the R43 safe 44.1kHz path.
        const uint32_t r41_selected_clock = r47_ym2151_select_native_clock(
            r41_clock, actual_sample_rate, chip_type == CHIP_YM2151);
#elif defined(VGMM5_R43_YM2151_SAFE_44100)
        const uint32_t r41_selected_clock = 0u;
#else
        const uint32_t r41_selected_clock = (chip_type == CHIP_YM2151) ? r41_clock : 0u;
#endif
        r41_native_init(&r41_ym2151_native,
                        r41_selected_clock,
                        actual_sample_rate);
        const uint32_t r41_fm_rate = r41_ym2151_native.enabled
                                 ? r41_ym2151_native.native_hz : actual_sample_rate;
        fm_engine_init(&g_fm_engine, r41_fm_rate, fm_clock, chip_type);
        if (chip_type == CHIP_YM2151 && r41_ym2151_native.enabled) {
            // R40 EG control: preserve existing envelope wall-clock speed.
            g_fm_engine.r41_eg_output_hz=actual_sample_rate;
            g_fm_engine.r41_eg_phase=4u * r41_fm_rate - actual_sample_rate;
        }
        if (chip_type == CHIP_YM2151) {
            Serial.printf("[R41][YM2151] clock=%lu synth=%lu output=%lu native=%u\n",
                          (unsigned long)r41_clock, (unsigned long)r41_fm_rate,
                          (unsigned long)actual_sample_rate, (unsigned)r41_ym2151_native.enabled);
        }
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

        }
    }


    // R31: independent YM2413 is not OPL2 and must never enter fm_engine.
    if (vgm_ym2413_clock != 0) {
        if (active_chip_count >= MAX_CHIPS) {
            Serial.println("[R31] chip count limit; refusing YM2413 playback");
            vgm_engine_stop();
            return false;
        }
        Serial.printf("[R31][INIT] clock=%lu rate=%lu smooth=1\n",
                      (unsigned long)(vgm_ym2413_clock & 0x3fffffff),
                      (unsigned long)actual_sample_rate);
        r31_choose_hot_state_memory();
        r31_vgm_bridge_init(g_r31_vgm_ptr, vgm_ym2413_clock & 0x3fffffff,
                            actual_sample_rate, YM2413_SMOOTH);
        g_r31_active = true;
        active_chips[active_chip_count++] = &r31_ym2413_wrapper;
        active_channel_count += 9;
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
        if (vgm_msm6258_clock != 0) pcm_engine_set_msm6258_clock(&g_pcm_engine, vgm_msm6258_clock);
        if (vgm_oki_clock != 0)     pcm_engine_set_oki_clock(&g_pcm_engine, vgm_oki_clock);
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

    // Configuration occurs before generation is notified, never during active audio.
    tab5::g_live_monitor.configure_vgm(vgm_ym2413_clock!=0,vgm_ym2151_clock!=0,
        (vgm_ym2612_clock|vgm_ym2203_clock|vgm_ym2608_clock|vgm_ym2610_clock)!=0,
        (vgm_ay8910_clock|vgm_ym2203_clock|vgm_ym2608_clock|vgm_ym2610_clock)!=0,
        vgm_sn_clock!=0,vgm_scc_clock!=0);

    // Static chip provenance from the actually initialized chips, not guessed
    // notes from the mixed output. Keep this off the synthesis path.
    {
        char* dst=g_vgm_meta.chips;size_t used=0;
        auto add=[&](const char* chip){
            if(used && used<sizeof(g_vgm_meta.chips)-1)used+=(size_t)snprintf(dst+used,sizeof(g_vgm_meta.chips)-used," + ");
            if(used<sizeof(g_vgm_meta.chips)-1)used+=(size_t)snprintf(dst+used,sizeof(g_vgm_meta.chips)-used,"%s",chip);
        };
        if(vgm_ym2413_clock)add("YM2413");
        if(vgm_ym2151_clock)add("YM2151");
        if(vgm_ym2612_clock)add("YM2612");
        if(vgm_ym2203_clock)add("YM2203");
        if(vgm_ym2608_clock)add("YM2608");
        if(vgm_ym2610_clock)add("YM2610");
        if(vgm_ym3812_clock)add("YM3812");
        if(vgm_ay8910_clock || vgm_ym2203_clock || vgm_ym2608_clock || vgm_ym2610_clock)add("SSG");
        if(vgm_sn_clock)add("SN76489");
        if(vgm_scc_clock)add("SCC");
        if(vgm_segapcm_clock || vgm_c140_clock || vgm_c352_clock)add("PCM");
    }

    // ========================================================
    // ★ ミキサー用の除数（チップ数）をグローバルに一度だけ事前計算
    // ========================================================
    global_chip_count = (active_chip_count > 0) ? active_chip_count : 1;
    Serial.printf("[VGM] Mixer: chip_count=%d\n", global_chip_count);

    waitSamples = 8820; vgm_time_acc = 0; ym2612_pcm_offset = 0;
    sn76489_writes = 0;
    // Queue reset belongs to stop/flush, where no speaker or producer owns
    // any slot.  Never reset pointers while AudioPlayTask may be in playRaw.
    if (!vgm_audio_queue_is_flushed()) {
        snprintf(vgm_last_error, sizeof(vgm_last_error), "Audio queue not flushed");
        return false;
    }
    
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
                    esp_partition_munmap(vgm_mmap_handle);
                    vgm_mmap_handle = 0;
                }
                const void* mapped_ptr;
                esp_err_t mmap_err = esp_partition_mmap(vgm_swap_partition, 0, (vgm_swap_offset + 0xFFFF) & ~0xFFFF, ESP_PARTITION_MMAP_DATA, &mapped_ptr, &vgm_mmap_handle);
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
        } else {
            // A streaming VGM cannot retain 0x67 PCM pointers without swap.
            // Refuse explicitly instead of silently playing missing instruments.
            uint32_t scan_ptr = vgmDataStart;
            while (scan_ptr < vgm_file_size) {
                if (!vgm_stream_file.seek(scan_ptr)) break;
                int value = vgm_stream_file.read();
                if (value < 0) break;
                if (value == 0x67) {
                    int marker = vgm_stream_file.read();
                    if (marker == 0x66) {
                        snprintf(vgm_last_error, sizeof(vgm_last_error),
                                 "PCM VGM needs PSRAM (no vgm_swap)");
                        Serial.println("[VGM] Refusing incomplete PCM stream");
                        return false;
                    }
                    if (marker < 0) {
                        snprintf(vgm_last_error, sizeof(vgm_last_error), "Truncated VGM command");
                        return false;
                    }
                }
                if (value == 0x66) break;
                int width = get_cmd_length((uint8_t)value);
                scan_ptr += (width > 0 ? (uint32_t)width : 1u);

            }
        }
    } else {
        // Memory mode (Flash MMAP or PSRAM) registers PCM pointers in-place.
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
                if (!tab5::range_fits(scan_ptr, 5u, vgm_file_size)) {
                    snprintf(vgm_last_error, sizeof(vgm_last_error), "Truncated VGM PCM header");
                    Serial.println("[VGM] FAIL: truncated 0x67 PCM header");
                    return false;
                }
                uint8_t type = vgm_data[scan_ptr++];
                uint32_t size = vgm_data[scan_ptr] | (vgm_data[scan_ptr+1] << 8) | (vgm_data[scan_ptr+2] << 16) | (vgm_data[scan_ptr+3] << 24);
                size &= 0x7FFFFFFF; // ★ これを追加！(VGM 1.71+ の巨大サイズ化を防ぐ)
                scan_ptr += 4;
                // A clipped PCM block would silently produce wrong instruments.
                // Subtraction-based validation also avoids 32-bit overflow.
                if (!tab5::range_fits(scan_ptr, size, vgm_file_size)) {
                    snprintf(vgm_last_error, sizeof(vgm_last_error), "Truncated VGM PCM data");
                    Serial.printf("[VGM] FAIL: PCM block %d exceeds file bounds\n", pcm_block_count);
                    return false;
                }
                // Do not continue at the beginning of an oversized block's payload.
                if (size > 0x1000000u) {
                    snprintf(vgm_last_error, sizeof(vgm_last_error), "VGM PCM block too large");
                    Serial.printf("[VGM] FAIL: PCM block %d exceeds 16MB limit\n", pcm_block_count);
                    return false;
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
#if !defined(VGMM5_TAB5_FORCE_NEUTRAL_EQ)
    eq_lpf_l = 0; eq_lpf_r = 0; eq_bass_l = 0; eq_bass_r = 0;
#endif
    

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

#if defined(VGMM5_R49_SEGMENT_SWITCH)
    bool r49_has_pcm=false;
    for (int i=0; i<active_chip_count; ++i) {
        if (active_chips[i]==&pcm_wrapper) r49_has_pcm=true;
    }
    // Only compare a native 4MHz YM2151+PCM song; all other songs are unchanged.
    r49_segments_init(&s_r49_segments, actual_sample_rate,
        chip_type==CHIP_YM2151 && r41_ym2151_native.enabled &&
        r41_ym2151_native.native_hz==62500u &&
        actual_sample_rate==44100u && r49_has_pcm);
#if defined(VGMM5_R53_STEADY_FULL_AUDIO)
    // R53: Keep R49's exact FM/FIR hot loop, unity FM+PCM gains, and audio
    // generator timing. Reset ONLY the diagnostic-mode timer after each block,
    // so no audio-mode switch or fade occurs even on multi-hour playback.
    // Synth/FIR/EG/PCM history and R41 resampler phase are never reset.
    if (s_r49_segments.enabled)
        Serial.println("[R53][AUDIO] continuous native YM2151 FIR+PCM, no mode switching");
#else
    if (s_r49_segments.enabled)
        Serial.println("[R49][SEGMENT] enabled, 3s per mode: FIR+PCM, LINEAR+PCM, PCM-only, FM-only");
#endif
#endif
    prebuffering = true;
    s_vgm_manual_stop.store(false, std::memory_order_release);
    s_vgm_gen_idle.store(false, std::memory_order_release);
    isPlaying = true;
    xSemaphoreGive(xSemaphore); 
    return true;
}

void vgm_engine_stop() {
    s_vgm_manual_stop.store(true, std::memory_order_release);
    vgm_audio_queue_cancel_tail_drain();
    isPlaying = false;
    // Wait for a real worker acknowledgment before freeing PCM/MMAP resources.
    const uint32_t began = millis();
    while (!vgm_engine_generation_idle() && (uint32_t)(millis() - began) < 2000u) {
        vTaskDelay(1);
    }
    if (!vgm_engine_generation_idle()) {
        // The caller must refuse any new playback while the old producer is alive.
        Serial.println("[R31_OPT3][VGM] STOP TIMEOUT: retaining live PCM/MMAP memory");
        return;
    }
    if (!mdx_engine_is_playing() && !vgm_audio_queue_flush_if_idle()) {
        Serial.println("[R31_OPT4][VGM] Refusing cleanup: audio queue did not drain");
        return;
    }
    g_r31_active=false;
    pcm_engine_free(&g_pcm_engine); 
    if (vgm_mmap_handle) {
        esp_partition_munmap(vgm_mmap_handle);
        vgm_mmap_handle = 0;
        vgm_data = nullptr;
    } else if (vgm_data) {
        free(vgm_data); // may be PSRAM; PCM pointers already released above
        vgm_data = nullptr;
    }
    if (vgm_stream_file) vgm_stream_file.close();
}

static inline uint8_t readByte() {
    if (vgm_ptr >= vgm_file_size) { isPlaying = false; return 0x66; }
    if (vgm_is_streaming) {
        if (vgm_ptr < vgm_stream_buf_start || vgm_ptr >= vgm_stream_buf_start + vgm_stream_buf_len) {
            uint32_t read_len = sizeof(vgm_stream_buf);
            if (vgm_ptr + read_len > vgm_file_size) read_len = vgm_file_size - vgm_ptr;
            
            // ★修正: vgm_swap_partitionからの読み込みを完全削除
            // ストリーミング時は必ずSDカード(vgm_stream_file)からコマンドを読む
            if (!vgm_stream_file.seek(vgm_ptr)) {
                snprintf(vgm_last_error, sizeof(vgm_last_error), "VGM SD seek failed");
                isPlaying = false;
                return 0x66;
            }
            vgm_stream_buf_len = vgm_stream_file.read(vgm_stream_buf, read_len);
            vgm_stream_buf_start = vgm_ptr;
            if (vgm_stream_buf_len == 0) {
                snprintf(vgm_last_error, sizeof(vgm_last_error), "VGM SD read failed");
                isPlaying = false;
                return 0x66;
            }
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
                r31_ym2413_wrapper.write(0, reg, val);
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
                // R13: no UI register observers on the audio core.
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
                else { 
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


void IRAM_ATTR processAudioBlock() {
    if (!isPlaying) return;
    
    int16_t *out_buf = wav_buff[wd];
    // R30: exclude Atom EQ *at compile time* from the Tab5 firmware.
    // Other board builds retain the R17 board-gated Atom path.
#if !defined(VGMM5_TAB5_FORCE_NEUTRAL_EQ)
    const auto board = M5.getBoard();
    const bool atom_eq = board == m5::board_t::board_M5AtomS3 ||
        board == m5::board_t::board_M5AtomVoiceS3R ||
        board == m5::board_t::board_M5AtomS3R;
#endif
    const bool vgm_native_rate = (actual_sample_rate == VGM_SAMPLE_RATE);

    for (int i = 0; i < BUFF_SIZE; i++) {
#if defined(VGMM5_R49_SEGMENT_SWITCH)
        r49_segments_advance(&s_r49_segments);
#endif
        // At Tab5's native VGM 44.1kHz rate, one output frame advances
        // exactly one VGM wait tick. Preserve the old fractional path for
        // other sample rates; eliminate repeated generic division and loop.
        if (vgm_native_rate) {
            if (waitSamples) --waitSamples;
            if (waitSamples == 0 && isPlaying) processVGM();
        } else {
            vgm_time_acc += VGM_SAMPLE_RATE;
            while (vgm_time_acc >= actual_sample_rate && isPlaying) {
                if (waitSamples > 0) {
                    uint32_t consume = vgm_time_acc / actual_sample_rate;
                    if (consume > waitSamples) consume = waitSamples;
                    waitSamples -= consume;
                    vgm_time_acc -= consume * actual_sample_rate;
                }
                if (waitSamples == 0 && isPlaying) {
                    processVGM();
                    if (waitSamples == 0) continue;
                }
            }
        }

        // =====================================
        // ミキサー処理（各チップの出力を合計し、チップ数で割る）
        // =====================================
        int32_t mix_l = 0;
        int32_t mix_r = 0;

        for (int c = 0; c < active_chip_count; c++) {
            int32_t ch_l = 0, ch_r = 0;
            
            active_chips[c]->tick(&ch_l, &ch_r);
#if defined(VGMM5_R49_SEGMENT_SWITCH)
            // Keep synthesizer state, register timing, and FIR history running.
            // Change ONLY how much of each chip reaches the final mix.
            if (s_r49_segments.enabled) {
                const uint16_t gain = (active_chips[c]==&fm_wrapper)
                    ? s_r49_segments.fm_q8 :
                    (active_chips[c]==&pcm_wrapper ? s_r49_segments.pcm_q8 : 256u);
                if (gain!=256u) {
                    ch_l=r49_q8_gain(ch_l,gain);
                    ch_r=r49_q8_gain(ch_r,gain);
                }
            }
#endif
            mix_l += ch_l;
            mix_r += ch_r;
        }

        // グローバルに計算済みのチップ数で割る
        // （各音源の出力が非常に大きく合算でクリップするため、さらに3で割ってヘッドルームを確保し音割れを防ぐ）
        mix_l /= (global_chip_count * 3);
        mix_r /= (global_chip_count * 3);

        int32_t in_l = mix_l;
        int32_t in_r = mix_r;
        
        dc_state_l = (int64_t)(in_l - dc_prev_l) * 16384 + (dc_state_l * 1019) / 1024;
        dc_state_r = (int64_t)(in_r - dc_prev_r) * 16384 + (dc_state_r * 1019) / 1024;
        
        dc_prev_l = in_l;
        dc_prev_r = in_r;
        
        mix_l = (int32_t)(dc_state_l / 16384);
        mix_r = (int32_t)(dc_state_r / 16384);

#if !defined(VGMM5_TAB5_FORCE_NEUTRAL_EQ)
if (atom_eq) {
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
#endif // !VGMM5_TAB5_FORCE_NEUTRAL_EQ

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
#if defined(VGMM5_R53_STEADY_FULL_AUDIO)
    // R49 mode0 is the full-quality FM(FIR)+PCM path. Re-arm the diagnostic
    // segment timer once per output block; never disturb synthesis history.
    // This keeps 256/256 FM, PCM and FIR gains indefinitely, including >27h.
    if (s_r49_segments.enabled) s_r49_segments.frame_in_mode = 0;
#endif
    wav_buff_size[wd] = BUFF_SIZE;
    // R13: optional lossy UI snapshot from the *finished* generated PCM,
    // at generator priority 2, before publishing its ring slot. Never
    // inspect/reuse Speaker-borrowed PCM in the priority-4 consumer.
    tab5::g_spectrum_tap.publish_if_due(wav_buff[wd], BUFF_SIZE);
    // R14: audio producer only, once per eight *completed* 512-frame blocks.
    // This reads existing chip latches and envelope states; it adds no work to
    // the register-write hot path or the higher-priority PCM consumer.
    static unsigned r14_monitor_block=0;
    if(++r14_monitor_block>=8u){
        r14_monitor_block=0;
        if(g_r31_active.load(std::memory_order_relaxed)){
            const ym2413_core& chip=g_r31_vgm_ptr->chip;
            for(unsigned ch=0;ch<YM2413_CHANNELS;++ch)
                tab5::g_live_monitor.opll_write(0x20u+ch,
                    (chip.ch[ch].key_on || chip.ch[ch].rhythm_key)?0x10u:0u);
        }
        const unsigned fm_channels=(g_fm_engine.chip_type==CHIP_YM2151)?8u:
              (g_fm_engine.chip_type==CHIP_YM2203?3u:6u);
        const tab5::LiveMonitor::Chip fm_chip=(g_fm_engine.chip_type==CHIP_YM2151)?
                tab5::LiveMonitor::OPM:tab5::LiveMonitor::OPN;
        for(unsigned ch=0;ch<fm_channels;++ch){
            bool envelope_active=false;
            for(unsigned op=0;op<4;++op){
                const unsigned state=g_fm_engine.ops[ch*4u+op].env_state;
                if(state!=EG_OFF && state!=EG_RELEASE) envelope_active=true;
            }
            tab5::g_live_monitor.observe_activity(fm_chip,ch,envelope_active);
        }
        for(unsigned ch=0;ch<3;++ch){
            const unsigned v=g_ssg_engine.regs[8u+ch];
            const unsigned gate=g_ssg_engine.regs[7];
            const bool enabled=!(gate&(1u<<ch)) || !(gate&(1u<<(ch+3u)));
            tab5::g_live_monitor.observe_activity(tab5::LiveMonitor::AY,ch,
                                      enabled && ((v&0x1fu)!=0u));
        }
        for(unsigned ch=0;ch<4;++ch){
            const unsigned vol=ch<3?g_psg_engine.sn76489.tone[ch].vol:
                                      g_psg_engine.sn76489.noise.vol;
            tab5::g_live_monitor.observe_activity(tab5::LiveMonitor::SN,ch,vol<15u);
        }
        for(unsigned ch=0;ch<5;++ch)
            tab5::g_live_monitor.observe_activity(tab5::LiveMonitor::SCC,ch,
                      (g_scc_engine.enable&(1u<<ch)) && g_scc_engine.vol[ch]!=0);
    }
    wd = (wd + 1) % WAV_BUFF_COUNT;
    vgm_audio_queue_publish();
    // The VGM stream may end inside this very block. The already produced
    // PCM is still queued, so treat it as a natural tail unless STOP won.
    if (!isPlaying.load(std::memory_order_acquire) &&
        !s_vgm_manual_stop.load(std::memory_order_acquire)) {
        vgm_audio_queue_begin_tail_drain();
        // STOP can race the check above; never resurrect tail-drain after STOP.
        if (s_vgm_manual_stop.load(std::memory_order_acquire))
            vgm_audio_queue_cancel_tail_drain();
    }
    
}

// ────────────────────────────────────────────────────────────────────────
// Core 0：波形生成（Speaker/I2S優先度より低く、UIのCore 1から分離）
// ────────────────────────────────────────────────────────────────────────
void audio_generate_task(void *args) {
    while (true) {
        // 再生開始のシグナルを待つ
        xSemaphoreTake(xSemaphore, portMAX_DELAY);
        unsigned urgent_burst = 0;
        // Fixed per-track output-frame budget; no division on the block hot path.
        const uint32_t r42_block_budget_us=(uint32_t)((1000000ULL*BUFF_SIZE)/actual_sample_rate);

        while (isPlaying) {
            // バッファに空きがある場合は音声を生成
            if (wav_count < (WAV_BUFF_COUNT - 4)) {
                // R42: measure *whole-block generation* only for YM2151 native.
                // Does not include Speaker.playRaw(), I2S, or the consumer queue.
                const bool r42_watch=(g_fm_engine.chip_type==CHIP_YM2151 &&
                                      r41_ym2151_native.enabled);
                const int64_t r42_begin_us=r42_watch ? esp_timer_get_time() : 0;
                processAudioBlock();
                if (r42_watch) {
                    const uint32_t elapsed_us=(uint32_t)(esp_timer_get_time()-r42_begin_us);
                    r42_native_blocks.fetch_add(1,std::memory_order_relaxed);
                    r42_atomic_max(r42_native_max_gen_us,elapsed_us);
                    // Block PCM delivery time, not an invented hard real-time deadline:
                    // buffered producer can legitimately overrun one block occasionally.
                    if (elapsed_us>r42_block_budget_us) r42_native_over_budget.fetch_add(1,std::memory_order_relaxed);
                }
                r22_gen_blocks.fetch_add(1,std::memory_order_relaxed);
#if defined(VGMM5_R49_SEGMENT_SWITCH)
                // Log only AFTER the R42 generation stopwatch; never print
                // inside the synthesis loop or include serial I/O in maxGenUs.
#if !defined(VGMM5_R53_STEADY_FULL_AUDIO)
                if (s_r49_segments.enabled && s_r49_segments.changed) {
                    s_r49_segments.changed=0;
                    Serial.printf("[R49][SEGMENT] t=%lus mode=%u %s\n",
                        (unsigned long)(s_r49_segments.total_frames/44100u),
                        (unsigned)s_r49_segments.mode,
                        s_r49_segment_names[s_r49_segments.mode]);
                }
#endif // !VGMM5_R53_STEADY_FULL_AUDIO
#endif
                // R3: a half-empty YM2413 ring needs immediate refill;
                // taskYIELD alone cannot schedule lower-priority UI tasks.
                // Therefore allow at most three consecutive urgent blocks,
                // then delay one tick on the fourth even while underfilled.
                // When healthy, delay each block as in R2. Other chips keep
                // their exact previous scheduling policy.
                if (g_r31_active) {
                    const bool healthy = wav_count.load(std::memory_order_acquire) >= (WAV_BUFF_COUNT / 2);
                    if (healthy || ++urgent_burst >= 4u) {
                        urgent_burst = 0;
                        vTaskDelay(1);
                    } else {
                        taskYIELD();
                    }
                } else {
                    taskYIELD();
                } 
            } else {
                // ★修正点：バッファが満杯の場合は break でループを抜けるのではなく、
                // I2S（スピーカー）が音声を消費してバッファが空くまで待機する
                vTaskDelay(pdMS_TO_TICKS(5)); 
            }
        }
        // This store must happen after processAudioBlock() and all other reads
        // from the active chip backing storage are finished.
        s_vgm_gen_idle.store(true, std::memory_order_release);
    }
}

// ────────────────────────────────────────────────────────────────────────
// Core 0：I2S（DMA）転送 ＆ 減衰検知時のセマフォ発行
// ────────────────────────────────────────────────────────────────────────
void audio_play_task(void *args) {
    while (true) {
        if (!audio_consumer_enter()) { vTaskDelay(1); continue; }
        // Reclaim only buffers for which M5Unified reported the release.
        s_audio_queue.reclaim(wav_count);
        const bool natural_tail = s_audio_tail_drain.load(std::memory_order_acquire);
        const bool any_playing = (isPlaying && !isPaused) || mdx_engine_is_playing() || natural_tail;
        // Short tracks may end before 12 blocks were buffered. Drain them,
        // rather than waiting forever for a producer that has already ended.
        if (prebuffering && (s_audio_queue.pending() >= (WAV_BUFF_COUNT - 4) || natural_tail)) {
            prebuffering = false;
        }
        if (any_playing) {
            r22_min(r22_low_water,(uint32_t)s_audio_queue.pending());
            if (!s_audio_queue.has_pending() && !prebuffering)
                r22_starve.fetch_add(1,std::memory_order_relaxed);
        }
        if (any_playing && s_audio_queue.has_pending() && !prebuffering) {
#if defined(IS_ATOMS3)
            static int16_t mono_buff[512];
            const int frames = wav_buff_size[rd];
            for (int i = 0; i < frames; i++)
                mono_buff[i] = (wav_buff[rd][i * 2] + wav_buff[rd][i * 2 + 1]) / 2;
            s_audio_queue.begin_submit((unsigned)rd);
            bool queued = M5.Speaker.playRaw(mono_buff, frames, actual_sample_rate, false, 1, 0, false);
#else
            s_audio_queue.begin_submit((unsigned)rd);
            bool queued = M5.Speaker.playRaw((const int16_t *)wav_buff[rd],
                             wav_buff_size[rd] * 2, actual_sample_rate, true, 1, 0, false);
#endif
            // R13: Speaker submission is free of GUI sampling/monitoring.
            // The callback may retire and recycle a slot immediately.
            if(queued)r22_play_ok.fetch_add(1,std::memory_order_relaxed);
            else r22_play_fail.fetch_add(1,std::memory_order_relaxed);
            // A release callback can run before playRaw() returns. Move the
            // consumer cursor BEFORE finish_submit() can retire that slot and
            // allow its producer-side reuse.
            const unsigned completed_slot = (unsigned)rd;
            if (queued) rd = (rd + 1) % WAV_BUFF_COUNT;
            s_audio_queue.finish_submit(completed_slot, queued, wav_count);
            if (queued && wav_count <= (WAV_BUFF_COUNT / 2)) xSemaphoreGive(xSemaphore);
            audio_consumer_leave();
            // YM2413 R31: do not add a fixed 8-ms post-submit pause to
            // Speaker.playRaw() backpressure (which may itself wait).
            // 512 frames @ 44100 Hz = 11.61 ms of audio. Keep all other
            // chips and the M5Unified callback-based FIFO ownership unchanged.
            vTaskDelay(queued && g_r31_active.load(std::memory_order_relaxed)
                       ? 1 : (queued ? pdMS_TO_TICKS(8) : 1));
        } else {
            // Natural post-roll completes only once the speaker relinquishes
            // every pointer and the FIFO retirement has returned every slot.
            if (natural_tail && tab5::audio_output_complete(
                true,
                wav_count.load(std::memory_order_acquire),
                s_audio_queue.pending(),
                s_audio_queue.outstanding(),
                M5.Speaker.isPlaying())) {
                vgm_audio_queue_cancel_tail_drain();
            }
            // R31 OPT5: do not enqueue raw PCM when the engine is idle.
            // M5Unified 0.2.26 spk_task itself feeds the output hold level
            // when there is no data; periodic idle playRaw() unnecessarily
            // fills its pointer queue and delays stop/flush on Core 0.
            // A temporary starvation while PLAYING is still observable.
            if (any_playing && !prebuffering) {
                r22_silence.fetch_add(1,std::memory_order_relaxed);
            }
            audio_consumer_leave();
            vTaskDelay(1);
        }
    }
}

static bool s_tab5_audio_ready = false; // R15_AUDIO_STARTUP_GUARD
bool vgm_engine_audio_ready(void) { return s_tab5_audio_ready; }

void vgm_engine_init() {
    s_tab5_audio_ready = false;
    // R30: one diagnostic at startup only. No Serial calls in the audio loop.
    const auto r30_board = M5.getBoard();
    const bool r30_is_tab5 = r30_board == m5::board_t::board_M5Tab5 ||
        r30_board == m5::board_t::board_M5Tab5X;
#if defined(VGMM5_TAB5_FORCE_NEUTRAL_EQ)
    Serial.printf("[R30][AUDIO_EQ] board_id=%d tab5=%d atom_eq=OFF compile_guard=1\n",
                  static_cast<int>(r30_board), static_cast<int>(r30_is_tab5));
#else
    Serial.printf("[R30][AUDIO_EQ] board_id=%d tab5=%d atom_eq=BOARD_GATED compile_guard=0\n",
                  static_cast<int>(r30_board), static_cast<int>(r30_is_tab5));
#endif
    auto spk_cfg = M5.Speaker.config();
#if defined(IS_ATOMS3)
    spk_cfg.sample_rate = 48000;
#else
    spk_cfg.sample_rate = 44100;
#endif
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
    
    // Tab5 X68K cold-start recovery: define a known ES8388/AMP state before
    // starting the ONLY audio session. Never do this during playback.
    // M5.begin() can have already brought up Speaker; restart once here.
    if (M5.getBoard() == m5::board_t::board_M5Tab5 ||
        M5.getBoard() == m5::board_t::board_M5Tab5X) {
        M5.Speaker.stop();
        M5.Speaker.end();
        delay(50);
        Serial.println("[R31][X68K_AUDIO] startup cold reinit: stop/end/50ms/begin");
    }
    M5.Speaker.config(spk_cfg);
    // Register before first playRaw(), as required by M5Unified 0.2.26.
    M5.Speaker.setBufferReleaseCallback(nullptr, tab5_speaker_buffer_released);
    if (!M5.Speaker.begin()) {
        Serial.println("VGMM5_R15 FAIL: M5.Speaker.begin");
        return;
    }
    delay(20); // X68K analog codec settle: before route readback / playback.
    // X68K Tab5-inspired output routing: same M5Unified I2S/ES8388
    // init and dedicated PI4IOE headphone/speaker switching, without any
    // codec reset or Speaker.end() while sample pointers are in flight.
    // Official Tab5 UserDemo: PI4IOE1 input status=0x0F (HP_DET=P7);
    // output latch=0x05 (SPK_EN=P1). Codec DAC OUT1 remains enabled.
    if (M5.getBoard() == m5::board_t::board_M5Tab5 ||
        M5.getBoard() == m5::board_t::board_M5Tab5X) {
        constexpr uint8_t codec = 0x10;
        constexpr uint8_t ioe = 0x43;
        constexpr uint32_t bus_hz = 400000;
        uint8_t hp_status = 0;
        const bool detect_ok = M5.In_I2C.readRegister(ioe, 0x0F, &hp_status, 1, bus_hz);
        const bool inserted = detect_ok && ((hp_status & 0x80) != 0);
        // Do not enable the speaker amp unless unplugged is CONFIRMED.
        // With an ambiguous headphone-detect read, favor silent speaker.
        const bool amp_set = inserted || !detect_ok
            ? M5.In_I2C.bitOff(ioe, 0x05, 0x02, bus_hz)
            : M5.In_I2C.bitOn(ioe, 0x05, 0x02, bus_hz);
        uint8_t amp_latch = 0xFF;
        const bool latch_ok = M5.In_I2C.readRegister(ioe, 0x05, &amp_latch, 1, bus_hz);
        const bool amp_expected_on = detect_ok && !inserted;
        const bool amp_verified = amp_set && latch_ok &&
            (((amp_latch & 0x02) != 0) == amp_expected_on);
        const uint8_t dac_power = M5.In_I2C.readRegister8(codec, 0x04, bus_hz);
        const uint8_t lout1 = M5.In_I2C.readRegister8(codec, 0x2E, bus_hz);
        const uint8_t rout1 = M5.In_I2C.readRegister8(codec, 0x2F, bus_hz);
        Serial.printf("[R31][X68K_AUDIO] hp_det=%s inserted=%d spk_amp=%s verified=%d dac=%02X L1=%02X R1=%02X\n",
                      detect_ok ? "OK" : "I2C_FAIL", int(inserted),
                      amp_expected_on ? "ON" : "OFF", int(amp_verified),
                      dac_power, lout1, rout1);
        // These reads are evidence, NOT proof of an analog waveform.
        // Do not disable audio if the expander readback fails: that makes
        // recording pure circuit noise and obscures the actual failure.
    }

    if (M5.getBoard() == m5::board_t::board_M5Cardputer) {
        M5.Speaker.setVolume(128);
    } else if (M5.getBoard() == m5::board_t::board_M5AtomVoiceS3R || M5.getBoard() == m5::board_t::board_M5AtomS3R) {
        M5.Speaker.setVolume(80); // 48kHz化で音量が安定したため引き上げ
        // アンプ起動時のポップノイズを防ぐため、無音トーンを短く鳴らしてI2Sを安全にアクティブ化
        M5.Speaker.tone(0, 50);
    } else if (M5.getBoard() == m5::board_t::board_M5Tab5 ||
               M5.getBoard() == m5::board_t::board_M5Tab5X) {
        // Browser::volume defaults to 128. Match it, as did the recovered
        // X68K Tab5 build; older 48 was 17dB quieter on M5Unified's
        // squared-volume scale. DAC/PCM samples and chip math unchanged.
        M5.Speaker.setAllChannelVolume(255);
        M5.Speaker.setChannelVolume(0, 255);
        M5.Speaker.setVolume(128);
        Serial.printf("[R31][X68K_AUDIO] volume=%u all-channel=255 sample_rate=%lu\n",
                      static_cast<unsigned>(M5.Speaker.getVolume()), static_cast<unsigned long>(spk_cfg.sample_rate));
    } else {
        M5.Speaker.setVolume(48); // Other boards retain their previous levels.
    }
    
    
    xSemaphore = xSemaphoreCreateBinary();
    if (xSemaphore == nullptr) {
        Serial.println("VGMM5_R15 FAIL: audio semaphore allocation");
        return;
    } 

    // DSP生成タスクをCore 0に移動し、メインループ(Core 1)のM5.update()による遅延から完全に切り離す
    // R7: Core0 / priority2. AudioPlayTask priority4 and Speaker I2S top priority remain above AudioGen.
    BaseType_t gen_created = xTaskCreatePinnedToCore(audio_generate_task, "AudioGenTask", 8192, NULL, 2, &playTaskHandle, 0);
    if (gen_created != pdPASS) {
        Serial.println("VGMM5_R15 FAIL: AudioGenTask create");
        vSemaphoreDelete(xSemaphore);
        xSemaphore = nullptr;
        return;
    }
    
    // 再生タスクはCore 0の高優先度
    BaseType_t play_created = xTaskCreatePinnedToCore(audio_play_task, "AudioPlayTask", 4096, NULL, 4, NULL, 0);
    if (play_created != pdPASS) {
        Serial.println("VGMM5_R15 FAIL: AudioPlayTask create");
        vTaskDelete(playTaskHandle);
        playTaskHandle = nullptr;
        vSemaphoreDelete(xSemaphore);
        xSemaphore = nullptr;
        return;
    }

    isPlaying = false;
    s_tab5_audio_ready = true;
    Serial.println("VGMM5_R15 PASS: speaker and audio tasks initialized");
}

void vgm_engine_service_headphone_route(void) {
    if (!s_tab5_audio_ready) return;
    if (M5.getBoard() != m5::board_t::board_M5Tab5 &&
        M5.getBoard() != m5::board_t::board_M5Tab5X) return;
    static uint32_t last_ms = 0;
    static int last_inserted = -1;
    const uint32_t now = millis();
    if ((uint32_t)(now - last_ms) < 250) return;
    last_ms = now;
    uint8_t input = 0;
    if (!M5.In_I2C.readRegister(0x43, 0x0F, &input, 1, 400000)) return;
    const int inserted = (input & 0x80) ? 1 : 0;
    if (last_inserted == inserted) return;
    // Write only the speaker amplifier gate: no ES8388 reset / I2S restart.
    const bool ok = inserted
        ? M5.In_I2C.bitOff(0x43, 0x05, 0x02, 400000)
        : M5.In_I2C.bitOn(0x43, 0x05, 0x02, 400000);
    if (ok) {
        last_inserted = inserted;
        Serial.printf("[R31][X68K_AUDIO] jack=%s speaker=%s\n",
                      inserted ? "INSERTED" : "REMOVED",
                      inserted ? "MUTED" : "ENABLED");
    }
}

bool vgm_engine_is_playing(void) {
    return isPlaying;
}

void vgm_engine_set_volume(uint8_t vol) {
    M5.Speaker.setVolume(vol);
}

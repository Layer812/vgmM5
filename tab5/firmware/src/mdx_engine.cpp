// mdx_engine.cpp
// MDX/PDXファイル再生エンジン実装
// mdxPC/mdxPC.ino のロジックをvoice統合用に再構成
// FM音源: voice側fm_engine(YM2151モード)
// ADPCM : adpcm_voice_driver (ヒープ上8chPCMミキサー)
//
// 【バッファ共有について】
// 独自の wav_buff は持たない。
// vgm_engine の共有バッファ (wav_buff / rd / wd / wav_count) を extern 参照し、
// vgm_engine の audio_play_task がそのまま MDX 生成データも消費する。
// これにより BSS を約 16KB 削減する。

#include <Arduino.h>
#include <SD.h>
#include <FFat.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include "tab5_psram_bounds.hpp"
#include <atomic>
#include "mdx_engine.hpp"
#include "tab5_track_meta.hpp"
#include "tab5_cp932.hpp"
#include "tab5_live_monitor.hpp"
#include "tab5_spectrum.hpp"
#include "vgm_engine.h"   // 共有バッファ参照

extern "C" {
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mdx.h"
#include "mdx_driver.h"
#include "pdx.h"
#include "adpcm.h"
#include "lzx.h"
#include "mdx_timer.h"
#include "mdx_fm.h"
#include "mdx_adpcm.h"
#include "fm_engine.h"
#include "esp_partition.h"
}

// 共有オーディオバッファ（vgm_engine.cpp側で定義）
extern int32_t *vgm_mix_buf;

// ============================================================
// 内部バッファ（int32 ミックス用のみ — 共有バッファへの変換に使用）
// sizeof = 512 × 4 = 2048 byte (BSS)
// ============================================================
static int32_t s_mix_buf_l[VGM_BUFF_SIZE];
static int32_t s_mix_buf_r[VGM_BUFF_SIZE];

// ============================================================
// MDX/PDXドライバー
// ============================================================
static struct mdx_file            s_mdx_file;
static struct pdx_file            s_pdx_file;
static struct mdx_driver          s_mdx_driver;
static struct mdx_timer           s_timer_driver;
static struct mdx_fm              s_fm_driver;
static struct mdx_adpcm           s_adpcm_driver;

static uint8_t *s_mdx_buffer = NULL;   // MDXファイルデータ（ヒープ）

// PDXデコード済みデータはFlash vgm_swapパーティションにMMAP（SRAMを消費しない）
static esp_partition_mmap_handle_t s_pdx_mmap_handle = 0;
static const void* s_pdx_mmap_ptr = NULL;
static uint8_t* s_pdx_psram = NULL;  // decoded PDX when no vgm_swap partition exists

// PDXロード用ワークバッファ（スタックオーバーフロー防止のためBSS配置）
static uint8_t      s_pdx_readbuf[1024];
static uint8_t      s_pdx_chunk[1024];
static int16_t      s_pdx_decode_buf[2048];
static uint32_t     s_pdx_flash_offsets[96];

static char          s_title[256]    = {0};
static tab5::TrackMeta g_mdx_meta;
const tab5::TrackMeta& mdx_engine_get_metadata(){return g_mdx_meta;}
static char          s_error[128]    = {0};
static std::atomic<bool> s_is_playing{false};
static std::atomic<bool> s_stop_request{false};
static bool          s_use_sd        = true;

static TaskHandle_t s_gen_task_handle = NULL;  // MDX生成タスク (Core 0)

// ============================================================
// 内部ヘルパー
// ============================================================

// PDXファイルの同ディレクトリ検索
static bool find_pdx_file(const char *mdx_path, const char *pdx_name, char *out, int out_len) {
    char dir[256] = "/";
    const char *slash = strrchr(mdx_path, '/');
    if (slash && slash != mdx_path) {
        int dirlen = (int)(slash - mdx_path + 1);
        if (dirlen < 256) { strncpy(dir, mdx_path, dirlen); dir[dirlen] = '\0'; }
    }

    snprintf(out, out_len, "%s%s", dir, pdx_name);
    if (s_use_sd ? SD.exists(out) : FFat.exists(out)) return true;
    snprintf(out, out_len, "%s%s.PDX", dir, pdx_name);
    if (s_use_sd ? SD.exists(out) : FFat.exists(out)) return true;
    snprintf(out, out_len, "%s%s.pdx", dir, pdx_name);
    if (s_use_sd ? SD.exists(out) : FFat.exists(out)) return true;

    out[0] = '\0';
    return false;
}

// PDXファイルのロードとADPCMデコード（Flash vgm_swapパーティションへMMAP）
static bool load_pdx(const char *pdx_path) {
    Serial.printf("[MDX::PDX] Opening: %s\n", pdx_path);
    File f;
    if (s_use_sd) f = SD.open(pdx_path);
    else f = FFat.open(pdx_path);
    if (!f) { Serial.println("[MDX::PDX] FAIL: File open"); return false; }

    int pdxlen = (int)f.size();
    Serial.printf("[MDX::PDX] File size: %d bytes\n", pdxlen);
    if (pdxlen <= 0) { f.close(); return false; }

    if (pdxlen < 768 || !f.seek(0) || f.read(s_pdx_readbuf, 768) != 768) {
        Serial.println("[MDX::PDX] FAIL: truncated PDX header");
        f.close(); return false;
    }

    int sample_count = 0;
    for (int i = 0; i < 96; i++) {
        uint32_t ofs = ((uint32_t)s_pdx_readbuf[i*8]<<24)|((uint32_t)s_pdx_readbuf[i*8+1]<<16)|((uint32_t)s_pdx_readbuf[i*8+2]<<8)|(uint32_t)s_pdx_readbuf[i*8+3];
        uint32_t len = ((uint32_t)s_pdx_readbuf[i*8+4]<<24)|((uint32_t)s_pdx_readbuf[i*8+5]<<16)|((uint32_t)s_pdx_readbuf[i*8+6]<<8)|(uint32_t)s_pdx_readbuf[i*8+7];
        if (len > 0 && !tab5::pdx_sample_range(ofs, len, (uint32_t)pdxlen)) {
            Serial.printf("[MDX::PDX] FAIL: invalid sample %d offset=%lu length=%lu\n",
                          i, (unsigned long)ofs, (unsigned long)len);
            f.close();
            return false;  // Do not substitute silence for a corrupt PDX entry.
        }
        if (len > 0) {
            s_pdx_file.samples[i].ofs = (int)ofs;
            s_pdx_file.samples[i].len = (int)len;
            sample_count++;
        } else {
            s_pdx_file.samples[i].ofs = 0;
            s_pdx_file.samples[i].len = 0;
        }
        s_pdx_file.samples[i].decoded_data = NULL;
        s_pdx_file.samples[i].num_samples  = 0;
    }
    Serial.printf("[MDX::PDX] Parsed %d samples from header\n", sample_count);

    // No-vgm_swap Tab5 layout: decoded PCM lives in PSRAM, never in the
    // unpartitioned flash tail.  FM/ADPCM drivers still receive the same
    // int16_t sample pointers and the same number of decoded samples.
    const esp_partition_t* found_swap = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "vgm_swap");
    if (!found_swap) {
        uint64_t total_decoded = 0;
        for (int i = 0; i < 96; ++i) {
            uint32_t len = (uint32_t)s_pdx_file.samples[i].len;
            if (!tab5::decoded_pdx_fits(total_decoded, len, SIZE_MAX)) {
                Serial.println("[MDX::PDX] FAIL: decoded size overflow");
                f.close(); return false;
            }
            total_decoded += (uint64_t)len * 4u;
        }
        if (total_decoded == 0) { f.close(); return true; }
        uint8_t* ram = static_cast<uint8_t*>(heap_caps_malloc(
            (size_t)total_decoded, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!ram) {
            Serial.printf("[MDX::PDX] FAIL: needs %llu bytes PSRAM\n",
                          (unsigned long long)total_decoded);
            f.close(); return false;
        }
        size_t write_pos = 0;
        bool good = true;
        for (int i = 0; i < 96 && good; ++i) {
            const uint32_t bytes = (uint32_t)s_pdx_file.samples[i].len;
            s_pdx_flash_offsets[i] = (uint32_t)write_pos;
            if (!bytes) continue;
            struct adpcm_status st;
            adpcm_init(&st);
            for (uint32_t chunk_ofs = 0; chunk_ofs < bytes; chunk_ofs += sizeof(s_pdx_chunk)) {
                size_t chunk_len = bytes - chunk_ofs;
                if (chunk_len > sizeof(s_pdx_chunk)) chunk_len = sizeof(s_pdx_chunk);
                if (!f.seek((uint32_t)s_pdx_file.samples[i].ofs + chunk_ofs) ||
                    f.read(s_pdx_chunk, chunk_len) != chunk_len) {
                    good = false;
                    break;
                }
                for (size_t j = 0; j < chunk_len; ++j) {
                    const uint8_t val = s_pdx_chunk[j];
                    s_pdx_decode_buf[j*2]   = adpcm_decode(val >> 4, &st);
                    s_pdx_decode_buf[j*2+1] = adpcm_decode(val & 0x0f, &st);
                }
                memcpy(ram + write_pos, s_pdx_decode_buf, chunk_len * 4u);
                write_pos += chunk_len * 4u;
            }
            if (good) s_pdx_file.samples[i].num_samples = (int)(bytes * 2u);
            vTaskDelay(1);  // bounded decode WDT yield, no timing effect on playback
        }
        f.close();
        if (!good || write_pos != (size_t)total_decoded) {
            free(ram);
            for (int i = 0; i < 96; ++i) {
                s_pdx_file.samples[i].decoded_data = NULL;
                s_pdx_file.samples[i].num_samples = 0;
            }
            Serial.println("[MDX::PDX] FAIL: incomplete PDX read");
            return false;
        }
        s_pdx_psram = ram;
        for (int i = 0; i < 96; ++i) {
            if (s_pdx_file.samples[i].len > 0) {
                s_pdx_file.samples[i].decoded_data =
                    reinterpret_cast<int16_t*>(ram + s_pdx_flash_offsets[i]);
            }
        }
        Serial.printf("[MDX::PDX] PSRAM success: %lu decoded bytes, %d samples\n",
                      (unsigned long)write_pos, sample_count);
        return true;
    }

    uint32_t total_chunks = 0;
    for (int i = 0; i < 96; i++) {
        if (s_pdx_file.samples[i].len > 0) {
            total_chunks += (s_pdx_file.samples[i].len / 1024) + 1;
        }
    }

    uint32_t required_bytes = total_chunks * 4096;

    // ★ VGMと同じ vgm_swap パーティションを再利用
    const esp_partition_t* swap_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "vgm_swap");
    if (!swap_part) {
        Serial.println("[MDX::PDX] FAIL: vgm_swap partition not found!");
        f.close(); return false;
    }
    Serial.printf("[MDX::PDX] vgm_swap partition size: %u bytes\n", swap_part->size);
    if (required_bytes > swap_part->size) {
        Serial.printf("[MDX::PDX] FAIL: PDX size (%u) exceeds vgm_swap (%u)\n", required_bytes, swap_part->size);
        f.close(); return false;
    }

    // 既存MMAPの解除
    if (s_pdx_mmap_handle) {
        Serial.println("[MDX::PDX] Releasing old MMAP");
        esp_partition_munmap(s_pdx_mmap_handle);
        s_pdx_mmap_handle = 0;
        s_pdx_mmap_ptr = NULL;
    }

    // Flash領域の消去
    uint32_t erase_size = (required_bytes + 4095) & ~4095;
    Serial.printf("[MDX::PDX] Erasing %u bytes from Flash...\n", erase_size);
    
    // ★ 消去は非常に重いため、直前と直後に休止を入れてWDTパニックを防ぐ
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_partition_erase_range(swap_part, 0, erase_size);
    vTaskDelay(pdMS_TO_TICKS(10));
    Serial.println("[MDX::PDX] Flash erase complete");

    uint32_t write_offset = 0;

    for (int i = 0; i < 96; i++) {
        if (s_pdx_file.samples[i].len == 0) continue;
        
        s_pdx_flash_offsets[i] = write_offset;
        s_pdx_file.samples[i].num_samples  = s_pdx_file.samples[i].len * 2;

        Serial.printf("[MDX::PDX] Decoding sample %d: len=%d blocks=%d\n", 
                      i, s_pdx_file.samples[i].len, (s_pdx_file.samples[i].len / 1024) + 1);

        struct adpcm_status st;
        adpcm_init(&st);
        int k = (s_pdx_file.samples[i].len / 1024) + 1;
        
        for (int l = 0; l < k; l++) {
            int m = (l == (k-1)) ? (s_pdx_file.samples[i].len % 1024) : 1024;
            if (m == 0) break;
            
            f.seek(s_pdx_file.samples[i].ofs + l * 1024);
            f.read(s_pdx_chunk, m);
            
            int16_t *dst = s_pdx_decode_buf;
            for (int j = 0; j < m; j++) {
                uint8_t c = s_pdx_chunk[j];
                *dst++ = adpcm_decode(c >> 4,   &st);
                *dst++ = adpcm_decode(c & 0x0f, &st);
            }
            
            // Write exact PCM data length (1 ADPCM byte = 2 PCM samples = 4 bytes)
            esp_partition_write(swap_part, write_offset, s_pdx_decode_buf, m * 4);
            write_offset += m * 4;

            // ★ 8ブロック処理するごとに1ms休んでWDTを回避する
            if (l % 8 == 0) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
    }
    f.close();
    Serial.printf("[MDX::PDX] All samples decoded, total write_offset=%u\n", write_offset);

    // ★ デコード済みのデータを直接MMAPする（SRAMを一切消費しない）
    uint32_t mmap_size = (required_bytes + 0xFFFF) & ~0xFFFF;
    Serial.printf("[MDX::PDX] Creating MMAP: %u bytes\n", mmap_size);
    esp_err_t err = esp_partition_mmap(swap_part, 0, mmap_size, ESP_PARTITION_MMAP_DATA, &s_pdx_mmap_ptr, &s_pdx_mmap_handle);
    if (err != ESP_OK) {
        Serial.printf("[MDX::PDX] FAIL: mmap failed: %d\n", err);
        return false;
    }
    Serial.printf("[MDX::PDX] MMAP success: ptr=%p handle=%u\n", s_pdx_mmap_ptr, (unsigned)s_pdx_mmap_handle);

    uint8_t* base_ptr = (uint8_t*)s_pdx_mmap_ptr;
    for (int i = 0; i < 96; i++) {
        if (s_pdx_file.samples[i].len > 0) {
            s_pdx_file.samples[i].decoded_data = (int16_t*)(base_ptr + s_pdx_flash_offsets[i]);
        }
    }

    Serial.printf("[MDX::PDX] SUCCESS: %u chunks loaded to Flash\n", total_chunks);
    return true;
}

// MDXファイルのロード
static bool load_mdx(const char *mdx_path) {
    Serial.printf("[MDX::LOAD] Loading: %s\n", mdx_path);
    if (s_mdx_buffer) { free(s_mdx_buffer); s_mdx_buffer = NULL; }
    if (s_pdx_mmap_handle) {
        esp_partition_munmap(s_pdx_mmap_handle);
        s_pdx_mmap_handle = 0;
        s_pdx_mmap_ptr = NULL;
    }
    if (s_pdx_psram) { free(s_pdx_psram); s_pdx_psram = NULL; }
    memset(&s_mdx_file, 0, sizeof(s_mdx_file));
    memset(&s_pdx_file, 0, sizeof(s_pdx_file));

    File mdx_fd;
    if (s_use_sd) mdx_fd = SD.open(mdx_path);
    else mdx_fd = FFat.open(mdx_path);
    if (!mdx_fd) { snprintf(s_error, sizeof(s_error), "Open failed: %s", mdx_path); Serial.printf("[MDX::LOAD] FAIL: %s\n", s_error); return false; }

    int mdxlen = (int)mdx_fd.size();
    Serial.printf("[MDX::LOAD] MDX file size: %d bytes\n", mdxlen);
    if (mdxlen <= 0 || mdxlen > 0x20000) {
        mdx_fd.close();
        snprintf(s_error, sizeof(s_error), "Invalid MDX size: %d", mdxlen);
        return false;
    }

    uint8_t *mdxdata = (uint8_t *)malloc(mdxlen + 16);
    if (!mdxdata) {
        mdx_fd.close();
        snprintf(s_error, sizeof(s_error), "MDX malloc failed (%d bytes)", mdxlen);
        return false;
    }
    if (!mdx_fd.seek(0) || mdx_fd.read(mdxdata, mdxlen) != (size_t)mdxlen) {
        mdx_fd.close();
        free(mdxdata);
        snprintf(s_error, sizeof(s_error), "Incomplete MDX file read");
        Serial.printf("[MDX::LOAD] FAIL: %s\n", s_error);
        return false;
    }
    mdx_fd.close();
    Serial.println("[MDX::LOAD] MDX file read from SD");

    // LZX解凍を試みる
    uint32_t dec_size = 0;
    uint8_t *dec_data = LZXDecode(mdxdata, mdxlen, &dec_size);
    if (dec_data) {
        Serial.printf("[MDX::LOAD] LZX decompressed: %d -> %u bytes\n", mdxlen, dec_size);
        free(mdxdata); mdxdata = dec_data; mdxlen = (int)dec_size;
    } else {
        Serial.println("[MDX::LOAD] No LZX decompression (raw MDX)");
    }
    s_mdx_buffer = mdxdata;

    int err = mdx_file_load(&s_mdx_file, mdxdata, mdxlen);
    if (err != 0) { snprintf(s_error, sizeof(s_error), "MDX parse error: %d", err); Serial.printf("[MDX::LOAD] FAIL: %s\n", s_error); return false; }
    Serial.printf("[MDX::LOAD] MDX parsed OK: title_len=%d, tracks=%d, pdx_len=%d\n",
                  s_mdx_file.title_len, s_mdx_file.num_tracks, s_mdx_file.pdx_filename_len);

    int tlen = (s_mdx_file.title_len < 255) ? s_mdx_file.title_len : 255;
    if (tlen > 0) memcpy(s_title, s_mdx_file.title, tlen);
    s_title[tlen] = '\0';
    const std::string utf8_title=tab5::cp932_to_utf8(s_title,(size_t)tlen);
    tab5::meta_copy(g_mdx_meta.title,sizeof(g_mdx_meta.title),utf8_title);
    g_mdx_meta.tracks=(unsigned)s_mdx_file.num_tracks;
    snprintf(g_mdx_meta.chips,sizeof(g_mdx_meta.chips),"YM2151 / PCM8");
    Serial.printf("[MDX::LOAD] Title: %s\n", s_title);

    g_mdx_meta.pdx_requested = (s_mdx_file.pdx_filename_len>0 && s_mdx_file.pdx_filename[0]!='\0');
    if (s_mdx_file.pdx_filename_len > 0 && s_mdx_file.pdx_filename[0] != '\0') {
        char pdxname[256] = {0};
        int namelen = s_mdx_file.pdx_filename_len < 255 ? s_mdx_file.pdx_filename_len : 255;
        memcpy(pdxname, s_mdx_file.pdx_filename, namelen);
        pdxname[namelen] = '\0';

        char pdxpath[256] = {0};
        if (find_pdx_file(mdx_path, pdxname, pdxpath, sizeof(pdxpath))) {
            Serial.printf("[MDX::LOAD] Found PDX: %s\n", pdxpath);
            if (!load_pdx(pdxpath)) {
                snprintf(s_error, sizeof(s_error), "PDX load failed (PSRAM or file)");
                Serial.printf("[MDX::LOAD] %s - refusing incomplete playback\n", s_error);
                free(s_mdx_buffer);
                s_mdx_buffer = NULL;
                s_mdx_file.data = NULL;
                return false; // do not silently omit the sample instruments
            } else {g_mdx_meta.pdx_loaded=true;
                unsigned n=0;
                for(unsigned k=0;k<96;++k) if(s_pdx_file.samples[k].num_samples>0)++n;
                g_mdx_meta.pdx_samples=n;}
        } else {
            snprintf(s_error, sizeof(s_error), "PDX missing: %.100s", pdxname);
            Serial.printf("[MDX::LOAD] %s - refusing incomplete playback\n", s_error);
            free(s_mdx_buffer);
            s_mdx_buffer = NULL;
            s_mdx_file.data = NULL;
            return false;
        }
    } else {
        Serial.println("[MDX::LOAD] No PDX filename in MDX header");
    }

    Serial.println("[MDX::LOAD] Calling mdx_driver_load...");
    mdx_driver_load(&s_mdx_driver, &s_mdx_file, &s_pdx_file);
    Serial.printf("[MDX::LOAD] SUCCESS: %s  tracks=%d  free_heap=%u\n",
                  s_title, s_mdx_file.num_tracks, esp_get_free_heap_size());
    return true;
}

// ============================================================
// MDX生成タスク（Core 0）
// vgm_engine の共有バッファ (wav_buff/rd/wd/wav_count) に書き込む。
// 再生 (M5.Speaker.playRaw) は vgm_engine の audio_play_task が担う。
// ============================================================
static std::atomic<uint32_t> r22_mdx_blocks{0};
void mdx_engine_diag_print(void){
 Serial.printf("[R22][MDX] gen=%lu maxGenUs=OFF\n",
 (unsigned long)r22_mdx_blocks.exchange(0));
}
static int s_gen_loop_count = 0;  // デバッグ用：生成ループ回数をカウント
static void mdx_gen_task(void *args) {
    Serial.println("[MDX::TASK] Generation task started");
    while (true) {
        // 再生リクエストを待つ
        Serial.println("[MDX::TASK] Waiting for play notification...");
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        Serial.println("[MDX::TASK] Notification received, starting generation loop");

        s_gen_loop_count = 0;
        while (!s_mdx_driver.ended && !s_stop_request) {
            // 共有バッファに空きがあるまで待機
            if (wav_count >= (VGM_WAV_BUFF_COUNT - 4)) {
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }

            // R13: audio generation contains no per-track UI monitor writes.
            memset(s_mix_buf_l, 0, VGM_BUFF_SIZE * sizeof(int32_t));
            memset(s_mix_buf_r, 0, VGM_BUFF_SIZE * sizeof(int32_t));

            int remaining = VGM_BUFF_SIZE;
            int pos       = 0;
            while (remaining > 0) {
                int timer_est = mdx_timer_estimate(&s_timer_driver, remaining);
                int samples   = timer_est;
                if (samples <= 0) samples = 1;

                for (int i = 0; i < samples; i++) {
                    int32_t fm_l = 0, fm_r = 0;
                    fm_engine_tick(&g_fm_engine, &fm_l, &fm_r);

                    int32_t adpcm_l = 0, adpcm_r = 0;
                    mdx_adpcm_tick(&s_adpcm_driver, &adpcm_l, &adpcm_r);

                    s_mix_buf_l[pos + i] = fm_l + adpcm_l;
                    s_mix_buf_r[pos + i] = fm_r + adpcm_r;
                }

                remaining -= samples;
                pos += samples;

                if (mdx_timer_advance(&s_timer_driver, samples)) {
                    if (s_mdx_driver.ended) {
                        break;
                    }
                }
            }

            static int64_t dc_state_l = 0;
            static int32_t dc_prev_l = 0;
            static int64_t dc_state_r = 0;
            static int32_t dc_prev_r = 0;

            int16_t *out_buf = wav_buff[wd];
            for (int i = 0; i < VGM_BUFF_SIZE; i++) {
                int32_t raw_in_l = s_mix_buf_l[i];
                int32_t raw_in_r = s_mix_buf_r[i];

                dc_state_l = (int64_t)(raw_in_l - dc_prev_l) * 16384 + (dc_state_l * 1019) / 1024;
                dc_prev_l = raw_in_l;
                int32_t centered_l = (int32_t)(dc_state_l / 16384);

                dc_state_r = (int64_t)(raw_in_r - dc_prev_r) * 16384 + (dc_state_r * 1019) / 1024;
                dc_prev_r = raw_in_r;
                int32_t centered_r = (int32_t)(dc_state_r / 16384);

                int32_t vl = centered_l / 4; 
                if (vl >  32767) vl =  32767;
                if (vl < -32768) vl = -32768;

                int32_t vr = centered_r / 4; 
                if (vr >  32767) vr =  32767;
                if (vr < -32768) vr = -32768;

                out_buf[i * 2]     = (int16_t)vl; // L
                out_buf[i * 2 + 1] = (int16_t)vr; // R
            }
            wav_buff_size[wd] = VGM_BUFF_SIZE;
            // R13: real MDX PCM snapshot at generator priority, before publish.
            tab5::g_spectrum_tap.publish_if_due(wav_buff[wd], VGM_BUFF_SIZE);
            // R14: one MDX monitor snapshot per ~93 ms at the completed
            // block boundary. No per-note/per-tick telemetry in the FM loop.
            static unsigned r14_mdx_monitor_block=0;
            if(++r14_mdx_monitor_block>=8u){
                r14_mdx_monitor_block=0;
                for(unsigned ch=0;ch<16u;++ch){
                    const auto& t=s_mdx_driver.tracks[ch];
                    const bool on=t.used && !t.ended && !t.waiting &&
                                  t.note>=0 && t.key_on_delay_counter==0 &&
                                  t.staccato_counter>0;
                    tab5::g_live_monitor.mdx_track(ch,t.used!=0,on,on?t.note:-1);
                }
            }
            wd = (wd + 1) % VGM_WAV_BUFF_COUNT;
            vgm_audio_queue_publish();
            r22_mdx_blocks.fetch_add(1,std::memory_order_relaxed);

            s_gen_loop_count++;
            if (s_gen_loop_count % 100 == 0) {
                Serial.printf("[MDX::TASK] Gen loops: %d  ended=%d  stop=%d  heap=%u\n",
                              s_gen_loop_count, s_mdx_driver.ended, s_stop_request.load(std::memory_order_acquire) ? 1 : 0, esp_get_free_heap_size());
            }

            vTaskDelay(pdMS_TO_TICKS(1));
        }

        const bool natural_mdx_eof = s_mdx_driver.ended && !s_stop_request;
        // OPT3: do not acknowledge stop until the file and its mapped samples
        // are no longer referenced by this worker.
        Serial.printf("[MDX::TASK] Playback ended after %d loops. ended=%d  heap=%u\n",
                      s_gen_loop_count, s_mdx_driver.ended, esp_get_free_heap_size());

        // mdx_driver_free already frees s_mdx_file.data (same pointer as s_mdx_buffer)
        mdx_driver_free(&s_mdx_driver);
        s_mdx_buffer = NULL;
        if (s_pdx_mmap_handle) {
            esp_partition_munmap(s_pdx_mmap_handle);
            s_pdx_mmap_handle = 0;
            s_pdx_mmap_ptr = NULL;
        }
        if (s_pdx_psram) {
            free(s_pdx_psram);
            s_pdx_psram = NULL;
        }
        if (natural_mdx_eof) vgm_audio_queue_begin_tail_drain();
        s_stop_request = false;
        s_is_playing = false; // stop completion acknowledgment, after cleanup
        Serial.println("[MDX::TASK] Cleanup done, waiting for next notification");
    }
}

// ============================================================
// 公開API
// ============================================================

void mdx_engine_init(void) {
    BaseType_t created = xTaskCreatePinnedToCore(
        mdx_gen_task, "mdx_gen_task",
        4096,          // スタック (VGM生成タスクと同等)
        NULL, 2,       // 優先度 2 (audio_generate_task と同じ)
        &s_gen_task_handle, 0 // Core 0
    );
    if (created != pdPASS || s_gen_task_handle == NULL) {
        s_gen_task_handle = NULL;
        Serial.println("[R31_OPT3][MDX] FAIL: generator task allocation");
    }
}

bool mdx_engine_play(const char *mdx_path, bool use_sd) {
    if (s_gen_task_handle == NULL) {
        snprintf(s_error, sizeof(s_error), "MDX generator not initialized");
        return false;
    }
    s_use_sd = use_sd;
    Serial.printf("[MDX::PLAY] Requested: %s\n", mdx_path);
    Serial.printf("[MDX::PLAY] vgm_playing=%d  mdx_playing=%d\n", vgm_engine_is_playing(), s_is_playing.load(std::memory_order_acquire) ? 1 : 0);
    
    // ★ 排他制御: 共有バッファ(wav_buff)をVGMエンジンと共有しているため
    //   両エンジンの同時動作を防ぎ、メモリ破壊・クラッシュを防止する
    
    // VGMエンジンが再生中なら確実に停止
    if (vgm_engine_is_playing()) {
        Serial.println("[MDX::PLAY] Stopping VGM engine first");
        vgm_engine_stop();
        if (!vgm_engine_generation_idle()) {
            snprintf(s_error, sizeof(s_error), "VGM generator still active");
            return false;
        }
    }
    
    // A VGM which reached EOF may still be inside its final generation block.
    if (!vgm_engine_generation_idle()) {
        snprintf(s_error, sizeof(s_error), "Previous VGM generator still active");
        return false;
    }
    // MDXエンジン自身も再生中なら停止
    if (s_is_playing) {
        Serial.println("[MDX::PLAY] Stopping existing MDX playback");
        mdx_engine_stop();
        if (s_is_playing) {
            snprintf(s_error, sizeof(s_error), "MDX stop timeout; reload refused");
            return false;
        }
    }
    
    s_error[0] = '\0';
    s_title[0] = '\0';
    g_mdx_meta = {};

    Serial.printf("[MDX::PLAY] Init drivers (sample_rate=%d)\n", actual_sample_rate);
    mdx_timer_init(&s_timer_driver, actual_sample_rate);
    mdx_adpcm_init(&s_adpcm_driver, actual_sample_rate);
    mdx_fm_init(&s_fm_driver, actual_sample_rate);

    mdx_driver_init(
        &s_mdx_driver,
        &s_timer_driver,
        &s_fm_driver,
        &s_adpcm_driver
    );
    s_mdx_driver.max_loops = 2;

    if (!load_mdx(mdx_path)) {
        Serial.printf("[MDX::PLAY] Load failed: %s\n", s_error);
        return false;
    }

    // Speaker_Class may still own the previous song's ring payload;
    // reset only after both publishers are idle and the speaker has stopped.
    if (!vgm_audio_queue_flush_if_idle()) {
        snprintf(s_error, sizeof(s_error), "Audio queue flush failed");
        Serial.println("[R31_OPT4][MDX] Refusing start: audio queue busy");
        return false;
    }

    tab5::g_live_monitor.configure_mdx((unsigned)s_mdx_file.num_tracks);
    s_stop_request = false;
    s_is_playing   = true;

    // MDX生成タスクに通知
    Serial.println("[MDX::PLAY] Notifying generation task");
    xTaskNotifyGive(s_gen_task_handle);
    Serial.println("[MDX::PLAY] Return true, playback started");
    return true;
}

void mdx_engine_stop(void) {
    vgm_audio_queue_cancel_tail_drain();
    if (!s_is_playing) return;
    s_stop_request = true;
    // The worker clears s_is_playing only AFTER its cleanup; never force an
    // acknowledgement on timeout or risk use-after-free on the next MDX load.
    for (int i = 0; i < 200 && s_is_playing; i++) vTaskDelay(pdMS_TO_TICKS(10));
    if (s_is_playing) {
        Serial.println("[R31_OPT3][MDX] STOP TIMEOUT: old playback still active");
        return;
    }
    // EOF cleanup can publish a tail marker while STOP waits for its ack.
    // Cancel again after the acknowledgment to prevent a stale post-roll.
    vgm_audio_queue_cancel_tail_drain();
    if (!vgm_audio_queue_flush_if_idle()) {
        Serial.println("[R31_OPT4][MDX] STOP: audio queue flush failed");
    }
}

bool mdx_engine_is_playing(void) {
    return s_is_playing;
}

const char *mdx_engine_get_title(void) {
    if (s_title[0] == '\0') return NULL;
    return s_title;
}

const char *mdx_engine_get_error(void) {
    return s_error;
}

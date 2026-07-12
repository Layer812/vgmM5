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
#include <M5Unified.h>
#include "mdx_engine.hpp"
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
static int32_t s_mix_buf[VGM_BUFF_SIZE];

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
static spi_flash_mmap_handle_t s_pdx_mmap_handle = 0;
static const void* s_pdx_mmap_ptr = NULL;

// PDXロード用ワークバッファ（スタックオーバーフロー防止のためBSS配置）
static uint8_t      s_pdx_readbuf[1024];
static uint8_t      s_pdx_chunk[1024];
static int16_t      s_pdx_decode_buf[2048];
static uint32_t     s_pdx_flash_offsets[96];

static char          s_title[256]    = {0};
static char          s_error[128]    = {0};
static volatile bool s_is_playing    = false;
static volatile bool s_stop_request  = false;

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
    if (SD.exists(out)) return true;
    snprintf(out, out_len, "%s%s.PDX", dir, pdx_name);
    if (SD.exists(out)) return true;
    snprintf(out, out_len, "%s%s.pdx", dir, pdx_name);
    if (SD.exists(out)) return true;

    out[0] = '\0';
    return false;
}

// PDXファイルのロードとADPCMデコード（Flash vgm_swapパーティションへMMAP）
static bool load_pdx(const char *pdx_path) {
    Serial.printf("[MDX::PDX] Opening: %s\n", pdx_path);
    File f = SD.open(pdx_path);
    if (!f) { Serial.println("[MDX::PDX] FAIL: SD.open"); return false; }

    int pdxlen = (int)f.size();
    Serial.printf("[MDX::PDX] File size: %d bytes\n", pdxlen);
    if (pdxlen <= 0) { f.close(); return false; }

    f.seek(0);
    f.read(s_pdx_readbuf, 1024);

    int sample_count = 0;
    for (int i = 0; i < 96; i++) {
        int ofs = ((int)s_pdx_readbuf[i*8]<<24)|((int)s_pdx_readbuf[i*8+1]<<16)|((int)s_pdx_readbuf[i*8+2]<<8)|(int)s_pdx_readbuf[i*8+3];
        int len = ((int)s_pdx_readbuf[i*8+4]<<24)|((int)s_pdx_readbuf[i*8+5]<<16)|((int)s_pdx_readbuf[i*8+6]<<8)|(int)s_pdx_readbuf[i*8+7];
        if (len > 0 && ofs >= 0 && ofs < pdxlen) {
            if (ofs + len > pdxlen) {
                len = pdxlen - ofs;
            }
            s_pdx_file.samples[i].ofs = ofs;
            s_pdx_file.samples[i].len = len;
            sample_count++;
        } else {
            s_pdx_file.samples[i].ofs = 0;
            s_pdx_file.samples[i].len = 0;
        }
        s_pdx_file.samples[i].decoded_data = NULL;
        s_pdx_file.samples[i].num_samples  = 0;
    }
    Serial.printf("[MDX::PDX] Parsed %d samples from header\n", sample_count);

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
        spi_flash_munmap(s_pdx_mmap_handle);
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
    esp_err_t err = esp_partition_mmap(swap_part, 0, mmap_size, SPI_FLASH_MMAP_DATA, &s_pdx_mmap_ptr, &s_pdx_mmap_handle);
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
        spi_flash_munmap(s_pdx_mmap_handle);
        s_pdx_mmap_handle = 0;
        s_pdx_mmap_ptr = NULL;
    }
    memset(&s_mdx_file, 0, sizeof(s_mdx_file));
    memset(&s_pdx_file, 0, sizeof(s_pdx_file));

    File mdx_fd = SD.open(mdx_path);
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
    mdx_fd.seek(0);
    mdx_fd.read(mdxdata, mdxlen);
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
    Serial.printf("[MDX::LOAD] Title: %s\n", s_title);

    if (s_mdx_file.pdx_filename_len > 0 && s_mdx_file.pdx_filename[0] != '\0') {
        char pdxname[256] = {0};
        int namelen = s_mdx_file.pdx_filename_len < 255 ? s_mdx_file.pdx_filename_len : 255;
        memcpy(pdxname, s_mdx_file.pdx_filename, namelen);
        pdxname[namelen] = '\0';

        char pdxpath[256] = {0};
        if (find_pdx_file(mdx_path, pdxname, pdxpath, sizeof(pdxpath))) {
            Serial.printf("[MDX::LOAD] Found PDX: %s\n", pdxpath);
            if (!load_pdx(pdxpath)) Serial.println("[MDX::LOAD] PDX load failed (continuing without ADPCM)");
        } else {
            Serial.printf("[MDX::LOAD] PDX not found: %s\n", pdxname);
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

            memset(s_mix_buf, 0, VGM_BUFF_SIZE * sizeof(int32_t));

            int remaining = VGM_BUFF_SIZE;
            int pos       = 0;
            while (remaining > 0) {
            int timer_est = mdx_timer_estimate(&s_timer_driver, remaining);
            int samples   = timer_est;
            if (samples <= 0) samples = 1;

            // Generate ADPCM and FM mixed buffer
            mdx_adpcm_run(&s_adpcm_driver, &s_mix_buf[pos], samples);
            mdx_fm_run(&s_fm_driver, &s_mix_buf[pos], samples);

            remaining -= samples;
            pos += samples;

            if (mdx_timer_advance(&s_timer_driver, samples)) {
                if (s_mdx_driver.ended) {
                    break;
                }
            }
        }

	    static int64_t dc_state = 0;
            static int32_t dc_prev = 0;

            int16_t *out_buf = wav_buff[wd];
            for (int i = 0; i < VGM_BUFF_SIZE; i++) {
                int32_t raw_in = s_mix_buf[i];

                // ★追加: DCブロッカー（FM音源特有の波形の偏りを除去）
                dc_state = (int64_t)(raw_in - dc_prev) * 16384 + (dc_state * 1019) / 1024;
                dc_prev = raw_in;
                int32_t centered = (int32_t)(dc_state / 16384);

                // ★修正: 複数チャンネル合算時のハードクリップを防ぐため、ゲインを 1/4 (25%) 程度に下げる
                // （元の 80/100 では8和音時に上限を遥かに超えて波形が潰れていました）
                int32_t v = centered / 4; 

                if (v >  32767) v =  32767;
                if (v < -32768) v = -32768;
                out_buf[i * 2]     = (int16_t)v; // L
                out_buf[i * 2 + 1] = (int16_t)v; // R
            }
            wav_buff_size[wd] = VGM_BUFF_SIZE;
            wd = (wd + 1) % VGM_WAV_BUFF_COUNT;
            wav_count++;

            s_gen_loop_count++;
            if (s_gen_loop_count % 100 == 0) {
                Serial.printf("[MDX::TASK] Gen loops: %d  ended=%d  stop=%d  heap=%u\n",
                              s_gen_loop_count, s_mdx_driver.ended, s_stop_request, esp_get_free_heap_size());
            }

            vTaskDelay(pdMS_TO_TICKS(1));
        }

        // 再生終了
        s_is_playing   = false;
        s_stop_request = false;
        Serial.printf("[MDX::TASK] Playback ended after %d loops. ended=%d  heap=%u\n",
                      s_gen_loop_count, s_mdx_driver.ended, esp_get_free_heap_size());

        // mdx_driver_free already frees s_mdx_file.data (same pointer as s_mdx_buffer)
        mdx_driver_free(&s_mdx_driver);
        s_mdx_buffer = NULL;
        if (s_pdx_mmap_handle) {
            spi_flash_munmap(s_pdx_mmap_handle);
            s_pdx_mmap_handle = 0;
            s_pdx_mmap_ptr = NULL;
        }
        Serial.println("[MDX::TASK] Cleanup done, waiting for next notification");
    }
}

// ============================================================
// 公開API
// ============================================================

void mdx_engine_init(void) {
    xTaskCreatePinnedToCore(
        mdx_gen_task, "mdx_gen_task",
        4096,          // スタック (VGM生成タスクと同等)
        NULL, 2,       // 優先度 2 (audio_generate_task と同じ)
        &s_gen_task_handle, 0 // Core 0
    );
}

bool mdx_engine_play(const char *mdx_path) {
    Serial.printf("[MDX::PLAY] Requested: %s\n", mdx_path);
    Serial.printf("[MDX::PLAY] vgm_playing=%d  mdx_playing=%d\n", vgm_engine_is_playing(), s_is_playing);
    
    // ★ 排他制御: 共有バッファ(wav_buff)をVGMエンジンと共有しているため
    //   両エンジンの同時動作を防ぎ、メモリ破壊・クラッシュを防止する
    
    // VGMエンジンが再生中なら確実に停止
    if (vgm_engine_is_playing()) {
        Serial.println("[MDX::PLAY] Stopping VGM engine first");
        vgm_engine_stop();
    }
    
    // MDXエンジン自身も再生中なら停止
    if (s_is_playing) {
        Serial.println("[MDX::PLAY] Stopping existing MDX playback");
        mdx_engine_stop();
        delay(200);
    }
    
    // タスクが安全に停止するまで猶予を与える
    delay(50);
    
    s_error[0] = '\0';
    s_title[0] = '\0';

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

    // 共有バッファリセット（VGM未再生状態でも audio_play_task は動いているので安全）
    Serial.println("[MDX::PLAY] Resetting shared buffers");
    wav_count = 0;
    rd = wd = 0;

    s_stop_request = false;
    s_is_playing   = true;

    // MDX生成タスクに通知
    Serial.println("[MDX::PLAY] Notifying generation task");
    xTaskNotifyGive(s_gen_task_handle);
    Serial.println("[MDX::PLAY] Return true, playback started");
    return true;
}

void mdx_engine_stop(void) {
    if (!s_is_playing) return;
    s_stop_request = true;
    // タスクが終了するまで最大500ms待つ
    for (int i = 0; i < 50 && s_is_playing; i++) delay(10);
    s_is_playing = false;
    wav_count = 0;
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

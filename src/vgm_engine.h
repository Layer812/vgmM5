#pragma once
#include <Arduino.h>

void vgm_engine_init(void);
bool vgm_engine_play(const char* filepath, bool use_sd);
void vgm_engine_stop(void);
void vgm_engine_toggle_pause(void);
bool vgm_engine_is_playing(void);
void vgm_engine_set_volume(uint8_t vol);
const char* vgm_engine_get_title(void);
const char* vgm_engine_get_error(void);

// ─────────────────────────────────────────
// 共有オーディオバッファ (mdx_engine から参照)
// vgm_engine.cpp / audio_play_task が消費する
// ─────────────────────────────────────────
#define VGM_WAV_BUFF_COUNT 16
#define VGM_BUFF_SIZE      512

#ifdef __cplusplus
#include <atomic>
extern int16_t           wav_buff[VGM_WAV_BUFF_COUNT][VGM_BUFF_SIZE * 2];
extern size_t            wav_buff_size[VGM_WAV_BUFF_COUNT];
extern volatile int      rd;
extern volatile int      wd;
extern std::atomic<int>  wav_count;
extern uint32_t          actual_sample_rate;


#endif

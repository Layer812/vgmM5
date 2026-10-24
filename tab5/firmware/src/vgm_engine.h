#pragma once
#include <Arduino.h>
#include "tab5_track_meta.hpp"
const tab5::TrackMeta& vgm_engine_get_metadata();

void vgm_engine_init(void);
void vgm_engine_diag_print(void);
bool vgm_engine_audio_ready(void);
// X68K Tab5-style HP_DET / SPK_EN routing, safe on UI task only.
void vgm_engine_service_headphone_route(void);
bool vgm_engine_play(const char* filepath, bool use_sd);
void vgm_engine_stop(void);
// OPT4 shared queue: publish ONLY after a full 512-frame slot is written.
void vgm_audio_queue_publish(void);
// Natural VGM/MDX EOF post-roll; cancel on explicit stop.
void vgm_audio_queue_begin_tail_drain(void);
void vgm_audio_queue_cancel_tail_drain(void);
// Safe only after VGM/MDX generators are idle; fences audio playback and
// stops the Speaker queue before resetting pointers.
bool vgm_audio_queue_flush_if_idle(void);
bool vgm_audio_queue_is_flushed(void);
// True only after the VGM generation worker has exited its active loop.
bool vgm_engine_generation_idle(void);
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

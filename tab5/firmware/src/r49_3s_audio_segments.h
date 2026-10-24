#pragma once
// R49: 3-second controlled audible comparison; no FM/PCM state reset while playing.
// Source PCM remains 44,100 Hz, 512 frames/block. No dynamic allocation.
// MODE 0: FIR FM + PCM; MODE 1: LINEAR FM + PCM;
// MODE 2: PCM ONLY (FM muted); MODE 3: FIR FM ONLY (PCM muted).
// FM and PCM engines always advance; switches affect the mix only.
#include <stdint.h>

typedef struct {
    uint32_t frames_per_mode, frame_in_mode;
    uint64_t total_frames;
    uint16_t fm_q8, pcm_q8, fir_q8; // unity=256
    uint8_t enabled, mode, changed;
} R49AudioSegments;

static inline void r49_segments_init(R49AudioSegments *s, uint32_t hz, int enabled) {
    s->frames_per_mode=hz*3u;
    s->frame_in_mode=0;
    s->total_frames=0;
    s->fm_q8=s->pcm_q8=s->fir_q8=256u;
    s->enabled=(enabled && hz > 0u && hz <= 100000u) ? 1u : 0u;
    s->mode=0u;
    s->changed=0u;
}

static inline uint16_t r49_slew(uint16_t current, uint16_t target) {
    if(current<target) return current+1u;
    if(current>target) return current-1u;
    return current;
}

// Called only from the one audio generator, once for each output PCM frame.
// A 256-sample (~5.8ms at 44.1kHz) crossfade prevents switch clicks.
static inline void r49_segments_advance(R49AudioSegments *s) {
    if(!s->enabled) return;
    if(s->frame_in_mode >= s->frames_per_mode) {
        s->mode=(uint8_t)((s->mode+1u)&3u);
        s->frame_in_mode=0;
        s->changed=1u;
    }
    const uint16_t want_fm=(s->mode==2u)?0u:256u;
    const uint16_t want_pcm=(s->mode==3u)?0u:256u;
    const uint16_t want_fir=(s->mode==1u)?0u:256u;
    s->fm_q8=r49_slew(s->fm_q8,want_fm);
    s->pcm_q8=r49_slew(s->pcm_q8,want_pcm);
    s->fir_q8=r49_slew(s->fir_q8,want_fir);
    s->frame_in_mode++;
    s->total_frames++;
}

static inline int32_t r49_q8_gain(int32_t sample, uint16_t gain) {
    if(gain==256u) return sample;
    if(gain==0u) return 0;
    return (int32_t)(((int64_t)sample*gain)/256);
}

static inline int32_t r49_q8_blend(int32_t raw, int32_t filtered, uint16_t gain) {
    if(gain==256u) return filtered;
    if(gain==0u) return raw;
    const int64_t sum=(int64_t)raw+((((int64_t)filtered-(int64_t)raw)*gain)/256);
    return (sum>INT32_MAX) ? INT32_MAX : ((sum<INT32_MIN) ? INT32_MIN : (int32_t)sum);
}

#include <string.h>
#include <stdio.h>
#include "mdx_adpcm.h"

const int mdx_adpcm_freqtbl[MDX_ADPCM_FREQ_COUNT] = {3900, 5200, 7800, 10400, 15600};

static uint32_t calc_volume(uint8_t vol) {
    const uint8_t vol_00_0f[] = {
        0x6B, 0x6F, 0x71, 0x74, 0x76, 0x79, 0x7B, 0x7D,
        0x80, 0x82, 0x84, 0x87, 0x8A, 0x8C, 0x8F, 0x91,
    };
    const uint32_t vol_40_a0[] = {
        5, 6, 6, 7, 7, 8, 9, 10, 10, 11, 12, 14, 15, 16, 18, 20, 21,
        23, 25, 29, 31, 33, 37, 41, 46, 50, 54, 60, 66, 72, 80, 89,
        97, 107, 117, 130, 142, 156, 173, 189, 205, 226, 246, 267,
        308, 328, 369, 410, 431, 492, 533, 594, 656, 717, 799, 861,
        963, 1045, 1147, 1270, 1393, 1536, 1700, 1864, 2048, 2253,
        2479, 2724, 2991, 3298, 3625, 3994, 4383, 4834, 5325, 5837,
        6431, 7087, 7783, 8561, 9442, 10363, 11387, 12555, 13824,
        15217, 16733, 18371, 20255, 22221, 24454, 26932, 29696,
        32768, 36127, 39732, 43541
    };
    if (vol <= 15)
        return vol_40_a0[vol_00_0f[vol] - 0x40];
    if (vol >= 0x40 && vol <= 0xa0)
        return vol_40_a0[vol - 0x40];
    return 0;
}

static void channel_init(struct mdx_adpcm_channel *ch) {
    memset(ch, 0, sizeof(*ch));
    ch->volume = calc_volume(15);
}

static int32_t channel_get_sample(struct mdx_adpcm_channel *ch) {
    if (ch->data_len == 0 || ch->fin) return 0;

    int32_t sample = (int32_t)ch->chdata[ch->data_pos];

    ch->cnt += mdx_adpcm_freqtbl[ch->freq_num];
    if (ch->cnt >= 44100) {
        ch->cnt -= 44100;
        ch->data_pos++;
        if (ch->data_pos >= ch->data_len) {
            ch->data_pos = 0;
            ch->fin = 1;
        }
    }

    sample = sample * 16;
    sample = (int32_t)ch->volume * sample / 32768;
    sample = sample * 4; 
    if (sample >  32767) sample =  32767;
    if (sample < -32767) sample = -32767;
    return sample;
}

int mdx_adpcm_play(struct mdx_adpcm *driver, uint8_t channel, short *data, int len, uint8_t freq, uint8_t vol, uint8_t slot) {
    if (channel >= 8) return -1;
    struct mdx_adpcm_channel *ch = &driver->channels[channel];
    ch->chdata   = data;
    ch->data_len = len;
    ch->data_pos = 0;
    ch->fin      = 0;
    ch->freq_num = freq < MDX_ADPCM_FREQ_COUNT ? freq : 2;
    ch->skip     = 23220;
    ch->cnt      = 0;
    ch->slot     = slot;
    ch->volume   = calc_volume(vol);
    return 0;
}

int mdx_adpcm_stop(struct mdx_adpcm *driver, uint8_t channel) {
    if (channel >= 8) return -1;
    struct mdx_adpcm_channel *ch = &driver->channels[channel];
    ch->chdata   = NULL;
    ch->data_len = 0;
    ch->data_pos = 0;
    ch->fin      = 0;
    ch->cnt      = 0;
    return 0;
}

int mdx_adpcm_set_volume(struct mdx_adpcm *driver, uint8_t channel, uint8_t vol) {
    if (channel >= 8) return -1;
    driver->channels[channel].volume = calc_volume(vol);
    return 0;
}

int mdx_adpcm_set_freq(struct mdx_adpcm *driver, uint8_t channel, uint8_t freq) {
    if (channel >= 8) return -1;
    struct mdx_adpcm_channel *ch = &driver->channels[channel];
    ch->freq_num = freq < MDX_ADPCM_FREQ_COUNT ? freq : 2;
    ch->skip     = 23220;
    return 0;
}

int mdx_adpcm_set_pan(struct mdx_adpcm *driver, uint8_t pan) {
    driver->pan = pan & 0x03;
    return 0;
}

int mdx_adpcm_init(struct mdx_adpcm *driver, int sample_rate) {
    (void)sample_rate;
    driver->pan = 3;
    for (int i = 0; i < 8; i++) {
        channel_init(&driver->channels[i]);
    }
    return 0;
}

void mdx_adpcm_deinit(struct mdx_adpcm *driver) {
    for (int i = 0; i < 8; i++) {
        mdx_adpcm_stop(driver, i);
    }
}

int mdx_adpcm_estimate(struct mdx_adpcm *driver, int buf_size) {
    (void)driver;
    return buf_size;
}

void mdx_adpcm_run(struct mdx_adpcm *driver, int32_t *out_buf, int buf_size) {
    for (int j = 0; j < 8; j++) {
        struct mdx_adpcm_channel *ch = &driver->channels[j];
        if (ch->data_len == 0 || ch->fin) continue;
        for (int k = 0; k < buf_size; k++) {
            out_buf[k] += channel_get_sample(ch);
        }
    }
}

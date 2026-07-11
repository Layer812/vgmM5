// adpcm_voice_driver.c
// MDX用adpcm_driverインターフェースのvoice側実装
// adpcm_pcm_mix_driver.cの代替。ヒープ上のデコード済みPCMを8ch合成する。
// PCMエンジンの周波数変換ロジックをpcm_engine.cのADPCMと同仕様で実装。

#include <string.h>
#include <stdio.h>
#include "adpcm_voice_driver.h"

// ADPCM周波数テーブル (Hz) mdxPC/adpcm_pcm_mix_driver.hより
const int adpcm_voice_freqtbl[ADPCM_VOICE_FREQ_COUNT] = {3900, 5200, 7800, 10400, 15600};

// mdxPCのadpcm_mixer_calc_volに準拠した音量テーブル
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

// チャンネル初期化
static void channel_init(struct adpcm_voice_channel *ch) {
    memset(ch, 0, sizeof(*ch));
    ch->volume = calc_volume(15);
}

// チャンネルのサンプル1個を取得（周波数変換付き）
// X68000 ADPCMのサンプリング周波数(3.9/5.2/7.8/10.4/15.6kHz)へダウンサンプリング
// アルゴリズム: 加算型accumulatorで間引き。毎呼出しに44100*100を加算し、
// accumulatorがskip閾値を超えたら1サンプル読み出す。
static int32_t channel_get_sample(struct adpcm_voice_channel *ch) {
    if (ch->data_len == 0 || ch->fin) return 0;

    int32_t sample = (int32_t)ch->chdata[ch->data_pos];

    ch->cnt += adpcm_voice_freqtbl[ch->freq_num];
    if (ch->cnt >= 44100) {
        ch->cnt -= 44100;
        ch->data_pos++;
        if (ch->data_pos >= ch->data_len) {
            ch->data_pos = 0;
            ch->fin = 1;
        }
    }

    // 12-bit PCM to 16-bit PCM
    sample = sample * 16;
    // 音量適用（32768基準）
    sample = (int32_t)ch->volume * sample / 32768;
    // 全体的にADPCMの音量が小さすぎるため+12dBブースト
    sample = sample * 4; 
    if (sample >  32767) sample =  32767;
    if (sample < -32767) sample = -32767;
    return sample;
}

// ---- adpcm_driver コールバック群 ----

static int voice_play(struct adpcm_driver *d, uint8_t channel, short *data, int len, uint8_t freq, uint8_t vol, uint8_t slot) {
    struct adpcm_voice_driver *vd = (struct adpcm_voice_driver *)d;
    if (channel >= 8) return -1;
    struct adpcm_voice_channel *ch = &vd->channels[channel];
    ch->chdata   = data;
    ch->data_len = len;
    ch->data_pos = 0;
    ch->fin      = 0;
    ch->freq_num = freq < ADPCM_VOICE_FREQ_COUNT ? freq : 2;
//    ch->skip     = 44100 * 100 / (adpcm_voice_freqtbl[ch->freq_num] + 1); //duty hack
    ch->skip = 23220;
    ch->cnt      = 0;
    ch->slot     = slot;
    ch->volume   = calc_volume(vol);
    
    return 0;
}

static int voice_stop(struct adpcm_driver *d, uint8_t channel) {
    struct adpcm_voice_driver *vd = (struct adpcm_voice_driver *)d;
    if (channel >= 8) return -1;
    struct adpcm_voice_channel *ch = &vd->channels[channel];
    ch->chdata   = NULL;
    ch->data_len = 0;
    ch->data_pos = 0;
    ch->fin      = 0;
    ch->cnt      = 0;
    return 0;
}

static int voice_set_volume(struct adpcm_driver *d, uint8_t channel, uint8_t vol) {
    struct adpcm_voice_driver *vd = (struct adpcm_voice_driver *)d;
    if (channel >= 8) return -1;
    vd->channels[channel].volume = calc_volume(vol);
    return 0;
}

static int voice_set_freq(struct adpcm_driver *d, uint8_t channel, uint8_t freq) {
    struct adpcm_voice_driver *vd = (struct adpcm_voice_driver *)d;
    if (channel >= 8) return -1;
    struct adpcm_voice_channel *ch = &vd->channels[channel];
    ch->freq_num = freq < ADPCM_VOICE_FREQ_COUNT ? freq : 2;
//    ch->skip     = 44100 * 100 / (adpcm_voice_freqtbl[ch->freq_num] + 1); //duty hack
    ch->skip = 23220;
    return 0;
}

static int voice_set_pan(struct adpcm_driver *d, uint8_t pan) {
    struct adpcm_voice_driver *vd = (struct adpcm_voice_driver *)d;
    vd->base.pan = pan & 0x03;
    return 0;
}

// ---- 公開関数 ----

int adpcm_voice_driver_init(struct adpcm_voice_driver *driver, int sample_rate) {
    (void)sample_rate;
    // adpcm_driver基底を初期化
    adpcm_driver_init(&driver->base);

    // コールバックをvoice実装に差し替え
    driver->base.play       = voice_play;
    driver->base.stop       = voice_stop;
    driver->base.set_volume = voice_set_volume;
    driver->base.set_freq   = voice_set_freq;
    driver->base.set_pan    = voice_set_pan;

    for (int i = 0; i < 8; i++) {
        channel_init(&driver->channels[i]);
    }
    return 0;
}

void adpcm_voice_driver_deinit(struct adpcm_voice_driver *driver) {
    for (int i = 0; i < 8; i++) {
        voice_stop(&driver->base, i);
    }
}

int adpcm_voice_driver_estimate(struct adpcm_voice_driver *driver, int buf_size) {
    (void)driver;
    return buf_size;
}

// 全チャンネルのPCMをミックスしてout_bufに加算する
void adpcm_voice_driver_run(struct adpcm_voice_driver *driver, int32_t *out_buf, int buf_size) {
    for (int j = 0; j < 8; j++) {
        struct adpcm_voice_channel *ch = &driver->channels[j];
        if (ch->data_len == 0 || ch->fin) continue;
        for (int k = 0; k < buf_size; k++) {
            out_buf[k] += channel_get_sample(ch);
        }
    }
}

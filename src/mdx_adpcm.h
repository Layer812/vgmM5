#ifndef MDX_ADPCM_H_
#define MDX_ADPCM_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MDX_ADPCM_FREQ_COUNT 5
extern const int mdx_adpcm_freqtbl[MDX_ADPCM_FREQ_COUNT];

struct mdx_adpcm_channel {
    int16_t *chdata;
    int      data_len;
    int      data_pos;
    uint32_t volume;
    uint8_t  freq_num;
    int      skip;
    int      cnt;
    int      fin;
    uint8_t  slot;
};

struct mdx_adpcm {
    struct mdx_adpcm_channel channels[8];
    uint8_t pan;
};

int  mdx_adpcm_init(struct mdx_adpcm *driver, int sample_rate);
void mdx_adpcm_deinit(struct mdx_adpcm *driver);
int  mdx_adpcm_estimate(struct mdx_adpcm *driver, int buf_size);
void mdx_adpcm_run(struct mdx_adpcm *driver, int32_t *out_buf, int buf_size);

int mdx_adpcm_play(struct mdx_adpcm *driver, uint8_t channel, short *data, int len, uint8_t freq, uint8_t vol, uint8_t slot);
int mdx_adpcm_stop(struct mdx_adpcm *driver, uint8_t channel);
int mdx_adpcm_set_volume(struct mdx_adpcm *driver, uint8_t channel, uint8_t vol);
int mdx_adpcm_set_freq(struct mdx_adpcm *driver, uint8_t channel, uint8_t freq);
int mdx_adpcm_set_pan(struct mdx_adpcm *driver, uint8_t pan);

#ifdef __cplusplus
}
#endif

#endif /* MDX_ADPCM_H_ */

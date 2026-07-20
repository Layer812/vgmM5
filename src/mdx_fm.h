#ifndef MDX_FM_H_
#define MDX_FM_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mdx_fm {
    int dummy;
};

void mdx_fm_init(struct mdx_fm *driver, uint32_t sample_rate);
void mdx_fm_write(struct mdx_fm *driver, uint8_t reg, uint8_t val);
void mdx_fm_reset_key_sync(struct mdx_fm *driver, int channel);
void mdx_fm_set_pms_ams(struct mdx_fm *driver, int channel, uint8_t pms_ams);
void mdx_fm_set_pitch(struct mdx_fm *driver, int channel, int pitch);
void mdx_fm_set_tl(struct mdx_fm *driver, int channel, uint8_t tl, uint8_t *v);
void mdx_fm_note_on(struct mdx_fm *driver, int channel, uint8_t op_mask, uint8_t *v);
void mdx_fm_note_off(struct mdx_fm *driver, int channel);
void mdx_fm_write_opm_reg(struct mdx_fm *driver, uint8_t reg, uint8_t data);
void mdx_fm_set_pan(struct mdx_fm *driver, int channel, uint8_t pan, uint8_t *v);
void mdx_fm_set_noise_freq(struct mdx_fm *driver, int channel, int freq);
void mdx_fm_load_voice(struct mdx_fm *driver, int channel, uint8_t *v, int opm_volume, int pan);
void mdx_fm_load_lfo(struct mdx_fm *driver, int channel, uint8_t wave, uint8_t freq, uint8_t pmd, uint8_t amd);


#ifdef __cplusplus
}
#endif

#endif /* MDX_FM_H_ */

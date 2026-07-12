#include "mdx_fm.h"
#include "fm_engine.h"

extern FMSoundEngine g_fm_engine;

static int mdx_note_to_opm(int note) {
	uint8_t tbl[] = {
		0x0, // D#
		0x1, // E
		0x2, // F
		0x4, // F#
		0x5, // G
		0x6, // G#
		0x8, // A
		0x9, // A#
		0xA, // B
		0xC, // C
		0xD, // C#
		0xE, // D
	};
	return (note / 12) * 16 + tbl[note % 12];
}

void mdx_fm_write(struct mdx_fm *driver, uint8_t reg, uint8_t val) {
    (void)driver;
    fm_engine_register_write(&g_fm_engine, (uint16_t)reg, val);
}

void mdx_fm_reset_key_sync(struct mdx_fm *driver, int channel) {
	mdx_fm_write(driver, 0x01, 0x02);
	mdx_fm_write(driver, 0x01, 0x00);
}

void mdx_fm_set_pms_ams(struct mdx_fm *driver, int channel, uint8_t pms_ams) {
	mdx_fm_write(driver, 0x38 + channel, pms_ams);
}

void mdx_fm_set_pitch(struct mdx_fm *driver, int channel, int pitch) {
	mdx_fm_write(driver, 0x28 + channel, mdx_note_to_opm(pitch >> 14));
	mdx_fm_write(driver, 0x30 + channel, (pitch >> 6) & 0xfc);
}

void mdx_fm_set_tl(struct mdx_fm *driver, int channel, uint8_t tl, uint8_t *v) {
	const uint8_t con_masks[8] = {
		0x08, 0x08, 0x08, 0x08, 0x0c, 0x0e, 0x0e, 0x0f,
	};
	int mask = 1;
	for(int i = 0; i < 4; i++, mask <<= 1) {
		int op_vol = v[7 + i];
		if((con_masks[v[1] & 0x07] & mask) > 0) {
			int vol = tl + op_vol;
			if(vol > 0x7f) vol = 0x7f;
			mdx_fm_write(driver, 0x60 + i * 8 + channel, vol); // TL
		} else {
			mdx_fm_write(driver, 0x60 + i * 8 + channel, op_vol); // TL
		}
	}
}

void mdx_fm_note_on(struct mdx_fm *driver, int channel, uint8_t op_mask, uint8_t *v) {
	mdx_fm_write(driver, 0x08, ((op_mask & 0x0f) << 3) | (channel & 0x07)); // Key On
}

void mdx_fm_note_off(struct mdx_fm *driver, int channel) {
	mdx_fm_write(driver, 0x08, channel & 0x07);
}

void mdx_fm_write_opm_reg(struct mdx_fm *driver, uint8_t reg, uint8_t data) {
	mdx_fm_write(driver, reg, data);
}

void mdx_fm_set_pan(struct mdx_fm *driver, int channel, uint8_t pan, uint8_t *v) {
	mdx_fm_write(driver, 0x20 + channel, (pan << 6) | v[1]); // PAN, FL, CON
}

void mdx_fm_set_noise_freq(struct mdx_fm *driver, int channel, int freq) {
	mdx_fm_write(driver, 0x0F, freq & 0x1f);
}

void mdx_fm_load_voice(struct mdx_fm *driver, int channel, uint8_t *v, int opm_volume, int pan) {
	for(int i = 0; i < 4; i++)
		mdx_fm_write(driver, 0x40 + i * 8 + channel, v[ 3 + i]); // DT1, MUL
	mdx_fm_set_tl(driver, channel, opm_volume, v);
	for(int i = 0; i < 4; i++)
		mdx_fm_write(driver, 0x80 + i * 8 + channel, v[11 + i]); // KS, AR
	for(int i = 0; i < 4; i++)
		mdx_fm_write(driver, 0xa0 + i * 8 + channel, v[15 + i]); // AME, D1R
	for(int i = 0; i < 4; i++)
		mdx_fm_write(driver, 0xc0 + i * 8 + channel, v[19 + i]); // DT2, D2R
	for(int i = 0; i < 4; i++)
		mdx_fm_write(driver, 0xe0 + i * 8 + channel, v[23 + i]); // D1L, RR
	mdx_fm_write(driver,  0x20 + channel, (pan << 6) | v[1]); // PAN, FL, CON
}

void mdx_fm_load_lfo(struct mdx_fm *driver, int channel, uint8_t wave, uint8_t freq, uint8_t pmd, uint8_t amd) {
	mdx_fm_write(driver, 0x19, 0x00); // YM2151: LFO Amplitude Modul. Depth
	mdx_fm_write(driver, 0x1b, wave & 0x03); // YM2151: LFO Wave Select
	mdx_fm_write(driver, 0x18, freq); // YM2151: LFO Frequency
	if(pmd & 0x7f) mdx_fm_write(driver, 0x19, pmd); // YM2151: LFO Phase Modul. Depth
	if(amd) mdx_fm_write(driver, 0x19, amd); // YM2151: LFO Amplitude Modul. Depth
}

void mdx_fm_init(struct mdx_fm *driver, uint32_t sample_rate) {
    fm_engine_init(&g_fm_engine, sample_rate, 4000000, 0); // YM2151 at 4MHz
	// reset registers
	for(int i = 0; i < 0x60; i++)
		mdx_fm_write(driver, i, 0x00);

	for(int i = 0x60; i < 0x80; i++)
		mdx_fm_write(driver, i, 0x7f);

	for(int i = 0x80; i < 0xe0; i++)
		mdx_fm_write(driver, i, 0x00);

	for(int i = 0xe0; i <= 0xff; i++)
		mdx_fm_write(driver, i, 0x0f);

	// Key off
	for(int i = 0; i < 8; i++)
		mdx_fm_write(driver, 0x08, i);
}

void mdx_fm_run(struct mdx_fm *driver, int32_t *out_buf, int num_samples) {
    (void)driver;
    for (int i = 0; i < num_samples; i++) {
        int32_t l = 0, r = 0;
        fm_engine_tick(&g_fm_engine, &l, &r);
        out_buf[i] += (l + r) >> 1;
    }
}

#ifndef FM_OPM_VOICE_DRIVER_H_
#define FM_OPM_VOICE_DRIVER_H_

// MDX用FMドライバーのインターフェースをvoice側のfm_engineに橋渡しするアダプター
// mdxPCのym2151.c + fm_opm_emu_driver.c の代替として機能する

#include "fm_opm_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

// voice側のfm_engineを使用するOPMドライバー
struct fm_opm_voice_driver {
    struct fm_opm_driver opm_driver; // 基底ドライバー（fm_opm_driver）
    int sample_rate;
};

void fm_opm_voice_driver_init(struct fm_opm_voice_driver *driver, int sample_rate);
void fm_opm_voice_driver_deinit(struct fm_opm_voice_driver *driver);
int  fm_opm_voice_driver_estimate(struct fm_opm_voice_driver *driver, int num_samples);
void fm_opm_voice_driver_run(struct fm_opm_voice_driver *driver, int32_t *out_buf, int num_samples);

#ifdef __cplusplus
}
#endif

#endif /* FM_OPM_VOICE_DRIVER_H_ */

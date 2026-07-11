// fm_opm_voice_driver.c
// MDX用fm_driverインターフェースをvoice側のfm_engine(YM2151モード)に橋渡しするアダプター
// mdxPCのym2151.c + fm_opm_emu_driver.c の代替

#include "fm_opm_voice_driver.h"
#include "fm_engine.h"  // voice側FMエンジン

// voice側のグローバルエンジンを参照
extern FMSoundEngine g_fm_engine;

// fm_opm_driverからのレジスタ書き込みをvoice fm_engineに転送する
static void voice_opm_write(struct fm_opm_driver *driver, uint8_t reg, uint8_t val) {
    (void)driver;
    fm_engine_register_write(&g_fm_engine, (uint16_t)reg, val);
}

void fm_opm_voice_driver_init(struct fm_opm_voice_driver *driver, int sample_rate) {
    driver->sample_rate = sample_rate;

    // voice側fm_engineをYM2151(OPM)モードで初期化
    fm_engine_init(&g_fm_engine, (uint32_t)sample_rate, 4000000, CHIP_YM2151);

    // fm_opm_driverのwriteポインタをvoice側に差し替え
    driver->opm_driver.write = voice_opm_write;

    // OPMドライバー（レジスタ書き込みロジック）初期化（note_on/off等のコールバックを設定）
    fm_opm_driver_init(&driver->opm_driver);
}

void fm_opm_voice_driver_deinit(struct fm_opm_voice_driver *driver) {
    (void)driver;
    // voice側fm_engineは共有リソースなので、ここでは何もしない
}

int fm_opm_voice_driver_estimate(struct fm_opm_voice_driver *driver, int num_samples) {
    (void)driver;
    // OPMは毎サンプル同期なので推定値=そのまま
    return num_samples;
}

// voice fm_engineからサンプルを生成してミックスバッファに加算する
void fm_opm_voice_driver_run(struct fm_opm_voice_driver *driver, int32_t *out_buf, int num_samples) {
    (void)driver;
    for (int i = 0; i < num_samples; i++) {
        int32_t l = 0, r = 0;
        fm_engine_tick(&g_fm_engine, &l, &r);
        // モノラルにミックスして加算（mdxPC同様にモノラル出力）
        out_buf[i] += (l + r) >> 1;
    }
}

#ifndef ADPCM_VOICE_DRIVER_H_
#define ADPCM_VOICE_DRIVER_H_

// MDX用adpcm_driverインターフェースをvoice側pcm_engineに橋渡しするアダプター
// mdxPCのadpcm_pcm_mix_driver.cの代替として機能する
// PDXデコード済みint16 PCMを8チャンネルでヒープに保持し再生するPCMミキサー

#include "adpcm_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

// ADPCMサンプル周波数テーブル (Hz) - mdxPCより
#define ADPCM_VOICE_FREQ_COUNT 5
extern const int adpcm_voice_freqtbl[ADPCM_VOICE_FREQ_COUNT];

// 1チャンネル分の状態
struct adpcm_voice_channel {
    int16_t *chdata;    // デコード済みPCMデータへのポインタ（外部管理）
    int      data_len;  // サンプル数
    int      data_pos;  // 現在再生位置
    uint32_t volume;    // 音量 (0-32768)
    uint8_t  freq_num;  // 周波数インデックス (0-4)
    int      skip;      // レート変換: 44100×100/(freqtbl[n]+1)
    int      cnt;       // レート変換カウンタ
    int      fin;       // 再生終了フラグ
    uint8_t  slot;      // デバッグ用スロット番号
};

// adpcm_driverを継承したvoiceアダプター
struct adpcm_voice_driver {
    struct adpcm_driver base;                    // 基底ドライバー（先頭に置くことでキャストが有効）
    struct adpcm_voice_channel channels[8];      // 8チャンネル分
};

int  adpcm_voice_driver_init(struct adpcm_voice_driver *driver, int sample_rate);
void adpcm_voice_driver_deinit(struct adpcm_voice_driver *driver);
int  adpcm_voice_driver_estimate(struct adpcm_voice_driver *driver, int buf_size);
void adpcm_voice_driver_run(struct adpcm_voice_driver *driver, int32_t *out_buf, int buf_size);

#ifdef __cplusplus
}
#endif

#endif /* ADPCM_VOICE_DRIVER_H_ */

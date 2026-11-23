# vgmM5 NuttX MP3/FLAC decoder R1 — SOURCE ONLY / NO FLASH

対象: ESP32-P4 Tab5 / NuttX `/home/ito/pyxel-tab5-nuttx-smp-r2` または通常 `/home/ito/pyxel-tab5-nuttx` の**既存音声パイプライン**。

## 実装済み
- NuttXで使える POSIX `open/read/lseek/close` だけを使い、MP3/FLACを逐次デコード。
- `minimp3` (CC0) と `dr_flac` (Public Domain/MIT-0) を固定。microSDのファイルを全量RAMに展開しない。
- モノラル/ステレオをPCM 16bitステレオへ正規化し、既存Pyxel/NuttX音声レートに合わせ **48,000Hz** で512フレーム単位のコールバック。
- MP3: 8〜48kHz、FLAC: 8〜96kHz。別レートは整数線形補間でリサンプリング。
- `vgmm5_decode_file(path,48000,sink,stop,opaque,&stats)` の単一同期関数。
- `sink` が持つのは **呼び出し中だけ有効な一時PCMポインタ**。I2S DMAへ渡す場合は既存DMAの所有権契約に従い**必ずコピーするかDMA消費完了まで待つ**必要あり。
- NO FLASH / NO SD WRITE / NO NVS。既存 `pxaudio_module.c`、ES8388、I2S、FIX29のI2S連続転送修正を変更しない。

## 未実装（重要）
このソースは**デコーダー単体**です。現行のNuttX側 `pxaudio_module.c` 実ファイルをこちらで取得できていないため、その内部I2Sミキサーへのコールバック接続は未実装です。よって、このZIPを展開するだけではTab5で音は出ません。Arduino/ESP-IDF版vgmM5 R68もそのままRAM起動できません。既存音声経路の入口を推測して書き換えることはしません。

## 接続の形（例：既存48kHz PCMミキサーへの橋渡し）

```c
// pxaudio_module.c など、既存オーディオワーカー側の実装が必要。
// ここに示す pxaudio_submit_pcm_block は説明用の仮名であり、
// 実在するNuttX APIではない。
static int sink(void *ctx, const int16_t *stereo, size_t frames, uint32_t hz) {
    return existing_audio_pcm_queue_copy(ctx, stereo, frames, hz);
}
vgmm5_decode_file("/sd/music/song.flac", 48000, sink, should_stop, ctx, &stats);
```

既存のI2S送信処理に直接割り込まず、その手前のPCM供給に接続すること。並行にPyxel PSGと再生する場合はミキサーの設計と排他・音量管理が必要。

## Desktop host検証（FlashやSDへの操作なし）

```sh
gcc -std=gnu11 -O2 -I. vgmm5_nuttx_codec.c test_codec.c -o vgmm5_test
./vgmm5_test some.mp3 song.raw
./vgmm5_test some.flac song.raw
```

`song.raw` は48kHz/16bit/ステレオ（LE）PCM、プレイヤー再生済みの証拠ではなくデコーダー出力。
GitHub Actionsが別途MP3/FLACの生成サンプルをデコードしてフレーム数・レート・非無音を検証します。

## ソースを既存NuttXツリーへ入れる場合

R1 ZIPの `vgmm5_nuttx_codec.c`・`.h`・`vendor/` を、既存の `pxaudio_module.c` と同じディレクトリに**コピーするだけ**で、NuttX側ソースの上書きは行いません。PCMミキサーの入口が分かるまで、Makefileやビルド済みNuttXを変更しないでください。

R68本体および参照PR: https://github.com/Layer812/vgmM5/pull/72

### ライセンス
`minimp3.h`: CC0 / public domain dedication, https://github.com/lieff/minimp3
`dr_flac.h`: public domain or MIT-0, https://github.com/mackron/dr_libs
`vgmm5_nuttx_codec.[ch]` と `test_codec.c`: MIT-0。

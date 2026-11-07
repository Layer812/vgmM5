# vgmM5 — M5Stack CoreS3 R67

[日本語](#日本語) / [English](#english)

## 日本語

M5Stack CoreS3（ESP32-S3）向けのVGM/VGZ・MDXプレイヤーです。microSDからの楽曲再生、タッチ操作、チャンネル・スペクトラム表示に対応します。内蔵スピーカーとM5Stack Module Audio（ES8388）に対応しています。

### インストール

- **Web Burner（R67初期版）**：[ブラウザから書き込み](https://burner.m5stack.com/share/firmware/P8DPPV)
- **M5Burner**：シェアコード `wEBeKwOKFsqAmlsu`
- **ソースコード**：[CoreS3 Production（main）](https://github.com/Layer812/vgmM5/tree/main)

Module Audioを使用する場合は、モジュール内部のDIPスイッチを**B**に設定してください。

### R67

M5Unified 0.2.28・M5GFX 0.2.35を採用。YM2151、C140/C352、PCMミキサー、44.1kHz I2S出力、ES8388音量操作などを更新しました。

---

## English

The CoreS3 R67 edition of vgmM5 plays VGM/VGZ and MDX files from microSD, with touch controls, channel/spectrum display, and output through either the built-in speaker or M5Stack Module Audio (ES8388).

### Installation

- **Web Burner (initial R67 edition):** [Flash in a browser](https://burner.m5stack.com/share/firmware/P8DPPV)
- **M5Burner share code:** `wEBeKwOKFsqAmlsu`
- **Source code:** [CoreS3 Production (main)](https://github.com/Layer812/vgmM5/tree/main)

For Module Audio, set its internal DIP switch to **B**.

### R67

Uses M5Unified 0.2.28 and M5GFX 0.2.35, with updated YM2151, C140/C352, PCM mixing, 44.1kHz I2S output and ES8388 volume control.

# vgmM5 — M5Stack Tab5 edition

[日本語](#日本語) / [English](#english)

## 日本語

M5Stack Tab5（ESP32-P4）向けのVGM/VGZ・MDXプレイヤーです。microSD上の楽曲再生、タッチ操作、曲情報・チャンネル表示、スペクトラム、JPEG/PNG/BMPカバー画像に対応します。

### 正式版 R67（2026年11月4日）

**R67はGitHub `main` に正式統合済みです。** [統合PR #24](https://github.com/Layer812/vgmM5/pull/24) / [ネイティブビルド・回帰テスト PASS](https://github.com/Layer812/vgmM5/actions/runs/41661806444)

- **音符のプチノイズを解消**：デジタル無音区間に16bit PCMで±1の微弱な疑似ランダム信号を出力する、R66-A方式を採用しました。実機Tab5で「プチプチノイズが完全に消えた」と報告されています。非無音サンプル、発音周波数や音色、再生タイミングは変更しません。VGM再生中の無音区間のみ対象です。
- **R66-Bの2msフェードは不採用**：実機で激しいノイズが発生したため、正式版に入れていません。
- **R62〜R65の音源修正**：AY8910ノイズ分周、SCC/SCC+、C140バンク設定、C352/OKIM6258クロック分周、FM/PCMの音量計算、C140音量、曲全体の終端処理。
- **ライブラリ固定**：M5Unified `0.2.28`、M5GFX `0.2.35`。Tab5以外の機種の音声処理は変更していません。
- 旧正式版R61は[保管ブランチ](https://github.com/Layer812/vgmM5/tree/stable/tab5-r61-ym2151-unified028-20261025)に残しています。

### ファームウェアと書き込み

R67の実際のESP32-P4ネイティブビルドは[GitHub Actions R67の成果物](https://github.com/Layer812/vgmM5/actions/runs/41661806444)から取得できます（Actions成果物は保存期限があります）。生成されたアプリBINのサイズは **870,192バイト**、SHA-256 は

```text
ba26ef34a3eb9f9565c8a7e9e7159beef252d61eafbaa70d1dfc81abbe7607d8
```

既存のTab5で確認済みのパーティション配置を維持する場合、`firmware.bin` はアプリ領域 `0x10000` にのみ書き込みます（例：COM9）。

```bat
python -m esptool --chip esp32p4 --port COM9 --baud 460800 --before default_reset --after hard_reset write_flash 0x10000 firmware.bin
```

**アプリ領域以外（ブートローダー、パーティションテーブル、HumanXDFなど）を上書きしません。** 上記の書き込み先はR61以来のTab5用レイアウトを前提とします。R67のBIN自体のビルドは成功していますが、R67のBINへの再書き込みは未確認です。無音時PCM方式の実機での成功は、同じ方式を搭載したR66-Aの報告に基づきます。

### M5Burner（別配布）

既存のTab5用M5Burnerシェアコード：`8i9Cp1C6ufGpGC5J`。

**このコードで配布されるイメージはR67として更新・SHA照合されていません。** R67を使用する場合は上記のGitHubビルドを使ってください。M5Burnerへの新バージョン登録は別作業です。

この版ではオンラインモード、USBマスストレージ、USBキーボード操作は提供していません。楽曲データは同梱していません。ライセンスは[プロジェクトREADME](../README.md#license)を参照してください。

---

## English

vgmM5 for M5Stack Tab5 (ESP32-P4) plays VGM/VGZ and MDX from microSD with touch navigation, channel/spectrum display, and JPEG/PNG/BMP cover artwork.

### Official R67 (November 4, 2026)

**Merged into `main`.** [Merge PR #24](https://github.com/Layer812/vgmM5/pull/24) / [ESP32-P4 native build and host tests PASS](https://github.com/Layer812/vgmM5/actions/runs/41661806444).

- Adopted the **R66-A faint ±1-LSB stereo PCM in digitally silent VGM frames**. On an actual Tab5 the user reported complete removal of repetitive note clicks. The note synthesizers and nonzero output samples are unchanged.
- Rejected R66-B's 2ms note fade after severe noise on real hardware; its code is **not** included.
- Includes R62–R65 AY noise divider, SCC/SCC+, C140, C352, MSM6258, physical-chip mixing, C140 level and end-of-track improvements.
- Pinned M5Unified 0.2.28 / M5GFX 0.2.35. Previous R61 is preserved in the [R61 stable branch](https://github.com/Layer812/vgmM5/tree/stable/tab5-r61-ym2151-unified028-20261025).

### Download and app-only flash

[Verified R67 CI artifact](https://github.com/Layer812/vgmM5/actions/runs/41661806444) (subject to GitHub retention). The native app image is 870,192 bytes with SHA-256 `ba26ef34a3eb9f9565c8a7e9e7159beef252d61eafbaa70d1dfc81abbe7607d8`. For a Tab5 with the established partition map, flash the app **only** at `0x10000` using the command above. Do not overwrite the bootloader, partition table, or HumanXDF. Firmware flashing was not carried out by CI; R67's exact BIN has not been separately device-tested, while R66-A with identical audio logic was verified by the user.

**M5Burner share code** `8i9Cp1C6ufGpGC5J` is a separate distribution and is **not yet verified or updated to R67**. To obtain R67, use the native build artifact instead. No bundled music or online/USB MSC/USB keyboard mode in this Tab5 edition.

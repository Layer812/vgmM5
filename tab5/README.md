# vgmM5 — M5Stack Tab5 edition

[日本語](#日本語) / [English](#english)

## 日本語

M5Stack Tab5（ESP32-P4）向けのVGM/VGZ・MDXプレイヤーです。microSD上の楽曲再生、タッチ操作、曲情報・チャンネル表示、スペクトラム、JPEG/PNG/BMPカバー画像に対応しています。

### M5Burnerで入手

**Tab5用シェアコード：`8i9Cp1C6ufGpGC5J`**

[M5Stack公式Tab5ページ](https://docs.m5stack.com/ja/core/Tab5)からM5Burnerなどの情報を参照できます。他機種用のイメージは使用しないでください。

### 更新内容（2026年10月25日）

- **M5Unified 0.2.28 / M5GFX 0.2.35に対応**
- **YM2151を含む複数の音源処理を修正・改善**
- Tab5実機で再生・音質を確認

### ソースとビルド

最新のTab5ソースは [`firmware/src/`](firmware/src/) にあります。使用するプラットフォームは pioarduino `54.03.21`、ライブラリはM5Unified `0.2.28` とM5GFX `0.2.35` です。更新内容を含むソースは [GitHubの安定版ブランチ](https://github.com/Layer812/vgmM5/tree/stable/tab5-r61-ym2151-unified028-20261025) でも参照できます。

ESP32-P4ネイティブビルド：[`GitHub Actions PASS`](https://github.com/Layer812/vgmM5/actions/runs/40199799670)。app-onlyビルドのSHA-256：`ea03ec333db0a220fe4f497130bd0f493ed489d7c651766bddb6f3491ad722fb`。

従来の [R61 FIX1ソースZIP](vgmM5_Tab5_R61_FIX1_source.zip)、[`RUN_THIS_TAB5_BUILD_ONLY.cmd`](RUN_THIS_TAB5_BUILD_ONLY.cmd)、[`BUILD_ONLY.md`](BUILD_ONLY.md) は**旧版の保管資料**です。最新版の依存関係や音源修正を含むビルド結果とは異なります。

シェアコードで配信されるM5Burner側のBINと、上記GitHubビルドBINとのハッシュ照合は行っていません。フルフラッシュ書込では既存データが上書きされる場合があります。

この版ではオンラインモード、USBマスストレージ、USBキーボード操作は提供していません。楽曲データは同梱していません。ライセンスについては[プロジェクトREADME](../README.md#license)を参照してください。

---

## English

vgmM5 for M5Stack Tab5 (ESP32-P4) plays local VGM/VGZ and MDX from microSD, with touch navigation, channel/spectrum display, and JPEG/PNG/BMP cover artwork.

### M5Burner

**Tab5 share code: `8i9Cp1C6ufGpGC5J`**

### What's new — October 25, 2026

- Updated to **M5Unified 0.2.28 / M5GFX 0.2.35**
- Improved several sound engines, including **YM2151**
- Playback and audio confirmed on Tab5 hardware

### Source and build

Current source: [`firmware/src/`](firmware/src/) and the [stable branch](https://github.com/Layer812/vgmM5/tree/stable/tab5-r61-ym2151-unified028-20261025). Native ESP32-P4 build: [GitHub Actions PASS](https://github.com/Layer812/vgmM5/actions/runs/40199799670).

The [R61 FIX1 source ZIP](vgmM5_Tab5_R61_FIX1_source.zip) and legacy build-only scripts are retained as **historical snapshots** and do not reproduce the updated libraries or sound corrections.

The M5Burner image has not been independently compared by SHA-256 with the GitHub CI binary. Full-device flashing can overwrite existing data. Online mode, USB mass storage and USB keyboard controls are not part of this edition. Music files are not bundled.

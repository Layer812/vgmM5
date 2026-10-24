# vgmM5 (v0.98)
vgm and mdx player for M5Stack

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

![CoreS3](out.png)

(English version follows below)

---

## 日本語版 (Japanese)

### 概要
vgmM5 は M5Stackシリーズで動作するVGM/VGZおよびMDX(MXDRV)ファイルプレイヤーです。
🎥 **動作風景 (Demonstration):** [https://x.com/layer812/status/2065461081305489671](https://x.com/layer812/status/2065461081305489671)

#### 対応デバイスと機能 (Supported Devices & Features)
| 機能 (Features) \ デバイス (Device) | M5Stack AtomS3R (Echo) | M5Stack Cardputer | M5Stack CoreS3 | + [Module Audio](https://docs.m5stack.com/ja/module/Module-Audio) | [M5 TAB5](https://docs.m5stack.com/ja/core/Tab5) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **ステータス (Status)** | ✔️ 対応済 | ✔️ 対応済 | ✔️ 対応済 | ✔️ 対応済 | [✔️ R61 FIX1](tab5/README.md) ※注意事項 |
| **VGM/VGZ 再生** | ✔️ | ✔️ | ✔️ | ✔️ | ✔️ |
| **MDX 再生** | ✔️ | ✔️ | ✔️ | ✔️ | ✔️ |
| **内蔵フラッシュ (USB Drive)** | ✔️ | - | - | - | - |
| **MicroSD** | - | ✔️ | ✔️ | ✔️ | ✔️ |
| **オンラインモード** | - | ✔️ | - | - | - |
| **キーボード操作** | - | ✔️ | - | - | - |


#### 対応音源チップ (Supported Sound Chips)
| チップ名 (Chip) | ステータス (Status) |
| :--- | :--- |
| **YM2612 (OPN2)** | ✔️ 対応済 (Supported) |
| **YM3812 (OPL2)** | ✔️ 対応済 (Supported) |
| **SN76489 (PSG)** | ✔️ 対応済 (Supported) |
| **AY-3-8910 (SSG)** | ✔️ 対応済 (Supported) |
| **YM2203 (OPN)** | ✔️ 対応済 (Supported) |
| **YM2608 (OPNA)** | ✔️ 対応済 (Supported |
| **YM2610 (OPNB)** | ✔️ 対応済 (Supported) |
| **YM2151 (OPM)** | ✔️ 対応済 (Supported) |
| **YM2413 (OPLL)** | ✔️ 対応済 (Supported) |
| **SegaPCM** | ✔️ 対応済 (Supported) |
| **K051649 (SCC1)** | ✔️ 対応済 (Supported) |
| **MSM6258** | ✔️ 対応済 (Supported) |
| **OKIM6295** | ✔️ 対応済 (Supported) |
| **Namco C140** | ✔️ 対応済 (Supported |
| **Namco C352** | ✔️ 対応済 (Supported) |

※ いくつかのチップのサウンドテストを経た安定板（v0.98）です。ソースは近日更新します。
※ ステータスが「対応済」となっていても、もし再生時に音がおかしい・違和感があると感じた場合は、曲名とおかしい点を優しく申告していただければ対応いたします！

#### M5Stack Tab5版 (R61 FIX1)
ESP32-P4採用のTab5に対応しました。VGM/VGZ・MDXのmicroSD再生、タッチ操作、JPEG/PNG/BMPカバー画像表示が利用できます。カバー画像の高速化は対象外です。

**既知の問題:** 激しいクリックノイズが同じファームウェアで再発した報告があります。電源断後に正常再生へ戻った実例がありますが、原因はまだ特定できていません。過去の音声スケジューリング変更で根本解決したとは扱いません。長時間安定性も未保証です。

詳しくは [Tab5版の説明と注意事項](tab5/README.md) をご覧ください。

### 特徴
- **オンラインモード**: Wi-Fiに接続することで、VGM/VGZXファイルをダウンロードする手間なく、クラウドのファイルを指定して再生できます。
- **MDX (MXDRV) フォーマット対応**: VGM形式に加えて、X68000で広く使われたMDX形式の再生にも対応しました。
- **VGZ(GZIP圧縮)対応**: 解凍せずにそのまま再生可能。
- **軽量・高速**: M5Stackの限られたリソースに最適化。

### インストール方法 (M5 Burner)
手軽に試す場合は、M5 Burnerから以下のシェアコードでファームウェアを直接書き込めます。

- **M5Stack Tab5用 シェアコード:** `rx5H5eOnM45ed4eX`（イメージSHAは独立未照合）
- **M5Stack CoreS3用 シェアコード:** `HRmuTxOVO7iUxfeo`
- **M5Stack Cardputer用 シェアコード:** `KxVPdM380FWTyMmC`
- **M5Stack Atom Echo S3R用 シェアコード:** `zoSd8neRRq7ZQFjB`

---

### 操作方法

#### 📺 M5Stack CoreS3
CoreS3版はタッチパネル操作に対応しています。外部モジュール [Module-Audio](https://docs.m5stack.com/ja/module/Module-Audio) を接続しての高音質再生もサポートしています。

> [!IMPORTANT]
> **Module-Audio を使用する場合、正常に動作させるためにモジュール内部のDIPスイッチを「B」に切り替えてから接続してください。**

**基本操作 (タッチパネル):**
- **ファイルリストのタップ**: フォルダ移動、ファイル選択・再生
- **画面下部のアイコン**: (左から) ブラウザに戻る / 前の曲 / 再生・停止 / 次の曲
- **再生中サムネイルのタップ**: 上半分で音量アップ、下半分で音量ダウン

#### 💻 M5Stack Cardputer

**[Cardputer v0.98 independent source/build guide](cardputer/README.md)** (preserves the original; online catalog requires separately supplied DB files).

**[Cardputer v0.98 独立ソース・ビルド手順](cardputer/README.md)**（既存版を保護した別ディレクトリ。オンラインDB欠落時はローカル再生用ビルド）
Cardputer版はMicroSDカード内のファイル再生に加え、オンラインモードでの再生に対応しています。

**基本操作:**
- **上下 / 方向キー**: ファイルや項目の選択
- **Spaceキー**: 選択した曲の再生 / 停止
- **= キー**: 音量アップ
- **- キー**: 音量ダウン

**オンラインモードの操作:**
- Online/Local: オンラインモードとローカルモード（SDカード）を切り替えます。
- オンラインモードでは、アルバムや曲のリストを読み込み、選択するだけでジュークボックスのように自動で再生が始まります。

#### 🎧 M5Stack Atom Echo S3R
AtomS3Rは画面表示を持たず、メインボタンで操作します。起動時は無音状態で待機します。
- **1クリック**: 再生 / 停止
- **ダブルクリック**: 次の曲
- **トリプルクリック**: 前の曲
- **起動中にボタン長押し**: USBドライブモードに入り、PCから直接ファイルを追加できます。

---

## English Version

### Overview
vgmM5 is a VGM/VGZ and MDX (X68000) file player designed specifically for the M5Stack series.
🎥 **Demonstration:** [https://x.com/layer812/status/2065461081305489671](https://x.com/layer812/status/2065461081305489671)


| Features \ Device | M5Stack AtomS3R (Echo) | M5Stack Cardputer | M5Stack CoreS3 | + [Module Audio](https://docs.m5stack.com/ja/module/Module-Audio) | [M5 TAB5](https://docs.m5stack.com/ja/core/Tab5) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Status** | ✔️ Supported | ✔️ Supported | ✔️ Supported | ✔️ Supported | [✔️ R61 FIX1](tab5/README.md) (known issue) |
| **VGM/VGZ Playback** | ✔️ | ✔️ | ✔️ | ✔️ | ✔️ |
| **MDX Playback** | ✔️ | ✔️ | ✔️ | ✔️ | ✔️ |
| **Internal Flash (USB Drive)** | ✔️ | - | - | - | - |
| **MicroSD** | - | ✔️ | ✔️ | ✔️ | ✔️ |
| **Online Mode** | - | ✔️ | - | - | - |
| **Keyboard Control** | - | ✔️ | - | - | - |

#### Supported Sound Chips
*(Please refer to the Supported Sound Chips table in the Japanese section above)*

*Note: This is a stable version (v0.98) after sound tests on various chips.*
*Even if a chip is listed as "Supported," if you notice any strange sounds or something feels off during playback, please kindly report it (gently) with the song title and the issue, and I will address it!*

#### M5Stack Tab5 edition (R61 FIX1)
Supports ESP32-P4 Tab5 with VGM/VGZ and MDX playback from microSD, touch controls, and JPEG/PNG/BMP cover artwork. Cover-art performance optimization is intentionally not included.

**Known issue:** Severe click/crackle noise has recurred on the *same firmware*. It disappeared after removing power in one observed case; the root cause remains unconfirmed. Earlier speaker scheduling changes are not a proven cure. Long-run audio stability remains unverified.

See the [Tab5 documentation and safety notes](tab5/README.md).

### Features
- **Online Mode (Jukebox)**: Connect via Wi-Fi to play VGM/VGZ files directly from the cloud without the need to download and save them to a MicroSD card.
- **MDX (X68000) Support**: Added playback support for the MDX format, popular on the Sharp X68000.
- **VGZ (GZIP) Support**: Plays directly without decompression.
- **Lightweight & Fast**: Highly optimized for M5Stack's limited hardware resources.

### Installation (via M5 Burner)
For a quick and easy start, you can flash the firmware directly to your device using M5 Burner with the following share codes:

- **M5Stack Tab5 Share Code:** `rx5H5eOnM45ed4eX` (image SHA not independently verified)
- **M5Stack CoreS3 Share Code:** `HRmuTxOVO7iUxfeo`
- **M5Stack Cardputer Share Code:** `KxVPdM380FWTyMmC`
- **M5Stack Atom Echo S3R Share Code:** `zoSd8neRRq7ZQFjB`
---

### Controls

#### 📺 M5Stack CoreS3
The CoreS3 version supports touch panel controls. It also supports high-quality audio output via an external [Module-Audio](https://docs.m5stack.com/ja/module/Module-Audio).

> [!IMPORTANT]
> **When using the Module-Audio, you must switch its internal DIP switch to "B" before connecting it for proper operation.**

**Basic Controls (Touch Panel):**
- **Tap File List**: Navigate folders, select/play files
- **Tap Bottom Icons**: (From left to right) Back to Browser / Previous Track / Play/Stop / Next Track
- **Tap Thumbnail Image**: Top half for Volume Up, Bottom half for Volume Down

#### 💻 M5Stack Cardputer
The Cardputer version plays files directly from a MicroSD card and supports streaming via Online Mode.

**Basic Controls:**
- **Up/Down / Arrow Keys**: Select file or item
- **Space Key**: Play/Stop selected track
- **= Key**: Volume Up
- **- Key**: Volume Down

**Online Mode Controls:**
- Online/Local: Toggle between Online Mode and Local Mode (MicroSD).
- In Online Mode, simply browse through albums or tracks and select one to start streaming automatically like a jukebox.

#### 🎧 M5Stack Atom Echo S3R
The AtomS3R version runs headlessly and is controlled via the main screen button. It boots into a silent idle state.
- **1 Click**: Play / Stop
- **Double Click**: Next Track
- **Triple Click**: Previous Track
- **Hold while booting**: Enters USB Drive Mode to add files from your PC.

---

### License
Our original source code, modifications, and the matrix processing sound engine are released under the **MIT License**.

This project utilizes and references the following excellent libraries, code, and assets:
- [mdxtools](https://github.com/vampirefrog/mdxtools) - GPL3.0
- [M5Unified](https://github.com/m5stack/M5Unified) - MIT License
- [MAME](https://www.mamedev.org/) - GPL
- ESP-IDF (FFat / Wear Levelling) - Apache License 2.0
- Puff (zlib decompressor) by Mark Adler - zlib License
- [Misaki Font](https://littlelimit.net/misaki.htm) / Little Limit - Free Font License
- [ymfm](https://github.com/aaronsgiles/ymfm) by Aaron Giles - BSD 3-Clause License
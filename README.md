# vgmM5 (v0.9)
vgm and mdx player for M5Stack

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

![Cardputer](card.jpg)

(English version follows below)

---

## 日本語版 (Japanese)

### 概要
vgmM5 は M5Stackシリーズで動作するVGM/VGZおよびMDX(X68000)ファイルプレイヤーです。
🎥 **動作風景 (Demonstration):** [https://x.com/layer812/status/2065461081305489671](https://x.com/layer812/status/2065461081305489671)

#### 対応デバイス (Supported Devices)
| デバイス (Device) | ステータス (Status) | 備考 (Notes) |
| :--- | :--- | :--- |
| **M5Stack AtomS3R (Echo)** | ✔️ 対応済 (Supported) | 内蔵フラッシュUSBドライブ対応 |
| **M5Stack Cardputer** | ✔️ 対応済 (Supported) | MicroSDカード・オンラインモード・キーボード操作対応 |
| **M5Stack CoreS3** | 🔮 将来対応予定 (Future) | |

#### 対応音源チップ (Supported Sound Chips)
| チップ名 (Chip) | ステータス (Status) |
| :--- | :--- |
| **YM2612 (OPN2)** | ✔️ 対応済 (Supported) |
| **YM3812 (OPL2)** | ✔️ 対応済 (Supported) |
| **SN76489 (PSG)** | ✔️ 対応済 (Supported) |
| **AY-3-8910 (SSG)** | ✔️ 対応済 (Supported) |
| **YM2203 (OPN)** | ✔️ 対応済 (Supported) |
| **YM2608 (OPNA)** | ✔️ 対応済 (Supported) |
| **YM2610 (OPNB)** | ✔️ 対応済 (Supported) |
| **YM2151 (OPM)** | ✔️ 対応済 (Supported) |
| **YM2413 (OPLL)** | ✔️ 対応済 (Supported) |
| **SegaPCM** | ✔️ 対応済 (Supported) |
| **K051649 (SCC1)** | ✔️ 対応済 (Supported) |
| **MSM6258** | ✔️ 対応済 (Supported) |
| **OKIM6295** | ✔️ 対応済 (Supported) |
| **Namco C140** | ✔️ 対応済 (Supported) |
| **Namco C352** | ✔️ 対応済 (Supported) |

### 特徴
- **オンラインモード (ジュークボックス)**: Wi-Fiに接続することで、VGM/VGZ/MDXファイルをいちいちダウンロードしてSDカードに保存する手間なく、クラウド上から直接ストリーミング再生できます。
- **MDX (X68000) フォーマット対応**: VGM形式に加えて、X68000で広く使われたMDX形式の再生にも対応しました。
- **VGZ(GZIP圧縮)対応**: 解凍せずにそのまま再生可能。
- **軽量・高速**: M5Stackの限られたリソースに最適化。

### インストール方法 (M5 Burner)
手軽に試す場合は、M5 Burnerから以下のシェアコードでファームウェアを直接書き込めます。

- **M5Stack Cardputer用 シェアコード:** `UdY7vTALdYPxChON`
- **M5Stack Atom Echo S3R用 シェアコード:** `c5t52qiD50X3eo58`

---

### 操作方法

#### 💻 M5Stack Cardputer
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
AtomS3Rは画面表示を持たず、メインボタンで操作します。起動時は一時停止状態で開始します。
- **1クリック**: 再生 / 一時停止
- **ダブルクリック**: 次の曲
- **トリプルクリック**: 前の曲
- **起動中にボタン長押し**: USBドライブモードに入り、PCから直接ファイルを追加できます。

---

## English Version

### Overview
vgmM5 is a VGM/VGZ and MDX (X68000) file player designed specifically for the M5Stack series.
🎥 **Demonstration:** [https://x.com/layer812/status/2065461081305489671](https://x.com/layer812/status/2065461081305489671)

#### Supported Devices
| Device | Status | Notes |
| :--- | :--- | :--- |
| **M5Stack AtomS3R (Echo)** | ✔️ Supported | Supports built-in Flash USB Drive |
| **M5Stack Cardputer** | ✔️ Supported | Reads from MicroSD, Online Mode & Keyboard control |
| **M5Stack CoreS3** | 🔮 Future | |

#### Supported Sound Chips
*(Please refer to the Supported Sound Chips table in the Japanese section above)*

### Features
- **Online Mode (Jukebox)**: Connect via Wi-Fi to stream VGM/VGZ/MDX files directly from the cloud without the need to download and save them to a MicroSD card.
- **MDX (X68000) Support**: Added playback support for the MDX format, popular on the Sharp X68000.
- **VGZ (GZIP) Support**: Plays directly without decompression.
- **Lightweight & Fast**: Highly optimized for M5Stack's limited hardware resources.

### Installation (via M5 Burner)
For a quick and easy start, you can flash the firmware directly to your device using M5 Burner with the following share codes:

- **M5Stack Cardputer Share Code:** `UdY7vTALdYPxChON`
- **M5Stack Atom Echo S3R Share Code:** `c5t52qiD50X3eo58`

---

### Controls

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
The AtomS3R version runs headlessly and is controlled via the main screen button. It boots into a paused state.
- **1 Click**: Play / Pause
- **Double Click**: Next Track
- **Triple Click**: Previous Track
- **Hold while booting**: Enters USB Drive Mode to add files from your PC.

---

### License
Our original source code, modifications, and the matrix processing sound engine are released under the **MIT License**.

This project utilizes and references the following excellent libraries, code, and assets:
- [M5Unified](https://github.com/m5stack/M5Unified) - MIT License
- ESP-IDF (FFat / Wear Levelling) - Apache License 2.0
- Puff (zlib decompressor) by Mark Adler - zlib License
- [Misaki Font](https://littlelimit.net/misaki.htm) / Little Limit - Free Font License
- [ymfm](https://github.com/aaronsgiles/ymfm) by Aaron Giles - BSD 3-Clause License
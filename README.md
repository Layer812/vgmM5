# vgmM5
vgm player for M5Stack

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

(English version follows below)

---

## 日本語版 (Japanese)

### 概要
vgmM5 は M5Stackシリーズで動作するVGM/VGZファイルプレイヤーです。

#### 対応デバイス (Supported Devices)
| デバイス (Device) | ステータス (Status) | 備考 (Notes) |
| :--- | :--- | :--- |
| **M5Stack AtomS3** | ✅ 対応済 (Supported) | 内蔵スピーカー専用の最適化EQ搭載 |
| **M5Stack Cardputer** | 🚧 次回対応予定 (Next) | MicroSDカードからの読み込みに対応予定 |

#### 対応音源チップ (Supported Sound Chips)
| チップ名 (Chip) | ステータス (Status) |
| :--- | :--- |
| **YM2612 (OPN2)** | ✅ 対応済 (Supported) |
| **YM3812 (OPL2)** | ✅ 対応済 (Supported) |
| **YM2203 (OPN)**  | ✅ 対応済 (Supported) |
| **YM2151 (OPM)**  | ✅ 対応済 (Supported) |
| **SN76489 (DCSG)**| 🚧 次回対応予定 (Next) |
| **Famicom (2A03)**| 🔮 将来対応予定 (Future) |
| **SID (MOS6581)** | 🔮 将来対応予定 (Future) |

最大の特徴として、**マトリクス処理による新設計のサウンドエンジン**を搭載しています。これにより、限られたリソースのマイコン上でも効率的に複数のFM音源チップ（YM2612、YM3812、YM2203など）を統合してエミュレーションし、高音質で再生することが可能になっています。

また、AtomS3のような極小スピーカーの特性に合わせた専用のイコライザー（EQ）処理も搭載しており、物理的な制約の中でも最大限の音響体験を提供します。

### インストール方法（M5 Burnerを使用する場合）
手軽にデモ楽曲を試したい方は、M5 Burnerから以下のシェアコードを利用して直接ファームウェアを書き込むことができます。
**Share Code:** `PETvTzcjDHAx9WP8`

### 好きなVGM/VGZファイルを追加・再生する方法（USBメモリ機能）
本プログラムは **USBマスストレージクラス (MSC)** に対応しており、PCに繋ぐだけでUSBメモリのように直接ファイルをドラッグ＆ドロップで追加できます！専用ソフト（PlatformIO等）は一切不要です。

1. AtomS3の **画面（ボタン）を押したまま** PCにUSB接続するか、本体側面の小さなリセットボタンを押します。
2. LEDが点灯し「USB Drive Mode」で起動します。この時、PC側に新しいUSBドライブ（USBフラッシュメモリ）が現れます。
3. そのドライブを開き、お手持ちの `.vgm` や `.vgz` ファイルをそのままコピー（ドラッグ＆ドロップ）してください。
4. コピーが完了したら、今度は**ボタンを押さずに**側面の小さなリセットボタンを押す（あるいはUSBケーブルを挿し直す）と、普通にプレイヤーとして起動し、入れた曲が再生されます。

> **※ファームウェアの書き換えについて（開発者向け）**
> TinyUSBモードを採用しているため、通常の書き込みポートが見つからなくなる場合があります。PlatformIO等でファームウェアを書き換える際は、**「側面の小さなリセットボタンを長押し（約2秒）」**して、LEDが緑色に点灯する「ダウンロードモード」に入れてから書き込み（Upload）を実行してください。


### ライセンス (License)
本プロジェクトの独自のソースコード（改修部分、マトリクス処理によるサウンドエンジン等）は **MITライセンス** の下で公開されています。

また、本プロジェクトは以下の素晴らしいライブラリ・コードを利用/参照しています。心より感謝申し上げます。
- [M5Unified](https://github.com/m5stack/M5Unified) - MIT License
- ESP-IDF (FFat / Wear Levelling) - Apache License 2.0
- Puff (zlib decompressor) by Mark Adler - zlib License

---

## English

### Overview
vgmM5 is a VGM/VGZ file player designed for the M5Stack series.

#### Supported Devices
| Device | Status | Notes |
| :--- | :--- | :--- |
| **M5Stack AtomS3** | ✅ Supported | Features optimized EQ for internal micro-speaker |
| **M5Stack Cardputer** | 🚧 Next | Will support loading from MicroSD card |

#### Supported Sound Chips
| Chip Name | Status |
| :--- | :--- |
| **YM2612 (OPN2)** | ✅ Supported |
| **YM3812 (OPL2)** | ✅ Supported |
| **YM2203 (OPN)**  | ✅ Supported |
| **YM2151 (OPM)**  | ✅ Supported |
| **SN76489 (DCSG)**| 🚧 Next |
| **Famicom (2A03)**| 🔮 Future |
| **SID (MOS6581)** | 🔮 Future |

Its core feature is a **newly designed sound engine based on matrix processing**. This allows efficient integration and accurate emulation of multiple sound chips (such as YM2612, YM3812, YM2203) on microcontrollers with limited hardware resources.

Additionally, it features a dedicated equalizer (EQ) tailored for the acoustic characteristics of micro-speakers (like the one found in the AtomS3), delivering the best possible audio experience despite physical constraints.

### Installation (via M5 Burner)
For a quick and easy start with demo tracks, you can flash the firmware directly to your AtomS3 using M5 Burner with the following share code:
**Share Code:** `PETvTzcjDHAx9WP8`

### How to Add Custom VGM/VGZ Files (USB Mass Storage Mode)
This firmware features **USB Mass Storage Class (MSC)** support, meaning you can plug it into your PC and drag & drop files just like a standard USB flash drive! No special tools like PlatformIO are required to add songs.

1. Connect the AtomS3 to your PC **while pressing and holding the main screen button**, or press the small reset button on the side while holding the screen button.
2. The device will boot into "USB Drive Mode" and the LED will light up. Your PC will recognize it as a new USB flash drive.
3. Open the drive and simply drag & drop your `.vgm` or `.vgz` files into it.
4. Once the copy is complete, reset the device normally (press the small reset button on the side **without** holding the screen button, or reconnect the USB). It will boot as a player and start playing your files.

> **※ Note for Developers (Flashing Firmware)**
> Because this project uses TinyUSB, the default hardware CDC port may disappear during normal operation. To flash new firmware via PlatformIO, you must manually put the device into Download Mode: **Press and hold the small reset button on the side for about 2 seconds** until the internal LED turns green, then execute the Upload task.


### License
Our original source code, modifications, and the matrix processing sound engine are released under the **MIT License**.

This project utilizes and references the following excellent libraries and code:
- [M5Unified](https://github.com/m5stack/M5Unified) - MIT License
- ESP-IDF (FFat / Wear Levelling) - Apache License 2.0
- Puff (zlib decompressor) by Mark Adler - zlib License

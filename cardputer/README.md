# vgmM5 — Cardputer edition (v0.98 source snapshot)

## 日本語

既存の [vgmM5 v0.98](../README.md) から、Cardputer用ソースを独立させた **ESP32-S3 / M5Stack Cardputer向けのビルド環境**です。2026-10-25時点の旧ルートソース45ファイルを **バイト単位で変更せず** コピーし、既存の `src/`・Tab5 R61 FIX1・他機種版は変更していません。

### Windowsでのビルド（書込なし）

PlatformIO Coreが利用できるWindowsでこのリポジトリを取得し、以下を実行します。

~~~bat
cardputer\RUN_THIS_CARDPUTER_BUILD_ONLY.cmd
~~~

ビルドはCardputer専用の `platformio.ini` と `partitions.csv` を使用します。レポートはG:ドライブが存在する場合 `G:\VGMM5_CARDPUTER_BUILD_SEND_THIS.zip`、それ以外はCardputerディレクトリ内に生成します。**実機へは書き込みません。**

### オンラインモード用DBの注意

旧ソースの `platformio.ini` は `src/albums.bin`、`tracks.bin`、`strings.bin`、`companies.bin`、`composers.bin`、`systems.bin` の **6ファイルをファームウェアに埋め込む設計**でした。しかし、旧GitHubリポジトリにはこの6ファイルも生成用 `build_db.py` も存在しません。

- 6ファイルが存在しない場合：ビルドスクリプトが容量1バイトの**空のDB**を6個生成します。microSDのVGM/VGZ・MDXローカル再生コードは元のままですが、**オンライン楽曲カタログは空で使用できません**。
- 完全な6ファイルを正当に所持している場合：すべてを `cardputer/src/` に配置すると、そのファイルを埋め込みます。配布権限のない第三者DBはGitHubにアップロードしないでください。
- 1～5ファイルだけ、または空DBと実DBが混在する場合：内容の不整合を避けるため**安全停止**します。オンラインDBの内容や実際のネットワーク再生については未検証です。
- 空DBはGit管理対象に含まず、`src/*.bin` として無視されます。既に生成した空DBの代わりに実DBを使う場合は**6個すべて**を置き換えてください。

### 正確な検証状態

- 45ソースファイルと元のパーティション定義のGit Blob SHA-1を `SOURCE_LOCK.json` で固定。
- Tab5のR61用演算修正・Speaker・LCD初期化はCardputerへ**移植していません**。
- 本環境ではPlatformIOのESP32-S3ツールチェーンを実行できていないため、**ネイティブビルドPASS・実機再生PASSは主張しません**。旧Cardputer版のM5Burner用コード `KxVPdM380FWTyMmC` は今回生成したBINと同一とは確認できていません。
- `RUN_THIS_CARDPUTER_BUILD_ONLY.cmd` はビルド専用。フラッシュや既存機の消去は行いません。

旧Cardputer版に合わせ、**Espressif32プラットフォーム6.13.0／Arduino 2.0.17に固定**しました。2026年の最新8.0.0／Arduino 3.3.12では旧Flash mmap APIが変更され、元コードのままではコンパイルできなかったためです。`env:m5stack-cardputer` は維持しています。音源共通化や新しいArduino/ESP-IDFへのアップグレードは将来の別作業です。

## English

This directory independently snapshots the original **M5Stack Cardputer / ESP32-S3 v0.98** source. All 45 compiled source/header files and the original partition table are preserved byte-for-byte; existing root projects and Tab5 R61 FIX1 are untouched.

Run `cardputer\RUN_THIS_CARDPUTER_BUILD_ONLY.cmd` on Windows with PlatformIO Core to perform a **build only**. No device flashing is performed. A single report ZIP is written to the root of G: when available, otherwise into the Cardputer directory.

The original project referenced six embedded online-catalogue `.bin` files which were **not in the repository**. If none are present, the build runner generates six one-byte empty files, allowing an **offline/local microSD build** but leaving **Online catalogue mode empty/unusable**. To use the full online catalogue, supply all six valid DB files that you are entitled to use. Incomplete or mixed DB sets fail validation, and provided DB contents are not fully audited. Never commit proprietary datasets.

The build platform is pinned to **Espressif32 6.13.0 / Arduino 2.0.17**, since unpinned 8.0.0 / Arduino 3.3.12 failed on the original Flash mmap API. Source hashes and the partition layout are protected by `SOURCE_LOCK.json`. No Cardputer native compile or hardware playback has been verified in this environment; do not infer that the previously distributed M5Burner binary is bit-identical to a new build. Third-party source licensing and external media rights remain governed by their original terms.

> AUDIO_R1: このブランチは音源演算の安全性検証専用の開発版です。旧v0.98の正式ソースは main に保存されています。実機での音質確認までは統合しません。

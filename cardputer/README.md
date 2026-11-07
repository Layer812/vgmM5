# vgmM5 — M5Stack Cardputer R7 / R6

[日本語](#日本語) / [English](#english)

## 日本語

### Cardputer R7 — C140音量を独立比較する追加開発版

[共通エンジン版PR #61](https://github.com/Layer812/vgmM5/pull/61)を基準に、**Cardputer専用のC140音量**を1倍／2倍／4倍／8倍から比較できるようにした追加開発版です。標準の1倍はCoreS3とPCMが完全一致します。8倍は旧R6のC140の増幅量に近づける試験用設定で、旧R6のFM音色まで再現するものではありません。

- **通常版**（CoreS3同一音量）：`pio run -d cardputer -e m5stack-cardputer`
- **Classic比較版**（CardputerのC140のみ8倍）：`pio run -d cardputer -e m5stack-cardputer-classic`
- **任意の手持ちVGM/VGZの聴き比べ**：`python cardputer/tools/compare_vgm_audio.py "C:\\music\\sample.vgz" --seconds 30 --cardputer-gain 8 --zip`

比較器はオリジナルCardputer R6・R7・CoreS3の3系統それぞれについてFM単独／C140単独／ミックスを44.1kHzステレオWAVへ出力します。音量をそろえたミックスWAVと差分レポートも作成します。入力音源は自分のPC上だけで読み込み、GitHubやCIへアップロードしません。

CIには独自作曲ではなく**独自生成したオリジナルのVGMテスト信号**を使用し、市販ゲーム音源データは同梱していません。実際のゲーム曲の比較には手持ちの正規VGM/VGZを指定してください。対象はYM2151＋C140のみです（他の音源チップや実機スピーカーの音響特性は比較対象外）。

**この音量バリエーションは試験用です。正式版および元のCardputer R6を変更しません。**

---

### Cardputer R7 — 共通 vgmM5lib 開発版

Cardputerの音源エンジンをCoreS3 R67と共通の **vgmM5lib** に移行しました。YM2151などのFM、C140/C352・SegaPCM等のPCM、PSG、SSG、SCC、R31 YM2413を共通化しています。Cardputer固有のキーボード・画面・microSD・オンライン機能・MDX再生コードは維持しています。

**Windowsでのネイティブビルド（書き込みなし）**：`cardputer\\RUN_THIS_CARDPUTER_R7_VGMM5LIB_BUILD_ONLY.cmd`

**音声比較**：オリジナルCardputer R6・R7（vgmM5lib）・CoreS3（vgmM5lib）の3系統について、同じYM2151レジスタ列と24ch C140 ROMから生成した44.1kHzのPCMを比較します。R7とCoreS3の音源コア出力はビット単位で一致します。元R6とはFM・C140とも異なります。音声比較のテストは合成レジスタ入力であり、実機スピーカーの音響特性を比較するものではありません。

- [R7開発ブランチ](https://github.com/Layer812/vgmM5/tree/dev/cardputer-r7-vgmm5lib-cores3-compare-20261107)
- [元のCardputer R6固定ブランチ](https://github.com/Layer812/vgmM5/tree/stable/cardputer-r6-unified028-20261026)
- [R7ネイティブビルドと3系統PCM比較](https://github.com/Layer812/vgmM5/actions/runs/42328480024)
- [Web Burner・Cardputer公開版（元のR6）](https://burner.m5stack.com/METQC2)

---

### Cardputer R6 — 従来版

M5Stack Cardputer（ESP32-S3）向けVGM/VGZ・MDXプレイヤーです。従来のv0.98ソースを保護し、音源処理と依存ライブラリを更新したR6ソースを公開しています。

### M5Burner

**Cardputer R6用シェアコード：`rqz95QwaAsM6bN3O`**

プロジェクト作者から提供されたR6の公開用コードです。M5Burner側の配信BINとGitHubビルドBINのSHA-256一致は、こちらでは確認していません。

### 更新内容（2026年10月26日）

- **M5Unified 0.2.28 / M5GFX 0.2.35に対応**
- **YM2151の音程・変調・KEY ONと発音時の位相処理を修正**
- **C140 / C352のPCM演算を改善**

### ソースと検証

- [Cardputer R6ソースの固定ブランチ](https://github.com/Layer812/vgmM5/tree/stable/cardputer-r6-unified028-20261026)
- [Cardputerファームウェアソース](src/)
- [YM2151 R6修正 PR #11](https://github.com/Layer812/vgmM5/pull/11) と [M5Unified更新 PR #12](https://github.com/Layer812/vgmM5/pull/12)
- [R6 ESP32-S3ネイティブビルド PASS](https://github.com/Layer812/vgmM5/actions/runs/40204050559)
- [旧R5ソースの保管ブランチ](https://github.com/Layer812/vgmM5/tree/stable/cardputer-r5-pre-r6-20261026)

**R6はネイティブビルドとホスト側の回帰テストを確認済みです。** R6実機での音質・連続再生については、現時点で独立した検証結果を記載していません。

### Windowsでのビルド（書込なし）

PlatformIO CoreがあるWindowsで、このリポジトリを取得して次を実行します。

```bat
cardputer\RUN_THIS_CARDPUTER_BUILD_ONLY.cmd
```

`cardputer/platformio.ini` の `m5stack-cardputer` 環境（Espressif32 6.13.0、M5Unified 0.2.28、M5GFX 0.2.35）を使います。**デバイスには書き込みません。** レポートZIPは `F:\build\tmp` があればそこに出力します。

### オンラインモード用DB

元のGitHubリポジトリには、オンライン楽曲カタログを構成する6つの `.bin` ファイルが含まれていません。全6ファイルを所持していない場合、ビルドスクリプトは1バイトの空DBを生成します。この場合、**microSDからのローカル再生用ビルドとなり、オンラインカタログは利用できません**。

適切な権利を持つ完全な6ファイルを別途配置した場合のみ、実DBを埋め込みます。欠落・混在は安全停止し、第三者の楽曲・データベースはこのリポジトリに同梱しません。

---

## English

### Optional Cardputer C140 balance — independent comparison

The shared **Cardputer R7 / CoreS3 R67** PCM remains bit-exact at the default **1×** setting. This separate experimental variant offers a C140-only **8× Classic** build for listening comparison against the original Cardputer R6's much stronger PCM scaling. The original R6 uses different FM synthesis, so Classic does **not** mean bit-exact R6 sound.

- **Default**: `pio run -d cardputer -e m5stack-cardputer`
- **C140 Classic 8×**: `pio run -d cardputer -e m5stack-cardputer-classic`
- **Offline real VGM/VGZ 3-way A/B**: `python cardputer/tools/compare_vgm_audio.py "C:\\music\\sample.vgz" --seconds 30 --cardputer-gain 8 --zip`

The comparator processes your supplied file locally through the original Cardputer R6, shared Cardputer R7 and CoreS3 YM2151/C140 engines; it exports separate FM, C140 and mixed WAV tracks plus loudness-matched mixes and JSON results. Other VGM chips, physical DACs and speakers are not analyzed. No music files are uploaded, included or redistributed. CI uses a newly generated, noncommercial synthetic VGM command fixture rather than any game recording.

**Experimental, unmerged; neither Cardputer R6 nor CoreS3 Production is changed.**

---

### Cardputer R7 — shared vgmM5lib development edition

Cardputer now uses the **same vgmM5lib sound engines as CoreS3 R67**, including FM, C140/C352 PCM, PSG, SSG, SCC and the R31 YM2413 bridge. Cardputer-specific keyboard/UI, microSD, online mode and MDX integration are retained.

**Build on Windows (no flashing):** `cardputer\\RUN_THIS_CARDPUTER_R7_VGMM5LIB_BUILD_ONLY.cmd`

The 3-way 44.1kHz PCM comparison includes the **original Cardputer R6**, the migrated Cardputer R7 and CoreS3. The R7 and CoreS3 shared core output is bit-exact for the identical synthetic YM2151 and 24-voice C140 inputs tested; the original R6 output differs. Speaker/DAC acoustics are not compared.

- [R7 development branch](https://github.com/Layer812/vgmM5/tree/dev/cardputer-r7-vgmm5lib-cores3-compare-20261107)
- [Original R6 preserved branch](https://github.com/Layer812/vgmM5/tree/stable/cardputer-r6-unified028-20261026)
- [Native build and 3-way PCM comparison](https://github.com/Layer812/vgmM5/actions/runs/42328480024)
- [Web Burner (original R6 published edition)](https://burner.m5stack.com/METQC2)

---

### Cardputer R6 — previous edition

The **M5Stack Cardputer R6** VGM/VGZ and MDX edition uses the protected v0.98 Cardputer baseline with updated sound calculations and library dependencies.

### M5Burner

**Cardputer R6 share code: `rqz95QwaAsM6bN3O`**

This code was supplied by the project author. The M5Burner-hosted image has not been independently compared by SHA-256 with the native GitHub CI binary.

### What's new — October 26, 2026

- **M5Unified 0.2.28 / M5GFX 0.2.35**
- Improved **YM2151** pitch, modulation, KEY ON and new-note phase behavior
- Safer **C140 / C352** PCM calculations

### Source and validation

See the [R6 source branch](https://github.com/Layer812/vgmM5/tree/stable/cardputer-r6-unified028-20261026), [native ESP32-S3 CI PASS](https://github.com/Layer812/vgmM5/actions/runs/40204050559), [YM2151 PR #11](https://github.com/Layer812/vgmM5/pull/11), and [M5Unified PR #12](https://github.com/Layer812/vgmM5/pull/12). The [previous R5 snapshot](https://github.com/Layer812/vgmM5/tree/stable/cardputer-r5-pre-r6-20261026) remains available. Native compilation and host tests passed; no independent R6 physical speaker/long-duration playback result is claimed here.

Run `cardputer\RUN_THIS_CARDPUTER_BUILD_ONLY.cmd` from the repository root to compile without flashing. The build stays on Espressif32 6.13.0 / Arduino 2.0.17 and pins the new M5 libraries.

The original six online-catalogue `.bin` files are absent from GitHub. Without all six valid files, the build script creates empty placeholders: microSD local playback code is retained, but the online catalogue will be empty. Never redistribute third-party databases without permission.

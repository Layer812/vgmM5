# vgmM5 — M5Stack Tab5 edition (R61 FIX1)

[日本語](#日本語) / [English](#english)

## 日本語

**2026年10月25日時点のTab5対応版**です。既存のAtomS3R／Cardputer／CoreS3版には影響を与えない独立した開発系列です。

### 動作環境・機能
- M5Stack Tab5（ESP32-P4、microSD、タッチパネル、内蔵Speaker）
- VGM/VGZおよびMDXのローカルファイル再生
- 再生・停止、ファイル／フォルダ選択、曲情報・チャンネル表示・スペクトラム
- SDカード内のカバー画像（JPEG / PNG / BMP。画像高速化は未実施）
- ST7121、ST7123、従来のILI9881Cパネル向け互換初期化（全個体での検証は未完了）
- オンラインモード、USBマスストレージ、USBキーボード操作はこの版の対応対象外

### M5Burnerで入手
**Tab5用シェアコード:** `rx5H5eOnM45ed4eX`

[M5Stack公式Tab5ページ](https://docs.m5stack.com/ja/core/Tab5)からM5Burnerなどの情報を参照してください。

このコードはプロジェクト提供の値です。**M5Burnerから実際にダウンロードされるBINと、ここに記載するR61 FIX1のローカル実機ビルドBINが同一か、こちらではハッシュ照合できていません。** 他機種用のコードをTab5に使用しないでください。フルフラッシュ書込は既存データや別アプリを消す可能性があります。既存領域を保持する必要がある場合、一般的なフル書込を避けてください。

### 既知の問題と検証範囲
- **クリックノイズ未解決。** 同一のR61 FIX1でも激しいクリックノイズが再発し、電源を抜いて時間を置いてから起動すると消えた例があります。電源・I2S・ES8388・アンプなどを調査対象としていますが、原因は**未確定**です。
- スピーカー待機時間やFIRなど以前の変更に、クリックノイズを根本解決したという因果関係は証明されていません。
- R61 FIX1ではWindows/PlatformIOでのESP32-P4ネイティブビルドと、既知のTab5への**app-only書込**の成功記録があります。長時間再生、全音源・全パネル個体の完全な検証は未了です。
- 音源計算の安全性に関する改善（YM2151高音域の範囲外変換、C352等）は、クリックノイズの問題とは別に扱います。

### ソース・ビルド情報
- 版：R61 FIX1（R58 Speaker待機、R59 YM2151安全化、R60液晶互換）
- pioarduino platform-espressif32：`54.03.21`
- M5Unified：`e125e73f28b6b83e621e48df432af426bb2ff935`
- M5GFX：`347a23274081a105cb9fc0fc3bbf0e15c78e79c3`（0.2.33）。PlatformIOの依存解決で0.2.34となる場合もあるため、LCDの差分は該当公式Git blobを検証して適用。
- 実機でビルドされた**app-only** BINのSHA-256：`099d26838d9c84edc32eb1855489c725e8c5e897e749d29cab509abd140beede`。**このSHAはM5Burnerのシェア先BINを表すものではありません。**
- **Tab5版の全ソース公開:** [R61 FIX1 の完全ソースZIP](vgmM5_Tab5_R61_FIX1_source.zip)（84個のファームウェアソース＋SHA-256検査付きビルド専用スクリプト）。ZIP内の `tab5/` を展開し、`RUN_THIS_TAB5_BUILD_ONLY.cmd` を実行します。既存のルート `src/` は旧機種用であり、Tab5の実機ビルドを再現するソースではありません。

### 権利と収録物
楽曲データや第三者のゲーム／サウンドトラックは含まれていません。既存プロジェクトの[ライセンス・利用ライブラリ表記](../README.md#license)をご参照ください。Tab5版の出典・依存ライセンスについてはソース公開時にも個別に明記します。

---

## English

**M5Stack Tab5 edition as of October 25, 2026.** This branch of development targets the ESP32-P4 Tab5 without replacing the existing AtomS3R/Cardputer/CoreS3 implementation.

**Features:** Local VGM/VGZ and MDX playback from microSD; touch-based file navigation and playback; channel/spectrum display; SD-based JPEG/PNG/BMP cover art (no speed optimization); panel compatibility settings for ST7121/ST7123/ILI9881C. Online playback, USB mass-storage mode, and keyboard controls are not provided by this edition.

**M5Burner share code:** `rx5H5eOnM45ed4eX`. Supplied by the project author; the downloaded firmware's SHA has not been independently matched to the locally built R61 FIX1 image. Full-flash installation can erase existing data or firmware.

**Known unresolved issue:** Severe intermittent clicking/crackling recurred on the same R61 FIX1 firmware; in one instance it disappeared after fully removing power and waiting. The root cause is unknown. Audio scheduling edits must not be described as a proven fix, and long-term stability is not certified.

**Verification:** An ESP32-P4 native build and guarded app-only flash on one Tab5 succeeded. Locally built app image SHA-256: `099d26838d9c84edc32eb1855489c725e8c5e897e749d29cab509abd140beede` (not verified against the M5Burner download).

**Source availability:** [Download the full R61 FIX1 Tab5 source ZIP](vgmM5_Tab5_R61_FIX1_source.zip) (84 firmware source files plus source-hash checks and a build-only wrapper). Extract the `tab5/` folder and run `RUN_THIS_TAB5_BUILD_ONLY.cmd` on Windows with PlatformIO. The root `src/` directory remains the earlier M5Stack edition, not the Tab5 build. Third-party code and media rights are not implied by the project's MIT notice.

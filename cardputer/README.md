# vgmM5 — M5Stack Cardputer R6

[日本語](#日本語) / [English](#english)

## 日本語

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

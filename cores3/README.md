# vgmM5 — M5Stack CoreS3 R6

[日本語](#日本語) / [English](#english)

## 日本語

M5Stack CoreS3（ESP32-S3）向けのVGM/VGZ・MDXプレイヤーです。microSDの楽曲再生、タッチ操作、曲情報・チャンネル／スペクトラム表示に対応します。

### M5Burner

**CoreS3用シェアコード：`wEBeKwOKFsqAmlsu`**

このコードは**CoreS3内蔵スピーカー**と、外部の**M5Stack Module Audio（ES8388）接続時**の両方で使用する版です。2026年10月26日に、両方の構成で実機動作を確認しました。

Module Audioを使用する場合は、従来どおり**モジュール内部のDIPスイッチをB**に設定してください。外部ES8388のI²S出力経路とコーデック初期化コードは元のまま維持しています。

### 更新内容（2026年10月26日）

- **M5Unified 0.2.28／M5GFX 0.2.35に対応**
- **YM2151の音程・変調・KEY ON・位相などの音源処理を修正**
- **C140/C352のPCM計算を改善**

### ソースと検証

- [CoreS3 R6安定版ソース](https://github.com/Layer812/vgmM5/tree/stable/cores3-r6-unified028-20261026)
- [ファームウェアソース](../src/)
- [ESP32-S3ネイティブビルド・回帰検証 PASS](https://github.com/Layer812/vgmM5/actions/runs/40206819224)
- [R6のGitHub統合履歴（PR #15）](https://github.com/Layer812/vgmM5/pull/15)
- [旧CoreS3 v0.98の保管ブランチ](https://github.com/Layer812/vgmM5/tree/stable/cores3-v098-pre-r6-20261026)

M5Burner経由で配信されるイメージとGitHubネイティブビルドのBINは、ハッシュ一致を独立に確認したものではありません。フルフラッシュ書込では以前の設定やデータが上書きされる可能性があります。

Tab5／Cardputer／AtomS3RのファームウェアはこのCoreS3更新の対象外です。

---

## English

The CoreS3 R6 edition of vgmM5 supports VGM/VGZ and MDX local playback from microSD, touch controls and channel/spectrum display.

### M5Burner

**CoreS3 share code: `wEBeKwOKFsqAmlsu`**

The **same R6 firmware** supports the CoreS3 **built-in speaker** and the external **M5Stack Module Audio (ES8388)**. Both configurations were confirmed on physical hardware by the project author on October 26, 2026. When using Module Audio, select **DIP switch position B**. The existing module I²S/ES8388 audio path is preserved.

### Release changes — October 26, 2026

- Updated to **M5Unified 0.2.28 / M5GFX 0.2.35**
- YM2151 pitch, modulation, KEY ON and note phase corrections
- Improved C140/C352 signed PCM arithmetic

### Source and verification

See the [CoreS3 R6 stable branch](https://github.com/Layer812/vgmM5/tree/stable/cores3-r6-unified028-20261026), [native ESP32-S3 CI (PASS)](https://github.com/Layer812/vgmM5/actions/runs/40206819224), and [integrated PR #15](https://github.com/Layer812/vgmM5/pull/15). The [previous CoreS3 v0.98 branch](https://github.com/Layer812/vgmM5/tree/stable/cores3-v098-pre-r6-20261026) is retained for reference.

The M5Burner-distributed image has not been independently compared by SHA-256 with the GitHub CI firmware. Full-device flashing may overwrite existing data.

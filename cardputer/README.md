# vgmM5 — M5Stack Cardputer R67

[日本語](#日本語) | [English](#english)

## 日本語

Cardputer向け **vgmM5 R67**。CoreS3 R67と同じ `vgmM5lib` 音源コアを採用し、240×135 LCDの表示をリニューアルしました。

### R67の更新内容

- **音源共通化**：YM2151などのFM、C140/C352、PSG・SSG・SCC、YM2413（R31）を共通ライブラリへ統合
- **読みやすい画面**：起動メニュー・曲一覧・ファイル一覧の文字サイズを拡大。画面下の操作案内も整理
- **再生中の専用表示**：曲名・使用音源・ONLINE／MICRO SD・音量・経過時間を表示
- **再生バー**：オンライン楽曲は進行状況、ローカル楽曲は再生中を示すアニメーション
- **音量バランス**：標準版はCoreS3と同じ音量・演算。ClassicはCardputerのC140音量のみ8倍にする別ビルド
- microSD、MDX/PDX、オンラインカタログ、キーボード操作を維持

### 操作

| 場面 | 操作 |
|---|---|
| 一覧を移動 | `;` 上 ／ `.` 下 ／ `,` 前のページ ／ `/` 次のページ |
| 曲を選ぶ・再生 | `Enter` または `Space` |
| 再生中に停止 | `Space` |
| 再生中の音量 | `+`／`-` |
| オンライン再生中の曲移動 | `;` 前曲 ／ `.` 次曲 |
| オンラインの連続再生 | `A` 全曲 ／ `R` ランダム |
| 戻る・メニュー | `B` 戻る ／ `M` メニュー |

### Windows / PlatformIOでビルド

リポジトリのルートから実行します。ESP32-S3向けネイティブビルドで、書き込みは行いません。

```bat
cd cardputer
RUN_THIS_CARDPUTER_R67_BUILD_ONLY.cmd
```

PlatformIOから直接ビルドする場合：

```bat
pio run -d cardputer -e m5stack-cardputer
pio run -d cardputer -e m5stack-cardputer-classic
```

- `m5stack-cardputer`：R67標準（CoreS3と同一のC140出力）
- `m5stack-cardputer-classic`：R67 Classic（CardputerのC140のみ8倍）
- オンラインカタログ用データベース6ファイルは、利用者が持つデータを `cardputer/src/` に配置できます。データなしでもmicroSDからローカル再生可能です。

### ファームウェアとWeb Burner

- [Cardputer R67ソース](https://github.com/Layer812/vgmM5/tree/main/cardputer)
- [Cardputer R67ビルドと音源検証](https://github.com/Layer812/vgmM5/actions)
- [M5Stack Web Burner — Cardputer公開版](https://burner.m5stack.com/METQC2)

**Web Burnerのリンクは従来掲載されていたCardputer R6版です。** R67の新バイナリとは別です。

### 音源比較

R67標準のYM2151・C140生PCMおよびミックス出力は、同一のテスト入力に対してCoreS3 R67と完全一致しました。従来のCardputer R6とはC140音量・FM内部演算に違いがあります。比較ツール・試験用音声は開発ブランチの [PR #61](https://github.com/Layer812/vgmM5/pull/61) / [PR #62](https://github.com/Layer812/vgmM5/pull/62) に残しています。

元のCardputer R6：[固定ブランチ](https://github.com/Layer812/vgmM5/tree/stable/cardputer-r6-unified028-20261026)。

---

## English

**vgmM5 R67 for M5Stack Cardputer** shares the same `vgmM5lib` sound core as CoreS3 R67 and introduces a refreshed, readable 240×135 display.

### What's new

- Shared YM2151/FM, C140/C352, PSG, SSG, SCC and R31 YM2413 engines
- Larger text throughout the file browser and menus, improved readable key hints
- Now-playing screen with track title, sound chip, ONLINE / MICRO SD, volume and elapsed time
- Progress indicator for online tracks; live activity indicator for local tracks
- Standard build matches CoreS3 sound-core PCM levels; optional Classic build boosts only Cardputer C140 by 8×
- Existing microSD, MDX/PDX, online catalog and keyboard operations retained

### Keyboard

| Action | Key |
|---|---|
| Navigate lists | `;` up, `.` down, `,` page up, `/` page down |
| Select/play | `Enter` or `Space` |
| Stop while playing | `Space` |
| Volume | `+` / `-` |
| Online previous/next track | `;` / `.` |
| Online play all / random | `A` / `R` |
| Back / menu | `B` / `M` |

### Build on Windows

```bat
cd cardputer
RUN_THIS_CARDPUTER_R67_BUILD_ONLY.cmd
```

Or build both variants using PlatformIO (no flash):

```bat
pio run -d cardputer -e m5stack-cardputer
pio run -d cardputer -e m5stack-cardputer-classic
```

The six optional online-catalog `.bin` files may be placed in `cardputer/src/` if available. microSD local playback is supported without them.

### Links

- [R67 Cardputer source](https://github.com/Layer812/vgmM5/tree/main/cardputer)
- [GitHub Actions native firmware builds](https://github.com/Layer812/vgmM5/actions)
- [Cardputer Web Burner — previously published R6 edition](https://burner.m5stack.com/METQC2)
- [Original R6 protected source](https://github.com/Layer812/vgmM5/tree/stable/cardputer-r6-unified028-20261026)

With the same test input, standard R67 and CoreS3 R67 produce bit-identical YM2151, C140 and pre-speaker mixed PCM. The original R6 differs in C140 level and some FM synthesis details; compare the experimental sound tests in [PR #61](https://github.com/Layer812/vgmM5/pull/61) and [PR #62](https://github.com/Layer812/vgmM5/pull/62).

# vgmM5 — M5Stack Cardputer R67

[日本語](#日本語) | [English](#english)

## 日本語

**Cardputer R67はmicroSD専用のVGM/VGZ・MDXプレイヤーです。** 起動すると直接ファイル一覧を表示します。オンライン／ローカルの選択画面、Wi-Fi設定、クラウドカタログはありません。オンラインDBの6ファイルも不要です。

### R67の機能

- CoreS3 R67と共通の `vgmM5lib`：YM2151などのFM、C140/C352などのPCM、PSG・SSG・SCC、YM2413（R31）
- 240×135 LCDに合わせた大きな文字のファイル一覧、曲名・音源・音量・経過時間を示す再生画面
- microSDのフォルダを移動してVGM/VGZまたはMDX/PDXを再生
- 標準版（CoreS3と同じ音源出力）、Classic版（CardputerのC140出力のみ8倍）を選択可能
- 元のCardputer R6は[固定ブランチ](https://github.com/Layer812/vgmM5/tree/stable/cardputer-r6-unified028-20261026)に保持

### キーボード操作

| ファイル一覧 | 操作 |
|---|---|
| 上／下 | `;` ／ `.` |
| ページ移動 | `,` ／ `/` |
| ファイル・フォルダを選択 | `Space` |
| フォルダを戻る | `..` を選択 |
| 全曲連続再生 | `A` |
| 操作ヘルプ | `M` |
| ルートで再読み込み | `B`／Backspace |
| 音量 | `+`／`-` |
| 再生中の停止 | `Space` |

microSDのルートにVGM/VGZ・MDXファイルを直接置いて使えます。フォルダに分類しても利用できます。MDXに対応するPDXは、従来のMDX読み込み仕様に合わせて配置してください。

### Windowsでビルド（書き込みなし）

リポジトリのルートから：

```bat
cd cardputer
RUN_THIS_CARDPUTER_R67_BUILD_ONLY.cmd
```

Classic（C140 8倍）の場合：

```bat
cd cardputer
RUN_THIS_CARDPUTER_R67_CLASSIC_BUILD_ONLY.cmd
```

PlatformIO CLIからは次のようにビルドできます。

```bat
pio run -d cardputer -e m5stack-cardputer
pio run -d cardputer -e m5stack-cardputer-classic
```

どちらもESP32-S3ネイティブファームウェアを生成するだけです。データベース、バックアップ、デバイス書き込みを要求しません。

### 公開・音源比較

- [Cardputer R67ソース（main）](https://github.com/Layer812/vgmM5/tree/main/cardputer)
- [GitHub Actions／ネイティブビルド](https://github.com/Layer812/vgmM5/actions)
- [M5Stack Web Burner — 従来のCardputer掲載版](https://burner.m5stack.com/METQC2)

**Web BurnerのCardputerリンクは従来版です。** GitHub R67の新しいバイナリとは別です。

標準R67とCoreS3 R67のYM2151・C140・ミックスのPCMは、同一のテスト入力でビット単位で一致しています。元のR6とはC140の増幅率やFM内部演算に違いがあります。比較ツールと検証ソースは開発履歴の[PR #61](https://github.com/Layer812/vgmM5/pull/61)・[PR #62](https://github.com/Layer812/vgmM5/pull/62)にあります。

---

## English

**Cardputer R67 is a microSD-only VGM/VGZ and MDX player.** It starts directly in the file browser, with **no Online/Local chooser, Wi-Fi setup, cloud catalog or required DB assets**.

### Features

- The same portable `vgmM5lib` FM/PCM/PSG/SSG/SCC and YM2413 core used by CoreS3 R67
- Clear, larger-text 240×135 file browser and playback screen showing title, sound chip, volume and elapsed time
- microSD directory navigation and playback of VGM/VGZ or MDX with PDX support
- Standard mode with CoreS3-equivalent output, or optional C140-only Classic 8× build
- [Preserved original Cardputer R6 source](https://github.com/Layer812/vgmM5/tree/stable/cardputer-r6-unified028-20261026)

### Keys

| File browser | Key |
|---|---|
| Up/down | `;` / `.` |
| Page up/down | `,` / `/` |
| Select/play | `Space` |
| Go to parent directory | Select `..` |
| Play all | `A` |
| Help | `M` |
| Rescan from root | `B` / Backspace |
| Volume | `+` / `-` |
| Stop playback | `Space` |

### Build on Windows (no flash)

```bat
cd cardputer
RUN_THIS_CARDPUTER_R67_BUILD_ONLY.cmd
```

For optional C140 Classic 8×:

```bat
cd cardputer
RUN_THIS_CARDPUTER_R67_CLASSIC_BUILD_ONLY.cmd
```

Or run:

```bat
pio run -d cardputer -e m5stack-cardputer
pio run -d cardputer -e m5stack-cardputer-classic
```

No online DB, backup or flashing is required. Place your own VGM/VGZ/MDX files on the microSD card.

### Links

- [Cardputer R67 source](https://github.com/Layer812/vgmM5/tree/main/cardputer)
- [Native builds](https://github.com/Layer812/vgmM5/actions)
- [Cardputer Web Burner — previously published edition](https://burner.m5stack.com/METQC2)

The Web Burner link still targets the previously published Cardputer image, **not** the new R67 firmware. Identical YM2151/C140 register sequences produce bit-exact pre-speaker PCM for standard Cardputer R67 and CoreS3 R67. Audio A/B tools remain in [PR #61](https://github.com/Layer812/vgmM5/pull/61) and [PR #62](https://github.com/Layer812/vgmM5/pull/62).

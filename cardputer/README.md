# vgmM5 — M5Stack Cardputer R67

[日本語](#日本語) | [English](#english)

## 日本語

**Cardputer R67はmicroSD専用のVGM/VGZ・MDXプレイヤーです。** 起動すると直接ファイル一覧を表示します。オンライン／ローカルの選択画面、Wi-Fi設定、クラウドカタログはありません。オンラインDBの6ファイルも不要です。

### R67の機能

- CoreS3 R67と共通の `vgmM5lib`：YM2151などのFM、C140/C352などのPCM、PSG・SSG・SCC、YM2413（R31）
- **2カラムのファイラ**：左に5行の曲・フォルダ一覧、右にサムネイル＋形式・容量・音源情報。フォルダは水色の名前＋末尾 `/` 表示
- タイトル横の **`M:HELP`** で操作説明を表示／非表示。常時表示される下部の操作説明は廃止
- 再生中は曲名・音源・音量・経過時間を表示
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
| 再生を停止して一覧へ戻る | **`BS`（Backspace）** |
| VGMの一時停止・再開 | `P`（画像の下に `PAUSED` 表示） |

microSDのルートにVGM/VGZ・MDXファイルを直接置いて使えます。フォルダに分類しても利用できます。MDXに対応するPDXは、従来のMDX読み込み仕様に合わせて配置してください。

### サムネイル（省略可能）

microSDに以下のJPEG画像を置くと、右側のプレビューに表示します。

- 曲と同じ名前の画像：`DRAGON_SABER.vgm` に対して `DRAGON_SABER.jpg`（曲ごとの絵）
- フォルダ全体の画像：同じフォルダに `cover.jpg`（アルバム共通）
- フォルダの画像：フォルダ内に `cover.jpg`

画像は **64×64ピクセル以上のJPEG/PNG** を使用できます（最大2MB）。大きなJPEGもサムネイルサイズに縮小します。画像がない場合や読み込みに失敗した場合には、曲ごとに色と波形の異なるサムネイルを自動描画するため、画像ファイルを用意する必要はありません。**選択画面と再生画面は同じ画像**を表示します。

情報欄には形式・ファイルサイズを表示します。非圧縮VGMにYM2151/C140のヘッダ情報がある場合には音源名も表示します。VGZの内部音源名は選択時には展開しません（`COMPRESSED` と表示）。

**再生画面**は曲名・音源とサムネイルのみのシンプルな表示です。サムネイルの下に `NOW PLAYING`、`P` で一時停止中は `PAUSED` と表示します。`MICRO SD`、`SPACE STOP`、`VOL`、下部進行バーは表示しません。`BS` を押せば再生を止めて同じフォルダのファイル一覧へ戻ります。

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
- **Two-column 240×135 browser**: five readable filename/folder rows on the left, a 64×64 optional cover and format/size/chip information on the right
- Folders are cyan and end in `/`; **`M:HELP`** in the header opens/closes the key guide, so no permanent bottom instruction strip is needed
- Readable playback screen with title, chip, volume and elapsed time
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
| Stop and return to file browser | **`BS` (Backspace)** |
| Pause/resume VGM | `P` (shows `PAUSED` below cover) |

### Optional microSD thumbnails

Put `song.jpg` or `song.png` next to `song.vgm`, or a shared `cover.jpg` / `cover.png` in the same folder (also supports subfolder artwork). Artwork up to 2 MB is supported; JPEG images are downscaled. If no image exists or decoding fails, each track receives its own colorful waveform cover. **The browser and now-playing view use the same image.** No external DB or internet connection is required.

The right pane shows file type and size. For uncompressed VGM it also detects YM2151/C140 header clocks when present. VGZ is marked compressed without decompression during browsing.

The now-playing screen has only the track title, chip and artwork with `NOW PLAYING` / `PAUSED` below it. The old SD, stop/volume footer and activity bar are gone. `BS` immediately stops playback and returns to the same directory.

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

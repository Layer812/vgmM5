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
| 全曲連続再生を開始 | `A`（ファイラ） |
| 操作ヘルプ | `M` |
| ルートで再読み込み | `B`／Backspace |
| 音量 | `+`／`-` |
| 再生中の連続再生ON/OFF | **`A`**（曲は止めず、次曲への自動移行だけ切替） |
| 再生中の停止／連続再生解除 | `Space` |
| 再生を停止・連続再生解除して一覧へ戻る | **`BS`（Backspace）** |
| VGMの一時停止・再開 | `P`（画像の下に `PAUSED` 表示） |

microSDのルートにVGM/VGZ・MDXファイルを直接置いて使えます。フォルダに分類しても利用できます。MDXに対応するPDXは、従来のMDX読み込み仕様に合わせて配置してください。

### サムネイル — microSDのPNG/JPEGから自動生成

アルバムフォルダに通常のPNG/JPEG画像を入れておけば、**最初にそのフォルダへ入った時だけ**Cardputerが自動的に64×64へ縮小し、microSDの `/VGMM5/XXXXXXXX.RGB` に保存します。**事前にWindowsで画像を変換する必要はありません。**

- 対応する名前：`cover.png`、`cover.jpg`、`folder.png`、`フォルダ名.png` など。アルバムフォルダ内で別の名前が付いたPNG/JPGも、ファイル一覧を読み込む際に見つかった最初の画像を使用します。親フォルダの `フォルダ名.png` も候補になります。
- 縮小時には画像の縦横比を維持します。PNG/JPEGは最大4MiB、縦横各4096pxまでを対象とします。読み取りに失敗した画像は代替グラフィックを表示します。
- **カーソル移動・再生中にPNG/JPEGを展開しません。** 最初に生成した8,192バイトのRGB565画像を4行（512バイト）単位で読み出します。曲や元画像は変更しません。
- キャッシュはアルバムフォルダ単位です。フォルダ内で曲を切り替えても同じジャケットを使用します。ファイル名・容量の表示と`M:HELP`は従来どおりです。
- 特に大きい画像を一括変換したい場合は、補助のWindowsツールも利用できます（`E:`はmicroSDのドライブ名に変更）。

```bat
MAKE_CARDPUTER_THUMBS.cmd E:\
```

画像表示は実機検証中です。既存のR67正式版にはまだ統合していません。

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
| Start play all | `A` in file browser |
| Help | `M` |
| Rescan from root | `B` / Backspace |
| Volume | `+` / `-` |
| Toggle auto-play while playing | **`A`**, keeps current song playing |
| Stop and cancel auto-play | `Space` |
| Stop, cancel auto-play, return to file browser | **`BS` (Backspace)** |
| Pause/resume VGM | `P` (shows `PAUSED` below cover) |

### Album artwork — automatic PNG/JPEG thumbnail preparation

Put an ordinary PNG or JPEG in an album folder on the microSD card. **When entering the folder for the first time**, Cardputer scales it to 64×64, preserves its aspect ratio and saves an 8,192-byte RGB565 thumbnail under `/VGMM5/XXXXXXXX.RGB`. **You do not need a PC conversion step.**

- Typical names: `cover.png`, `cover.jpg`, `folder.png`, `albumname.png`, plus the first other PNG/JPEG found while enumerating the directory once. `albumname.png` next to its folder is supported.
- Source images up to 4 MiB and 4096×4096 are eligible. Unsupported or undecodable files use the built-in fallback tile.
- **No image decompression, album enumeration or artwork sprite allocation on cursor movement or during playback.** The cached RGB565 thumbnail is streamed using a four-row (512-byte) buffer; original files remain unchanged.
- Album artwork is shared between tracks in one folder. This is intentionally an album-level cover, not per-track artwork.
- Optional Windows batch preconversion is still available for bulk processing; replace `E:` with your SD drive letter.

```bat
MAKE_CARDPUTER_THUMBS.cmd E:\
```

Native builds have CI coverage; on-device artwork rendering remains to be confirmed before merging into R67 production.

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

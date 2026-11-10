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

### サムネイル — カーソル移動中の画像・フォルダ検索なし

microSDの各アルバムフォルダにある大きな `フォルダ名.png`／`cover.png`／JPEGは、**Cardputerでは直接開きません**。画像を探すためにフォルダを一覧取得する処理もありません。

**元のPNGが大きい場合**はWindowsでmicroSDを挿入し、一度だけ次のコマンドを実行します（`E:` は実際のドライブに変更）。**SDドライブ直下**を指定してください。`E:\MUSIC` などのサブフォルダを指定するとハッシュが一致しなくなるため、ツールはエラーにします。

```bat
MAKE_CARDPUTER_THUMBS.cmd E:\
```

画像を64×64のRGB565（各8,192バイト）に変換し、フォルダの**フルパスのFNV-1aハッシュ8桁**を使って `/.VGMM5/XXXXXXXX.RGB` に保存します。元画像・音楽ファイルは変更しません。ハッシュ済みの画像は再利用します。

- **カーソル移動時**：ハッシュ名でキャッシュを直接読むだけ。JPEG/PNGの探索・デコード、選択ファイルの再オープン、フォルダの再一覧取得を行いません。
- **同じフォルダの別の曲**：右側の画像を再描画せず、形式・ファイル容量だけ更新します。
- **再生画面**：ハッシュ画像を4行（512バイト）ずつ読み出し。画像用のスプライト・常駐ビットマップは不要です。
- **キャッシュがないとき**：フォルダに入ったときの1回だけ、`cover64.png` または `フォルダ名.png` などの**64×64・透過なし・小さなPNG**を決め打ちの名前で探してキャッシュを作ります。大きなPNGは端末では変換せず、Windowsツールで事前に変換してください。どちらもなければすぐ内蔵サムネイルになります。
- **曲の一覧取得**：フォルダへ入ったときに一度だけ行います（ファイル名の表示には必要です）。その際に容量も記録し、カーソル移動では再取得しません。Bキーで明示的に再読み込みできます。

フォルダのサムネイルは、ファイル一覧と再生画面で共通です。ファイルごとのジャケット検索は行わず、アルバム単位で高速表示します。

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

### Album thumbnails without directory scans on cursor movement

Large `foldername.png`, `cover.png` or JPEG album images are **not opened or enumerated on the Cardputer**. Run the included Windows helper once with the microSD in your PC (replace `E:` with your drive). Specify the **SD drive root**, never `E:\MUSIC` or another subfolder, because the hashed filenames would not match:

```bat
MAKE_CARDPUTER_THUMBS.cmd E:\
```

This converts existing folder artwork to **64×64 RGB565, exactly 8,192 bytes**, stored at `/.VGMM5/XXXXXXXX.RGB` using an **eight-digit FNV-1a hash of the normalized full album folder path**. Originals and music are unchanged; pre-existing caches are reused.

- Cursor movement opens only the deterministic hashed thumbnail path, without listing artwork folders, decoding PNG/JPEG or opening a selected music file to read its size.
- Moving between tracks in the same folder keeps the existing cover on screen and changes only the file type/size text.
- Playback streams the RGB565 cover in four-row (512-byte) chunks without an extra image sprite or retained bitmap.
- Missing cache: **only once on entering an album folder**, Cardputer can create the cache automatically from a **small 64×64 opaque RGB PNG** such as `cover64.png` or `foldername.png`. This does not run on cursor movement. Larger existing PNG/JPEG files still require the Windows tool; otherwise the built-in graphic appears immediately.
- The actual music-file listing is performed **once when entering a directory** (necessary to display filenames); file sizes are indexed then. B explicitly forces a rescan.

The same *album* image is used in the file browser and now-playing view; per-track art is not searched at runtime.

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

# R61 FIX1 historical build-only contract (archive)

**この手順は2026年10月25日の更新前のR61 FIX1用です。** 現行のTab5公開版（M5Unified 0.2.28／M5GFX 0.2.35／YM2151修正版）の再現ビルド手順ではありません。最新版については[Tab5 README](README.md)を参照してください。

`RUN_THIS_TAB5_BUILD_ONLY.cmd` runs `build_tab5.py` for the archived R61 FIX1 snapshot.

- Verifies original source SHA-256 before and after compilation, and checks the native BIN header.
- Uses original pioarduino `54.03.21`, M5Unified `e125e73f28b6b83e621e48df432af426bb2ff935`, and pinned M5GFX `0.2.33/0.2.34` compatibility overlay.
- Runs `pio pkg install`, the original vendor overlay, and `pio run` in `tab5-r61-speaker-revert`.
- Does not connect to or flash any device, and does not write the bootloader or partition table.
- Unexpected vendor source hashes or versions trigger a safe stop.

**現行M5Burnerシェアコード（Tab5用）：`8i9Cp1C6ufGpGC5J`。** M5Burner側の配信BINとGitHubビルドBINの同一性は未照合です。

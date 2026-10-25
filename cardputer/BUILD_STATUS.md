# Cardputer v0.98 separation status

- Source: exact original root `src/` snapshot, 45 source/header files, unchanged; tracked in `SOURCE_LOCK.json`.
- Build config: original `[env:m5stack-cardputer]` section, same board / libraries / compile flags / embedded DB references.
- Partition map: same Git blob as original root `partitions.csv`.
- Online DB: six binary assets missing in original public repository, so build-only runner creates empty, safe-offline placeholders if *all six* are missing.
- If full real DBs are provided, build sources remain original, though DB contents are not independently verified.
- Native-build baseline (GitHub Actions 40148306324): FAILED against floating Espressif32 8.0.0 / Arduino 3.3.12 because legacy Flash mmap APIs changed. No device flashed.
- **Native build PASS**: [GitHub Actions run 40148553881](https://github.com/Layer812/vgmM5/actions/runs/40148553881), Espressif32 6.13.0, Arduino 2.0.17. No device flash.
- Build output `firmware.bin`: 1,240,480 bytes; SHA-256 `9ebc9d4004947b3708943b72724fd5371ec030f68755c15fe12dd223347a64a7`.
- Build usage: 161,964 / 327,680 bytes of RAM (49.4%) and 1,240,117 / 6,225,920 bytes of app partition (19.9%).
- CI artifact: [BIN, ELF and logs](https://github.com/Layer812/vgmM5/actions/runs/40148553881/artifacts/12582105950).
- Device startup, audio quality and SD playback **not tested**; offline DB placeholder disables catalog content.
- Flash: **none**; no app-only/whole-flash script is included.
- R61 FIX1 Tab5: unchanged and not used as the base for this snapshot.

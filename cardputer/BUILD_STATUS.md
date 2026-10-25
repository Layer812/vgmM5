# Cardputer v0.98 separation status

- Source: exact original root `src/` snapshot, 45 source/header files, unchanged; tracked in `SOURCE_LOCK.json`.
- Build config: original `[env:m5stack-cardputer]` section, same board / libraries / compile flags / embedded DB references.
- Partition map: same Git blob as original root `partitions.csv`.
- Online DB: six binary assets missing in original public repository, so build-only runner creates empty, safe-offline placeholders if *all six* are missing.
- If full real DBs are provided, build sources remain original, though DB contents are not independently verified.
- Native-build baseline (GitHub Actions 40148306324): FAILED against floating Espressif32 8.0.0 / Arduino 3.3.12 because legacy Flash mmap APIs changed. No device flashed.
- Build compatibility correction: pin Espressif32 platform 6.13.0 (Arduino 2.0.17, ESP-IDF 4.4.7) without changing the 45 protected Cardputer source files. Build result for the pinned platform is pending CI.
- Flash: **none**; no app-only/whole-flash script is included.
- R61 FIX1 Tab5: unchanged and not used as the base for this snapshot.

- AUDIO_R1 development branch: two guarded engine source modifications; native build pending, physical test not performed. The main-branch Cardputer v0.98 build already passed.

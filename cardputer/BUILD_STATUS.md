# Cardputer v0.98 separation status

- Source: exact original root `src/` snapshot, 45 source/header files, unchanged; tracked in `SOURCE_LOCK.json`.
- Build config: original `[env:m5stack-cardputer]` section, same board / libraries / compile flags / embedded DB references.
- Partition map: same Git blob as original root `partitions.csv`.
- Online DB: six binary assets missing in original public repository, so build-only runner creates empty, safe-offline placeholders if *all six* are missing.
- If full real DBs are provided, build sources remain original, though DB contents are not independently verified.
- Build result: **ESP32-S3 native build not executed here** (PlatformIO toolchain unavailable).
- Flash: **none**; no app-only/whole-flash script is included.
- R61 FIX1 Tab5: unchanged and not used as the base for this snapshot.

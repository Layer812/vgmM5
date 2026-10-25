# Cardputer R5 (YM2151) integration status — 2026-10-25

## Baseline preservation

- Original v0.98 45-source snapshot: `cardputer/SOURCE_LOCK.json` remains unchanged.
- R5 source differences: only `fm_engine.c`, `pcm_engine.c` and added `sound_safe_math.h`; changes are individually pinned in `SAFETY_PATCH_LOCK.json`.
- Original `cardputer/partitions.csv`, root `src/`, Tab5 R61 FIX1, Speaker/SD/Wi-Fi paths unchanged.
- Six offline-placeholder catalogue DBs are generated only when no valid catalogue files are present. Online catalogue is not supplied.

## Changes retained in R5

1. R1: defined YM2151 float-to-uint32 phase wrap and remove signed PCM left shifts in C140/C352.
2. R2: convert YM2151 DT2 pitch offsets to frequency ratios.
3. R3: decode YM2151 KF as raw register bits 7..2.
4. R4: correct dedicated YM2151 four-operator connection topology.
5. R5: remove duplicated YM2151 inter-operator FM modulation x2. No change to generic FM paths.

## Verification

- ESP32-S3 native PlatformIO build: PASS, GitHub Actions [run 40186870273](https://github.com/Layer812/vgmM5/actions/runs/40186870273).
- Source lock and host ASan/UBSan arithmetic tests: PASS in the same run.
- Offline 2-song ymfm A/B: R5 is judged more natural by the user; this is a **host-rendered FM core comparison**.
- Cardputer physical app-only flash and readback: PASS for **R1 only**, not R5.
- R5 physical flash, speaker reproduction, real-time load/underruns and long-term stability: NOT yet tested. No automatic flashing or release approval is implied.
- Main integration is source-only. Do not substitute the R5 binary for a verified physical release without further evidence.

## Other platforms

X68KTab5 already includes no-double-modulation (R33), correctly extracted KF (R38), DT2 ratios (R36), and phase step safety (R59/R61). Its optimized native 62.5kHz synthesis/FIR and guest YM2151 timekeeping differ from Cardputer; never drop in the Cardputer FM source. Evaluate YM2151 operator routing separately, without altering guest IRQ, WRITE BUSY, MIDI, audio queue or partitions.

vgmM5 Tab5 R61 FIX1 remains the protected audio baseline. R5 improvements require a separate verified backport based on the **exact R61 production source**, not the old v0.98 root.

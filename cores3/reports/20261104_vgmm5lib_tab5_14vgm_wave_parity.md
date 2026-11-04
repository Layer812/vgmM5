# CoreS3 vgmM5lib vs Tab5 R67 — per-chip WAV comparison (2026-11-04)

## Status and boundaries
- **ESP32-S3 native build PASS**: [Actions 41817868783](https://github.com/Layer812/vgmM5/actions/runs/41817868783), with `vgmM5lib` actually linked once, M5Unified 0.2.28/M5GFX 0.2.35.
- The 19 vgmM5lib synthesis source files are **byte-identical** to official Tab5 R67. P4-specific audio tasks, display, SPM memory and DMA were excluded. **Tab5 not switched or modified.**
- Previously supplied test VGM set available in the working session: 01–04 and 06–15 (14 files), all VGM v1.71, 60.000s. **05 YM2413 original 60s VGM is unavailable and was not fabricated.** The YM2413 bridge nevertheless compiled for ESP32-S3.
- Two separate host binaries used identical VGM register events and the exact 44.1 kHz rate. Each 60s run covers 2,646,000 frames × stereo and hashes the **pre-mixer signed32 PCM**. All **14/14 pre-mixer hashes and first-8-second PCM WAV bytes matched exactly** at that common render rate.

| VGM | Chip | Raw core PCM | Tab model RMS dBFS | CoreS3 model RMS dBFS |
|---|---|---|---:|---:|
| 01 | SN76489 | identical | -29.13 | -37.66 |
| 02 | AY8910 | identical | -29.20 | -37.73 |
| 03 | SCC K051649 | identical | -25.27 | -33.79 |
| 04 | SCC+ K052539 | identical | -25.27 | -33.79 |
| 05 | YM2413 | **source file missing** | — | — |
| 06 | YM2612 | identical | -27.18 | -35.71 |
| 07 | YM2151, **common 44.1k only** | identical | -25.64 | -34.17 |
| 08 | YM3812 | identical | -29.26 | -37.78 |
| 09 | YM2203 | identical | -26.95 | -35.48 |
| 10 | YM2608 | identical | -27.10 | -35.62 |
| 11 | YM2610 | identical | -27.10 | -35.63 |
| 12 | MSM6258 | identical | -23.51 | -32.03 |
| 13 | SegaPCM | identical | -21.63 | -30.15 |
| 14 | C140 | identical | -33.30 | -41.83 |
| 15 | C352 | identical | -18.87 | -27.39 |

The table's **post-mixer** RMS numbers are *host models*, not physical captures. The modeled common /3 vs /8 master gain difference is ~**-8.52dB**, and these 14 VGM models had **0 16-bit clips**. Model includes the device's DC blocker and distinct peak limiting; not speaker, DAC, I2S, release queues or P4-only behavior.

## Important exception: YM2151 4 MHz / Tab5 R47
Tab5's **actual R67 app** selects **62,500 Hz native YM2151 synthesis with 25-tap FIR and R41 44.1 kHz resampling** at clock=4 MHz; CoreS3 continues direct 44.1 kHz synthesis. With user 07 YM2151 60s VGM, a separate host reproduction of these *different* app paths found first-8s waveform correlation **0.8857** and level-fit relative RMSE **46.4%**. Thus the raw shared-core equality under a common 44.1k rate does **not** prove final YM2151 waveform equality.

The separate Dragon Saber VGZ uses **3,579,545 Hz** YM2151; Tab5's R47 native selector is limited to exactly 4 MHz and does not apply to that mixed track.

## Mixed 100s Dragon Saber C140+YM2151 stress
- 4,410,000 stereo frames, **183,123 chip writes** (36,639 YM2151 + 146,484 C140); nine C140 ROM blocks.
- Max 22 active C140 voices by key-on at ~28.06s. The 28–36s A/B WAV comparison captures that dense region.
- Equal pre-mixer signed32 PCM hash between both engines, maximal combined raw peak **17,760**. The host's Tab5 master /6 and CoreS3 master /16 models recorded **0 clips** at the final signed16 PCM outputs across the full 100 seconds.
- This cannot rule out I2S hardware, codec-level distortion, scheduling, M5Unified routing, or differences not reproduced in host.

## Input limitations and next implications
- Original 02 AY fixture first mixer reg7 write is at 12s; lacks earlier initialization.
- 03 SCC/04 SCC+ fixtures output the same waveform; they do not stress SCC+ unique CH5 behavior.
- Original 12 MSM6258 is missing 0x90/0x91/0x92 stream setup; vgmM5 app still starts direct 0x95 streams, unlike libvgm reference (silent).
- Original 15 C352 VGM divider byte 0xD6=0; both Tab5 and shared core use default divider 288.
- Tab5 R49/R53 switches and R66-A sub-LSB dither and CoreS3 ES8388 output were not rendered as actual board output.
- Therefore for persistent CoreS3 tone differences, look **after the shared synth cores**: 4 MHz YM2151 R47 path when relevant, /3 vs /8 mixing, audio output clocks/queues, ES8388/I2S route.
- No flash was performed, no Tab5 sources changed, and no audio-input binaries were committed. A separate session-local ZIP contains 14×8s triple WAV excerpts, gain-matched listening versions, plots, 100s numerical analysis, and the deterministic host renderer source.

# vgmM5lib (CoreS3 integration trial)

The 19 chip core source files in `src/` are copied **byte-for-byte** from `tab5/firmware/src/` at Tab5 R67 main.
Supported: YM2151, YM2612, YM2203, YM2608, YM2610, YM3812, YM2413 dedicated R31, SN76489, AY8910/YM2149, SCC/SCC+, YM2612 DAC, YM2608/2610 ADPCM/rhythm, SegaPCM, MSM6258/6295, C140 and C352.
Current deployment: **CoreS3 only**. Tab5 is deliberately **not switched** in this PR; a separate future change can point Tab5 at `vgmM5lib` after regression tests.

## Portability boundary
Only sound-core `.c/.h` files live here. Tab5's `vgm_engine.cpp`, audio task, LCD/UI, P4-specific SPM selector, ESP32-P4 speaker DMA/ISR, and filesystem management remain outside the library.
Tab5's `pcm_engine.h` includes `esp_partition.h` (an ESP-IDF API shared with ESP32-S3) and conditionally `spi_flash_mmap.h`; `pcm_engine.c` includes `esp_heap_caps.h` but makes no ESP32-P4 SPM allocation.
This is an ESP32 / Arduino shared library, not a generic desktop library; ESP-IDF `esp_attr.h` is still used for IRAM annotations.

## Link / ABI
The CoreS3 app includes `vgmM5lib.h`. Its five legacy local headers forward only under `IS_CORES3` to **the same library headers**; CoreS3's previous sound `.c` files are excluded with `build_src_filter`, preventing duplicate globals and incompatible struct layouts. AtomS3 and Cardputer continue building their original sources.
YM2413 requires the independent R31 register-to-core adapter in the CoreS3 VGM command path, implemented separately from platform audio output.
The CoreS3 output codec and task/scheduling remain separate from synthesis, including ES8388 and existing R67 buffer/UI fixes.

Neither Tab5 nor CoreS3 stable main are changed or flashed. Verification must include real ESP32-S3 PlatformIO native build and host sound tests.
